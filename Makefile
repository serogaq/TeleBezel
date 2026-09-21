SHELL := /bin/bash
.DEFAULT_GOAL := help
include toolchain.env
COMPOSER_VERSION := $(shell python3 .github/scripts/current_dependencies.py composer-version)

PHP_BIN ?= $(shell if [ -x /opt/homebrew/opt/php/bin/php ]; then echo /opt/homebrew/opt/php/bin/php; else command -v php; fi)
COMPOSER_BIN ?= $(shell command -v composer)
NODE_BIN ?= $(shell if command -v mise >/dev/null 2>&1 && mise where node@$(NODE_VERSION) >/dev/null 2>&1; then printf '%s/bin' "$$(mise where node@$(NODE_VERSION))"; elif [ -x /opt/homebrew/opt/node@24/bin/node ]; then echo /opt/homebrew/opt/node@24/bin; else dirname "$$(command -v node)"; fi)
CMAKE_BIN ?= $(shell if command -v uvx >/dev/null 2>&1; then echo 'uvx --from cmake==$(CMAKE_VERSION) cmake'; else echo cmake; fi)
CLANG_FORMAT_BIN ?= $(shell if command -v uvx >/dev/null 2>&1; then echo 'uvx --from clang-format==$(CLANG_FORMAT_VERSION) clang-format'; else echo clang-format; fi)
PEBBLE ?= pebble
PLATFORM ?= emery
PBW ?= app/build/app.pbw
IP ?=
QEMU_FLAGS ?=
LOG_FLAGS ?=
GHCR_OWNER ?= serogaq
TDLIB_BUILD_DIR ?= backend-tdlib/build
TDLIB_CMAKE_ARGS ?=
CMAKE_GENERATOR_ARGS ?= $(if $(wildcard $(TDLIB_BUILD_DIR)/CMakeCache.txt),,-G Ninja)

.PHONY: help toolchain-check app-build app-check app-qemu app-install api-check tdlib-check workflow-audit compose-config compose-build secrets-init secrets-provision preflight bootstrap-code db-migrate api-client-issue integration-test image-smoke check clean
help:
	@awk 'BEGIN {FS = ":.*## "} /^[a-zA-Z0-9_-]+:.*## / {printf "%-22s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

toolchain-check: ## Verify the locally selected toolchain
	@python3 .github/scripts/check_toolchain_drift.py
	@$(PHP_BIN) -r 'exit(PHP_VERSION === "$(PHP_VERSION)" ? 0 : 1);'
	@$(PHP_BIN) $(COMPOSER_BIN) --version | grep -E '^Composer version $(COMPOSER_VERSION)( |$$)'
	@PATH="$(NODE_BIN):$$PATH" node -e 'if (process.versions.node !== "$(NODE_VERSION)") process.exit(1)'
	@PATH="$(NODE_BIN):$$PATH" npm --version | grep -Fx '$(NPM_VERSION)'
	@pebble --version | grep -F '$(PEBBLE_TOOL_VERSION)'
	@pebble sdk list | grep -F '$(PEBBLE_SDK_VERSION) (active)'
	@$(CMAKE_BIN) --version | head -1 | grep -F '$(CMAKE_VERSION)'
	@$(CLANG_FORMAT_BIN) --version | grep -F '$(CLANG_FORMAT_VERSION)'

app-build: ## Build and validate app/build/app.pbw
	cd app && PATH="$(NODE_BIN):$$PATH" npm ci
	cd app && PATH="$(NODE_BIN):$$PATH" pebble build
	python3 .github/scripts/validate_pbw.py app/build/app.pbw

app-check: app-build ## Build, lint, and test the Pebble app
	cd app && PATH="$(NODE_BIN):$$PATH" npm test

app-qemu: ## Install an existing PBW in QEMU: PLATFORM=emery QEMU_FLAGS=--vnc
	@command -v "$(PEBBLE)" >/dev/null 2>&1 || { echo 'Pebble CLI is required' >&2; exit 1; }
	@test -f "$(PBW)" || { printf 'PBW not found: %s\nRun make app-build first or set PBW=/path/to/app.pbw\n' "$(PBW)" >&2; exit 1; }
	"$(PEBBLE)" install "$(PBW)" --emulator "$(PLATFORM)" $(QEMU_FLAGS)

app-install: ## Install an existing PBW on a watch: IP=phone-ip (or CloudPebble)
	@command -v "$(PEBBLE)" >/dev/null 2>&1 || { echo 'Pebble CLI is required' >&2; exit 1; }
	@test -f "$(PBW)" || { printf 'PBW not found: %s\nRun make app-build first or set PBW=/path/to/app.pbw\n' "$(PBW)" >&2; exit 1; }
	@if test -n "$(IP)"; then \
		"$(PEBBLE)" ping --phone "$(IP)" && \
		"$(PEBBLE)" install "$(PBW)" --phone "$(IP)" && \
		"$(PEBBLE)" logs --phone "$(IP)" $(LOG_FLAGS); \
	else \
		"$(PEBBLE)" login --status >/dev/null 2>&1 || { echo 'Log in with pebble login or set IP=phone-ip' >&2; exit 1; }; \
		"$(PEBBLE)" ping --cloudpebble && \
		"$(PEBBLE)" install "$(PBW)" --cloudpebble && \
		"$(PEBBLE)" logs --cloudpebble $(LOG_FLAGS); \
	fi

api-check: ## Validate, format-check, analyze, and test backend-api
	PHP_BIN="$(PHP_BIN)" COMPOSER_BIN="$(COMPOSER_BIN)" bash tests/api/check.sh

tdlib-check: ## Configure, build, and test backend-tdlib
	$(CLANG_FORMAT_BIN) --dry-run --Werror $$(find backend-tdlib/include backend-tdlib/src backend-tdlib/tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -print)
	$(CMAKE_BIN) -S backend-tdlib -B $(TDLIB_BUILD_DIR) $(CMAKE_GENERATOR_ARGS) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTELEBEZEL_BUILD_TESTS=ON $(TDLIB_CMAKE_ARGS)
	$(CMAKE_BIN) --build $(TDLIB_BUILD_DIR) --target telebezel-tdlib telebezel-tdlib-tests telebezel-tdlib-runtime-tests --parallel 2
	$(CMAKE_BIN) --build $(TDLIB_BUILD_DIR) --target test

workflow-audit: ## Run actionlint and zizmor through pinned tools
	@python3 .github/scripts/production_licenses.py --check
	@python3 -m unittest discover -s tests/workflows -p '*_test.py'
	@if command -v actionlint >/dev/null 2>&1; then actionlint; \
	elif command -v go >/dev/null 2>&1; then go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12; \
	elif command -v docker >/dev/null 2>&1; then docker run --rm -v "$(CURDIR):/repo:ro" -w /repo rhysd/actionlint@$(ACTIONLINT_IMAGE_DIGEST); \
	else echo 'actionlint, Go, or Docker is required' >&2; exit 1; fi
	uvx --from zizmor==$(ZIZMOR_VERSION) zizmor --strict-collection .github

compose-config: ## Validate the Compose model
	docker compose config --quiet

compose-build: ## Build both application images
	docker compose build backend-api backend-tdlib

secrets-init: ## Create ignored local development secrets if absent
	@umask 077; test -f secrets/postgres_password || openssl rand -base64 32 > secrets/postgres_password
	@umask 077; test -f secrets/tdlib_internal_token || openssl rand -hex 32 > secrets/tdlib_internal_token
	@umask 077; test -f secrets/tdlib_database_master_key || openssl rand -hex 32 > secrets/tdlib_database_master_key
	@umask 077; test -f secrets/laravel_app_key || { printf 'base64:' > secrets/laravel_app_key; openssl rand -base64 32 | tr -d '\n' >> secrets/laravel_app_key; printf '\n' >> secrets/laravel_app_key; }
	@$(MAKE) --no-print-directory secrets-provision

preflight: ## Validate Docker, Compose, and existing key files without changing them
	sh deploy/preflight.sh
	docker compose run --rm backend-api php artisan telebezel:preflight-keys

bootstrap-code: ## Print a one-time owner bootstrap code
	docker compose run --rm backend-api php artisan telebezel:bootstrap-code

secrets-provision: ## Grant only required container UIDs access to secret files on native Linux
	bash tests/secrets/provision_file.sh secrets/postgres_password 999 10001
	bash tests/secrets/provision_file.sh secrets/laravel_app_key 10001
	bash tests/secrets/provision_file.sh secrets/tdlib_internal_token 10001 10002
	bash tests/secrets/provision_file.sh secrets/tdlib_database_master_key 10002

db-migrate: ## Apply production-safe migrations explicitly
	docker compose run --rm -e PGOPTIONS='-c statement_timeout=30000 -c lock_timeout=5000' backend-api php artisan telebezel:migrate-locked

api-client-issue: ## Issue an API token: make api-client-issue NAME=my-watch
	test -n "$(NAME)"
	docker compose run --rm backend-api php artisan telebezel:api-client-issue "$(NAME)"

integration-test: ## Run the real Compose health path
	bash tests/integration/health_path.sh

image-smoke: ## Smoke an image: make image-smoke COMPONENT=backend-api
	test "$(COMPONENT)" = backend-api -o "$(COMPONENT)" = backend-tdlib
	bash .github/scripts/image_smoke_$(subst -,_,$(COMPONENT)).sh "ghcr.io/$(GHCR_OWNER)/telebezel-$(COMPONENT):local"

check: toolchain-check app-check api-check tdlib-check workflow-audit compose-config ## Run every non-integration check

clean: ## Remove generated build output
	rm -rf app/build backend-tdlib/build
