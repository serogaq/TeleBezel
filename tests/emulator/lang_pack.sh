#!/usr/bin/env bash
set -euo pipefail

platform="${1:?platform is required}"
keep="${XDG_CACHE_HOME:-$HOME/.cache}/telebezel/emulator"
remembered="$keep/lang-pack"
mkdir -p "$keep"

pack="${LANG_PACK:-}"
if test "$pack" = none; then
  rm -f "$remembered"
  pack=""
elif test -n "$pack"; then
  echo "$pack" >"$remembered"
elif test -f "$remembered"; then
  pack=$(cat "$remembered")
fi
stop() { pebble kill >/dev/null 2>&1 || true; sleep 2; }
test -n "$pack" || { stop; exit 0; }

if test "$pack" = ru; then
  pack="$keep/ru_RU.pbl"
  test -f "$pack" || curl -sSfL -o "$pack" https://binaries.rebble.io/lp/d0oGecv-ru_RU.pbl
fi
test -f "$pack" || { echo "Language pack not found: $pack" >&2; exit 1; }
sdk="${PEBBLE_SDK_VERSION:-}"
flash="$HOME/Library/Application Support/Pebble SDK/$sdk/$platform/qemu_spi_flash.bin"
test -d "$(dirname "$flash")" || flash="$HOME/.pebble-sdk/$sdk/$platform/qemu_spi_flash.bin"
marker="$keep/$platform-$sdk.installed"
stamp() { printf '%s %s\n' "$(cksum <"$pack")" "$(ls -i "$flash" 2>/dev/null | awk '{print $1}')"; }
if test -f "$flash" && test -f "$marker" && test "$(cat "$marker")" = "$(stamp)"; then stop; exit 0; fi
for attempt in 1 2 3; do
  if pebble fw --emulator "$platform" install-lang "$pack"; then break; fi
  test "$attempt" -lt 3 || exit 1
  pebble kill >/dev/null 2>&1 || true
  sleep 5
done
stop
stamp >"$marker"
