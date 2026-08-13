# Homography App

`app/`는 호모그래피 엔진을 브라우저에서 사용할 수 있도록 제공하는 Python
LAN 웹 UI/API임.

## 1. 제공 기능

- ArUco 마커 이미지 생성 요청
- 여러 마커 이미지 ZIP 다운로드
- 생성된 이미지 브라우저 미리보기
- 촬영 이미지 업로드
- 마커 크기 및 실제 X/Y 좌표 입력
- 호모그래피 산출 요청
- 검출 결과와 재투영 오차 확인
- 오버레이 이미지와 결과 JSON 다운로드

호모그래피 계산 자체와 CLI 사용법은 [engine 문서](../engine/README.md) 참고.

## 2. CCTV 설정

`../config/camera_config.json`에 카메라 IP, 포트, 계정, 프로필을 입력함.
비밀번호는 `camera.password`에만 입력하며 URL에 직접 넣지 않음.
서버가 입력값으로 RTSP와 스냅샷 주소를 조합함.
호모그래피 산출 화면의 채널 버튼으로 1~4번 영상을 런타임에 선택함.
백엔드는 RTSP 연결을 유지하고 `/api/camera/video`로 최신 프레임을 MJPEG로 전달함.
캡처 버튼은 수신 중인 최신 프레임만 별도 파일로 저장해 검출에 사용함.

실제 카메라 설정은 저장소에 커밋하지 않는다. 최초 설정 시 예제 파일을 복사하고
실제 환경 값으로 수정한 뒤 접근 권한을 제한한다.

```sh
cd /home/veda3/01_Workspace/server/02_homography
cp config/camera_config.example.json config/camera_config.json
chmod 600 config/camera_config.json
```

`camera_config.json`은 `.gitignore`로 보호되며, `camera_config.example.json`만
형식 안내용으로 관리한다. 비밀번호나 토큰은 예제 파일과 로그에 넣지 않는다.

## 3. 서버 실행

### 3.1 systemd 서비스로 실행

```sh
sudo systemctl start homography-app.service
sudo systemctl status homography-app.service --no-pager
```

부팅 시 자동 실행이 필요하면 한 번만 활성화함.

```sh
sudo systemctl enable homography-app.service
```

### 3.2 개발용 직접 실행

저장소의 `server/` 디렉터리에서 실행함.

```sh
cd /home/veda3/01_Workspace/server
HOMOGRAPHY_TOOL=02_homography/engine/build/homography_tool \
  python3 02_homography/app/server.py
```

## 4. 접속

### 4.1 라즈베리파이에서 접속

라즈베리파이 자체의 브라우저에서는 다음 주소 사용.

```text
http://127.0.0.1:8001
```

### 4.2 다른 PC에서 접속

같은 LAN의 다른 PC에서는 라즈베리파이 IP 주소 사용.

```text
http://192.168.0.13:8001
```

`127.0.0.1`은 접속한 장치 자신을 의미하므로, 다른 PC에서는 사용 불가.

## 5. 파일 수정 시 반영

### 5.1 Python 웹 앱 수정

```sh
sudo systemctl restart homography-app.service
sudo systemctl status homography-app.service --no-pager
curl -fsS http://127.0.0.1:8001/api/status
```

### 5.2 systemd 서비스 파일 수정

```sh
cd /home/veda3/01_Workspace/server
sudo install -m 644 \
  02_homography/app/systemd/homography-app.service \
  /etc/systemd/system/homography-app.service
sudo systemctl daemon-reload
sudo systemctl restart homography-app.service
```

### 5.3 로그 확인

```sh
journalctl -u homography-app.service -n 100 --no-pager
```

엔진 실행 파일을 수정한 경우에는 [엔진 빌드 절차](../engine/README.md)를
먼저 수행한 뒤 서비스 재시작.
