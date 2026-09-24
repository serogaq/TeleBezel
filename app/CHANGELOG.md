# Changelog

All notable changes to the TeleBezel Pebble app will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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

- Long-lived state lives on the heap and localized strings are loaded from resources, keeping the static image under a 58 KiB budget checked in CI.
