// test_judgment_pipeline.cpp
// JudgmentPipeline 배선 검증 테스트 (nearest_person_selector 결과 -> CameraInput -> evaluate)
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   판정 로직이 아니라 "배선"을 검증한다. 임계값·예외 우선순위 자체는
//   test_exception_trigger.cpp가 이미 보고 있으므로 여기서는 중복하지 않고,
//   glue가 값을 옳은 필드로 옮기는지와 범위 밖으로 남겨둔 동작이 유지되는지만 본다.
//
//   [테스트 0] StubSensorReader 경고가 프로세스 전체에서 1회만 찍히는지
//   [테스트 1] toCameraInput() 매핑 규칙 (found -> person_detected, camera_id 출처, zone 공백)
//   [테스트 2] camera_id_mismatch 판정 조건
//   [테스트 3] processFrame() end-to-end (스텁 센서 기준 위험도/예외/거리)
//   [테스트 4] 하류 JSON에 camera_id가 실제로 실리는지 (이 배선의 존재 이유)
//   [테스트 5] StubSensorReader가 돌려주는 값 계약 (드라이버로 교체되면 여기서 깨져야 함)
//   [테스트 6] 진짜 selectNearestPerson() 출력을 processFrame()에 흘려보내는 통합 확인
//
//   핸드오버(여러 camera_id 동시 중첩) 처리는 구현 범위 밖이므로 검증 대상도 아니다.
//   대신 [테스트 3]에서 "다른 카메라에서 온 사람도 드랍되지 않는다"를 고정해 둔다 -
//   나중에 worst-case 방식을 넣을 때 이 케이스의 기대값이 바뀌어야 정상이다.
//
// 거리 비교는 부동소수점이라 expectNear(허용오차 1e-6)로 본다.
// 판정 불가 상태의 sentinel(-1.0)은 엔진이 상수를 그대로 대입하므로 정확히 비교된다.
//
// 빌드: g++ -std=c++17 -I../tracking test_judgment_pipeline.cpp judgment_pipeline.cpp \
//           danger_judgment_engine.cpp ../tracking/nearest_person_selector.cpp \
//           -o test_judgment_pipeline -pthread
// 실행: ./test_judgment_pipeline   (종료코드 0=성공, 1=실패)

#include <cstddef>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "judgment_pipeline.h"

namespace {

int failures = 0;

// 케이스 이름에 한글이 섞이면 std::setw(바이트 수 기준)로는 열이 안 맞는다.
// UTF-8 멀티바이트 문자를 2열(터미널 전각폭)로 세어 표시폭 기준으로 패딩한다.
// (test_exception_trigger.cpp와 동일한 로직 — 두 테스트가 공유 헤더 없이 독립 실행되므로 중복 유지)
std::string padTo(const std::string& s, std::size_t width) {
    std::size_t cols = 0;
    for (std::size_t i = 0; i < s.size(); ) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch < 0x80)        { cols += 1; i += 1; }  // ASCII
        else if (ch < 0xE0)   { cols += 1; i += 2; }  // 2바이트 (라틴 확장 등)
        else if (ch < 0xF0)   { cols += 2; i += 3; }  // 3바이트 (한글/CJK)
        else                  { cols += 2; i += 4; }  // 4바이트 (이모지 등)
    }
    return cols >= width ? s + " " : s + std::string(width - cols, ' ');
}

// 실패 시에만 기대/실제를 붙여 출력한다.
void report(const std::string& name, bool ok,
            const std::string& expected, const std::string& actual) {
    if (!ok) ++failures;
    std::cout << (ok ? "  [OK]   " : "  [FAIL] ") << padTo(name, 52);
    if (ok) {
        std::cout << "\n";
    } else {
        std::cout << "\n           -> 기대: " << expected << " | 실제: " << actual << "\n";
    }
}

std::string boolStr(bool v) { return v ? "true" : "false"; }

void expectBool(const std::string& name, bool actual, bool expected) {
    report(name, actual == expected, boolStr(expected), boolStr(actual));
}

void expectStr(const std::string& name, const std::string& actual, const std::string& expected) {
    report(name, actual == expected, "\"" + expected + "\"", "\"" + actual + "\"");
}

void expectNear(const std::string& name, double actual, double expected, double tol = 1e-6) {
    const bool ok = std::fabs(actual - expected) <= tol;
    report(name, ok, std::to_string(expected), std::to_string(actual));
}

void expectRisk(const std::string& name, RiskLevel actual, RiskLevel expected) {
    report(name, actual == expected, toString(expected), toString(actual));
}

void expectExc(const std::string& name, ExceptionState actual, ExceptionState expected) {
    report(name, actual == expected, toString(expected), toString(actual));
}

void expectContains(const std::string& name, const std::string& haystack,
                    const std::string& needle) {
    const bool ok = haystack.find(needle) != std::string::npos;
    report(name, ok, "포함: " + needle, haystack);
}

// ── 테스트 픽스처 ────────────────────────────────────────────
// nearest_person_selector.cpp의 테스트 시나리오와 같은 좌표를 써서 값 추적이 쉽게 한다.

const int        kActiveCamera = 1;
const WorldPoint kForklift{5.0, 5.0};

// 지게차(5,5) 기준 거리별 사람 위치.
const WorldPoint kPersonDanger{5.5, 5.5};    // 약 0.71m -> danger_threshold_m(1.5) 이내
const WorldPoint kPersonCaution{7.0, 7.0};   // 약 2.83m -> caution_threshold_m(3.0) 이내
const WorldPoint kPersonSafe{10.0, 10.0};    // 약 7.07m -> SAFE

NearestPersonResult makeFound(int track_id, int camera_id, const WorldPoint& pos) {
    NearestPersonResult n;
    n.found      = true;
    n.track_id   = track_id;
    n.camera_id  = camera_id;
    n.position   = pos;
    // distance_m은 glue가 일부러 안 넘기는 값이라 일부러 엉뚱한 값을 넣어 둔다.
    // 엔진이 좌표로 다시 계산하므로 결과 distance_m이 이 값에 오염되면 안 된다.
    n.distance_m = 999.0;
    return n;
}

// 사람 없음 (selector가 후보를 전부 걸러낸 경우 = 기본 생성값)
NearestPersonResult makeNotFound() { return NearestPersonResult{}; }

std::size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0;
    for (std::size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

// ── 테스트 0: 스텁 경고 1회 출력 ─────────────────────────────
// [주의] 이 테스트는 반드시 다른 테스트보다 먼저 실행돼야 한다.
//        경고 억제 플래그가 함수 로컬 static이라 프로세스 전체에서 1회만 찍히므로,
//        앞선 테스트가 read()를 먼저 부르면 여기서는 아무것도 관측되지 않는다.
void testStubWarningPrintedOnce() {
    std::cout << "[테스트 0] StubSensorReader 경고 - 프로세스 전체 1회만 출력\n";

    // 경고가 테스트 출력에 섞이지 않도록 cerr를 잠시 가로챈다.
    std::ostringstream captured;
    std::streambuf* saved = std::cerr.rdbuf(captured.rdbuf());

    StubSensorReader first;
    first.read();
    first.read();          // 같은 인스턴스 재호출 -> 추가 출력 없어야 함
    StubSensorReader second;
    second.read();         // 다른 인스턴스 -> static이 인스턴스별이 아니므로 역시 없어야 함

    std::cerr.rdbuf(saved);

    const std::string log = captured.str();
    const std::size_t n = countOccurrences(log, "StubSensorReader");
    report("read() 3회 호출 시 경고 1회만 출력", n == 1, "1회", std::to_string(n) + "회");
    expectContains("경고 문구에 교체 안내 포함", log, "드라이버로 교체");
}

// ── 테스트 1: 매핑 규칙 ──────────────────────────────────────
void testCameraInputMapping() {
    std::cout << "\n[테스트 1] toCameraInput() 매핑 규칙\n";

    StubSensorReader sensors;
    JudgmentPipeline pipeline(kActiveCamera, sensors);

    std::cout << "  -- 사람 검출됨 --\n";
    {
        const CameraInput cam =
            pipeline.toCameraInput(kForklift, true, makeFound(2, kActiveCamera, kPersonDanger));

        expectBool("forklift_localized 통과",        cam.forklift_localized, true);
        expectNear("forklift.x 통과",                cam.forklift.x, kForklift.x);
        expectNear("forklift.y 통과",                cam.forklift.y, kForklift.y);
        expectBool("found=true -> person_detected",  cam.person_detected, true);
        expectNear("person.x <- nearest.position.x", cam.person.x, kPersonDanger.x);
        expectNear("person.y <- nearest.position.y", cam.person.y, kPersonDanger.y);
        expectStr ("camera_id <- 활성 camera_id",    cam.camera_id, "1");
        expectStr ("zone은 매핑 미확정이라 공백",     cam.zone, "");
    }

    std::cout << "  -- 사람 미검출 --\n";
    {
        const CameraInput cam = pipeline.toCameraInput(kForklift, true, makeNotFound());

        expectBool("found=false -> person_detected", cam.person_detected, false);
        expectNear("person 좌표는 기본값(0,0)",       cam.person.x, 0.0);
        expectNear("person 좌표는 기본값(0,0)",       cam.person.y, 0.0);
        // 사람이 안 보여도 "어느 카메라에서 안 보였는지"는 하류에 남아야 한다.
        expectStr ("사람 미검출에도 camera_id 유지",  cam.camera_id, "1");
    }

    std::cout << "  -- 마커 폐색 (지게차 좌표 없음) --\n";
    {
        const CameraInput cam =
            pipeline.toCameraInput(kForklift, false, makeFound(2, kActiveCamera, kPersonDanger));

        expectBool("forklift_localized=false 통과", cam.forklift_localized, false);
        // stale 좌표를 굳이 지우지 않는다(엔진이 이 경우 거리 계산을 건너뜀).
        expectNear("stale forklift 좌표 그대로",     cam.forklift.x, kForklift.x);
    }

    std::cout << "  -- 활성 camera_id 미확정(음수) --\n";
    {
        JudgmentPipeline unset(-1, sensors);
        const CameraInput cam =
            unset.toCameraInput(kForklift, true, makeFound(2, 1, kPersonDanger));

        // 미확정은 빈 문자열 -> 하류 toJsonOrNull()이 null로 직렬화한다.
        expectStr("음수 활성 id -> 빈 문자열", cam.camera_id, "");
    }

    std::cout << "  -- 다른 카메라에서 온 최근접 사람 --\n";
    {
        const CameraInput cam =
            pipeline.toCameraInput(kForklift, true, makeFound(3, 2, kPersonDanger));

        // 판정 대상 카메라는 이 파이프라인의 활성 카메라이므로 nearest.camera_id를 쓰지 않는다.
        expectStr("nearest.camera_id=2여도 활성 id 사용", cam.camera_id, "1");
    }
}

// ── 테스트 2: camera_id_mismatch 판정 ────────────────────────
void testCameraIdMismatch() {
    std::cout << "\n[테스트 2] camera_id_mismatch 판정 조건\n";

    StubSensorReader sensors;
    JudgmentPipeline pipeline(kActiveCamera, sensors);

    expectBool("같은 camera_id -> mismatch 아님",
               pipeline.isCameraIdMismatch(makeFound(2, kActiveCamera, kPersonDanger)), false);

    expectBool("다른 camera_id -> mismatch",
               pipeline.isCameraIdMismatch(makeFound(3, 2, kPersonDanger)), true);

    // found=false면 camera_id가 -1이지만 비교 대상 자체가 없으므로 mismatch가 아니다.
    expectBool("사람 미검출 -> mismatch 아님",
               pipeline.isCameraIdMismatch(makeNotFound()), false);

    // 활성 카메라가 미확정이면 비교 기준이 없다.
    JudgmentPipeline unset(-1, sensors);
    expectBool("활성 id 미확정 -> mismatch 판단 안 함",
               unset.isCameraIdMismatch(makeFound(2, 5, kPersonDanger)), false);
}

// ── 테스트 3: processFrame() end-to-end ─────────────────────
// 스텁 센서가 "정상 + ToF 5m(SAFE) + 가속도 0"을 주므로, 여기서 관측되는 위험도는
// 전부 카메라(최근접 사람 거리) 경로에서 나온 값이다.
void testProcessFrame() {
    std::cout << "\n[테스트 3] processFrame() end-to-end (스텁 센서 기준)\n";

    StubSensorReader sensors;
    JudgmentPipeline pipeline(kActiveCamera, sensors);

    std::cout << "  -- 거리별 위험도 --\n";
    {
        const PipelineOutput out =
            pipeline.processFrame(kForklift, true, makeFound(2, kActiveCamera, kPersonDanger));
        expectRisk("0.71m -> DANGER",    out.result.final_risk, RiskLevel::DANGER);
        expectExc ("예외 없음",           out.result.exception,  ExceptionState::NONE);
        // nearest.distance_m(999.0)이 아니라 엔진이 좌표로 재계산한 값이어야 한다.
        expectNear("거리는 엔진 재계산값", out.result.distance_m, std::sqrt(0.5));
        expectStr ("결과에 camera_id 전달", out.result.camera_id, "1");
        expectBool("mismatch 아님",        out.camera_id_mismatch, false);
    }
    {
        const PipelineOutput out =
            pipeline.processFrame(kForklift, true, makeFound(5, kActiveCamera, kPersonCaution));
        expectRisk("2.83m -> CAUTION", out.result.final_risk, RiskLevel::CAUTION);
        expectExc ("예외 없음",         out.result.exception,  ExceptionState::NONE);
    }
    {
        const PipelineOutput out =
            pipeline.processFrame(kForklift, true, makeFound(6, kActiveCamera, kPersonSafe));
        expectRisk("7.07m -> SAFE", out.result.final_risk, RiskLevel::SAFE);
        expectExc ("예외 없음",      out.result.exception,  ExceptionState::NONE);
    }

    std::cout << "  -- 사람 미검출 (selector가 후보를 전부 걸러낸 경우) --\n";
    {
        const PipelineOutput out = pipeline.processFrame(kForklift, true, makeNotFound());
        // ToF도 SAFE라 UNCONFIRMED_PROXIMITY 조건이 성립하지 않는다 -> 진짜 아무도 없는 정상 상황.
        expectRisk("사람 없음 -> SAFE",        out.result.final_risk, RiskLevel::SAFE);
        expectExc ("사람 없음 -> 예외 없음",    out.result.exception,  ExceptionState::NONE);
        expectNear("거리 판정 불가 -> -1",      out.result.distance_m, -1.0);
        expectStr ("camera_id는 그대로 유지",   out.result.camera_id, "1");
    }

    std::cout << "  -- 마커 폐색 --\n";
    {
        const PipelineOutput out =
            pipeline.processFrame(kForklift, false, makeFound(2, kActiveCamera, kPersonDanger));
        expectExc ("폐색 -> DEAD_RECKONING",  out.result.exception,  ExceptionState::DEAD_RECKONING);
        expectRisk("폐색 -> 최소 CAUTION",     out.result.final_risk, RiskLevel::CAUTION);
        expectNear("거리 판정 불가 -> -1",     out.result.distance_m, -1.0);
    }

    std::cout << "  -- 다른 카메라에서 온 사람 (핸드오버 미구현 상태의 동작 고정) --\n";
    {
        const PipelineOutput same =
            pipeline.processFrame(kForklift, true, makeFound(2, kActiveCamera, kPersonDanger));
        const PipelineOutput other =
            pipeline.processFrame(kForklift, true, makeFound(3, 2, kPersonDanger));

        // 사람을 드랍하는 쪽이 안전 측면에서 더 위험하므로 판정은 그대로 진행한다.
        expectBool("mismatch 플래그만 켜짐", other.camera_id_mismatch, true);
        expectRisk("판정은 드랍되지 않음",    other.result.final_risk, RiskLevel::DANGER);
        expectRisk("같은 카메라 케이스와 동일 위험도",
                   other.result.final_risk, same.result.final_risk);
        expectNear("거리도 동일하게 계산됨",
                   other.result.distance_m, same.result.distance_m);
        // [TODO] worst-case 핸드오버가 들어오면 이 블록의 기대값을 갱신해야 한다
        //        (카메라별 판정 후 더 위험한 쪽 채택 + camera_id는 채택된 쪽 값).
    }
}

// ── 테스트 4: 하류 JSON 연결 확인 ────────────────────────────
// 이 배선의 목적 자체가 "camera_id가 null로 나가는 문제"를 없애는 것이라
// 최종 직렬화까지 확인한다. utc_time은 매번 달라지므로 부분 문자열로 본다.
void testJsonCarriesCameraId() {
    std::cout << "\n[테스트 4] 하류 JSON에 camera_id 반영\n";

    StubSensorReader sensors;
    {
        JudgmentPipeline pipeline(kActiveCamera, sensors);
        const PipelineOutput out =
            pipeline.processFrame(kForklift, true, makeFound(2, kActiveCamera, kPersonDanger));
        const std::string json = toJson(out.result);

        expectContains("camera_id가 문자열로 실림", json, "\"camera_id\":\"1\"");
        expectContains("zone은 미확정이라 null",     json, "\"zone\":null");
        // 단말이 toInt()로 읽으므로 정수여야 한다. 따옴표가 붙으면 항상 0(Safe)이 된다.
        expectContains("risk_level이 정수 2(DANGER)", json, "\"risk_level\":2");
    }
    {
        // 활성 camera_id 미확정이면 예전처럼 null로 나가야 한다(빈 문자열이 그대로 나가면 안 됨).
        JudgmentPipeline unset(-1, sensors);
        const PipelineOutput out =
            unset.processFrame(kForklift, true, makeFound(2, 1, kPersonDanger));
        expectContains("활성 id 미확정 -> camera_id null",
                       toJson(out.result), "\"camera_id\":null");
    }
}

// ── 테스트 5: 스텁 센서 값 계약 ──────────────────────────────
// 스텁이 무슨 값을 주는지 고정해 둔다. 실제 드라이버로 교체되면 이 테스트가 깨지는데,
// 그때는 이 블록을 지우는 게 맞다(교체 완료 신호로 쓴다).
void testStubSensorContract() {
    std::cout << "\n[테스트 5] StubSensorReader 값 계약 (드라이버 교체 시 갱신 대상)\n";

    StubSensorReader sensors;
    const SensorInput sen = sensors.read();

    expectBool("imu_ok = true",             sen.imu_ok, true);
    expectBool("tof_ok = true",             sen.tof_ok, true);
    expectNear("tof_distance_m = 5.0 (원거리)", sen.tof_distance_m, 5.0);
    expectNear("imu_accel_g = 0.0",         sen.imu_accel_g, 0.0);
    expectBool("is_dead_reckoning = false", sen.is_dead_reckoning, false);
}

// ── 테스트 6: 상류 구현체와의 통합 ───────────────────────────
// 위 테스트들은 NearestPersonResult를 손으로 만들어 넣는다. 여기서는 진짜
// selectNearestPerson()을 호출해 그 출력을 그대로 processFrame()에 넘긴다.
// 미러링된 타입이 아니라 실제 구현체와 링크됐는지, 그리고 selector의 필드 의미
// (found / position / camera_id)가 glue의 기대와 어긋나지 않는지 함께 확인한다.
//
// 트랙 구성은 nearest_person_selector_main.cpp의 더미 시나리오와 같은 값을 쓴다.
void testSelectorIntegration() {
    std::cout << "\n[테스트 6] selectNearestPerson() -> processFrame() 통합\n";

    StubSensorReader sensors;
    JudgmentPipeline pipeline(kActiveCamera, sensors);

    std::cout << "  -- 3명 중 최근접 선택 (selector 시나리오 1) --\n";
    {
        const std::vector<Track> tracks = {
            {1, 1, {8.0, 8.0}, 0},   // 약 4.24m
            {2, 1, {5.5, 5.5}, 0},   // 약 0.71m <- 선택돼야 함
            {3, 2, {1.0, 1.0}, 0},   // 약 5.66m
        };
        const NearestPersonResult nearest = selectNearestPerson(kForklift, tracks);

        // selector 쪽 결과부터 확인 (여기가 틀리면 아래 판정은 볼 의미가 없다)
        expectBool("selector가 사람을 찾음",   nearest.found, true);
        report("선택된 track_id=2", nearest.track_id == 2, "2", std::to_string(nearest.track_id));

        const PipelineOutput out = pipeline.processFrame(kForklift, true, nearest);
        expectRisk("최근접 0.71m -> DANGER",   out.result.final_risk, RiskLevel::DANGER);
        expectNear("거리 일치",                out.result.distance_m, nearest.distance_m);
        expectStr ("camera_id 전달",            out.result.camera_id, "1");
        expectBool("같은 카메라 -> mismatch 아님", out.camera_id_mismatch, false);
    }

    std::cout << "  -- 미검출 트랙 제외 후 차순위 선택 (selector 시나리오 2) --\n";
    {
        const std::vector<Track> tracks = {
            {4, 1, {5.1, 5.1}, 2},   // 매우 가깝지만 미검출 -> 제외
            {5, 1, {7.0, 7.0}, 0},   // 약 2.83m <- 선택돼야 함
        };
        const NearestPersonResult nearest = selectNearestPerson(kForklift, tracks);

        report("선택된 track_id=5", nearest.track_id == 5, "5", std::to_string(nearest.track_id));
        // 제외된 트랙(0.14m)이 뽑혔다면 DANGER가 나온다 -> CAUTION이어야 정상
        const PipelineOutput out = pipeline.processFrame(kForklift, true, nearest);
        expectRisk("차순위 2.83m -> CAUTION", out.result.final_risk, RiskLevel::CAUTION);
    }

    std::cout << "  -- 전부 미검출 (selector 시나리오 4) --\n";
    {
        const std::vector<Track> tracks = {
            {6, 1, {5.2, 5.2}, 1},
            {7, 1, {6.0, 6.0}, 3},
        };
        const NearestPersonResult nearest = selectNearestPerson(kForklift, tracks);

        expectBool("selector가 사람을 못 찾음", nearest.found, false);
        const PipelineOutput out = pipeline.processFrame(kForklift, true, nearest);
        expectBool("person_detected=false로 전달",
                   pipeline.toCameraInput(kForklift, true, nearest).person_detected, false);
        expectRisk("사람 없음 -> SAFE",        out.result.final_risk, RiskLevel::SAFE);
        expectNear("거리 판정 불가 -> -1",     out.result.distance_m, -1.0);
    }

    std::cout << "  -- 다른 카메라 트랙만 남은 경우 --\n";
    {
        // camera_id=2 트랙만 유효 -> selector는 그걸 고르고, glue는 mismatch로 표시한다.
        const std::vector<Track> tracks = {
            {8, 1, {5.2, 5.2}, 1},   // 활성 카메라 트랙이지만 미검출 -> 제외
            {9, 2, {5.5, 5.5}, 0},   // 다른 카메라 트랙 -> 이게 선택됨
        };
        const NearestPersonResult nearest = selectNearestPerson(kForklift, tracks);

        report("선택된 camera_id=2", nearest.camera_id == 2, "2", std::to_string(nearest.camera_id));
        const PipelineOutput out = pipeline.processFrame(kForklift, true, nearest);
        expectBool("mismatch 플래그 켜짐",  out.camera_id_mismatch, true);
        expectRisk("판정은 드랍되지 않음",   out.result.final_risk, RiskLevel::DANGER);
    }
}

} // namespace

int main() {
    std::cout << "=== JudgmentPipeline 배선 테스트 ===\n\n";

    testStubWarningPrintedOnce();  // read()를 먼저 부르는 테스트보다 앞에 있어야 한다
    testCameraInputMapping();
    testCameraIdMismatch();
    testProcessFrame();
    testJsonCarriesCameraId();
    testStubSensorContract();
    testSelectorIntegration();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
