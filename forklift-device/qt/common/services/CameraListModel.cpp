#include "CameraListModel.h"

CameraListModel::CameraListModel(QObject *parent)
    : QAbstractListModel(parent)                                               // - 생성자: 부모 객체 지정 및 모델 초기화
{
}

void CameraListModel::setCameras(const QVector<CameraInfo> &cameras)
{
    beginResetModel();                                                         // - 모델 리셋 시작: QML 알림용 리셋 작업 개시
    m_rows.clear();                                                            // - 기존 목록 비우기: 이전 카메라 행 목록 초기화
    m_rows.reserve(cameras.size());                                            // - 메모리 확보: 카메라 개수만큼 미리 공간 할당
    for (const CameraInfo &info : cameras) {                                   // - 카메라 목록 순회: 개별 카메라 정보를 행 객체로 변환
        Row row;                                                               // - 행 객체 생성: 개별 카메라 정보 및 상태 데이터 보관용
        row.info = info;                                                       // - 정보 설정: 카메라 기본 설정값 저장
        m_rows.append(row);                                                    // - 행 추가: 카메라 목록 배열에 저장
    }
    endResetModel();                                                           // - 모델 리셋 완료: QML 알림용 리셋 작업 종료
}

int CameraListModel::rowForCameraId(const QString &cameraId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {                                  // - 목록 순회: 등록된 카메라 항목 검색
        const auto &info = m_rows.at(i).info;
        if (info.effectiveId() == cameraId || info.streamId == cameraId || info.cameraId == cameraId)
            return i;
    }
    return -1;                                                                 // - 검색 실패: 미존재 시 -1 반환
}

QString CameraListModel::cameraIdAt(int row) const
{
    if (row >= 0 && row < m_rows.size())
        return m_rows.at(row).info.effectiveId();
    return QString();
}

int CameraListModel::indexForCameraId(const QString &cameraId) const
{
    return rowForCameraId(cameraId);                                           // - 인덱스 조회: 카메라 ID 기준 행 인덱스 반환
}

int CameraListModel::videoConnectionStateFor(const QString &cameraId) const
{
    const int row = rowForCameraId(cameraId);
    if (row >= 0 && row < m_rows.size())
        return static_cast<int>(m_rows.at(row).videoConnectionState);
    return static_cast<int>(RiskTypes::ConnectionState::Disconnected);
}

int CameraListModel::riskLevelFor(const QString &cameraId) const
{
    const int row = rowForCameraId(cameraId);
    if (row >= 0 && row < m_rows.size())
        return static_cast<int>(m_rows.at(row).riskLevel);
    return static_cast<int>(RiskTypes::RiskLevel::Safe);
}

int CameraListModel::exceptionStateFor(const QString &cameraId) const
{
    const int row = rowForCameraId(cameraId);
    if (row >= 0 && row < m_rows.size())
        return static_cast<int>(m_rows.at(row).exceptionState);
    return static_cast<int>(RiskTypes::ExceptionState::None);
}

double CameraListModel::distanceMFor(const QString &cameraId) const
{
    const int row = rowForCameraId(cameraId);
    if (row >= 0 && row < m_rows.size())
        return m_rows.at(row).distanceM;
    return 0.0;
}

bool CameraListModel::distanceValidFor(const QString &cameraId) const
{
    const int row = rowForCameraId(cameraId);
    if (row >= 0 && row < m_rows.size())
        return m_rows.at(row).distanceValid;
    return false;
}

QString CameraListModel::nameFor(const QString &cameraId) const
{
    const int row = rowForCameraId(cameraId);
    if (row >= 0 && row < m_rows.size())
        return m_rows.at(row).info.name;
    return cameraId;
}

QString CameraListModel::zoneFor(const QString &cameraId) const
{
    const int row = rowForCameraId(cameraId);
    if (row >= 0 && row < m_rows.size())
        return m_rows.at(row).info.zone;
    return QString();
}

void CameraListModel::updateRisk(const QString &cameraId, RiskTypes::RiskLevel level,
                                  RiskTypes::ExceptionState exception, double distanceM, bool distanceValid)
{
    const int row = rowForCameraId(cameraId);                                  // - 행 위치 조회: 대상 카메라 항목의 목록 인덱스 검색
    if (row < 0)                                                               // - 무효 검증: 목록에 없는 카메라 ID인 경우 처리 생략
        return;

    Row &target = m_rows[row];                                                 // - 참조 획득: 대상 카메라 행 참조
    if (target.riskLevel == level && target.exceptionState == exception &&
        qFuzzyCompare(target.distanceM, distanceM) && target.distanceValid == distanceValid) {
        return;                                                                // - 변경사항 없음: 불필요한 시그널 방출 방지
    }

    target.riskLevel = level;                                                  // - 위험 단계 갱신: 새 위험 수준 저장
    target.exceptionState = exception;                                         // - 예외 상태 갱신: 새 예외 상태 저장
    target.distanceM = distanceM;                                              // - 측정 거리 갱신: 새 거리값 저장
    target.distanceValid = distanceValid;                                      // - 거리 유효성 갱신: 새 유효성 여부 저장

    const QModelIndex idx = index(row);                                       // - 모델 인덱스 생성: 갱신 위치 인덱스 획득
    emit dataChanged(idx, idx, {RiskLevelRole, ExceptionStateRole, DistanceRole, DistanceValidRole}); // - 변경 신호 발생: 변경된 속성에 대한 QML 갱신 알림
}

void CameraListModel::updateVideoConnectionState(const QString &cameraId, RiskTypes::ConnectionState state)
{
    const int row = rowForCameraId(cameraId);                                  // - 행 위치 조회: 대상 카메라 항목의 목록 인덱스 검색
    if (row < 0)                                                               // - 무효 검증: 목록에 없는 카메라 ID인 경우 처리 생략
        return;

    if (m_rows[row].videoConnectionState == state)
        return;

    m_rows[row].videoConnectionState = state;                                  // - 영상 상태 갱신: 새 연결 상태 저장
    const QModelIndex idx = index(row);                                       // - 모델 인덱스 생성: 갱신 위치 인덱스 획득
    emit dataChanged(idx, idx, {VideoConnectionStateRole});                    // - 변경 신호 발생: 영상 연결 상태에 대한 QML 갱신 알림
}

int CameraListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())                                                      // - 유효성 검증: 부모 인덱스가 유효한 경우 0 반환
        return 0;
    return m_rows.size();                                                      // - 행 수 반환: 전체 카메라 개수 반환
}

QVariant CameraListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())   // - 범주 검증: 유효하지 않은 인덱스 요청 시 빈 값 반환
        return {};

    const Row &row = m_rows.at(index.row());                                   // - 행 참조: 요청 인덱스의 데이터 획득
    switch (role) {
    case CameraIdRole: return row.info.effectiveId();                          // - ID 반환: effectiveId(스트림 ID) 전달
    case NameRole: return row.info.name;                                       // - 이름 반환: 카메라 명칭 전달
    case ZoneRole: return row.info.zone;                                       // - 구역 반환: 설치 위치 전달
    case RiskLevelRole: return QVariant::fromValue(row.riskLevel);             // - 위험 단계 반환: 위험 수준 전달
    case ExceptionStateRole: return QVariant::fromValue(row.exceptionState);   // - 예외 상태 반환: 예외 상태 전달
    case DistanceRole: return row.distanceM;                                  // - 거리 반환: 측정 거리 전달
    case DistanceValidRole: return row.distanceValid;                         // - 거리 유효성 반환: 유효성 전달
    case VideoConnectionStateRole: return QVariant::fromValue(row.videoConnectionState); // - 영상 상태 반환: 영상 연결 상태 전달
    case StreamIdRole: return row.info.effectiveId();                          // - 스트림 ID 반환: 채널별 스트림 식별자 전달
    case ChannelRole: return row.info.channel;                                 // - 채널 번호 반환: 채널 인덱스 전달
    default: return {};                                                        // - 기본값 반환: 정의되지 않은 역할 요청 시 빈 값 반환
    }
}

QHash<int, QByteArray> CameraListModel::roleNames() const
{
    return {
        {CameraIdRole, "cameraId"},                                           // - 역할 매핑: QML cameraId 속성 연결
        {NameRole, "name"},                                                   // - 역할 매핑: QML name 속성 연결
        {ZoneRole, "zone"},                                                   // - 역할 매핑: QML zone 속성 연결
        {RiskLevelRole, "riskLevel"},                                         // - 역할 매핑: QML riskLevel 속성 연결
        {ExceptionStateRole, "exceptionState"},                               // - 역할 매핑: QML exceptionState 속성 연결
        {DistanceRole, "distanceM"},                                          // - 역할 매핑: QML distanceM 속성 연결
        {DistanceValidRole, "distanceValid"},                                 // - 역할 매핑: QML distanceValid 속성 연결
        {VideoConnectionStateRole, "videoConnectionState"},                   // - 역할 매핑: QML videoConnectionState 속성 연결
        {StreamIdRole, "streamId"},                                           // - 역할 매핑: QML streamId 속성 연결
        {ChannelRole, "channel"},                                             // - 역할 매핑: QML channel 속성 연결
    };
}
