#pragma once

#include <cstdint>

// The simulation: player, zombies, bullets, waves and score, in world px. It owns its
// state and never touches the screen, the menus or the network, so the renderer, the panel
// and the menus can only read it through view().
class sim {
  public:
    static constexpr uint8_t PLAYER_SIZE = 8;
    static constexpr uint8_t ZOMBIE_SIZE = 6;
    static constexpr uint8_t BULLET_SIZE = 4;
    static constexpr uint8_t MAX_ZOMBIES = 8;
    static constexpr uint8_t MAX_BULLETS = 8;
    static constexpr uint8_t PLAYER_HP_MAX = 5; // the panel draws one pip per point

    struct state {
      struct actor {
        float x, y;
        uint8_t hp;
        bool active;
      };
      struct shot {
        float x, y, vx, vy;
        bool active;
      };
      struct {
        float x, y;
      } player;
      uint8_t player_hp, wave, kills;
      uint32_t score;
      actor zombies[MAX_ZOMBIES];
      shot bullets[MAX_BULLETS];
    };

    // the only way in: a const ref, so nothing outside can move a zombie or spend a point.
    // The renderer and the panel read it every frame, and it is the whole net-sync payload.
    static const state& view() { return _s; }

    static void reset();
    static bool step(uint32_t now); // false once the player is out of hp

  private:
    static constexpr float player_speed = 110.0f;
    static constexpr float zombie_speed = 40.0f;
    static constexpr uint8_t zombie_hp = 2;
    static constexpr uint16_t spawn_min_d2 = 100 * 100; // keep spawns >= 100px away
    static constexpr float bullet_speed = 320.0f;
    static constexpr float fire_range = 160.0f;
    static constexpr uint32_t fire_cd_ms = 500;
    static constexpr float hit_dist = 5.0f;
    static constexpr float contact_dist = 9.0f;
    static constexpr uint32_t damage_cd_ms = 500;
    static constexpr uint32_t score_per_kill = 10;

    // 8-neighbourhood, cardinals first: the zombie aims at the best neighbour's centre,
    // so a diagonal step reads as smooth drift instead of a tile-by-tile shuffle
    static constexpr int8_t nbr_x[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int8_t nbr_y[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    // host-local runtime state, deliberately outside the view: the frame clock, the fire and
    // damage cooldowns and the tile the BFS field was last built for are not peer state
    static state _s;
    static uint32_t _last_ms, _last_shot, _last_damage;
    static int16_t _path_tx, _path_ty;

    static void _move_entity(float& x, float& y, float dx, float dy, uint8_t size);
    static bool _step_zombie(uint8_t z, float ddx, float ddy, float dt);
    static void _zombie_steer(uint8_t z, float pcx, float pcy, float dt);
    static void _spawn_wave();
    static void _do_fire(uint32_t now);
};
