#!/usr/bin/env bash
set -euo pipefail
image="${1:?image is required}"
work_dir=$(mktemp -d)
container="telebezel-api-smoke-${RANDOM}"
trap 'docker rm -f "$container" >/dev/null 2>&1 || true; rm -rf "$work_dir"' EXIT
printf '%s\n' 'base64:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=' > "$work_dir/app_key"
printf '%s\n' 'not-a-production-password' > "$work_dir/db_password"
printf '%s\n' '0123456789abcdef0123456789abcdef' > "$work_dir/internal_token"
bash tests/secrets/provision_file.sh "$work_dir/app_key" 10001
bash tests/secrets/provision_file.sh "$work_dir/db_password" 10001
bash tests/secrets/provision_file.sh "$work_dir/internal_token" 10001
docker run -d --name "$container" --read-only --cap-drop ALL --security-opt no-new-privileges \
  --tmpfs /tmp:rw --tmpfs /config:rw,uid=10001,gid=10001 --tmpfs /data:rw,uid=10001,gid=10001 \
  --tmpfs /app/storage:rw,uid=10001,gid=10001 --tmpfs /app/bootstrap/cache:rw,uid=10001,gid=10001 \
  -e LOG_CHANNEL=stderr -e LOG_STDERR_STREAM=/app/storage/logs/stderr.fifo \
  -e APP_KEY_FILE=/run/secrets/app_key -e DB_PASSWORD_FILE=/run/secrets/db_password \
  -e TDLIB_INTERNAL_TOKEN_FILE=/run/secrets/internal_token \
  --mount "type=bind,src=$work_dir/app_key,dst=/run/secrets/app_key,readonly" \
  --mount "type=bind,src=$work_dir/db_password,dst=/run/secrets/db_password,readonly" \
  --mount "type=bind,src=$work_dir/internal_token,dst=/run/secrets/internal_token,readonly" "$image" >/dev/null
ready=false
for _ in {1..30}; do
  if docker exec "$container" php -r "exit(@file_get_contents('http://127.0.0.1:8080/healthz')===false?1:0);"; then ready=true; break; fi
  sleep 1
done
if [[ "$ready" != true ]]; then docker logs "$container" >&2; exit 1; fi
test "$(docker exec "$container" id -u)" = 10001
expected_version=$(tr -d '\n' < backend-api/VERSION)
docker exec -e EXPECTED_VERSION="$expected_version" "$container" php -r '
  $body = json_decode(file_get_contents("http://127.0.0.1:8080/healthz"), true);
  exit(($body["version"] ?? null) === getenv("EXPECTED_VERSION") ? 0 : 1);
'
test "$(docker inspect -f '{{index .Config.Labels "org.opencontainers.image.version"}}' "$container")" = "$expected_version"
docker logs "$container" 2>&1 | grep -q '"message":"http_request"'
docker exec "$container" sh -c 'test ! -e /app/telebezel_test && test -z "$(find /app -type f \( -name "*.sqlite" -o -name "*.sqlite3" -o -name "*.db" \) -print -quit)"'
test "$(docker inspect -f '{{.HostConfig.ReadonlyRootfs}}' "$container")" = true
test "$(docker inspect -f '{{json .HostConfig.CapDrop}}' "$container")" = '["ALL"]'
