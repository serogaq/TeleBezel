# Changelog

All notable changes to the TeleBezel TDLib adapter will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Private authenticated HTTP adapter running many TDLib clients in one process.
- Account registry with immutable storage identity, reconciliation and tombstoned removal.
- Authorization, proxy, logout and removal commands with durable completion.
- Chat and message projections with captions and named content kinds, bounded caches and message previews.
- Signed list, history and update cursors, a replayable update journal and bounded interest leases.
- Per-account load isolation and safe shutdown.
- Message sending with replies keyed by operation ID: at most 256 operations per account for 24 hours, the temporary message replaced by the confirmed one, refusals mapped to safe codes and `send_changed` journal events.
- `forward_from` in message projections: the original author of a forwarded or imported message.
- `connection_changed` journal events, a `can_send` hint derived from chat type, membership and permissions, and `reply_to` with the replied sender and a short text in message projections.
