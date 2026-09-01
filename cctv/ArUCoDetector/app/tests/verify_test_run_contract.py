"""Verify the schema-v2 test-run status contract. 사용법: python verify_test_run_contract.py STATUS_JSON."""

import json
import sys


TIMING_FIELDS = (
    "input_copy_us",
    "queue_wait_us",
    "dispatch_scan_us",
    "frame_get_us",
    "worker_setup_us",
    "resize_us",
    "preprocess_us",
    "detect_us",
    "coordinate_restore_us",
    "send_us",
    "processing_total_us",
    "end_to_end_us",
    "thread_cpu_us",
    "batch_cycle_us",
    "process_cpu_us",
)
RUNTIME_FIELDS = (
    "worker_count",
    "configured_worker_count",
    "effective_worker_count",
    "allowed_cpu_count",
    "opencv_thread_count",
)
SCALES = {"full": 1.0, "half": 0.5, "quarter": 0.25}


def fail(message):
    raise ValueError(message)


def require(condition, message):
    if not condition:
        fail(message)


def validate_scale(scale):
    require(scale in SCALES, "unknown scale_id")


def validate_timing_series(timings, name, expected_count):
    series = timings.get(name)
    require(isinstance(series, dict), "missing timing series " + name)
    require(series.get("count") == expected_count,
            "timing count mismatch: " + name)
    for stat in ("min", "avg", "p50", "p95", "max"):
        value = series.get(stat)
        if expected_count == 0:
            require(value is None, "empty timing series must be null: " + name)
        else:
            require(isinstance(value, (int, float)) and value >= 0,
                    "invalid timing statistic: " + name)


def aggregate_key(item):
    return (item.get("worker_count"), item.get("scale_id"), item.get("channel"))


def main(path):
    with open(path, "r", encoding="utf-8") as source:
        status = json.load(source)

    require(status.get("status") in ("completed", "cancelled", "error"),
            "status must be terminal")
    run_id = status.get("run_id")
    require(isinstance(run_id, str) and run_id, "run_id is required")

    input_data = status.get("input")
    require(isinstance(input_data, dict), "input must be an object")
    worker_counts = input_data.get("worker_counts")
    require(worker_counts in ([1], [2], [1, 2]),
            "worker_counts must be [1], [2], or [1,2]")
    channels = status.get("channels")
    scales = status.get("scales")
    require(isinstance(channels, list) and channels and
            all(isinstance(value, int) and 1 <= value <= 4 for value in channels) and
            len(channels) == len(set(channels)),
            "channels must be a non-empty unique list in range 1..4")
    require(isinstance(scales, list) and scales, "scales must be non-empty")
    for scale in scales:
        validate_scale(scale)
    scale_ids = scales
    require(len(scale_ids) == len(set(scale_ids)), "duplicate scale_id")

    dictionary_name = input_data.get("dictionary_name")
    require(isinstance(dictionary_name, str) and dictionary_name,
            "dictionary_name is required")
    warmup = input_data.get("warmup_samples")
    measurement = input_data.get("measurement_samples")
    require(isinstance(warmup, int) and warmup >= 0 and
            isinstance(measurement, int) and measurement >= 1,
            "invalid warmup or measurement count")
    total_cycles = warmup + measurement
    expected_total = (len(worker_counts) * len(scale_ids) *
                      len(channels) * total_cycles)

    has_ground_truth = bool(input_data.get("has_ground_truth"))
    if has_ground_truth:
        require(isinstance(input_data.get("expected_ids"), list),
                "ground-truth expected_ids must be a list")

    aggregates = status.get("aggregates")
    require(isinstance(aggregates, list), "aggregates must be a list")
    expected_keys = {(worker, scale, channel)
                     for worker in worker_counts for scale in scale_ids for channel in channels}
    aggregate_by_key = {}
    for aggregate in aggregates:
        require(isinstance(aggregate, dict), "aggregate must be an object")
        key = aggregate_key(aggregate)
        require(key in expected_keys and key not in aggregate_by_key,
                "aggregate worker/scale/channel coverage mismatch")
        aggregate_by_key[key] = aggregate
        for name in RUNTIME_FIELDS:
            require(isinstance(aggregate.get(name), int) and aggregate[name] >= 1,
                    "aggregate missing runtime field " + name)
        require(aggregate["configured_worker_count"] == aggregate["worker_count"],
                "aggregate configured worker count mismatch")
        require(aggregate["opencv_thread_count"] == 1,
                "OpenCV thread count must be one")
        require(aggregate.get("dictionary_name") == dictionary_name,
                "aggregate dictionary mismatch")
        require(aggregate.get("scale_factor") == SCALES[aggregate["scale_id"]],
                "aggregate scale factor mismatch")
    if status["status"] == "completed":
        require(set(aggregate_by_key) == expected_keys,
                "completed run is missing an aggregate")

    progress = status.get("progress")
    require(isinstance(progress, dict), "progress must be an object")
    require(progress.get("total_samples") == expected_total,
            "total_samples does not include worker counts")
    completed = progress.get("completed_samples")
    require(isinstance(completed, int) and 0 <= completed <= expected_total,
            "invalid completed_samples")
    if status["status"] == "completed":
        require(completed == expected_total,
                "completed run does not contain all requested samples")

    require("samples" not in status and "samples_truncated" not in status,
            "status must not carry raw samples; use the CSV export")
    always_timed = {"input_copy_us", "queue_wait_us", "dispatch_scan_us",
                    "frame_get_us", "worker_setup_us", "batch_cycle_us", "process_cpu_us"}
    executed_total = 0
    for key, aggregate in aggregate_by_key.items():
        expected_requested = total_cycles
        if status["status"] == "completed":
            require(aggregate.get("requested") == expected_requested,
                    "aggregate requested count mismatch")
        else:
            require(isinstance(aggregate.get("requested"), int) and
                    0 <= aggregate["requested"] <= expected_requested,
                    "aggregate requested count is invalid")
        outcome_total = sum(aggregate.get(name, 0)
                            for name in ("success", "failure", "unscored", "skip", "error"))
        executed = aggregate.get("throughput_count")
        require(isinstance(executed, int) and 0 <= executed <= measurement,
                "invalid throughput_count")
        require(executed == sum(aggregate.get(name, 0)
                                for name in ("success", "failure", "unscored")),
                "throughput_count must equal executed outcomes")
        if status["status"] == "completed":
            require(aggregate.get("warmup") == warmup and
                    aggregate.get("measurement") == measurement and
                    outcome_total == measurement,
                    "completed aggregate phase or outcome count mismatch")
        if not has_ground_truth or aggregate.get("success", 0) + aggregate.get("failure", 0) == 0:
            require(aggregate.get("detection_rate") is None,
                    "detection_rate must be null without scored ground truth")
        else:
            rate = aggregate["success"] / float(aggregate["success"] + aggregate["failure"])
            require(abs(aggregate.get("detection_rate") - rate) <= 1e-12,
                    "detection_rate mismatch")
        timings = aggregate.get("timings")
        require(isinstance(timings, dict), "aggregate timings are required")
        for name in TIMING_FIELDS:
            expected_count = measurement if name in always_timed else executed
            if status["status"] != "completed":
                series = timings.get(name)
                require(isinstance(series, dict), "missing timing series " + name)
                expected_count = series.get("count")
            require(isinstance(expected_count, int) and expected_count >= 0,
                    "invalid timing count: " + name)
            validate_timing_series(timings, name, expected_count)
        executed_total += executed

    if status["status"] == "completed":
        require(executed_total > 0,
                "completed run must contain at least one executed measurement")


    print("PASS")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_test_run_contract.py STATUS.json")
    try:
        main(sys.argv[1])
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
        print("FAIL: " + str(error))
        raise SystemExit(1)
