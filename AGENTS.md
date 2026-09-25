# AGENTS.md

## Style

- `snake_case` everywhere; class private members and instances prefixed `_`; read `.editorconfig`

## Network

- packed structs (`__attribute__((packed))`); first field is always `type`
- 250 bytes/msg max; frequent state must be binary, not JSON
- host is authoritative (runs game logic, sends state); client sends inputs
- fixed custom MACs; role picks the peer
- role comes from build: `pio run -e host` | `-e client` (flag `DEVICE_ROLE`); `include/config.h` only validates it

## Layout

- `include/` — `pins.h` (pins), `tft_setup.h` (TFT_eSPI user setup), `config.h` (role validation)
- `lib/network` — static `network`: raw ESP-NOW transport, no message logic
- `lib/protocol` — header-only wire format: `msg_type` + packed structs
- `lib/handler` — `handler`: typed routing/dispatch over `network`
- `lib/display` — static `display` + `colour`; the only place TFT_eSPI is used
- `lib/input` — static `input`: joystick (ADC1) + buttons (debounce + edge)
- `lib/game` — static `game`: state machine (menu/mode/scores/playing/pause/game_over) + local sim (zombies chase, visible bullets, waves, score); P3 net sync next
- `src/main.cpp` — bootstrap: `game::begin(DEVICE_ROLE)` + `game::update()`

Libraries resolve via LDF `chain` (follow `#include`). TFT config is applied repo-wide from `[env]` build flags: `-D USER_SETUP_LOADED` + `-include tft_setup.h` (pre-includes `pins.h` into every TU incl. TFT_eSPI sources). Filenames are case-sensitive on Linux

## Display (TFT)

- glass is a 240x320 ST7789 IPS; `include/tft_setup.h` sets `ST7789_DRIVER`, `TFT_INVERSION_OFF` (without it 0x0000 renders as white, everything washed), `LOAD_GLCD`, 40 MHz SPI
- `display::begin()` uses rotation 1 = landscape 320x240: HUD strip (10px) on top, arena below
- game paints `_grass` once in `begin`; entities erase-then-draw (old pos repainted with grass)
- if a screen region keeps stale/ghost content across fills and reboots → driver/window mismatch, not a dead panel

## Add a message

- add a `msg_type` value in `lib/protocol/src/Protocol.h`
- add its packed struct (with `type` first) in the same header
- register `on_message(type, callback)` in `lib/game/src/Game.cpp`
- send it with `_handler.send(&msg, sizeof(msg))`

## microSD (future)

- persist per-player scores with ArduinoJson; hardware not integrated yet
