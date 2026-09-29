# AGENTS.md — Z32 (ex game)

## Style

- `snake_case` everywhere; class private members and instances prefixed `_`; read `.editorconfig`

## Network

- packed structs (`__attribute__((packed))`); first field is always `type`
- 250 bytes/msg max; frequent state must be binary, not JSON
- host is authoritative (runs game logic, sends state); client sends inputs
- fixed custom MACs; role picks the peer
- role comes from build: `pio run -e host` | `-e client` (flag `DEVICE_ROLE`); `include/config.h` only validates it

## Pins

- TFT SPI: `CS5/RST4/DC2/MOSI23/SCLK18/MISO19/BL21`; joystick ADC1 `32/33`; buttons `FIRE13/RELOAD14/INTERACT15/PAUSE27`
- buzzer pasivo: GPIO 26 vía LEDC (`lib/buzz`, non-blocking)
- microSD: comparte bus SPI del TFT (23/19/18) + CS dedicado GPIO 22 + VCC/GND
- ADC2 no se usa para analógico (WiFi/ESP-NOW lo inhabilita); LEDC en 25/26 es digital y no choca

## Layout

- `include/` — `pins.h`, `tft_setup.h` (ST7789 + `TFT_INVERSION_OFF` + `TFT_RGB_ORDER TFT_BGR`, panel BGR), `config.h`
- `lib/network` — raw ESP-NOW, no message logic; `lib/protocol` — `msg_type` + packed structs; `lib/handler` — typed routing/dispatch
- `lib/display` — `display` + `colour`; the only place TFT_eSPI is used
- `lib/input` — joystick (ADC1) + buttons (debounce + edge)
- `lib/world` — `tilemap`: 60x30 all-grass maze, `_art` rows, wall queries, spawn, BFS `field`; tiles: grass (only walkable), hedge walls, three 2x2 vendings (H heal green, D damage red, S speed blue) + 2x2 roulette (visual 32px sprites in `color_at`, price tags in `render`)
- `lib/sim` — player, zombies, bullets, waves, score-as-wallet, weapons, buffs; owns state, never touches screen/menus/network/sound (emits `last_event`)
- `lib/render` — camera, terrain repaint, arena sprites + shop price tags; reads `sim::view()` + `tilemap` (tags are tile-anchored)
- `lib/panel` — bottom strip: `SCORE/WAVE/KILLS/GUN` + HP pips + buff timers + shop hint + 2px/tile minimap; reads `render` + `sim::view()`
- `lib/buzz` — passive-buzzer jingles, non-blocking (`update(now)`); `game` fires it from `sim::last_event`
- `lib/scores` — RTC-backed `{best, total_kills}` today; the microSD seam (same 4 functions)
- `lib/screens` — `id` enum + item tables + menu chrome
- `lib/game` — state machine + input edges + **frame order**; only place calling sim + render + panel + screens + buzz together
- `src/main.cpp` — `game::begin(DEVICE_ROLE)` + `game::update()`
- `test/test_native` — host-side tilemap/camera checks; `./test/test_native/run.sh` builds `map_test.cpp` with plain `g++` (binary to `$TMPDIR`, no Unity); `test_ignore = test_native` keeps `pio test` off it

Libraries resolve via LDF `chain`. Every `lib/*/src/*.cpp` compiles always; cross-library includes use `<>`, same-directory use `""`. TFT config repo-wide from `[env]`: `-D USER_SETUP_LOADED` + `-include tft_setup.h`. Filenames case-sensitive on Linux.

## Display (TFT)

- glass 240x320 ST7789; world 960x480 (60x30 x 16px); bands: HUD 10px (role badge only), arena 160px, panel 70px (10+160+70 = 240 exactly)
- arena 320x160, camera clamped x[0,640] y[0,320] → exact **3x3 grid**, x{0,320,640} y{0,160,320}; hard-cut by cell (`_cell_cam`), repaint **80 rows/frame** before sprites
- frame pacing is a target deadline (`_frame_ms` 33), not `delay(33)`
- menus never repaint per frame: `screens::paint(scr, sel)` full-paints on entry, then only the two cursor lines; rows at `y0 + i*_menu_row`, `_item` takes absolute y; anything entering `playing` calls `screens::invalidate()`
- menu wrap reads `screens::count(scr)`; `static_assert` pins one table row per `screens::id`
- erase is per-pixel-equivalent (`_erase_world_rect` run-lengths over `tilemap::color_at`); frame order `render::clear()` → `sim::step()` → `render::update_camera()` → `render::draw()` owned by `game`; all arena fills clip to `[10,170)`
- `test/test_native` never compiles `Game.cpp`/sim/render/panel/screens (need Arduino); verify menu geometry on glass

## World / sim contracts

- `_art` rows exactly `COLS` chars; map: all grass + 2-tile ring lanes, maze everywhere, center holds 3 vendings + roulette; 1303 walkable (72%), 0 orphans, machines 16/16 reachable (4 per shop), BFS max 76, 0 local minima (all in `run.sh`)
- machine sprites must read at 32px (ASCII dump of `color_at`, never by eye)
- `tilemap::solid_rect` gates movement per axis (X then Y); bullets die on non-walkable
- zombie spawns: random-offset scan, first walkable tile >= 100px away
- BFS `field` rebuilds only when the player's **tile** changes; `_zombie_steer` descends to the best of 8 neighbours' centres (3 retries, direct chase on `UNREACHABLE`); pass `float&` members (never copies) to `_move_entity`
- `sim` exposes one read-only `view()` (`reset()`, `step(now)->bool`); death reported by return value, screens raised by caller; score zeroed only in `reset()`
- economy: score is the spendable wallet; `peak` tracks max wallet for best; `render`/`panel` never move state; `game` owns shop proximity + `INTERACT` edge + `buzz` firing from `last_event`

## Shop (F1) — agreed prices/stats

- vending (proximity + `INTERACT`, one machine per buff): **H heal green 100** (+2 HP), **D damage red x2 60s 150**, **S speed blue x1.4 30s 120**; denied hint when broke; price tags float over the machines
- roulette 100 → random weapon; weapons are **roulette-only, never bought**; start pistol (dmg1/cd500); SMG (dmg1/cd180); shotgun (3 pellets/cd900); rifle (dmg3/cd800)
- panel shows `GUN xxx` (text, no sprites); buffs expire by `speed_until`/damage timer
- zombies: normal (40/hp2/+10), **runner** (70/hp1/+15, orange), **boss** every wave%5==0 (30/hp25/+200, purple, 1 of 8 slots); `render` colours by `actor.kind`

## Roadmap

- **F1 shop+roulette** ✅ done: `sim::state` += `weapon/dmg_mult/speed_until/peak/last_event` (+`actor.kind`); `game` proximity+buy; `panel` hint+`GUN`; verify exact-score buys, expiry, peak-after-spend on glass
- **F2 Z32+intro+screens**: `ZOMBIES`→`Z32` strings + README (repo path unchanged); `screens::id` += `logo` (centered, timed/skippable) → `team` (names TBD by user) → `menu`
- **F3 buzzer**: `lib/buzz` on GPIO 26, jingles menu/shoot/buy/roulette/hurt/wave/game-over, fired from `last_event`
- **F4 runners+boss**: kinds, waves, colours, cap-8 slots; balance on glass
- **F5 microSD**: share TFT SPI + CS22; `Scores.cpp` → JSON `{best,total_kills}` (best = peak wallet); same 4 functions; needs hardware
- **F6 P3 net**: packed `game_state` ~120B (u16 positions + bit flags, no floats) + `player_input` 4B; host authoritative, client inputs; Solo/Multi handshake; 2-board test with logs. Runs last, once `sim::state` is final

## Add a message

- add `msg_type` in `lib/protocol/src/Protocol.h`, packed struct with `type` first, `on_message(type, cb)` in `lib/game/src/Game.cpp`, send via `_handler.send(&msg, sizeof(msg))`
