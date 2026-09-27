#include <Arduino.h>
#include <cmath>
#include <cstring>

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <Display.h>
#include <map.h>
#include <Panel.h>
#include <Render.h>
#include <Scores.h>
#include <Sim.h>

#include "Game.h"
#include "pins.h"

handler game::_handler;
uint32_t game::_tick = 0;
uint32_t game::_peer_tick = 0;

uint8_t game::_menu_scr = 0xFF; // no menu painted yet
uint8_t game::_menu_sel = 0;

uint32_t game::_last_frame_ms = 0;

game::_screens game::_scr = game::_screens::menu;
uint8_t game::_sel = 0;
int8_t game::_nav_dir = 0;

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

  scores::load();

  _scr = _screens::menu;
  _sel = 0;
  _nav_dir = 0;
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
  snprintf(buf, sizeof(buf), "Best score: %lu", scores::best());
  display::text(buf, 16, 60, colour::white, 1);
  snprintf(buf, sizeof(buf), "Total kills: %lu", scores::total_kills());
  display::text(buf, 16, 76, colour::white, 1);

  display::text("FIRE/PAUSE: back", 16, 120, colour::white, 1);
}

void game::_start_game() {
  sim::reset();
  panel::init(); // static panel + minimap terrain, then blips on top
  render::update_camera();
  render::repaint(); // forced: the game over screen cleared the arena and the camera may not move
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
  scores::add_run(v.kills, v.score);
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
  render::clear();
  if (!sim::step(millis())) {
    _enter_game_over(); // sim reports the death, the screen change belongs here
    return;
  }
  render::update_camera();
  render::draw();
  panel::draw();
  panel::blips();
  // the role badge is net state, not sim state, so it does not belong to the renderer
  display::text(_handler.role() == ROLE_HOST ? "HOST" : "CLIENT", (int16_t)(display::width() - 34), 1,
                colour::cyan, 1);
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
    panel::init(); // the pause menu covered the panel and the minimap
    render::repaint(); // clear leftover pause menu
  } else if (input::fire_pressed()) {
    switch (_sel) {
      case 0: // Continue
        _scr = _screens::playing;
        _menu_invalidate(); // the next pause must repaint its chrome
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
  snprintf(buf, sizeof(buf), "Score: %lu   Best: %lu", v.score, scores::best());
  display::text(buf, 24, 60, colour::white, 1);
  snprintf(buf, sizeof(buf), "Wave: %u  Kills: %u", v.wave, v.kills);
  display::text(buf, 24, 76, colour::white, 1);

  for (uint8_t i = 0; i < 2; ++i) {
    _menu_item(_over_items, i, i == _sel, _over_x, _over_y + (int16_t)i * _menu_row);
  }

  display::text("JOY: move   FIRE: select", _over_x, _over_y + 2 * _menu_row + 24, colour::white, 1);
}

