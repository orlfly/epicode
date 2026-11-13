#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

GAME_PORT="${EPICODE_VIEWER_GAME_PORT:-7798}"
API_PORT="${EPICODE_VIEWER_API_PORT:-8799}"
WS_PORT="${EPICODE_VIEWER_WS_PORT:-8800}"
VIEWER_NAME="${EPICODE_VIEWER_NAME:-SmokeViewer}"
WAIT_SECONDS="${EPICODE_VIEWER_WAIT:-90}"
LOG_DIR="${ENGINE_DIR}/Saved/Logs"
SERVER_LOG="${LOG_DIR}/viewer-smoke-server.log"
VIEWER_LOG="${LOG_DIR}/viewer-smoke.log"
SUMMARY_LOG="${LOG_DIR}/viewer-smoke-summary.log"
API_TOKEN="viewer-smoke-token"
mkdir -p "${LOG_DIR}"

SCENE_ID="$(python3 -c 'import uuid; print(str(uuid.uuid4()).upper())')"
SCENE_DIR="${ENGINE_DIR}/Saved/Epicode/Scenes/${SCENE_ID}"
mkdir -p "${SCENE_DIR}"
python3 - "${SCENE_DIR}/scene.json" "${SCENE_ID}" <<'PY'
import json
import sys
import uuid

scene_path, scene_id = sys.argv[1:3]
character_id = str(uuid.uuid4()).upper()
camera_id = str(uuid.uuid4()).upper()
document = {
    "schemaVersion": 2,
    "sceneId": scene_id,
    "name": "Viewer Smoke Scene",
    "levelPath": "/Game/Epicode/Scenes/ViewerSmoke",
    "environment": {"timeOfDay": 18, "preset": "sunset"},
    "entities": [
        {
            "id": character_id,
            "type": "character",
            "name": "Viewer Smoke Character",
            "visible": True,
            "tags": [],
            "transform": {"location": [0, 0, 0], "rotation": [0, 90, 0], "scale": [1, 1, 1]},
            "character": {"skeletalMesh": "/Engine/EngineMeshes/SkeletalCube", "loop": True, "playRate": 1},
        },
        {
            "id": camera_id,
            "type": "camera",
            "name": "Viewer Smoke Camera",
            "visible": True,
            "tags": [],
            "transform": {"location": [0, -450, 200], "rotation": [-10, 0, 0], "scale": [1, 1, 1]},
            "camera": {"focalLength": 35},
        },
    ],
    "runtime": {
        "viewer": {
            "cameraIds": [camera_id],
            "defaultCameraId": camera_id,
            "spawnTransform": {"location": [0, -450, 200], "rotation": [-10, 0, 0], "scale": [1, 1, 1]},
            "showOverlay": True,
        },
    },
}
with open(scene_path, "w", encoding="utf-8") as handle:
    json.dump(document, handle, indent="\t")
PY

SERVER_PID=""
VIEWER_PID=""
VIEWER2_PID=""
cleanup() {
  for pid in "${VIEWER2_PID}" "${VIEWER_PID}" "${SERVER_PID}"; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
    fi
  done
  for pid in "${VIEWER2_PID}" "${VIEWER_PID}" "${SERVER_PID}"; do
    if [[ -n "${pid}" ]]; then
      wait "${pid}" 2>/dev/null || true
    fi
  done
  rm -rf "${SCENE_DIR}"
}
trap cleanup EXIT

rm -f "${SERVER_LOG}" "${VIEWER_LOG}" "${SUMMARY_LOG}"
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
    echo "Viewer smoke server exited early." >&2
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

SANDBOX_STATE="${LOG_DIR}/viewer-smoke-sandbox.json"
if ! curl -fsS -X POST "http://127.0.0.1:${API_PORT}/api/v1/command" \
  -H "Authorization: Bearer ${API_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"sandbox.state","params":{}}' >"${SANDBOX_STATE}"; then
  echo "Viewer smoke failed: sandbox.state is unavailable." >&2
  exit 1
fi
python3 - "${SANDBOX_STATE}" "${SCENE_ID}" <<'PY'
import json
import sys

state = json.load(open(sys.argv[1], encoding="utf-8")).get("result", {})
if state.get("sceneId") != sys.argv[2]:
    raise SystemExit(f"sandbox scene mismatch: {state.get('sceneId')}")
if not state.get("legacy"):
    raise SystemExit("scene-only viewer session should use the legacy sandbox view")
if not state.get("digest"):
    raise SystemExit("sandbox state has no digest")
PY

TOKEN_FILE="${ENGINE_DIR}/Saved/Epicode/viewer-token"
VIEWER_TOKEN=""
for _ in $(seq 1 30); do
  if [[ -f "${TOKEN_FILE}" ]]; then
    VIEWER_TOKEN="$(tr -d '[:space:]' < "${TOKEN_FILE}")"
    [[ -n "${VIEWER_TOKEN}" ]] && break
  fi
  sleep 1
done
if [[ -z "${VIEWER_TOKEN}" ]]; then
  echo "Viewer token was not generated at ${TOKEN_FILE}." >&2
  exit 1
fi

EPICODE_VIEWER_LOG="${VIEWER_LOG}" "${SCRIPT_DIR}/Viewer.sh" \
  --server "127.0.0.1:${GAME_PORT}" \
  --name "${VIEWER_NAME}" \
  --token "${VIEWER_TOKEN}" \
  --camera free \
  --res 1280x720 \
  -- -nullrhi -unattended -nosplash >/dev/null 2>&1 &
VIEWER_PID=$!

VIEWER_READY=0
for _ in $(seq 1 "${WAIT_SECONDS}"); do
  if grep -aq "Viewer '.*' registered" "${SERVER_LOG}" 2>/dev/null; then
    VIEWER_READY=1
    break
  fi
  if ! kill -0 "${VIEWER_PID}" 2>/dev/null; then
    break
  fi
  sleep 1
done
if [[ "${VIEWER_READY}" -ne 1 ]]; then
  echo "Viewer did not register. See ${VIEWER_LOG}." >&2
  tail -120 "${VIEWER_LOG}" >&2 || true
  exit 1
fi

EPICODE_VIEWER_LOG="${LOG_DIR}/viewer-smoke-second.log" "${SCRIPT_DIR}/Viewer.sh" \
  --server "127.0.0.1:${GAME_PORT}" \
  --name "${VIEWER_NAME}Two" \
  --token "${VIEWER_TOKEN}" \
  --camera fixed \
  --res 960x540 \
  -- -nullrhi -unattended -nosplash >/dev/null 2>&1 &
VIEWER2_PID=$!

snapshot_request() {
  curl -fsS -H "Authorization: Bearer ${API_TOKEN}" \
    "http://127.0.0.1:${API_PORT}/api/v1/commands/session.snapshot" \
    -H "Content-Type: application/json" \
    -d '{"params":{}}'
}

VIEWERS_READY=0
for _ in $(seq 1 "${WAIT_SECONDS}"); do
  snapshot_request >"${SUMMARY_LOG}" 2>/dev/null || true
  COUNT="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1],encoding="utf-8")).get("result",{}).get("viewerCount",0))' "${SUMMARY_LOG}" 2>/dev/null || echo 0)"
  if [[ "${COUNT}" -ge 2 ]]; then
    VIEWERS_READY=1
    break
  fi
  if ! kill -0 "${VIEWER2_PID}" 2>/dev/null; then
    break
  fi
  sleep 1
done
if [[ "${VIEWERS_READY}" -ne 1 ]]; then
  echo "Second viewer did not register." >&2
  tail -80 "${LOG_DIR}/viewer-smoke-second.log" >&2 || true
  exit 1
fi

python3 - "${SUMMARY_LOG}" <<'PY'
import json
import sys

result = json.load(open(sys.argv[1], encoding="utf-8")).get("result", {})
if result.get("viewerCount") != 2:
    raise SystemExit(f"expected two viewers, got {result.get('viewerCount')}")
cameras = result.get("viewerCameras", [])
if len(cameras) != 2:
    raise SystemExit(f"expected two viewer camera summaries, got {len(cameras)}")
for player in result.get("players", []):
    if player.get("participantKind") == "viewer" and player.get("characterId"):
        raise SystemExit("viewer unexpectedly owns a character")
PY

kill "${VIEWER2_PID}" 2>/dev/null || true
wait "${VIEWER2_PID}" 2>/dev/null || true
VIEWER2_PID=""

for _ in $(seq 1 30); do
  snapshot_request >"${SUMMARY_LOG}" 2>/dev/null || true
  COUNT="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1],encoding="utf-8")).get("result",{}).get("viewerCount",0))' "${SUMMARY_LOG}" 2>/dev/null || echo 0)"
  [[ "${COUNT}" -eq 1 ]] && break
  sleep 1
done

curl -fsS -H "Authorization: Bearer ${API_TOKEN}" \
  "http://127.0.0.1:${API_PORT}/api/v1/commands/session.snapshot" \
  -H "Content-Type: application/json" \
  -d '{"params":{}}' >"${SUMMARY_LOG}"

python3 - "${SUMMARY_LOG}" "${VIEWER_NAME}" <<'PY'
import json
import sys

payload = json.load(open(sys.argv[1], encoding="utf-8"))
result = payload.get("result", {})
if result.get("viewerCount") != 1:
    raise SystemExit(f"expected one viewer, got {result.get('viewerCount')}")
players = result.get("players", [])
viewer = next((player for player in players if player.get("participantKind") == "viewer"), None)
if not viewer:
    raise SystemExit("viewer participant missing from snapshot")
if viewer.get("characterId"):
    raise SystemExit("viewer unexpectedly owns a character")
if not viewer.get("viewerCameraMode"):
    raise SystemExit("viewer camera summary missing")
cameras = result.get("viewerCameras", [])
if len(cameras) != 1:
    raise SystemExit(f"expected one viewer camera summary, got {len(cameras)}")
scene = result.get("scene")
if not isinstance(scene, dict):
    raise SystemExit("scene summary missing from snapshot")
if len(scene.get("cameras", [])) != 1:
    raise SystemExit("scene camera missing from snapshot")
if len(scene.get("characters", [])) != 1:
    raise SystemExit("scene character missing from snapshot")
if not isinstance(result.get("environment"), dict):
    raise SystemExit("environment missing from snapshot")
if "items" not in result or "builds" not in result:
    raise SystemExit("world state items/builds missing from snapshot")
print("Epicode viewer smoke passed")
PY

BASELINE_RESULT="${LOG_DIR}/viewer-smoke-baseline.json"
LO_RX_BEFORE="$(cat /sys/class/net/lo/statistics/rx_bytes 2>/dev/null || echo 0)"
LO_TX_BEFORE="$(cat /sys/class/net/lo/statistics/tx_bytes 2>/dev/null || echo 0)"
sleep 3
LO_RX_AFTER="$(cat /sys/class/net/lo/statistics/rx_bytes 2>/dev/null || echo 0)"
LO_TX_AFTER="$(cat /sys/class/net/lo/statistics/tx_bytes 2>/dev/null || echo 0)"
python3 - "${SUMMARY_LOG}" "${BASELINE_RESULT}" "${LO_RX_BEFORE}" "${LO_RX_AFTER}" "${LO_TX_BEFORE}" "${LO_TX_AFTER}" <<'PY'
import json
import sys

snapshot = json.load(open(sys.argv[1], encoding="utf-8")).get("result", {})
rx = (int(sys.argv[4]) - int(sys.argv[3])) / 3.0 / 1024.0
tx = (int(sys.argv[6]) - int(sys.argv[5])) / 3.0 / 1024.0
baseline = {
    "sceneEntities": len(snapshot.get("scene", {}).get("entityIds", [])),
    "sceneCharacters": len(snapshot.get("scene", {}).get("characters", [])),
    "sceneCameras": len(snapshot.get("scene", {}).get("cameras", [])),
    "runtimeItems": len(snapshot.get("items", [])),
    "runtimeBuilds": len(snapshot.get("builds", [])),
    "viewers": snapshot.get("viewerCount", 0),
    "characterUpdateFrequencyHz": 30,
    "eventSequence": snapshot.get("eventSequence", 0),
    "loopbackRxKiBPerSecond": round(rx, 1),
    "loopbackTxKiBPerSecond": round(tx, 1),
}
json.dump(baseline, open(sys.argv[2], "w", encoding="utf-8"), indent=2)
print(json.dumps(baseline, sort_keys=True))
PY
python3 - "${BASELINE_RESULT}" "${VIEWER_LOG}" <<'PY'
import json
import sys

baseline = json.load(open(sys.argv[1], encoding="utf-8"))
line = "Epicode viewer baseline: " + " ".join(f"{key}={value}" for key, value in sorted(baseline.items()))
with open(sys.argv[2], "a", encoding="utf-8") as handle:
    handle.write(line + "\n")
print(line)
PY

END_RESULT="${LOG_DIR}/viewer-smoke-end.json"
curl -fsS -H "Authorization: Bearer ${API_TOKEN}" \
  "http://127.0.0.1:${API_PORT}/api/v1/commands/session.end" \
  -H "Content-Type: application/json" \
  -d '{"params":{}}' >"${END_RESULT}"
python3 - "${END_RESULT}" <<'PY'
import json
import sys

result = json.load(open(sys.argv[1], encoding="utf-8")).get("result", {})
if result.get("state") != "ended":
    raise SystemExit(f"expected ended session, got {result.get('state')}")
PY

echo "Epicode viewer smoke passed" >>"${VIEWER_LOG}"
