#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
php_bin="${PHP_BIN:-php}"
composer_bin="${COMPOSER_BIN:-composer}"
container="telebezel-api-test-${RANDOM}-${RANDOM}"
database="telebezel_disposable_${RANDOM}_${RANDOM}"
image=$(python3 "$root/.github/scripts/current_dependencies.py" postgres-image)
trap 'docker rm -f "$container" >/dev/null 2>&1 || true' EXIT

docker run -d --name "$container" \
  -e POSTGRES_DB="$database" \
  -e POSTGRES_USER=telebezel \
  -e POSTGRES_PASSWORD=telebezel_test \
  -p 127.0.0.1::5432 "$image" >/dev/null

for _ in {1..30}; do
  if docker exec "$container" pg_isready -U telebezel -d "$database" >/dev/null; then break; fi
  sleep 1
done
docker exec "$container" pg_isready -U telebezel -d "$database" >/dev/null
mapping=$(docker port "$container" 5432/tcp)
port="${mapping##*:}"

cd "$root/backend-api"
if test -f bootstrap/cache/config.php; then
  echo 'Refusing database tests with a cached Laravel configuration' >&2
  exit 1
fi
node --test tests/Js/*Test.js
"$php_bin" "$composer_bin" install --no-interaction --prefer-dist
"$php_bin" "$composer_bin" validate --strict
"$php_bin" "$composer_bin" lint
"$php_bin" vendor/bin/phpstan analyse --memory-limit=1G
export APP_ENV=testing DB_CONNECTION=pgsql DB_URL= DB_HOST=127.0.0.1 DB_PORT="$port"
export PGCONNECT_TIMEOUT=2 PGOPTIONS='-c statement_timeout=2000 -c lock_timeout=500'
export DB_DATABASE="$database" DB_USERNAME=telebezel DB_PASSWORD=telebezel_test
export TELEBEZEL_DISPOSABLE_DB="$database" TELEBEZEL_DISPOSABLE_PORT="$port"
"$php_bin" "$root/tests/api/assert_disposable_db.php"
"$php_bin" artisan migrate:fresh --force --database=pgsql
"$php_bin" artisan migrate:reset --force --database=pgsql
"$php_bin" artisan migrate --force --database=pgsql
"$php_bin" vendor/bin/pest

if test -n "${TDLIB_FIXTURE_BIN:-}"; then
  "$php_bin" vendor/bin/pest tests/Integration
  PHP_BIN="$php_bin" bash "$root/tests/browser/settings.sh"
fi
