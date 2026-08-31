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
    def setUp(self):
        SERVER._RECENT_LOG_CACHE = []

    @mock.patch.object(SERVER.subprocess, "run",
                       return_value=mock.Mock(returncode=0, stdout="line-0\nline-1\nline-2\n"))
    def test_recent_logs_reads_safety_unit_journal(self, run):
        self.assertEqual(SERVER.read_recent_logs(3), ["line-0", "line-1", "line-2"])
        args = run.call_args.args[0]
        self.assertIn("forklift_safety_server.service", args)
        self.assertIn("-n", args)

    def test_recent_logs_survives_journalctl_failure(self):
        with mock.patch.object(SERVER.subprocess, "run",
                               side_effect=SERVER.subprocess.SubprocessError("boom")):
            self.assertEqual(SERVER.read_recent_logs(), [])

    @mock.patch.object(SERVER.subprocess, "run",
                       side_effect=[mock.Mock(returncode=0, stdout="line-0\nline-1\n"),
                                    SERVER.subprocess.SubprocessError("boom")])
    def test_recent_logs_keeps_last_success_during_journalctl_failure(self, _run):
        self.assertEqual(SERVER.read_recent_logs(), ["line-0", "line-1"])
        self.assertEqual(SERVER.read_recent_logs(), ["line-0", "line-1"])

    @mock.patch.dict(SERVER.os.environ, {"SERVER_MONITORING_REFRESH_INTERVAL_SECONDS": "2"})
    def test_index_uses_configured_refresh_interval(self):
        page = SERVER.render_index()

        self.assertIn('data-refresh-interval-seconds="2"', page)
        self.assertIn("${REFRESH_INTERVAL_SECONDS}초마다", page)

    @mock.patch.dict(SERVER.os.environ, {"SERVER_MONITORING_REFRESH_INTERVAL_SECONDS": "invalid"})
    def test_invalid_refresh_interval_uses_safe_default(self):
        self.assertEqual(
            SERVER.refresh_interval_seconds(),
            SERVER.DEFAULT_REFRESH_INTERVAL_SECONDS,
        )

    def test_index_renders_structured_server_logs(self):
        page = SERVER.render_index()

        self.assertIn('id="server-logs"', page)
        self.assertIn('class="log-list"', page)
        self.assertIn("const parseServerLogLine=", page)
        self.assertIn("const isSystemdLogLine=", page)
        self.assertIn("const visibleLogCount=renderServerLogs(recentLines)", page)
        self.assertNotIn("<pre id=\"server-logs\">", page)
        self.assertNotIn("recentLines.join('\\n')", page)

    def test_index_replaces_status_lists_with_site_map(self):
        page = SERVER.render_index()

        self.assertIn('data-tab-target="tab-map">전체 맵</button>', page)
        self.assertIn('id="site-map"', page)
        self.assertIn('renderSiteMap(snapshot.site_map,snapshot.terminals||[])', page)
        self.assertIn("r:radius,class:`map-forklift map-risk-${risk}`", page)
        self.assertIn('const insideWorkArea=(position)=>pointInPolygon(position,displayBoundary);', page)
        self.assertIn('const xs=displayBoundary.map((point)=>Number(point[0]));', page)
        self.assertNotIn('...livePoints]', page)
        self.assertNotIn("clip-path':'url(#map-boundary-clip)", page)
        self.assertNotIn('>단말 상태</button>', page)
        self.assertNotIn('>검출 현황</button>', page)

    def test_failed_refresh_keeps_last_resource_values(self):
        page = SERVER.render_index()
        unknown_state = page.split("const setUnknownState=()=>{", 1)[1].split(
            "async function refresh", 1)[0]

        self.assertNotIn("renderResources({})", page)
        self.assertNotIn("renderSiteMap(null,[])", unknown_state)
        self.assertIn("if(!lastSuccessfulRefresh)", page)
        self.assertIn("마지막 정상 상태를 유지 중입니다", page)

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

                repeated = SERVER.host_resource()
                self.assertEqual(repeated["cpu_percent"], 66.7)
                self.assertEqual(repeated["cpu_cores"], second["cpu_cores"])

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

    @mock.patch.object(SERVER, "file_timestamp", return_value=None)
    @mock.patch.object(SERVER, "resource_snapshot", return_value={})
    @mock.patch.object(SERVER, "tcp_reachable", return_value=False)
    @mock.patch.object(SERVER, "service_state", return_value="inactive")
    @mock.patch.object(
        SERVER,
        "read_runtime_status",
        return_value={
            "state": "online",
            "checked_utc": SERVER.datetime.now(SERVER.timezone.utc).isoformat(),
        },
    )
    def test_not_ready_when_dependencies_are_down(self, _runtime, service_state, _tcp, _resources, _timestamp):
        value = SERVER.status_snapshot()
        self.assertFalse(value["ok"])
        self.assertFalse(value["mqtt"]["reachable"])
        self.assertEqual(value["safety_server"]["state"], "inactive")

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

    def test_http_routes_serve_real_responses(self):
        # 라우팅·직렬화 회귀를 잡는 최소 end-to-end: 실제 서버를 포트 0으로 띄우고 요청한다.
        import json
        import threading
        import urllib.error
        import urllib.request

        server = SERVER.ThreadingHTTPServer(("127.0.0.1", 0), SERVER.Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            port = server.server_address[1]
            base = f"http://127.0.0.1:{port}"

            with mock.patch.object(
                SERVER,
                "status_snapshot",
                return_value={"ok": True, "monitoring": "online"},
            ):
                with urllib.request.urlopen(f"{base}/api/status") as response:
                    self.assertEqual(response.status, 200)
                    self.assertEqual(json.load(response)["monitoring"], "online")

            with urllib.request.urlopen(f"{base}/health/live") as response:
                self.assertEqual(json.load(response)["service"], "monitoring-app")

            with mock.patch.object(SERVER, "read_recent_logs", return_value=["line-0"]):
                with urllib.request.urlopen(f"{base}/api/logs") as response:
                    payload = json.load(response)
                    self.assertEqual(response.status, 200)
                    self.assertEqual(payload["recent_lines"], ["line-0"])
                    self.assertEqual(payload["logs"], ["line-0"])

            with self.assertRaises(urllib.error.HTTPError) as raised:
                urllib.request.urlopen(f"{base}/missing")
            self.assertEqual(raised.exception.code, 404)
        finally:
            server.shutdown()


if __name__ == "__main__":
    unittest.main()
