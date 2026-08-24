import importlib.util
import pathlib
import tempfile
import unittest
from unittest import mock


SERVER_PATH = pathlib.Path(__file__).resolve().parents[1] / "server.py"
STATIC_PATH = pathlib.Path(__file__).resolve().parents[1] / "static" / "index.html"
SPEC = importlib.util.spec_from_file_location("monitoring_server", SERVER_PATH)
SERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SERVER)


class MonitoringStatusTests(unittest.TestCase):
    def test_refresh_interval_has_one_named_source(self):
        page = STATIC_PATH.read_text(encoding="utf-8")

        self.assertIn(
            "const REFRESH_INTERVAL_SECONDS = Number(document.body.dataset.refreshIntervalSeconds);",
            page,
        )
        self.assertIn(
            'data-refresh-interval-seconds="__SERVER_MONITORING_REFRESH_INTERVAL_SECONDS__"',
            page,
        )
        self.assertIn(
            "const REFRESH_INTERVAL_MS = REFRESH_INTERVAL_SECONDS * MILLISECONDS_PER_SECOND;",
            page,
        )
        self.assertIn(
            "안전 서버와 단말별 상태를 한눈에 확인합니다. · ${REFRESH_INTERVAL_SECONDS}초마다 갱신",
            page,
        )
        self.assertIn("window.setInterval(refresh,REFRESH_INTERVAL_MS);", page)
        self.assertNotIn("setInterval(refresh,1000)", page)
        self.assertIn('<pre id="server-logs">확인 중</pre>', page)
        self.assertIn("recentLines.join('\\n')", page)
        self.assertIn("로그 불러온 시각:", page)
        self.assertIn("표시 범위:", page)
        self.assertIn('id="server-log-fetched-time"', page)
        self.assertIn("formatTimestamp(value.checked_utc)", page)
        for label in ("서버 운영 콘솔", "운영 요약", "단말 상태", "검출 현황", "시스템·로그", "안전 서버", "MQTT TLS", "서버 실행 시간", "운영 판단", "판정 처리", "위험 알림", "이벤트 저장", "시스템 세부 진단", "메타데이터 처리", "이벤트 DB", "센서 입력", "라즈베리파이 자원 사용량", "단말별 운영 상태", "최근 서버 로그", "사람 검출"):
            self.assertIn(label, page)
        for tab_id in ("tab-overview", "tab-terminals", "tab-detection", "tab-system"):
            self.assertIn(f'id="{tab_id}"', page)
        self.assertIn("activateTab", page)
        self.assertIn("aria-selected", page)
        self.assertIn(".status-value.good .status-dot", page)
        self.assertIn(".status-value.problem .status-dot", page)
        self.assertIn("dot.className=`status-dot ${kind}`", page)
        self.assertIn("node.replaceChildren(dot)", page)
        self.assertIn("손실", page)
        self.assertIn("RUNTIME_STATUS_MAX_AGE_SECONDS", page)
        self.assertIn("server_started_utc", page)
        self.assertIn("formatDuration", page)
        self.assertIn("renderServerUptime", page)
        self.assertIn('id="server-uptime-detail"', page)
        self.assertIn("terminal.sensor", page)
        self.assertIn("terminal.risk", page)
        self.assertIn("terminal.localization", page)
        self.assertIn("terminal.people", page)
        self.assertIn("renderPeopleSection", page)
        self.assertIn('id="people-detections"', page)
        self.assertIn('id="people-summary"', page)
        self.assertIn("MARKER_NOT_DETECTED:'마커 미검출'", page)
        self.assertIn("지게차 위치", page)
        self.assertIn("설정 ID는 이번 실행에서 검출되지 않음", page)
        self.assertIn("sensorBypassed?'센서 제외 테스트'", page)
        self.assertIn("events.state_changes", page)
        self.assertIn("입력 모드 확인 중", page)
        self.assertIn("resource-host-cpu", page)
        self.assertIn("resource-host-memory", page)
        self.assertIn("resource-host-temperature", page)
        self.assertIn("CPU 코어별 사용량", page)
        self.assertIn('id="resource-host-cores"', page)
        self.assertIn('id="resource-host-peak-core"', page)
        self.assertIn("renderCpuCores", page)
        self.assertIn("cpu_cores", page)
        self.assertIn("최고 코어:", page)
        self.assertIn("cpu-core-bar-value", page)
        self.assertIn("formatTemperature", page)
        self.assertIn("전체 프로세스 포함", page)
        self.assertIn("renderResources", page)
        self.assertIn("renderOperationalSummary", page)
        self.assertIn("decision-state", page)
        self.assertIn("risk-output-state", page)
        self.assertIn("event-storage-state", page)
        self.assertNotIn("센서 수신 누적</h3>", page)
        self.assertNotIn("연결 단말</h3>", page)
        self.assertNotIn("status-label", page)
        self.assertNotIn("active:'O'", page)
        self.assertNotIn("inactive:'X'", page)
        self.assertNotIn("peopleField", page)
        self.assertNotIn("호모그래피 앱 열기", page)
        self.assertNotIn("유지보수 도구", page)
        self.assertNotIn("id='homography'", page)

    def test_recent_logs_returns_only_the_requested_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "server.log"
            path.write_text(
                "\n".join(f"line-{index}" for index in range(5)) + "\n",
                encoding="utf-8",
            )

            self.assertEqual(
                SERVER.read_recent_logs(path, limit=3),
                ["line-2", "line-3", "line-4"],
            )

    def test_recent_logs_returns_empty_for_missing_file(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(
                SERVER.read_recent_logs(pathlib.Path(directory) / "missing.log"),
                [],
            )

    @mock.patch.dict(SERVER.os.environ, {"SERVER_MONITORING_REFRESH_INTERVAL_SECONDS": "2"})
    def test_index_uses_configured_refresh_interval(self):
        page = SERVER.render_index()

        self.assertIn('data-refresh-interval-seconds="2"', page)
        self.assertIn("${REFRESH_INTERVAL_SECONDS}초마다", page)

    def test_index_renders_per_core_cpu_and_peak_contract(self):
        page = STATIC_PATH.read_text(encoding="utf-8")

        self.assertIn('id="resource-host-cores"', page)
        self.assertIn('id="resource-host-peak-core"', page)
        self.assertIn("const renderCpuCores", page)
        self.assertIn("core.cpu_percent", page)
        self.assertIn("CPU ${core.core}", page)
        self.assertIn("최고 코어: CPU ${peak.core}", page)
        self.assertIn("최고 코어: 측정 중", page)
        self.assertIn("코어별 CPU 측정 불가", page)

    @mock.patch.dict(SERVER.os.environ, {"SERVER_MONITORING_REFRESH_INTERVAL_SECONDS": "invalid"})
    def test_invalid_refresh_interval_uses_safe_default(self):
        self.assertEqual(
            SERVER.refresh_interval_seconds(),
            SERVER.DEFAULT_REFRESH_INTERVAL_SECONDS,
        )

    def test_successful_requests_are_quiet(self):
        handler = object.__new__(SERVER.Handler)
        with mock.patch.object(SERVER.BaseHTTPRequestHandler, "log_message") as parent:
            handler.log_message('%s %s %s', 'GET /api/status', '200', '-')
        parent.assert_not_called()

    def test_http_errors_are_logged(self):
        handler = object.__new__(SERVER.Handler)
        with mock.patch.object(SERVER.BaseHTTPRequestHandler, "log_message") as parent:
            handler.log_message('%s %s %s', 'GET /missing', '404', '-')
        parent.assert_called_once_with('%s %s %s', 'GET /missing', '404', '-')

    def test_host_resource_reads_cpu_and_memory_from_procfs(self):
        with tempfile.TemporaryDirectory() as directory:
            proc_root = pathlib.Path(directory)
            (proc_root / "stat").write_text(
                "cpu 100 0 100 700 100 0 0 0\n"
                "cpu0 50 0 50 350 50 0 0 0\n"
                "cpu1 50 0 50 350 50 0 0 0\n",
                encoding="utf-8",
            )
            (proc_root / "meminfo").write_text(
                "MemTotal:       4096000 kB\nMemAvailable:   2048000 kB\n",
                encoding="utf-8",
            )
            thermal_root = pathlib.Path(directory) / "thermal"
            thermal_zone = thermal_root / "thermal_zone0"
            thermal_zone.mkdir(parents=True)
            (thermal_zone / "type").write_text("cpu-thermal\n", encoding="utf-8")
            (thermal_zone / "temp").write_text("45678\n", encoding="utf-8")
            with mock.patch.object(SERVER, "PROC_ROOT", proc_root), \
                 mock.patch.object(SERVER, "THERMAL_ROOT", thermal_root), \
                 mock.patch.object(SERVER, "_HOST_CPU_SAMPLE", None):
                first = SERVER.host_resource()
                self.assertIsNone(first["cpu_percent"])
                self.assertEqual(
                    first["cpu_cores"],
                    [{"core": 0, "cpu_percent": None}, {"core": 1, "cpu_percent": None}],
                )
                self.assertEqual(first["memory_used_mb"], 2000.0)
                self.assertEqual(first["memory_percent"], 50.0)
                self.assertEqual(first["temperature_c"], 45.7)
                self.assertEqual(first["temperature_source"], "cpu-thermal")

                (proc_root / "stat").write_text(
                    "cpu 150 0 150 750 100 0 0 0\n"
                    "cpu0 100 0 100 350 50 0 0 0\n"
                    "cpu1 50 0 50 400 50 0 0 0\n",
                    encoding="utf-8",
                )
                second = SERVER.host_resource()
                self.assertEqual(second["cpu_percent"], 66.7)
                self.assertEqual(
                    second["cpu_cores"],
                    [{"core": 0, "cpu_percent": 100.0}, {"core": 1, "cpu_percent": 0.0}],
                )
                self.assertEqual(second["memory_total_mb"], 4000.0)

                (proc_root / "stat").write_text(
                    "cpu 200 0 200 800 100 0 0 0\n"
                    "cpu0 125 0 125 375 50 0 0 0\n"
                    "cpu1 75 0 75 425 50 0 0 0\n"
                    "cpu2 0 0 0 0 0 0 0 0\n",
                    encoding="utf-8",
                )
                changed_cores = SERVER.host_resource()
                self.assertIsNone(changed_cores["cpu_percent"])
                self.assertTrue(all(core["cpu_percent"] is None for core in changed_cores["cpu_cores"]))

    def test_host_resource_marks_procfs_failure_unavailable(self):
        with tempfile.TemporaryDirectory() as directory:
            proc_root = pathlib.Path(directory)
            (proc_root / "meminfo").write_text(
                "MemTotal:       4096000 kB\nMemAvailable:   2048000 kB\n",
                encoding="utf-8",
            )
            with mock.patch.object(SERVER, "PROC_ROOT", proc_root), \
                 mock.patch.object(SERVER, "_HOST_CPU_SAMPLE", None):
                value = SERVER.host_resource()
            self.assertEqual(value["state"], "unavailable")
            self.assertIsNone(value["cpu_percent"])
            self.assertEqual(value["cpu_cores"], [])

    def test_resource_snapshot_collects_whole_host(self):
        sample = {
            "state": "ok",
            "cpu_percent": 1.0,
            "cpu_cores": [{"core": 0, "cpu_percent": 1.0}],
            "memory_used_mb": 2000.0,
        }
        with mock.patch.object(SERVER, "host_resource", return_value=sample) as resource:
            value = SERVER.resource_snapshot()
        self.assertIn("checked_utc", value)
        self.assertEqual(value["host"], sample)
        self.assertEqual(value["host"]["cpu_cores"][0]["core"], 0)
        resource.assert_called_once()

    @mock.patch.object(SERVER, "file_timestamp", return_value="2026-08-21T00:00:00+00:00")
    @mock.patch.object(SERVER, "resource_snapshot", return_value={})
    @mock.patch.object(SERVER, "tcp_reachable", return_value=True)
    @mock.patch.object(SERVER, "service_state")
    @mock.patch.object(
        SERVER,
        "read_runtime_status",
        return_value={
            "state": "online",
            "checked_utc": SERVER.datetime.now(SERVER.timezone.utc).isoformat(),
        },
    )
    def test_ready_snapshot(self, _runtime, service_state, _tcp, _resources, _timestamp):
        service_state.return_value = "active"
        value = SERVER.status_snapshot()
        self.assertTrue(value["ok"])
        self.assertEqual(value["safety_server"]["state"], "active")
        self.assertNotIn("homography", value)
        self.assertTrue(value["runtime_status"]["fresh"])
        self.assertIn("resources", value)
        self.assertNotIn("homography", value)

    @mock.patch.object(SERVER, "file_timestamp", return_value=None)
    @mock.patch.object(SERVER, "resource_snapshot", return_value={})
    @mock.patch.object(SERVER, "tcp_reachable", return_value=False)
    @mock.patch.object(SERVER, "service_state", return_value="inactive")
    def test_not_ready_when_dependencies_are_down(self, _service, _tcp, _resources, _timestamp):
        value = SERVER.status_snapshot()
        self.assertFalse(value["ok"])
        self.assertFalse(value["mqtt"]["reachable"])

    def test_runtime_status_health_rejects_stale_snapshot(self):
        now = SERVER.datetime.fromisoformat("2026-08-21T00:00:05+00:00")
        value = SERVER.runtime_status_health(
            {"state": "online", "checked_utc": "2026-08-21T00:00:00+00:00"},
            now=now,
        )
        self.assertFalse(value["fresh"])
        self.assertEqual(value["age_ms"], 5000)

    @mock.patch.object(SERVER.subprocess, "run", side_effect=SERVER.subprocess.TimeoutExpired("systemctl", 2))
    def test_systemctl_timeout_is_unknown(self, _run):
        self.assertEqual(SERVER.service_state("example.service"), "unknown")


if __name__ == "__main__":
    unittest.main()
