# 호모그래피 엔진

OpenCV 기반 C++ 호모그래피 엔진이다. 현재는 자유롭게 배치한 ArUco
마커를 검출하고, 카메라 화면 펴기와 겹침 구간 연결을 수행한다.
카메라 화면을 채널 지도 좌표로 변환하는 행렬은
`H_camera_pixels_to_channel_map`, 채널 지도에서 공유 지도로 연결하는 행렬은
`H_channel_map_to_shared_map`이다. 두 단계를 합친 카메라 화면 → 전체 지도
변환은 `H_camera_pixels_to_shared_map`이며, 채널 지도에서 카메라 화면으로
되돌리는 행렬은 `H_channel_map_to_camera_pixels`이다.

## 명령

### 개별 마커 생성

```sh
homography_tool gen-marker \
  --config server/config/homography/homography_config.json \
  --id 0 \
  --output marker_000.png \
  --size-mm 100 \
  --margin-mm 20 \
  --dpi 300
```

마커 ID의 모양은 설정의 `dictionary`를 따른다. 출력 크기와 여백은
`marker_output`에서 기본값을 가져오며 명령행 인자로 덮어쓸 수 있다.

### 마커 검출

```sh
homography_tool detect-markers \
  --config server/config/homography/homography_config.json \
  --input capture.png \
  --output markers.json \
  --overlay markers_overlay.png
```

이미지에서 ArUco ID와 네 꼭짓점의 픽셀 좌표만 추출한다.

### 카메라 화면 펴기

```sh
homography_tool solve-manual \
  --config server/config/homography/homography_config.json \
  --input capture.png \
  --layout layout.json \
  --output homography_manual.json \
  --overlay homography_overlay.png
```

`layout.json`은 기준 마커 ID, 실제 마커 크기, 선택적인 꼭짓점 보정값을
담는다. 마커 위치를 격자 좌표로 입력하지 않는다. 이 단계는
`H_camera_pixels_to_channel_map`을 산출하며, 반대 방향 변환에는
`H_channel_map_to_camera_pixels`를 사용한다.

### 겹침 구간 연결

```sh
homography_tool align-markers \
  --config server/config/homography/homography_config.json \
  --source channel_a.png \
  --destination channel_b.png \
  --output alignment.json
```

두 이미지에서 같은 ID의 마커를 찾아 source 픽셀 좌표를 destination 픽셀
좌표로 변환하는 대응 관계를 계산한다. 실제 운영 정합은 웹 앱이 각 채널의
`H_camera_pixels_to_channel_map`과 이 대응 관계를 함께 사용해
`H_channel_map_to_shared_map`과 `H_camera_pixels_to_shared_map`을 저장한다.

## 설정

설정 설명과 예시는 [`server/config/homography/README.md`](../../../config/homography/README.md)에 있다.
핵심은 다음 네 가지다.

- `dictionary`: 검출할 ArUco 사전
- `marker_output`: 개별 마커 출력 기본값
- `manual_solve.marker_size_mm`: 수동 산출 기본 마커 크기
- `map.min_common_markers`: 웹 앱이 관리하는 채널 정합 정책

고정 격자 기반의 `cols`, `rows`, `gap_mm` 같은 값과 `calibrate`, `view`
명령은 제거했다. 현재의 폼보드 자유 배치 방식에서는 마커 ID와 실제
검출 꼭짓점이 기준이다.

## 검증 지표

- `marker_shape_rmse_mm`: 마커 모양 오차
- `overlap_join_rmse_mm`: 겹침 맞춤 오차
- `shared_map_overlap_rmse_mm`: 전체 지도 겹침 구간 맞춤 오차
- `held_out_overlap_check`: 숨긴 겹침 마커 예측 확인

## 빌드

```sh
cmake -S server/apps/homography_app/processing \
  -B server/apps/homography_app/processing/build \
  -DBUILD_TESTING=ON
cmake --build server/apps/homography_app/processing/build -j2
```

## 테스트

```sh
cd server/apps/homography_app/processing/build
ctest --output-on-failure
```

단위 테스트는 자유 배치 마커의 정사각형 제약, 꼭짓점 보정, 공통 ID 기반
채널 정합, 좌표계 방향 반전 처리를 검증한다.
