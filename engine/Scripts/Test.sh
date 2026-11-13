#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

mkdir -p "${ENGINE_DIR}/Saved/Logs"
LOG_FILE="${ENGINE_DIR}/Saved/Logs/test.log"
echo "Running Epicode automation tests"
set +e
"${EDITOR_CMD_BIN}" "${PROJECT_FILE}" /Engine/Maps/Entry \
  -unattended -nosplash -nullrhi \
  -ExecCmds="Automation RunTests Epicode; Quit" \
  -TestExit="Automation Test Queue Empty" \
  -stdout -FullStdOutLogOutput 2>&1 | tee "${LOG_FILE}"
STATUS=${PIPESTATUS[0]}
set -e
if grep -q "TEST COMPLETE. EXIT CODE: 0" "${LOG_FILE}" && ! grep -q "Result={Fail}" "${LOG_FILE}"; then
  STATUS=0
fi
echo "Test log: ${LOG_FILE}"
exit "${STATUS}"
