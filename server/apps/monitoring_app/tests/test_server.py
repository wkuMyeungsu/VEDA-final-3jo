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
            "`읽기 전용 운영 상태입니다. ${REFRESH_INTERVAL_SECONDS}초마다 갱신됩니다.`;",
            page,
        )
        self.assertIn("window.setInterval(refresh,REFRESH_INTERVAL_MS);", page)
        self.assertNotIn("setInterval(refresh,1000)", page)
        self.assertIn('<pre id="server-logs">확인 중</pre>', page)
        self.assertIn("recentLines.join('\\n')", page)
        for label in ("서버 운영 콘솔", "안전 서버", "호모그래피 앱", "최근 서버 로그", "유지보수 도구"):
            self.assertIn(label, page)
        self.assertIn("active:'O'", page)
        self.assertIn("inactive:'X'", page)

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

    @mock.patch.object(SERVER, "file_timestamp", return_value="2026-08-21T00:00:00+00:00")
    @mock.patch.object(SERVER, "tcp_reachable", return_value=True)
    @mock.patch.object(SERVER, "service_state")
    def test_ready_snapshot(self, service_state, _tcp, _timestamp):
        service_state.side_effect = ["active", "inactive"]
        value = SERVER.status_snapshot()
        self.assertTrue(value["ok"])
        self.assertEqual(value["safety_server"]["state"], "active")
        self.assertEqual(value["homography"]["state"], "inactive")
        self.assertEqual(value["homography"]["lifecycle"], "on-demand")

    @mock.patch.object(SERVER, "file_timestamp", return_value=None)
    @mock.patch.object(SERVER, "tcp_reachable", return_value=False)
    @mock.patch.object(SERVER, "service_state", return_value="inactive")
    def test_not_ready_when_dependencies_are_down(self, _service, _tcp, _timestamp):
        value = SERVER.status_snapshot()
        self.assertFalse(value["ok"])
        self.assertFalse(value["mqtt"]["reachable"])

    @mock.patch.object(SERVER.subprocess, "run", side_effect=SERVER.subprocess.TimeoutExpired("systemctl", 2))
    def test_systemctl_timeout_is_unknown(self, _run):
        self.assertEqual(SERVER.service_state("example.service"), "unknown")


if __name__ == "__main__":
    unittest.main()
