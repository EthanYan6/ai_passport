#!/usr/bin/env python3
"""TRAE hook client for the pc-monitor "TRAE battle page".

TRAE hooks run this script with one JSON payload on stdin (config installed by
tools/install_hooks.py). The script forwards the event to the bridge's TRAE
monitor over loopback TCP (127.0.0.1:8765 by default) and, for RunCommand
pre-tool-use events, blocks until the badge decides::

    allow  -> stdout {"hookSpecificOutput": {... "permissionDecision": "allow"}}
    deny   -> ... "permissionDecision": "deny"
    timeout / bridge down -> ... "permissionDecision": "ask"
              (TRAE shows its native confirmation dialog)

Other events only drive the badge's status display; they always print nothing
and exit 0 so TRAE proceeds normally even without the badge.

Usage (as installed by install_hooks.py, not meant to be run by hand):
    python trae_hook.py   # stdin: hook JSON, stdout: decision JSON (if any)
"""

from __future__ import annotations

import json
import os
import socket
import sys
import time

TRAE_TCP_HOST = "127.0.0.1"
TRAE_TCP_PORT = int(os.environ.get("PCMON_TRAE_PORT") or 8765)
CONNECT_TIMEOUT_S = 3.0
DECISION_WAIT_S = 110.0  # < the hook's 120s budget, < the badge's 120s TTL

_HOOK_LOG = os.path.join(
    os.environ.get("LOCALAPPDATA") or os.path.expanduser("~"),
    "FoloToy",
    "trae-hook.log",
)


def _log(event: str, note: str = "") -> None:
    """Append one line per invocation so 'did TRAE call me?' is checkable."""
    try:
        os.makedirs(os.path.dirname(_HOOK_LOG), exist_ok=True)
        with open(_HOOK_LOG, "a", encoding="utf-8") as fh:
            fh.write(f"{time.strftime('%H:%M:%S')} {event} {note}\n")
    except OSError:
        pass


def _decision_json(decision: str, reason: str) -> str:
    return json.dumps(
        {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": decision,
                "permissionDecisionReason": reason,
            }
        }
    )


def _ask(badge_reason: str) -> int:
    """Fall back to TRAE's native confirmation dialog."""
    sys.stdout.write(_decision_json("ask", badge_reason) + "\n")
    return 0


def _send(req: dict) -> dict | None:
    """Send one event to the monitor, return its JSON reply or None."""
    try:
        with socket.create_connection(
            (TRAE_TCP_HOST, TRAE_TCP_PORT), timeout=CONNECT_TIMEOUT_S
        ) as sock:
            sock.settimeout(DECISION_WAIT_S + 5.0)
            sock.sendall((json.dumps(req, ensure_ascii=False) + "\n").encode("utf-8"))
            reader = sock.makefile("rb")
            line = reader.readline(65536)
    except OSError:
        return None
    try:
        reply = json.loads(line.decode("utf-8", "ignore"))
    except ValueError:
        return None
    return reply if isinstance(reply, dict) else None


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return 0
    if not isinstance(payload, dict):
        return 0

    event = str(payload.get("hook_event_name") or "")
    tool = str(payload.get("tool_name") or "")
    if not event:
        _log("(no event)", "keys=" + ",".join(sorted(payload))[:120])
        return 0
    _log(event, f"tool={tool}")

    req: dict = {"event": event, "tool": tool}

    if event == "PreToolUse" and tool == "RunCommand":
        tool_input = payload.get("tool_input") or {}
        if isinstance(tool_input, dict):
            req["cmd"] = str(
                tool_input.get("command") or tool_input.get("cmd") or ""
            )
        req["wait"] = DECISION_WAIT_S
        reply = _send(req)
        if reply is None:
            return _ask("badge bridge not reachable")
        decision = str(reply.get("decision") or "timeout")
        if decision == "allow":
            sys.stdout.write(_decision_json("allow", "approved on the badge") + "\n")
        elif decision == "deny":
            sys.stdout.write(_decision_json("deny", "denied on the badge") + "\n")
        else:
            return _ask("badge did not answer in time")
        return 0

    if event == "PostToolUse" and tool == "RunCommand":
        response = payload.get("tool_response")
        exit_code = None
        if isinstance(response, dict):
            for key in ("exit_code", "exitCode", "returncode", "code"):
                if key in response:
                    exit_code = response.get(key)
                    break
        elif isinstance(response, int):
            exit_code = response
        req["exit"] = exit_code if isinstance(exit_code, int) else 0
        _send(req)
        return 0

    # State-only events: UserPromptSubmit, PreToolUse(Edit|Write), Stop, ...
    req["cmd"] = ""
    _send(req)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception:
        # A hook must never wedge TRAE: any unexpected failure just proceeds.
        raise SystemExit(0)
