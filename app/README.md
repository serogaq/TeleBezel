# TeleBezel Pebble app

Stage 0 contains only the bilingual connectivity screen and the PebbleKit JS
HTTP bridge. It does not contain Telegram UI or account authorization.

Run `npm ci`, `npm test`, and `pebble build` from this directory.

## Building and running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

## Target platforms

Stage 0 targets `diorite`, `emery`, `flint`, and `gabbro`: the current modern
family supported by the pinned rePebble SDK. PBW validation requires all four
platform payloads and the project UUID.

## Project layout

```
src/c/           C source for the watchapp
src/pkjs/        Modular PebbleKit JS HTTP bridge
localization/    Canonical English and Russian source strings
protocol/        Canonical AppMessage contract
scripts/         Deterministic source generation
tests/           Host-side C and JavaScript tests
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Pebble build rules
```

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
