#include "AlertListModel.h"

AlertListModel::AlertListModel(QObject *parent)
    : QAbstractListModel(parent)                                               // - 생성자: 부모 객체 지정 및 모델 초기화
{
}

int AlertListModel::rowForCameraId(const QString &cameraId) const
{
    for (int i = 0; i < m_entries.size(); ++i) {                               // - 목록 순회: 등록된 카메라 항목 검색
        if (m_entries.at(i).cameraId == cameraId)                              // - ID 일치 확인: 일치하는 카메라 항목 행 번호 반환
            return i;
    }
    return -1;                                                                 // - 검색 실패: 미존재 시 -1 반환
}

void AlertListModel::upsert(const QString &cameraId, const QString &name, const QString &zone,
                             RiskTypes::RiskLevel level, double distanceM, bool distanceValid,
                             RiskTypes::ExceptionState exception)
{
    const bool shouldBeVisible = level != RiskTypes::RiskLevel::Safe || exception != RiskTypes::ExceptionState::None; // - 표출 여부 판단: 안전 단계가 아니거나 예외 발생 시 표시
    const int row = rowForCameraId(cameraId);                                  // - 행 위치 조회: 해당 카메라 항목의 현재 목록 위치 확인

    if (!shouldBeVisible) {                                                    // - 제거 조건: 안전 상태 전환 시 목록에서 삭제
        if (row >= 0) {                                                        // - 존재 확인: 기존 목록에 있던 항목 삭제 처리
            beginRemoveRows(QModelIndex(), row, row);                          // - 행 제거 시작: QML 알림용 제거 작업 개시
            m_entries.removeAt(row);                                           // - 항목 삭제: 목록 데이터에서 제거
            endRemoveRows();                                                   // - 행 제거 완료: QML 알림용 제거 작업 종료
        }
        return;
    }

    if (row >= 0) {                                                            // - 갱신 조건: 이미 목록에 존재하는 카메라 데이터 업데이트
        Entry &entry = m_entries[row];                                         // - 참조 획득: 해당 행 항목 참조
        entry.riskLevel = level;                                               // - 위험 단계 갱신: 새 위험 수준 설정
        entry.distanceM = distanceM;                                           // - 측정 거리 갱신: 새 거리값 설정
        entry.distanceValid = distanceValid;                                   // - 거리 유효성 갱신: 새 유효성 설정
        entry.exceptionState = exception;                                      // - 예외 상태 갱신: 새 예외 상태 설정
        const QModelIndex idx = index(row);                                    // - 모델 인덱스 생성: 갱신 위치 인덱스 획득
        emit dataChanged(idx, idx, {RiskLevelRole, DistanceRole, DistanceValidRole, ExceptionStateRole}); // - 변경 신호 발생: QML 화면 데이터 갱신 알림
        return;
    }

    Entry entry;                                                               // - 항목 생성: 신규 경보 항목 객체 생성
    entry.cameraId = cameraId;                                                 // - ID 설정: 카메라 ID 보관
    entry.name = name;                                                         // - 이름 설정: 카메라 명칭 보관
    entry.zone = zone;                                                         // - 구역 설정: 설치 위치 보관
    entry.riskLevel = level;                                                   // - 위험 단계 설정: 위험 수준 보관
    entry.distanceM = distanceM;                                               // - 측정 거리 설정: 거리값 보관
    entry.distanceValid = distanceValid;                                       // - 거리 유효성 설정: 유효성 보관
    entry.exceptionState = exception;                                          // - 예외 상태 설정: 예외 상태 보관

    beginInsertRows(QModelIndex(), 0, 0);                                      // - 행 추가 시작: 목록 최상단 신규 행 추가 알림
    m_entries.prepend(entry);                                                  // - 최상단 추가: 신규 경보 항목을 목록 맨 앞에 삽입
    endInsertRows();                                                           // - 행 추가 완료: QML 알림용 추가 작업 종료
}

int AlertListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())                                                      // - 유효성 검증: 부모 인덱스가 유효한 경우 0 반환
        return 0;
    return m_entries.size();                                                   // - 행 수 반환: 등록된 전체 항목 수 반환
}

QVariant AlertListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) // - 범주 검증: 유효하지 않은 인덱스 요청 시 빈 값 반환
        return {};

    const Entry &entry = m_entries.at(index.row());                            // - 항목 참조: 요청 인덱스의 데이터 획득
    switch (role) {
    case CameraIdRole: return entry.cameraId;                                  // - ID 반환: 카메라 ID 전달
    case NameRole: return entry.name;                                          // - 이름 반환: 카메라 명칭 전달
    case ZoneRole: return entry.zone;                                          // - 구역 반환: 설치 위치 전달
    case RiskLevelRole: return QVariant::fromValue(entry.riskLevel);            // - 위험 단계 반환: 위험 수준 전달
    case DistanceRole: return entry.distanceM;                                 // - 거리 반환: 측정 거리 전달
    case DistanceValidRole: return entry.distanceValid;                        // - 거리 유효성 반환: 유효성 전달
    case ExceptionStateRole: return QVariant::fromValue(entry.exceptionState); // - 예외 상태 반환: 예외 상태 전달
    default: return {};                                                        // - 기본값 반환: 정의되지 않은 역할 요청 시 빈 값 반환
    }
}

QHash<int, QByteArray> AlertListModel::roleNames() const
{
    return {
        {CameraIdRole, "cameraId"},                                           // - 역할 매핑: QML cameraId 속성 연결
        {NameRole, "name"},                                                   // - 역할 매핑: QML name 속성 연결
        {ZoneRole, "zone"},                                                   // - 역할 매핑: QML zone 속성 연결
        {RiskLevelRole, "riskLevel"},                                         // - 역할 매핑: QML riskLevel 속성 연결
        {DistanceRole, "distanceM"},                                          // - 역할 매핑: QML distanceM 속성 연결
        {DistanceValidRole, "distanceValid"},                                 // - 역할 매핑: QML distanceValid 속성 연결
        {ExceptionStateRole, "exceptionState"},                               // - 역할 매핑: QML exceptionState 속성 연결
    };
}
