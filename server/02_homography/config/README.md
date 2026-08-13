# 설정 파일 안내

이 폴더의 JSON은 사용할 마커 보드와 통과 오차 기준을 정하는 파일임.
보통 `homography_config.json`을 복사한 뒤 보드의 실제 크기에 맞춰
기본 격자 부분만 수정함.

## 먼저 확인할 것

- `marker_len_cm`, `gap_cm`: 실제 인쇄·배치 마커의 크기와 간격.
- `cols`, `rows`: 가로·세로 마커 개수.
- `id_offset`: 왼쪽 위 마커의 ID. ID가 10부터 시작하면 `10` 입력.
- `inputs.image`: 고정 격자 산출과 검증에 사용할 서버 내 촬영 이미지 파일명.
- 길이 단위: 기본 격자와 자동 산출은 `cm`, 출력 크기와 수동 산출은 `mm`.
- `origin_corner`: 현재 `TL`(왼쪽 위)만 지원. 좌표의 X는 오른쪽,
  Y는 아래쪽 방향.

`marker_len_cm`는 실제 보드에서 검출할 검은 마커 한 변이고,
`marker_output.size_mm`는 개별 마커 파일을 만들 때 출력할 영역의 한 변.
두 값은 같은 마커를 가리킬 수 있지만 단위와 용도가 달라 별도 설정함.

ID는 `id_offset`부터 왼쪽에서 오른쪽, 위에서 아래 순서로 배치함.
예를 들어 `cols=4`, `rows=3`, `id_offset=0`이면 다음과 같음.

```text
원점 (0,0) → X
┌────┬────┬────┬────┐
│  0 │  1 │  2 │  3 │
├────┼────┼────┼────┤
│  4 │  5 │  6 │  7 │
├────┼────┼────┼────┤
│  8 │  9 │ 10 │ 11 │
└────┴────┴────┴────┘
Y는 아래 방향
```

## 설정 예시

아래는 4열 × 8행 보드 예시임. 마커 한 변은 8 cm, 마커 사이 간격은
4.3 cm, 첫 번째 마커의 ID는 0.

```json
{
  "dictionary": "DICT_4X4_100",
  "cols": 4,
  "rows": 8,
  "marker_len_cm": 8.0,
  "gap_cm": 4.3,
  "id_offset": 0,
  "origin_corner": "TL",
  "inputs": {
    "image": "capture.png"
  },
  "marker_output": {
    "size_mm": 100.0,
    "margin_mm": 20.0,
    "dpi": 300.0,
    "label": ""
  },
  "calibration": {
    "max_rmse_cm": 2.0,
    "ransac_threshold_cm": 3.0,
    "channel": 1
  },
  "manual_solve": {
    "marker_size_mm": 100.0,
    "ransac_threshold_mm": 3.0
  },
  "outputs": {
    "calibration": "homography_ch1.json",
    "manual": "homography_manual.json",
    "view_dir": "view_result"
  },
  "preview": {
    "scale": 20,
    "good_error_cm": 0.5,
    "warning_error_cm": 1.0
  }
}
```

## 항목별 의미

### 보드 기본값

| 항목 | 의미 | 예시 |
| --- | --- | --- |
| `dictionary` | ArUco ID를 읽는 규칙. 인쇄에 사용한 사전과 같아야 함. | `DICT_4X4_100` |
| `cols`, `rows` | 보드의 열·행 개수. | `4`, `8` |
| `marker_len_cm` | 마커 검은 사각형 한 변의 실제 길이. | `8.0` |
| `gap_cm` | 이웃한 마커의 검은 사각형 사이 실제 간격. | `4.3` |
| `id_offset` | 왼쪽 위 마커의 시작 ID. 이후 왼쪽에서 오른쪽, 위에서 아래 순서로 1씩 증가함. | `0` |

예를 들어 위 설정에서 ID `5`는 두 번째 행의 두 번째 마커임. 보드
좌표의 시작점은 왼쪽 위 마커의 왼쪽 위 모서리.

### 마커 출력값

- `marker_output`: 마커 하나를 만들 때의 기본 크기, 여백, PNG 해상도.

### 결과 파일

- `outputs.calibration`: 고정 격자 호모그래피 결과 파일명.
- `outputs.manual`: 수동 배치 호모그래피 결과 파일명.
- `outputs.view_dir`: 검증 이미지 출력 디렉터리명.
- 결과 파일명은 웹 UI에서 입력하지 않고 이 설정을 서버가 사용함.

출력 단위는 다음과 같음.

| 값 | 단위 | 기준 |
| --- | --- | --- |
| `marker_len_cm`, `gap_cm` | cm | 실제 고정 격자의 검출 좌표 |
| `max_rmse_cm`, `ransac_threshold_cm` | cm | 자동 산출 오차 |
| `marker_output.size_mm` | mm | 개별 마커 출력물의 실제 크기 |
| `marker_size_mm`, `x_mm`, `y_mm` | mm | 수동 배치의 실제 좌표 |
| `dpi` | DPI | PNG 출력 해상도 |
| `preview.scale` | px/cm | 검증용 합성 이미지 배율 |

### 정확도 판정

- `max_rmse_cm`: `calibrate` 명령이 성공으로 인정할 최대 평균 오차.
- `ransac_threshold_cm`: 일부 코너를 이상값으로 제외할 거리 기준.
- `channel`: 결과 JSON에 기록할 채널 번호. 채널 1은 `1` 입력.
- `manual_solve`의 두 값: 수동 배치 방식에서 사용하며 길이 단위는 `mm`.
- `preview.scale`: 검증 이미지에서 1 cm를 그리는 픽셀 수.
- `good_error_cm`, `warning_error_cm`: 검증 이미지의 초록·노랑·빨강 기준.

정확도 기준을 바꾸기 전에 실제 촬영 이미지의 RMSE 확인 권장.
기준을 너무 크게 잡으면 잘못된 보드도 통과할 수 있고, 너무 작게 잡으면
정상적인 촬영도 실패할 수 있음.

## 수동 배치 파일 예시

격자가 일정하지 않거나 마커를 임의의 위치에 붙인 경우 별도의
`layout.json` 사용함. `x_mm`, `y_mm`는 각 마커의 **왼쪽 위 모서리**
위치이며, 원점에서 오른쪽·아래쪽으로 측정함.

```json
{
  "marker_size_mm": 100,
  "markers": [
    {"id": 0, "x_mm": 0,   "y_mm": 0},
    {"id": 1, "x_mm": 250, "y_mm": 40},
    {"id": 2, "x_mm": 80,  "y_mm": 300}
  ]
}
```

명령은 다음처럼 실행함.

```sh
homography_tool solve-manual \
  --config ../config/homography_config.json \
  --input capture.png \
  --layout layout.json \
  --output homography_manual.json \
  --overlay homography_overlay.png
```

`markers`에 적은 ID가 촬영 이미지에서 보이지 않으면 결과의 `missing_ids`에
기록됨. 최소 한 개의 유효한 마커가 보여야 수동 계산 진행 가능함.
