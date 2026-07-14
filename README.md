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
│   ├── main.cpp
│   ├── csv_logger.cpp / .hpp
│   ├── onvif_metadata_parser.cpp / .hpp
│   ├── rtp_metadata_receiver.cpp / .hpp
│   ├── test_main.cpp
│   └── analyze_detections.py
├── cctv/                       # 한화 카메라 SoC 온디바이스 (예정)
├── forklift-device/            # 지게차 탑재 운전자 단말 (예정)
└── operator-device/            # 관제용 PC 멀티뷰 대시보드 (예정)
```

각 트랙 디렉토리는 같은 방식(트랙명/ 하위에 소스와 빌드 설정을 둠)으로 구성되며,
`cctv/`, `forklift-device/`, `operator-device/`는 아직 비어 있고 순차적으로 추가될 예정.
각 폴더의 세부 구조와 빌드 방법은 해당 폴더의 README를 참고.

## 협업 규칙

브랜치 전략, 커밋 메시지, PR 규칙은 별도 문서를 따른다.

- 브랜치: `main`(발표용) / `develop`(통합) / `feature/<트랙>/<작업>`
- 커밋: Conventional Commits (`feat`, `fix`, `docs`, `chore` ...)
- 병합: PR + 리뷰어 1명 + Squash and merge
- 자세한 내용은 `docs/git협업규칙.md` 참고