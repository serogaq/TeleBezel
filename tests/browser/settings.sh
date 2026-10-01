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
  if curl -fsS "http://127.0.0.1:$laravel_port/settings" >/dev/null 2>&1; then break; fi
  sleep 0.1
done
for _ in {1..50}; do
  if curl -fsS "$base/settings" >/dev/null 2>&1; then break; fi
  sleep 0.1
done
curl -fsS "$base/settings" >/dev/null

ab() { agent-browser --session "$session" "$@"; }
press() {
  ab scrollintoview "$1" >/dev/null
  ab click "$1" >/dev/null
}
assert_eval() {
  local expression="$1" expected="$2" actual
  for _ in {1..100}; do
    actual=$(ab eval "$expression")
    if test "$actual" = "$expected"; then return; fi
    sleep 0.1
  done
  printf 'Browser assertion failed at %s: %s returned %s; expected %s\n' "$step" "$expression" "$actual" "$expected" >&2
  ab eval "JSON.stringify({url: location.href, ready: document.readyState, status: document.querySelector('#status').textContent,
    access: document.querySelector('#access').hidden, configuration: document.querySelector('#configuration').hidden,
    forms: [...document.querySelectorAll('#access form')].map(form => ({id: form.id, valid: form.checkValidity(),
      fields: [...form.elements].filter(field => field.name).map(field => ({name: field.name, length: field.value.length, message: field.validationMessage}))})),
    buttons: [...document.querySelectorAll('#access button')].map(button => { const box = button.getBoundingClientRect(); const top = document.elementFromPoint(box.x + box.width / 2, box.y + box.height / 2);
      return {form: button.form?.id, box: [Math.round(box.x), Math.round(box.y), Math.round(box.width), Math.round(box.height)], hit: top ? top.tagName + '#' + top.id + '.' + top.className : null}; }),
    viewport: [innerWidth, innerHeight],
    requests: performance.getEntriesByType('resource').filter(entry => entry.name.includes('/v1/')).map(entry => [entry.name.replace(location.origin, ''), entry.responseStatus, Math.round(entry.duration)])})" >&2 || true
  printf -- '--- page errors\n' >&2; ab errors >&2 || true
  printf -- '--- console\n' >&2; ab console >&2 || true
  printf -- '--- browser requests\n' >&2; ab network requests --filter /v1/ >&2 || true
  printf -- '--- proxy\n' >&2; cat "$work/proxy.log" >&2 || true
  printf -- '--- laravel\n' >&2; cat "$work/laravel.log" >&2 || true
  exit 1
}
step=start

sign_out() {
  ab eval "window.telebezelPage = 'old'" >/dev/null
  press '#sign-out'
  assert_eval "window.telebezelPage === undefined && document.querySelector('#access').hidden === false" true
}

step=bootstrap
ab set viewport 1280 1024 >/dev/null
ab open "$base/settings" >/dev/null
assert_eval "document.querySelector('#access').hidden" false
assert_eval "document.querySelector('#configuration').hidden" true
ab fill '#bootstrap [name=bootstrap_code]' "$bootstrap_code" >/dev/null
test "$(ab get value '#bootstrap [name=bootstrap_code]')" = "$bootstrap_code"
ab fill '#bootstrap [name=password]' 'correct horse battery staple' >/dev/null
press '#bootstrap button'
assert_eval "document.querySelector('#configuration').hidden" false
assert_eval "document.querySelector('#recovery').hidden" false
recovery_code=$(ab eval "document.querySelector('#recovery').textContent.split('\\n').at(-1)" | python3 -c 'import json, sys; print(json.load(sys.stdin))')
test -n "$recovery_code"

# The proxy forwards the first create to Laravel, then drops its response.
# The retry must reuse the same payload and key, returning the same account.
step=account-retry
ab fill '#account-add [name=label]' 'Browser account' >/dev/null
press '#account-add button'
assert_eval "sessionStorage.getItem('telebezel.pending-account-create') !== null" true
assert_eval "JSON.parse(sessionStorage.getItem('telebezel.pending-account-create')).body === JSON.stringify({label:'Browser account',proxy:{mode:'inherit'}})" true
press '#account-add button'
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

step=login
sign_out
ab fill '#login [name=password]' 'correct horse battery staple' >/dev/null
press '#login button'
assert_eval "document.querySelector('#configuration').hidden" false
step=recover
sign_out
ab fill '#recover [name=recovery_code]' "$recovery_code" >/dev/null
ab fill '#recover [name=password]' 'new correct horse battery staple' >/dev/null
press '#recover button'
assert_eval "document.querySelector('#configuration').hidden" false
assert_eval "document.querySelectorAll('#accounts .item').length" 1

step=language
ab select '#language' ru >/dev/null
assert_eval "document.documentElement.lang" '"ru"'
assert_eval "document.querySelector('#configuration').hidden" false
assert_eval "document.querySelector('#account-add button').textContent" '"Добавить аккаунт"'
ab screenshot "${SETTINGS_SCREENSHOT:-$work/settings-ru.png}" >/dev/null
ab select '#language' en >/dev/null
assert_eval "document.documentElement.lang" '"en"'
printf 'Settings browser flow passed: bootstrap, retry, login, recovery, language\n'
