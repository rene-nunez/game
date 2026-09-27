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
- `lib/world` — static `tilemap`: the 60x30 tile map, its `_art` rows, wall queries, spawn point and the BFS `field` that `build_field()` floods. It was extracted out of `lib/game` so the sim and the renderer can both depend on it without depending on the game
- `lib/sim` — static `sim`: player, zombies, bullets, waves, score, the zombie AI and the firing, in world px. It owns its state and never touches the screen, the menus or the network
- `lib/render` — static `render`: the camera, the incremental terrain repaint and the arena sprites. It reads `sim::view()` and publishes only `cam_x()/cam_y()`; it knows nothing about the panel, the menus or the net
- `lib/panel` — static `panel`: the bottom strip — status text, HP pips and the minimap with the camera-cell frame. It reads `render` for the arena origin and the camera, and `sim::view()` for the blips
- `lib/scores` — static `scores`: the RTC-backed best score and total kills. `load()` at boot, `add_run(kills, score)` on game over, `best()`/`total_kills()` for the screens. It exists as a library so P4 can swap the RTC for microSD behind the same four functions
- `lib/game` — static `game`: the state machine (menu/mode/scores/playing/pause/game_over), the menus and the **frame order**. It is the only place that calls sim + render + panel together; P3 net sync next
- `src/main.cpp` — bootstrap: `game::begin(DEVICE_ROLE)` + `game::update()`
- `test/test_native` — host-side checks for the tilemap and the world/camera invariants; `./test/test_native/run.sh` builds `map_test.cpp` with plain `g++` (no Arduino, no hardware) and returns non-zero on failure. The `test_` prefix is PlatformIO's own suite naming: its `list_test_names` skips any subdir that is not `test_*` and then falls back to treating the **whole** `test/` dir as one suite, which would try to compile the bank for the ESP32. `test_ignore = test_native` in the firmware envs makes `pio test` skip it on purpose — `run.sh` is the entry point, no Unity framework needed

Libraries resolve via LDF `chain` (follow `#include`). Every `lib/*/src/*.cpp` is compiled regardless of what includes it — `map.cpp` was never `#include`d by anything yet always landed in the binary, which is what lets a class be split across libraries (see `lib/world`) by just moving files. Cross-library includes use `<>` (`#include <map.h>`), same-directory ones use `""`. TFT config is applied repo-wide from `[env]` build flags: `-D USER_SETUP_LOADED` + `-include tft_setup.h` (pre-includes `pins.h` into every TU incl. TFT_eSPI sources). Filenames are case-sensitive on Linux

## Display (TFT)

- glass is a 240x320 ST7789 IPS; `include/tft_setup.h` sets `ST7789_DRIVER`, `TFT_INVERSION_OFF` (without it 0x0000 renders as white, everything washed), `LOAD_GLCD`, 40 MHz SPI
- world is 960x480 (60x30 tiles x 16px), parsed in `tilemap::init`; every entity lives in **world px**; screen bands are the only screen-space things: HUD strip 10px, arena 160px, panel 70px (10+160+70 = 240 exactly)
- screen→world: `sx = wx - render::_cam_x`, `sy = wy - render::_cam_y + render::HUD_H`; arena is 320x160, camera clamped to x[0,640] y[0,320] → an exact **3x3 grid of cells**, x{0,320,640} y{0,160,320}
- the map is a 2-tile ring road plus cross streets on the cell borders, so a hard cut always lands on a street and there is always a kiting loop; 9 districts, 68% walkable, no sealed pockets (flood-filled in `test/test_native/map_test.cpp`)
- camera is **hard-cut by cell**: `render::_cell_cam(player_centre, arena, max)` → the cell the player is in, clamped; any change schedules `render::repaint()`, which repaints **80 arena rows per frame** (`render::repaint_step`, 51 KB ≈ 10.4 ms instead of 102 KB in one 20.9 ms frame) — one frame shows a half-painted cell, the repaint runs before sprites are drawn so it can never paint over a live one
- frame pacing is a target deadline (`_frame_ms` 33) in `update()`, not `delay(33)`: a heavy frame pushes the next one out and never tries to catch up
- the menu/mode/scores/pause/game-over screens **never repaint per frame**. A full-screen `fill_rect` is 320x240x2 = 153600 bytes ≈ 30 ms of SPI at 40 MHz, so doing it every frame ate the whole 33 ms budget *and* tore against the panel scan-out (a line sweeping corner to corner). `_menu_entered()` returns true once per screen entry → the caller does its one full paint; after that `_menu_cursor(items, count, x, y0)` repaints only the two lines whose `>` moved (~6 KB, sub-ms). `count` is the bounds guard on `_sel`. Adding a 4th item means fixing the `% 4` wrap too, or the cursor walks off the list
- menu rows live in shared constants (`_menu_x`/`_menu_y`/`_menu_row` 16/56/16, `_over_x`/`_over_y` 24/110 because game over is indented) and both the full paint and the cursor repaint must place row `i` at `y0 + i*_menu_row`. `_menu_item` takes an **absolute** y and must never add the pitch itself: when it did, the full paint landed on 56/88/120 while `_menu_cursor` repainted 56/72/88, so moving the cursor overwrote two different lines and the old text stayed on screen
- anything that enters `playing` must call `_menu_invalidate()` (`_start_game` + both resume paths). `playing` never calls `_menu_entered`, so nothing else clears `_menu_scr`: without it `pause → resume → pause` matches the stored screen id, skips the full paint, and the second pause shows stale chrome
- `test/test_native` does **not** compile `Game.cpp` (nor `sim`/`render`/`panel`, which need Arduino), so it cannot catch either of those two. Its layout check only covers row collisions and room for a 4th item; a check that re-derives the coordinate formula would be decorative, so verify menu geometry by reading the code or on the glass
- erase is per-pixel-equivalent: `render::_erase_world_rect` walks every world px with `tilemap::color_at` and emits one `fill_rect` per same-colour run (verified identical to per-pixel); the order is `render::clear()` → `sim::step()` → `render::update_camera()` → `render::draw()`, so the clear-before-sim is what erases entities that die mid-frame. `game` owns that order, it is not spread across the libraries
- hard cut hides up to half a sprite while crossing a cell edge; `render::_fill_world_box`, `render::_fill_world_run` and `render::_erase_world_rect` all clip to the arena `[10,170)`, so nothing bleeds into the HUD nor the panel
- bottom panel: `panel::init` paints it once per run (black + 388-run minimap terrain + 3x3 cell separators, ~17 ms one-off), then per frame only `panel::draw` (text + HP pips) and `panel::blips` run; the minimap is 2px/tile (120x60) and dots are erased by repainting `tilemap::color_at` of the tile underneath, tracked in `panel::_mm_px/_mm_py`
- the minimap also outlines the camera cell (`panel::_mm_frame`, 1px `colour::yellow`, 40x20 px at `panel::_mm_ctx/_mm_cty`): the 4 edges of last frame's cell are restored first via `_mm_restore_row/_mm_restore_col` (run-length along their own axis, so the vertical edges collapse to ~2 calls each), then the new outline, then the dots on top. `panel::init` clears `_mm_ctx`/`_mm_n` because the base repaint wipes both
- zombie spawns scan tiles from a random offset and take the first walkable one >= 100px away, so it never depends on the world border being walkable
- `tilemap::solid_rect` gates movement per axis (X then Y); bullets die on any non-FLOOR tile
- zombie AI steers down a BFS distance field to the player: `tilemap::build_field(tx, ty)` floods `tilemap::field[ROWS][COLS]` (uint16, `UNREACHABLE` = 0xFFFF) with tile distances, and only runs when the player's **tile** changes, so it needs no timer and cannot go stale (~7 builds/s, ~0.9 ms each). Every reachable tile with `d > 0` has a 4-neighbour with `d-1`, so descending never stalls (asserted in `test/test_native/map_test.cpp`: 1218 tiles, 0 local minima, max 76)
- `sim::_zombie_steer` picks the lowest-distance of the 8 neighbours (`_nbr_x/_nbr_y`, cardinals first) and aims at that tile's **centre**, so a diagonal reads as drift instead of a tile-by-tile shuffle; it tries up to 3 candidates because the best one can be a diagonal that a wall corner blocks, and `_move_entity` turns that into a slide along the wall rather than a pass through it. A zombie on the player's tile stops (the contact check lands the hit) and an `UNREACHABLE` tile falls back to direct chase
- `sim::_step_zombie` must hand `sim::_move_entity` the **real** members, not copies: it takes `float&`, so passing locals silently discards the movement and the zombie stands still forever. It keeps the before-coordinates to report whether the body actually shifted, which is what the 3-candidate retry keys on
- `sim` exposes its state as a single read-only `sim::view()` and nothing else: `reset()`, `step(now)` (own `dt` + clamp) and the size constants. The renderer, the panel and the menus read the view, never the fields, so a `const state&` reference is enough and no subsystem can move a zombie or spend a point. `step` returns `false` when the player is out of hp and the **caller** raises the game over screen: sim never touches `_scr`
- the run score belongs to `sim` and is zeroed by `sim::reset()`, never by the game over path. Zeroing it when entering game over made `_draw_game_over` print `Score: 0` for every run, since it read the field after the wipe
- this means **no zombie ever gets stuck** now, so the alley dead ends and the dock warehouse are no longer safe refuges and the waves are harder; the knobs are `_zombie_speed`, the wave count and `_spawn_min_d2`
- `render` and `panel` must not call each other: `panel` reads `render::cam_x()/cam_y()`, and `game` calls `render::draw()` then `panel::draw()` then `panel::blips()`. The role badge (HOST/CLIENT) is net state, so it is drawn by `game` after the panel, not by the renderer — that keeps `render` free of any `handler` dependency
- if a screen region keeps stale/ghost content across fills and reboots → driver/window mismatch, not a dead panel

## Next (M3)

- **(P3) P2P net sync** — marshal game state over ESP-NOW (bullets/zombies/player), roles host/client from build; reproducible hand-roll steps in `lib/protocol`. The client currently runs the same local sim from its own spawn, which is fine for one peer but diverges as soon as shots land.
- (P4/future) microSD scores persistence — not integrated yet; ArduinoJson only after hardware. The seam already exists: `scores` is the only place that touches the RTC
- `_art` rows must stay exactly `COLS` chars: a longer row shifts the districts and eats the ring road (the ring check in `test/test_native/map_test.cpp` catches it).

## Add a message

- add a `msg_type` value in `lib/protocol/src/Protocol.h`
- add its packed struct (with `type` first) in the same header
- register `on_message(type, callback)` in `lib/game/src/Game.cpp`
- send it with `_handler.send(&msg, sizeof(msg))`

## microSD (future)

- persist per-player scores with ArduinoJson; hardware not integrated yet
