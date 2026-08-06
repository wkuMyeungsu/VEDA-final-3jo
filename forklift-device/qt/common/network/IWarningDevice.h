#pragma once

#include <QObject>

#include "../models/Types.h"

// - 물리 경고 장치 제어 인터페이스 (부저/경고등 등 하드웨어 제어 추상 클래스)
class IWarningDevice : public QObject
{
    Q_OBJECT

public:
    explicit IWarningDevice(QObject *parent = nullptr) : QObject(parent) {}     // - 생성자: 부모 객체 지정 및 초기화
    ~IWarningDevice() override = default;                                       // - 소멸자: 기본 소멸자 지정

    virtual void setRiskLevel(RiskTypes::RiskLevel level) = 0;                  // - 위험 단계 설정: 위험 수치에 따른 경고 작동 순수 가상 함수
};
