#!/usr/bin/env bash
# Deploy trae_hook.py + hooks.json to a Rocky Linux remote TRAE host.
# Run from anywhere on your local machine:
#   bash tools/deploy_remote_hook.sh user@your-remote-host
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 user@host"
  exit 1
fi
REMOTE="$1"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HOOK_LOCAL="$SCRIPT_DIR/trae_hook.py"
HOOKS_LOCAL="$SCRIPT_DIR/hooks-remote.json"

echo "==> copying trae_hook.py + hooks.json to $REMOTE"
ssh "$REMOTE" 'mkdir -p ~/.trae-hooks'
scp "$HOOK_LOCAL"  "$REMOTE:~/.trae-hooks/trae_hook.py"
scp "$HOOKS_LOCAL" "$REMOTE:~/.trae-hooks/hooks.json"

echo "==> patching ~/.trae-cn/hooks.json on $REMOTE"
ssh "$REMOTE" 'python3 - <<PY
import json, pathlib
src = pathlib.Path("~/.trae-hooks/hooks.json").expanduser()
dst = pathlib.Path.home() / ".trae-cn" / "hooks.json"
dst.parent.mkdir(parents=True, exist_ok=True)
data = json.loads(src.read_text(encoding="utf-8"))
# Backup existing
if dst.exists():
    dst.replace(dst.with_suffix(".json.bak"))
dst.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
print(f"installed to {dst}")
PY'

echo "==> done. In TRAE's remote project window:"
echo "    1. Settings > Hooks: confirm entries show up"
echo "    2. Run Mode: Local Auto-Run (NOT sandbox)"
echo "    3. Restart the remote TRAE window"
echo
echo "==> on YOUR local machine, keep this tunnel running:"
echo "    ssh -N -R 8765:127.0.0.1:8765 $REMOTE"
