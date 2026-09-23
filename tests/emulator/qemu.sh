#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
platform="${1:-emery}"
backend="${BACKEND:-mock}"
sdk="${PEBBLE_SDK_VERSION:?PEBBLE_SDK_VERSION is required}"
pbw="${PBW:-$root/app/build/app.pbw}"
state="$root/app/build/emulator"
python=$(head -1 "$(command -v pebble)" | sed 's/^#!//')
test -f "$pbw" || { printf 'PBW not found: %s\nRun make app-build first or set PBW=/path/to/app.pbw\n' "$pbw" >&2; exit 1; }
mkdir -p "$state"

mock=""
stop() {
  trap - EXIT INT TERM
  if test -n "$mock"; then kill "$mock" 2>/dev/null || true; fi
  if test "${1:-}" = interrupted; then pebble kill >/dev/null 2>&1 || true; fi
}
trap 'stop' EXIT
trap 'stop interrupted; exit 130' INT TERM

case "$backend" in
  mock)
    port="${MOCK_PORT:-8787}"
    PORT="$port" node "$root/app/tests/e2e/mock_api.js" >"$state/mock.log" 2>&1 &
    mock=$!
    address="127.0.0.1:$port"
    ssl=0
    token="tb_$(printf 'e%.0s' $(seq 1 43))"
    ;;
  docker)
    lan=$(sed -n 's/^TELEBEZEL_LAN_ADDRESS=//p' "$root/.env" 2>/dev/null | tail -1)
    if test -n "$lan"; then
      address="${ADDRESS:-$lan:443}"
      ssl="${SSL:-1}"
    else
      api_port=$(sed -n 's/^API_PORT=//p' "$root/.env" 2>/dev/null | tail -1)
      address="${ADDRESS:-127.0.0.1:${API_PORT:-${api_port:-8080}}}"
      ssl="${SSL:-0}"
    fi
    saved="$state/docker-token"
    if test -n "${TOKEN:-}"; then
      (umask 077 && printf '%s' "$TOKEN" >"$saved")
    fi
    test -f "$saved" || { echo "No Docker token saved yet: pass TOKEN=tb_... (a device token issued in /settings)" >&2; exit 1; }
    token=$(cat "$saved")
    if test "$ssl" = 1 && test -z "${ADDRESS:-}"; then
      (cd "$root" && docker compose cp caddy:/data/caddy/pki/authorities/local/root.crt "$state/lan-root.crt" >/dev/null) ||
        { echo "Could not read the Caddy root certificate; is the LAN stack running?" >&2; exit 1; }
      proxy_port="${PROXY_PORT:-8788}"
      TARGET_HOST="${address%:*}" TARGET_PORT="${address##*:}" CA_FILE="$state/lan-root.crt" PORT="$proxy_port" \
        node "$root/tests/emulator/lan_proxy.js" >"$state/lan-proxy.log" 2>&1 &
      mock=$!
      address="127.0.0.1:$proxy_port"
      ssl=0
    fi
    ;;
  *)
    echo "Unknown BACKEND=$backend (use mock or docker)" >&2
    exit 1
    ;;
esac

pebble kill >/dev/null 2>&1 || true
sleep 2

TB_ADDRESS="$address" TB_SSL="$ssl" TB_TOKEN="$token" "$python" - "$platform" "$sdk" <<'PY'
import dbm.dumb, json, os, sys
platform, sdk = sys.argv[1:3]
directory = os.path.expanduser(f"~/Library/Application Support/Pebble SDK/{sdk}/{platform}/localstorage")
if not os.path.isdir(os.path.dirname(directory)):
    directory = os.path.expanduser(f"~/.pebble-sdk/{sdk}/{platform}/localstorage")
os.makedirs(directory, exist_ok=True)
store = dbm.dumb.open(os.path.join(directory, "b91f715e-af74-4a90-9df4-fda0fbcd9762"), "c")
try:
    current = json.loads(store["telebezel.settings.v1"])
except (KeyError, ValueError):
    current = {}
token = os.environ["TB_TOKEN"] or current.get("token", "")
if not token:
    sys.exit("No token stored in the emulator: pass TOKEN=tb_... (a device token issued in /settings)")
current.update({"address": os.environ["TB_ADDRESS"], "ssl": os.environ["TB_SSL"] == "1", "token": token})
store["telebezel.settings.v1"] = json.dumps(current)
store.close()
print(f"{platform}: connected to {os.environ['TB_ADDRESS']}")
PY

pebble install --emulator "$platform" "$pbw" ${QEMU_FLAGS:-}

remembered="$state/lang-pack"
pack="${LANG_PACK:-}"
if test "$pack" = none; then
  rm -f "$remembered"
  pack=""
elif test -n "$pack"; then
  echo "$pack" >"$remembered"
elif test -f "$remembered"; then
  pack=$(cat "$remembered")
fi

if test -n "$pack"; then
  if test "$pack" = ru; then
    pack="$root/app/build/lang/ru_RU.pbl"
    if ! test -f "$pack"; then
      mkdir -p "$(dirname "$pack")"
      curl -sSfL -o "$pack" https://binaries.rebble.io/lp/d0oGecv-ru_RU.pbl
    fi
  fi
  test -f "$pack" || { echo "Language pack not found: $pack" >&2; exit 1; }
  pebble fw --emulator "$platform" install-lang "$pack"
  pebble install --emulator "$platform" "$pbw" ${QEMU_FLAGS:-}
fi

qemu=$("$python" -c 'import sys
from pebble_tool.sdk.emulator import get_emulator_info
info = get_emulator_info(sys.argv[1])
print(info["qemu"]["pid"] if info else "")' "$platform")
test -n "$qemu" || { echo "The $platform emulator is not running" >&2; exit 1; }
echo "$platform emulator runs against $address. Close the emulator or press Ctrl+C to stop."
while kill -0 "$qemu" 2>/dev/null; do sleep 1; done
