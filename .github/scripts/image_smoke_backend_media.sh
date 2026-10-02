#!/usr/bin/env bash
set -euo pipefail

image="$1"
token=$(openssl rand -hex 32)
name="telebezel-media-smoke-$RANDOM"
cleanup() { docker rm -f "$name" >/dev/null 2>&1 || true; }
trap cleanup EXIT

test "$(docker image inspect --format '{{.Config.User}}' "$image")" = 10003:10003
if docker run --rm --entrypoint /bin/sh "$image" -c true >/dev/null 2>&1; then
  echo 'the media image must not contain a shell' >&2
  exit 1
fi
docker run -d --name "$name" --read-only --cap-drop ALL --security-opt no-new-privileges:true --network none \
  -e MEDIA_INTERNAL_TOKEN="$token" "$image" >/dev/null
for _ in $(seq 1 50); do
  if docker exec "$name" /telebezel-media --healthcheck; then break; fi
  sleep 0.2
done
docker exec "$name" /telebezel-media --healthcheck
if docker logs "$name" 2>&1 | grep -F "$token"; then
  echo 'the media token appeared in the logs' >&2
  exit 1
fi
echo "backend-media image smoke passed"
