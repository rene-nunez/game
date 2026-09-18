#include <Arduino.h>
#include <cmath>
#include <cstring>

#include <Display.h>
#include "Game.h"

handler game::_handler;
uint32_t game::_tick = 0;
uint32_t game::_peer_tick = 0;

uint16_t game::_grass = 0;
game::_player_data game::_player;
uint32_t game::_last_ms = 0;

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

  const uint16_t iw = display::width();
  const uint16_t ih = display::height();

  _grass = display::rgb565(38, 62, 38);
  _player = {
      (float)(iw - _player_size) / 2.0f,
      (float)(_hud_h + ih - _player_size) / 2.0f,
  };
  _last_ms = millis();

  display::fill_rect(0, 0, iw, _hud_h, colour::black); // hud strip
  display::fill_rect(0, _hud_h, iw, ih - _hud_h, _grass); // arena

  Serial.println("[game] ready");
  return true;
}

void game::update() {
  input::update();

  heartbeat_msg hb;
  hb.tick = _tick++;
  hb.role = role();
  _handler.send(&hb, sizeof(hb));

  const uint32_t now = millis();
  const float dt = (float)(now - _last_ms) / 1000.0f;
  _last_ms = now;

  _render_clear();
  _sim(dt);
  _render_draw();

  delay(33); // ~30 fps
}

uint8_t game::role() {
  return _handler.role();
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

void game::_sim(float dt) {
  float dx = input::jx();
  float dy = input::jy();

  const float len = sqrtf(dx * dx + dy * dy);
  if (len > 1.0f) { // keep diagonal speed equal
    dx /= len;
    dy /= len;
  }

  _player.x += dx * _player_speed * dt;
  _player.y += dy * _player_speed * dt;

  _player.x = constrain(_player.x, 0.0f, (float)(display::width() - _player_size));
  _player.y = constrain(_player.y, (float)_hud_h, (float)(display::height() - _player_size));
}

void game::_render_clear() {
  display::fill_rect((int16_t)_player.x, (int16_t)_player.y, _player_size, _player_size, _grass);
}

void game::_render_draw() {
  display::text(role() == ROLE_HOST ? "role: host" : "role: client", 4, 1, colour::yellow, 1);
  display::fill_rect((int16_t)_player.x, (int16_t)_player.y, _player_size, _player_size, colour::blue);
}
