"""Verify UTF-8 BOM, CRLF, RFC 4180, and fixed-column test CSV exports. 사용법: python verify_csv_exports.py EXPORT_DIRECTORY."""

import csv
import io
import json
import re
import sys
from pathlib import Path


HEADERS = {
    "samples": [
        "run_id", "sample_seq", "cycle_index", "worker_count", "configured_worker_count",
        "effective_worker_count", "allowed_cpu_count", "opencv_thread_count", "channel",
        "scale_id", "scale_factor", "dictionary_name", "phase", "input_mode",
        "width", "height", "step", "processed_width", "processed_height",
        "frame_generation", "coordinate_space", "expected_ids",
        "detected_ids", "outcome", "executed", "reason", "rejected_count", "input_copy_us",
        "queue_wait_us", "dispatch_scan_us", "frame_get_us", "worker_setup_us", "resize_us",
        "preprocess_us", "detect_us",
        "coordinate_restore_us", "send_us", "processing_total_us", "end_to_end_us",
        "thread_cpu_us", "batch_cycle_us", "process_cpu_us", "worker_busy_ratio",
    ],
}
RUNTIME_COLUMNS = (
    "worker_count", "configured_worker_count", "effective_worker_count",
    "allowed_cpu_count", "opencv_thread_count",
)
ARRAY_COLUMNS = {"expected_ids", "detected_ids"}
NUMBER_RE = re.compile(r"^-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?$")


def fail(message):
    raise ValueError(message)


def read_csv(path, expected_header):
    raw = path.read_bytes()
    if not raw.startswith(b"\xef\xbb\xbf"):
        fail(str(path) + ": UTF-8 BOM is missing")
    payload = raw[3:]
    if b"\n" in payload.replace(b"\r\n", b""):
        fail(str(path) + ": line endings are not CRLF")
    text = payload.decode("utf-8")
    if not text.endswith("\r\n"):
        fail(str(path) + ": CSV must end with CRLF")
    reader = csv.reader(io.StringIO(text, newline=""))
    rows = list(reader)
    if not rows or rows[0] != expected_header:
        fail(str(path) + ": header does not match the fixed contract")
    width = len(expected_header)
    for row_number, row in enumerate(rows[1:], 2):
        if len(row) != width:
            fail(str(path) + ": row " + str(row_number) + " has the wrong column count")
        values = dict(zip(expected_header, row))
        for name in RUNTIME_COLUMNS:
            if not values[name].isdigit() or int(values[name]) < 1:
                fail(str(path) + ": invalid runtime value in " + name)
        if int(values["opencv_thread_count"]) != 1:
            fail(str(path) + ": OpenCV thread count is not one")
        for name in ARRAY_COLUMNS.intersection(values):
            if values[name] == "":
                fail(str(path) + ": empty JSON array in " + name)
            parsed = json.loads(values[name])
            if not isinstance(parsed, list) or values[name] != json.dumps(parsed, separators=(",", ":")):
                fail(str(path) + ": array is not compact JSON in " + name)
        for name, value in values.items():
            if name in ("run_id", "scale_id", "dictionary_name", "phase",
                        "input_mode", "coordinate_space", "outcome", "reason"):
                continue
            if value and not NUMBER_RE.match(value) and name != "executed":
                fail(str(path) + ": non-locale numeric value in " + name)
        if "executed" in values and values["executed"] not in ("true", "false"):
            fail(str(path) + ": executed must be true or false")
        if values.get("executed") == "true":
            end_to_end = int(values["end_to_end_us"])
            expected = sum(int(values[name]) for name in (
                "input_copy_us", "queue_wait_us", "frame_get_us", "worker_setup_us",
                "processing_total_us",
            ))
            if end_to_end != expected:
                fail(str(path) + ": end_to_end_us does not match its non-overlapping stages")
    return rows


def main(directory):
    root = Path(directory)
    rows = {}
    for name, header in HEADERS.items():
        rows[name] = read_csv(root / (name + ".csv"), header)
    run_ids = set()
    for name, csv_rows in rows.items():
        for row in csv_rows[1:]:
            run_ids.add(row[0])
    if len(run_ids) > 1:
        fail("CSV files contain multiple run IDs")
    print("PASS")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_csv_exports.py EXPORT_DIRECTORY")
    try:
        main(sys.argv[1])
    except (OSError, ValueError, UnicodeError, csv.Error, json.JSONDecodeError) as error:
        print("FAIL: " + str(error))
        raise SystemExit(1)
