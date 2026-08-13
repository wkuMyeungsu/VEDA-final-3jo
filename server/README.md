# Server workspace

`server/`는 라즈베리파이에 배포되는 서버 영역입니다. 기능별 앱은 서로의
설정·테스트·빌드 산출물을 공유하지 않도록 번호가 붙은 디렉터리로 분리합니다.

```text
server/
├── 01_main/
│   ├── src/       # 메인 C++ 서버
│   ├── config/    # 단말·카메라 설정
│   ├── tests/
│   └── tools/
├── 02_homography/
│   ├── app/       # 8001 웹 GUI/API
│   ├── engine/    # C++ ArUco·호모그래피 계산
│   ├── config/    # 호모그래피 전용 설정
│   └── tests/
├── 03_server_monitoring/
│   └── app/       # 8000 모니터링 화면/API
└── CMakeLists.txt
```

## Main server

```sh
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

## Homography app

```sh
cmake -S 02_homography/engine -B 02_homography/engine/build
cmake --build 02_homography/engine/build -j2
02_homography/engine/build/homography_unit_tests
```

웹 앱은 `02_homography/app/systemd/homography-app.service`를 사용하며 기본 포트는
8001입니다. 모니터링 앱은 `03_server_monitoring/app/systemd/server-monitoring.service`
를 사용하며 기본 포트는 8000입니다.
