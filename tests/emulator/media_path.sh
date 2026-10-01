#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
platform="${1:-emery}"
port="${MOCK_PORT:-8787}"
sdk="${PEBBLE_SDK_VERSION:?PEBBLE_SDK_VERSION is required}"
pbw="$root/app/build/app.pbw"
out="$root/app/build/emulator/$platform/media"
python=$(head -1 "$(command -v pebble)" | sed 's/^#!//')
test -f "$pbw" || { echo "Build the app first: make app-emulator-check builds it with TB_DIAG=1" >&2; exit 1; }
rm -rf "$out"
mkdir -p "$out"

PORT="$port" MOCK_LOG_REQUESTS=1 node "$root/app/tests/e2e/mock_api.js" >"$out/mock.log" 2>&1 &
mock=$!
logs=""
serial=""
cleanup() {
  kill "$mock" 2>/dev/null || true
  if test -n "$logs"; then kill "$logs" 2>/dev/null || true; fi
  if test -n "$serial"; then kill "$serial" 2>/dev/null || true; fi
}
trap cleanup EXIT

bash "$root/tests/emulator/lang_pack.sh" "$platform"

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
install() { for attempt in 1 2 3 4 5 6; do pebble install --emulator "$platform" "$pbw" && return 0; sleep $((3 * attempt)); done; return 1; }

mark() { before=$(grep -c "media_phase=5" "$out/diag.log" || true); }
ready() {
  local count
  for _ in $(seq 1 80); do
    count=$(grep -c "media_phase=5" "$out/diag.log" || true)
    if test "$count" -gt "$before"; then return 0; fi
    sleep 0.25
  done
  echo "the image never became ready ($1)" >&2
  exit 1
}

install
pebble logs --emulator "$platform" >"$out/diag.log" 2>&1 &
logs=$!
"$python" "$root/tests/emulator/serial.py" "$platform" "$out/serial.log" &
serial=$!
sleep 6
press select; sleep 5
press select; sleep 4
press up
mark; press select; ready photo; shot 01-reader 1
hold select; shot 02-menu 1
press back; press down; shot 02-reader-scrolled 1
press select; shot 02-reader-menu 1
press back; press up; shot 02-reader-top 1
press select; shot 03-viewer 0.5
shot 04-viewer-settled 2.5
press back; press back; sleep 2
press up 11; hold select; sleep 1
mark; press select; ready album; shot 05-album 0.3
mark; press down; ready album-next; shot 06-album-next 0.3
mark; press down; ready album-last; shot 07-album-last 0.3
press down; sleep 1
mark; press up; ready album-back; shot 08-album-back 0.3
press back; press back; sleep 2
press up 2; hold select; sleep 1
press select; shot 09-spoiler 2
mark; press select; ready spoiler; shot 10-revealed 0.3
hold select; shot 11-viewer-menu 1
press back; press back; press back; sleep 2
shot 12-history 1
press up 3; mark; press select; ready carousel; shot 13-carousel-reader 1
press select; shot 14-carousel 1
mark; press down; ready carousel-next; shot 15-carousel-next 0.3
press back; press back; sleep 2
sleep 2

kill "$logs" 2>/dev/null || true
wait "$logs" 2>/dev/null || true
logs=""
grep -q "/media" "$out/mock.log" || { echo "the watch never asked for an image" >&2; exit 1; }
echo "media path screenshots in $out"
