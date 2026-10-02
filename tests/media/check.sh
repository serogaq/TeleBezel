#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
source "$root/toolchain.env"

steps=$(cat <<SCRIPT
set -eu
test -z "\$(go fmt ./...)"
go vet ./...
go run honnef.co/go/tools/cmd/staticcheck@${STATICCHECK_VERSION} ./...
go run golang.org/x/vuln/cmd/govulncheck@${GOVULNCHECK_VERSION} ./...
TELEBEZEL_REQUIRE_CONTRACTS=1 go test -race ./...
go test -run '^\$' -fuzz FuzzDecode -fuzztime 10s ./internal/tbi
go test -run '^\$' -fuzz FuzzRender -fuzztime 10s ./internal/render
go test -run '^\$' -bench . -benchtime 10x ./internal/render
SCRIPT
)

if test "${MEDIA_DOCKER:-0}" = 1; then
  image=$(sed -n 's/^FROM \(golang:[^ ]*\) AS build$/\1/p' "$root/backend-media/Dockerfile")
  docker run --rm -v "$root:/repo" -w /repo/backend-media "$image" sh -c "$steps"
else
  go_bin="${GO_BIN:-$(command -v go || true)}"
  test -n "$go_bin" || { echo "Go ${GO_VERSION} is required, or run with MEDIA_DOCKER=1" >&2; exit 1; }
  "$go_bin" version | grep -F "go${GO_VERSION} " >/dev/null || { echo "Go ${GO_VERSION} is required" >&2; exit 1; }
  (cd "$root/backend-media" && PATH="$(dirname "$go_bin"):$PATH" sh -c "$steps")
fi
