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
