#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

LOG_FILE="${ENGINE_DIR}/Saved/Logs/replay-smoke.log"
SUMMARY_FILE="${ENGINE_DIR}/Saved/Logs/replay-smoke-summary.txt"
mkdir -p "$(dirname "${LOG_FILE}")"
rm -f "${LOG_FILE}" "${SUMMARY_FILE}"

TESTS=(
  "Epicode.Replay.Journal"
  "Epicode.Replay.Snapshot"
  "Epicode.Replay.Ghost"
  "Epicode.Replay.Resim"
  "Epicode.Replay.Branch"
  "Epicode.Replay.Retention"
  "Epicode.Replay.Job"
)

echo "Replay smoke: journal, ghost, resim, branch, cleanup and jobs"
if ! EPICODE_REPLAY_LOG="${LOG_FILE}" "${SCRIPT_DIR}/Replay.sh" --tests "Epicode.Replay" >>"${LOG_FILE}.driver" 2>&1; then
  echo "Replay smoke failed: Replay.sh returned non-zero." >&2
  tail -120 "${LOG_FILE}.driver" >&2 || true
  exit 1
fi

for test_name in "${TESTS[@]}"; do
  if ! grep -aq "Path={${test_name}}" "${LOG_FILE}"; then
    echo "Replay smoke failed: missing ${test_name}." >&2
    exit 1
  fi
  if ! grep -aq "Result={Success}.*Path={${test_name}}" "${LOG_FILE}"; then
    echo "Replay smoke failed: ${test_name} did not succeed." >&2
    exit 1
  fi
done

if ! grep -aq "TEST COMPLETE. EXIT CODE: 0" "${LOG_FILE}"; then
  echo "Replay smoke failed: automation did not complete." >&2
  exit 1
fi

{
  echo "Epicode replay smoke passed"
  echo "log=${LOG_FILE}"
  echo "journal=create/append/checksum"
  echo "events=sequence/retention"
  echo "replay=ghost/resim/snapshot"
  echo "branch=isolated-fork"
  echo "cleanup=retention"
  echo "job=queue/status"
  for test_name in "${TESTS[@]}"; do
    echo "test=${test_name}"
  done
} | tee "${SUMMARY_FILE}"

echo "Replay smoke log: ${LOG_FILE}"
