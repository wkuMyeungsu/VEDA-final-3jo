#!/usr/bin/env bash
set -euo pipefail

# 하위 호환용 진입점. 실제 설치 절차는 인증서·세 앱까지 포함한 스크립트에서 관리한다.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/install-server.sh" "$@"
