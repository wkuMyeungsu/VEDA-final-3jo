#!/bin/sh
# DISPLAY/XAUTHORITY를 systemd --user로 넘기고 서비스 기동 (이유는 README 참고)
systemctl --user import-environment DISPLAY XAUTHORITY        # X 세션 환경값 복사
systemctl --user start operator-terminal.service              # 실제 프로세스 기동
