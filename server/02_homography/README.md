# Homography

카메라 이미지에서 ArUco 마커를 검출하고, 실제 평면 좌표와 픽셀 좌표 사이의 호모그래피를 산출·검증하는 Raspberry Pi용 도구.

## 주요 기능

- CCTV 고해상도 캡처 및 ArUco 마커 검출
- ArUco 마커 꼭짓점과 실제 마커 크기를 이용한 평면 좌표계 설정
- 불규칙하게 배치된 동일 크기 정사각형 마커 기반 호모그래피 산출
- 전체 CCTV×채널 정합 결과의 전체 맵 오버레이 검증
- 변환 행렬과 검증 지표가 포함된 결과 JSON 저장
- LAN 환경의 웹 UI/API 제공

## 구조

```text
02_homography/
├── README.md       # 전체 개요
├── app/             # Python 웹 UI/API
├── engine/          # C++ 알고리즘 라이브러리·CLI
├── config/          # 마커 격자 및 실행 설정
└── tests/           # 테스트 및 테스트 데이터
```

실행 흐름은 다음과 같음.

```text
브라우저
  ↓
app/ 웹 UI·API
  ↓
engine/ CLI 또는 homography_core
  ↓
ArUco 검출·호모그래피 계산
```

세부 문서는 다음을 참고함.

- [웹 앱 문서](app/README.md)
- [엔진 및 CLI 문서](engine/README.md)

## TODO

- 카메라 내부 파라미터와 렌즈 왜곡 계수를 별도 캘리브레이션하고, 검출 코너와 판별용 픽셀 좌표가 호모그래피에 들어가기 전에 동일한 undistort 경로를 거치도록 연결한다.
- 현재 단계에서는 렌즈 캘리브레이션 계수를 산출하거나 적용하지 않는다.
