#!/usr/bin/env python3
"""TRAE companion state machine shared by the pcmon bridge and hook clients.

The bridge owns one TraeMonitor instance. TRAE hooks (tools/trae_hook.py) are
short-lived processes: each connects to the monitor's loopback TCP server,
sends one JSON line describing the hook event, and reads one JSON line back.

Protocol (line-delimited JSON over 127.0.0.1:<port>):

  hook -> monitor: {"event": "UserPromptSubmit"}
                   {"event": "PreToolUse", "tool": "Edit"}
                   {"event": "PreToolUse", "tool": "RunCommand",
                    "cmd": "pip install x", "wait": 110}
                   {"event": "PostToolUse", "tool": "RunCommand", "exit": 0}
                   {"event": "Stop"}
  monitor -> hook: {"ok": true, "state": "code"}
                   {"ok": true, "id": 3, "decision": "allow|deny|timeout"}

The badge sees the result through the bridge: every serial answer carries a
``$TSTAT,...`` status frame and, while a RunCommand waits for a decision, a
``$TAPRV,...`` frame. The badge answers with ``$TBTN,id=..,r=allow|deny#``,
which the bridge feeds back through :meth:`TraeMonitor.on_btn_frame`.

States follow the device's ``pcmon_trae_state_t``: idle/think/code/build/
pass/fail. "Coding minutes" accumulate while a session is actively working
(think/code/build); the pass rate and counters reset at midnight.
"""

from __future__ import annotations

import json
import logging
import re
import socket
import threading
import time

TRAE_TCP_HOST = "127.0.0.1"
TRAE_TCP_PORT = 8765
TRAE_CMD_MAX_BYTES = 119  # C PCMON_TRAE_CMD_MAX minus the terminating NUL
DEFAULT_WAIT_S = 110.0
MAX_WAIT_S = 110.0  # keep below the device's 120s approval TTL

ACTIVE_STATES = ("think", "code", "build")

_BTN_RE = re.compile(r"\$TBTN,id=(-?\d+),r=(allow|deny)")


def clean_cmd(text, byte_limit: int = TRAE_CMD_MAX_BYTES) -> str:
    """Make a command safe for one $TAPRV field and the badge's 120-byte buffer.

    Newlines collapse to spaces, commas (field separators) become semicolons
    and '#' (frame terminator) is dropped; then the text is cut on a UTF-8
    character boundary to the byte budget.
    """
    cleaned = " ".join(str(text or "").split())
    cleaned = cleaned.replace(",", ";").replace("#", "")
    return cleaned.encode("utf-8", errors="ignore")[:byte_limit].decode(
        "utf-8", errors="ignore"
    )


class _Approval:
    """One pending RunCommand waiting for a badge decision."""

    __slots__ = ("id", "cmd", "created", "done", "decision")

    def __init__(self, aprv_id: int, cmd: str) -> None:
        self.id = aprv_id
        self.cmd = cmd
        self.created = time.monotonic()
        self.done = threading.Event()
        self.decision: str | None = None  # "allow" | "deny"


class TraeMonitor:
    """Tracks TRAE's state, the approval queue and today's battle stats."""

    def __init__(
        self,
        tcp_host: str = TRAE_TCP_HOST,
        tcp_port: int = TRAE_TCP_PORT,
        logger: logging.Logger | None = None,
    ) -> None:
        self._host = tcp_host
        self._port = tcp_port
        self._log = logger or logging.getLogger("pcmon.bridge")
        self._lock = threading.Lock()
        # Per-window session states (sid = TRAE session_id, one per window).
        # The badge shows the most active session, so one window's Stop no
        # longer wipes another window's "thinking" state.
        self._sessions: dict[str, dict] = {}
        self._task = 0
        self._passed = 0
        self._failed = 0
        self._coding_s = 0.0
        self._day = time.localtime().tm_yday
        self._pending: list[_Approval] = []
        self._next_id = 1
        self._server: socket.socket | None = None
        self._thread: threading.Thread | None = None

    # ------------------------------------------------------------ lifecycle

    def start(self) -> None:
        """Bind the loopback TCP server and start serving hook clients."""
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self._host, self._port))
        server.listen(8)
        self._server = server
        self._thread = threading.Thread(
            target=self._serve_forever, name="trae-monitor", daemon=True
        )
        self._thread.start()
        self._log.info("TRAE monitor listening on %s:%d", self._host, self._port)

    def _serve_forever(self) -> None:
        while True:
            try:
                conn, addr = self._server.accept()
            except OSError:
                return
            threading.Thread(
                target=self._handle_conn,
                args=(conn, addr),
                daemon=True,
            ).start()

    def _handle_conn(self, conn: socket.socket, addr) -> None:
        with conn:
            conn.settimeout(180.0)  # hooks wait at most ~110s for a decision
            try:
                with conn.makefile("rb") as reader:
                    for raw in reader:
                        if len(raw) > 65536:
                            break
                        try:
                            req = json.loads(raw.decode("utf-8", "ignore"))
                        except ValueError:
                            reply = {"ok": False, "error": "bad json"}
                        else:
                            try:
                                reply = self.handle_event(
                                    req if isinstance(req, dict) else {}
                                )
                            except Exception as exc:  # never kill the thread
                                self._log.warning(
                                    "TRAE hook event failed: %s", exc
                                )
                                reply = {"ok": False, "error": str(exc)}
                        conn.sendall(
                            (json.dumps(reply, ensure_ascii=False) + "\n").encode(
                                "utf-8"
                            )
                        )
            except (OSError, ValueError):
                return

    # --------------------------------------------------------- hook events

    def handle_event(self, req: dict) -> dict:
        event = str(req.get("event") or "")
        tool = str(req.get("tool") or "")
        sid = str(req.get("sid") or "")
        now = time.monotonic()

        if event == "PreToolUse" and tool == "RunCommand":
            return self._ask_run_command(req, sid, now)

        with self._lock:
            self._roll_day()
            if event == "UserPromptSubmit":
                self._set_session(sid, "think", now)
                self._task += 1
                return {"ok": True, "state": "think", "task": self._task}
            if event == "PreToolUse":
                self._set_session(sid, "code", now)
                return {"ok": True, "state": "code"}
            if event == "PostToolUse":
                if tool == "RunCommand":
                    try:
                        code = int(req.get("exit"))
                    except (TypeError, ValueError):
                        code = 1
                    if code == 0:
                        self._passed += 1
                        self._set_session(sid, "pass", now)
                    else:
                        self._failed += 1
                        self._set_session(sid, "fail", now)
                    return {"ok": True, "state": self._session_state(sid)}
                self._set_session(sid, "code", now)
                return {"ok": True, "state": "code"}
            if event in ("Stop", "SessionStart", "SessionEnd"):
                self._set_session(sid, "idle", now)
                return {"ok": True, "state": "idle"}
            return {"ok": False, "error": f"unknown event {event!r}"}

    def _ask_run_command(self, req: dict, sid: str, now: float) -> dict:
        """Queue the command for badge approval and block until it answers.

        The hook passes its own remaining budget in ``wait``; the monitor
        never waits longer than MAX_WAIT_S so the hook can still fall back to
        TRAE's native confirmation dialog inside its timeout.
        """
        cmd = clean_cmd(req.get("cmd"))
        try:
            wait = float(req.get("wait") or DEFAULT_WAIT_S)
        except (TypeError, ValueError):
            wait = DEFAULT_WAIT_S
        wait = max(1.0, min(wait, MAX_WAIT_S))

        with self._lock:
            self._roll_day()
            self._set_session(sid, "build", now)
            aprv = _Approval(self._next_id, cmd)
            self._next_id += 1
            self._pending.append(aprv)

        decided = aprv.done.wait(wait)

        with self._lock:
            try:
                self._pending.remove(aprv)
            except ValueError:
                pass
            if aprv.decision != "allow":
                # Denied or timed out: TRAE is still working on the task.
                self._set_session(sid, "code", time.monotonic())
        return {
            "ok": True,
            "id": aprv.id,
            "decision": aprv.decision if decided and aprv.decision else "timeout",
        }

    # --------------------------------------------------------- badge frames

    def tstat_frame(self) -> bytes:
        with self._lock:
            now = time.monotonic()
            fields = [f"st={self._aggregate_state(now)}"]
            if self._task > 0:
                fields.append(f"task={self._task}")
            total = self._passed + self._failed
            if total > 0:
                fields.append(
                    f"rate={(self._passed * 100 + total // 2) // total}"
                )
            cm = self._coding_minutes(now)
            if cm > 0:
                fields.append(f"cm={cm}")
            return ("$TSTAT," + ",".join(fields) + "#\n").encode("ascii")

    def aprv_frame(self) -> bytes | None:
        """The oldest pending approval, or None when nothing waits."""
        with self._lock:
            if not self._pending:
                return None
            aprv = self._pending[0]
            return f"$TAPRV,id={aprv.id},t={aprv.cmd}#\n".encode("utf-8")

    def on_btn_frame(self, text: str) -> bool:
        """Feed a ``$TBTN,id=..,r=allow|deny#`` line from the badge."""
        match = _BTN_RE.search(text or "")
        if not match:
            return False
        aprv_id = int(match.group(1))
        decision = match.group(2)
        with self._lock:
            for aprv in self._pending:
                if aprv.id == aprv_id:
                    aprv.decision = decision
                    aprv.done.set()
                    return True
        self._log.info("TRAE button for unknown approval id %d", aprv_id)
        return False

    # -------------------------------------------------------------- internals

    def _set_session(self, sid: str, state: str, now: float) -> None:
        """Switch one session's state (lock held) and bank its active time."""
        session = self._sessions.get(sid)
        if session is None:
            session = self._sessions[sid] = {
                "state": "idle",
                "since": None,
                "last": now,
            }
        session["last"] = now
        if state == session["state"]:
            return
        if session["state"] in ACTIVE_STATES and session["since"] is not None:
            self._coding_s += now - session["since"]
        session["state"] = state
        session["since"] = now if state in ACTIVE_STATES else None

    def _session_state(self, sid: str) -> str:
        session = self._sessions.get(sid)
        return session["state"] if session else "idle"

    def _aggregate_state(self, now: float) -> str:
        """The badge shows the most active session (lock held).

        Sessions silent for 30 minutes are pruned so a closed window stops
        contributing its last state forever.
        """
        for sid in list(self._sessions):
            session = self._sessions[sid]
            if now - session["last"] > 1800.0:
                if session["state"] in ACTIVE_STATES and session["since"] is not None:
                    self._coding_s += now - session["since"]
                del self._sessions[sid]
        states = {s["state"] for s in self._sessions.values()}
        # Priority: a pending approval is most urgent, then real work, then
        # the transient pass/fail flashes, idle only when nothing moves.
        for want in ("build", "code", "think"):
            if want in states:
                return want
        for want in ("pass", "fail"):
            if want in states:
                return want
        return "idle"

    def _roll_day(self) -> None:
        day = time.localtime().tm_yday
        if day != self._day:
            self._day = day
            self._coding_s = 0.0
            self._passed = 0
            self._failed = 0

    def _coding_minutes(self, now: float) -> int:
        # Banked time plus every session's live active stretch (two windows
        # working in parallel both count).
        active = self._coding_s
        for session in self._sessions.values():
            if session["state"] in ACTIVE_STATES and session["since"] is not None:
                active += now - session["since"]
        return int(active // 60)
