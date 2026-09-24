# TeleBezel Pebble app

A Telegram client for the watch: choose an account, open the main or archive
chat list, read a conversation, load older messages, open a message in full and
reply with Pebble Dictation or a quick reply. Media and near-realtime delivery
are later stages.

Run `npm ci`, `npm test`, and `pebble build` from this directory.

## Building and running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

From the repository root, `make app-emulator-check PLATFORM=emery` (or
`gabbro`) builds the app with `TB_DIAG=1`, walks the read path and the send path
(quick reply, reply, a late result, dictation cancelled, a refused send, pull in
history) in QEMU against `tests/e2e/mock_api.js` and saves screenshots under
`build/emulator/<platform>/`. It then writes `diag-summary.json` and fails on a
stack overflow or app fault, on free heap below the floor, or on stack depth
above the baseline in `tests/emulator/diag-baseline.json`. Run `make app-build`
afterwards before installing on a watch.

`make app-screens` takes screenshots for review without checks: it builds the
release PBW, runs every chosen case on a freshly started emulator with a clean
phone storage against the mock API and writes each screen plus `sheet.png`
(all screens joined) to `build/screens/<platform>/`, with logs in `logs/`.

```sh
make app-screens                                        # emery, every case
make app-screens PLATFORM="emery,gabbro"                # both watches
make app-screens PLATFORM=gabbro SCREENS="compose,reply" # cases
make app-screens SCREENS="review,card-sent"             # single screens
make app-screens SCREENS=list                           # cases and screens
```

`PLATFORM` takes `emery` and `gabbro`, comma-separated; `SCREENS` takes case or
screen names. A single screen runs its case only as far as that screen. Both
watches run in parallel, each with its own mock port (`8787`, `8788`); steps
wait until the mock is idle and the screen stops changing instead of sleeping,
so a full run of both watches takes about three minutes.

`make open-clay PLATFORM=emery` opens the phone settings page (Clay) of the app
running in the emulator in your browser, as the Pebble app would on the phone.
Start the emulator first with `make app-qemu` in another terminal; without it the
command stops with that hint. Save sends the settings to the emulator (the watch
reloads as on the phone), shows an alert and keeps the page open with the saved
values, so you can keep changing and saving. Ctrl+C closes the settings like
the phone's back arrow and stops the local server (`CLAY_PORT`, default 8733).

## Watch navigation

`Connect → Accounts → Chats → History → Reader`. Back always returns to the
previous screen with its data and selection kept. With one ready account, or a
ready default account, the app opens its chats directly.

- Accounts: Select opens a ready account or explains what is needed; a long
  Select makes the account the default. The default account can also be chosen
  on the phone in the app settings.
- Chats: a top bar that scrolls with the list shows when the list was updated,
  the Telegram connection state with the current time, and the unread counter
  (chats or messages with sound on, chosen on the phone). The next row switches between
  the main list and the archive (hidden when the archive is turned off on the
  phone); the last row loads more chats. To refresh, pull the list down from
  the top on a touch watch, or press Up twice quickly at the top of the list:
  one press fills the circle under the top bar halfway and lets it fade, two
  presses fill it and refresh. A long Select also refreshes. While Telegram is
  connecting or updating the watch polls the status for a short while, see
  `../docs/protocol.md`.
- History: oldest at the top, newest at the bottom. The top row loads earlier
  messages and states when the start of the history was reached; below the
  messages is the Write row. To refresh, pull up past the bottom on a touch
  watch or press Down twice quickly on the last row. A status row appears
  above Write only while there is something to report: updating, an update
  error, Telegram connecting, or newer messages that need a reload. Select on a message opens
  the reader; a long Select opens its menu: Reply, Open, and Retry sending for
  the last message this watch could not send. Tapping a message opens it, a
  long tap opens its menu. Pending and failed own messages carry a clock or
  alert mark. A reply starts with one line quoting the original (author and
  text, as much as fits); a forwarded message shows a forward arrow with its
  original author. In Saved Messages names are shown as in groups: the
  original author for forwarded messages and "You" for your own notes.
- Reader: the sender of an incoming message is always named, a forwarded
  message says whom it is forwarded from, and a reply shows the original
  message in full above its own text. Select opens the actions menu (Reply,
  Write to chat, Retry sending); a long Select retries loading the full text
  after an error.
- Write and Reply: the first screen offers Dictate and the quick replies from
  `/settings` (marked when they may be outdated). The chosen text opens a review
  page with the target chat and reply, the length and the hints: Select sends,
  a long Select offers Dictate again, Another reply and Cancel. Texts over the
  Telegram limit cannot be sent. The result page shows Sent, Not sent (Send
  again) or Result unknown (Check, then Send anyway after a duplicate warning);
  Sent closes by itself. Leaving while sending keeps the send going in the
  background.
- Notifications: connection problems, a list that could not be updated and the
  progress of a send that left the compose screens appear as one card at the
  top of the chats and history lists, most important first. Up at the first
  row focuses it; Select refreshes, opens the chat or checks the send, depending
  on the card.
- Touch (emery, gabbro): the app opts into the system touch bridge, so lists and
  the reader scroll by swiping and rows open by tapping. Every action is also a
  visible row, so nothing depends on a long press.

## Target platforms

`emery` and `gabbro`. Diorite and flint are not supported: their 64 KB of app
memory cannot hold the reply features together with a usable heap. The page
sizes and text budgets live at the top of `src/c/main.c`.

## Memory rules

The loader stores the static footprint (`.text + .data + .bss`) in a 16-bit
field, so it can never exceed 64 KiB on any platform, even though emery and
gabbro have 128 KB of app RAM. The heap is plentiful, the static image is not.

- Long-lived state lives on the heap. `main.c` allocates one `TbApp` with every
  controller and window; controllers allocate their own buffers at init. Static
  variables hold only pointers, small flags and constant tables.
- Buffers needed only while drawing or composing text are borrowed from
  `scratch.c` or allocated for the draw call (the round chats bar) and are never
  kept between frames. Window texts (compose) are allocated with the first
  window and freed with the last one.
- Localized watch strings are raw resources (`resources/generated/strings_*.bin`,
  generated by `scripts/generate.js`) loaded into one heap block at start.
- Avoid code that pulls in large library routines: no `qsort`, no 64-bit
  division or `printf` of 64-bit values; prefer shared helpers (`tb_theme_page`
  lays out the reader, review and result pages; one timer slot helper drives all
  controller timers in `main.c`).
- Budget: `.github/scripts/validate_pbw.py` reads `virtual_size` from each
  `pebble-app.bin` and fails above 58 KiB. CI checks both the release and the
  `TB_DIAG=1` build. Stage 4 uses about 48.0 KB (emery) and 50.0 KB (gabbro),
  leaving room for media and near-realtime work.
- The build uses LTO. The linker keeps `__pbl_app_info` explicitly (see
  `wscript`), otherwise LTO drops the app header and the PBW cannot be
  installed; `validate_pbw.py` checks the header.

## Diagnostics

`TB_DIAG=1 pebble build` adds `TBDIAG key=value` log lines (event, window,
free and minimum heap, stack depth, AppMessage sizes, queued requests, timers)
used by the emulator check. Each event is split over short lines because
`APP_LOG` truncates long messages. The release build contains no diagnostics.

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

Protocol: `../docs/protocol.md`.
