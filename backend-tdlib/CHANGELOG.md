# Changelog

All notable changes to the TeleBezel TDLib adapter will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `GET /internal/v1/accounts/{uuid}/chats/{chat}/messages/{message}/media`:
  album lookup, spoiler and restriction states, the smallest photo size that
  covers the watch, asynchronous downloads with backoff and abandonment, and
  the source bytes on request. File identities never leave the adapter.
- `content.media` (type, size, spoiler, album, restriction, item count) and
  `is_channel_post` in message projections.
- Items inside one message: link preview pictures and carousels
  (`linkPreviewTypeAlbum`, photo, article, video) and purchased paid media are
  served by `index` like album items; paid items not bought stay restricted.
- Private authenticated HTTP adapter running many TDLib clients in one process.
- Account registry with immutable storage identity, reconciliation and tombstoned removal.
- Authorization, proxy, logout and removal commands with durable completion.
- Chat and message projections with captions and named content kinds, and bounded caches.
- Signed list, history and update cursors, a replayable update journal and bounded interest leases.
- Per-account load isolation and safe shutdown.
- Message sending with replies keyed by operation ID: at most 256 operations per account for 24 hours, the temporary message replaced by the confirmed one, refusals mapped to safe codes and `send_changed` journal events.
- `forward_from` in message projections: the original author of a forwarded or imported message.
- `connection_changed` journal events, a `can_send` hint derived from chat type, membership and permissions, and `reply_to` with the replied sender and a short text in message projections.

### Changed

- The image is based on Debian 13.7, which carries the fixed pcre2 and OpenSSL
  packages flagged by the image scan.
