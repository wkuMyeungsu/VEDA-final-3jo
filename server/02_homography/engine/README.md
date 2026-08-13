# 호모그래피 도구

OpenCV 기반 호모그래피 CLI 도구임. ArUco 마커 생성, 촬영 이미지의
마커 검출·호모그래피 계산·결과 검증 기능 제공함.

## 1. 엔진 구조

```text
engine/
├── include/homography/   # 재사용 가능한 공개 헤더
├── src/core/             # ArUco 검출·호모그래피·오차 계산
├── src/io/               # 설정·JSON 입출력
├── src/render/           # 마커 이미지 생성
├── main.cpp              # homography_tool 명령행 진입점
└── third_party/          # 외부 헤더 의존성
```

`homography_core`는 CLI와 분리된 C++ 정적 라이브러리.
웹 앱이나 추후 Python 바인딩이 추가되더라도 동일한 알고리즘을 재사용할 수 있도록,
알고리즘 코드는 CLI 인자나 웹 요청을 직접 처리하지 않음.

## 2. 설정

운영에 사용하는 마커 규격, 출력 기본값, 오차 기준, 미리보기 배율은 `../config/homography_config.json`에서 관리함.
CLI 옵션 지정 시 해당 실행에 한해 설정 파일의 기본값을 덮어씀.

주요 설정 그룹은 다음과 같음.

- 기본 격자: `dictionary`, `cols`, `rows`, `marker_len_mm`, `gap_mm`
- 마커 출력: `marker_output`
- 결과 파일: `outputs`
- 고정 격자 산출: `calibration`
- 수동 배치 산출: `manual_solve`
- 결과 미리보기: `preview`

각 항목을 처음 설정할 때는 [설정 파일 안내](../config/README.md)를
먼저 확인함. 실제 설정 JSON에는 주석을 넣을 수 없으므로, 항목별
설명과 복사 가능한 예시를 별도 문서로 제공함.

길이 단위 확인 필요. 고정 격자의 `marker_len_mm`, `gap_mm`와
자동 산출 오차를 포함한 모든 거리 값은 mm임.

## 3. 제공 기능

### 3.1 개별 ArUco 마커 생성

```sh
homography_tool gen-marker \
  --config ../config/homography_config.json \
  --id 0 \
  --output marker_000.png \
  --size-mm 100 \
  --margin-mm 20 \
  --label 00 \
  --dpi 300
```

- ID별 개별 마커 생성
- 마커 크기와 흰색 여백 지정
- PNG 또는 SVG 출력
- 기준점 `+`와 숫자 라벨 포함 가능

### 3.2 고정 격자 호모그래피 산출

```sh
homography_tool calibrate \
  --config ../config/homography_config.json \
  --input capture.png \
  --output homography.json \
  --max-rmse-mm 20
```

설정 파일에 정의된 격자의 ID와 실제 위치를 기준으로 마커를 검출하고,
픽셀 좌표와 월드 좌표 사이의 변환 행렬을 산출함.

### 3.3 수동 배치 호모그래피 산출

```sh
homography_tool solve-manual \
  --config ../config/homography_config.json \
  --input capture.png \
  --layout layout.json \
  --output homography_manual.json \
  --overlay homography_overlay.png
```

`layout.json`에는 마커 크기와 각 ID의 실제 좌표를 입력함. 좌표는 각
마커의 왼쪽 위 모서리 기준. X는 오른쪽, Y는 아래쪽 방향.

```json
{
  "marker_size_mm": 100,
  "markers": [
    {"id": 0, "x_mm": 0, "y_mm": 0},
    {"id": 1, "x_mm": 250, "y_mm": 40},
    {"id": 2, "x_mm": 80, "y_mm": 300}
  ]
}
```

마커 간격이 일정하지 않아도 각 ID의 실제 좌표를 개별 입력 가능함.
결과 JSON에는 변환 행렬, 검출 ID, 누락 ID, 사용 마커 수, 재투영 오차가 포함됨.

### 3.4 결과 이미지 생성

```sh
homography_tool view \
  --config ../config/homography_config.json \
  --homography homography.json \
  --input capture.png \
  --output-dir view_result
```

호모그래피를 적용한 평면 이미지와 오차 표시 이미지 생성함.

## 4. 빌드

저장소 루트에서 실행함.

```sh
cmake -S server/02_homography/engine -B server/02_homography/engine/build
cmake --build server/02_homography/engine/build -j2
```

실행 파일:

```text
server/02_homography/engine/build/homography_tool
```

## 5. 테스트

### 5.1 단위 테스트

```sh
cmake -S server/02_homography/engine \
  -B server/02_homography/engine/build \
  -DBUILD_TESTING=ON
cmake --build server/02_homography/engine/build -j2
cd server/02_homography/engine/build
ctest --output-on-failure
```

단위 테스트는 격자 좌표 계산, 행렬 JSON 변환, 보드 렌더링, 합성 이미지 기반 호모그래피 산출을 검증함.
