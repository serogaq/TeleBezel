#!/usr/bin/env bash
set -euo pipefail

secret_file="${1:?secret file required}"
shift
test -f "$secret_file"
chmod 600 "$secret_file"

# Docker Desktop translates host filesystem ownership. Native Linux bind mounts
# preserve it; grant read access to only the container UIDs needing this file.
if [[ "$(uname -s)" == Linux ]]; then
  command -v setfacl >/dev/null || { echo 'Install the acl package before provisioning Docker secrets' >&2; exit 1; }
  for uid in "$@"; do
    [[ "$uid" =~ ^[0-9]+$ ]] || exit 1
    setfacl -m "u:${uid}:r" "$secret_file"
  done
fi
