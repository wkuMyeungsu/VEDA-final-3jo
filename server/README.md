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
│   ├── homography/                  # 호모그래피 앱 정책 설정
│   ├── operational/homography/      # 생성된 운영 H
│   ├── camera_model.json
│   └── camera_list.json
├── deploy/systemd/
├── third_party/
├── build/                           # 로컬 생성물
└── var/                             # 런타임 DB·CSV·로그
```

## Main server

```sh
cmake -S server -B server/build
cmake --build server/build -j2
```

기본 설정은 `server/config/safety`에서 읽고, CCTV 목록·모델·운영 H는
`server/config`에서 읽는다. 다른 위치를 사용할 때는 다음처럼 명시한다.

```sh
server/build/apps/main_app/forklift_safety_server \
  --config-dir /path/to/server/config/safety \
  --common-config-dir /path/to/server/config
```

실행 중 생성되는 DB·CSV는 `server/var/main_app/storage`에 둔다.

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
