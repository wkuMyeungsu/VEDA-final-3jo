# 호모그래피 설정·결과 안내

이 폴더의 `homography_config.json`과 `stream_config.json`은 자유 배치 ArUco 마커를 이용한
스트림별 호모그래피 산출과 전체 CCTV×채널 정합에 필요한 공통 설정이다.
정합이 끝난 카메라별 최종 결과도 이 폴더 아래 `CAM_*` 디렉터리에 저장한다.

현재 보정 방식은 마커를 격자에 맞춰 배치하지 않는다. 각 채널에서 검출한
마커의 실제 정사각형 크기와 꼭짓점을 이용해 카메라 화면을 펴고, 겹치는 공통
마커의 ID와 방향으로 모든 CCTV×채널 스트림을 하나의 전체 맵 좌표계에 연결한다.

## 설정 파일

```json
{
  "dictionary": "DICT_4X4_50",
  "marker_output": {
    "size_mm": 100.0,
    "margin_mm": 20.0,
    "dpi": 300.0,
    "label": ""
  },
  "manual_solve": {
    "marker_size_mm": 100.0
  },
  "map": {
    "min_common_markers": 3
  },
  "outputs": {
    "manual": "homography_manual.json"
  }
}
```

| 항목 | 의미 |
| --- | --- |
| `dictionary` | 실제로 인쇄한 ArUco 사전. 검출 대상과 반드시 같아야 함. |
| `marker_output` | 개별 마커 PNG·SVG를 만들 때 사용할 기본 출력 크기와 여백. |
| `manual_solve.marker_size_mm` | 사용자가 별도 입력하지 않았을 때 적용할 마커 한 변의 실제 길이. |
| `map.min_common_markers` | 두 스트림 연결을 허용할 최소 공통 마커 수. 현재 최소 3개. |
| `outputs.manual` | 스트림별 임시 산출물의 파일명. 최종 H의 저장 위치는 앱이 결정함. |

## 의도적으로 제거한 항목

다음 값은 고정 격자 보드에서만 의미가 있어 현재 설정에서 사용하지 않는다.

- `cols`, `rows`: 격자의 열·행 수
- `marker_len_mm`, `gap_mm`: 격자 마커 크기와 간격
- `id_offset`, `origin_corner`: 격자 ID 순서와 좌표 원점
- `inputs.image`: 예전 정적 이미지 기본 입력 경로
- `calibration`: 격자 자동 산출 기준
- `preview`: 격자 기반 결과 이미지 표시 기준

현재 엔진에는 고정 격자용 `calibrate`·`view` 명령이 없다. 따라서 위 값을
다시 추가해도 사용되지 않으며, 실제 위치는 마커 검출 결과와 사용자가
화면에서 보정한 꼭짓점으로 결정한다.

## 수동 산출 입력

웹 앱이 엔진에 전달하는 `layout.json`은 다음과 같은 형태다.

```json
{
  "marker_size_mm": 100,
  "reference_marker_id": 0,
  "excluded_ids": [],
  "corner_overrides": {
    "0": [
      {"x": 100, "y": 80},
      {"x": 180, "y": 82},
      {"x": 178, "y": 162},
      {"x": 98, "y": 160}
    ]
  }
}
```

`corner_overrides`의 네 점은 원본 캡처의 픽셀 좌표다. 검출된 꼭짓점이
조금 어긋난 경우에만 사용자가 화면에서 직접 보정한다. X/Y 기준선이나
전체 보드의 고정 격자 좌표는 입력하지 않는다.

## 결과 저장

전체 스트림 정합이 끝난 최종 H는 공용 서버 설정 경로에 저장된다.

```text
server/config/homography/CAM_01/homography_result_cam01_ch<channel>_mm.json
```

main 서버는 이 파일의 `H_camera_pixels_to_shared_map`, `image_size`, `stream_id`, `channel`을 읽어
실시간 객체 좌표를 전체 맵 mm 좌표로 변환한다.
