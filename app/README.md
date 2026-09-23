# TeleBezel Pebble app

A read-only Telegram client for the watch: choose an account, open the main or
archive chat list, read a conversation, load older messages and open a message
in full. Sending, media and near-realtime delivery are later stages.

Run `npm ci`, `npm test`, and `pebble build` from this directory.

## Building and running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

From the repository root, `make app-emulator-check PLATFORM=emery` (after `make app-build`)
walks the read path in QEMU against `tests/e2e/mock_api.js` and saves
screenshots under `build/emulator/<platform>/`.

## Watch navigation

`Connect → Accounts → Chats → History → Reader`. Back always returns to the
previous screen with its data and selection kept. With one ready account, or a
ready default account, the app opens its chats directly.

- Accounts: Select opens a ready account or explains what is needed; a long
  Select makes the account the default. The default account can also be chosen
  on the phone in the app settings.
- Chats: a top bar that scrolls with the list shows when the list was updated,
  the Telegram connection state with the current time, and the unread counter
  (chats or messages with sound on, chosen on the phone). An info row appears
  under it only while loading or after an error; the next row switches between
  the main list and the archive (hidden when the archive is turned off on the
  phone); the last row loads more chats. To refresh, pull the list down from
  the top on a touch watch, or press Up twice quickly at the top of the list:
  one press fills the circle under the top bar halfway and lets it fade, two
  presses fill it and refresh. A long Select also refreshes. While Telegram is
  connecting or updating the watch polls the status for a short while, see
  `../docs/protocol.md`.
- History: oldest at the top, newest at the bottom. The top row loads earlier
  messages and states when the start of the history was reached; the bottom row
  refreshes. Select on a message opens the reader.
- Touch (emery, gabbro): the app opts into the system touch bridge, so lists and
  the reader scroll by swiping and rows open by tapping. Every action is also a
  visible row, so nothing depends on a long press.

## Target platforms

`diorite`, `emery`, `flint` and `gabbro`. The 64 KB platforms (diorite, flint)
use smaller page sizes and text budgets than emery and gabbro; the budgets live
at the top of `src/c/main.c`.

## Project layout

```
src/c/           Watch app: request layer, codec, controllers, windows
src/pkjs/        PebbleKit JS: API client, read service, codec, transport
localization/    Canonical English and Russian source strings
protocol/        Canonical AppMessage contract
scripts/         Deterministic source generation
tests/           Host-side C tests, PKJS tests, end-to-end test and mock API
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Pebble build rules
```

## Documentation

Protocol: `../docs/protocol.md`. Acceptance: `../docs/stage-3-acceptance.md`.
SDK docs: <https://developer.repebble.com>
