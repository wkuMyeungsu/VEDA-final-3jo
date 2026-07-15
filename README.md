# 지게차 사각지대 충돌 방지 시스템

한화비전 PNM-C16083RVQ 4채널 카메라와 ArUco 마커 기반 좌표 정합으로
지게차 사각지대의 위험 구역을 감시하고 충돌을 사전 경고하는 시스템.

## 디렉터리 구조

```
root
├── README.md
├── .gitignore
├── server/                     # 라즈베리파이4: 이벤트 수신/큐잉, 호모그래피 좌표변환,
│   │                           # 위험판정, RTSP 수신·라우팅, zone 매핑
│   ├── CMakeLists.txt
│   ├── main.cpp                # rtp_metadata_receiver 실행 파일 진입점
│   ├── csv_logger.cpp / .hpp
│   ├── onvif_metadata_parser.cpp / .hpp
│   ├── rtp_metadata_receiver.cpp / .hpp
│   ├── test_main.cpp           # parser_test 실행 파일 진입점 (mock 데이터)
│   └── scripts/
│       └── analyze_detections.py   # CSV 결과 후처리 분석 스크립트
├── cctv/                       # 한화 카메라 SoC 온디바이스 (예정)
├── forklift-device/            # 지게차 탑재 운전자 단말 (예정)
└── operator-device/            # 관제용 PC 멀티뷰 대시보드 (예정)
```

각 트랙 디렉토리는 같은 방식(트랙명/ 하위에 소스와 빌드 설정을 둠)으로 구성되며,
`cctv/`, `forklift-device/`, `operator-device/`는 아직 비어 있고 순차적으로 추가될 예정.

## server/ 빌드 및 실행 (Windows)

### 사전 준비

- [vcpkg](https://github.com/microsoft/vcpkg)로 `pugixml` 설치 (CMake가 vcpkg 툴체인
  파일을 통해 찾음)
- [GStreamer](https://gstreamer.freedesktop.org/download/) 별도 설치 (vcpkg 패키지 아님).
  `gstreamer-1.0`, `gstreamer-app-1.0`을 pkg-config로 찾으므로, 빌드 전에
  `PKG_CONFIG_PATH` 환경변수가 GStreamer 설치본의 `pkgconfig` 폴더를 가리키도록
  설정해야 함 (예: `C:\Program Files\gstreamer\1.0\msvc_x86_64\lib\pkgconfig`)

### 사용법

```powershell
cd server
$env:PKG_CONFIG_PATH = "C:\Program Files\gstreamer\1.0\msvc_x86_64\lib\pkgconfig"
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Debug
```

빌드 결과로 실행 파일이 2개 생성됨:

- **`parser_test`** — 실제 카메라 없이 mock 데이터로 ONVIF 메타데이터 파서 로직만 검증
- **`rtp_metadata_receiver`** — 실제 카메라(RTSP)에 붙어서 ONVIF 메타데이터를 수신해
  콘솔 출력 + CSV로 로깅 (영상 녹화 기능은 없음)

```powershell
.\build\Debug\parser_test.exe

.\build\Debug\rtp_metadata_receiver.exe "rtsp://<user>:<password>@192.168.0.3:554/0/onvif/profile2/media.smp"
```

`rtp_metadata_receiver`는 RTSP URL 문자열 하나만 인자로 받는다. 파이프라인 문자열에
`protocols=tcp`가 이미 고정되어 있어 별도로 설정할 옵션이 아니다.

### 실행 팁 (Windows)

RTSP 연결 시 `giolibproxy.dll` 로드 실패로 요청마다 2~5초씩 지연될 수 있음.
아래 환경변수를 설정하면 이 지연을 피할 수 있다:

```powershell
$env:GIO_USE_PROXY_RESOLVER = "dummy"
```

### 출력물

- 콘솔에 프레임별 감지 결과 실시간 출력
- `detections_YYYYMMDD_HHMMSS.csv` (실행 시작 시각은 로컬 PC 기준 파일명에 고정되며,
  재연결이 발생해도 같은 파일에 계속 append됨)

CSV는 실행 시점의 현재 디렉터리(위 예시대로 `server\`에서 실행한 경우 `server\`)에
생성되며, `server/scripts/analyze_detections.py`로 후처리 분석할 수 있다:

```powershell
python server\scripts\analyze_detections.py server\detections_20260713_153916.csv
```

### 종료

반드시 **Ctrl+C(SIGINT)**로 종료해야 CSV가 안전하게 마무리된다. 강제 종료(창 닫기,
프로세스 kill 등) 시 마지막 행이 유실될 수 있다.

### 알려진 이슈

- `ProximateObject.Distance` 값이 실제 카메라 데이터에서 항상 `0.0`으로 옴 (단위/산출 로직 미확인)
- `CsvLogger` 소멸자에서의 flush가 모든 종료 경로에서 검증되지 않음
- CSV 파일명은 로컬 PC 시각 기준이고 프레임별 `utcTime`은 카메라 기준 시간이라 서로
  불일치할 수 있음 (정밀 동기화 시 주의)

## 협업 규칙

브랜치 전략, 커밋 메시지, 병합 규칙은 별도 문서를 따른다.

- 브랜치: `main`(발표용) / `develop`(통합) / `feature/<트랙>/<작업>`
- 커밋: Conventional Commits (`feat`, `fix`, `docs`, `chore` ...)
- 병합: PR 없이 CLI로 `develop`에 직접 merge
- 자세한 내용은 `docs/git협업규칙.md` 참고
