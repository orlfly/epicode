#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

GAME_PORT="${EPICODE_SANDBOX_GAME_PORT:-7795}"
if [[ -n "${EPICODE_SANDBOX_API_PORT:-}" && -n "${EPICODE_SANDBOX_WS_PORT:-}" ]]; then
  API_PORT="${EPICODE_SANDBOX_API_PORT}"
  WS_PORT="${EPICODE_SANDBOX_WS_PORT}"
else
  read -r API_PORT WS_PORT <<<"$(python3 - <<'PY'
import socket

ports = []
for _ in range(2):
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    ports.append(sock.getsockname()[1])
    sock.close()
print(ports[0], ports[1])
PY
)"
fi
WAIT_SECONDS="${EPICODE_SANDBOX_WAIT:-90}"
LOG_DIR="${ENGINE_DIR}/Saved/Logs"
SERVER_LOG="${LOG_DIR}/sandbox-smoke-server.log"
SUMMARY_LOG="${LOG_DIR}/sandbox-smoke.log"
LOG_FILE="${LOG_DIR}/sandbox-smoke-run.log"
API_TOKEN="sandbox-smoke-token"
PRODUCTION_ID="SandboxSmoke-$(python3 -c 'import uuid; print(uuid.uuid4().hex[:8])')"
SCENE_ID="$(python3 -c 'import uuid; print(str(uuid.uuid4()).upper())')"
SCENE_DIR="${ENGINE_DIR}/Saved/Epicode/Scenes/${SCENE_ID}"
PRODUCTION_DIR="${ENGINE_DIR}/Saved/Epicode/Productions/${PRODUCTION_ID}"
mkdir -p "${LOG_DIR}" "${SCENE_DIR}" "${PRODUCTION_DIR}"
rm -f "${SERVER_LOG}" "${SUMMARY_LOG}" "${LOG_FILE}"

SERVER_PID=""
cleanup() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
  rm -rf "${SCENE_DIR}" "${PRODUCTION_DIR}"
}
trap cleanup EXIT

python3 - "${SCENE_DIR}/scene.json" "${PRODUCTION_DIR}/production.json" "${SCENE_ID}" "${PRODUCTION_ID}" <<'PY'
import json
import sys
import uuid

scene_path, production_path, scene_id, production_id = sys.argv[1:5]
performer_id = str(uuid.uuid4()).upper()
terminal_id = str(uuid.uuid4()).upper()
node_id = str(uuid.uuid4()).upper()
guard_id = str(uuid.uuid4()).upper()
camera_id = str(uuid.uuid4()).upper()

scene = {
    "schemaVersion": 2,
    "sceneId": scene_id,
    "name": "Sandbox Smoke Scene",
    "levelPath": "/Game/Epicode/Scenes/SandboxSmoke",
    "environment": {"timeOfDay": 12, "preset": "clear"},
    "entities": [
        {
            "id": performer_id,
            "type": "character",
            "name": "Sandbox Performer",
            "visible": True,
            "tags": ["hero"],
            "transform": {"location": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
            "character": {"skeletalMesh": "/Engine/EngineMeshes/SkeletalCube", "loop": True, "playRate": 1},
        },
        {
            "id": terminal_id,
            "type": "prop",
            "name": "Sandbox Terminal",
            "visible": True,
            "tags": [],
            "transform": {"location": [50, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
            "prop": {"mesh": "/Engine/BasicShapes/Cube"},
        },
        {
            "id": node_id,
            "type": "prop",
            "name": "Metal Node",
            "visible": True,
            "tags": [],
            "transform": {"location": [60, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
            "prop": {"mesh": "/Engine/BasicShapes/Cube"},
        },
        {
            "id": guard_id,
            "type": "character",
            "name": "Sandbox Guard",
            "visible": True,
            "tags": [],
            "transform": {"location": [200, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]},
            "character": {
                "skeletalMesh": "/Engine/EngineMeshes/SkeletalCube",
                "npcProfileId": "guard",
                "loop": True,
                "playRate": 1,
            },
        },
        {
            "id": camera_id,
            "type": "camera",
            "name": "Sandbox Camera",
            "visible": True,
            "tags": [],
            "transform": {"location": [0, -400, 180], "rotation": [-10, 0, 0], "scale": [1, 1, 1]},
            "camera": {"focalLength": 35},
        },
    ],
}
with open(scene_path, "w", encoding="utf-8") as handle:
    json.dump(scene, handle, indent="\t")

production = {
    "schemaVersion": 1,
    "productionId": production_id,
    "name": "Sandbox Smoke Production",
    "sceneId": scene_id,
    "cast": [
        {
            "slot": "performer",
            "entityId": performer_id,
            "participantKind": "performer",
        }
    ],
    "worldRules": {
        "interactionPoints": [
            {
                "id": "terminal-use",
                "entityId": terminal_id,
                "verbs": ["use"],
                "maxDistance": 300,
                "cooldownSeconds": 0,
                "conditions": [],
                "outcomes": ["setVariable:alarmLevel=1", "addItem:keycard=1"],
            }
        ],
        "zones": [
            {"id": "yard", "name": "Yard", "maxEntities": 20, "center": [0, 0, 0], "extent": [400, 400, 100]}
        ],
        "spawnTables": [
            {"id": "props", "entries": [{"entityType": "Prop", "archetypeId": "Crate", "weight": 1}]}
        ],
        "resources": [
            {"id": "metal-node", "entityId": node_id, "itemId": "metal", "capacity": 4, "respawnSeconds": 30}
        ],
        "npcProfiles": [
            {
                "id": "guard",
                "behavior": "patrol",
                "perceptionRadius": 300,
                "speed": 100,
                "targetTag": "hero",
                "patrolPoints": [{"location": [200, 0, 0]}, {"location": [220, 0, 0]}],
            }
        ],
        "recipes": [{"id": "make-key", "inputs": {"metal": 2}, "outputs": {"key": 1}}],
        "budgets": {"maxEntities": 100, "maxEventsPerSecond": 200, "maxSpawnPerSecond": 10, "maxDirectorCues": 20},
    },
    "narrative": {
        "entryBeatId": "beat-arrival",
        "variables": {"alarmLevel": 0, "hasKeycard": False},
        "beats": [
            {
                "id": "beat-arrival",
                "name": "Arrival",
                "entryConditions": ["true"],
                "exitConditions": ["alarmLevel >= 1"],
                "objectives": [],
                "cues": [],
                "next": ["beat-alarm"],
            },
            {
                "id": "beat-alarm",
                "name": "Alarm",
                "entryConditions": ["alarmLevel >= 1"],
                "exitConditions": [],
                "objectives": ["objective-alarm"],
                "cues": ["cue-lights"],
                "next": [],
            },
        ],
        "objectives": [
            {"id": "objective-alarm", "description": "Confirm the alarm", "condition": "alarmLevel >= 1"}
        ],
        "cues": [
            {"id": "cue-lights", "trigger": "beat", "beatId": "beat-alarm", "actions": ["environment.lights"], "once": True}
        ],
    },
    "cameraPresets": [{"id": "preset-main", "name": "Main", "cameraId": camera_id, "settings": {}}],
}
with open(production_path, "w", encoding="utf-8") as handle:
    json.dump(production, handle, indent="\t")
print(f"{performer_id} {terminal_id} {node_id} {guard_id} {camera_id}")
PY

read -r PERFORMER_ID TERMINAL_ID NODE_ID GUARD_ID CAMERA_ID <<<"$(python3 - "${SCENE_DIR}/scene.json" <<'PY'
import json
import sys

scene = json.load(open(sys.argv[1], encoding="utf-8"))
print(" ".join(entity["id"] for entity in scene["entities"]))
PY
)"

EPICODE_API_HTTP_PORT="${API_PORT}" EPICODE_API_WS_PORT="${WS_PORT}" EPICODE_API_TOKEN="${API_TOKEN}" \
  EPICODE_SERVER_LOG="${SERVER_LOG}" "${SCRIPT_DIR}/Server.sh" --port "${GAME_PORT}" --scene "${SCENE_ID}" >/dev/null 2>&1 &
SERVER_PID=$!

READY=0
for _ in $(seq 1 "${WAIT_SECONDS}"); do
  if curl -fsS -H "Authorization: Bearer ${API_TOKEN}" "http://127.0.0.1:${API_PORT}/api/v1/health" >/dev/null 2>&1; then
    READY=1
    break
  fi
  if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "Sandbox smoke server exited early." >&2
    tail -100 "${SERVER_LOG}" >&2 || true
    exit 1
  fi
  sleep 1
done
if [[ "${READY}" -ne 1 ]]; then
  echo "Timed out waiting for the control API on port ${API_PORT}." >&2
  tail -100 "${SERVER_LOG}" >&2 || true
  exit 1
fi

api() {
  local method="$1"
  local params="$2"
  local output="$3"
  curl -sS -X POST "http://127.0.0.1:${API_PORT}/api/v1/command" \
    -H "Authorization: Bearer ${API_TOKEN}" \
    -H "Content-Type: application/json" \
    -d "{\"method\":\"${method}\",\"params\":${params}}" >"${output}"
  python3 - "${output}" "${method}" <<'PY'
import json
import sys

path, method = sys.argv[1:3]
payload = json.load(open(path, encoding="utf-8"))
if not payload.get("ok"):
    error = payload.get("error", {})
    raise SystemExit(f"{method} failed: {error.get('code')}: {error.get('message')}")
PY
}

wait_sandbox() {
  for _ in $(seq 1 "${WAIT_SECONDS}"); do
    if api sandbox.state '{}' "${SUMMARY_LOG}.state" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done
  return 1
}
if ! wait_sandbox; then
  echo "Sandbox bridge did not become ready." >&2
  tail -100 "${SERVER_LOG}" >&2 || true
  exit 1
fi

OWNER_ID="$(python3 - "${API_PORT}" "${API_TOKEN}" <<'PY'
import json
import sys
import urllib.request

port, token = sys.argv[1:3]
request = urllib.request.Request(
    f"http://127.0.0.1:{port}/api/v1/commands/session.snapshot",
    data=b'{"params":{}}',
    headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
)
payload = json.load(urllib.request.urlopen(request))
players = payload.get("result", {}).get("players", [])
owner = next((player for player in players if player.get("role") == "owner"), None)
if not owner:
    raise SystemExit("session snapshot has no owner player")
print(owner["playerId"])
PY
)"

api production.list '{}' "${SUMMARY_LOG}.production-list"
python3 - "${SUMMARY_LOG}.production-list" "${PRODUCTION_ID}" <<'PY'
import json
import sys

payload = json.load(open(sys.argv[1], encoding="utf-8"))
production_id = sys.argv[2]
ids = {item.get("productionId") for item in payload.get("result", {}).get("productions", [])}
if production_id not in ids:
    raise SystemExit(f"production {production_id} missing from list: {sorted(ids)}")
PY

api production.open "{\"productionId\":\"${PRODUCTION_ID}\"}" "${SUMMARY_LOG}.production-open"
python3 - "${SUMMARY_LOG}.production-open" "${PRODUCTION_ID}" "${SCENE_ID}" <<'PY'
import json
import sys

payload = json.load(open(sys.argv[1], encoding="utf-8"))
production_id, scene_id = sys.argv[2:4]
result = payload.get("result", {})
if result.get("productionId") != production_id or result.get("sceneId") != scene_id:
    raise SystemExit("production.open returned the wrong identifiers")
narrative = result.get("narrative", {})
if not narrative.get("cues"):
    raise SystemExit("production.open did not expose director cues")
PY

api production.validate "{\"productionId\":\"${PRODUCTION_ID}\"}" "${SUMMARY_LOG}.production-validate"
python3 - "${SUMMARY_LOG}.production-validate" <<'PY'
import json
import sys

payload = json.load(open(sys.argv[1], encoding="utf-8"))
if not payload.get("ok"):
    raise SystemExit(f"production.validate failed: {payload.get('error')}")
PY

api sandbox.rules.get '{}' "${SUMMARY_LOG}.rules"
python3 - "${SUMMARY_LOG}.rules" <<'PY'
import json
import sys

rules = json.load(open(sys.argv[1], encoding="utf-8")).get("result", {})
for key in ("interactionPoints", "resources", "npcProfiles", "recipes"):
    if not rules.get(key):
        raise SystemExit(f"sandbox rules missing {key}")
PY

api sandbox.interact "{\"playerId\":\"performer\",\"entityId\":\"${TERMINAL_ID}\",\"verb\":\"use\"}" "${SUMMARY_LOG}.interact"
api sandbox.collect "{\"playerId\":\"performer\",\"entityId\":\"${NODE_ID}\",\"amount\":3}" "${SUMMARY_LOG}.collect"
api sandbox.craft '{"playerId":"performer","recipeId":"make-key"}' "${SUMMARY_LOG}.craft"
api sandbox.spawn "{\"actorId\":\"${OWNER_ID}\",\"kind\":\"prop\",\"archetypeId\":\"SandboxCrate\",\"location\":[10,10,0]}" "${SUMMARY_LOG}.spawn"
SPAWNED_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1],encoding="utf-8"))["result"]["id"])' "${SUMMARY_LOG}.spawn")"

api sandbox.state '{}' "${SUMMARY_LOG}.state"
python3 - "${SUMMARY_LOG}.state" "${PRODUCTION_ID}" "${PERFORMER_ID}" "${GUARD_ID}" "${SPAWNED_ID}" <<'PY'
import json
import sys

payload = json.load(open(sys.argv[1], encoding="utf-8"))
production_id, performer_id, guard_id, spawned_id = sys.argv[2:6]
result = payload.get("result", {})
if result.get("productionId") != production_id:
    raise SystemExit(f"sandbox production mismatch: {result.get('productionId')}")
if not result.get("replayable") or result.get("legacy"):
    raise SystemExit("sandbox state is not a replayable production session")
narrative = result.get("narrative", {})
if narrative.get("variables", {}).get("alarmLevel") != 1:
    raise SystemExit(f"interaction did not set alarmLevel: {narrative.get('variables')}")
entities = {entity.get("id"): entity for entity in result.get("entities", [])}
performer = entities.get(performer_id, {})
inventory = performer.get("inventory", {})
if inventory.get("metal", 0) < 1 or inventory.get("key", 0) < 1 or inventory.get("keycard", 0) < 1:
    raise SystemExit(f"collect/craft did not fill inventory: {inventory}")
if guard_id not in entities:
    raise SystemExit("NPC character missing from sandbox state")
if entities.get(guard_id, {}).get("data", {}).get("npcProfileId") != "guard":
    raise SystemExit("NPC profile was not projected into the sandbox entity")
if spawned_id not in entities:
    raise SystemExit("sandbox.spawn did not create the entity")
PY

api director.status '{}' "${SUMMARY_LOG}.director-status"
python3 - "${SUMMARY_LOG}.director-status" <<'PY'
import json
import sys

result = json.load(open(sys.argv[1], encoding="utf-8")).get("result", {})
if result.get("beatId") != "beat-arrival":
    raise SystemExit(f"unexpected entry beat: {result.get('beatId')}")
PY

api director.setObjective "{\"actorId\":\"${OWNER_ID}\",\"objectiveId\":\"objective-alarm\",\"idempotencyKey\":\"sandbox-objective\"}" "${SUMMARY_LOG}.director-objective"
api director.enterBeat "{\"actorId\":\"${OWNER_ID}\",\"beatId\":\"beat-alarm\",\"idempotencyKey\":\"sandbox-beat\"}" "${SUMMARY_LOG}.director-beat"
api director.setTimeScale "{\"actorId\":\"${OWNER_ID}\",\"scale\":1.5,\"idempotencyKey\":\"sandbox-scale\"}" "${SUMMARY_LOG}.director-scale"
api director.pause "{\"actorId\":\"${OWNER_ID}\",\"idempotencyKey\":\"sandbox-pause\"}" "${SUMMARY_LOG}.director-pause"
api director.pause "{\"actorId\":\"${OWNER_ID}\",\"idempotencyKey\":\"sandbox-pause\"}" "${SUMMARY_LOG}.director-pause-repeat"
api director.resume "{\"actorId\":\"${OWNER_ID}\",\"idempotencyKey\":\"sandbox-resume\"}" "${SUMMARY_LOG}.director-resume"
api director.status '{}' "${SUMMARY_LOG}.director-status-final"
python3 - "${SUMMARY_LOG}.director-status-final" "${SUMMARY_LOG}.director-pause-repeat" <<'PY'
import json
import sys

status = json.load(open(sys.argv[1], encoding="utf-8")).get("result", {})
if status.get("beatId") != "beat-alarm":
    raise SystemExit(f"director did not enter beat-alarm: {status.get('beatId')}")
if status.get("objectiveId") != "objective-alarm":
    raise SystemExit(f"director objective missing: {status.get('objectiveId')}")
if abs(float(status.get("timeScale", 0)) - 1.5) > 1e-6:
    raise SystemExit(f"director time scale missing: {status.get('timeScale')}")
if status.get("paused"):
    raise SystemExit("director still paused after resume")
repeat = json.load(open(sys.argv[2], encoding="utf-8")).get("result", {})
if not repeat.get("duplicate"):
    raise SystemExit("duplicate director command was not marked duplicate")
PY

api director.setView "{\"actorId\":\"${OWNER_ID}\",\"mode\":0,\"idempotencyKey\":\"sandbox-view\"}" "${SUMMARY_LOG}.director-view"

api production.delete "{\"productionId\":\"${PRODUCTION_ID}\"}" "${SUMMARY_LOG}.production-delete" || true
api session.end '{}' "${SUMMARY_LOG}.session-end" >/dev/null 2>&1 || true
kill "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true
SERVER_PID=""

{
  echo "Epicode sandbox smoke"
  echo "production=${PRODUCTION_ID}"
  echo "scene=${SCENE_ID}"
  echo "owner=${OWNER_ID}"
  echo "performer=${PERFORMER_ID}"
  echo "guard=${GUARD_ID}"
  echo "camera=${CAMERA_ID}"
  echo "spawned=${SPAWNED_ID}"
  echo "interaction=terminal-use"
  echo "resource=metal-node"
  echo "recipe=make-key"
  echo "npcProfile=guard"
  echo "directorBeat=beat-alarm"
  echo "directorObjective=objective-alarm"
  echo "directorCue=cue-lights"
  echo "Epicode sandbox smoke passed"
} | tee "${SUMMARY_LOG}" >/dev/null
echo "Epicode sandbox smoke passed"
echo "Sandbox smoke log: ${SUMMARY_LOG}"
