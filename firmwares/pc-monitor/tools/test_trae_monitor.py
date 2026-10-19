#!/usr/bin/env python3
"""Unit tests for the TRAE monitor (tools/trae_monitor.py).

Run from the pc-monitor directory:

    python tools/test_trae_monitor.py

Covers clean_cmd sanitization, the hook-event state machine, the approval
queue (allow / deny / timeout), the badge frame builders and one loopback
TCP round trip against the real connection handler.
"""

from __future__ import annotations

import json
import re
import socket
import threading
import time

from trae_monitor import TRAE_CMD_MAX_BYTES, TraeMonitor, clean_cmd


def test_clean_cmd() -> None:
    assert clean_cmd("pip install x") == "pip install x"
    # ',' is the frame field separator -> ';'; '#' is the frame terminator.
    assert clean_cmd("a,b") == "a;b"
    assert clean_cmd("a#b") == "ab"
    # Newlines and runs of whitespace collapse to single spaces.
    assert clean_cmd("  a \n\t b  ") == "a b"
    assert clean_cmd(None) == ""
    assert clean_cmd("") == ""
    # UTF-8 truncation must not split a character: 119 bytes // 3 = 39 chars.
    out = clean_cmd("中" * 60)
    assert len(out.encode("utf-8")) <= TRAE_CMD_MAX_BYTES
    assert out == "中" * 39
    print("clean_cmd: OK")


def new_monitor() -> TraeMonitor:
    # tcp_port=0 keeps the instance from binding anything we care about;
    # handle_event / frames are exercised directly.
    return TraeMonitor(tcp_port=0)


def test_states() -> None:
    m = new_monitor()
    assert m.tstat_frame().decode("ascii") == "$TSTAT,st=idle#\n"

    r = m.handle_event({"event": "UserPromptSubmit"})
    assert r == {"ok": True, "state": "think", "task": 1}
    assert m.tstat_frame().decode("ascii") == "$TSTAT,st=think,task=1#\n"

    r = m.handle_event({"event": "PreToolUse", "tool": "Edit"})
    assert r == {"ok": True, "state": "code"}

    r = m.handle_event({"event": "PostToolUse", "tool": "RunCommand", "exit": 0})
    assert r == {"ok": True, "state": "pass"}
    r = m.handle_event({"event": "PostToolUse", "tool": "RunCommand", "exit": 3})
    assert r == {"ok": True, "state": "fail"}
    # 1 of 2 commands passed -> rate=50.
    assert "rate=50" in m.tstat_frame().decode("ascii")

    # A missing exit code counts as a failure (fail-safe).
    r = m.handle_event({"event": "PostToolUse", "tool": "RunCommand"})
    assert r == {"ok": True, "state": "fail"}

    r = m.handle_event({"event": "Stop"})
    assert r == {"ok": True, "state": "idle"}

    assert m.handle_event({"event": "Bogus"}).get("ok") is False
    print("state machine: OK")


def test_coding_minutes() -> None:
    m = new_monitor()
    with m._lock:  # bank some active seconds directly
        m._coding_s = 90.0
    assert "cm=1" in m.tstat_frame().decode("ascii")
    print("coding minutes: OK")


def test_approval_allow() -> None:
    m = new_monitor()
    result: dict = {}

    def ask() -> None:
        result["r"] = m.handle_event(
            {
                "event": "PreToolUse",
                "tool": "RunCommand",
                "cmd": "pytest -k foo,bar # comment",
                "wait": 5,
            }
        )

    t = threading.Thread(target=ask)
    t.start()
    frame = None
    for _ in range(200):
        frame = m.aprv_frame()
        if frame:
            break
        time.sleep(0.02)
    assert frame is not None
    # ',' -> ';', '#' dropped; the parser takes the first '=' as separator.
    assert frame.decode("utf-8") == "$TAPRV,id=1,t=pytest -k foo;bar  comment#\n"
    assert m.tstat_frame().decode("ascii").startswith("$TSTAT,st=build")

    assert m.on_btn_frame("noise $TBTN,id=1,r=allow# tail") is True
    t.join(timeout=5)
    assert not t.is_alive()
    assert result["r"] == {"ok": True, "id": 1, "decision": "allow"}
    # After an allow the monitor keeps the build state until the next event.
    assert m.tstat_frame().decode("ascii").startswith("$TSTAT,st=build")
    print("approval allow: OK")


def test_approval_deny() -> None:
    m = new_monitor()
    result: dict = {}

    def ask() -> None:
        result["r"] = m.handle_event(
            {"event": "PreToolUse", "tool": "RunCommand", "cmd": "rm -rf /", "wait": 5}
        )

    t = threading.Thread(target=ask)
    t.start()
    for _ in range(200):
        if m.aprv_frame():
            break
        time.sleep(0.02)

    assert m.on_btn_frame("$TBTN,id=1,r=deny#") is True
    t.join(timeout=5)
    assert result["r"] == {"ok": True, "id": 1, "decision": "deny"}
    # Denied: TRAE is still working, so the state returns to code.
    assert m.tstat_frame().decode("ascii").startswith("$TSTAT,st=code")
    print("approval deny: OK")


def test_approval_timeout() -> None:
    m = new_monitor()
    result: dict = {}

    def ask() -> None:
        result["r"] = m.handle_event(
            {"event": "PreToolUse", "tool": "RunCommand", "cmd": "make", "wait": 1}
        )

    t = threading.Thread(target=ask)
    t.start()
    for _ in range(200):
        if m.aprv_frame():
            break
        time.sleep(0.02)
    t.join(timeout=10)
    assert result["r"] == {"ok": True, "id": 1, "decision": "timeout"}
    assert m.aprv_frame() is None  # the expired approval left the queue
    print("approval timeout: OK")


def test_approval_edge_cases() -> None:
    m = new_monitor()
    assert m.on_btn_frame("garbage line") is False
    assert m.on_btn_frame("$TBTN,id=99,r=allow#") is False  # unknown id

    # Two pending approvals: the oldest one is shown first.
    results: dict = {}

    def ask(cmd: str) -> None:
        r = m.handle_event(
            {"event": "PreToolUse", "tool": "RunCommand", "cmd": cmd}
        )
        results[r["id"]] = r

    t1 = threading.Thread(target=ask, args=("first",))
    t1.start()
    for _ in range(200):
        if m.aprv_frame():
            break
        time.sleep(0.02)
    t2 = threading.Thread(target=ask, args=("second",))
    t2.start()
    for _ in range(200):
        if "id=2" in (m.aprv_frame() or b"").decode("utf-8", "ignore"):
            break
        time.sleep(0.02)

    frame = m.aprv_frame().decode("utf-8")
    assert frame == "$TAPRV,id=1,t=first#\n"
    # Deciding the second id does not release the first waiter.
    assert m.on_btn_frame("$TBTN,id=2,r=deny#") is True
    t2.join(timeout=5)
    assert not t2.is_alive()
    assert results[2] == {"ok": True, "id": 2, "decision": "deny"}
    assert m.aprv_frame() is not None  # id=1 still pending
    t1.join(timeout=0.5)
    assert t1.is_alive()

    # A stale button frame for an already-decided id is ignored.
    assert m.on_btn_frame("$TBTN,id=2,r=allow#") is False
    assert m.on_btn_frame("$TBTN,id=1,r=allow#") is True
    t1.join(timeout=5)
    assert not t1.is_alive()
    assert results[1] == {"ok": True, "id": 1, "decision": "allow"}
    print("approval edge cases: OK")


def test_tcp_roundtrip() -> None:
    m = TraeMonitor(tcp_port=0)
    m.start()
    port = m._server.getsockname()[1]
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        reader = s.makefile("rb")
        s.sendall(b'{"event": "UserPromptSubmit"}\n')
        assert json.loads(reader.readline()) == {
            "ok": True,
            "state": "think",
            "task": 1,
        }
        # Bad JSON gets an error reply and the connection stays open.
        s.sendall(b"not json\n")
        assert json.loads(reader.readline()).get("ok") is False
        # A RunCommand approval over TCP: the reply line only arrives once
        # the badge decides, so press the button before reading it.
        s.sendall(
            b'{"event": "PreToolUse", "tool": "RunCommand",'
            b' "cmd": "echo hi", "wait": 5}\n'
        )
        # Wait for the connection handler to queue the approval.
        for _ in range(200):
            if m.aprv_frame():
                break
            time.sleep(0.02)
        assert m.aprv_frame() is not None
        assert m.on_btn_frame("$TBTN,id=1,r=allow#") is True
        assert json.loads(reader.readline()) == {
            "ok": True,
            "id": 1,
            "decision": "allow",
        }

    m._server.close()
    print("tcp roundtrip: OK")


def main() -> None:
    test_clean_cmd()
    test_states()
    test_coding_minutes()
    test_approval_allow()
    test_approval_deny()
    test_approval_timeout()
    test_approval_edge_cases()
    test_tcp_roundtrip()
    print("test_trae_monitor: OK")


if __name__ == "__main__":
    main()
