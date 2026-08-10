# Homography App

Homography 전용 LAN 웹 앱입니다. `homography_tool`의 보드 생성,
Homography 산출, view, selftest를 실제 CLI로 호출합니다.

개발 실행:

```sh
HOMOGRAPHY_TOOL=/path/to/homography_tool \
  python3 _tools/server_admin_gui/server.py
```

브라우저에서 `http://127.0.0.1:8001` 또는 라즈베리파이의
`http://192.168.0.13:8001`으로 접속합니다.

라즈베리파이에서는 `systemd/server-admin-gui.service`를 참고해
`/etc/systemd/system/`에 설치합니다. `User`, 작업 경로, 실행 파일 경로는
실제 배포 계정과 설치 위치에 맞게 수정해야 합니다.
