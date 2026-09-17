#pragma once

#include <cstdint>

#include <Handler.h>
#include <Input.h>

class game {
public:
  static bool begin(uint8_t role);
  static void update();
  static uint8_t role();

private:
  static handler _handler;
  static uint32_t _tick;
  static uint32_t _peer_tick;

  static void _on_heartbeat(const uint8_t* data, size_t len);
  static void _render();
};
