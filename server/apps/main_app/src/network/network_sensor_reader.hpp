#pragma once
#include <string>

#include "logic/judgment/judgment_pipeline.h"   // ISensorReader, SensorInput
#include "network/sensor_uplink_receiver.hpp"

// NetworkSensorReader - SensorUplinkReceiver(MQTT 캐시)를 ISensorReader로 감싸는 어댑터.
//
// sensor_collector_reader.cpp가 이미 예고해둔 교체 지점이다: 센서(forklift-device)와
// 판정 엔진(server)이 물리적으로 분리된 배포에서는 SensorCollectorReader처럼 프로세스
// 내부에서 직접 물릴 수 없고, 단말 -> 서버 네트워크 업링크로 받은 값을 감싸야 한다.
// 그 감싸는 역할이 이 클래스다.
//
// [단위/필드 변환] SensorCollectorReader::read()와 정확히 같은 변환을 쓴다
// (ToF mm -> m, IMU 3축 g -> magnitude). SensorUplinkReceiver 쪽 스냅샷은 원본 단위를
// 그대로 들고 있고 변환을 하지 않으므로(sensor_uplink_receiver.hpp 주석 참고), 그 책임이
// 여기로 온다.
//
// [fail-safe] 아직 한 번도 못 받았거나(getLatest()==false) stale이면(isStale()==true)
// imu_ok/tof_ok를 둘 다 false로 내려보낸다. DangerJudgmentEngine이 그걸 SENSOR_FAULT ->
// 최소 CAUTION 유지로 처리하는 fail-safe 정책이라, "값이 없다"를 "정상인데 안전하다"로
// 오해시키지 않기 위함이다 (SensorUplinkReceiver::isStale() 설계 결정과 같은 원칙).
class NetworkSensorReader : public ISensorReader {
public:
    // receiver: 호출부가 소유한다(이 어댑터보다 오래 살아야 함).
    // terminal_id: K개 단말 중 이 어댑터가 읽을 단말 식별자. 중앙 서버는 단말마다
    //              이 어댑터를 하나씩 생성해 센서 캐시를 섞지 않는다.
    NetworkSensorReader(risk_transport::SensorUplinkReceiver& receiver, std::string terminal_id,
                        int stale_timeout_ms = risk_transport::SensorUplinkReceiver::kDefaultStaleTimeoutMs);

    SensorInput read() override;

private:
    risk_transport::SensorUplinkReceiver* receiver_;   // non-owning
    std::string terminal_id_;
    int stale_timeout_ms_;
};
