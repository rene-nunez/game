#include <Arduino.h>
#include <cmath>
#include <cstring>

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <Display.h>
#include "Game.h"
#include <map.h>
#include <Sim.h>
#include "pins.h"

handler game::_handler;
uint32_t game::_tick = 0;
uint32_t game::_peer_tick = 0;

uint8_t game::_menu_scr = 0xFF; // no menu painted yet
uint8_t game::_menu_sel = 0;

int16_t game::_cam_x = 0;
int16_t game::_cam_y = 0;
int16_t game::_paint_y = game::_arena_h;
uint32_t game::_last_frame_ms = 0;

int16_t game::_mm_px[1 + sim::MAX_ZOMBIES] = {0};
int16_t game::_mm_py[1 + sim::MAX_ZOMBIES] = {0};
uint8_t game::_mm_n = 0;
int16_t game::_mm_ctx = -1;
int16_t game::_mm_cty = -1;

uint32_t game::_best = 0;
uint32_t game::_total_kills = 0;

game::_screens game::_scr = game::_screens::menu;
uint8_t game::_sel = 0;
int8_t game::_nav_dir = 0;

namespace {
  constexpr uint32_t _RTC_MAGIC = 0x5A3C21EDu;

  struct _rtc_scores {
    uint32_t magic;
    uint32_t best;
    uint32_t total_kills;
  };

  RTC_NOINIT_ATTR _rtc_scores _rtc;
}

namespace {
  const char* const _menu_items[] = { "Start Game", "Scores", "Exit" };
  const char* const _mode_items[] = { "Solo", "Multiplayer", "Back" };
  const char* const _pause_items[] = { "Continue", "Restart", "Exit to Menu" };
  const char* const _over_items[] = { "Restart", "Menu" };
}

bool game::begin(uint8_t role) {
  Serial.begin(115200);
  delay(200);

  if (!_handler.begin(role)) {
    Serial.println("[game] network init failed, resetting...");
    delay(1000);
    ESP.restart();
    return false;
  }

  _handler.on_message(msg_type::heartbeat, _on_heartbeat);

  if (!display::begin()) {
    Serial.println("[game] display init failed, resetting...");
    delay(1000);
    ESP.restart();
    return false;
  }

  if (!input::begin()) {
    Serial.println("[game] input init failed, resetting...");
    delay(1000);
    ESP.restart();
    return false;
  }

  tilemap::init();

  if (_rtc.magic == _RTC_MAGIC) {
    _best = _rtc.best;
    _total_kills = _rtc.total_kills;
  } else {
    _best = 0;
    _total_kills = 0;
    _rtc.magic = _RTC_MAGIC;
    _rtc.best = 0;
    _rtc.total_kills = 0;
  }

  _scr = _screens::menu;
  _sel = 0;
  _nav_dir = 0;
  _paint_y = _arena_h;
  _last_frame_ms = millis();

  _draw_menu("ZOMBIES", _menu_items, 3);

  Serial.println("[game] ready");
  return true;
}

void game::update() {
  input::update();

  switch (_scr) {
    case _screens::menu: _update_menu(); break;
    case _screens::mode: _update_mode(); break;
    case _screens::scores: _update_scores(); break;
    case _screens::playing: _update_playing(); break;
    case _screens::pause: _update_pause(); break;
    case _screens::game_over: _update_game_over(); break;
  }

  // target-paced: a heavy frame pushes the next one out, it never catches up
  const uint32_t spent = millis() - _last_frame_ms;
  if (spent < _frame_ms) {
    delay(_frame_ms - spent);
  }
  _last_frame_ms = millis();
}

void game::_on_heartbeat(const uint8_t* data, size_t len) {
  if (len < sizeof(heartbeat_msg)) {
    return;
  }

  heartbeat_msg hb;
  memcpy(&hb, data, sizeof(hb));
  _peer_tick = hb.tick;
  Serial.printf("[game] heartbeat from peer: tick=%lu role=%u\n", hb.tick, hb.role);
}

int8_t game::_nav_edge() {
  const int8_t cur = input::jy() > 0.5f ? 1 : (input::jy() < -0.5f ? -1 : 0);
  const int8_t edge = (cur != 0 && _nav_dir == 0) ? cur : 0;
  _nav_dir = cur;
  return edge;
}

void game::_menu_item(const char* const* items, uint8_t i, bool selected, int16_t x, int16_t y) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%c %s", selected ? '>' : ' ', items[i]);
  display::text(buf, x, y, selected ? colour::green : colour::white, 1);
}

// A full-screen fill is 320*240*2 = 153600 bytes, ~31ms of SPI at 40MHz, so repainting
// it every frame both blew the 33ms budget and tore against the panel scan-out: that was
// the line sweeping corner to corner. Paint the chrome once per screen entry, then only
// the two cursor lines when the selection moves.
bool game::_menu_entered(void) {
  if (_menu_scr == (uint8_t)_scr) {
    return false;
  }
  _menu_scr = (uint8_t)_scr;
  _menu_sel = _sel;
  return true;
}

// The playing screen never calls _menu_entered, so nothing else invalidates the paint: on
// pause -> resume -> pause the screen id matches again and the full paint would be skipped.
void game::_menu_invalidate(void) {
  _menu_scr = 0xFF;
}

void game::_menu_cursor(const char* const* items, uint8_t count, int16_t x, int16_t y0) {
  if (_sel == _menu_sel || _sel >= count || _menu_sel >= count) { // count guards _sel
    return;
  }
  _menu_item(items, _menu_sel, false, x, y0 + (int16_t)_menu_sel * _menu_row); // loses the cursor
  _menu_item(items, _sel, true, x, y0 + (int16_t)_sel * _menu_row);            // gains it
  _menu_sel = _sel;
}

void game::_draw_menu(const char* title, const char* const* items, uint8_t count) {
  display::fill_rect(0, 0, display::width(), display::height(), colour::black);
  display::text(title, (display::width() - 6 * (int16_t)strlen(title) * 2) / 2, 24, colour::yellow, 2);

  for (uint8_t i = 0; i < count; ++i) {
    _menu_item(items, i, i == _sel, _menu_x, _menu_y + (int16_t)i * _menu_row);
  }

  display::text("JOY: move   FIRE: select", _menu_x, _menu_y + (int16_t)count * _menu_row + 24,
                colour::white, 1);
}

void game::_draw_scores() {
  display::fill_rect(0, 0, display::width(), display::height(), colour::black);
  display::text("SCORES", (display::width() - 6 * 6 * 2) / 2, 24, colour::yellow, 2);

  char buf[32];
  snprintf(buf, sizeof(buf), "Best score: %lu", _best);
  display::text(buf, 16, 60, colour::white, 1);
  snprintf(buf, sizeof(buf), "Total kills: %lu", _total_kills);
  display::text(buf, 16, 76, colour::white, 1);

  display::text("FIRE/PAUSE: back", 16, 120, colour::white, 1);
}

void game::_start_game() {
  sim::reset();
  _panel_init(); // static panel + minimap terrain, then blips on top
  _update_camera();
  _paint_view(); // forced: the game over screen cleared the arena and the camera may not move
  _scr = _screens::playing;
  _menu_invalidate(); // the next pause must repaint its chrome
}

void game::_enter_menu() {
  _scr = _screens::menu;
  _sel = 0;
  _nav_dir = 0;
}

void game::_enter_game_over() {
  const sim::state& v = sim::view();
  _total_kills += v.kills;
  if (v.score > _best) {
    _best = v.score;
  }
  _rtc.best = _best;
  _rtc.total_kills = _total_kills;
  _scr = _screens::game_over;
  _sel = 0;
}

void game::_sleep() {
  display::backlight(false);

  rtc_gpio_pullup_en((gpio_num_t)BTN_PAUSE);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PAUSE, 0); // LOW = pressed

  Serial.println("[game] sleeping...");
  Serial.flush();
  esp_deep_sleep_start();
}

void game::_update_menu() {
  if (_menu_entered()) {
    _draw_menu("ZOMBIES", _menu_items, 3);
  }

  const int8_t e = _nav_edge();
  if (e) {
    _sel = (uint8_t)((_sel + 3 + e) % 3);
  }
  _menu_cursor(_menu_items, 3, _menu_x, _menu_y);
  if (input::fire_pressed()) {
    switch (_sel) {
      case 0:
        _scr = _screens::mode;
        _sel = 0;
        break;
      case 1:
        _scr = _screens::scores;
        _sel = 0;
        break;
      default: // Exit
        _sleep();
        break;
    }
  }
}

void game::_update_mode() {
  if (_menu_entered()) {
    _draw_menu("GAME MODE", _mode_items, 3);
  }

  const int8_t e = _nav_edge();
  if (e) {
    _sel = (uint8_t)((_sel + 3 + e) % 3);
  }
  _menu_cursor(_mode_items, 3, _menu_x, _menu_y);
  if (input::fire_pressed()) {
    if (_sel == 2) { // Back
      _enter_menu();
    } else {
      _start_game(); // Solo / Multiplayer (net-handshake comes in P3)
    }
  }
}

void game::_update_scores() {
  if (_menu_entered()) {
    _draw_scores();
  }

  if (input::fire_pressed() || input::pause_pressed()) {
    _enter_menu();
  }
}

void game::_update_playing() {
  heartbeat_msg hb;
  hb.tick = _tick++;
  hb.role = _handler.role();
  _handler.send(&hb, sizeof(hb));

  if (input::pause_pressed()) {
    _scr = _screens::pause;
    _sel = 0;
    return;
  }

  // the order matters: the clear-before-sim is what erases entities that die mid-frame
  _render_clear();
  if (!sim::step(millis())) {
    _enter_game_over(); // sim reports the death, the screen change belongs here
    return;
  }
  _update_camera();
  _render_draw();
}

void game::_update_pause() {
  if (_menu_entered()) {
    _draw_menu("PAUSED", _pause_items, 3);
  }

  const int8_t e = _nav_edge();
  if (e) {
    _sel = (uint8_t)((_sel + 3 + e) % 3);
  }
  _menu_cursor(_pause_items, 3, _menu_x, _menu_y);
  if (input::pause_pressed()) {
    _scr = _screens::playing;
    _menu_invalidate(); // the next pause must repaint its chrome
    _panel_init(); // the pause menu covered the panel and the minimap
    _paint_view(); // clear leftover pause menu
  } else if (input::fire_pressed()) {
    switch (_sel) {
      case 0: // Continue
        _scr = _screens::playing;
        _menu_invalidate(); // the next pause must repaint its chrome
        _panel_init(); // the pause menu covered the panel and the minimap
        _paint_view(); // clear leftover pause menu
        break;
      case 1: // Restart
        _start_game();
        break;
      default: // Exit to Menu
        _enter_menu();
        break;
    }
  }
}

void game::_update_game_over() {
  if (_menu_entered()) {
    _draw_game_over();
  }

  const int8_t e = _nav_edge();
  if (e) {
    _sel = (uint8_t)((_sel + 2 + e) % 2);
  }
  _menu_cursor(_over_items, 2, _over_x, _over_y);
  if (input::fire_pressed()) {
    if (_sel == 0) {
      _start_game();
    } else {
      _enter_menu();
    }
  }
}

void game::_draw_game_over() {
  display::fill_rect(0, 0, display::width(), display::height(), colour::black);
  display::text("GAME OVER", (display::width() - 6 * 9 * 2) / 2, 24, colour::red, 2);

  char buf[32];
  const sim::state& v = sim::view();
  snprintf(buf, sizeof(buf), "Score: %lu   Best: %lu", v.score, _best);
  display::text(buf, 24, 60, colour::white, 1);
  snprintf(buf, sizeof(buf), "Wave: %u  Kills: %u", v.wave, v.kills);
  display::text(buf, 24, 76, colour::white, 1);

  for (uint8_t i = 0; i < 2; ++i) {
    _menu_item(_over_items, i, i == _sel, _over_x, _over_y + (int16_t)i * _menu_row);
  }

  display::text("JOY: move   FIRE: select", _over_x, _over_y + 2 * _menu_row + 24, colour::white, 1);
}

void game::_paint_view() {
  display::fill_rect(0, 0, (int16_t)display::width(), _hud_h, colour::black); // hud strip
  _paint_y = 0; // the arena repaint runs from here, _paint_chunk rows per frame
}

void game::_paint_step() {
  if (_paint_y >= _arena_h) {
    return; // nothing pending
  }

  const int16_t sw = (int16_t)display::width();
  const int16_t end = (_paint_y + _paint_chunk < _arena_h) ? _paint_y + _paint_chunk : _arena_h;

  for (int16_t ry = _paint_y; ry < end; ++ry) { // merge same-colour runs per row
    const int16_t wy = _cam_y + ry;
    const int16_t sy = _hud_h + ry;
    int16_t run_x = 0;
    uint16_t run_col = tilemap::color_at(_cam_x, wy);

    for (int16_t rx = 1; rx < sw; ++rx) {
      const uint16_t col = tilemap::color_at(_cam_x + rx, wy);
      if (col != run_col) {
        display::fill_rect(run_x, sy, rx - run_x, 1, run_col);
        run_x = rx;
        run_col = col;
      }
    }
    display::fill_rect(run_x, sy, sw - run_x, 1, run_col);
  }

  _paint_y = end;
}

int16_t game::_cell_cam(int16_t p, int16_t step, int16_t max_cam) {
  int16_t cam = (int16_t)((p / step) * step);
  if (cam < 0) {
    cam = 0;
  } else if (cam > max_cam) {
    cam = max_cam;
  }
  return cam;
}

void game::_update_camera() {
  const int16_t aw = (int16_t)display::width();
  const sim::state& v = sim::view();
  const int16_t pcx = (int16_t)(v.player.x + sim::PLAYER_SIZE / 2);
  const int16_t pcy = (int16_t)(v.player.y + sim::PLAYER_SIZE / 2);

  const int16_t cx = _cell_cam(pcx, aw, (int16_t)(tilemap::WORLD_W - aw));
  const int16_t cy = _cell_cam(pcy, _arena_h, (int16_t)(tilemap::WORLD_H - _arena_h));

  if (cx == _cam_x && cy == _cam_y) {
    return;
  }
  _cam_x = cx;
  _cam_y = cy;
  _paint_view(); // the new screen must be repainted from the tilemap
}


void game::_fill_world_run(int16_t wx, int16_t sy, int16_t w, uint16_t col) {
  if (w <= 0 || sy < _hud_h || sy >= _arena_bottom) {
    return;
  }
  const int16_t sw = (int16_t)display::width();
  int16_t x0 = wx - _cam_x;
  int16_t x1 = x0 + w;
  if (x1 <= 0 || x0 >= sw) {
    return; // fully off-viewport
  }
  if (x0 < 0) {
    x0 = 0;
  }
  if (x1 > sw) {
    x1 = sw;
  }
  display::fill_rect(x0, sy, x1 - x0, 1, col);
}

void game::_erase_world_rect(int16_t wx, int16_t wy, uint8_t size) {
  for (int16_t dy = 0; dy < (int16_t)size; ++dy) {
    const int16_t wyy = wy + dy;
    const int16_t sy = wyy - _cam_y + _hud_h;
    if (sy < _hud_h || sy >= _arena_bottom) {
      continue;
    }

    int16_t run_x = wx;
    uint16_t run_col = tilemap::color_at(wx, wyy);
    for (int16_t dx = 1; dx < (int16_t)size; ++dx) {
      const uint16_t col = tilemap::color_at(wx + dx, wyy);
      if (col != run_col) {
        _fill_world_run(run_x, sy, (int16_t)(wx + dx - run_x), run_col);
        run_x = wx + dx;
        run_col = col;
      }
    }
    _fill_world_run(run_x, sy, (int16_t)(wx + size - run_x), run_col);
  }
}

void game::_fill_world_box(int16_t wx, int16_t wy, uint8_t size, uint16_t col) {
  const int x0 = wx - _cam_x;
  const int y0 = wy - _cam_y + _hud_h;
  // clip to the arena so a half-sprite never bleeds into the hud strip nor the panel
  const int cx0 = x0 > 0 ? x0 : 0;
  const int cy0 = y0 > _hud_h ? y0 : _hud_h;
  const int cx1 = x0 + size < (int)display::width() ? x0 + size : (int)display::width();
  const int cy1 = y0 + size < (int)_arena_bottom ? y0 + size : (int)_arena_bottom;

  if (cx0 < cx1 && cy0 < cy1) {
    display::fill_rect(cx0, cy0, cx1 - cx0, cy1 - cy0, col);
  }
}



void game::_render_clear() {
  const sim::state& v = sim::view();
  _erase_world_rect((int16_t)v.player.x, (int16_t)v.player.y, sim::PLAYER_SIZE);
  for (uint8_t i = 0; i < sim::MAX_ZOMBIES; ++i) {
    if (v.zombies[i].active) {
      _erase_world_rect((int16_t)v.zombies[i].x, (int16_t)v.zombies[i].y, sim::ZOMBIE_SIZE);
    }
  }
  for (uint8_t i = 0; i < sim::MAX_BULLETS; ++i) {
    if (v.bullets[i].active) {
      _erase_world_rect((int16_t)v.bullets[i].x, (int16_t)v.bullets[i].y, sim::BULLET_SIZE);
    }
  }
}

void game::_render_draw() {
  char buf[32];
  _paint_step(); // terrain first, so a cut never paints over a live sprite

  const sim::state& v = sim::view();
  snprintf(buf, sizeof(buf), "SCORE %lu", v.score);
  display::text(buf, 4, 1, colour::yellow, 1);
  display::text(_handler.role() == ROLE_HOST ? "HOST" : "CLIENT", (int16_t)(display::width() - 34), 1,
                colour::cyan, 1);

  _fill_world_box((int16_t)v.player.x, (int16_t)v.player.y, sim::PLAYER_SIZE, colour::blue);
  for (uint8_t i = 0; i < sim::MAX_ZOMBIES; ++i) {
    if (v.zombies[i].active) {
      _fill_world_box((int16_t)v.zombies[i].x, (int16_t)v.zombies[i].y, sim::ZOMBIE_SIZE,
                      colour::red);
    }
  }
  for (uint8_t i = 0; i < sim::MAX_BULLETS; ++i) {
    if (v.bullets[i].active) {
      _fill_world_box((int16_t)v.bullets[i].x, (int16_t)v.bullets[i].y, sim::BULLET_SIZE,
                      colour::white);
    }
  }

  _draw_panel();
  _minimap_blips();
}

int16_t game::_mm_x() {
  return (int16_t)display::width() - _mm_w - _mm_gap;
}

void game::_panel_init() {
  const int16_t mx = _mm_x();
  _mm_ctx = -1; // the base repaint wipes the frame and the blips
  _mm_n = 0;
  display::fill_rect(0, _arena_bottom, (int16_t)display::width(), _panel_h, colour::black);

  for (uint8_t r = 0; r < tilemap::ROWS; ++r) { // minimap terrain, same-colour runs
    const int16_t sy = _mm_y + (int16_t)r * _mm_scale;
    const int16_t wy = (int16_t)r * tilemap::TILE;
    int16_t run_x = 0;
    uint16_t run_col = tilemap::color_at(0, wy);

    for (uint8_t c = 1; c < tilemap::COLS; ++c) {
      const uint16_t col = tilemap::color_at((int16_t)c * tilemap::TILE, wy);
      if (col != run_col) {
        display::fill_rect(mx + run_x * _mm_scale, sy, (int16_t)(c - run_x) * _mm_scale, _mm_scale,
                           run_col);
        run_x = c;
        run_col = col;
      }
    }
    display::fill_rect(mx + run_x * _mm_scale, sy, (int16_t)(tilemap::COLS - run_x) * _mm_scale,
                       _mm_scale, run_col);
  }

  const uint16_t grid = display::rgb565(80, 80, 80); // 3x3 camera cell separators
  for (uint8_t i = 1; i < 3; ++i) {
    display::fill_rect(mx + (int16_t)(i * (tilemap::COLS / 3)) * _mm_scale, _mm_y, 1, _mm_h, grid);
    display::fill_rect(mx, _mm_y + (int16_t)(i * (tilemap::ROWS / 3)) * _mm_scale, _mm_w, 1, grid);
  }
  _mm_n = 0;
}

void game::_draw_panel() {
  const sim::state& v = sim::view();
  char buf[32];

  snprintf(buf, sizeof(buf), "BEST %lu", _best);
  display::text(buf, 4, _arena_bottom + 4, colour::white, 1);
  snprintf(buf, sizeof(buf), "WAVE %u", v.wave);
  display::text(buf, 4, _arena_bottom + 16, colour::white, 1);
  snprintf(buf, sizeof(buf), "KILLS %u", v.kills);
  display::text(buf, 4, _arena_bottom + 28, colour::white, 1);

  display::text("HP", 4, _arena_bottom + 40, colour::white, 1);
  const uint16_t live = (v.player_hp <= 2) ? colour::red : colour::green;
  const uint16_t spent = display::rgb565(40, 40, 40);
  for (uint8_t i = 0; i < sim::PLAYER_HP_MAX; ++i) {
    display::fill_rect(30 + (int16_t)i * 10, _arena_bottom + 40, 8, 8, (i < v.player_hp) ? live : spent);
  }
}

void game::_mm_restore_row(int16_t tx0, int16_t tx1, int16_t ty) {
  const int16_t mx = _mm_x();
  const int16_t sy = _mm_y + ty * _mm_scale;
  int16_t run_x = tx0;
  uint16_t run_col = tilemap::color_at(tx0 * tilemap::TILE, ty * tilemap::TILE);

  for (int16_t tx = tx0 + 1; tx <= tx1; ++tx) {
    const uint16_t col = tilemap::color_at(tx * tilemap::TILE, ty * tilemap::TILE);
    if (col != run_col) {
      display::fill_rect(mx + run_x * _mm_scale, sy, (tx - run_x) * _mm_scale, _mm_scale, run_col);
      run_x = tx;
      run_col = col;
    }
  }
  display::fill_rect(mx + run_x * _mm_scale, sy, (tx1 - run_x + 1) * _mm_scale, _mm_scale, run_col);
}

void game::_mm_restore_col(int16_t tx, int16_t ty0, int16_t ty1) {
  const int16_t mx = _mm_x() + tx * _mm_scale;
  int16_t run_y = ty0;
  uint16_t run_col = tilemap::color_at(tx * tilemap::TILE, ty0 * tilemap::TILE);

  for (int16_t ty = ty0 + 1; ty <= ty1; ++ty) {
    const uint16_t col = tilemap::color_at(tx * tilemap::TILE, ty * tilemap::TILE);
    if (col != run_col) {
      display::fill_rect(mx, _mm_y + run_y * _mm_scale, _mm_scale, (ty - run_y) * _mm_scale, run_col);
      run_y = ty;
      run_col = col;
    }
  }
  display::fill_rect(mx, _mm_y + run_y * _mm_scale, _mm_scale, (ty1 - run_y + 1) * _mm_scale, run_col);
}

void game::_mm_dot(int16_t wx, int16_t wy, uint16_t col) {
  if (_mm_n >= (uint8_t)(1 + sim::MAX_ZOMBIES)) {
    return;
  }
  display::fill_rect(_mm_x() + (wx / tilemap::TILE) * _mm_scale, _mm_y + (wy / tilemap::TILE) * _mm_scale,
                     _mm_scale, _mm_scale, col);
  _mm_px[_mm_n] = wx;
  _mm_py[_mm_n] = wy;
  ++_mm_n;
}

void game::_mm_frame() {
  // the camera snaps to a whole cell, so the frame always lands on even minimap pixels
  const int16_t cell_w = (int16_t)(display::width() / tilemap::TILE);
  const int16_t cell_h = (int16_t)(_arena_h / tilemap::TILE);
  const int16_t ctx = (int16_t)(_cam_x / tilemap::TILE);
  const int16_t cty = (int16_t)(_cam_y / tilemap::TILE);

  if (_mm_ctx >= 0) { // put the terrain back under the frame drawn last frame
    _mm_restore_row(_mm_ctx, _mm_ctx + cell_w - 1, _mm_cty);
    _mm_restore_row(_mm_ctx, _mm_ctx + cell_w - 1, _mm_cty + cell_h - 1);
    _mm_restore_col(_mm_ctx, _mm_cty, _mm_cty + cell_h - 1);
    _mm_restore_col(_mm_ctx + cell_w - 1, _mm_cty, _mm_cty + cell_h - 1);
  }

  const int16_t fx = _mm_x() + ctx * _mm_scale;
  const int16_t fy = _mm_y + cty * _mm_scale;
  const int16_t fw = cell_w * _mm_scale;
  const int16_t fh = cell_h * _mm_scale;

  display::fill_rect(fx, fy, fw, 1, colour::yellow);
  display::fill_rect(fx, fy + fh - 1, fw, 1, colour::yellow);
  display::fill_rect(fx, fy, 1, fh, colour::yellow);
  display::fill_rect(fx + fw - 1, fy, 1, fh, colour::yellow);

  _mm_ctx = ctx;
  _mm_cty = cty;
}

void game::_minimap_blips() {
  for (uint8_t i = 0; i < _mm_n; ++i) { // restore the terrain under last frame's dots
    const int16_t tx = _mm_px[i] / tilemap::TILE;
    const int16_t ty = _mm_py[i] / tilemap::TILE;
    display::fill_rect(_mm_x() + tx * _mm_scale, _mm_y + ty * _mm_scale, _mm_scale, _mm_scale,
                       tilemap::color_at(_mm_px[i], _mm_py[i]));
  }
  _mm_n = 0;

  _mm_frame();

  const sim::state& v = sim::view();
  _mm_dot((int16_t)v.player.x, (int16_t)v.player.y, colour::white);
  for (uint8_t i = 0; i < sim::MAX_ZOMBIES; ++i) {
    if (v.zombies[i].active) {
      _mm_dot((int16_t)v.zombies[i].x, (int16_t)v.zombies[i].y, colour::red);
    }
  }
}
