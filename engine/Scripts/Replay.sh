#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

TEST_FILTER="${EPICODE_REPLAY_TESTS:-Epicode.Replay}"
LOG_FILE="${EPICODE_REPLAY_LOG:-${ENGINE_DIR}/Saved/Logs/replay.log}"
EXTRA_ARGS=()

usage() {
  cat <<'EOF'
Usage: Replay.sh [options] [-- <unreal args>]

  --tests <filter>   Automation test filter (default Epicode.Replay)
  --log <path>       Log path (default Saved/Logs/replay.log)
  --                 Remaining arguments are passed to Unreal
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tests)
      TEST_FILTER="${2:?--tests requires a value}"
      shift 2
      ;;
    --log)
      LOG_FILE="${2:?--log requires a path}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      EXTRA_ARGS+=("$@")
      break
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "${TEST_FILTER}" ]]; then
  echo "Test filter must not be empty." >&2
  exit 2
fi

mkdir -p "$(dirname "${LOG_FILE}")"
echo "Replay automation: filter=${TEST_FILTER} log=${LOG_FILE}"

set +e
"${EDITOR_CMD_BIN}" "${PROJECT_FILE}" /Engine/Maps/Entry \
  -unattended -nosplash -nullrhi \
  -ExecCmds="Automation RunTests ${TEST_FILTER}; Quit" \
  -TestExit="Automation Test Queue Empty" \
  -stdout -FullStdOutLogOutput "${EXTRA_ARGS[@]}" 2>&1 | tee "${LOG_FILE}"
STATUS=${PIPESTATUS[0]}
set -e

if ! grep -aq "TEST COMPLETE. EXIT CODE: 0" "${LOG_FILE}"; then
  echo "Replay automation did not complete successfully. See ${LOG_FILE}." >&2
  exit 1
fi
if grep -aq "Result={Fail}" "${LOG_FILE}"; then
  echo "Replay automation reported failures. See ${LOG_FILE}." >&2
  exit 1
fi

echo "Replay automation passed"
exit "${STATUS}"
