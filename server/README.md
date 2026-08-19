# Server workspace

`server/`는 라즈베리파이에 배포되는 서버 영역이다. 실행 앱은 `apps/`, 공용·운영
설정은 `config/`, 서비스 파일은 `deploy/` 아래에 둔다.

```text
server/
├── apps/
│   ├── main_app/                    # C++ 안전 서버
│   ├── homography_app/
│   │   ├── web/                     # 8001 웹 UI/API
│   │   └── processing/              # C++ ArUco·호모그래피 처리
│   └── monitoring_app/              # 8000 모니터링 UI/API
├── config/
│   ├── safety/                      # 안전 서버 정책 설정
│   ├── homography/                  # 정책 설정 + 카메라별 최종 H 결과
│   ├── camera_model.json
│   └── camera_list.json
├── deploy/systemd/
├── third_party/
├── build/                           # 로컬 생성물
└── var/                             # 런타임 DB·CSV·로그
```

자동 테스트 소스는 각 독립 CMake 타깃 옆에 둔다.

```text
apps/main_app/tests/                         # 안전 서버 단위·통합·E2E 테스트
apps/homography_app/processing/tests/        # 호모그래피 처리 단위 테스트
apps/main_app/tools/sensor_fusion_smoke_main.cpp  # 실제 센서 수동 진단 도구
```

`Testing/` 디렉터리는 CTest가 실행 중 만드는 산출물이므로 소스 구조에 포함하지
않는다.

## Main server

```sh
cmake -S server -B server/build
cmake --build server/build -j2
```

기본 설정은 `server/config/safety`에서 읽고, CCTV 목록·모델·H 결과는
`server/config`에서 읽는다. 다른 위치를 사용할 때는 다음처럼 명시한다.

```sh
server/build/apps/main_app/forklift_safety_server \
  --config-dir /path/to/server/config/safety \
  --common-config-dir /path/to/server/config
```

실행 중 생성되는 DB·CSV는 `server/var/main_app/storage`에 둔다.

## Main server tests

```sh
cmake -S server -B server/build
cmake --build server/build -j2
cd server/build
ctest --output-on-failure
ctest -L unit --output-on-failure
ctest -L e2e --output-on-failure
```

현재 저장소에는 실제 카메라·센서·단말을 연결하는 운영 E2E 테스트를 등록하지
않는다. 고정 fixture와 테스트 입력을 사용하는 파이프라인 통합 테스트는
`integration`으로만 분류한다. `e2e` 결과는 실제 장비 기반 테스트가 추가된 뒤
운영 지표로 사용한다.

MQTT 브로커가 필요한 업링크 통합 테스트는 기본 테스트 묶음에 포함하지 않는다.
브로커가 준비된 환경에서만 명시적으로 활성화한다. 브로커가 없으면 성공으로
처리하지 않고 실패한다.

```sh
cmake -S server -B server/build -DENABLE_NETWORK_INTEGRATION_TESTS=ON
cmake --build server/build -j2
cd server/build
ctest -L requires-mqtt --output-on-failure
```

## Homography app

처리 엔진은 별도 CMake 프로젝트다.

```sh
cmake -S server/apps/homography_app/processing \
  -B server/apps/homography_app/processing/build \
  -DBUILD_TESTING=ON
cmake --build server/apps/homography_app/processing/build -j2
server/apps/homography_app/processing/build/homography_unit_tests
```

웹 앱과 모니터링 앱의 systemd 유닛은 `server/deploy/systemd/`에 있다.
호모그래피 웹 앱의 기본 포트는 8001, 모니터링 앱의 기본 포트는 8000이다.
