#include <Arduino.h>
#include <cmath>
#include <cstring>

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <Display.h>
#include "Game.h"
#include "map.h"
#include "pins.h"

handler game::_handler;
uint32_t game::_tick = 0;
uint32_t game::_peer_tick = 0;

int16_t game::_cam_x = 0;
int16_t game::_cam_y = 0;
int16_t game::_paint_y = game::_arena_h;
game::_player_data game::_player;
uint32_t game::_last_ms = 0;
uint32_t game::_last_frame_ms = 0;

game::_zombie game::_zombies[game::_max_zombies];
game::_bullet game::_bullets[game::_max_bullets];
int16_t game::_mm_px[1 + game::_max_zombies] = {0};
int16_t game::_mm_py[1 + game::_max_zombies] = {0};
uint8_t game::_mm_n = 0;
uint8_t game::_player_hp = 0;
uint8_t game::_wave = 0;
uint8_t game::_kills = 0;
uint32_t game::_last_shot = 0;
uint32_t game::_last_damage = 0;

uint32_t game::_score = 0;
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

void game::_draw_menu(const char* title, const char* const* items, uint8_t count) {
  display::fill_rect(0, 0, display::width(), display::height(), colour::black);
  display::text(title, (display::width() - 6 * (int16_t)strlen(title) * 2) / 2, 24, colour::yellow, 2);

  int16_t y = 56;
  for (uint8_t i = 0; i < count; ++i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%c %s", (i == _sel) ? '>' : ' ', items[i]);
    display::text(buf, 16, y, (i == _sel) ? colour::green : colour::white, 1);
    y += 16;
  }

  display::text("JOY: move   FIRE: select", 16, y + 24, colour::white, 1);
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
  _restart();
  _scr = _screens::playing;
}

void game::_enter_menu() {
  _scr = _screens::menu;
  _sel = 0;
  _nav_dir = 0;
}

void game::_enter_game_over() {
  _total_kills += _kills;
  if (_score > _best) {
    _best = _score;
  }
  _rtc.best = _best;
  _rtc.total_kills = _total_kills;
  _score = 0;
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
  _draw_menu("ZOMBIES", _menu_items, 3);

  const int8_t e = _nav_edge();
  if (e) {
    _sel = (uint8_t)((_sel + 3 + e) % 3);
  }
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
  _draw_menu("GAME MODE", _mode_items, 3);

  const int8_t e = _nav_edge();
  if (e) {
    _sel = (uint8_t)((_sel + 3 + e) % 3);
  }
  if (input::fire_pressed()) {
    if (_sel == 2) { // Back
      _enter_menu();
    } else {
      _start_game(); // Solo / Multiplayer (net-handshake comes in P3)
    }
  }
}

void game::_update_scores() {
  _draw_scores();

  if (input::fire_pressed() || input::pause_pressed()) {
    _enter_menu();
  }
}

void game::_update_playing() {
  heartbeat_msg hb;
  hb.tick = _tick++;
  hb.role = _handler.role();
  _handler.send(&hb, sizeof(hb));

  const uint32_t now = millis();
  float dt = (float)(now - _last_ms) / 1000.0f;
  if (dt > 0.05f) { // clamp big gaps (serial pauses, menu)
    dt = 0.05f;
  }
  _last_ms = now;

  if (input::pause_pressed()) {
    _scr = _screens::pause;
    _sel = 0;
    return;
  }

  _render_clear();
  _sim(dt, now);
  if (_scr != _screens::playing) { // died this frame, the game-over screen takes over
    return;
  }
  _update_camera();
  _render_draw();
}

void game::_update_pause() {
  _draw_menu("PAUSED", _pause_items, 3);

  const int8_t e = _nav_edge();
  if (e) {
    _sel = (uint8_t)((_sel + 3 + e) % 3);
  }
  if (input::pause_pressed()) {
    _scr = _screens::playing;
    _panel_init(); // the pause menu covered the panel and the minimap
    _paint_view(); // clear leftover pause menu
  } else if (input::fire_pressed()) {
    switch (_sel) {
      case 0: // Continue
        _scr = _screens::playing;
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
  _draw_game_over();

  const int8_t e = _nav_edge();
  if (e) {
    _sel = (uint8_t)((_sel + 2 + e) % 2);
  }
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
  snprintf(buf, sizeof(buf), "Score: %lu   Best: %lu", _score, _best);
  display::text(buf, 24, 60, colour::white, 1);
  snprintf(buf, sizeof(buf), "Wave: %u  Kills: %u", _wave, _kills);
  display::text(buf, 24, 76, colour::white, 1);

  int16_t y = 110;
  for (uint8_t i = 0; i < 2; ++i) {
    snprintf(buf, sizeof(buf), "%c %s", (i == _sel) ? '>' : ' ', _over_items[i]);
    display::text(buf, 24, y, (i == _sel) ? colour::green : colour::white, 1);
    y += 16;
  }

  display::text("JOY: move   FIRE: select", 24, y + 24, colour::white, 1);
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
  const int16_t pcx = (int16_t)(_player.x + _player_size / 2);
  const int16_t pcy = (int16_t)(_player.y + _player_size / 2);

  const int16_t cx = _cell_cam(pcx, aw, (int16_t)(tilemap::WORLD_W - aw));
  const int16_t cy = _cell_cam(pcy, _arena_h, (int16_t)(tilemap::WORLD_H - _arena_h));

  if (cx == _cam_x && cy == _cam_y) {
    return;
  }
  _cam_x = cx;
  _cam_y = cy;
  _paint_view(); // the new screen must be repainted from the tilemap
}

void game::_move_entity(float& x, float& y, float dx, float dy, uint8_t size) {
  x = constrain(x, 0.0f, (float)(tilemap::WORLD_W - size));
  y = constrain(y, 0.0f, (float)(tilemap::WORLD_H - size));

  const float nx = x + dx;
  if (!tilemap::solid_rect((int16_t)nx, (int16_t)y, size, size)) {
    x = nx;
  }
  const float ny = y + dy;
  if (!tilemap::solid_rect((int16_t)x, (int16_t)ny, size, size)) {
    y = ny;
  }
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

void game::_restart() {
  for (uint8_t i = 0; i < _max_bullets; ++i) {
    _bullets[i].active = false;
  }

  _kills = 0;
  _wave = 0;
  _score = 0;
  _last_shot = 0;
  _last_damage = 0;
  _player_hp = _player_hp_max;

  _player = {
      (float)tilemap::spawn_px - _player_size / 2.0f,
      (float)tilemap::spawn_py - _player_size / 2.0f,
  };

  _panel_init(); // static panel + minimap terrain, then blips on top
  _update_camera();
  _paint_view(); // forced: the game over screen cleared the arena and the camera may not move
  _spawn_wave();
}

void game::_spawn_wave() {
  ++_wave;
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    _zombies[i].active = false;
  }

  const uint8_t count = (_wave + 3u > _max_zombies) ? _max_zombies : (_wave + 3u);
  const float px = _player.x + _player_size / 2.0f;
  const float py = _player.y + _player_size / 2.0f;
  const uint16_t total = (uint16_t)(tilemap::COLS * tilemap::ROWS);
  const int16_t off = (int16_t)((tilemap::TILE - _zombie_size) / 2);

  for (uint8_t i = 0; i < count; ++i) {
    // scan every tile from a random offset, so a spot is always found
    const uint16_t start = (uint16_t)(esp_random() % total);
    for (uint16_t k = 0; k < total; ++k) {
      const uint16_t idx = (uint16_t)((start + k) % total);
      const int16_t tx = (int16_t)(idx % tilemap::COLS);
      const int16_t ty = (int16_t)(idx / tilemap::COLS);
      if (tilemap::solid(tx, ty)) {
        continue;
      }
      const float x = (float)(tx * tilemap::TILE + off);
      const float y = (float)(ty * tilemap::TILE + off);
      const float dx = x - px;
      const float dy = y - py;
      if (dx * dx + dy * dy < (float)_spawn_min_d2 && k + 1 < total) {
        continue; // too close to the player, keep looking
      }
      _zombies[i] = { x, y, _zombie_hp, true };
      break;
    }
  }
}

void game::_do_fire(uint32_t now) {
  if (now - _last_shot < _fire_cd_ms) {
    return;
  }

  int16_t best = -1;
  float best_d = _fire_range * _fire_range;
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    if (!_zombies[i].active) {
      continue;
    }
    const float dx = _zombies[i].x - _player.x;
    const float dy = _zombies[i].y - _player.y;
    const float d = dx * dx + dy * dy;
    if (d <= best_d) {
      best_d = d;
      best = (int16_t)i;
    }
  }
  if (best < 0) {
    return;
  }

  const float bx = _player.x + _player_size / 2.0f;
  const float by = _player.y + _player_size / 2.0f;
  float dx = _zombies[best].x + _zombie_size / 2.0f - bx;
  float dy = _zombies[best].y + _zombie_size / 2.0f - by;
  const float len = sqrtf(dx * dx + dy * dy);
  dx /= len;
  dy /= len;

  for (uint8_t i = 0; i < _max_bullets; ++i) {
    if (!_bullets[i].active) {
      _bullets[i] = {
          bx,
          by,
          dx * _bullet_speed,
          dy * _bullet_speed,
          true,
      };
      _last_shot = now;
      break;
    }
  }
}

void game::_sim(float dt, uint32_t now) {
  float dx = input::jx();
  float dy = input::jy();

  const float len = sqrtf(dx * dx + dy * dy);
  if (len > 1.0f) { // keep diagonal speed equal
    dx /= len;
    dy /= len;
  }

  _move_entity(_player.x, _player.y, dx * _player_speed * dt, dy * _player_speed * dt, _player_size);

  if (input::fire_pressed()) {
    _do_fire(now);
  }

  const float pcx = _player.x + _player_size / 2.0f;
  const float pcy = _player.y + _player_size / 2.0f;

  for (uint8_t i = 0; i < _max_bullets; ++i) {
    if (!_bullets[i].active) {
      continue;
    }
    _bullets[i].x += _bullets[i].vx * dt;
    _bullets[i].y += _bullets[i].vy * dt;
    if (_bullets[i].x < 0.0f || _bullets[i].x > (float)(tilemap::WORLD_W - _bullet_size) ||
        _bullets[i].y < 0.0f || _bullets[i].y > (float)(tilemap::WORLD_H - _bullet_size) ||
        tilemap::solid_rect((int16_t)_bullets[i].x, (int16_t)_bullets[i].y, _bullet_size, _bullet_size)) {
      _bullets[i].active = false;
      continue;
    }
    for (uint8_t z = 0; z < _max_zombies; ++z) {
      if (!_zombies[z].active) {
        continue;
      }
      const float zcx = _zombies[z].x + _zombie_size / 2.0f;
      const float zcy = _zombies[z].y + _zombie_size / 2.0f;
      const float hdx = _bullets[i].x - zcx;
      const float hdy = _bullets[i].y - zcy;
      if (hdx * hdx + hdy * hdy <= _hit_dist * _hit_dist) {
        _bullets[i].active = false;
        if (--_zombies[z].hp == 0) {
          _zombies[z].active = false;
          ++_kills;
          _score += _score_per_kill;
        }
        break;
      }
    }
  }

  for (uint8_t z = 0; z < _max_zombies; ++z) {
    if (!_zombies[z].active) {
      continue;
    }
    const float zcx = _zombies[z].x + _zombie_size / 2.0f;
    const float zcy = _zombies[z].y + _zombie_size / 2.0f;

    float ddx = pcx - zcx;
    float ddy = pcy - zcy;
    const float d = sqrtf(ddx * ddx + ddy * ddy);
    if (d > 0.5f) {
      ddx /= d;
      ddy /= d;
      _move_entity(_zombies[z].x, _zombies[z].y, ddx * _zombie_speed * dt, ddy * _zombie_speed * dt,
                   _zombie_size);
    }

    const float cdx = pcx - zcx;
    const float cdy = pcy - zcy;
    if (cdx * cdx + cdy * cdy <= _contact_dist * _contact_dist &&
        now - _last_damage >= _damage_cd_ms) {
      _last_damage = now;
      if (_player_hp > 0) {
        --_player_hp;
      }
    }
  }

  if (_player_hp == 0) {
    _enter_game_over();
    return;
  }

  bool any = false;
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    any |= _zombies[i].active;
  }
  if (!any) {
    _spawn_wave();
  }
}

void game::_render_clear() {
  _erase_world_rect((int16_t)_player.x, (int16_t)_player.y, _player_size);
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    if (_zombies[i].active) {
      _erase_world_rect((int16_t)_zombies[i].x, (int16_t)_zombies[i].y, _zombie_size);
    }
  }
  for (uint8_t i = 0; i < _max_bullets; ++i) {
    if (_bullets[i].active) {
      _erase_world_rect((int16_t)_bullets[i].x, (int16_t)_bullets[i].y, _bullet_size);
    }
  }
}

void game::_render_draw() {
  char buf[32];
  _paint_step(); // terrain first, so a cut never paints over a live sprite

  snprintf(buf, sizeof(buf), "SCORE %lu", _score);
  display::text(buf, 4, 1, colour::yellow, 1);
  display::text(_handler.role() == ROLE_HOST ? "HOST" : "CLIENT", (int16_t)(display::width() - 34), 1,
                colour::cyan, 1);

  _fill_world_box((int16_t)_player.x, (int16_t)_player.y, _player_size, colour::blue);
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    if (_zombies[i].active) {
      _fill_world_box((int16_t)_zombies[i].x, (int16_t)_zombies[i].y, _zombie_size, colour::red);
    }
  }
  for (uint8_t i = 0; i < _max_bullets; ++i) {
    if (_bullets[i].active) {
      _fill_world_box((int16_t)_bullets[i].x, (int16_t)_bullets[i].y, _bullet_size, colour::white);
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
  char buf[32];

  snprintf(buf, sizeof(buf), "BEST %lu", _best);
  display::text(buf, 4, _arena_bottom + 4, colour::white, 1);
  snprintf(buf, sizeof(buf), "WAVE %u", _wave);
  display::text(buf, 4, _arena_bottom + 16, colour::white, 1);
  snprintf(buf, sizeof(buf), "KILLS %u", _kills);
  display::text(buf, 4, _arena_bottom + 28, colour::white, 1);

  display::text("HP", 4, _arena_bottom + 40, colour::white, 1);
  const uint16_t live = (_player_hp <= 2) ? colour::red : colour::green;
  const uint16_t spent = display::rgb565(40, 40, 40);
  for (uint8_t i = 0; i < _player_hp_max; ++i) {
    display::fill_rect(30 + (int16_t)i * 10, _arena_bottom + 40, 8, 8, (i < _player_hp) ? live : spent);
  }
}

void game::_mm_dot(int16_t wx, int16_t wy, uint16_t col) {
  if (_mm_n >= (uint8_t)(1 + _max_zombies)) {
    return;
  }
  display::fill_rect(_mm_x() + (wx / tilemap::TILE) * _mm_scale, _mm_y + (wy / tilemap::TILE) * _mm_scale,
                     _mm_scale, _mm_scale, col);
  _mm_px[_mm_n] = wx;
  _mm_py[_mm_n] = wy;
  ++_mm_n;
}

void game::_minimap_blips() {
  for (uint8_t i = 0; i < _mm_n; ++i) { // restore the terrain under last frame's dots
    const int16_t tx = _mm_px[i] / tilemap::TILE;
    const int16_t ty = _mm_py[i] / tilemap::TILE;
    display::fill_rect(_mm_x() + tx * _mm_scale, _mm_y + ty * _mm_scale, _mm_scale, _mm_scale,
                       tilemap::color_at(_mm_px[i], _mm_py[i]));
  }
  _mm_n = 0;

  _mm_dot((int16_t)_player.x, (int16_t)_player.y, colour::white);
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    if (_zombies[i].active) {
      _mm_dot((int16_t)_zombies[i].x, (int16_t)_zombies[i].y, colour::red);
    }
  }
}
