# operator_terminal 부팅 자동 실행 (systemd 사용자 서비스)

`operator_terminal`을 라즈베리파이 부팅 후 데스크톱 환경 위에서 자동 실행하고,
비정상 종료 시 재시작·`journalctl` 로그 확인이 가능하도록 systemd **사용자
서비스**로 관리하는 구성임. 데스크톱 환경(LXDE/lightdm autologin) 자체는
수정하지 않음 — 같은 보드/이미지를 공유하는 다른 사용자에게 영향 없음.

## 구성 요소와 이유

| 파일 | 역할 |
|---|---|
| `operator-terminal.service` | 실제 프로세스 관리 유닛 (재시작 정책, 로그) |
| `operator-terminal-launch.sh` | DISPLAY/XAUTHORITY를 systemd --user에 전달 후 서비스 기동 |
| `operator-terminal-autostart.desktop` | 위 스크립트를 데스크톱 세션 시작 시 자동 실행 (XDG 자동시작) |

**서비스를 `[Install] WantedBy=default.target`으로 부팅 시 바로 기동시키지
않는 이유**: `systemd --user` 매니저(`user@<uid>.service`)는 로그인 시점에
이미 떠 있는데, 이 시점은 X 서버가 뜨기 전이라 `DISPLAY` 환경변수를 아직 모름.
이 상태에서 자동 기동하면 Qt 앱은 항상 "화면 연결 불가"로 즉시 실패함.

대신 데스크톱 세션이 완전히 뜬 뒤(XDG 자동시작 시점, 이때는 `DISPLAY`가 이미
유효함) `operator-terminal-launch.sh`가 `systemctl --user import-environment`로
그 값을 systemd --user 환경에 복사해 넣고, 그다음 서비스를 `start`함. 이후
크래시로 인한 재시작은 이미 올바른 환경을 물려받으므로 별도 조치 불필요.

## 설치 (라즈베리파이에서 직접 실행)

```bash
# 1. 유닛/스크립트 배치
mkdir -p ~/.config/systemd/user ~/.config/autostart
cp ~/VEDA_Final_project/forklift-device/qt/deploy/systemd/operator-terminal.service \
   ~/.config/systemd/user/
cp ~/VEDA_Final_project/forklift-device/qt/deploy/systemd/operator-terminal-autostart.desktop \
   ~/.config/autostart/
chmod +x ~/VEDA_Final_project/forklift-device/qt/deploy/systemd/operator-terminal-launch.sh

# 2. 경로 확인/수정 (클론 위치가 ~/VEDA_Final_project가 아닌 경우에만)
#    - operator-terminal.service: WorkingDirectory / ExecStart (%h 자동 치환)
#    - operator-terminal-autostart.desktop: Exec
#      .desktop 스펙상 Exec은 물결표/환경변수 미확장 -> sh -c로 감싸 $HOME 사용
#      절대경로 하드코딩 시 사용자명이 다른 보드에서 실패, 오류 로그도 없음
#      (2026-08-12 /home/pi 하드코딩으로 발생, 원인 파악에 시간 소요)

# 3. 빌드 산출물 위치 확인 (README.md 기준 build/operator_terminal)
ls ~/VEDA_Final_project/forklift-device/qt/build/operator_terminal

# 4. 유닛 등록
systemctl --user daemon-reload
```

이후 재부팅해서 데스크톱이 자동으로 뜨면, autostart 항목이 실행되어
`operator_terminal`이 자동으로 기동됨.

**ExecStart 옵션**: `--demo`(데모 패널), `--camera <id>`(시작 카메라),
`--config <dir>`(설정 디렉터리 강제 지정) 조정 가능 — 상위
[forklift-device/README.md](../../../README.md#1-라즈베리파이-실기기--linux-빌드-및-실행) 참고. `--config`를

안 주면 실행 파일 옆 `config/`를 읽으므로 `WorkingDirectory` 설정과 무관하게
항상 올바른 경로를 찾음 (`ConfigLoader`가 CWD가 아니라
`QCoreApplication::applicationDirPath()` 기준으로 계산, [main.cpp:43-45](../../apps/operator_terminal/main.cpp#L43-L45)).

재부팅 없이 지금 세션에서 바로 테스트하려면:

```bash
~/VEDA_Final_project/forklift-device/qt/deploy/systemd/operator-terminal-launch.sh
```

## 확인 방법

```bash
# 상태 확인
systemctl --user status operator-terminal.service

# 실시간 로그 (Qt qCInfo/qCWarning 포함 stdout/stderr가 모두 여기로 감)
journalctl --user -u operator-terminal.service -f

# 재시작 테스트 (크래시 상황 재현)
systemctl --user kill --signal=SIGKILL operator-terminal.service
# -> RestartSec(3초) 후 자동 재기동되는지 journalctl로 확인
```

## 부팅 직후 네트워크 미연결 상태 확인

`HandoverClient`(TCP, 9001)와 `RiskEventSource`(MQTT)는 최초 연결 시도 실패를
이후의 연결 끊김과 **동일한 경로**로 처리함 — 별도 크래시나 행(hang) 없이
`Disconnected` 상태로 표시되고 지수 백오프(3초→최대 30초)로 자동 재시도함.

- `HandoverClient::handleError` (소켓 연결 실패 포함 모든 `errorOccurred`) →
  `scheduleReconnect()` 호출 ([HandoverClient.cpp:67-72](../../common/network/HandoverClient.cpp#L67-L72))
- `RiskEventSource`는 `mosquitto_connect_async` + `mosquitto_loop_start`로
  접속을 라이브러리 내부 스레드에 위임하고, `mosquitto_reconnect_delay_set`으로
  백오프를 등록해둠 ([RiskEventSource.cpp:60-72](../../common/network/RiskEventSource.cpp#L60-L72)) —
  최초 접속 실패도 이후 재연결과 같은 백오프 로직을 탐

즉, 유닛에 `After=network-online.target` 같은 네트워크 대기 조건을 걸 필요는
없음 (라즈베리파이 OS 기본 `dhcpcd`는 `network-online.target`을 신뢰성 있게
보장하지 않기도 하고, 앱 자체가 이미 감내하도록 설계됨). 단, 코드 분석 기반
결론이므로 **실제 재부팅 후 `journalctl`로 "connect failed" → 재시도 →
연결 성공 로그가 정상적으로 찍히는지 한 번은 직접 확인 권장**.

## 문제 해결

- **"화면 연결 불가" 계속 실패**: `operator-terminal-launch.sh`가 실행됐는지
  확인 (`~/.config/autostart/`에 있는지, 데스크톱 세션 안에서 실행됐는지).
  `systemctl --user show-environment | grep DISPLAY`로 값이 들어갔는지 확인.
- **공유 라이브러리 로드 실패 로그**: Qt/GStreamer를 apt 표준 경로가 아닌
  별도 prefix에 설치한 경우 `operator-terminal.service`에
  `Environment=LD_LIBRARY_PATH=...` 추가 필요.
- **`systemctl --user` 명령이 "Failed to connect to bus" 오류**: SSH로
  접속해 테스트하는 경우, 실제 로그인 세션의 버스에 접속하려면
  `export XDG_RUNTIME_DIR=/run/user/$(id -u)` 필요 (autostart 경로에서는
  세션 안에서 실행되므로 해당 없음).
- **재시작 루프가 5회 만에 멈춤(failed)**: `StartLimitBurst` 초과.
  `systemctl --user reset-failed operator-terminal.service` 후 원인(로그) 확인.
