#!/usr/bin/env python3
"""전역 호모그래피 정합의 독립 검증.

기존 C++ 테스트는 합성된 마커가 정사각형으로 복원되는지만 확인했다.
이 테스트는 실제 물리 좌표를 별도로 알고 있는 합성 장면을 만든 뒤,
정합에 사용하지 않은 체크 포인트를 최종 H로 다시 투영한다.
따라서 공통 마커 위치의 RMSE만 낮추고 마커 바깥을 잘못 매핑하는 회귀도
통과하지 못한다.
"""

import importlib.util
import json
import math
import os
import sys
import tempfile
import unittest
from pathlib import Path


def _load_web_module():
    """실제 web/server.py의 순수 정합 함수를 테스트용 설정으로 로드한다."""
    test_directory = tempfile.TemporaryDirectory(prefix="homography-web-test-")
    test_root = Path(test_directory.name)
    homography_config_dir = test_root / "homography"
    common_config_dir = test_root / "common"
    result_dir = test_root / "results"
    homography_config_dir.mkdir(parents=True)
    common_config_dir.mkdir(parents=True)

    (homography_config_dir / "homography_config.json").write_text(
        json.dumps({
            "dictionary": "DICT_4X4_50",
            "manual_solve": {"marker_size_mm": 80},
            "map": {"min_common_markers": 3},
            "outputs": {"manual": "homography_manual.json"},
        }),
        encoding="utf-8",
    )
    (homography_config_dir / "stream_config.json").write_text(
        json.dumps({"verification": {"max_streams": 16}}),
        encoding="utf-8",
    )
    (common_config_dir / "camera_model.json").write_text(
        json.dumps({"models": [{"model": "TEST_MODEL", "channel_count": 1}]}),
        encoding="utf-8",
    )
    (common_config_dir / "camera_list.json").write_text(
        json.dumps({
            "cameras": [{
                "camera_id": "CAM_TEST",
                "model": "TEST_MODEL",
                "channels": [{
                    "channel": 1,
                    "image_width_px": 2400,
                    "image_height_px": 1600,
                }],
            }],
        }),
        encoding="utf-8",
    )

    os.environ.update({
        "HOMOGRAPHY_CONFIG_DIR": str(homography_config_dir),
        "SERVER_COMMON_CONFIG_DIR": str(common_config_dir),
        "HOMOGRAPHY_RESULT_DIR": str(result_dir),
        "SAFETY_SERVER_HOMOGRAPHY_DIR": str(test_root / "operational"),
        "HOMOGRAPHY_TOOL": "/bin/false",
    })

    module_path = Path(__file__).resolve().parents[1] / "web" / "server.py"
    spec = importlib.util.spec_from_file_location(
        "homography_web_server_under_test", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"웹 서버 모듈을 읽을 수 없습니다: {module_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    # 모듈이 사용하는 임시 설정 디렉터리를 테스트 프로세스가 끝날 때까지
    # 유지한다. 반환하지 않으면 TemporaryDirectory가 즉시 정리될 수 있다.
    module._test_root = test_root
    module._test_directory = test_directory
    return module


SERVER = _load_web_module()


def _point(x, y):
    return {"x": float(x), "y": float(y)}


def _apply(matrix, point):
    return SERVER.transform_point(matrix, _point(point[0], point[1]))


def _square(center, side=80.0):
    half = side / 2.0
    x, y = center
    return [
        _point(x - half, y - half),
        _point(x + half, y - half),
        _point(x + half, y + half),
        _point(x - half, y + half),
    ]


def _identity():
    return [[1.0, 0.0, 0.0],
            [0.0, 1.0, 0.0],
            [0.0, 0.0, 1.0]]


def _make_stream_fixture():
    """서로 다른 카메라 평면을 공통 BOARD 좌표로 변환하는 장면을 만든다."""
    markers = {
        1: (120, 120),
        2: (900, 140),
        3: (130, 650),
        4: (920, 690),
        5: (500, 350),
        6: (180, 1120),
    }
    # global -> 각 스트림의 로컬 평면 좌표. 실제 카메라별 로컬 H가
    # 서로 다른 원점·방향을 갖는 상황을 의도적으로 만든다.
    local_frames = {
        "STREAM_A": _identity(),
        "STREAM_B": [
            [0.9659258, -0.2588190, 360.0],
            [0.2588190, 0.9659258, -180.0],
            [0.0, 0.0, 1.0],
        ],
        "STREAM_C": [
            [0.9396926, 0.3420201, -260.0],
            [-0.3420201, 0.9396926, 420.0],
            [0.0, 0.0, 1.0],
        ],
    }
    # 각 로컬 평면을 실제 영상 픽셀로 보는 카메라 투영. 약한 원근을
    # 넣어 단순 평행이동만 맞추는 테스트가 되지 않도록 한다.
    pixel_frames = {
        "STREAM_A": [
            [1.80, 0.08, 360.0],
            [-0.05, 1.35, 260.0],
            [0.00010, -0.00008, 1.0],
        ],
        "STREAM_B": [
            [1.55, -0.12, 820.0],
            [0.07, 1.60, 260.0],
            [0.00008, 0.00005, 1.0],
        ],
        "STREAM_C": [
            [1.65, 0.10, 620.0],
            [-0.08, 1.45, 430.0],
            [0.00005, -0.00008, 1.0],
        ],
    }
    visible_ids = {
        # A-C는 직접 연결되지 않지만 B를 통해 전체 그래프가 연결된다.
        "STREAM_A": (1, 2, 3, 4),
        "STREAM_B": (1, 2, 3, 4, 5, 6),
        "STREAM_C": (3, 4, 5, 6),
    }
    stream_results = {}
    stream_corners = {}
    for stream_id, ids in visible_ids.items():
        pixel_from_local = pixel_frames[stream_id]
        local_from_pixel = SERVER.invert_matrix(pixel_from_local)
        stream_results[stream_id] = {
            "stream_id": stream_id,
            "camera_id": "CAM_TEST",
            "channel": 1,
            "H_pixel_to_world": local_from_pixel,
            "image_size": {"width": 2400, "height": 1600},
        }
        corners = {}
        for marker_id in ids:
            world_corners = _square(markers[marker_id])
            local_corners = [
                _apply(local_frames[stream_id], (point["x"], point["y"]))
                for point in world_corners
            ]
            pixel_corners = []
            for point in local_corners:
                projected = _apply(pixel_from_local, (point["x"], point["y"]))
                pixel_corners.append(_point(projected["x"], projected["y"]))
            corners[marker_id] = pixel_corners
        stream_corners[stream_id] = corners
    return stream_results, stream_corners, markers, local_frames, pixel_frames


class GlobalAlignmentTests(unittest.TestCase):
    def test_final_homographies_map_independent_points(self):
        """사용 마커 밖의 체크 포인트도 BOARD 좌표로 돌아와야 한다."""
        stream_results, stream_corners, _, local_frames, pixel_frames = \
            _make_stream_fixture()
        transforms, edge_results, skipped_pairs, global_rmse, _, cross_validation = \
            SERVER.align_all_streams(stream_results, stream_corners, "STREAM_A")

        self.assertEqual(len(transforms), 3)
        self.assertTrue(any(set(edge["stream_ids"]) == {"STREAM_A", "STREAM_B"}
                            for edge in edge_results))
        self.assertTrue(any(set(edge["stream_ids"]) == {"STREAM_B", "STREAM_C"}
                            for edge in edge_results))
        self.assertTrue(skipped_pairs, "직접 연결되지 않은 A-C 쌍이 기록되어야 함")
        self.assertLess(global_rmse, 0.01)
        self.assertTrue(cross_validation["available"])

        # 이 점들은 어떤 마커의 네 꼭짓점에도 포함되지 않는다.
        check_points = [
            (260, 230), (760, 250), (260, 500),
            (760, 520), (420, 860), (760, 980),
        ]
        for stream_id in stream_results:
            global_h = SERVER.matrix_multiply(
                transforms[stream_id],
                stream_results[stream_id]["H_pixel_to_world"],
            )
            for expected in check_points:
                local = _apply(local_frames[stream_id], expected)
                pixel = _apply(pixel_frames[stream_id], (local["x"], local["y"]))
                actual = SERVER.transform_point(global_h, pixel)
                error = math.hypot(actual["x"] - expected[0],
                                   actual["y"] - expected[1])
                self.assertLess(
                    error, 0.1,
                    f"{stream_id} 독립 체크 포인트 오차가 큼: {expected} -> {actual}",
                )

    def test_disconnected_stream_is_rejected(self):
        """공통 마커 그래프가 끊기면 일부 스트림만 저장해서는 안 된다."""
        stream_results, stream_corners, _, _, _ = _make_stream_fixture()
        stream_corners["STREAM_C"] = {
            marker_id: _square((100 + marker_id * 120, 300))
            for marker_id in (20, 21, 22, 23)
        }
        with self.assertRaisesRegex(ValueError, "연결되지 않은 스트림"):
            SERVER.align_all_streams(stream_results, stream_corners, "STREAM_A")

    def test_pixel_noise_does_not_break_independent_area(self):
        """검출 코너에 작은 노이즈가 있어도 마커 밖의 오차가 폭발하지 않는다."""
        stream_results, stream_corners, _, local_frames, pixel_frames = \
            _make_stream_fixture()
        for stream_id, values in stream_corners.items():
            for marker_id, corners in values.items():
                for index, point in enumerate(corners):
                    point["x"] += ((marker_id * 3 + index) % 5 - 2) * 0.75
                    point["y"] += ((marker_id * 5 + index * 2) % 5 - 2) * 0.75

        transforms, _, _, _, _, _ = SERVER.align_all_streams(
            stream_results, stream_corners, "STREAM_A")
        check_points = [(260, 230), (760, 250), (260, 500),
                        (760, 520), (420, 860), (760, 980)]
        for stream_id in stream_results:
            global_h = SERVER.matrix_multiply(
                transforms[stream_id],
                stream_results[stream_id]["H_pixel_to_world"],
            )
            for expected in check_points:
                local = _apply(local_frames[stream_id], expected)
                pixel = _apply(pixel_frames[stream_id], (local["x"], local["y"]))
                actual = SERVER.transform_point(global_h, pixel)
                self.assertLess(
                    math.hypot(actual["x"] - expected[0],
                               actual["y"] - expected[1]),
                    5.0,
                    f"{stream_id} 노이즈 상황 체크 포인트 오차가 큼: {expected}",
                )

    def test_bad_common_marker_is_exposed_by_cross_validation(self):
        """잘못된 공통 마커는 낮은 적합 RMSE와 달리 교차검증에서 드러난다."""
        stream_results, stream_corners, _, _, _ = _make_stream_fixture()
        for point in stream_corners["STREAM_B"][2]:
            point["x"] += 80.0
            point["y"] -= 55.0

        _, _, _, global_rmse, _, cross_validation = SERVER.align_all_streams(
            stream_results, stream_corners, "STREAM_A")
        # 모든 점을 억지로 맞춘 적합 RMSE만 보면 오차가 작아 보일 수 있다.
        self.assertLess(global_rmse, 10.0)
        self.assertTrue(cross_validation["available"])
        self.assertGreater(cross_validation["max_error_mm"], 20.0)

    def test_collinear_common_markers_are_rejected(self):
        """공통 마커가 한 직선이면 projective 정합을 허용하지 않는다."""
        ids = (1, 2, 3)
        points = {marker_id: (100 + marker_id * 250, 500) for marker_id in ids}
        corners = {
            "STREAM_A": {marker_id: _square(center) for marker_id, center in points.items()},
            "STREAM_B": {marker_id: _square((center[0] + 80, center[1] + 40))
                         for marker_id, center in points.items()},
        }
        results = {
            stream_id: {
                "stream_id": stream_id,
                "H_pixel_to_world": _identity(),
                "image_size": {"width": 2000, "height": 1200},
            }
            for stream_id in corners
        }
        with self.assertRaises(ValueError):
            SERVER.align_all_streams(results, corners, "STREAM_A")

    def test_check_points_are_not_used_as_fit_points(self):
        """검증 코드는 fit 마커를 그대로 다시 평가하지 않아야 한다."""
        stream_results, stream_corners, markers, local_frames, pixel_frames = \
            _make_stream_fixture()
        # STREAM_A와 B의 연결에서 ID 4를 제거해, 이 마커는 정합에 쓰이지
        # 않는 독립 체크 포인트가 된다.
        held_out_id = 4
        fit_corners = {
            stream_id: {
                marker_id: corners
                for marker_id, corners in values.items()
                if not (stream_id in ("STREAM_A", "STREAM_B") and marker_id == held_out_id)
            }
            for stream_id, values in stream_corners.items()
        }
        # A-B는 1,2,3만 남아도 연결되지만, B-C는 3,4,5,6으로 연결된다.
        transforms, _, _, global_rmse, _, _ = SERVER.align_all_streams(
            stream_results, fit_corners, "STREAM_A")
        self.assertLess(global_rmse, 0.1)

        for stream_id in ("STREAM_A", "STREAM_B"):
            global_h = SERVER.matrix_multiply(
                transforms[stream_id], stream_results[stream_id]["H_pixel_to_world"])
            world_corners = [_square(markers[held_out_id])[index]
                             for index in range(4)]
            for expected_point in world_corners:
                local = _apply(local_frames[stream_id],
                               (expected_point["x"], expected_point["y"]))
                pixel = _apply(pixel_frames[stream_id], (local["x"], local["y"]))
                actual = SERVER.transform_point(global_h, pixel)
                self.assertLess(
                    math.hypot(actual["x"] - expected_point["x"],
                               actual["y"] - expected_point["y"]),
                    0.2,
                )


if __name__ == "__main__":
    unittest.main()
