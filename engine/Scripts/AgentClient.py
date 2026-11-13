#!/usr/bin/env python3
"""Minimal Epicode Agent client using only HTTP. Never starts Unreal."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
import time
import urllib.error
import urllib.request
import uuid

ENGINE_DIR = pathlib.Path(__file__).resolve().parents[1]


class AgentError(RuntimeError):
    pass


class AgentClient:
    def __init__(self, base_url: str, token: str):
        self.base_url = base_url.rstrip("/")
        self.token = token
        self.participant_token: str | None = None
        self.player_id: str | None = None

    def call(self, method: str, params: dict | None = None, *, idempotency_key: str | None = None) -> dict:
        body: dict = {"params": params or {}}
        if idempotency_key:
            body["idempotencyKey"] = idempotency_key
        headers = {
            "Authorization": f"Bearer {self.token}",
            "Content-Type": "application/json",
            "X-Request-Id": str(uuid.uuid4()),
        }
        if self.participant_token:
            headers["X-Epicode-Participant"] = self.participant_token
        request = urllib.request.Request(
            f"{self.base_url}/api/v1/commands/{method}",
            data=json.dumps(body).encode("utf-8"),
            headers=headers,
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as error:
            raw = error.read().decode("utf-8", "replace")
            try:
                payload = json.loads(raw)
            except json.JSONDecodeError as decode_error:
                raise AgentError(f"HTTP {error.code}: {raw}") from decode_error
        if not payload.get("ok"):
            detail = payload.get("error", {})
            raise AgentError(f"{detail.get('code')}: {detail.get('message')}")
        return payload

    def join(self, display_name: str, player_id: str | None = None) -> dict:
        payload = self.call(
            "session.join",
            {
                "playerId": player_id or display_name.lower().replace(" ", "-"),
                "displayName": display_name,
                "participantKind": "agent",
            },
        )
        result = payload.get("result", {})
        self.player_id = result.get("playerId", player_id)
        self.participant_token = result.get("participantToken")
        if not self.participant_token:
            raise AgentError("session.join did not return participantToken")
        return payload

    def assign(self, character_id: str) -> dict:
        return self.call(
            "session.assignCharacter",
            {"requesterId": self.player_id, "targetPlayerId": self.player_id, "characterId": character_id},
        )

    def heartbeat(self) -> dict:
        return self.call("session.heartbeat", {})

    def state(self) -> dict:
        return self.call("character.state", {})

    def move_to(self, target: list[float]) -> dict:
        return self.call("character.moveTo", {"target": target}, idempotency_key=str(uuid.uuid4()))

    def stop(self) -> dict:
        return self.call("character.stop", {})

    def release(self) -> dict:
        return self.call("session.releaseCharacter", {"requesterId": self.player_id})

    def snapshot(self) -> dict:
        return self.call("session.snapshot", {"playerId": self.player_id})


def read_token(engine_dir: pathlib.Path) -> str:
    env_token = os.environ.get("EPICODE_API_TOKEN", "").strip()
    if env_token:
        return env_token
    path = engine_dir / "Saved" / "Epicode" / "api-token"
    if not path.exists():
        raise AgentError(f"missing API token file: {path}")
    token = path.read_text(encoding="utf-8").strip()
    if not token:
        raise AgentError(f"empty API token file: {path}")
    return token


def main() -> int:
    parser = argparse.ArgumentParser(description="Epicode Agent HTTP client")
    parser.add_argument("--base", default="http://127.0.0.1:8710")
    parser.add_argument("--name", default="Agent")
    parser.add_argument("--character", required=True)
    parser.add_argument("--target", default="300,200,0")
    parser.add_argument("--summary", default="")
    parser.add_argument("--heartbeat-seconds", type=float, default=0.0)
    parser.add_argument("--move-wait", type=float, default=2.0)
    args = parser.parse_args()

    client = AgentClient(args.base, read_token(ENGINE_DIR))
    summary: dict = {"playerId": None, "events": []}
    try:
        joined = client.join(args.name)
        summary["playerId"] = client.player_id
        summary["join"] = joined.get("result", {}).get("stateVersion")
        assigned = client.assign(args.character)
        summary["assign"] = assigned.get("result", {}).get("worldVersion", True)
        heartbeat = client.heartbeat()
        summary["heartbeat"] = heartbeat.get("result", {}).get("leaseExpiresAt")
        state = client.state()
        summary["state"] = {
            "characterId": state.get("result", {}).get("characterId"),
            "participantKind": state.get("result", {}).get("participantKind"),
            "leaseId": state.get("result", {}).get("leaseId"),
            "stateVersion": state.get("result", {}).get("stateVersion"),
        }
        target = [float(value) for value in args.target.split(",")]
        if len(target) != 3:
            raise AgentError("--target must be x,y,z")
        moved = client.move_to(target)
        summary["move"] = moved.get("result", {}).get("stateVersion")
        if args.move_wait > 0:
            time.sleep(args.move_wait)
        if args.heartbeat_seconds > 0:
            deadline = time.time() + args.heartbeat_seconds
            while time.time() < deadline:
                client.heartbeat()
                time.sleep(min(1.0, max(0.1, args.heartbeat_seconds / 5.0)))
        stopped = client.stop()
        summary["stop"] = stopped.get("result", {}).get("stateVersion")
        released = client.release()
        summary["release"] = released.get("result", {}).get("worldVersion", True)
        snapshot = client.snapshot()
        summary["events"] = [
            event.get("type")
            for event in snapshot.get("result", {}).get("events", [])
            if isinstance(event, dict)
        ]
        print(json.dumps(summary, indent=2, sort_keys=True))
        if args.summary:
            pathlib.Path(args.summary).write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
        return 0
    except AgentError as error:
        print(f"agent error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
