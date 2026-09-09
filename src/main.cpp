#include <Arduino.h>
#include <cstring>

#include "config.h"
#include <Protocol.h>

message_handler net;

uint32_t tick = 0;

void on_heartbeat(const uint8_t* data, size_t len) {
  if (len < sizeof(heartbeat_msg)) {
    return;
  }

  heartbeat_msg hb;
  memcpy(&hb, data, len);
  Serial.printf("[net] heartbeat from peer: tick=%lu role=%u\n", hb.tick, hb.role);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!net.begin(DEVICE_ROLE)) {
    Serial.println("[net] failed to start network, resetting...");
    delay(1000);
    ESP.restart();
  }

  net.on_message(msg_type::heartbeat, on_heartbeat);
}

void loop() {
  heartbeat_msg hb;
  hb.tick = tick++;
  hb.role = net.role();

  net.send(&hb, sizeof(hb));
  delay(33); // ~30 fps
}
