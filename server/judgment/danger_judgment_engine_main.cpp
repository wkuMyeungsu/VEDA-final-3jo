// danger_judgment_engine_main.cpp
// 위험 판정 엔진 실행 진입점 - 더미 데이터 테스트 시나리오 9종 + TCP 결과 송신
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 엔진 로직은 danger_judgment_engine.h / .cpp에 있고, 이 파일은 실행파일(danger_engine)의
// main()만 담당한다. 테스트(test_exception_trigger)는 엔진 헤더/구현만 링크하므로
// 이 파일의 main()과 충돌하지 않는다.
//
// 빌드 (라즈베리파이 / Linux, POSIX 전용):
//   g++ -std=c++17 danger_judgment_engine.cpp danger_judgment_engine_main.cpp \
//       -o danger_engine -pthread
//   또는 CMake: cmake -S . -B build && cmake --build build
//
// 외부 라이브러리 의존성은 없으나, ResultPublisher가 POSIX 소켓 + std::thread를
// 사용하므로 -pthread 링크가 필요하고 Windows/MSVC 네이티브 빌드는 지원하지 않는다.

#include <chrono>
#include <iostream>
#include <string>
#include <thread>   // std::this_thread::sleep_for용

#include "danger_judgment_engine.h"
#include "ResultPublisher.h"  // 판정 결과를 TCP로 하류(단말/서버)에 송신

// ============================================================
// 테스트 시나리오 (더미 데이터)
// ============================================================

int main() {
    DangerJudgmentEngine engine;

    // 판정 결과 TCP 송신기.
    // [교체 지점] 호스트/포트는 현재 로컬 더미값. 실제 배포 시 단말/수신 서버 주소로 교체
    //             (예: 환경변수 또는 설정파일에서 로드).
    risk_transport::ResultPublisher publisher("127.0.0.1", 9000);
    publisher.onStateChange([](risk_transport::LinkState s) {
        const char* name = s == risk_transport::LinkState::CONNECTED    ? "CONNECTED"
                         : s == risk_transport::LinkState::CONNECTING   ? "CONNECTING"
                                                                        : "DISCONNECTED";
        std::cerr << "[publisher] link state -> " << name << "\n";
    });
    publisher.start();

    // 판정 루프 한 스텝: 평가 -> 콘솔 출력 -> JSON 직렬화 후 TCP 송신.
    // (수신 서버가 없어도 publisher는 백그라운드에서 재접속을 시도하며 논블로킹으로 동작)
    // 시나리오 이름(name)은 콘솔 로그에만 쓰고, JSON 본문에는 넣지 않는다(프로덕션 스키마에 없음).
    auto step = [&](const std::string& name, const CameraInput& cam, const SensorInput& sen) {
        JudgmentResult r = engine.evaluate(cam, sen);
        printResult(name, r);
        publisher.publish(toJson(r));
    };

    std::cout << "=== 위험 판정 엔진 - 더미 데이터 테스트 (v2) ===\n\n";

    // CameraInput 인자 순서:
    //   forklift_localized, person_detected, forklift, person, camera_id, zone
    // camera_id/zone은 아직 상류 배선이 없어 전 시나리오 공통으로 빈 문자열이다.

    // 시나리오 1: 정상 - 안전 거리
    step("1. 정상-안전",
         CameraInput{true, true, {0.0, 0.0}, {5.0, 5.0}, "", ""},   // 거리 약 7.07m
         SensorInput{true, true, 5.0, 0.1, false});

    // 시나리오 2: 정상 - 주의 거리
    step("2. 정상-주의",
         CameraInput{true, true, {0.0, 0.0}, {2.0, 1.5}, "", ""},   // 거리 약 2.50m
         SensorInput{true, true, 3.0, 0.1, false});

    // 시나리오 3: 정상 - 위험 거리
    step("3. 정상-위험",
         CameraInput{true, true, {0.0, 0.0}, {1.0, 1.0}, "", ""},   // 거리 약 1.41m
         SensorInput{true, true, 2.0, 0.1, false});

    // 시나리오 4: 카메라는 안전인데 ToF는 위험 -> worst-case로 위험 채택
    step("4. 카메라SAFE/ToF위험",
         CameraInput{true, true, {0.0, 0.0}, {10.0, 10.0}, "", ""}, // 카메라 거리 멀어서 SAFE
         SensorInput{true, true, 0.3, 0.1, false});          // ToF 근접 -> DANGER

    // 시나리오 5: 센서 고장 (ToF 응답 없음) -> 최소 CAUTION 유지
    step("5. ToF 고장",
         CameraInput{true, true, {0.0, 0.0}, {8.0, 8.0}, "", ""},   // 카메라 상 안전
         SensorInput{true, false, 0.0, 0.1, false});         // ToF 고장

    // 시나리오 6: 마커 폐색 -> dead-reckoning 모드, 최소 CAUTION 유지
    step("6. 마커폐색(DR)",
         CameraInput{false, true, {0.0, 0.0}, {0.0, 0.0}, "", ""},  // 지게차 좌표 없음(폐색)
         SensorInput{true, true, 4.0, 0.1, true});           // IMU 추정 모드

    // 시나리오 7: 급정지/충돌 의심 -> 카메라·ToF와 무관하게 무조건 DANGER
    step("7. 충돌의심(급가속도)",
         CameraInput{true, true, {0.0, 0.0}, {9.0, 9.0}, "", ""},   // 카메라 상 안전
         SensorInput{true, true, 5.0, 3.5, false});          // 급격한 가속도 변화

    // 시나리오 8 [신규]: 지게차 좌표는 정상인데 사람이 카메라에 안 잡힘 + ToF만 근접 경보
    //                    -> UNCONFIRMED_PROXIMITY, 최종 위험도는 ToF 기준으로 DANGER 유지
    step("8. 사람미검출+ToF근접",
         CameraInput{true, false, {0.0, 0.0}, {0.0, 0.0}, "", ""},  // person_detected=false
         SensorInput{true, true, 0.4, 0.1, false});          // ToF 근접(0.4m) -> DANGER

    // 시나리오 9 [신규]: 지게차 좌표 정상, 사람도 안 잡히고, ToF도 멀리(SAFE) -> 그냥 정상 SAFE
    //                    (진짜로 주변에 아무도 없는 정상 상황과 구분되어야 함)
    step("9. 사람미검출+ToF안전",
         CameraInput{true, false, {0.0, 0.0}, {0.0, 0.0}, "", ""},
         SensorInput{true, true, 5.0, 0.1, false});          // ToF도 멀리 -> SAFE

    std::cout << "\n=== 테스트 종료 ===\n";

    // 백그라운드 송신 스레드가 pending 결과를 flush할 시간을 잠깐 준 뒤 정리.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    publisher.stop();  // 소멸자에서도 호출되지만 명시적으로 정리
    return 0;
}
