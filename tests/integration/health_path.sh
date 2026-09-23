#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
project="telebezel-integration-$$"
compose=(docker compose -p "$project" -f compose.yaml -f tests/integration/compose.yaml)
work_dir=$(mktemp -d)
export TEST_SECRET_DIR="$work_dir"
export API_PORT="${API_PORT:-18080}"
cleanup() {
  status=$?
  if [[ $status -ne 0 ]]; then
    "${compose[@]}" logs --no-color --no-log-prefix || true
  fi
  "${compose[@]}" down --volumes --remove-orphans >/dev/null 2>&1 || true
  rm -rf "$work_dir"
  exit "$status"
}
trap cleanup EXIT
umask 077
openssl rand -base64 32 > "$work_dir/postgres_password"
openssl rand -hex 32 > "$work_dir/tdlib_internal_token"
openssl rand -hex 32 > "$work_dir/tdlib_database_master_key"
{ printf 'base64:'; openssl rand -base64 32 | tr -d '\n'; printf '\n'; } > "$work_dir/laravel_app_key"
bash tests/secrets/provision_file.sh "$work_dir/postgres_password" 999 10001
bash tests/secrets/provision_file.sh "$work_dir/tdlib_internal_token" 10001 10002
bash tests/secrets/provision_file.sh "$work_dir/tdlib_database_master_key" 10002
bash tests/secrets/provision_file.sh "$work_dir/laravel_app_key" 10001
"${compose[@]}" config --format json | python3 tests/integration/assert_compose.py
curl_bounded() { curl --connect-timeout 2 --max-time 12 "$@"; }
up_args=()
if [[ "${TELEBEZEL_INTEGRATION_PREBUILT:-0}" == 1 ]]; then
  owner=${GHCR_OWNER:-serogaq}
  docker image inspect "ghcr.io/$owner/telebezel-backend-api:local" "ghcr.io/$owner/telebezel-backend-tdlib:local" >/dev/null
  up_args=(--no-build)
else
  "${compose[@]}" build backend-api backend-tdlib
fi
"${compose[@]}" up "${up_args[@]}" -d postgres backend-tdlib
"${compose[@]}" run --rm -e DB_STATEMENT_TIMEOUT=30000 -e DB_LOCK_TIMEOUT=5000 backend-api php artisan telebezel:migrate-locked
issue_output=$("${compose[@]}" run --rm backend-api php artisan telebezel:api-client-issue integration)
token=$(printf '%s\n' "$issue_output" | awk '/^tb_[A-Za-z0-9_-]+$/ {print; exit}')
client_id=$(printf '%s\n' "$issue_output" | awk -F': ' '/^Client ID:/ {print $2; exit}')
test -n "$token"
test -n "$client_id"
issue_output_b=$("${compose[@]}" run --rm backend-api php artisan telebezel:api-client-issue integration-second)
token_b=$(printf '%s\n' "$issue_output_b" | awk '/^tb_[A-Za-z0-9_-]+$/ {print; exit}')
test -n "$token_b"
"${compose[@]}" up "${up_args[@]}" -d backend-api ingress
ready=false
for _ in {1..60}; do
  if curl_bounded -fsS "http://127.0.0.1:$API_PORT/healthz" >/dev/null; then ready=true; break; fi
  sleep 1
done
test "$ready" = true
status=$(curl_bounded -fsS -H "Authorization: Bearer $token" "http://127.0.0.1:$API_PORT/v1/status")
python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["data"]["dependencies"]=={"postgres":"ready","tdlib":"ready"}' <<<"$status"
status_b=$(curl_bounded -fsS -H "Authorization: Bearer $token_b" "http://127.0.0.1:$API_PORT/v1/status")
python3 -c 'import json,sys; a=json.loads(sys.argv[1]); b=json.loads(sys.argv[2]); assert a["request_id"] != b["request_id"]' "$status" "$status_b"
create_code=$(curl_bounded -sS -o "$work_dir/account-a.json" -w '%{http_code}' \
  -H "Authorization: Bearer $token_b" -H 'Idempotency-Key: integration-account-a' -H 'Content-Type: application/json' \
  -d '{"label":"Integration A","proxy":{"id":"91112233-4455-4677-8899-aabbccddeeff","mode":"direct"}}' \
  "http://127.0.0.1:$API_PORT/v1/telegram/accounts")
test "$create_code" = 202
account_a=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["data"]["id"])' "$work_dir/account-a.json")
repeat_code=$(curl_bounded -sS -o "$work_dir/account-a-repeat.json" -w '%{http_code}' \
  -H "Authorization: Bearer $token_b" -H 'Idempotency-Key: integration-account-a' -H 'Content-Type: application/json' \
  -d '{"label":"Integration A","proxy":{"id":"91112233-4455-4677-8899-aabbccddeeff","mode":"direct"}}' \
  "http://127.0.0.1:$API_PORT/v1/telegram/accounts")
test "$repeat_code" = 200
create_code=$(curl_bounded -sS -o "$work_dir/account-b.json" -w '%{http_code}' \
  -H "Authorization: Bearer $token_b" -H 'Idempotency-Key: integration-account-b' -H 'Content-Type: application/json' \
  -d '{"label":"Integration B"}' \
  "http://127.0.0.1:$API_PORT/v1/telegram/accounts")
test "$create_code" = 202
account_b=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["data"]["id"])' "$work_dir/account-b.json")
test "$account_a" != "$account_b"
"${compose[@]}" run --rm backend-api php artisan telebezel:accounts-reconcile >/dev/null
authorization=''
for _ in {1..15}; do
  authorization=$(curl_bounded -fsS -H "Authorization: Bearer $token_b" \
    "http://127.0.0.1:$API_PORT/v1/telegram/accounts/$account_a/authorization")
  if python3 -c 'import json,sys; assert json.loads(sys.argv[1])["data"]["state"] in {"initializing","awaiting_phone_number"}' "$authorization" 2>/dev/null; then
    break
  fi
  sleep 2
done
account_snapshot=$(curl_bounded -fsS -H "Authorization: Bearer $token_b" \
  "http://127.0.0.1:$API_PORT/v1/telegram/accounts/$account_a")
python3 -c 'import json,sys; d=json.loads(sys.argv[1])["data"]; account=json.loads(sys.argv[2])["data"]; assert d["state"] in {"initializing","awaiting_phone_number"}, {"authorization":d,"account":account}; assert d["authorization_version"], d' "$authorization" "$account_snapshot"
for _ in {1..59}; do curl_bounded -fsS -H "Authorization: Bearer $token" "http://127.0.0.1:$API_PORT/v1/status" >/dev/null; done
code=$(curl_bounded -sS -o "$work_dir/rate-limited.json" -w '%{http_code}' -H "Authorization: Bearer $token" "http://127.0.0.1:$API_PORT/v1/status")
test "$code" = 429
curl_bounded -fsS -H "Authorization: Bearer $token_b" "http://127.0.0.1:$API_PORT/v1/status" >/dev/null
"${compose[@]}" run --rm backend-api php artisan telebezel:api-client-revoke "$client_id" >/dev/null
code=$(curl_bounded -sS -o "$work_dir/revoked.json" -w '%{http_code}' -H "Authorization: Bearer $token" "http://127.0.0.1:$API_PORT/v1/status")
test "$code" = 401
curl_bounded -fsS -H "Authorization: Bearer $token_b" "http://127.0.0.1:$API_PORT/v1/status" >/dev/null
"${compose[@]}" stop backend-tdlib
code=$(curl_bounded -sS -o "$work_dir/tdlib-down.json" -w '%{http_code}' -H "Authorization: Bearer $token_b" "http://127.0.0.1:$API_PORT/v1/status")
test "$code" = 503
grep -q 'service.tdlib_unavailable' "$work_dir/tdlib-down.json"
"${compose[@]}" start backend-tdlib
tdlib_ready=false
for _ in {1..30}; do
  if "${compose[@]}" exec -T backend-tdlib /usr/local/bin/telebezel-tdlib --readycheck; then tdlib_ready=true; break; fi
  sleep 1
done
test "$tdlib_ready" = true
before_reconcile=$(curl_bounded -fsS -H "Authorization: Bearer $token_b" \
  "http://127.0.0.1:$API_PORT/v1/telegram/accounts/$account_a")
python3 -c 'import json,sys; assert json.load(sys.stdin)["data"]["runtime"]["available"] is False' <<<"$before_reconcile"
"${compose[@]}" run --rm backend-api php artisan telebezel:accounts-reconcile >/dev/null
after_reconcile=$(curl_bounded -fsS -H "Authorization: Bearer $token_b" \
  "http://127.0.0.1:$API_PORT/v1/telegram/accounts/$account_a")
python3 -c 'import json,sys; assert json.load(sys.stdin)["data"]["runtime"]["available"] is True' <<<"$after_reconcile"
"${compose[@]}" stop postgres
code=$(curl_bounded -sS -o "$work_dir/postgres-down.json" -w '%{http_code}' -H "Authorization: Bearer $token_b" "http://127.0.0.1:$API_PORT/v1/status")
test "$code" = 503
grep -q 'service.database_unavailable' "$work_dir/postgres-down.json"
curl_bounded -fsS "http://127.0.0.1:$API_PORT/healthz" >/dev/null
"${compose[@]}" start postgres
"${compose[@]}" exec -T backend-api php artisan tinker --execute='echo config("logging.default");' | grep -q stderr
"${compose[@]}" logs --no-color --no-log-prefix backend-api | python3 -c '
import json,sys
events=[]
for line in sys.stdin:
    try: item=json.loads(line)
    except json.JSONDecodeError: continue
    if item.get("message") == "http_request": events.append(item)
assert events, "Missing structured HTTP request logs"
assert {200,401,429,503}.issubset({event["context"]["status"] for event in events})
assert all(event["context"].get("request_id") for event in events)
assert all("authorization" not in event["context"] and "token" not in event["context"] for event in events)
'
"${compose[@]}" exec -T backend-tdlib touch /var/lib/telebezel/tdlib/stage0-volume-sentinel
"${compose[@]}" up "${up_args[@]}" -d --force-recreate postgres backend-tdlib backend-api ingress
test "$("${compose[@]}" exec -T backend-tdlib sh -c 'test -f /var/lib/telebezel/tdlib/stage0-volume-sentinel && echo yes')" = yes
ready=false
for _ in {1..30}; do
  if curl_bounded -fsS -H "Authorization: Bearer $token_b" "http://127.0.0.1:$API_PORT/v1/status" >/dev/null; then ready=true; break; fi
  sleep 1
done
test "$ready" = true
"${compose[@]}" logs --no-color | PUBLIC_TOKEN="$token" SECOND_TOKEN="$token_b" python3 -c '
import os,sys
logs=sys.stdin.read()
secrets=[os.environ["PUBLIC_TOKEN"],os.environ["SECOND_TOKEN"]]
for name in ("postgres_password","tdlib_internal_token","tdlib_database_master_key","laravel_app_key"):
    secrets.append(open(os.path.join(os.environ["TEST_SECRET_DIR"],name)).read().strip())
assert all(secret not in logs for secret in secrets), "A secret appeared in Compose logs"
assert "account_awaiting_reconciliation" in logs
'
