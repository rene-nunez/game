#include <Arduino.h>
#include <cstring>

#include <Display.h>
#include <Game.h>

message_handler game::_net;
uint32_t game::_tick = 0;
uint32_t game::_peer_tick = 0;

bool game::begin(uint8_t role) {
  Serial.begin(115200);
  delay(200);

  if (!_net.begin(role)) {
    Serial.println("[game] network init failed, resetting...");
    delay(1000);
    ESP.restart();
    return false;
  }
  _net.on_message(msg_type::heartbeat, _on_heartbeat);

  if (!display::begin()) {
    Serial.println("[game] display init failed, resetting...");
    delay(1000);
    ESP.restart();
    return false;
  }

  Serial.println("[game] ready");
  return true;
}

void game::update() {
  heartbeat_msg hb;
  hb.tick = _tick++;
  hb.role = role();
  _net.send(&hb, sizeof(hb));

  _render();
  delay(33);  // ~30 fps
}

uint8_t game::role() {
  return _net.role();
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
  display::text(role() == ROLE_HOST ? "role: host" : "role: client",
                8, 8, colour::yellow, 2);
  display::text("peer:", 8, 36, colour::green, 2);

  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", _peer_tick);
  display::text(buf, 90, 36, colour::green, 2);

  snprintf(buf, sizeof(buf), "tx: %lu", _tick);
  display::text(buf, 8, 60, colour::blue, 1);
}
