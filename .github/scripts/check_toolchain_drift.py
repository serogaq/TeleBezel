#!/usr/bin/env python3
"""Fail before builds when reviewed pins drift from their consumers."""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from current_dependencies import composer_version, postgres_image

root = Path(__file__).resolve().parents[2]
pins = dict(line.split("=", 1) for line in (root / "toolchain.env").read_text().splitlines() if "=" in line)
composer = json.loads((root / "backend-api/composer.lock").read_text())
locked_packages = {package["name"] for package in composer["packages"]}
for package in ("laravel/framework", "laravel/octane"):
    if package not in locked_packages:
        raise SystemExit(f"toolchain drift: missing {package} from Composer lock")

def require(path, pattern, expected):
    content = (root / path).read_text()
    found = re.search(pattern, content, re.MULTILINE)
    if not found or found.group(1) != expected:
        raise SystemExit(f"toolchain drift: {path}: expected {expected}, found {found.group(1) if found else 'missing'}")

require("backend-tdlib/cmake/dependencies.lock.cmake", r'set\(TELEBEZEL_TDLIB_COMMIT "([a-f0-9]{40})"\)', pins["TDLIB_COMMIT"])
require("backend-tdlib/CMakeLists.txt", r'set\(CMAKE_CXX_STANDARD ([0-9]+)\)', pins["CXX_STANDARD"])
require("backend-tdlib/cmake/Dependencies.cmake", r'set\(CMAKE_CXX_STANDARD ([0-9]+)\)\s*FetchContent_MakeAvailable\(tdlib\)', pins["TDLIB_CXX_STANDARD"])
composer_version(root)
api_dockerfile = (root / "backend-api/Dockerfile").read_text()
builder_versions = re.findall(
    r'^FROM dunglas/frankenphp:([0-9.]+)-builder-php8\.5-bookworm@sha256:[a-f0-9]{64} AS frankenphp-builder$',
    api_dockerfile, re.MULTILINE,
)
runtime_images = re.findall(
    r'^(FROM dunglas/frankenphp:([0-9.]+)-php8\.5-bookworm@sha256:[a-f0-9]{64}) AS (build|runtime)$',
    api_dockerfile, re.MULTILINE,
)
if (len(builder_versions) != 1 or len(runtime_images) != 2
        or {stage for _, _, stage in runtime_images} != {"build", "runtime"}
        or len({image for image, _, _ in runtime_images}) != 1
        or any(version != builder_versions[0] for _, version, _ in runtime_images)
        or len(re.findall(r'^FROM dunglas/frankenphp:', api_dockerfile, re.MULTILINE)) != 3):
    raise SystemExit("toolchain drift: FrankenPHP image stages")
require("backend-api/Dockerfile", r'CustomVersion=FrankenPHP ([0-9.]+) PHP', builder_versions[0])
for module, pin in (("github.com/getkin/kin-openapi", "FRANKENPHP_KIN_OPENAPI_VERSION"),
                    ("golang.org/x/crypto", "FRANKENPHP_X_CRYPTO_VERSION"),
                    ("google.golang.org/grpc", "FRANKENPHP_GRPC_VERSION")):
    require("backend-api/Dockerfile", rf'^RUN go get .*{re.escape(module)}@v([0-9.]+)', pins[pin])
tdlib_dockerfile = (root / "backend-tdlib/Dockerfile").read_text()
debian_images = re.findall(r'^(FROM debian:[^@\s]+@sha256:[a-f0-9]{64}) AS (build|runtime)$', tdlib_dockerfile, re.MULTILINE)
if (len(debian_images) != 2 or {stage for _, stage in debian_images} != {"build", "runtime"}
        or len({image for image, _ in debian_images}) != 1
        or len(re.findall(r'^FROM debian:', tdlib_dockerfile, re.MULTILINE)) != 2):
    raise SystemExit("toolchain drift: Debian image stages")
require("backend-tdlib/Dockerfile", r'^ARG CMAKE_VERSION=([0-9.]+)$', pins["CMAKE_VERSION"])
postgres_image(root)
require("app/package.json", r'"node": "([0-9.]+)"', pins["NODE_VERSION"])
require("app/package.json", r'"npm": "([0-9.]+)"', pins["NPM_VERSION"])
api_version = (root / "backend-api/VERSION").read_text().strip()
tdlib_service_version = (root / "backend-tdlib/VERSION").read_text().strip()
require("backend-api/Dockerfile", r'^ARG VERSION=([0-9.]+)$', api_version)
require("backend-tdlib/Dockerfile", r'^ARG VERSION=([0-9.]+)$', tdlib_service_version)
require("compose.yaml", r'args: \{VERSION: "\$\{API_VERSION:-([0-9.]+)\}"\}', api_version)
require("compose.yaml", r'args: \{VERSION: "\$\{TDLIB_SERVICE_VERSION:-([0-9.]+)\}"\}', tdlib_service_version)
require("backend-tdlib/tests/contracts/tdlib_status_ready.json", r'"service_version":"([0-9.]+)"', tdlib_service_version)
require("backend-tdlib/tests/contracts/tdlib_status_not_ready.json", r'"service_version":"([0-9.]+)"', tdlib_service_version)
require("backend-tdlib/tests/contracts/tdlib_status_ready.json", r'"tdlib_version":"([0-9.]+)"', pins["TDLIB_VERSION"])
require(".github/scripts/repebble_draft.py", r'^EXPECTED_TOOL_VERSION = "([0-9.]+)"', pins["PEBBLE_TOOL_VERSION"])
