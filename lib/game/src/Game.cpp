#include <Arduino.h>
#include <cstring>

#include <Display.h>
#include "Game.h"

handler game::_handler;
uint32_t game::_tick = 0;
uint32_t game::_peer_tick = 0;

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

  Serial.println("[game] ready");
  return true;
}

void game::update() {
  input::update();

  heartbeat_msg hb;
  hb.tick = _tick++;
  hb.role = role();
  _handler.send(&hb, sizeof(hb));

  _render();
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

void game::_render() {
  display::text(role() == ROLE_HOST ? "role: host" : "role: client", 8, 8, colour::yellow, 2);

  char buf[16];

  display::fill_rect(8, 36, 200, 14, colour::black);
  display::text("peer:", 8, 36, colour::green, 2);
  snprintf(buf, sizeof(buf), "%lu", _peer_tick);
  display::text(buf, 60, 36, colour::green, 2);

  snprintf(buf, sizeof(buf), "tx: %lu", _tick);
  display::text(buf, 8, 56, colour::blue, 1);

  display::fill_rect(8, 88, 160, 50, colour::black);

  display::fill_rect(8, 88, 30, 30, colour::black);
  display::fill_rect(8 + 14, 88, 2, 30, colour::blue);
  display::fill_rect(8, 88 + 14, 30, 2, colour::blue);

  const float dx = input::jx();
  const float dy = input::jy();
  const int8_t ox = (int8_t)(dx * 12.0f);
  const int8_t oy = (int8_t)(dy * 12.0f);
  const uint16_t dot = (ox == 0 && oy == 0) ? colour::white : colour::yellow;
  display::fill_rect(8 + 14 + ox, 88 + 14 + oy, 3, 3, dot);

  snprintf(buf, sizeof(buf), "jx:%+.0f", dx * 100);
  display::text(buf, 42, 90, colour::white, 1);
  snprintf(buf, sizeof(buf), "jy:%+.0f", dy * 100);
  display::text(buf, 42, 100, colour::white, 1);

  display::fill_rect(8, 124, 10, 10, input::fire_down() ? colour::red : colour::black);
  display::text("F", 10, 124, input::fire_down() ? colour::white : colour::blue, 1);

  display::fill_rect(24, 124, 10, 10, input::interact_down() ? colour::green : colour::black);
  display::text("E", 26, 124, input::interact_down() ? colour::white : colour::green, 1);
}
