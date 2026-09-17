#!/usr/bin/env bash
set -euo pipefail
image="${1:?image is required}"
work_dir=$(mktemp -d)
container="telebezel-tdlib-smoke-${RANDOM}"
trap 'docker rm -f "$container" >/dev/null 2>&1 || true; rm -rf "$work_dir"' EXIT
printf '%s\n' '0123456789abcdef0123456789abcdef' > "$work_dir/internal_token"
bash tests/secrets/provision_file.sh "$work_dir/internal_token" 10002
docker run -d --name "$container" --read-only --cap-drop ALL --security-opt no-new-privileges \
  --tmpfs /tmp:rw,uid=10002,gid=10002 --tmpfs /var/lib/telebezel/tdlib:rw,uid=10002,gid=10002 \
  -e TDLIB_INTERNAL_TOKEN_FILE=/run/secrets/internal_token \
  --mount "type=bind,src=$work_dir/internal_token,dst=/run/secrets/internal_token,readonly" "$image" >/dev/null
ready=false
for _ in {1..30}; do
  if docker exec "$container" /usr/local/bin/telebezel-tdlib --readycheck; then ready=true; break; fi
  sleep 1
done
if [[ "$ready" != true ]]; then docker logs "$container" >&2; exit 1; fi
test "$(docker exec "$container" id -u)" = 10002
test "$(docker inspect -f '{{index .Config.Labels "org.opencontainers.image.version"}}' "$container")" = "$(tr -d '\n' < backend-tdlib/VERSION)"
docker exec "$container" /usr/local/bin/telebezel-tdlib --wrong-token-check
docker logs "$container" 2>&1 | SECRET_PATH="$work_dir/internal_token" python3 -c '
import json,os,sys
logs=sys.stdin.read()
events=[]
for line in logs.splitlines():
    try: value=json.loads(line)
    except json.JSONDecodeError: continue
    if value.get("event") == "http_request": events.append(value)
assert {200,401}.issubset({event["status"] for event in events})
assert all(event["request_id"] for event in events)
assert open(os.environ["SECRET_PATH"]).read().strip() not in logs, "Internal token appeared in TDLib logs"
'
test "$(docker inspect -f '{{.HostConfig.ReadonlyRootfs}}' "$container")" = true
test "$(docker inspect -f '{{json .HostConfig.CapDrop}}' "$container")" = '["ALL"]'
docker stop --time 5 "$container" >/dev/null
test "$(docker inspect -f '{{.State.ExitCode}}' "$container")" = 0
