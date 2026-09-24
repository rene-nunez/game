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

    static constexpr uint8_t _zombie_size = 6;
    static constexpr float _zombie_speed = 40.0f;
    static constexpr uint8_t _zombie_hp = 2;
    static constexpr uint8_t _max_zombies = 8;
    static constexpr uint8_t _zombie_margin = 20;

    static constexpr uint8_t _bullet_size = 4;
    static constexpr float _bullet_speed = 320.0f;
    static constexpr uint8_t _max_bullets = 8;
    static constexpr float _fire_range = 160.0f;
    static constexpr uint32_t _fire_cd_ms = 500;
    static constexpr float _hit_dist = 5.0f;

    static constexpr float _contact_dist = 9.0f;
    static constexpr uint32_t _damage_cd_ms = 500;
    static constexpr uint8_t _player_hp_max = 5;

    struct _player_data {
      float x;
      float y;
    };

    struct _zombie {
      float x;
      float y;
      uint8_t hp;
      bool active;
    };

    struct _bullet {
      float x;
      float y;
      float vx;
      float vy;
      bool active;
    };

    static uint16_t _grass;
    static _player_data _player;
    static uint32_t _last_ms;

    static _zombie _zombies[_max_zombies];
    static _bullet _bullets[_max_bullets];
    static uint8_t _player_hp;
    static uint8_t _wave;
    static uint8_t _kills;
    static uint32_t _last_shot;
    static uint32_t _last_damage;
    static bool _game_over;

    static handler _handler;
    static uint32_t _tick;
    static uint32_t _peer_tick;

    static void _on_heartbeat(const uint8_t* data, size_t len);
    static void _spawn_wave();
    static void _do_fire(uint32_t now);
    static void _restart();
    static void _sim(float dt, uint32_t now);
    static void _render_clear();
    static void _render_draw();
};