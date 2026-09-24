#!/usr/bin/env bash
# Confirms with the real TDLib that history already downloaded into the message
# database survives a runtime restart and is readable with Telegram egress
# blocked. It needs a real authorized Telegram account, so it is never part of
# `make check`; run it deliberately against a stack you control.
#
# Required environment:
#   TELEBEZEL_OFFLINE_COMPOSE_PROJECT  Compose project holding the running stack
#   TELEBEZEL_OFFLINE_API              Public API base URL, e.g. http://127.0.0.1:18080
#   TELEBEZEL_OFFLINE_TOKEN            Device bearer for that API (reads history)
#   TELEBEZEL_OFFLINE_MAINTENANCE_TOKEN  Maintenance bearer for that API (logout)
#   TELEBEZEL_OFFLINE_ACCOUNT          UUID of an authorized Telegram account
#   TELEBEZEL_OFFLINE_CHAT             Chat ID of an ordinary chat
#   TELEBEZEL_OFFLINE_CHANNEL          Chat ID of a channel
set -euo pipefail

for variable in TELEBEZEL_OFFLINE_COMPOSE_PROJECT TELEBEZEL_OFFLINE_API TELEBEZEL_OFFLINE_TOKEN TELEBEZEL_OFFLINE_MAINTENANCE_TOKEN \
  TELEBEZEL_OFFLINE_ACCOUNT TELEBEZEL_OFFLINE_CHAT TELEBEZEL_OFFLINE_CHANNEL; do
  if [[ -z "${!variable:-}" ]]; then
    printf 'offline_restart.sh requires %s\n' "$variable" >&2
    exit 2
  fi
done

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
project="$TELEBEZEL_OFFLINE_COMPOSE_PROJECT"
base="$TELEBEZEL_OFFLINE_API"
account="$TELEBEZEL_OFFLINE_ACCOUNT"
view=$(python3 -c 'import uuid; print(uuid.uuid4())')
compose=(docker compose -p "$project" -f "$root/compose.yaml")
work=$(mktemp -d)
egress_detached=0

cleanup() {
  status=$?
  if [[ $egress_detached -eq 1 ]]; then
    docker network connect "${project}_tdlib-egress" "${project}-backend-tdlib-1" >/dev/null 2>&1 || true
  fi
  rm -rf "$work"
  exit "$status"
}
trap cleanup EXIT

read_history() {
  curl --connect-timeout 2 --max-time 20 -sS -H "Authorization: Bearer $TELEBEZEL_OFFLINE_TOKEN" \
    "$base/v1/telegram/accounts/$account/chats/$1/messages?view_id=$view&limit=20"
}

collect_ids() {
  python3 -c 'import json,sys; print(" ".join(item["id"] for item in json.load(sys.stdin)["data"]["items"]))'
}

# 1. Warm the message database while Telegram is reachable.
for chat in "$TELEBEZEL_OFFLINE_CHAT" "$TELEBEZEL_OFFLINE_CHANNEL"; do
  for _ in {1..10}; do
    read_history "$chat" > "$work/online-$chat.json"
    if [[ $(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["data"]["items"]))' "$work/online-$chat.json") -gt 0 ]]; then
      break
    fi
    sleep 2
  done
  ids=$(collect_ids < "$work/online-$chat.json")
  test -n "$ids"
  printf '%s\n' "$ids" > "$work/expected-$chat.txt"
done

# 2. Stop the runtime, then cut its Telegram egress before it comes back.
"${compose[@]}" stop backend-tdlib
docker network disconnect "${project}_tdlib-egress" "${project}-backend-tdlib-1"
egress_detached=1

# 3. Start again on the same volume and the same master key.
"${compose[@]}" start backend-tdlib
"${compose[@]}" exec -T backend-api php artisan telebezel:accounts-reconcile

# 4. The previously downloaded history must come back from disk, not from a
#    network fallback: the read has to answer well inside the local deadline.
for chat in "$TELEBEZEL_OFFLINE_CHAT" "$TELEBEZEL_OFFLINE_CHANNEL"; do
  started=$(python3 -c 'import time; print(time.monotonic())')
  read_history "$chat" > "$work/offline-$chat.json"
  elapsed=$(python3 -c 'import sys,time; print(time.monotonic()-float(sys.argv[1]))' "$started")
  python3 - "$work/offline-$chat.json" "$work/expected-$chat.txt" "$elapsed" <<'PY'
import json
import sys

page = json.load(open(sys.argv[1], encoding='utf-8'))['data']
expected = open(sys.argv[2], encoding='utf-8').read().split()
returned = [item['id'] for item in page['items']]
missing = [identifier for identifier in expected if identifier not in returned]
assert not missing, f'offline read lost {missing}'
assert float(sys.argv[3]) < 5, f'offline read waited {sys.argv[3]}s for a network fallback'
assert page['partial'] in (True, False)
assert page['local_exhausted'] in (True, False)
assert page['connection'] != 'ready', 'Telegram still reachable; egress was not blocked'
PY
done

# 5. History that was never downloaded must be reported, not invented.
python3 - "$work/offline-$TELEBEZEL_OFFLINE_CHAT.json" <<'PY'
import json
import sys

page = json.load(open(sys.argv[1], encoding='utf-8'))['data']
if page['next_cursor'] is None and not page['local_exhausted']:
    assert page['partial'] is True, 'an incomplete offline page must be marked partial'
PY

# 6. Restoring the network must refresh rather than require a restart.
docker network connect "${project}_tdlib-egress" "${project}-backend-tdlib-1"
egress_detached=0
recovered=false
for _ in {1..30}; do
  read_history "$TELEBEZEL_OFFLINE_CHAT" > "$work/recovered.json"
  if python3 -c 'import json,sys; sys.exit(0 if json.load(open(sys.argv[1]))["data"]["connection"] == "ready" else 1)' "$work/recovered.json"; then
    recovered=true
    break
  fi
  sleep 2
done
test "$recovered" = true

# 7. A logout followed by a new authorization must not expose the old cache.
curl --connect-timeout 2 --max-time 20 -sS -X POST -H "Authorization: Bearer $TELEBEZEL_OFFLINE_MAINTENANCE_TOKEN" \
  -H 'Content-Type: application/json' -d '{}' \
  "$base/v1/telegram/accounts/$account/logout" > "$work/logout.json"
"${compose[@]}" exec -T backend-api php artisan telebezel:accounts-reconcile
read_history "$TELEBEZEL_OFFLINE_CHAT" > "$work/after-logout.json"
python3 - "$work/after-logout.json" <<'PY'
import json
import sys

body = json.load(open(sys.argv[1], encoding='utf-8'))
if 'data' in body:
    assert body['data']['items'] == [], 'the previous session\'s history survived a logout'
else:
    assert body['error']['code'] in ('authorization.invalid_state', 'sync.resync_required'), body
PY

printf 'Offline restart verified: cached history readable without Telegram egress\n'
