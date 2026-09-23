# TeleBezel

TeleBezel is a modern Pebble Telegram client. This repository contains the
Stage 2 phone-managed, multi-account backend: owner-issued device tokens, durable
configuration and reconciliation, chat/history reads, and a replayable update
journal. Sending messages and a complete watch-side Telegram UI remain outside
this stage. Remote
CI, cross-architecture release, RePebble staging, and physical-watch acceptance
remain deployment gates rather than claims established by local tests.

```text
Pebble C app ⇄ PebbleKit JS ⇄ backend-api ⇄ backend-tdlib ⇄ TDLib
                                  │
                              PostgreSQL
```

## Quick start

Local toolchain versions are pinned in `toolchain.env`; application dependencies
and container images are pinned in their lockfiles, Dockerfiles, and Compose.
CI tests the versions in those manifests, including Dependabot updates. Start
Docker Desktop, then run:

```sh
make secrets-init
docker compose up -d postgres backend-tdlib
make db-migrate
docker compose up -d backend-api scheduler
docker compose run --rm backend-api php artisan telebezel:bootstrap-code
make integration-test
```

Open HTTPS `/settings` in the phone browser, use the one-time bootstrap code,
and create a device token under Connected Pebbles. Enter the reachable backend
address and that token in the Pebble Clay configuration page. In a local phone
emulator, use a host address it can actually reach; `127.0.0.1` on a physical
phone means the phone itself, not this Docker host. For physical devices, place
a trusted HTTPS reverse proxy in front of the loopback-bound API and enter its
reachable hostname and port. Never expose the private TDLib or PostgreSQL ports.

The persistence boundary is strict: PostgreSQL stores application-owned API
state, while the TDLib volume stores Telegram-owned session and cache data.
The API exposes account lifecycle plus chats, history, individual messages,
interest leases, and update polling under `/v1/telegram/accounts`. The dedicated
`scheduler` service runs reconciliation; migrations remain an explicit one-shot
deployment step.

For a standalone watch build, run `make app-build`; it writes and validates
`app/build/app.pbw`. `make app-check` also runs the C and PebbleKit JS checks.
Use `make app-qemu PLATFORM=emery` (or `PLATFORM=gabbro`) to run that PBW in an
emulator against the mock API (`app/tests/e2e/mock_api.js`); add
`BACKEND=docker TOKEN=tb_...` to use the Docker backend instead. With the LAN
HTTPS stack the command reads `TELEBEZEL_LAN_ADDRESS` from `.env` and relays the
emulator through a local proxy that trusts the Caddy root certificate, because
the emulator's JavaScript runtime only trusts public CAs. The token is saved in
`app/build/emulator/docker-token` (mode 600) for every platform and may be
omitted on later runs; mock runs do not touch it. The command keeps running until
the emulator is closed or Ctrl+C is pressed, and then stops the mock or proxy.
`LANG_PACK=ru` installs Rebble's official `ru_RU` language pack (downloaded once
into `app/build/lang/`), so Cyrillic text renders in the emulator; any other
value is taken as a path to a `.pbl` file. The choice is remembered for later
runs; `LANG_PACK=none` turns it off. Use `make app-install IP=phone-ip` for a local phone developer
connection, or `make app-install` for an already authenticated CloudPebble
connection. Both installation targets use the existing PBW; `app-install`
streams logs until interrupted.

Architecture, protocols, persistence, configuration, deployment, development,
release policy, and the scoped roadmap are documented under [`docs/`](docs/).
