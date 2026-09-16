# AGENTS.md

- `snake_case` everywhere; class private members and instances prefixed `_`
- read .editorconfig
- network structs: `__attribute__((packed))`, first field is `type` always
- authoritative host/client. Host runs game logic and sends state; client sends inputs
- role is a static (network), chosen per environment: `pio run -e host` or `pio run -e client` (`DEVICE_ROLE` build flag); `include/config.h` only validates it
- fixed custom MACs; role picks peer
- ESP-NOW limit: 250 bytes/msg. For frequent state use binary, not JSON

## Layout

- `include/config.h` — role validation only
- `include/pins.h` — hardware pins (TFT SPI, joystick ADC1, buttons)
- `include/tft_setup.h` — TFT_eSPI user setup (auto-loaded via `__has_include(<tft_setup.h>)`; defines pin macros from `pins.h`)
- `lib/network/src/Network.{h,cpp}` — static class `network`: raw ESP-NOW transport, no message knowledge
- `lib/protocol/src/Protocol.h` — wire format, header-only: `msg_type`, packed structs
- `lib/handler/src/Handler.{h,cpp}` — class `handler`: typed routing/dispatch on top of `network`
- `lib/display/src/Display.{h,cpp}` — static class `display` + namespace `colour`; TFT_eSPI visible only here
- `lib/input/src/Input.{h,cpp}` — static class `input`: joystick (ADC1) + buttons with debounce and edge detection
- `lib/game/src/Game.{h,cpp}` — static class `game`: owns pieces, drives the loop
- `src/main.cpp` — bootstrap only: `game::begin(DEVICE_ROLE)` + `game::update()`
- project libs see each other via LDF `chain` (follows `#include`); no extra build flags. Filenames are case-sensitive on Linux (`Protocol.h`, `Network.h`)
- microSD (future): scores persisted per player via ArduinoJson; hardware not yet integrated

## To add a new message

- add `msg_type` value in `lib/protocol/src/Protocol.h`
- define packed struct with `type` first in the same header
- register `game::_net.on_message(type, cb)` in `lib/game/src/Game.cpp` for now
- send with `_net.send(&msg, sizeof(msg))`