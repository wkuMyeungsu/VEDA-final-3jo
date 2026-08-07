#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "../models/CameraInfo.h"
#include "ConfigTypes.h"

// - 설정 로더 클래스 (JSON 설정 파일 읽기 및 카메라/관제/단말 설정 데이터 파싱)
class ConfigLoader
{
public:
    explicit ConfigLoader(QString configDir);                                 // - 생성자: 설정 파일 디렉토리 경로 지정

    QVector<CameraInfo> loadCameras() const;                                  // - 카메라 설정 로드: cameras.json 읽기 및 정보 목록 반환
    ControlCenterConfig loadControlCenterConfig() const;                       // - 관제 센터 설정 로드: control_center.json 읽기 및 설정 반환
    TerminalConfig loadTerminalConfig() const;                                 // - 단말 설정 로드: terminal.json 읽기 및 설정 반환

    static QVector<CameraInfo> parseCamerasJson(const QByteArray &jsonData);  // - 카메라 JSON 파싱: JSON 바이너리 데이터를 카메라 정보 목록으로 변환

    QString configDir() const { return m_configDir; }                         // - 설정 경로 조회: 설정 파일 저장 디렉토리 경로 반환

private:
    QByteArray readFile(const QString &fileName) const;                       // - 파일 읽기: 지정된 파일 데이터 읽기

    QString m_configDir;                                                       // - 설정 디렉토리: 설정 파일이 위치한 경로 보관
};
