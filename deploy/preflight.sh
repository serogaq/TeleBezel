#!/bin/sh
set -eu

command -v docker >/dev/null 2>&1 || { echo 'Docker Engine is required' >&2; exit 1; }
docker compose version >/dev/null
docker info >/dev/null
docker compose config --quiet

for name in postgres_password laravel_app_key tdlib_internal_token tdlib_database_master_key; do
  path="secrets/$name"
  test -s "$path" || { echo "missing secret: $path" >&2; exit 1; }
  test "$(wc -c < "$path" | tr -d ' ')" -le 512 || { echo "invalid secret size: $path" >&2; exit 1; }
done

case "$(cat secrets/laravel_app_key)" in base64:*) ;; *) echo 'invalid APP_KEY format' >&2; exit 1;; esac
test "$(tr -d '\r\n' < secrets/tdlib_internal_token | wc -c | tr -d ' ')" -eq 64 || { echo 'invalid internal bearer format' >&2; exit 1; }
test "$(tr -d '\r\n' < secrets/tdlib_database_master_key | wc -c | tr -d ' ')" -eq 64 || { echo 'invalid TDLib master key format' >&2; exit 1; }

echo 'TeleBezel preflight passed.'
