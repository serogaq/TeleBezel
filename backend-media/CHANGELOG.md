# Changelog

All notable changes to the TeleBezel media service will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- A private, stateless service that turns a Telegram JPEG into a TBI1 image for
  emery (200×228) and gabbro (260×260, round) within the memory budget the
  watch states: fitted and centred, 16 or 4 of the watch's 64 colours, damped
  Floyd–Steinberg for photos, sharp palettes for screenshots and diagrams.
- One bearer-authenticated route with strict parameters, a 5 MB body limit,
  size checks before decoding, a render deadline and bounded concurrency.
- No volumes, no network egress, no Telegram or database access; the image is
  `scratch` with a single static binary running as UID 10003.
