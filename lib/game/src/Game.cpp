#include <Arduino.h>
#include <cmath>

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <Buzz.h>
#include <Display.h>
#include <map.h>
#include <Panel.h>
#include <Render.h>
#include <Screens.h>
#include <Points.h>
#include <Sim.h>

#include "Game.h"
#include "pins.h"

handler game::_handler;
uint32_t game::_tick = 0;
uint32_t game::_peer_tick = 0;

bool game::_net_multi = false;
uint32_t game::_wait_since = 0;
uint32_t game::_wait_last_hb = 0;
bool game::_peer_seen = false;
uint16_t game::_seq_out = 0;
uint8_t game::_in_seq = 0;
uint8_t game::_in_buttons = 0;
uint8_t game::_in_prev = 0;
float game::_in_jx = 0.0f;
float game::_in_jy = 0.0f;
uint32_t game::_in_last_ms = 0;
volatile bool game::_rx_ready = false;
bool game::_cli_mirror = false;
net::game_state_msg game::_rx_state;
net::game_state_msg game::_tx_state;
uint32_t game::_cli_last_rx = 0;

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
bool game::_p2_interact = false;
bool game::_p2_pause_edge = false;

uint32_t game::_last_frame_ms = 0;
uint32_t game::_intro_ms0 = 0;

screens::id game::_scr = screens::id::logo;
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
  _handler.on_message(msg_type::game_state, _on_state);
  _handler.on_message(msg_type::player_input, _on_input);

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

  points::load();

  if (!buzz::begin()) {
    Serial.println("[game] buzz init failed, silent mode");
  }

  _scr = screens::id::logo;
  _sel = 0;
  _nav_dir = 0;
  _intro_ms0 = millis();
  _last_frame_ms = _intro_ms0;

  screens::paint(_scr, _sel);

  Serial.println("[game] ready");
  return true;
}

void game::update() {
  input::update();
  buzz::update(millis()); // the note sequencer runs on every screen

  switch (_scr) {
    case screens::id::logo: _update_logo(); break;
    case screens::id::team: _update_team(); break;
    case screens::id::menu: _update_menu(); break;
    case screens::id::mode: _update_mode(); break;
    case screens::id::points: _update_points(); break;
    case screens::id::playing: _update_playing(); break;
    case screens::id::pause: _update_pause(); break;
    case screens::id::game_over: _update_game_over(); break;
    case screens::id::waiting: _update_waiting(); break;
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
  // the host starts a co-op run off heartbeats; the client waits for a snapshot
  // (live map) instead, so heartbeats never flip a waiting client into playing.
  if (_handler.role() == ROLE_HOST) {
    _peer_seen = true; // any peer heartbeat counts while waiting (consumed there)
  }
}

void game::_on_state(const uint8_t* data, size_t len) {
  if (_handler.role() != ROLE_CLIENT) {
    return; // host never applies snapshots
  }
  if (len < sizeof(net::game_state_msg)) {
    return;
  }
  memcpy(&_rx_state, data, sizeof(net::game_state_msg));
  _rx_ready = true;
  _peer_seen = true; // snapshots also join a waiting client, enabling late join
}

void game::_on_input(const uint8_t* data, size_t len) {
  if (_handler.role() != ROLE_HOST) {
    return; // client never applies inputs
  }
  if (len < sizeof(net::player_input_msg)) {
    return;
  }
  net::player_input_msg in;
  memcpy(&in, data, sizeof(in));
  _in_jx = net::uqaxis(in.jx);
  _in_jy = net::uqaxis(in.jy);
  _in_buttons = in.buttons;
  _in_seq = in.seq;
  _in_last_ms = millis();
}

int8_t game::_nav_edge() {
  const int8_t cur = input::jy() > 0.5f ? 1 : (input::jy() < -0.5f ? -1 : 0);
  const int8_t edge = (cur != 0 && _nav_dir == 0) ? cur : 0;
  _nav_dir = cur;
  return edge;
}

void game::_start_game(bool multi) {
  _net_multi = multi;
  // solo always frames player 1; multi frames the local board (host P1, client P2)
  render::set_focus((multi && _handler.role() == ROLE_CLIENT) ? 1 : 0); // each board frames its own
  _seq_out = 0;
  _in_prev = _in_buttons; // hold levels so a held PAUSE/FIRE does not phantom-edge on entry
  _in_jx = _in_jy = 0.0f;
  _rx_ready = false;
  _cli_mirror = false; // a fresh run owns the arena again, never the pause chrome
  _cli_last_rx = millis(); // grace window so a fresh client is not instantly "quiet"
  _p2_interact = _p2_pause_edge = false;
  sim::reset();
  if (multi && _handler.role() == ROLE_HOST) {
    sim::set_p2_active(true); // the client never steps, it follows snapshots
  }
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
  // the host owns the wallet and the SD card; a client only mirrors the screen
  if (!_net_multi || _handler.role() == ROLE_HOST) {
    points::add_run(v.kills, v.points, v.wave); // fold the run: wallet at death, kills, wave
  }
  _scr = screens::id::game_over;
  _sel = 0;
}

void game::_broadcast() {
  _tx_state.type = net::TYPE_STATE;
  _tx_state.seq = _seq_out++;
  sim::snapshot(_tx_state);
  // the menu mirror rides the snapshot: the host owns pause/over chrome, the client paints it
  if (_scr == screens::id::pause) {
    _tx_state.screen = net::SCREEN_PAUSE;
  } else if (_scr == screens::id::game_over) {
    _tx_state.screen = net::SCREEN_OVER;
  } else {
    _tx_state.screen = net::SCREEN_PLAYING;
  }
  _tx_state.sel = _sel;
  _handler.send(&_tx_state, sizeof(_tx_state));
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

uint8_t game::_shop_at(uint8_t p) {
  const sim::state& v = sim::view();
  if (p >= sim::NUM_PLAYERS || !v.players[p].active) {
    return 0;
  }
  const float pcx = v.players[p].x + sim::PLAYER_SIZE / 2.0f;
  const float pcy = v.players[p].y + sim::PLAYER_SIZE / 2.0f;
  const float r2 = (float)(_shop_r * _shop_r);
  auto near = [&](int16_t sx, int16_t sy) -> bool {
    if (sx < 0) {
      return false;
    }
    const float dx = pcx - (float)sx;
    const float dy = pcy - (float)sy;
    return dx * dx + dy * dy <= r2;
  };
  if (near(_shop_hx, _shop_hy)) {
    return 1;
  } else if (near(_shop_dx, _shop_dy)) {
    return 2;
  } else if (near(_shop_sx, _shop_sy)) {
    return 3;
  } else if (near(_shop_rx, _shop_ry)) {
    return 4;
  }
  return 0;
}

bool game::_revive_near(uint8_t p) {
  const sim::state& v = sim::view();
  if (p >= sim::NUM_PLAYERS || !v.players[p].active || v.players[p].hp == 0) {
    return false; // only the standing can lift
  }
  const uint8_t q = (p == 0) ? 1 : 0;
  if (!v.players[q].active || !v.players[q].downed) {
    return false;
  }
  const float dx = (v.players[p].x - v.players[q].x);
  const float dy = (v.players[p].y - v.players[q].y);
  return dx * dx + dy * dy <= (float)(_revive_r * _revive_r);
}

bool game::_revive_update(uint32_t now) {
  const bool edge[2] = {input::interact_pressed(), _p2_interact};
  for (uint8_t p = 0; p < sim::NUM_PLAYERS; ++p) {
    if (!edge[p] || !_revive_near(p)) {
      continue;
    }
    const uint8_t q = (p == 0) ? 1 : 0;
    if (sim::revive(q)) {
      snprintf(_hint_buf, sizeof(_hint_buf), p == 0 ? "REVIVED!" : "P2 REVIVED!");
      _hint_until = now + 1500;
      if (p == 1) {
        _p2_interact = false; // consumed: no accidental buy next frame
      }
      return true;
    }
  }
  return false;
}

void game::_shop_update(uint32_t now) {
  const sim::state& v = sim::view();
  const uint8_t shop = _shop_at(0);
  const bool p2_out = v.players[1].active && v.players[1].hp > 0;
  const uint8_t shop2 = p2_out ? _shop_at(1) : 0;

  if (input::interact_pressed() && shop != 0) {
    bool ok = false;
    switch (shop) {
      case 1:
        ok = sim::buy_heal(now);
        if (ok) {
          snprintf(_hint_buf, sizeof(_hint_buf), "HEALED +2HP");
        } else {
          snprintf(_hint_buf, sizeof(_hint_buf),
                   v.players[0].hp >= sim::PLAYER_HP_MAX ? "HP FULL" : "NEED %lu",
                   (unsigned long)sim::PRICE_HEAL);
        }
        break;
      case 2:
        if (v.dmg_lvl >= sim::MAX_LVL) {
          snprintf(_hint_buf, sizeof(_hint_buf), "DMG MAX");
        } else {
          ok = sim::buy_damage(now);
          if (ok) {
            snprintf(_hint_buf, sizeof(_hint_buf), "DMG LV%u!", sim::view().dmg_lvl);
          } else {
            snprintf(_hint_buf, sizeof(_hint_buf), "NEED %lu",
                     (unsigned long)sim::price_for(sim::PRICE_DMG, v.dmg_lvl));
          }
        }
        break;
      case 3:
        if (v.spd_lvl >= sim::MAX_LVL) {
          snprintf(_hint_buf, sizeof(_hint_buf), "SPD MAX");
        } else {
          ok = sim::buy_speed(now);
          if (ok) {
            snprintf(_hint_buf, sizeof(_hint_buf), "SPD LV%u!", sim::view().spd_lvl);
          } else {
            snprintf(_hint_buf, sizeof(_hint_buf), "NEED %lu",
                     (unsigned long)sim::price_for(sim::PRICE_SPD, v.spd_lvl));
          }
        }
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

  // player 2 shops from the shared wallet on its own INTERACT edge (net, F6.3 sets it)
  if (_p2_interact && shop2 != 0) {
    bool ok = false;
    switch (shop2) {
      case 1:
        ok = sim::buy_heal(now, 1);
        snprintf(_hint_buf, sizeof(_hint_buf), ok ? "P2 HEALED" : "P2 NEED %lu",
                 (unsigned long)sim::PRICE_HEAL);
        break;
      case 2:
        ok = sim::buy_damage(now, 1);
        snprintf(_hint_buf, sizeof(_hint_buf), ok ? "P2 DMG LV%u!" : "P2 NEED %lu",
                 (unsigned)sim::view().dmg_lvl,
                 (unsigned long)sim::price_for(sim::PRICE_DMG, v.dmg_lvl));
        break;
      case 3:
        ok = sim::buy_speed(now, 1);
        snprintf(_hint_buf, sizeof(_hint_buf), ok ? "P2 SPD LV%u!" : "P2 NEED %lu",
                 (unsigned)sim::view().spd_lvl,
                 (unsigned long)sim::price_for(sim::PRICE_SPD, v.spd_lvl));
        break;
      default:
        ok = sim::roll_roulette(now, 1);
        if (ok) {
          snprintf(_hint_buf, sizeof(_hint_buf), "P2 GUN: %s", sim::gun_name());
        } else {
          snprintf(_hint_buf, sizeof(_hint_buf), "P2 NEED %lu", (unsigned long)sim::PRICE_ROLL);
        }
        break;
    }
    _hint_until = now + 1500;
  }
  _p2_interact = false; // consumed every frame, edge semantics

  if (now < _hint_until && _hint_buf[0] != '\0') {
    render::prompt(_hint_buf); // recent result wins over the prompt
    return;
  }
  if (_revive_near(0)) {
    render::prompt("INT: REVIVE");
    return;
  }
  if (_revive_near(1)) {
    render::prompt("P2 INT: REVIVE");
    return;
  }
  const uint8_t pshop = (shop != 0) ? shop : shop2; // P2 prompts only when P1 is away
  const char* who = (shop != 0) ? "" : "P2 ";
  _shop_prompt(pshop, who);
}

void game::_shop_prompt(uint8_t shop, const char* who) {
  const sim::state& v = sim::view();
  switch (shop) {
    case 1:
      snprintf(_hint_buf, sizeof(_hint_buf), "%sINT: HEAL +2HP", who);
      render::prompt(_hint_buf);
      break;
    case 2:
      if (v.dmg_lvl >= sim::MAX_LVL) {
        render::prompt("DMG MAX");
      } else {
        snprintf(_hint_buf, sizeof(_hint_buf), "%sINT: DMG LV%u", who, (unsigned)v.dmg_lvl + 1u);
        render::prompt(_hint_buf);
      }
      break;
    case 3:
      if (v.spd_lvl >= sim::MAX_LVL) {
        render::prompt("SPD MAX");
      } else {
        snprintf(_hint_buf, sizeof(_hint_buf), "%sINT: SPD LV%u", who, (unsigned)v.spd_lvl + 1u);
        render::prompt(_hint_buf);
      }
      break;
    case 4:
      snprintf(_hint_buf, sizeof(_hint_buf), "%sINT: ROLL", who);
      render::prompt(_hint_buf);
      break;
    default: render::prompt(nullptr); break;
  }
}

void game::_sleep() {
  buzz::stop();
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
    buzz::play(buzz::jingle::menu); // cursor tick on every menu move
  }
}

// one jingle per sim event, fired after step()+shop so buys win over same-frame shots
void game::_fire_buzz() {
  switch (sim::view().last_event) {
    case sim::event::shoot: buzz::play(buzz::jingle::shoot); break;
    case sim::event::buy_heal:
    case sim::event::buy_dmg:
    case sim::event::buy_spd: buzz::play(buzz::jingle::buy); break;
    case sim::event::revive: buzz::play(buzz::jingle::buy); break; // a lift, not a purchase
    case sim::event::roulette: buzz::play(buzz::jingle::roulette); break;
    case sim::event::denied: buzz::play(buzz::jingle::denied); break;
    case sim::event::hurt: buzz::play(buzz::jingle::hurt); break;
    case sim::event::wave: buzz::play(buzz::jingle::wave); break;
    case sim::event::over: buzz::play(buzz::jingle::over); break;
    default: break; // none: silence
  }
}

void game::_update_logo() {
  if (input::fire_pressed() || millis() - _intro_ms0 >= _intro_ms) {
    _scr = screens::id::team; // timed or skippable, then the team screen
    _sel = 0;
    _intro_ms0 = millis();
  }
  screens::paint(_scr, _sel);
}

void game::_update_team() {
  if (input::fire_pressed() || input::pause_pressed() || millis() - _intro_ms0 >= _intro_ms) {
    _enter_menu(); // timed like the logo, FIRE/PAUSE skip it
  }
  screens::paint(_scr, _sel);
}

void game::_update_menu() {
  _nav_step();
  if (input::fire_pressed()) {
    buzz::play(buzz::jingle::menu); // confirm blip (inaudible on Exit, it sleeps)
    switch (_sel) {
      case 0:
        _scr = screens::id::mode;
        _sel = 0;
        break;
      case 1:
        _scr = screens::id::points;
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
    buzz::play(buzz::jingle::menu);
    if (_sel == 2) { // Back
      _enter_menu();
    } else if (_sel == 0) {
      _start_game(false); // Solo: local run, the radio stays silent
    } else {
      _scr = screens::id::waiting; // Multiplayer: wait for the peer
      _sel = 0;
      _wait_since = millis();
      _wait_last_hb = 0;
      _peer_seen = false;
      _rx_ready = false;
    }
  }
  screens::paint(_scr, _sel);
}

void game::_update_waiting() {
  const uint32_t now = millis();
  if (now - _wait_last_hb >= _wait_hb_ms) {
    heartbeat_msg hb;
    hb.tick = _tick++;
    hb.role = _handler.role();
    _handler.send(&hb, sizeof(hb));
    _wait_last_hb = now;
  }
  if (input::pause_pressed()) {
    _enter_menu(); // bail out
    return;
  }
  if (input::fire_pressed()) {
    buzz::play(buzz::jingle::menu);
    _start_game(false); // tired of waiting: play solo
    return;
  }
  if (_handler.role() == ROLE_CLIENT) {
    // the client joins off a live snapshot (host map), never off a bare heartbeat,
    // so it lands straight into the running frame instead of an empty reset.
    if (_rx_ready) {
      const net::game_state_msg snap = _rx_state; // copy: _start_game clears the flag
      _rx_ready = false;
      _peer_seen = false;
      buzz::play(buzz::jingle::wave);
      _start_game(true);
      sim::apply_snapshot(snap); // start from the live frame, not the reset one
      _cli_last_rx = millis();
      return;
    }
  } else if (_peer_seen) {
    _peer_seen = false;
    buzz::play(buzz::jingle::wave);
    _start_game(true); // peer showed up (a client also joins mid-run off snapshots)
    return;
  }
  if (now - _wait_since >= _wait_ms) {
    buzz::play(buzz::jingle::denied); // nobody out there
    _enter_menu();
    return;
  }
  screens::paint(_scr, _sel);
}

void game::_update_points() {
  if (input::fire_pressed() || input::pause_pressed()) {
    if (input::fire_pressed()) {
      buzz::play(buzz::jingle::menu);
    }
    _enter_menu();
  }
  screens::paint(_scr, _sel);
}

void game::_update_playing() {
  // solo is always a local sim on both boards (silent radio); only multi + client mirrors.
  if (!_net_multi) {
    _update_playing_host();
    return;
  }
  if (_handler.role() == ROLE_CLIENT) {
    _update_playing_client();
    return;
  }
  _update_playing_host();
}

void game::_update_playing_host() {
  // fold the latest peer input into player 2 (edges from levels, neutral when stale)
  const uint32_t now = millis();
  sim::ctl c = {};
  if (_net_multi && now - _in_last_ms < _in_stale_ms) {
    c.jx = _in_jx;
    c.jy = _in_jy;
    c.fire = (_in_buttons & net::fire_bit) && !(_in_prev & net::fire_bit);
    _p2_interact = (_in_buttons & net::interact_bit) && !(_in_prev & net::interact_bit);
    _p2_pause_edge = (_in_buttons & net::pause_bit) && !(_in_prev & net::pause_bit);
    _in_prev = _in_buttons;
  } else {
    _in_prev = _in_buttons; // stale: hold levels so the next packet re-edges cleanly
  }
  c.pause = false; // pause travels via _p2_pause_edge, sim never sees it
  sim::set_p2(c);

  if (input::pause_pressed() || _p2_pause_edge) {
    _p2_pause_edge = false;
    _scr = screens::id::pause;
    _sel = 0;
    return;
  }
  _p2_pause_edge = false;

  // the order matters: the clear-before-sim is what erases entities that die mid-frame
  render::clear();
  if (!sim::step(now)) {
    buzz::play(buzz::jingle::over); // death jingle, then the screen change below
    _enter_game_over(); // sim reports the death, the screen change belongs here
    if (_net_multi) {
      _broadcast(); // the client mirrors game over off the over event
    }
    return;
  }
  if (_revive_update(now)) {
    render::prompt(_hint_buf); // lift result now, shop waits a frame
  } else {
    _shop_update(now); // INTERACT buys + prompt, before the panel paints it
  }
  if (sim::view().last_event == sim::event::wave && _boss_alive()) {
    // boss waves announce over the proximity prompt: buys still win, proximity waits
    snprintf(_hint_buf, sizeof(_hint_buf), "BOSS WAVE!");
    _hint_until = now + 2000;
    render::prompt(_hint_buf);
  }
  _fire_buzz(); // jingle for the frame's event (revive/buys already overwrote shots)
  render::update_camera();
  render::draw();
  panel::draw();
  panel::blips();
  _draw_hud(); // wave/kills + gun + role, every frame with clears (values shrink)
  if (_net_multi) {
    _broadcast(); // ~30Hz state to the client, right after the frame simmed
  }
}

void game::_update_playing_client() {
  _send_input();

  // route off the last snapshot before touching the frame: while the host-owned menu is
  // up, clear() would spray terrain erases over it, so mirror frames skip clear/draw.
  if (_rx_state.screen == net::SCREEN_PAUSE) {
    _mirror_pause(); // _scr stays playing so update() keeps routing here
    return;
  }
  if (_cli_mirror) {
    // first playing frame after the menu: its chrome covered the arena, so rebuild it
    // exactly like the host resume path (progressive terrain over the next 2 frames).
    _cli_mirror = false;
    screens::invalidate(); // the next pause must repaint its chrome
    panel::init();         // the pause menu covered the panel and the minimap
    render::repaint();     // clear leftover pause menu
  }

  // erase at the old frame, then step to the new one: same order as the host
  render::clear();
  if (_rx_ready) {
    sim::apply_snapshot(_rx_state);
    _rx_ready = false;
    _cli_last_rx = millis();
  }
  if (millis() - _cli_last_rx >= _cli_quiet_ms) {
    buzz::play(buzz::jingle::denied); // host went away (menu/sleep): drop to menu
    _net_multi = false;
    _enter_menu();
    return;
  }
  if (sim::view().last_event == sim::event::over) {
    _enter_game_over(); // host declared it, we only mirror (no SD write, see guard)
    return;
  }
  if (_revive_near(1)) {
    render::prompt("INT: REVIVE"); // local prompt off the snapshot, buys run on the host
  } else {
    _shop_prompt(_shop_at(1), "");
  }
  const uint32_t cli_now = millis();
  if (sim::view().last_event == sim::event::wave && _boss_alive()) {
    snprintf(_hint_buf, sizeof(_hint_buf), "BOSS WAVE!"); // 2s locally, like the host hint
    _hint_until = cli_now + 2000;
  }
  if (cli_now < _hint_until && _hint_buf[0] != '\0') {
    render::prompt(_hint_buf); // recent boss wave wins over the proximity prompt
  }
  _fire_buzz(); // the snapshot carries the event, so both buzzers sing
  render::update_camera();
  render::draw();
  panel::draw();
  panel::blips();
  _draw_hud();
}

bool game::_boss_alive() {
  const sim::state& v = sim::view();
  for (uint8_t i = 0; i < sim::MAX_ZOMBIES; ++i) {
    if (v.zombies[i].active && v.zombies[i].kind == sim::actor_kind::boss) {
      return true;
    }
  }
  return false;
}

void game::_draw_hud() {
  // the 10px strip is net+sim state, not renderer state, so game paints it: wave/kills
  // left, gun centred, role badge right. Every field is cleared first (K12 -> K9 and
  // SHOTGUN -> SMG shrink, overpainting alone would leave ghost digits behind).
  const sim::state& v = sim::view();
  const int16_t sw = (int16_t)display::width();
  char buf[24];

  display::fill_rect(0, 0, 76, 8, colour::black);
  snprintf(buf, sizeof(buf), "W%u K%u", v.wave, v.kills);
  display::text(buf, 4, 1, colour::white, 1);

  char gun[12];
  snprintf(gun, sizeof(gun), "GUN %s", sim::gun_name());
  uint8_t glen = 0;
  while (gun[glen] != '\0') {
    ++glen;
  }
  const int16_t gx = (sw - (int16_t)glen * 6) / 2;
  display::fill_rect(116, 0, 88, 8, colour::black);
  display::text(gun, gx < 0 ? 0 : gx, 1, colour::white, 1);

  const char* badge = !_net_multi ? "SOLO" : (_handler.role() == ROLE_HOST ? "HOST" : "CLIENT");
  uint8_t blen = 0;
  while (badge[blen] != '\0') {
    ++blen;
  }
  const int16_t bx = sw - 4 - (int16_t)blen * 6; // same 4px margin as the left field
  display::fill_rect(sw - 46, 0, 46, 8, colour::black);
  display::text(badge, bx < 0 ? 0 : bx, 1, colour::cyan, 1);
}

void game::_send_input() {
  // ship our sticks and buttons every frame; the host owns the sim
  net::player_input_msg in;
  in.jx = net::qaxis(input::jx());
  in.jy = net::qaxis(input::jy());
  in.buttons = (input::fire_down() ? net::fire_bit : 0) |
               (input::reload_down() ? net::reload_bit : 0) |
               (input::interact_down() ? net::interact_bit : 0) |
               (input::pause_down() ? net::pause_bit : 0);
  in.seq = (uint8_t)_seq_out++;
  _handler.send(&in, sizeof(in));
}

void game::_mirror_pause() {
  // Host-owned menu on the client: keep shipping inputs (PAUSE resumes from either
  // board), paint the host cursor, never step it locally. _scr stays playing so
  // update() keeps routing here; the chrome entry is tracked by screens::paint itself.
  // No buzz here: the frozen snapshot event would otherwise jingle every frame.
  _send_input();

  if (_rx_ready) {
    sim::apply_snapshot(_rx_state);
    _rx_ready = false;
    _cli_last_rx = millis();
  }
  if (millis() - _cli_last_rx >= _cli_quiet_ms) {
    _cli_mirror = false;
    _net_multi = false;
    _enter_menu(); // host left: drop to menu
    return;
  }
  if (_rx_state.screen == net::SCREEN_PLAYING) {
    // host resumed or restarted: rebuild the arena chrome like the host resume path
    _cli_mirror = false;
    screens::invalidate(); // the next pause must repaint its chrome
    panel::init();         // the pause menu covered the panel and the minimap
    render::repaint();     // clear leftover pause menu
    return;
  }
  if (_rx_state.screen == net::SCREEN_OVER) {
    _cli_mirror = false;
    _enter_game_over(); // host declared it, we only mirror (no SD write, see guard)
    return;
  }
  uint8_t s = _rx_state.sel;
  if (s >= screens::count(screens::id::pause)) {
    s = 0; // corrupt/clamped cursor never blanks the chrome (paint guards sel)
  }
  _cli_mirror = true; // the arena chrome is covered until the one-shot exit above
  screens::paint(screens::id::pause, s);
}

void game::_update_pause() {
  _nav_step();
  // the client keeps shipping inputs while frozen, so derive its PAUSE edge here too:
  // otherwise nobody converts levels to an edge while the host sits in pause and the
  // client can never resume.
  if (_net_multi && _handler.role() == ROLE_HOST) {
    const bool lvl = (_in_buttons & net::pause_bit) != 0;
    _p2_pause_edge = lvl && ((_in_prev & net::pause_bit) == 0);
    _in_prev = _in_buttons;
  }
  if (input::pause_pressed() || _p2_pause_edge) {
    _p2_pause_edge = false;
    _scr = screens::id::playing;
    screens::invalidate(); // the next pause must repaint its chrome
    panel::init(); // the pause menu covered the panel and the minimap
    render::repaint(); // clear leftover pause menu
  } else if (input::fire_pressed()) {
    buzz::play(buzz::jingle::menu);
    switch (_sel) {
      case 0: // Continue
        _scr = screens::id::playing;
        screens::invalidate(); // the next pause must repaint its chrome
        panel::init(); // the pause menu covered the panel and the minimap
        render::repaint(); // clear leftover pause menu
        break;
      case 1: // Restart
        _start_game(_net_multi); // a co-op pause restarts co-op
        break;
      default: // Exit to Menu
        _enter_menu();
        break;
    }
  }
  screens::paint(_scr, _sel);
  if (_net_multi && _handler.role() == ROLE_HOST) {
    _broadcast(); // frozen sim keeps flowing so the client holds the frame
  }
}

void game::_update_game_over() {
  _nav_step();
  // solo on either board is a local game over; only a multi client mirrors the host.
  if (_net_multi && _handler.role() == ROLE_CLIENT) {
    // mirror only: the host owns restart, we follow its snapshots back to playing
    if (_rx_ready) {
      sim::apply_snapshot(_rx_state);
      _rx_ready = false;
      _cli_last_rx = millis();
      bool anyone = false;
      for (uint8_t p = 0; p < sim::NUM_PLAYERS; ++p) {
        anyone |= sim::view().players[p].active && sim::view().players[p].hp > 0;
      }
      if (anyone) {
        _start_game(true); // host restarted: re-init chrome, snapshots fill the sim
        return;
      }
    }
    if (input::fire_pressed()) {
      buzz::play(buzz::jingle::menu); // feedback only, the host drives restart
    }
    if (millis() - _cli_last_rx >= _cli_quiet_ms) {
      _net_multi = false;
      _enter_menu(); // host left: drop to menu
      return;
    }
    screens::paint(_scr, _sel);
    return;
  }
  if (input::fire_pressed()) {
    buzz::play(buzz::jingle::menu);
    if (_sel == 0) {
      _start_game(_net_multi);
    } else {
      _enter_menu();
    }
  }
  screens::paint(_scr, _sel);
  if (_net_multi) {
    _broadcast(); // the over event keeps flowing until the host restarts or exits
  }
}
