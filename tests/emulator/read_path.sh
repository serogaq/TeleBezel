#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
platform="${1:-emery}"
port="${MOCK_PORT:-8787}"
sdk="${PEBBLE_SDK_VERSION:?PEBBLE_SDK_VERSION is required}"
pbw="$root/app/build/app.pbw"
out="$root/app/build/emulator/$platform"
python=$(head -1 "$(command -v pebble)" | sed 's/^#!//')
test -f "$pbw" || { echo "Build the app first: make app-build" >&2; exit 1; }
mkdir -p "$out"

PORT="$port" node "$root/app/tests/e2e/mock_api.js" >"$out/mock.log" 2>&1 &
mock=$!
trap 'kill "$mock" 2>/dev/null || true' EXIT


"$python" - "$platform" "$sdk" "$port" <<'PY'
import dbm.dumb, json, os, sys
platform, sdk, port = sys.argv[1:4]
directory = os.path.expanduser(f"~/Library/Application Support/Pebble SDK/{sdk}/{platform}/localstorage")
if not os.path.isdir(os.path.dirname(directory)):
    directory = os.path.expanduser(f"~/.pebble-sdk/{sdk}/{platform}/localstorage")
os.makedirs(directory, exist_ok=True)
store = dbm.dumb.open(os.path.join(directory, "b91f715e-af74-4a90-9df4-fda0fbcd9762"), "c")
store["telebezel.settings.v1"] = json.dumps({"address": f"127.0.0.1:{port}", "ssl": False, "token": "tb_" + "e" * 43})
store.close()
PY

press() { pebble emu-button --emulator "$platform" click "$1" >/dev/null; }
shot() { sleep "${2:-2}"; pebble screenshot --emulator "$platform" --no-open "$out/$1.png" >/dev/null; echo "$out/$1.png"; }
repeat() { for _ in $(seq 1 "$2"); do press "$1"; done; }

pebble install --emulator "$platform" "$pbw"
shot 01-accounts 6
press select; shot 02-chats 4
repeat down 3; shot 03-chats-scrolled 1
repeat up 3; repeat down 6; press select; shot 04-history 5
repeat up 3; shot 05-history-kinds 1
repeat down 3; press select; shot 06-reader 4
repeat down 6; shot 07-reader-scrolled 1
press back; repeat up 24; shot 08-older-loaded 4
press back; repeat up 8; press select; shot 09-archive 4
press back; shot 10-accounts-again 2
press down; press select; shot 11-needs-login 2
press back; press up
pebble emu-bt-connection --emulator "$platform" --connected no >/dev/null; sleep 3
press select; shot 12-phone-offline 6
pebble emu-bt-connection --emulator "$platform" --connected yes >/dev/null
shot 13-reconnected 8
