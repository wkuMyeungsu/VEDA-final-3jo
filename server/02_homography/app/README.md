# 호모그래피 보정 앱

이 앱은 등록된 모든 CCTV의 모든 채널을 `stream_id`별로 차례대로
보정하고, 캡처한 전체 스트림을 공통 마커로 동시에 정합해 보드 전체의
공통 mm 좌표를 만드는 도구다.
예를 들어 `CAM_01`의 1번과 `CAM_02`의 1번은 각각
`CAM_01_CH_01`, `CAM_02_CH_01`이라는 서로 다른 스트림이다.

## 작업 순서

1. `CCTV · CH`가 함께 표시된 스트림을 선택하고 `선택 스트림 캡처`를 누른다.
2. 검출된 ArUco 마커의 꼭짓점을 확인한다. 어긋난 꼭짓점은 화면에서
   주황색 점을 드래그해 보정한다.
3. 마커 검은 사각형 한 변의 실제 길이(mm)를 입력하고 `이 채널 로컬 H
   산출`을 누른다.
4. 겹치는 마커가 있는 다른 CCTV의 채널도 같은 방법으로 로컬 H를 산출한다.
5. 모든 스트림의 로컬 H가 준비되면 전체 맵 기준 스트림 하나를 선택한다. 각
   스트림 쌍의 공통 마커가 최소 개수 이상이고, 전체 스트림이 연결망으로
   이어져야 정합 버튼이 활성화된다.
6. `전체 채널 정합`을 누르면 모든 CCTV×채널 스트림 쌍의 공통 마커 제약을
   한 번에 사용해 전체 오차가 최소가 되는 전역 변환을 계산한다. 스트림을 순서대로
   누적하지 않으므로 연결 순서에 따른 오차 누적을 피한다.
7. 정합이 끝나면 모든 캡처 원본을 전체 맵 좌표에 투영한 검증 화면이 열린다.
   원본 색상은 그대로 두고, 채널 외곽선·투명도·채널별 깜빡임 비교로
   겹침 상태를 확인한다.

X/Y 기준선, 자로 잰 거리, 관심 영역은 이 흐름에 사용하지 않는다. 마커
ID와 네 꼭짓점 순서를 대응점으로 사용하므로, 같은 ID의 마커 방향과 위치가
정합에 함께 반영된다. 공통 마커가 한 직선에 몰려 기하적으로 불안정하면
서버가 정합을 거부한다.

검증 화면의 영상은 별도 합성 이미지가 아니라 각 채널의 실제 `capture.jpg`를
저장된 전역 H로 직접 투영한 것이다. 따라서 화면에서 보이는 원본 질감과
위치는 서버가 해당 H로 해석하는 결과를 그대로 확인하는 용도다.

## 설정

공통 `server/config/camera_model.json`에는 카메라 종류별 채널 수를,
`server/config/camera_list.json`에는 실제 CCTV 목록과 `camera_id ↔ model`
매핑을 저장한다. 호모그래피 앱과 안전 서버는 이 공통 파일을 같이 읽으며,
호모그래피 전용 정책은 `02_homography/config/homography_config.json`에 둔다.
비밀번호가 들어가는 `camera_list.json`은 운영 장비에서만 작성한다.

```sh
cd /home/veda3/01_Workspace/server
cp config/camera_list.sample.json config/camera_list.json
chmod 600 config/camera_list.json
```

`homography_config.json`의 `map`은 겹치는 채널을 연결할 정책만 정한다.

검증 화면이 한 번에 합성할 수 있는 최대 스트림 수는 별도
`02_homography/config/stream_config.json`에서 관리한다.

```json
{
  "verification": {
    "max_streams": 16
  }
}
```

이 값은 실제 CCTV RTSP 수신 개수가 아니라, 전체 맵 검증 화면의 WebGL 입력
슬롯 수다. 실제 정합 대상은 `camera_list.json`에 등록된 모든 CCTV×채널이며,
설정값을 넘는 경우에는 검증 화면이 명확한 오류를 표시한다.

```json
"map": {
  "min_common_markers": 3
}
```

- `min_common_markers`: 스트림 쌍을 전체 정합 제약으로 사용할 최소 공통 ID 수.
  기본값은 3.
- 지원 채널 수는 `camera_model.json`의 `channel_count`와
  `camera_list.json`의 `model` 매핑으로 결정한다.

## 결과 저장

스트림별 로컬 H는 임시 작업 결과에 남고, 전체 정합이 완료되면 참여한
모든 스트림의 운영 파일을 원자적으로 새로 만들거나 덮어쓴다.

```text
server/config/homography/CAM_01/homography_channel_3_mm.json
```

운영 H에는 서버가 실제로 읽을 값만 저장한다.

```json
{
  "schema_version": 2,
  "world_unit": "mm",
  "camera_id": "CAM_01",
  "stream_id": "CAM_01_CH_03",
  "channel": 3,
  "H_pixel_to_world": [[...], [...], [...]],
  "image_size": {"width": 2592, "height": 1520}
}
```

로컬 H와 채널 간 제약은 현재 앱 세션에서만 전역 계산에 사용하고 운영
경로에는 저장하지 않는다. 최종 H는 공통 `server/config/homography`에
저장되며 main이 같은 파일을 직접 읽는다. RMSE, 사용 마커, 캡처 ID 같은 상세 정보도
`/tmp/homography-results/<capture_id>` 아래 작업 결과에만 남긴다.

## 실행

systemd 서비스:

```sh
sudo systemctl restart homography-app.service
sudo systemctl status homography-app.service --no-pager
```

개발 실행:

```sh
cd /home/veda3/01_Workspace/server
HOMOGRAPHY_TOOL=02_homography/engine/build/homography_tool \
  python3 02_homography/app/server.py
```

브라우저 주소:

```text
http://127.0.0.1:8001
```

다른 PC에서는 라즈베리파이 주소를 사용한다.

```text
http://192.168.0.13:8001
```

엔진을 수정했다면 먼저 빌드한다.

```sh
cmake --build server/02_homography/engine/build -j2
```
