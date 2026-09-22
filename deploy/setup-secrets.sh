#!/bin/sh
set -eu

umask 077
mkdir -p secrets
project="${COMPOSE_PROJECT_NAME:-telebezel}"
state_exists=false
for volume in "${project}_postgres-data" "${project}_tdlib-data"; do
  if docker volume inspect "$volume" >/dev/null 2>&1; then state_exists=true; fi
done

missing=false
for name in postgres_password laravel_app_key tdlib_internal_token tdlib_database_master_key; do
  if test ! -s "secrets/$name"; then missing=true; fi
done
if test "$state_exists" = true && test "$missing" = true; then
  echo 'existing data volumes were found but one or more key files are missing; restore the original keys' >&2
  exit 1
fi

test -s secrets/postgres_password || openssl rand -base64 32 > secrets/postgres_password
if test ! -s secrets/laravel_app_key; then
  { printf 'base64:'; openssl rand -base64 32 | tr -d '\n'; printf '\n'; } > secrets/laravel_app_key
fi
test -s secrets/tdlib_internal_token || openssl rand -hex 32 > secrets/tdlib_internal_token
test -s secrets/tdlib_database_master_key || openssl rand -hex 32 > secrets/tdlib_database_master_key

echo 'Secrets are ready. Existing files were preserved.'
