# Changelog

All notable changes to the TeleBezel Pebble app will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Photos on the watch: a Photo item in the reader, selected at the top of the
  message so Select opens the photo or album at once, a full-screen viewer centred on
  emery and gabbro, albums browsed with Up and Down or vertical swipes, spoilers
  revealed on request, clear states for loading, disappearing, paid, expired and
  unsupported media, and a "Photos" setting (load when a message opens, or only
  on request). Channel posts are signed by their author. A long Select opens the
  reader menu, which now also retries a failed text.
- Images arrive as TBI1 drawn straight into the frame buffer (only the visible
  circle on gabbro), within `free heap − 18 KiB`, with resumable chunks, CRC
  checks and cancellation; image chunks yield to every other answer and push.
- `make app-size` and per-stage size budgets; a table-driven record codec, string
  tables instead of label switches and shared pull-to-refresh helpers keep the
  static image within budget.
- Telegram client for the watch: account selection, main and archive chat lists, paged conversation history and a full-text message reader.
- Replies and new messages with Pebble Dictation or quick replies from the server, with a review page before sending and a clear result: sent, not sent (send again) or unknown (check first, then send anyway).
- A send keeps going after leaving the compose screens; its result appears as a notification card, and an open send survives the app being closed.
- Notification cards for connection problems, lists that could not be updated and sends, at the top of the chat list and the history.
- Forwarded messages show a forward arrow with their original author; in Saved Messages names are shown as in groups.
- Replies show a one-line quote of the original in the history and the original in full in the reader, which also always names the sender of an incoming message.
- Pending, failed and reply marks in the history, a message menu on long Select, an actions menu in the reader and pull at the bottom of the history to refresh.
- Navigation by buttons on every supported watch and by touch on touch-capable watches.
- Clear status and error screens for missing configuration, an unreachable phone or server, revoked device tokens and accounts that need sign-in.
- English and Russian interface.
- PebbleKit JS bridge to the TeleBezel API with phone-local device token storage and a Clay settings page.
- Support for emery and gabbro. Diorite and flint are not supported: 64 KB of app memory cannot hold the reply features.

### Changed

- Icons are 2-bit masks in a resource drawn by one routine instead of fourteen
  drawing functions, which saves about 2.1 KB of the static image.
- Long-lived state lives on the heap and localized strings are loaded from resources, keeping the static image under a 58 KiB budget checked in CI.

### Fixed

- Photos from link previews, including carousels of several photos, and
  purchased paid media open in the viewer like albums; paid posts no longer
  carry a "disappearing" mark.
- A message sent from the watch appears in the history as soon as "Sent" closes,
  without a manual refresh.
- The app no longer closes by itself after a send: the compose windows are kept
  instead of being destroyed while their closing animation was still running.
