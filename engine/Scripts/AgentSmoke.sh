#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

GAME_PORT="${EPICODE_AGENT_GAME_PORT:-7797}"
API_PORT="${EPICODE_AGENT_API_PORT:-8797}"
WS_PORT="${EPICODE_AGENT_WS_PORT:-8798}"
AGENT_NAME="${EPICODE_AGENT_NAME:-SmokeAgent}"
WAIT_SECONDS="${EPICODE_AGENT_WAIT:-90}"
LOG_DIR="${ENGINE_DIR}/Saved/Logs"
SERVER_LOG="${LOG_DIR}/agent-smoke-server.log"
SUMMARY_LOG="${LOG_DIR}/agent-smoke-summary.json"
SMOKE_LOG="${LOG_DIR}/agent-smoke.log"
mkdir -p "${LOG_DIR}"

SCENE_ID="$(python3 -c 'import uuid; print(str(uuid.uuid4()).upper())')"
SCENE_DIR="${ENGINE_DIR}/Saved/Epicode/Scenes/${SCENE_ID}"
mkdir -p "${SCENE_DIR}"
IDS_FILE="${SCENE_DIR}/ids.json"
python3 - "${SCENE_DIR}/scene.json" "${IDS_FILE}" "${SCENE_ID}" <<'PY'
import json
import sys
import uuid

scene_path, ids_path, scene_id = sys.argv[1:4]
character_id = str(uuid.uuid4()).upper()
camera_id = str(uuid.uuid4()).upper()
document = {
    "schemaVersion": 2,
    "sceneId": scene_id,
    "name": "Agent Smoke Scene",
    "levelPath": "/Game/Epicode/Scenes/AgentSmoke",
    "environment": {"timeOfDay": 15, "preset": "clear"},
    "entities": [
        {
            "id": character_id,
            "type": "character",
            "name": "Agent Smoke Character",
            "visible": True,
            "tags": [],
            "transform": {"location": [100, 200, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
            "character": {"skeletalMesh": "/Engine/EngineMeshes/SkeletalCube", "loop": True, "playRate": 1},
        },
        {
            "id": camera_id,
            "type": "camera",
            "name": "Agent Smoke Camera",
            "visible": True,
            "tags": [],
            "transform": {"location": [0, -400, 180], "rotation": [-10, 0, 0], "scale": [1, 1, 1]},
            "camera": {"focalLength": 35},
        },
    ],
    "runtime": {
        "agent": {
            "controllableCharacterIds": [character_id],
            "spawnPoints": [],
            "allowedActions": ["wave"],
        },
        "viewer": {
            "cameraIds": [camera_id],
            "defaultCameraId": camera_id,
            "spawnTransform": {"location": [0, -400, 180], "rotation": [-10, 0, 0], "scale": [1, 1, 1]},
            "showOverlay": True,
        },
    },
}
with open(scene_path, "w", encoding="utf-8") as handle:
    json.dump(document, handle, indent="\t")
with open(ids_path, "w", encoding="utf-8") as handle:
    json.dump({"sceneId": scene_id, "characterId": character_id, "cameraId": camera_id}, handle, indent=2)
PY
CHARACTER_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["characterId"])' "${IDS_FILE}")"

SERVER_PID=""
cleanup() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
  rm -rf "${SCENE_DIR}"
}
trap cleanup EXIT

rm -f "${SERVER_LOG}" "${SUMMARY_LOG}"
EPICODE_API_HTTP_PORT="${API_PORT}" EPICODE_API_WS_PORT="${WS_PORT}" EPICODE_API_TOKEN="agent-smoke-token" \
  EPICODE_SERVER_LOG="${SERVER_LOG}" "${SCRIPT_DIR}/Server.sh" --port "${GAME_PORT}" --scene "${SCENE_ID}" >/dev/null 2>&1 &
SERVER_PID=$!

API_TOKEN="agent-smoke-token"
READY=0
for _ in $(seq 1 "${WAIT_SECONDS}"); do
  if curl -fsS -H "Authorization: Bearer ${API_TOKEN}" "http://127.0.0.1:${API_PORT}/api/v1/health" >/dev/null 2>&1; then
    READY=1
    break
  fi
  if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "Agent smoke server exited early." >&2
    tail -80 "${SERVER_LOG}" >&2 || true
    exit 1
  fi
  sleep 1
done
if [[ "${READY}" -ne 1 ]]; then
  echo "Timed out waiting for the control API on port ${API_PORT}." >&2
  tail -80 "${SERVER_LOG}" >&2 || true
  exit 1
fi

SANDBOX_STATE="${LOG_DIR}/agent-smoke-sandbox.json"
if ! curl -fsS -X POST "http://127.0.0.1:${API_PORT}/api/v1/command" \
  -H "Authorization: Bearer ${API_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"sandbox.state","params":{}}' >"${SANDBOX_STATE}"; then
  echo "Agent smoke failed: sandbox.state is unavailable." >&2
  exit 1
fi
python3 - "${SANDBOX_STATE}" "${SCENE_ID}" <<'PY'
import json
import sys

state = json.load(open(sys.argv[1], encoding="utf-8")).get("result", {})
if state.get("sceneId") != sys.argv[2]:
    raise SystemExit(f"sandbox scene mismatch: {state.get('sceneId')}")
if not state.get("legacy"):
    raise SystemExit("scene-only agent session should use the legacy sandbox view")
if not state.get("digest") or float(state.get("tick", 0)) < 0:
    raise SystemExit("sandbox state is incomplete")
PY

if ! EPICODE_API_TOKEN="${API_TOKEN}" python3 "${SCRIPT_DIR}/AgentClient.py" \
  --base "http://127.0.0.1:${API_PORT}" \
  --name "${AGENT_NAME}" \
  --character "${CHARACTER_ID}" \
  --summary "${SUMMARY_LOG}" >>"${SMOKE_LOG}" 2>&1; then
  echo "Agent client failed. See ${SMOKE_LOG}." >&2
  tail -80 "${SUMMARY_LOG}" >&2 || true
  exit 1
fi

python3 - "${SUMMARY_LOG}" <<'PY'
import json
import sys

summary = json.load(open(sys.argv[1], encoding="utf-8"))
required = ["join", "assign", "heartbeat", "move", "stop", "release"]
missing = [key for key in required if summary.get(key) is None]
if missing:
    raise SystemExit(f"missing agent summary fields: {missing}")
state = summary.get("state", {})
if state.get("participantKind") != "agent":
    raise SystemExit(f"expected agent participant, got {state.get('participantKind')}")
if not state.get("leaseId"):
    raise SystemExit("agent state has no leaseId")
events = set(summary.get("events", []))
if "character.move.completed" not in events:
    raise SystemExit(f"missing character.move.completed in events: {sorted(events)}")
print("Epicode agent smoke passed")
PY

echo "Epicode agent smoke passed" >>"${SMOKE_LOG}"
