# Changelog

All notable changes to the TeleBezel API will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Public HTTP API with health, readiness and status endpoints.
- Owner session, manually issued device tokens and maintenance API clients.
- Durable Telegram account lifecycle: creation, phone, email, code, password and QR authorization, proxy configuration, logout and local removal.
- Read API for accounts, chat lists, chat history, single messages, message previews, interest leases and update journals, with signed cursors.
- Device preferences for language, default account and chat list.
- Encrypted configuration storage and a scheduler for account reconciliation.
- Owner settings page.
