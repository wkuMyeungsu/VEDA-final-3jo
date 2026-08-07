#include "EventLogModel.h"

EventLogModel::EventLogModel(int maxEntries, QObject *parent)
    : QAbstractListModel(parent)
    , m_maxEntries(maxEntries)                                                 // - 생성자: 최대 보관 개수 설정 및 모델 초기화
{
}

void EventLogModel::addEntry(const RiskMetadata &metadata)
{
    beginInsertRows(QModelIndex(), 0, 0);                                      // - 행 추가 시작: 최상단(0번 행) 신규 항목 추가 알림
    m_entries.prepend(metadata);                                               // - 최상단 추가: 최신 이벤트를 목록 맨 앞에 삽입
    endInsertRows();                                                           // - 행 추가 완료: QML 알림용 추가 작업 종료

    if (m_entries.size() > m_maxEntries) {                                     // - 개수 초과 검증: 설정된 최대 로그 개수 초과 여부 확인
        beginRemoveRows(QModelIndex(), m_entries.size() - 1, m_entries.size() - 1); // - 행 제거 시작: 최하단(가장 오래된) 행 제거 알림
        m_entries.removeLast();                                                // - 최하단 제거: 가장 오래된 로그 항목 삭제
        endRemoveRows();                                                       // - 행 제거 완료: QML 알림용 제거 작업 종료
    }
}

void EventLogModel::clear()
{
    beginResetModel();                                                         // - 모델 리셋 시작: QML 알림용 리셋 작업 개시
    m_entries.clear();                                                         // - 전체 초기화: 수집된 이벤트 로그 전체 삭제
    endResetModel();                                                           // - 모델 리셋 완료: QML 알림용 리셋 작업 종료
}

int EventLogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())                                                      // - 유효성 검증: 부모 인덱스가 유효한 경우 0 반환
        return 0;
    return m_entries.size();                                                   // - 행 수 반환: 현재 수집된 로그 개수 반환
}

QVariant EventLogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) // - 범주 검증: 유효하지 않은 인덱스 요청 시 빈 값 반환
        return {};

    const RiskMetadata &entry = m_entries.at(index.row());                    // - 항목 참조: 요청 인덱스의 로그 데이터 획득
    switch (role) {
    case UtcTimeRole: return entry.utcTime();                                  // - 시각 반환: 이벤트 발생 시각 전달
    case CameraIdRole: return entry.cameraId();                                // - ID 반환: 카메라 ID 전달
    case ZoneRole: return entry.zone();                                        // - 구역 반환: 설치 위치 전달
    case RiskLevelRole: return QVariant::fromValue(entry.riskLevel());          // - 위험 단계 반환: 위험 수준 전달
    case DistanceRole: return entry.distanceM();                               // - 거리 반환: 측정 거리 전달
    case DistanceValidRole: return entry.distanceValid();                      // - 거리 유효성 반환: 유효성 전달
    case ExceptionStateRole: return QVariant::fromValue(entry.exceptionState());// - 예외 상태 반환: 예외 상태 전달
    default: return {};                                                        // - 기본값 반환: 정의되지 않은 역할 요청 시 빈 값 반환
    }
}

QHash<int, QByteArray> EventLogModel::roleNames() const
{
    return {
        {UtcTimeRole, "utcTime"},                                             // - 역할 매핑: QML utcTime 속성 연결
        {CameraIdRole, "cameraId"},                                           // - 역할 매핑: QML cameraId 속성 연결
        {ZoneRole, "zone"},                                                   // - 역할 매핑: QML zone 속성 연결
        {RiskLevelRole, "riskLevel"},                                         // - 역할 매핑: QML riskLevel 속성 연결
        {DistanceRole, "distanceM"},                                          // - 역할 매핑: QML distanceM 속성 연결
        {DistanceValidRole, "distanceValid"},                                 // - 역할 매핑: QML distanceValid 속성 연결
        {ExceptionStateRole, "exceptionState"},                               // - 역할 매핑: QML exceptionState 속성 연결
    };
}
