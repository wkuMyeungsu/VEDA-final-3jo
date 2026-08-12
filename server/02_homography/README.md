# Homography

카메라 이미지에서 ArUco 마커를 검출하고, 실제 평면 좌표와 픽셀 좌표 사이의 호모그래피를 산출·검증하는 Raspberry Pi용 도구.

## 주요 기능

- 브라우저에서 호모그래피용 마커와 보드 이미지 생성
- 촬영 이미지 업로드 및 마커 검출
- 고정 격자 또는 수동 마커 배치 기반 호모그래피 산출
- 변환 행렬, 검출 결과, 누락 마커, 재투영 오차 제공
- 검증용 오버레이와 결과 JSON 생성
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
