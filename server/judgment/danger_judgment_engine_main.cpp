// danger_judgment_engine_main.cpp
// 위험 판정 엔진 실행 진입점 - 더미 데이터 테스트 시나리오 9종 + TCP 결과 송신
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 엔진 로직은 danger_judgment_engine.h / .cpp에 있고, 이 파일은 실행파일(danger_engine)의
// main()만 담당한다. 테스트(test_exception_trigger)는 엔진 헤더/구현만 링크하므로
// 이 파일의 main()과 충돌하지 않는다.
//
// 송신 구조 (2026-08-03 확정):
//   - ResultPublisher가 고정 포트에서 대기(listen)하고 단말이 접속해 온다 (단말 IP 가변).
//   - 판정 결과는 ResultDispatcher를 거쳐 "상태 변화 시 즉시 + 무변화 시 200ms 재전송"으로 나간다.
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
#include "ResultPublisher.h"   // 판정 결과 TCP 송신 (서버: listen/accept)
#include "ResultDispatcher.h"  // 전송 정책 (변화 시 즉시 + 200ms 하트비트)
#include "EventLogger.h"       // 상태 변화 이벤트 SQLite 로그
#include "LatencyLogger.h"     // 서버 내부 판정 지연 CSV 로그

// ============================================================
// 테스트 시나리오 (더미 데이터)
// ============================================================

int main() {
    DangerJudgmentEngine engine;

    // 판정 결과 TCP 송신기 (서버 측에서 대기, 단말이 접속해 온다).
    // 첫 인자는 접속할 상대 주소가 아니라 "대기할 로컬 주소"다. 단말이 이동하며 IP가
    // 바뀌므로 모든 인터페이스(0.0.0.0)에서 받는다.
    // [교체 지점] 포트는 임시 9000. 실제 배포 포트 확정 시 환경변수/설정파일에서 로드.
    risk_transport::ResultPublisher publisher("0.0.0.0", 9000);
    publisher.onStateChange([](risk_transport::LinkState s) {
        const char* name = s == risk_transport::LinkState::CONNECTED ? "CONNECTED"
                         : s == risk_transport::LinkState::LISTENING ? "LISTENING"
                                                                     : "DISCONNECTED";
        std::cerr << "[publisher] link state -> " << name << "\n";
    });
    publisher.start();

    // 상태 변화 이벤트 로그(SQLite). 실제 쓰기는 로거 내부 워커 스레드가 담당한다.
    // start()가 실패해도(디스크/권한 등) 아래 판정·송신은 그대로 진행한다 - 로그를 못 남기는
    // 것보다 위험 경보가 끊기는 쪽이 훨씬 나쁘다.
    // [교체 지점] DB 경로는 기본값(server/judgment/events.db, CWD 기준)이다. 배포 시
    //             절대 경로를 환경변수/설정파일에서 읽어 생성자에 넘기면 된다.
    risk_log::EventLogger event_logger;
    event_logger.start();

    // 서버 내부 판정 지연(t0_ingest~t2_send) CSV 로그. EventLogger와 같은 원칙으로
    // start() 실패가 판정·송신을 막지 않는다. 기본 경로는 server/judgment/latency.csv
    // (CWD 기준, EventLogger::kDefaultDbPath와 같은 규약).
    risk_log::LatencyLogger latency_logger;
    latency_logger.start();

    // 전송 정책: 상태가 바뀌면 즉시, 안 바뀌면 200ms마다 마지막 결과 재전송.
    // 재전송은 dispatcher 내부 스레드가 하므로 아래 판정 루프는 여기서 멈추지 않는다.
    risk_transport::ResultDispatcher dispatcher(
        [&publisher](const std::string& json) { publisher.publish(json); },
        std::chrono::milliseconds(200));

    // 이벤트 로그는 dispatcher가 "상태 변화"로 판단한 순간에만 걸린다.
    // 200ms 하트비트 재전송은 이 훅을 타지 않으므로 같은 상태가 중복 기록되지 않는다.
    dispatcher.onStateChangeEvent([&event_logger](const JudgmentResult& r, int prev_risk) {
        event_logger.log(r, prev_risk);
    });

    // 지연 로그도 이벤트 로그와 같은 지점(상태 변화 시에만)에서 걸린다 - ResultDispatcher.h의
    // latency_sink_ 주석 참고. t2_send는 dispatcher가 채워서 넘겨준다.
    dispatcher.onLatencyEvent([&latency_logger](const LatencyStamps& stamps) {
        latency_logger.log(stamps);
    });
    dispatcher.start();

    // 판정 루프 한 스텝: 평가 -> 콘솔 출력 -> 전송 정책에 제출.
    // (단말이 아직 안 붙었어도 publisher는 백그라운드에서 대기하며 논블로킹으로 동작)
    // 시나리오 이름(name)은 콘솔 로그에만 쓰고, JSON 본문에는 넣지 않는다(프로덕션 스키마에 없음).
    //
    // 실제 판정 루프의 프레임 간격을 흉내 내려고 스텝마다 100ms 쉰다. 이 간격이 있어야
    // 무변화 구간에서 하트비트가 실제로 도는 걸 데모에서 확인할 수 있다.
    // 이 데모는 JudgmentPipeline을 거치지 않고 더미 CameraInput/SensorInput을 직접 만들어
    // evaluate()를 호출한다. 그래서 "센서/메타데이터가 서버에 들어온 시각"(t0_ingest)에
    // 대응하는 지점은 이 람다가 이번 프레임 데이터(cam/sen)를 받은 시점이고,
    // "판정 연산 시작 시각"(t1_judge_in)은 evaluate() 호출 직전이다.
    // (JudgmentPipeline::processFrame()과 같은 두 지점을, 파이프라인을 안 쓰는 이 경로에서도
    //  그대로 재현한 것뿐이다.)
    auto step = [&](const std::string& name, const CameraInput& cam, const SensorInput& sen) {
        const auto t0 = LatencyStamps::Clock::now();
        const auto t1 = LatencyStamps::Clock::now();
        JudgmentResult r = engine.evaluate(cam, sen);
        r.latency.t0_ingest   = t0;
        r.latency.t1_judge_in = t1;

        printResult(name, r);
        dispatcher.submit(r);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
    // (판정 상태가 시나리오 3과 같은 DANGER/NONE이라 즉시 전송 대상이 아니다.
    //  거리 값만 달라진 경우이므로 다음 200ms 하트비트에 최신 값이 실려 나간다.)
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

    // 마지막 시나리오 이후 판정 입력이 멈춘 구간 - 하트비트만 도는 상태를 보여준다.
    // (단말 입장에서는 링크가 살아 있고 마지막 판정이 유효하다는 신호가 계속 온다.)
    std::cout << "\n--- 무변화 구간 1초 (200ms 하트비트만 전송) ---\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    std::cout << "\n=== 테스트 종료 ===\n";
    std::cout << "전송 요약: 상태변화 즉시전송 " << dispatcher.changeSendCount()
              << "건 / 하트비트 재전송 " << dispatcher.heartbeatSendCount() << "건\n";

    // 정리 순서: 먼저 결과 제출을 끊고(dispatcher), 그 다음 송신 큐를 flush할 시간을 준다.
    dispatcher.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    publisher.stop();  // 소멸자에서도 호출되지만 명시적으로 정리

    // dispatcher를 먼저 멈춘 뒤에 로거를 닫아야 한다(닫힌 로거에 이벤트가 들어가지 않도록).
    // stop()은 남은 큐를 끝까지 쓰고 나서 반환한다.
    event_logger.stop();
    std::cout << "이벤트 로그: " << event_logger.writtenCount() << "건 기록 -> "
              << event_logger.dbPath() << "\n";

    latency_logger.stop();
    std::cout << "지연 로그: " << latency_logger.writtenCount() << "건 기록 -> "
              << latency_logger.csvPath() << "\n";
    return 0;
}
