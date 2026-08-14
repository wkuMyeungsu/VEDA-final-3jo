#pragma once

#include <QMetaType>
#include <QString>
#include <qqmlintegration.h>

// TtcExperiment::resultFor()가 QML로 돌려주는 값 타입.
// BBox.h와 같은 패턴(Q_GADGET + QML_VALUE_TYPE)이라 QML에서 별도 import 없이
// (Safety.Common만 import 돼 있으면) 필드에 바로 접근 가능함.
class TtcResult
{
    Q_GADGET
    QML_VALUE_TYPE(ttcResult)
    Q_PROPERTY(bool valid READ valid WRITE setValid)
    Q_PROPERTY(double ttcSeconds READ ttcSeconds WRITE setTtcSeconds)
    Q_PROPERTY(double confidence READ confidence WRITE setConfidence)
    Q_PROPERTY(QString reason READ reason WRITE setReason)

public:
    TtcResult() = default;

    // valid=false면 ttcSeconds/confidence는 참고용이 아니라 아예 안 믿어야 함 -- reason만 유효
    bool valid() const { return m_valid; }
    void setValid(bool valid) { m_valid = valid; }

    double ttcSeconds() const { return m_ttcSeconds; }
    void setTtcSeconds(double seconds) { m_ttcSeconds = seconds; }

    double confidence() const { return m_confidence; }
    void setConfidence(double confidence) { m_confidence = confidence; }

    // valid=false일 때 화면에 보여줄 짧은 무효 사유 (한글, TtcExperiment.cpp에서 채움)
    QString reason() const { return m_reason; }
    void setReason(const QString &reason) { m_reason = reason; }

private:
    bool m_valid = false;
    double m_ttcSeconds = 0.0;
    double m_confidence = 0.0;
    QString m_reason;
};

Q_DECLARE_METATYPE(TtcResult)
