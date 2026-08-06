// latency_stamps.h
// 서버 내부 판정 파이프라인 지연 계측 - 공개 인터페이스 (헤더 온리)
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 계측 지점 3곳 (서버 내부에서만 측정한다 - 단말/Qt 구간은 이번 범위 밖):
//   t0_ingest   : 센서/메타데이터가 서버에 들어온 시각
//                 -> JudgmentPipeline::processFrame() 진입 시점 (judgment_pipeline.cpp)
//   t1_judge_in : 판정 연산 시작 시각
//                 -> DangerJudgmentEngine::evaluate() 호출 직전 (judgment_pipeline.cpp)
//   t2_send     : 실제 전송 직전 시각
//                 -> ResultDispatcher::submit()에서 sink_() 호출 직전 (ResultDispatcher.h)
//
// [왜 steady_clock인가]
// 세 지점 모두 같은 서버 프로세스 안에서 일어나는 상대 구간이라 단조 증가만 보장되면
// 충분하고, system_clock처럼 NTP 보정에 뒤로 흐를 수 있는 시계는 구간 계산(뺄셈)에
// 오차를 만든다. 이 값을 단말(Qt)의 시각과 비교하는 데는 절대 쓰지 않는다 - 서로 다른
// 기기의 steady_clock은 기준점(epoch)도 속도도 다르게 취급될 수 있어 기기 간 비교 자체가
// 성립하지 않는다. 단말 구간 지연이 필요해지면 utc_time 같은 wall-clock 필드로
// 별도 프로토콜을 정의해야 한다(이번 작업 범위 밖).
//
// [ON/OFF 방식 선택: 컴파일타임 매크로]
// t0/t1/t2 스탬프 자체는 steady_clock::now() 호출 + 대입뿐이라 프레임당 비용이
// 무시할 수준이다(수십 ns) - 판정 루프(100ms 목표) 쪽에 런타임 분기를 넣을 이유가 없어
// JudgmentPipeline/ResultDispatcher는 항상 스탬프를 찍는다.
// 실제로 끄고 싶은 대상은 "CSV 쓰기 스레드 + 파일 I/O"(LatencyLogger)이므로, 매크로는
// 그 지점 하나에만 건다(LatencyLogger.cpp 참고). 매크로를 쓴 이유는 런타임 플래그보다
// 더 확실하게 "꺼졌을 때 스레드/파일이 아예 생기지 않는다"를 보장하기 때문이다
// (런타임 if였다면 끄더라도 로거 객체 생성 비용/실수로 켜질 여지가 남는다).
#pragma once

#ifndef SERVER_LATENCY_INSTRUMENTATION
#define SERVER_LATENCY_INSTRUMENTATION 1
#endif

#include <chrono>
#include <cstdio>
#include <string>

struct LatencyStamps {
    using Clock = std::chrono::steady_clock;

    Clock::time_point t0_ingest{};    // 센서/메타데이터가 서버에 들어온 시각
    Clock::time_point t1_judge_in{};  // 판정 연산 시작 시각
    Clock::time_point t2_send{};      // 전송 직전 시각

    static double toMs(Clock::duration d) {
        return std::chrono::duration<double, std::milli>(d).count();
    }

    // t0 -> t1: 판정 연산 시작 전 대기(수신 이후 전처리/큐잉) 구간
    double queueWaitMs() const { return toMs(t1_judge_in - t0_ingest); }
    // t1 -> t2: 판정 연산(엔진 evaluate) + 결과 조립 구간
    double judgeMs() const { return toMs(t2_send - t1_judge_in); }
    // t0 -> t2: 서버 내부 총 지연 (수신 ~ 전송 직전)
    double serverTotalMs() const { return toMs(t2_send - t0_ingest); }

    static std::string csvHeader() {
        return "queue_wait_ms,judge_ms,server_total_ms";
    }

    std::string toCsvRow() const {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f",
                     queueWaitMs(), judgeMs(), serverTotalMs());
        return buf;
    }
};
