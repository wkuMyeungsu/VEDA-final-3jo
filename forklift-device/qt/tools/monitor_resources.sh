#!/usr/bin/env bash
# ==============================================================================
# monitor_resources.sh
#
# 운전자 단말(operator_terminal)의 CPU 및 메모리(RSS) 사용량을 1초 주기로 CSV에 기록하는 샘플러 스크립트.
# 라즈베리파이 / Linux 임베디드 환경용.
#
# 사용법:
#   ./monitor_resources.sh [프로세스명] [출력_CSV_경로] [샘플링_주기_초]
# 예시:
#   ./monitor_resources.sh operator_terminal soak_test_pi.csv 1
# ==============================================================================

PROCESS_NAME="${1:-operator_terminal}"
OUTPUT_FILE="${2:-resource_usage_operator_terminal.csv}"
INTERVAL="${3:-1}"

# CSV 헤더 생성
if [ ! -f "$OUTPUT_FILE" ]; then
    echo "timestamp,elapsed_sec,cpu_percent,rss_kb" > "$OUTPUT_FILE"
    echo "[monitor_resources.sh] Created new CSV log file: $OUTPUT_FILE"
else
    echo "[monitor_resources.sh] Appending to existing CSV log file: $OUTPUT_FILE"
fi

echo "[monitor_resources.sh] Waiting for process '$PROCESS_NAME' to start..."

# 프로세스 대기 루프
TARGET_PID=""
while [ -z "$TARGET_PID" ]; do
    TARGET_PID=$(pgrep -x "$PROCESS_NAME" | head -n 1)
    if [ -z "$TARGET_PID" ]; then
        # pgrep 실패 시 pidof 시도
        TARGET_PID=$(pidof "$PROCESS_NAME" | awk '{print $1}')
    fi
    if [ -z "$TARGET_PID" ]; then
        sleep 1
    fi
done

echo "[monitor_resources.sh] Attached to process '$PROCESS_NAME' (PID: $TARGET_PID). Monitoring started (interval: ${INTERVAL}s)..."

START_TIME=$(date +%s)

cleanup() {
    EXIT_TIME=$(date -u +"%Y-%m-%dT%H:%M:%S.000Z")
    NOW_TIME=$(date +%s)
    FINAL_ELAPSED=$((NOW_TIME - START_TIME))
    echo "${EXIT_TIME},${FINAL_ELAPSED},PROCESS_TERMINATED,0" >> "$OUTPUT_FILE"
    echo "[monitor_resources.sh] Monitoring ended at $EXIT_TIME (Elapsed: ${FINAL_ELAPSED}s)."
    exit 0
}

trap cleanup INT TERM

while kill -0 "$TARGET_PID" 2>/dev/null; do
    sleep "$INTERVAL"
    
    if ! kill -0 "$TARGET_PID" 2>/dev/null; then
        break
    fi

    # ps 명령어로 CPU 및 RSS(KB) 수집
    # %cpu는 전체 누적 평균이 아닌 top/ps 형식의 순간 CPU 사용률
    STATS=$(ps -p "$TARGET_PID" -o %cpu,rss --no-headers 2>/dev/null)
    if [ -z "$STATS" ]; then
        break
    fi

    CPU=$(echo "$STATS" | awk '{print $1}')
    RSS=$(echo "$STATS" | awk '{print $2}')

    ISO_TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%S.000Z")
    NOW_TIME=$(date +%s)
    ELAPSED=$((NOW_TIME - START_TIME))

    echo "${ISO_TIMESTAMP},${ELAPSED},${CPU},${RSS}" >> "$OUTPUT_FILE"
done

cleanup
