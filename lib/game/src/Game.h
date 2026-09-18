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
    static constexpr float _player_speed = 110.0f;
    static constexpr uint8_t _player_size = 8;
    static constexpr uint8_t _hud_h = 10;

    struct _player_data {
      float x;
      float y;
    };

    static uint16_t _grass;
    static _player_data _player;
    static uint32_t _last_ms;
    static handler _handler;
    static uint32_t _tick;
    static uint32_t _peer_tick;

    static void _on_heartbeat(const uint8_t* data, size_t len);
    static void _sim(float dt);
    static void _render_clear();
    static void _render_draw();
};
