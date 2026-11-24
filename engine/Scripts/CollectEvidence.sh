#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

LOG_DIR="${ENGINE_DIR}/Saved/Logs"
OUTPUT="${LOG_DIR}/acceptance-summary.json"
CHECK=0

if [[ "${1:-}" == "--check" ]]; then
  CHECK=1
fi

python3 - "${LOG_DIR}" "${OUTPUT}" "${CHECK}" <<'PY'
import json
import os
import re
import sys

log_dir, output, check_mode = sys.argv[1:4]
check_mode = check_mode == "1"


def read(path):
    if not os.path.exists(path):
        return ""
    with open(path, "rb") as handle:
        return handle.read().decode("utf-8", "ignore")


def build_summary():
    text = read(os.path.join(log_dir, "build.log"))
    if not text:
        return {"status": "not-measured", "log": "build.log"}
    if "Result: Succeeded" in text:
        status = "passed"
    elif "Result: Failed" in text:
        status = "failed"
    else:
        status = "not-measured"
    target = "not-measured"
    for line in text.splitlines():
        if line.startswith("Building "):
            target = line.split()[1].rstrip(".")
            break
    return {"status": status, "target": target, "log": "build.log"}


def test_summary():
    text = read(os.path.join(log_dir, "test.log"))
    if not text:
        return {"status": "not-measured", "passed": "not-measured", "failed": "not-measured"}
    passed = len(re.findall(r"Test Completed\. Result=\{Success\}", text))
    failed = len(re.findall(r"Test Completed\. Result=\{Fail\}", text))
    status = "passed" if not check_mode and "TEST COMPLETE. EXIT CODE: 0" in text or (passed > 0 and failed == 0) else "not-measured"
    if failed > 0:
        status = "failed"
    return {"status": status, "passed": passed, "failed": failed, "log": "test.log"}


def smoke_summary(name, log_name):
    text = read(os.path.join(log_dir, log_name))
    if not text:
        return {"status": "not-measured", "log": log_name}
    if "passed" in text.lower() or "success" in text.lower():
        return {"status": "passed", "log": log_name}
    if "failed" in text.lower():
        return {"status": "failed", "log": log_name}
    return {"status": "not-measured", "log": log_name}


def network_summary():
    path = os.path.join(log_dir, "network-smoke-summary.json")
    if not os.path.exists(path):
        return {
            "status": "not-measured",
            "sampleCount": "not-measured",
            "maxRecentDrift": "not-measured",
            "rttMs": "not-measured",
            "packetLoss": "not-measured",
            "serverMode": "not-measured",
        }
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    data["status"] = "passed" if data.get("sampleCount") not in (None, 0, "not-measured") else "not-measured"
    return data


network = network_summary()
network_smoke = smoke_summary("network", "network-smoke.log")
if network.get("status") == "passed":
    network_smoke = {"status": "passed", "log": "network-smoke-summary.json"}

summary = {
    "build": build_summary(),
    "tests": test_summary(),
    "smoke": {
        "sandbox": smoke_summary("sandbox", "sandbox-smoke.log"),
        "agent": smoke_summary("agent", "agent-smoke.log"),
        "viewer": smoke_summary("viewer", "viewer-smoke.log"),
        "replay": smoke_summary("replay", "replay-smoke.log"),
        "network": network_smoke,
    },
    "network": network,
}

if check_mode:
    errors = []
    if not os.path.exists(output):
        errors.append("acceptance summary is missing")
    else:
        with open(output, encoding="utf-8") as handle:
            existing = json.load(handle)
        for group in ("build", "tests", "smoke", "network"):
            if group not in existing:
                errors.append(f"missing group: {group}")
        if existing.get("tests", {}).get("failed") not in (0, "not-measured", None):
            errors.append("test failures are recorded")
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        raise SystemExit(1)
    print(f"acceptance summary check passed: {output}")
else:
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
    print(f"acceptance summary written: {output}")
PY

if (( CHECK == 0 )); then
  echo "Evidence sources:"
  echo "  ${LOG_DIR}/build.log"
  echo "  ${LOG_DIR}/test.log"
  echo "  ${LOG_DIR}/network-smoke-summary.json"
fi
