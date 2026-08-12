# Homography App

카메라 이미지에서 ArUco 마커를 이용해 평면 호모그래피를 산출하고, 결과를 검증하는 LAN 웹 앱.

## 1. 제공 기능

### 1.1 ArUco 마커 이미지 생성

- ID별 마커 이미지 생성
- 마커 크기와 흰색 여백 설정
- PNG 또는 SVG 출력
- 여러 ID를 ZIP으로 묶어 다운로드
- 브라우저에서 전체 이미지 미리보기

### 1.2 수동 배치 호모그래피 산출

- 촬영 이미지 업로드
- 원점 `+` 표시 지정
- X축·Y축 방향 표시
- 마커 한 변의 실제 크기 입력
- 마커 ID별 실제 `X/Y(mm)` 좌표 입력
- OpenCV ArUco 자동 검출
- 마커 네 모서리 전체를 이용한 호모그래피 계산

### 1.3 결과 산출

호모그래피 계산 후 다음 결과를 제공.

- 픽셀 좌표 → 실제 평면 좌표 변환 행렬
- 실제 평면 좌표 → 픽셀 좌표 변환 행렬
- 검출된 마커 ID
- 사용된 마커 ID
- 누락된 마커 ID
- 재투영 오차(RMSE)
- 검증용 오버레이 이미지
- 결과 JSON 다운로드

### 1.4 검증 기능

- OpenCV 기반 ArUco 검출 확인
- 호모그래피 재투영 오차 확인
- 결과 이미지 위에 검출 모서리와 ID 표시
- CLI selftest 제공

## 2. 서버 실행

### 2.1 systemd로 실행

`homography-app.service`를 통해 서버를 실행한다.

```sh
sudo systemctl start homography-app.service
sudo systemctl status homography-app.service --no-pager
```

서버가 부팅할 때 자동으로 실행되어야 한다면 다음을 한 번만 실행한다.

```sh
sudo systemctl enable homography-app.service
```

### 2.2 개발용 직접 실행

```sh
cd server/02_homography/app
HOMOGRAPHY_TOOL=../build/homography_tool \
  python3 server.py
```

## 3. 접속

### 3.1 라즈베리파이에서 접속

```text
http://127.0.0.1:8001
```

### 3.2 다른 PC에서 접속

```text
http://192.168.0.13:8001
```

## 4. 호모그래피 툴 빌드 및 테스트

```sh
cmake -S server/02_homography/tool -B server/02_homography/build
cmake --build server/02_homography/build -j2
server/02_homography/build/homography_tool selftest --verbose
```

## 5. 파일 수정 시 반영 절차

작업 디렉터리:

```sh
cd /home/veda3/01_Workspace/server
```

### 5.1 Python/API 또는 화면 수정

```sh
sudo systemctl restart homography-app.service
sudo systemctl status homography-app.service --no-pager
curl -fsS http://127.0.0.1:8001/api/status
```

### 5.2 C++ 호모그래피 툴 수정

```sh
cmake -S 02_homography/tool -B 02_homography/build
cmake --build 02_homography/build -j2
sudo systemctl restart homography-app.service
```

### 5.3 systemd 설정 수정

```sh
sudo install -m 644 \
  02_homography/app/systemd/homography-app.service \
  /etc/systemd/system/homography-app.service
sudo systemctl daemon-reload
sudo systemctl restart homography-app.service
```

### 5.4 로그 확인

```sh
journalctl -u homography-app.service -n 100 --no-pager
```
