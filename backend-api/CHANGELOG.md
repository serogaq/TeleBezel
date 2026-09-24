# Changelog

All notable changes to the TeleBezel API will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Public HTTP API with health, readiness and status endpoints.
- Access tokens of two types, `device` and `maintenance`, with separate permissions and optional account claims; the settings page uses a short-lived maintenance token in an HttpOnly cookie with a token-bound CSRF header.
- Durable Telegram account lifecycle: creation, phone, email, code, password and QR authorization, proxy configuration, logout and local removal.
- Read API for accounts, chat lists, chat history, single messages, message previews, interest leases and update journals, with signed cursors.
- Device preferences for language, default account and chat list.
- Encrypted configuration storage and a scheduler for account reconciliation.
- Owner settings page.
- Message sends with replies: idempotent per `Idempotency-Key`, `202` with an operation that becomes `pending`, `sent`, `failed` or `unknown`, a status endpoint, and `send_changed` events in `/updates` for the sending token only.
- `GET /v1/quick-replies` with a revision, and `types` filtering for `/updates`.
- `can_send`, `reply_to`, `sending_state` and `forward_from` in chat and message projections.
