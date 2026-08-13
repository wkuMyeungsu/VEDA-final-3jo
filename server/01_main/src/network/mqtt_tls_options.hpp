#pragma once
#include <string>

// mosquitto_tls_set() 호출에 필요한 파라미터 묶음.
//
// ResultPublisher / SensorUplinkReceiver 둘 다 같은 브로커에 mTLS로 붙으므로 옵션
// 구조체를 공유한다. enabled=false(기본)면 두 클래스 모두 인증서 경로를 건드리지 않고
// 기존 평문 연결 그대로 동작한다 - 설정 플래그 방식(기본값 false)으로 팀 확정
// (2026-08-13, CA/서버/클라이언트 인증서는 2026-08-11 CLI 레벨에서 검증 완료).

namespace risk_transport {

struct MqttTlsOptions {
    bool enabled = false;
    std::string ca_cert_path;      // CA 인증서 (브로커 인증서 검증용)
    std::string client_cert_path;  // 이 프로세스의 클라이언트 인증서 (mTLS)
    std::string client_key_path;   // 위 인증서에 대응하는 개인키
};

} // namespace risk_transport
