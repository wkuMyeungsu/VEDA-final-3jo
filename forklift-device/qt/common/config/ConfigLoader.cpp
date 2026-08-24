#include "ConfigLoader.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcConfig, "safety.config")                                 // - 로깅 카테고리 정의: 설정 로더 로그 분류용 이름 지정
}

ConfigLoader::ConfigLoader(QString configDir)
    : m_configDir(std::move(configDir))                                       // - 생성자: 설정 파일 디렉토리 경로 저장
{
}

QByteArray ConfigLoader::readFile(const QString &fileName) const
{
    const QString path = QDir(m_configDir).filePath(fileName);                 // - 파일 경로 생성: 설정 디렉토리 내 대상 파일 경로 합치기
    QFile file(path);                                                         // - 파일 객체 생성: 지정된 경로의 파일 제어 객체 생성
    if (!file.open(QIODevice::ReadOnly)) {                                     // - 파일 열기 검증: 읽기 전용 모드로 열기 실패 시 예외 처리
        qCWarning(lcConfig) << "could not open config file" << path << "-" << file.errorString(); // - 경고 로그: 파일 열기 실패 원인 기록
        return {};                                                            // - 빈 값 반환: 파일 열기 실패 시 빈 데이터 반환
    }
    return file.readAll();                                                    // - 데이터 읽기: 파일의 전체 바이트 데이터 반환
}

QVector<CameraInfo> ConfigLoader::parseCamerasJson(const QByteArray &jsonData)
{
    QVector<CameraInfo> result;                                               // - 결과 목록 생성: 추출된 카메라 정보 저장용 배열
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);              // - JSON 해석: 수신 데이터를 JSON 문서로 변환
    const QJsonArray cameras = doc.object().value(QStringLiteral("cameras")).toArray(); // - 배열 추출: 'cameras' 키의 배열 데이터 가져오기

    for (const QJsonValue &value : cameras) {                                 // - 배열 순회: 각 카메라 정보 항목 반복 처리
        const QJsonObject obj = value.toObject();                             // - 객체 변환: JSON 항목을 객체 형태로 변환

        CameraInfo info;
        info.streamId = obj.value(QStringLiteral("stream_id")).toString();
        info.cameraId = obj.value(QStringLiteral("camera_id")).toString();
        info.channel = obj.value(QStringLiteral("channel")).toInt();
        info.name = obj.value(QStringLiteral("name")).toString();
        info.zone = obj.value(QStringLiteral("zone")).toString();
        info.sourceType = videoSourceTypeFromString(obj.value(QStringLiteral("source_type")).toString());
        info.rtspUrl = obj.value(QStringLiteral("rtsp_url")).toString();
        info.localFilePath = obj.value(QStringLiteral("local_file_path")).toString();
        info.rtspLatencyMs = obj.value(QStringLiteral("rtsp_latency_ms")).toInt(100);
        info.rtspProtocols = obj.value(QStringLiteral("rtsp_protocols")).toString(QStringLiteral("tcp"));
        info.rtspDecoder = obj.value(QStringLiteral("rtsp_decoder")).toString(QStringLiteral("avdec_h264"));

        if (info.cameraId.isEmpty() && !info.streamId.isEmpty()) {
            info.cameraId = info.streamId;
        } else if (info.streamId.isEmpty() && !info.cameraId.isEmpty()) {
            info.streamId = info.cameraId;
        }

        if (info.cameraId.isEmpty()) {
            qCWarning(lcConfig) << "skipping camera entry with an empty camera_id";
            continue;
        }
        result.append(info);                                                  // - 목록 추가: 유효한 카메라 정보 결과 배열에 저장
    }
    return result;                                                            // - 결과 반환: 카메라 정보 목록 배열 반환
}

QVector<CameraInfo> ConfigLoader::loadCameras() const
{
    return parseCamerasJson(readFile(QStringLiteral("cameras.json")));        // - 카메라 설정 파일 읽기: cameras.json 읽기 및 파싱 결과 반환
}

ControlCenterConfig ConfigLoader::loadControlCenterConfig() const
{
    ControlCenterConfig config;                                               // - 관제 설정 구조체 생성: 기본값 적용 객체 생성
    const QJsonObject obj = QJsonDocument::fromJson(readFile(QStringLiteral("control_center.json"))).object(); // - 파일 해석: control_center.json 읽기 및 JSON 변환
    if (obj.isEmpty())                                                        // - 유효성 검증: 파일이 없거나 비어있는 경우 기본값 반환
        return config;

    config.systemName = obj.value(QStringLiteral("system_name")).toString(config.systemName); // - 시스템 이름 읽기: 설정값 적용
    config.serverHost = obj.value(QStringLiteral("server_host")).toString(config.serverHost); // - 서버 주소 읽기: 설정값 적용
    config.serverPort = static_cast<quint16>(obj.value(QStringLiteral("server_port")).toInt(config.serverPort)); // - 서버 포트 읽기: 설정값 적용
    config.metadataSourceType =
        obj.value(QStringLiteral("metadata_source_type")).toString(config.metadataSourceType); // - 소스 유형 읽기: 설정값 적용
    config.eventLogMaxEntries = obj.value(QStringLiteral("event_log_max_entries")).toInt(config.eventLogMaxEntries); // - 최대 로그 수 읽기: 설정값 적용
    return config;                                                            // - 결과 반환: 완성된 관제 센터 설정 반환
}

TerminalConfig ConfigLoader::loadTerminalConfig() const
{
    TerminalConfig config;                                                    // - 단말 설정 구조체 생성: 기본값 적용 객체 생성
    const QJsonObject obj = QJsonDocument::fromJson(readFile(QStringLiteral("terminal.json"))).object(); // - 파일 해석: terminal.json 읽기 및 JSON 변환
    if (obj.isEmpty())                                                        // - 유효성 검증: 파일이 없거나 비어있는 경우 기본값 반환
        return config;

    config.defaultCameraId = obj.value(QStringLiteral("default_camera_id")).toString(); // - 기본 카메라 ID 읽기: 설정값 적용
    config.serverHost = obj.value(QStringLiteral("server_host")).toString(config.serverHost); // - 서버 주소 읽기: 설정값 적용
    config.handoverPort = static_cast<quint16>(obj.value(QStringLiteral("handover_port")).toInt(config.handoverPort)); // - 제어 포트 읽기: 설정값 적용
    config.mqttBrokerHost = obj.value(QStringLiteral("mqtt_broker_host")).toString(config.mqttBrokerHost); // - MQTT 브로커 주소 읽기: 설정값 적용
    config.mqttBrokerPort =
        static_cast<quint16>(obj.value(QStringLiteral("mqtt_broker_port")).toInt(config.mqttBrokerPort)); // - MQTT 브로커 포트 읽기: 설정값 적용
    config.metadataSourceType =
        obj.value(QStringLiteral("metadata_source_type")).toString(config.metadataSourceType); // - 소스 유형 읽기: 설정값 적용
    config.terminalId = obj.value(QStringLiteral("terminal_id")).toString();  // - 단말 ID 읽기: 설정값 적용
    config.fpgaSerialPort = obj.value(QStringLiteral("fpga_serial_port")).toString(config.fpgaSerialPort); // - FPGA UART 포트 읽기: 설정값 적용
    config.fpgaBaudRate = obj.value(QStringLiteral("fpga_baud_rate")).toInt(config.fpgaBaudRate); // - FPGA UART 보레이트 읽기: 설정값 적용
    config.warningDeviceType =
        obj.value(QStringLiteral("warning_device_type")).toString(config.warningDeviceType); // - 경고 장치 유형 읽기: 설정값 적용
    return config;                                                            // - 결과 반환: 완성된 단말 설정 반환
}
