#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
platform="${1:-emery}"
port="${MOCK_PORT:-8787}"
sdk="${PEBBLE_SDK_VERSION:?PEBBLE_SDK_VERSION is required}"
pbw="$root/app/build/app.pbw"
out="$root/app/build/emulator/$platform"
python=$(head -1 "$(command -v pebble)" | sed 's/^#!//')
test -f "$pbw" || { echo "Build the app first: make app-emulator-check builds it with TB_DIAG=1" >&2; exit 1; }
rm -rf "$out"
mkdir -p "$out"

PORT="$port" MOCK_CONNECTION="connecting,updating,updating,ready" node "$root/app/tests/e2e/mock_api.js" >"$out/mock.log" 2>&1 &
mock=$!
logs=""
serial=""
cleanup() {
  kill "$mock" 2>/dev/null || true
  if test -n "$logs"; then kill "$logs" 2>/dev/null || true; fi
  if test -n "$serial"; then kill "$serial" 2>/dev/null || true; fi
}
trap cleanup EXIT

configure() {
"$python" - "$platform" "$sdk" "$port" "$1" <<'PY'
import dbm.dumb, json, os, sys
platform, sdk, port, archive = sys.argv[1:5]
directory = os.path.expanduser(f"~/Library/Application Support/Pebble SDK/{sdk}/{platform}/localstorage")
if not os.path.isdir(os.path.dirname(directory)):
    directory = os.path.expanduser(f"~/.pebble-sdk/{sdk}/{platform}/localstorage")
os.makedirs(directory, exist_ok=True)
store = dbm.dumb.open(os.path.join(directory, "b91f715e-af74-4a90-9df4-fda0fbcd9762"), "c")
store["telebezel.settings.v1"] = json.dumps({"address": f"127.0.0.1:{port}", "ssl": False, "token": "tb_" + "e" * 43,
                                             "showArchive": archive == "1", "unreadMode": "chats" if archive == "1" else "messages"})
store.close()
PY
}
configure 1

control() { "$python" "$root/tests/emulator/control.py" "$platform" "$@"; }
press() { control button "$1"; }
shot() { sleep "${2:-2}"; control screenshot "$out/$1.png"; echo "$out/$1.png"; }
repeat() { control button "$1" "$2"; }
install() { for _ in 1 2 3; do pebble install --emulator "$platform" "$pbw" && return 0; sleep 5; done; return 1; }

install
pebble logs --emulator "$platform" >"$out/diag.log" 2>&1 &
logs=$!
"$python" "$root/tests/emulator/serial.py" "$platform" "$out/serial.log" &
serial=$!
shot 01-accounts 6
press select; shot 02-chats-connecting 1
shot 02-chats 10
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
control bluetooth no; sleep 3
press select; shot 12-phone-offline 6
control bluetooth yes
shot 13-reconnected 15

kill "$mock" 2>/dev/null || true
wait "$mock" 2>/dev/null || true
PORT="$port" MOCK_CONNECTION="connecting" MOCK_PROXY=1 node "$root/app/tests/e2e/mock_api.js" >"$out/mock-connecting.log" 2>&1 &
mock=$!
pebble kill >/dev/null 2>&1 || true
sleep 3
configure 0
install
kill "$logs" 2>/dev/null || true
pebble logs --emulator "$platform" >"$out/diag-connecting.log" 2>&1 &
logs=$!
kill "$serial" 2>/dev/null || true
"$python" "$root/tests/emulator/serial.py" "$platform" "$out/serial.log" &
serial=$!
sleep 8; press select; shot 14-connecting-hidden-archive 3
shot 15-cannot-connect 16
repeat up 3; shot 16-topbar 1
