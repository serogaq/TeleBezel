#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
platform="${1:-emery}"
port="${MOCK_PORT:-8787}"
sdk="${PEBBLE_SDK_VERSION:?PEBBLE_SDK_VERSION is required}"
pbw="$root/app/build/app.pbw"
out="$root/app/build/emulator/$platform/send"
python=$(head -1 "$(command -v pebble)" | sed 's/^#!//')
test -f "$pbw" || { echo "Build the app first: make app-emulator-check builds it with TB_DIAG=1" >&2; exit 1; }
rm -rf "$out"
mkdir -p "$out"

PORT="$port" MOCK_SEND_MODES="sent,pending,forbidden" MOCK_SETTLE_MS=8000 node "$root/app/tests/e2e/mock_api.js" >"$out/mock.log" 2>&1 &
mock=$!
logs=""
serial=""
cleanup() {
  kill "$mock" 2>/dev/null || true
  if test -n "$logs"; then kill "$logs" 2>/dev/null || true; fi
  if test -n "$serial"; then kill "$serial" 2>/dev/null || true; fi
}
trap cleanup EXIT

"$python" - "$platform" "$sdk" "$port" <<'PY'
import dbm.dumb, json, os, sys
platform, sdk, port = sys.argv[1:4]
directory = os.path.expanduser(f"~/Library/Application Support/Pebble SDK/{sdk}/{platform}/localstorage")
if not os.path.isdir(os.path.dirname(directory)):
    directory = os.path.expanduser(f"~/.pebble-sdk/{sdk}/{platform}/localstorage")
os.makedirs(directory, exist_ok=True)
store = dbm.dumb.open(os.path.join(directory, "b91f715e-af74-4a90-9df4-fda0fbcd9762"), "c")
store["telebezel.settings.v1"] = json.dumps({"address": f"127.0.0.1:{port}", "ssl": False, "token": "tb_" + "e" * 43,
                                             "showArchive": True, "unreadMode": "chats"})
store.close()
PY

control() { "$python" "$root/tests/emulator/control.py" "$platform" "$@"; }
press() { control button "$@"; }
hold() { control hold "$1"; }
shot() { sleep "${2:-2}"; control screenshot "$out/$1.png"; echo "$out/$1.png"; }
install() { for _ in 1 2 3; do pebble install --emulator "$platform" "$pbw" && return 0; sleep 5; done; return 1; }

install
pebble logs --emulator "$platform" >"$out/diag.log" 2>&1 &
logs=$!
"$python" "$root/tests/emulator/serial.py" "$platform" "$out/serial.log" &
serial=$!
sleep 6
press select; sleep 5
press select; sleep 4
press down; shot 01-write-row 1
press select; shot 02-compose 3
press down; press select; shot 03-review 3
press select; shot 04-sent 0.5
sleep 4
press up; hold select; shot 05-message-menu 1
press select; shot 06-reply-compose 3
press down 2; press select; shot 07-reply-review 3
press select; shot 08-sending 1.5
press back; shot 09-card-sending 1
for _ in $(seq 1 60); do grep -q "settled sent" "$out/diag.log" && break; sleep 0.5; done
shot 10-card-sent 0.3
press down 2; press select; sleep 3
press select; shot 11-dictation 2
press back; shot 12-dictation-cancelled 2
press down; press select; sleep 2
press select; shot 13-not-sent 3
press back; sleep 2
control double down; sleep 2
press back; shot 14-chats 2
sleep 2

kill "$logs" 2>/dev/null || true
wait "$logs" 2>/dev/null || true
logs=""
grep -q "settled op-2" "$out/mock.log" || { echo "the pending send was never settled" >&2; exit 1; }
grep -q "settled sent" "$out/diag.log" || { echo "PKJS never reported the late result" >&2; exit 1; }
echo "send path screenshots in $out"
