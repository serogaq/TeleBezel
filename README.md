# TeleBezel

TeleBezel is a modern Pebble Telegram client. This repository currently contains
the Stage 0 foundation and connectivity proof, not Telegram features. Remote
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
docker compose up -d backend-api
make api-client-issue NAME=my-pebble
make integration-test
```

Configure the returned token in the Pebble configuration page. In a local phone
emulator, use a host address it can actually reach; `127.0.0.1` on a physical
phone means the phone itself, not this Docker host. For physical devices, place
a trusted HTTPS reverse proxy in front of the loopback-bound API and enter its
reachable hostname and port. Never expose the private TDLib or PostgreSQL ports.

The persistence boundary is strict: PostgreSQL stores application-owned API
state, while the TDLib volume stores Telegram-owned session and cache data.

For a standalone watch build, run `make app-build`; it writes and validates
`app/build/app.pbw`. `make app-check` also runs the C and PebbleKit JS checks.
Use `make app-qemu PLATFORM=emery` (or `PLATFORM=gabbro`) to install that PBW
in an emulator. Use `make app-install IP=phone-ip` for a local phone developer
connection, or `make app-install` for an already authenticated CloudPebble
connection. Both installation targets use the existing PBW; `app-install`
streams logs until interrupted.

Architecture, protocols, persistence, configuration, deployment, development,
release policy, and the scoped roadmap are documented under [`docs/`](docs/).
