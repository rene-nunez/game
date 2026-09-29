#include <Arduino.h>
#include <cmath>

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <Display.h>
#include <map.h>
#include <Panel.h>
#include <Render.h>
#include <Screens.h>
#include <Scores.h>
#include <Sim.h>

#include "Game.h"
#include "pins.h"

handler game::_handler;
uint32_t game::_tick = 0;
uint32_t game::_peer_tick = 0;

int16_t game::_shop_hx = -1;
int16_t game::_shop_hy = -1;
int16_t game::_shop_dx = -1;
int16_t game::_shop_dy = -1;
int16_t game::_shop_sx = -1;
int16_t game::_shop_sy = -1;
int16_t game::_shop_rx = -1;
int16_t game::_shop_ry = -1;
char game::_hint_buf[28] = {0};
uint32_t game::_hint_until = 0;

uint32_t game::_last_frame_ms = 0;

screens::id game::_scr = screens::id::menu;
uint8_t game::_sel = 0;
int8_t game::_nav_dir = 0;

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

  scores::load();

  _scr = screens::id::menu;
  _sel = 0;
  _nav_dir = 0;
  _last_frame_ms = millis();

  screens::paint(_scr, _sel);

  Serial.println("[game] ready");
  return true;
}

void game::update() {
  input::update();

  switch (_scr) {
    case screens::id::menu: _update_menu(); break;
    case screens::id::mode: _update_mode(); break;
    case screens::id::scores: _update_scores(); break;
    case screens::id::playing: _update_playing(); break;
    case screens::id::pause: _update_pause(); break;
    case screens::id::game_over: _update_game_over(); break;
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

void game::_start_game() {
  sim::reset();
  _scan_shops();
  _hint_until = 0;
  _hint_buf[0] = '\0';
  panel::init(); // static panel + minimap terrain, then blips on top
  render::update_camera();
  render::repaint(); // forced: the game over screen cleared the arena and the camera may not move
  _scr = screens::id::playing;
  screens::invalidate(); // the next pause must repaint its chrome
}

void game::_enter_menu() {
  _scr = screens::id::menu;
  _sel = 0;
  _nav_dir = 0;
}

void game::_enter_game_over() {
  const sim::state& v = sim::view();
  scores::add_run(v.kills, v.peak); // best tracks the max wallet, spending never lowers it
  _scr = screens::id::game_over;
  _sel = 0;
}

void game::_scan_shops() {
  _shop_hx = _shop_hy = -1;
  _shop_dx = _shop_dy = -1;
  _shop_sx = _shop_sy = -1;
  _shop_rx = _shop_ry = -1;
  for (uint8_t r = 0; r < tilemap::ROWS; ++r) {
    for (uint8_t c = 0; c < tilemap::COLS; ++c) {
      const uint8_t t = tilemap::tiles[r][c];
      int16_t* ox = nullptr;
      int16_t* oy = nullptr;
      switch (t) {
        case tilemap::VENDING: ox = &_shop_hx; oy = &_shop_hy; break;
        case tilemap::V_DMG: ox = &_shop_dx; oy = &_shop_dy; break;
        case tilemap::V_SPD: ox = &_shop_sx; oy = &_shop_sy; break;
        case tilemap::ROULETTE: ox = &_shop_rx; oy = &_shop_ry; break;
        default: break;
      }
      // first (top-left) tile of the 2x2 block wins, so the centre is one tile in
      if (ox != nullptr && *ox < 0) {
        *ox = (int16_t)(((uint16_t)c + 1u) * tilemap::TILE);
        *oy = (int16_t)(((uint16_t)r + 1u) * tilemap::TILE);
      }
    }
  }
}

void game::_shop_update(uint32_t now) {
  const sim::state& v = sim::view();
  const float pcx = v.player.x + sim::PLAYER_SIZE / 2.0f;
  const float pcy = v.player.y + sim::PLAYER_SIZE / 2.0f;
  const float r2 = (float)(_shop_r * _shop_r);
  auto near = [&](int16_t sx, int16_t sy) -> bool {
    if (sx < 0) {
      return false;
    }
    const float dx = pcx - (float)sx;
    const float dy = pcy - (float)sy;
    return dx * dx + dy * dy <= r2;
  };
  uint8_t shop = 0; // 1 heal, 2 damage, 3 speed, 4 roulette
  if (near(_shop_hx, _shop_hy)) {
    shop = 1;
  } else if (near(_shop_dx, _shop_dy)) {
    shop = 2;
  } else if (near(_shop_sx, _shop_sy)) {
    shop = 3;
  } else if (near(_shop_rx, _shop_ry)) {
    shop = 4;
  }

  if (input::interact_pressed() && shop != 0) {
    bool ok = false;
    switch (shop) {
      case 1:
        ok = sim::buy_heal(now);
        if (ok) {
          snprintf(_hint_buf, sizeof(_hint_buf), "HEALED +2HP");
        } else {
          snprintf(_hint_buf, sizeof(_hint_buf),
                   v.player_hp >= sim::PLAYER_HP_MAX ? "HP FULL" : "NEED %lu",
                   (unsigned long)sim::PRICE_HEAL);
        }
        break;
      case 2:
        ok = sim::buy_damage(now);
        snprintf(_hint_buf, sizeof(_hint_buf), ok ? "DMG x2 60s!" : "NEED %lu",
                 (unsigned long)sim::PRICE_DMG);
        break;
      case 3:
        ok = sim::buy_speed(now);
        snprintf(_hint_buf, sizeof(_hint_buf), ok ? "SPEED UP 30s!" : "NEED %lu",
                 (unsigned long)sim::PRICE_SPD);
        break;
      default:
        ok = sim::roll_roulette(now);
        if (ok) {
          snprintf(_hint_buf, sizeof(_hint_buf), "GUN: %s", sim::gun_name());
        } else {
          snprintf(_hint_buf, sizeof(_hint_buf), "NEED %lu", (unsigned long)sim::PRICE_ROLL);
        }
        break;
    }
    _hint_until = now + 1500;
  }

  if (now < _hint_until && _hint_buf[0] != '\0') {
    panel::hint(_hint_buf); // recent result wins over the prompt
    return;
  }
  switch (shop) {
    case 1: panel::hint("E: HEAL +2HP 100"); break;
    case 2: panel::hint("E: DMG x2 150"); break;
    case 3: panel::hint("E: SPD UP 120"); break;
    case 4: panel::hint("E: ROLL 100"); break;
    default: panel::hint(nullptr); break;
  }
}

void game::_sleep() {
  display::backlight(false);

  rtc_gpio_pullup_en((gpio_num_t)BTN_PAUSE);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PAUSE, 0); // LOW = pressed

  Serial.println("[game] sleeping...");
  Serial.flush();
  esp_deep_sleep_start();
}

// the wrap reads the count from the screen itself, so adding an item cannot leave a
// hardcoded modulus behind
void game::_nav_step() {
  const int8_t e = _nav_edge();
  const uint8_t n = screens::count(_scr);
  if (e && n > 0) {
    _sel = (uint8_t)((_sel + n + e) % n);
  }
}

void game::_update_menu() {
  _nav_step();
  if (input::fire_pressed()) {
    switch (_sel) {
      case 0:
        _scr = screens::id::mode;
        _sel = 0;
        break;
      case 1:
        _scr = screens::id::scores;
        _sel = 0;
        break;
      default: // Exit
        _sleep();
        break;
    }
  }
  screens::paint(_scr, _sel);
}

void game::_update_mode() {
  _nav_step();
  if (input::fire_pressed()) {
    if (_sel == 2) { // Back
      _enter_menu();
    } else {
      _start_game(); // Solo / Multiplayer (net-handshake comes in P3)
    }
  }
  screens::paint(_scr, _sel);
}

void game::_update_scores() {
  if (input::fire_pressed() || input::pause_pressed()) {
    _enter_menu();
  }
  screens::paint(_scr, _sel);
}

void game::_update_playing() {
  heartbeat_msg hb;
  hb.tick = _tick++;
  hb.role = _handler.role();
  _handler.send(&hb, sizeof(hb));

  if (input::pause_pressed()) {
    _scr = screens::id::pause;
    _sel = 0;
    return;
  }

  // the order matters: the clear-before-sim is what erases entities that die mid-frame
  render::clear();
  const uint32_t now = millis();
  if (!sim::step(now)) {
    _enter_game_over(); // sim reports the death, the screen change belongs here
    return;
  }
  _shop_update(now); // INTERACT buys + prompt, before the panel paints it
  render::update_camera();
  render::draw();
  panel::draw();
  panel::blips();
  // the role badge is net state, not sim state, so it does not belong to the renderer
  display::text(_handler.role() == ROLE_HOST ? "HOST" : "CLIENT", (int16_t)(display::width() - 34), 1,
                colour::cyan, 1);
}

void game::_update_pause() {
  _nav_step();
  if (input::pause_pressed()) {
    _scr = screens::id::playing;
    screens::invalidate(); // the next pause must repaint its chrome
    panel::init(); // the pause menu covered the panel and the minimap
    render::repaint(); // clear leftover pause menu
  } else if (input::fire_pressed()) {
    switch (_sel) {
      case 0: // Continue
        _scr = screens::id::playing;
        screens::invalidate(); // the next pause must repaint its chrome
        panel::init(); // the pause menu covered the panel and the minimap
        render::repaint(); // clear leftover pause menu
        break;
      case 1: // Restart
        _start_game();
        break;
      default: // Exit to Menu
        _enter_menu();
        break;
    }
  }
  screens::paint(_scr, _sel);
}

void game::_update_game_over() {
  _nav_step();
  if (input::fire_pressed()) {
    if (_sel == 0) {
      _start_game();
    } else {
      _enter_menu();
    }
  }
  screens::paint(_scr, _sel);
}
