#!/usr/bin/env python3
"""Install (or remove) the TRAE battle-page hooks in %userprofile%/.trae-cn/hooks.json.

The hooks make TRAE report its status to the pcmon bridge and route RunCommand
approvals to the badge:

* UserPromptSubmit / Stop           -> badge status (think / idle)
* PreToolUse  matcher Edit|Write    -> badge status (code)
* PreToolUse  matcher RunCommand    -> badge approval overlay (timeout 120s)
* PostToolUse matcher RunCommand    -> badge pass/fail + pass rate

Existing hook entries added by this script (identified by ``trae_hook.py`` in
the command) are replaced, so re-running is idempotent; other entries are kept.
A timestamped backup of the original file is written before the first change.

Usage:
    python tools/install_hooks.py           # install/refresh
    python tools/install_hooks.py --remove  # remove the battle-page hooks
    python tools/install_hooks.py --status  # show what is installed
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import time

MARKER = "trae_hook.py"

# (event, matcher, timeout seconds); matcher "" means "no matcher key".
HOOK_PLAN = [
    ("UserPromptSubmit", "", 10),
    ("PreToolUse", "RunCommand", 120),
    ("PreToolUse", "Edit|Write", 15),
    ("PostToolUse", "RunCommand", 15),
    ("Stop", "", 10),
]

OUR_MATCHERS = {event: matcher for event, matcher, _ in HOOK_PLAN}


def hooks_path() -> str:
    return os.path.join(
        os.path.expanduser("~"), ".trae-cn", "hooks.json"
    )


def our_command() -> str:
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "trae_hook.py")
    # TRAE embeds the command inside its PowerShell wrapper, and embedded
    # double quotes break that wrapper (ParserError "unexpected token").
    # Bare unquoted paths parse everywhere; enforce space-free paths here.
    parts = [sys.executable, script]
    if any(" " in p for p in parts):
        raise SystemExit(
            "hook paths must not contain spaces (TRAE's PowerShell wrapper "
            "chokes on quoted commands); relocate Python or the script"
        )
    return " ".join(parts)


def _is_ours(entry: dict) -> bool:
    for hook in entry.get("hooks") or []:
        if MARKER in str(hook.get("command") or ""):
            return True
    return False


def load(path: str) -> dict:
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except FileNotFoundError:
        return {}
    except ValueError as exc:
        raise SystemExit(
            f"{path} is not valid JSON ({exc}); fix or remove it and retry"
        )
    return data if isinstance(data, dict) else {}


def install() -> int:
    path = hooks_path()
    config = load(path)
    hooks = config.setdefault("hooks", {})
    if not isinstance(hooks, dict):
        raise SystemExit(f"{path}: unexpected 'hooks' section")

    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
    else:
        backup = f"{path}.bak-{time.strftime('%Y%m%d-%H%M%S')}"
        shutil.copy2(path, backup)
        print(f"backup: {backup}")

    command = our_command()
    # Group by event: entries of the same event must be rebuilt together or a
    # later matcher (e.g. Edit|Write) would drop the earlier one (RunCommand).
    by_event: dict[str, list[tuple[str, int]]] = {}
    for event, matcher, timeout in HOOK_PLAN:
        by_event.setdefault(event, []).append((matcher, timeout))
    for event, matchers in by_event.items():
        entries = [e for e in hooks.get(event) or [] if isinstance(e, dict)]
        entries = [e for e in entries if not _is_ours(e)]
        for matcher, timeout in matchers:
            hook = {"type": "command", "command": command, "timeout": timeout}
            entry = {"hooks": [hook]}
            if matcher:
                entry["matcher"] = matcher
            entries.append(entry)
        hooks[event] = entries

    config.setdefault("version", 1)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(config, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(f"installed {len(HOOK_PLAN)} hook entries -> {path}")
    print("restart TRAE so it re-reads hooks.json; keep the bridge running.")
    return 0


def remove() -> int:
    path = hooks_path()
    config = load(path)
    hooks = config.get("hooks") or {}
    removed = 0
    for event in list(hooks):
        entries = [e for e in hooks.get(event) or [] if isinstance(e, dict)]
        kept = [e for e in entries if not _is_ours(e)]
        removed += len(entries) - len(kept)
        if kept:
            hooks[event] = kept
        else:
            hooks.pop(event)
    if removed:
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(config, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
    print(f"removed {removed} entries from {path}")
    return 0


def status() -> int:
    path = hooks_path()
    hooks = load(path).get("hooks") or {}
    for event, matcher, timeout in HOOK_PLAN:
        entries = [
            e
            for e in hooks.get(event) or []
            if isinstance(e, dict)
            and _is_ours(e)
            and str(e.get("matcher") or "") == matcher
        ]
        state = "installed" if entries else "MISSING"
        print(f"{event:<18} matcher={matcher or '-':<12} t={timeout:<4} {state}")
    print(f"config: {path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--remove", action="store_true", help="uninstall our hooks")
    group.add_argument("--status", action="store_true", help="show install state")
    args = parser.parse_args()
    if args.remove:
        return remove()
    if args.status:
        return status()
    return install()


if __name__ == "__main__":
    raise SystemExit(main())
