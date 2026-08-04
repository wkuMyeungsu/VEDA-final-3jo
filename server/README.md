# Server

카메라의 ONVIF 메타데이터를 수신해 객체와 ArUco 마커를 파싱하고,
좌표 변환·추적·위험 판정을 거쳐 단말로 결과를 전달하는 서버이다.

## 목표 데이터 흐름

```text
카메라 RTSP 메타데이터
  -> RTP 패킷 수신 및 XML 재조립
  -> 토픽별 파싱 (객체 검출 / ArUco)
  -> 공통 타입으로 정규화
  -> 시각 동기화 및 좌표 변환
  -> 사람 추적 및 최근접 대상 선택
  -> 위험도 판정
  -> 운전자·관제 단말 전송
```

## 목표 디렉터리 구조

```text
server/
  app/        main(), 설정 로드, 모듈 조립
  input/      RTSP 수신, RTP/XML 재조립, 토픽 분기 및 파싱
  logic/      동기화, 호모그래피, 추적, 위험 판정
  common/     공통 구조체, 타입, 유틸리티
  modules/    재사용 가능한 기존 모듈
  config/     카메라·채널별 보정값과 위험 임계값
  tests/      단위 및 통합 테스트
```

현재 위 디렉터리와 최소 실행 진입점만 생성한 상태다. 기존 `detection`, `tracking`,
`judgment`, `analysis` 코드는 검증 전까지 원래 위치에 유지한다.

```bash
cmake -S server -B build/server
cmake --build build/server
./build/server/forklift_safety_server
```

## 현재 모듈

- `detection/object_detection`: RTSP 메타데이터 수신, 객체 XML 파싱, CSV 기록
- `detection/_wip_aruco_parser`: ArUco XML 파서와 독립 테스트
- `tracking`: 크로스카메라 추적과 최근접 사람 선택
- `judgment`: 위험 판정 파이프라인, 결과 송신 및 이벤트 기록
- `analysis`: 검출 CSV 분석 도구

## ArUco 입력 계약

- Topic: `tns1:OpenApp/ArUCo_Detection/MarkerDetected`
- Source: `Channel` (1부터 시작)
- Message: `UtcTime`
- Data: `MarkerCount`, `MarkerIds`, `Marker{N}Corners`

코너는 원본 픽셀 좌표 `x0,y0,x1,y1,x2,y2,x3,y3` 순서를 그대로 보존한다.
`MarkerCount=0`은 오류가 아니라 해당 시점의 미검출로 처리한다. 카메라 UTC 시각과
서버 수신 시각은 모두 기록한다.

## 구현 순서

1. 기존 모듈별 역할·의존성과 새 위치를 확정한다.
2. 객체·ArUco 토픽 분기와 공통 입력 타입 변환을 연결한다.
3. 채널별 호모그래피 설정, 좌표 변환, 오차 검증 도구를 추가한다.
4. 추적·최근접 대상 선택·위험 판정을 새 진입점에 통합한다.
5. 현장 고정 후 채널별 행렬과 위험 임계값을 실측값으로 확정한다.

호모그래피 최종값은 카메라 위치·각도·줌·해상도와 바닥 기준점이 고정된 뒤에만
산출한다. 그전에는 샘플 설정과 가상 점으로 저장·로드·변환·검증 흐름을 준비한다.
