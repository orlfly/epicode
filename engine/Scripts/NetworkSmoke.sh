#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

SCENE_ID="${EPICODE_SMOKE_SCENE:-}"
PORT="${EPICODE_SMOKE_PORT:-7796}"
API_PORT="${EPICODE_SMOKE_API_PORT:-8794}"
WS_PORT="${EPICODE_SMOKE_WS_PORT:-8793}"
API_TOKEN="network-smoke-token"
PLAYER_A="${EPICODE_SMOKE_PLAYER_A:-Alice}"
PLAYER_B="${EPICODE_SMOKE_PLAYER_B:-Bob}"
WAIT_SECONDS="${EPICODE_SMOKE_WAIT:-60}"
LOG_DIR="${ENGINE_DIR}/Saved/Logs"
SERVER_LOG="${LOG_DIR}/network-smoke-server.log"
CLIENT_A_LOG="${LOG_DIR}/network-smoke-client-a.log"
CLIENT_B_LOG="${LOG_DIR}/network-smoke-client-b.log"
SUMMARY_LOG="${LOG_DIR}/network-smoke.log"
SCENE_CREATED=0
SCENE_DIR=""
mkdir -p "${LOG_DIR}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scene)
      SCENE_ID="${2:?--scene requires a value}"
      shift 2
      ;;
    --port)
      PORT="${2:?--port requires a value}"
      shift 2
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ -z "${SCENE_ID}" ]]; then
  SCENE_ID="$(python3 -c 'import uuid; print(str(uuid.uuid4()).upper())')"
  SCENE_DIR="${ENGINE_DIR}/Saved/Epicode/Scenes/${SCENE_ID}"
  mkdir -p "${SCENE_DIR}"
  python3 - "${SCENE_DIR}/scene.json" "${SCENE_ID}" <<'PY'
import json
import sys
import uuid

path, scene_id = sys.argv[1:3]
document = {
    "schemaVersion": 1,
    "sceneId": scene_id,
    "name": "Network Smoke Scene",
    "levelPath": "/Game/Epicode/Scenes/NetworkSmoke",
    "environment": {"timeOfDay": 15, "preset": "clear"},
    "entities": [
        {
            "id": str(uuid.uuid4()).upper(),
            "type": "character",
            "name": "Network Smoke Character",
            "visible": True,
            "tags": [],
            "transform": {"location": [100, 200, 0], "rotation": [0, 90, 0], "scale": [1, 1, 1]},
            "character": {"skeletalMesh": "/Engine/EngineMeshes/SkeletalCube", "loop": True, "playRate": 1},
        },
        {
            "id": str(uuid.uuid4()).upper(),
            "type": "character",
            "name": "Network Smoke Character B",
            "visible": True,
            "tags": [],
            "transform": {"location": [-100, -200, 0], "rotation": [0, -90, 0], "scale": [1, 1, 1]},
            "character": {"skeletalMesh": "/Engine/EngineMeshes/SkeletalCube", "loop": True, "playRate": 1},
        }
    ],
}
with open(path, "w", encoding="utf-8") as handle:
    json.dump(document, handle, indent="\t")
PY
  SCENE_CREATED=1
fi

rm -f "${SERVER_LOG}" "${CLIENT_A_LOG}" "${CLIENT_B_LOG}" "${SUMMARY_LOG}"
SERVER_PID=""
CLIENT_A_PID=""
CLIENT_B_PID=""

cleanup() {
  for pid in "${CLIENT_A_PID}" "${CLIENT_B_PID}" "${SERVER_PID}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
    fi
  done
  for pid in "${CLIENT_A_PID}" "${CLIENT_B_PID}" "${SERVER_PID}"; do
    if [[ -n "${pid}" ]]; then
      wait "${pid}" 2>/dev/null || true
    fi
  done
  if [[ "${SCENE_CREATED}" -eq 1 && -n "${SCENE_DIR}" ]]; then
    rm -rf "${SCENE_DIR}"
  fi
}
trap cleanup EXIT

wait_for_log() {
  local pattern="$1"
  local file="$2"
  local label="$3"
  for _ in $(seq 1 "${WAIT_SECONDS}"); do
    if python3 - "${file}" "${pattern}" <<'PY'
import os
import sys

path, pattern = sys.argv[1:3]
if not os.path.exists(path):
    raise SystemExit(1)
with open(path, "rb") as handle:
    text = handle.read().decode("utf-8", "ignore")
raise SystemExit(0 if pattern in text else 1)
PY
    then
      return 0
    fi
    sleep 1
  done
  echo "Timed out waiting for ${label}: ${pattern}" >&2
  return 1
}

echo "Network smoke: scene=${SCENE_ID} port=${PORT} players=${PLAYER_A},${PLAYER_B}"
EPICODE_API_HTTP_PORT="${API_PORT}" EPICODE_API_WS_PORT="${WS_PORT}" EPICODE_API_TOKEN="${API_TOKEN}" \
  EPICODE_SERVER_LOG="${SERVER_LOG}" "${SCRIPT_DIR}/Server.sh" --port "${PORT}" --scene "${SCENE_ID}" -- -EpicodeSmokeMove -EpicodeSmokePerformance -EpicodeSmokeWorld > /dev/null 2>&1 &
SERVER_PID=$!

if ! wait_for_log "Session created for scene" "${SERVER_LOG}" "server session"; then
  tail -80 "${SERVER_LOG}" >&2 || true
  exit 1
fi
SESSION_ID=""
for _ in $(seq 1 20); do
  SESSION_ID="$(grep -a "Session created for scene" "${SERVER_LOG}" | tail -1 | sed -n "s/.*as '\([^']*\)'.*/\1/p")"
  [[ ${#SESSION_ID} -eq 36 ]] && break
  sleep 0.5
done
if [[ ${#SESSION_ID} -ne 36 ]]; then
  echo "Unable to parse session id from server log." >&2
  exit 1
fi
SNAPSHOT="${ENGINE_DIR}/Saved/Epicode/Sessions/${SESSION_ID}/session.json"

API_READY=0
for _ in $(seq 1 "${WAIT_SECONDS}"); do
  if curl -fsS -H "Authorization: Bearer ${API_TOKEN}" "http://127.0.0.1:${API_PORT}/api/v1/health" >/dev/null 2>&1; then
    API_READY=1
    break
  fi
  sleep 1
done
if [[ "${API_READY}" -ne 1 ]]; then
  echo "Network smoke failed: control API did not start on ${API_PORT}." >&2
  tail -80 "${SERVER_LOG}" >&2 || true
  exit 1
fi
if ! curl -fsS -X POST "http://127.0.0.1:${API_PORT}/api/v1/command" \
  -H "Authorization: Bearer ${API_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"sandbox.state","params":{}}' >"${LOG_DIR}/network-smoke-sandbox.json"; then
  echo "Network smoke failed: sandbox.state is unavailable." >&2
  exit 1
fi
python3 - "${LOG_DIR}/network-smoke-sandbox.json" "${SCENE_ID}" <<'PY'
import json
import sys

state = json.load(open(sys.argv[1], encoding="utf-8")).get("result", {})
if state.get("sceneId") != sys.argv[2]:
    raise SystemExit(f"sandbox scene mismatch: {state.get('sceneId')}")
if not state.get("legacy") or not state.get("digest"):
    raise SystemExit("scene-only network session did not expose a legacy sandbox digest")
PY

EPICODE_CLIENT_LOG="${CLIENT_A_LOG}" "${SCRIPT_DIR}/Client.sh" --server "127.0.0.1:${PORT}" --name "${PLAYER_A}" -nullrhi -unattended -nosplash > /dev/null 2>&1 &
CLIENT_A_PID=$!
EPICODE_CONNECT_CMD="open 127.0.0.1:${PORT}; stat net" EPICODE_CLIENT_LOG="${CLIENT_B_LOG}" "${SCRIPT_DIR}/Client.sh" --server "127.0.0.1:${PORT}" --name "${PLAYER_B}" -nullrhi -unattended -nosplash > /dev/null 2>&1 &
CLIENT_B_PID=$!

READY=0
for _ in $(seq 1 "${WAIT_SECONDS}"); do
  if [[ -f "${SNAPSHOT}" ]] && python3 - "${SNAPSHOT}" "${PLAYER_A}" "${PLAYER_B}" >/dev/null 2>&1 <<'PY'
import json
import sys

path, player_a, player_b = sys.argv[1:4]
with open(path, encoding="utf-8") as handle:
    snapshot = json.load(handle)
players = {player["playerId"]: player for player in snapshot.get("players", [])}
characters = {character["playerId"]: character["entityId"] for character in snapshot.get("characters", [])}
for name in (player_a, player_b):
    if name not in players or not players[name].get("characterId"):
        raise SystemExit(1)
    if characters.get(name) != players[name]["characterId"]:
        raise SystemExit(1)
PY
  then
    READY=1
    break
  fi
  sleep 1
done
if [[ "${READY}" -ne 1 ]]; then
  echo "Timed out waiting for both players, characters and possessions." >&2
  python3 - "${SNAPSHOT}" "${PLAYER_A}" "${PLAYER_B}" <<'PY' || true
import json
import sys
path, player_a, player_b = sys.argv[1:4]
with open(path, encoding="utf-8") as handle:
    snapshot = json.load(handle)
players = {player["playerId"]: player for player in snapshot.get("players", [])}
characters = {character["playerId"]: character["entityId"] for character in snapshot.get("characters", [])}
print(f"players: {sorted(players)}")
print(f"characters: {characters}")
PY
  grep -a "possesses pawn" "${SERVER_LOG}" >&2 || true
  exit 1
fi

# Spawn mapping and appearance must be recorded for both performers.
for player in "${PLAYER_A}" "${PLAYER_B}"; do
  if ! wait_for_log "Spawn mapping player='${player}'" "${SERVER_LOG}" "spawn mapping for ${player}"; then
    echo "Server did not record spawn mapping for ${player}" >&2
    tail -80 "${SERVER_LOG}" >&2 || true
    exit 1
  fi
done
if [[ "${SCENE_CREATED}" -eq 1 ]]; then
  for player in "${PLAYER_A}" "${PLAYER_B}"; do
    if ! grep -aq "Spawn mapping player='${player}'.*scene=1.*appearance='/Engine/EngineMeshes/SkeletalCube'" "${SERVER_LOG}"; then
      echo "Server did not apply the scene spawn and appearance for ${player}" >&2
      grep -a "Spawn mapping" "${SERVER_LOG}" >&2 || true
      exit 1
    fi
  done
fi
for player in "${PLAYER_A}" "${PLAYER_B}"; do
  if ! wait_for_log "Performance smoke player='${player}' action='wave' emotion='happy' intensity=0.80 viseme='preset'" "${SERVER_LOG}" "performance smoke for ${player}"; then
    echo "Server did not apply performance state for ${player}" >&2
    grep -a "Performance smoke" "${SERVER_LOG}" >&2 || true
    exit 1
  fi
done
for player in "${PLAYER_A}" "${PLAYER_B}"; do
  if ! wait_for_log "World smoke player='${player}'" "${SERVER_LOG}" "world smoke for ${player}"; then
    echo "Server did not apply item, craft and build state for ${player}" >&2
    grep -a "World smoke" "${SERVER_LOG}" >&2 || true
    exit 1
  fi
done
for client_log in "${CLIENT_A_LOG}" "${CLIENT_B_LOG}"; do
  if ! wait_for_log "Remote interpolation character=" "${client_log}" "remote interpolation log"; then
    echo "Client did not record the remote interpolation path: ${client_log}" >&2
    exit 1
  fi
done
for player in "${PLAYER_A}" "${PLAYER_B}"; do
  if ! wait_for_log "Smoke movement player='${player}' step=6" "${SERVER_LOG}" "smoke movement for ${player}"; then
    echo "Server did not complete the movement sweep for ${player}" >&2
    exit 1
  fi
done
sleep 3
DRIFT_SUMMARY=""
if ! DRIFT_SUMMARY="$(python3 - "${SERVER_LOG}" <<'PY'
import re
import sys

text = open(sys.argv[1], "rb").read().decode("utf-8", "ignore")
distances = [float(value) for value in re.findall(r"Remote drift .* distance=([0-9.]+)", text)]
if not distances:
    raise SystemExit(1)
recent = distances[-10:]
if max(recent) > 300.0:
    print(f"remote drift too large: {max(recent):.2f}", file=sys.stderr)
    raise SystemExit(1)
print(f"remote drift samples={len(distances)} max_recent={max(recent):.2f}")
PY
)"
then
  echo "Remote characters drifted beyond the interpolation budget." >&2
  grep -a "Remote drift" "${SERVER_LOG}" | tail -10 >&2 || true
  exit 1
fi
echo "${DRIFT_SUMMARY}"

PERFORMANCE_SUMMARY=""
if ! PERFORMANCE_SUMMARY="$(python3 - "${SNAPSHOT}" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    snapshot = json.load(handle)
events = snapshot.get("performanceEvents", [])
types = {event.get("type") for event in events}
required = {"performance.action", "performance.expression", "performance.voice"}
if len(events) < 6 or not required.issubset(types):
    print(f"performance events missing: count={len(events)} types={sorted(types)}", file=sys.stderr)
    raise SystemExit(1)
print(f"performance events={len(events)} types={sorted(types)}")
PY
)"
then
  echo "Session snapshot does not contain the expected performance events." >&2
  exit 1
fi
echo "${PERFORMANCE_SUMMARY}"

WORLD_SUMMARY=""
if ! WORLD_SUMMARY="$(python3 - "${SNAPSHOT}" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    snapshot = json.load(handle)
events = snapshot.get("worldEvents", [])
types = {event.get("type") for event in events}
required = {"item.spawned", "item.pickedUp", "craft.started", "build.confirmed"}
if len(events) < 8 or not required.issubset(types):
    print(f"world events missing: count={len(events)} types={sorted(types)}", file=sys.stderr)
    raise SystemExit(1)
print(f"world events={len(events)} types={sorted(types)}")
PY
)"
then
  echo "Session snapshot does not contain the expected world events." >&2
  exit 1
fi
echo "${WORLD_SUMMARY}"

# Let the authoritative character tick record its runtime transform, then
# disconnect and reconnect Bob inside the grace period.
sleep 3
kill "${CLIENT_B_PID}" 2>/dev/null || true
wait "${CLIENT_B_PID}" 2>/dev/null || true
CLIENT_B_PID=""
if ! wait_for_log "Player '${PLAYER_B}' disconnected from session" "${SERVER_LOG}" "reconnect grace"; then
  tail -80 "${SERVER_LOG}" >&2 || true
  exit 1
fi
EPICODE_CLIENT_LOG="${CLIENT_B_LOG}" "${SCRIPT_DIR}/Client.sh" --server "127.0.0.1:${PORT}" --name "${PLAYER_B}" -nullrhi -unattended -nosplash > /dev/null 2>&1 &
CLIENT_B_PID=$!
RECONNECTED=0
for _ in $(seq 1 "${WAIT_SECONDS}"); do
  if grep -aq "Spawn mapping player='${PLAYER_B}'.*restored=1" "${SERVER_LOG}"; then
    RECONNECTED=1
    break
  fi
  sleep 1
done
if [[ "${RECONNECTED}" -ne 1 ]]; then
  echo "Server did not restore ${PLAYER_B} inside the reconnect grace period." >&2
  grep -a "Spawn mapping\|reconnect\|disconnected" "${SERVER_LOG}" >&2 || true
  exit 1
fi
if [[ "${SCENE_CREATED}" -eq 1 ]]; then
  RECONNECT_LINE="$(grep -a "Spawn mapping player='${PLAYER_B}'.*restored=1" "${SERVER_LOG}" | tail -1)"
  if [[ "${RECONNECT_LINE}" != *"appearance='/Engine/EngineMeshes/SkeletalCube'"* ]]; then
    echo "Reconnect did not restore the scene character appearance." >&2
    echo "${RECONNECT_LINE}" >&2
    exit 1
  fi
fi

for pid in "${CLIENT_A_PID}" "${CLIENT_B_PID}" "${SERVER_PID}"; do
  if [[ -n "${pid}" ]]; then
    kill "${pid}" 2>/dev/null || true
  fi
done
for pid in "${CLIENT_A_PID}" "${CLIENT_B_PID}" "${SERVER_PID}"; do
  if [[ -n "${pid}" ]]; then
    wait "${pid}" 2>/dev/null || true
  fi
done
CLIENT_A_PID=""
CLIENT_B_PID=""
SERVER_PID=""
sleep 2

for player in "${PLAYER_A}" "${PLAYER_B}"; do
  if ! grep -aqE "Player '${player}'.*possesses pawn" "${SERVER_LOG}"; then
    echo "Server did not record possession for ${player}" >&2
    tail -80 "${SERVER_LOG}" >&2 || true
    exit 1
  fi
done

SNAPSHOT_SUMMARY="$(python3 - "${SNAPSHOT}" "${PLAYER_A}" "${PLAYER_B}" <<'PY'
import json
import sys

path, player_a, player_b = sys.argv[1:4]
with open(path, encoding="utf-8") as handle:
    snapshot = json.load(handle)
players = {player["playerId"]: player for player in snapshot.get("players", [])}
characters = {character["playerId"]: character["entityId"] for character in snapshot.get("characters", [])}
print(f"snapshot players: {sorted(players)}")
print(f"snapshot characters: {characters}")
PY
)"
echo "${SNAPSHOT_SUMMARY}"

{
  echo "Epicode network smoke"
  echo "scene=${SCENE_ID}"
  echo "session=${SESSION_ID}"
  echo "port=${PORT}"
  echo "players=${PLAYER_A},${PLAYER_B}"
  echo "${DRIFT_SUMMARY}"
  echo "${PERFORMANCE_SUMMARY}"
  echo "${WORLD_SUMMARY}"
  echo "${SNAPSHOT_SUMMARY}"
  grep -a "possesses pawn" "${SERVER_LOG}" || true
  grep -a "Spawn mapping" "${SERVER_LOG}" || true
  grep -a "disconnected from session" "${SERVER_LOG}" || true
  grep -a "Remote interpolation" "${CLIENT_A_LOG}" "${CLIENT_B_LOG}" | tail -4 || true
  grep -a "ExecCmds=.*stat net" "${CLIENT_B_LOG}" | head -1 || true
  grep -a "Performance smoke" "${SERVER_LOG}" || true
  grep -a "World smoke" "${SERVER_LOG}" || true
} | tee "${SUMMARY_LOG}"

echo "Epicode network smoke passed"
