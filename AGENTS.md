# TeleBezel engineering rules

- `app`, `backend-api`, and `backend-tdlib` are independently versioned.
- PostgreSQL is owned exclusively by `backend-api` and contains application state only.
- The TDLib volume is owned exclusively by `backend-tdlib` and contains Telegram sessions, databases, files, and caches.
- Never mount TDLib storage into `backend-api`; never give PostgreSQL credentials to `backend-tdlib`.
- All API-to-TDLib communication uses the private authenticated HTTP contract.
- Never expose arbitrary or raw TDLib commands through either public API.
- One TDLib process will eventually own multiple client IDs; do not design one process/container per account.
- The product must remain correct with bounded HTTP request/response and future cursor polling. WebSockets are optional and non-foundational.
- Never log API tokens, internal bearer tokens, Telegram credentials, or proxy credentials.
- Octane workers are long-lived. Request/authentication state must not live in globals, statics, or mutable singletons.
- Migrations are explicit deployment steps. Container startup must never run destructive or automatic migrations.
- GitHub Actions must be pinned to immutable full commit SHAs and pass actionlint/zizmor.
