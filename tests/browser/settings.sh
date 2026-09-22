#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
php_bin="${PHP_BIN:-php}"
fixture_bin="${TDLIB_FIXTURE_BIN:?The scripted TDLib fixture is required}"
command -v agent-browser >/dev/null
work=$(mktemp -d)
session="telebezel-settings-${RANDOM}-${RANDOM}"
cleanup() {
  agent-browser --session "$session" close >/dev/null 2>&1 || true
  if test -n "${laravel_pid:-}"; then kill "$laravel_pid" 2>/dev/null || true; fi
  if test -n "${fixture_pid:-}"; then kill "$fixture_pid" 2>/dev/null || true; fi
  if test -n "${proxy_pid:-}"; then kill "$proxy_pid" 2>/dev/null || true; fi
  wait "${laravel_pid:-0}" 2>/dev/null || true
  wait "${fixture_pid:-0}" 2>/dev/null || true
  wait "${proxy_pid:-0}" 2>/dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT

ports=$(python3 - <<'PY'
import socket
result = []
for _ in range(3):
    sock = socket.socket()
    sock.bind(('127.0.0.1', 0))
    result.append(str(sock.getsockname()[1]))
    sock.close()
print(' '.join(result))
PY
)
read -r fixture_port laravel_port proxy_port <<< "$ports"
"$fixture_bin" "$fixture_port" "$work/tdlib" > "$work/fixture.log" 2>&1 &
fixture_pid=$!
export APP_ENV=testing APP_KEY='base64:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA='
export CACHE_STORE=database SESSION_DRIVER=database
export TDLIB_BASE_URL="http://127.0.0.1:$fixture_port" TDLIB_INTERNAL_TOKEN=integration-internal-token
cd "$root/backend-api"
bootstrap_output=$("$php_bin" artisan telebezel:bootstrap-code --no-ansi)
bootstrap_code=$(printf '%s\n' "$bootstrap_output" | python3 -c 'import re, sys; print(re.search(r"[A-Z0-9]{8}-[A-Z0-9]{8}", sys.stdin.read()).group())')
test -n "$bootstrap_code"
"$php_bin" artisan serve --host=127.0.0.1 --port="$laravel_port" > "$work/laravel.log" 2>&1 &
laravel_pid=$!
python3 "$root/tests/browser/lost_response_proxy.py" "$proxy_port" "$laravel_port" "$work/create-requests.jsonl" > "$work/proxy.log" 2>&1 &
proxy_pid=$!
base="http://127.0.0.1:$proxy_port"
for _ in {1..100}; do
  if curl -fsS "$base/settings" >/dev/null 2>&1; then break; fi
  sleep 0.1
done
curl -fsS "$base/settings" >/dev/null

ab() { agent-browser --session "$session" "$@"; }
assert_eval() {
  local expression="$1" expected="$2" actual
  for _ in {1..100}; do
    actual=$(ab eval "$expression")
    if test "$actual" = "$expected"; then return; fi
    sleep 0.1
  done
  printf 'Browser assertion failed: %s returned %s; expected %s\n' "$expression" "$actual" "$expected" >&2
  ab eval "document.querySelector('#status').textContent" >&2 || true
  tail -n 8 "$work/laravel.log" >&2 || true
  exit 1
}

ab open "$base/settings" >/dev/null
assert_eval "document.querySelector('#access').hidden" false
assert_eval "document.querySelector('#configuration').hidden" true
ab fill '#bootstrap [name=bootstrap_code]' "$bootstrap_code" >/dev/null
test "$(ab get value '#bootstrap [name=bootstrap_code]')" = "$bootstrap_code"
ab fill '#bootstrap [name=password]' 'correct horse battery staple' >/dev/null
ab click '#bootstrap button' >/dev/null
assert_eval "document.querySelector('#configuration').hidden" false
assert_eval "document.querySelector('#recovery').hidden" false
recovery_code=$(ab eval "document.querySelector('#recovery').textContent.split('\\n').at(-1)" | python3 -c 'import json, sys; print(json.load(sys.stdin))')
test -n "$recovery_code"

# The proxy forwards the first create to Laravel, then drops its response.
# The retry must reuse the same payload and key, returning the same account.
ab fill '#account-add [name=label]' 'Browser account' >/dev/null
ab click '#account-add button' >/dev/null
assert_eval "sessionStorage.getItem('telebezel.pending-account-create') !== null" true
assert_eval "JSON.parse(sessionStorage.getItem('telebezel.pending-account-create')).body === JSON.stringify({label:'Browser account',proxy:{mode:'inherit'}})" true
ab click '#account-add button' >/dev/null
assert_eval "sessionStorage.getItem('telebezel.pending-account-create') === null" true
assert_eval "document.querySelectorAll('#accounts .item').length" 1
python3 - "$work/create-requests.jsonl" <<'PY'
import json
import sys

with open(sys.argv[1], encoding='utf-8') as source:
    requests = [json.loads(line) for line in source]
assert len(requests) == 2, requests
assert requests[0]['body'] == requests[1]['body'] == {'label': 'Browser account', 'proxy': {'mode': 'inherit'}}
assert requests[0]['key'] and requests[0]['key'] == requests[1]['key']
assert requests[0]['status'] == 202 and requests[1]['status'] == 200
assert requests[0]['id'] == requests[1]['id']
PY

ab click '#sign-out' >/dev/null
assert_eval "document.querySelector('#access').hidden" false
ab fill '#login [name=password]' 'correct horse battery staple' >/dev/null
ab click '#login button' >/dev/null
assert_eval "document.querySelector('#configuration').hidden" false
ab click '#sign-out' >/dev/null
assert_eval "document.querySelector('#access').hidden" false
ab fill '#recover [name=recovery_code]' "$recovery_code" >/dev/null
ab fill '#recover [name=password]' 'new correct horse battery staple' >/dev/null
ab click '#recover button' >/dev/null
assert_eval "document.querySelector('#configuration').hidden" false
assert_eval "document.querySelectorAll('#accounts .item').length" 1
printf 'Settings browser flow passed: bootstrap, retry, login, recovery\n'
