#pragma once

#include <cstdint>

#include <Protocol.h>

// Game orchestration. Owns the pieces (network, screen) and drives the loop.
// main.cpp only calls begin()/update(); all game logic lives here or in
// modules it spawns (simulation, input, rendering).
class game {
public:
  static bool begin(uint8_t role);
  static void update();
  static uint8_t role();

private:
  static message_handler _net;
  static uint32_t _tick;
  static uint32_t _peer_tick;

  static void _on_heartbeat(const uint8_t* data, size_t len);
  static void _render();
};