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
- `lib/game` — static `game`: state machine (menu/mode/scores/playing/pause/game_over) + local sim (zombies chase, visible bullets, waves, score) in world px with wall collision; P3 net sync next
- `src/main.cpp` — bootstrap: `game::begin(DEVICE_ROLE)` + `game::update()`
- `test/native` — host-side checks for the tilemap and the world/camera invariants; `./test/native/run.sh` builds `map_test.cpp` with plain `g++` (no Arduino, no hardware) and returns non-zero on failure

Libraries resolve via LDF `chain` (follow `#include`). TFT config is applied repo-wide from `[env]` build flags: `-D USER_SETUP_LOADED` + `-include tft_setup.h` (pre-includes `pins.h` into every TU incl. TFT_eSPI sources). Filenames are case-sensitive on Linux

## Display (TFT)

- glass is a 240x320 ST7789 IPS; `include/tft_setup.h` sets `ST7789_DRIVER`, `TFT_INVERSION_OFF` (without it 0x0000 renders as white, everything washed), `LOAD_GLCD`, 40 MHz SPI
- world is 960x480 (60x30 tiles x 16px), parsed in `tilemap::init`; every entity lives in **world px**; screen bands are the only screen-space things: HUD strip 10px, arena 160px, panel 70px (10+160+70 = 240 exactly)
- screen→world: `sx = wx - _cam_x`, `sy = wy - _cam_y + _hud_h`; arena is 320x160, camera clamped to x[0,640] y[0,320] → an exact **3x3 grid of cells**, x{0,320,640} y{0,160,320}
- the map is a 2-tile ring road plus cross streets on the cell borders, so a hard cut always lands on a street and there is always a kiting loop; 9 districts, 68% walkable, no sealed pockets (flood-filled in `test/native/map_test.cpp`)
- camera is **hard-cut by cell**: `_cell_cam(player_centre, arena, max)` → the cell the player is in, clamped; any change schedules `_paint_view`, which repaints **80 arena rows per frame** (`_paint_step`, 51 KB ≈ 10.4 ms instead of 102 KB in one 20.9 ms frame) — one frame shows a half-painted cell, the repaint runs before sprites are drawn so it can never paint over a live one
- frame pacing is a target deadline (`_frame_ms` 33) in `update()`, not `delay(33)`: a heavy frame pushes the next one out and never tries to catch up
- erase is per-pixel-equivalent: `_erase_world_rect` walks every world px with `tilemap::color_at` and emits one `fill_rect` per same-colour run (verified identical to per-pixel); order is `_render_clear` → `_sim` → `_update_camera` → `_render_draw`, so the clear-before-sim is what erases entities that die mid-frame
- hard cut hides up to half a sprite while crossing a cell edge; `_fill_world_box`, `_fill_world_run` and `_erase_world_rect` all clip to the arena `[10,170)`, so nothing bleeds into the HUD nor the panel
- bottom panel: `_panel_init` paints it once per run (black + 388-run minimap terrain + 3x3 cell separators, ~17 ms one-off), then per frame only `_draw_panel` (text + HP pips) and `_minimap_blips` run; the minimap is 2px/tile (120x60) and dots are erased by repainting `tilemap::color_at` of the tile underneath, tracked in `_mm_px/_mm_py`
- the minimap also outlines the camera cell (`_mm_frame`, 1px `colour::yellow`, 40x20 px at `_mm_ctx/_mm_cty`): the 4 edges of last frame's cell are restored first via `_mm_restore_row/_mm_restore_col` (run-length along their own axis, so the vertical edges collapse to ~2 calls each), then the new outline, then the dots on top. `_panel_init` clears `_mm_ctx`/`_mm_n` because the base repaint wipes both
- zombie spawns scan tiles from a random offset and take the first walkable one >= 100px away, so it never depends on the world border being walkable
- `tilemap::solid_rect` gates movement per axis (X then Y); bullets die on any non-FLOOR tile
- zombie AI is direct chase with per-axis collision and no pathfinding, so the alley dead ends and the dock warehouse can trap them; the ring road keeps an escape route for the player
- if a screen region keeps stale/ghost content across fills and reboots → driver/window mismatch, not a dead panel

## Next (M3)

- **(P3) P2P net sync** — marshal game state over ESP-NOW (bullets/zombies/player), roles host/client from build; reproducible hand-roll steps in `lib/protocol`. The client currently runs the same local sim from its own spawn, which is fine for one peer but diverges as soon as shots land.
- (P4/future) microSD scores persistence — not integrated yet; ArduinoJson only after hardware.
- zombie pathfinding (BFS over the 60x30 grid) if the dead ends prove too sticky.
- `_art` rows must stay exactly `COLS` chars: a longer row shifts the districts and eats the ring road (the ring check in `test/native/map_test.cpp` catches it).

## Add a message

- add a `msg_type` value in `lib/protocol/src/Protocol.h`
- add its packed struct (with `type` first) in the same header
- register `on_message(type, callback)` in `lib/game/src/Game.cpp`
- send it with `_handler.send(&msg, sizeof(msg))`

## microSD (future)

- persist per-player scores with ArduinoJson; hardware not integrated yet
