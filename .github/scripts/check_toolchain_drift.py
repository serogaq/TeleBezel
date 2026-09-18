#!/usr/bin/env python3
"""Fail before builds when reviewed pins drift from their consumers."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
pins = dict(line.split("=", 1) for line in (root / "toolchain.env").read_text().splitlines() if "=" in line)
composer = json.loads((root / "backend-api/composer.lock").read_text())
locked_packages = {package["name"]: package["version"].lstrip("v") for package in composer["packages"]}
for package, pin in (("laravel/framework", "LARAVEL_VERSION"), ("laravel/octane", "OCTANE_VERSION")):
    if locked_packages.get(package) != pins[pin]:
        raise SystemExit(f"toolchain drift: {package} lock version")

def require(path, pattern, expected):
    content = (root / path).read_text()
    found = re.search(pattern, content, re.MULTILINE)
    if not found or found.group(1) != expected:
        raise SystemExit(f"toolchain drift: {path}: expected {expected}, found {found.group(1) if found else 'missing'}")

require("backend-tdlib/cmake/dependencies.lock.cmake", r'set\(TELEBEZEL_TDLIB_COMMIT "([a-f0-9]{40})"\)', pins["TDLIB_COMMIT"])
require("backend-tdlib/CMakeLists.txt", r'set\(CMAKE_CXX_STANDARD ([0-9]+)\)', pins["CXX_STANDARD"])
require("backend-tdlib/cmake/Dependencies.cmake", r'set\(CMAKE_CXX_STANDARD ([0-9]+)\)\s*FetchContent_MakeAvailable\(tdlib\)', pins["TDLIB_CXX_STANDARD"])
require("backend-api/Dockerfile", r'^FROM composer:([0-9.]+)@sha256:', pins["COMPOSER_VERSION"])
require("backend-api/Dockerfile", r'^FROM composer:[0-9.]+@(sha256:[a-f0-9]{64})', pins["COMPOSER_IMAGE_DIGEST"])
require("backend-api/Dockerfile", r'^FROM dunglas/frankenphp:([0-9.]+)-php8\.5-bookworm@sha256:', pins["FRANKENPHP_VERSION"])
require("backend-api/Dockerfile", r'^FROM dunglas/frankenphp:([0-9.]+)-builder-php8\.5-bookworm@sha256:', pins["FRANKENPHP_VERSION"])
require("backend-api/Dockerfile", r'^FROM dunglas/frankenphp:[0-9.]+-builder-php8\.5-bookworm@(sha256:[a-f0-9]{64})', pins["FRANKENPHP_BUILDER_IMAGE_DIGEST"])
for module, pin in (("github.com/getkin/kin-openapi", "FRANKENPHP_KIN_OPENAPI_VERSION"),
                    ("golang.org/x/crypto", "FRANKENPHP_X_CRYPTO_VERSION"),
                    ("google.golang.org/grpc", "FRANKENPHP_GRPC_VERSION")):
    require("backend-api/Dockerfile", rf'^RUN go get .*{re.escape(module)}@v([0-9.]+)', pins[pin])
frankenphp_digests = re.findall(r'^FROM dunglas/frankenphp:[0-9.]+-php8\.5-bookworm@(sha256:[a-f0-9]{64})', (root / "backend-api/Dockerfile").read_text(), re.MULTILINE)
for digest in frankenphp_digests:
    if digest != pins["FRANKENPHP_IMAGE_DIGEST"]:
        raise SystemExit("toolchain drift: FrankenPHP base digest")
if len(frankenphp_digests) != 2 or len(re.findall(r'^FROM dunglas/frankenphp:', (root / "backend-api/Dockerfile").read_text(), re.MULTILINE)) != 3:
    raise SystemExit("toolchain drift: unexpected FrankenPHP stages")
debian_digests = re.findall(r'^FROM debian:[^@\s]+@(sha256:[a-f0-9]{64})', (root / "backend-tdlib/Dockerfile").read_text(), re.MULTILINE)
for digest in debian_digests:
    if digest != pins["DEBIAN_IMAGE_DIGEST"]:
        raise SystemExit("toolchain drift: Debian base digest")
if len(debian_digests) != 2 or len(re.findall(r'^FROM debian:', (root / "backend-tdlib/Dockerfile").read_text(), re.MULTILINE)) != 2:
    raise SystemExit("toolchain drift: unexpected Debian stages")
require("backend-tdlib/Dockerfile", r'^ARG CMAKE_VERSION=([0-9.]+)$', pins["CMAKE_VERSION"])
require("compose.yaml", r'^    image: postgres:([0-9.]+)-bookworm@sha256:', pins["POSTGRES_VERSION"])
require("compose.yaml", r'^    image: postgres:[0-9.]+-bookworm@(sha256:[a-f0-9]{64})', pins["POSTGRES_IMAGE_DIGEST"])
require("tests/api/check.sh", r'^image="postgres:([0-9.]+)-bookworm@sha256:', pins["POSTGRES_VERSION"])
require("tests/api/check.sh", r'^image="postgres:[0-9.]+-bookworm@(sha256:[a-f0-9]{64})', pins["POSTGRES_IMAGE_DIGEST"])
require(".github/workflows/ci.yml", r'^        image: postgres:[0-9.]+-bookworm@(sha256:[a-f0-9]{64})', pins["POSTGRES_IMAGE_DIGEST"])
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
