#!/bin/sh
set -eu

load_secret() {
  value_name="$1"
  file_name="${value_name}_FILE"
  eval "file_path=\${$file_name:-}"
  if [ -n "$file_path" ]; then
    if [ ! -r "$file_path" ]; then
      echo "secret file for $value_name is not readable" >&2
      exit 1
    fi
    value=$(sed -e 's/[[:space:]]*$//' "$file_path")
    export "$value_name=$value"
  fi
}

load_secret APP_KEY
load_secret DB_PASSWORD
load_secret TDLIB_INTERNAL_TOKEN

if [ -z "${APP_KEY:-}" ] || [ -z "${DB_PASSWORD:-}" ] || [ -z "${TDLIB_INTERNAL_TOKEN:-}" ]; then
  echo "required application secrets are missing" >&2
  exit 1
fi

mkdir -p storage/framework/cache/data storage/framework/sessions storage/framework/views storage/logs bootstrap/cache
if [ "${LOG_STDERR_STREAM:-}" = /app/storage/logs/stderr.fifo ]; then
  mkfifo "$LOG_STDERR_STREAM"
  (while :; do cat "$LOG_STDERR_STREAM" >&2; done) &
fi
exec "$@"
