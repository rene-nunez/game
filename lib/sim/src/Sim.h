#pragma once

#include <cstdint>

// The simulation: player, zombies, bullets, waves and points, in world px. It owns its
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
    static constexpr uint8_t MAX_LVL = 5;       // damage and speed cap here, pips per level

    enum class weapon : uint8_t { pistol, smg, shotgun, rifle };
    enum class actor_kind : uint8_t { normal, runner, boss };
    enum class event : uint8_t { none, shoot, buy_heal, buy_dmg, buy_spd, roulette, denied, hurt, wave, over };

    // shop: points are the spendable wallet. Heal is flat; damage and speed are
    // permanent levels on the character (like HP) and each level costs more.
    static constexpr uint32_t PRICE_HEAL = 100; // +2 HP
    static constexpr uint32_t PRICE_DMG = 150;  // damage +25%/level, base price
    static constexpr uint32_t PRICE_SPD = 120;  // speed +8%/level, base price
    static constexpr uint32_t PRICE_ROLL = 100; // roulette: random weapon
    static constexpr uint32_t LVL_PRICE_STEP = 200; // extra cost per level owned

    struct state {
      struct actor {
        float x, y;
        uint8_t hp;
        bool active;
        actor_kind kind;
      };
      struct shot {
        float x, y, vx, vy;
        uint8_t dmg;
        bool active;
      };
      struct {
        float x, y;
      } player;
      uint8_t player_hp, wave, kills;
      uint32_t points;
      weapon gun;
      uint8_t dmg_lvl; // permanent damage levels, 0..MAX_LVL
      uint8_t spd_lvl; // permanent speed levels, 0..MAX_LVL
      event last_event; // set by step() and by the buy calls below, read by game
      actor zombies[MAX_ZOMBIES];
      shot bullets[MAX_BULLETS];
    };

    // the only way in: a const ref, so nothing outside can move a zombie or spend a point.
    // The renderer and the panel read it every frame, and it is the whole net-sync payload.
    static const state& view() { return _s; }

    static void reset();
    static bool step(uint32_t now); // false once the player is out of hp

    // shop, called by game on an INTERACT edge near a machine. Exact points pay:
    // points >= price succeeds. On denial last_event is denied.
    static bool buy_heal(uint32_t now);
    static bool buy_damage(uint32_t now);
    static bool buy_speed(uint32_t now);
    static bool roll_roulette(uint32_t now);

    static uint32_t price_for(uint32_t base, uint8_t lvl); // base + STEP*lvl
    static const char* gun_name();
    static const char* gun_name(weapon w);
  private:
    static constexpr float player_speed = 110.0f;
    static constexpr float zombie_speed = 40.0f; // normal; runner 70, boss 30
    static constexpr float runner_speed = 70.0f;
    static constexpr float boss_speed = 30.0f;
    static constexpr uint32_t runner_reward = 15; // base, +2 per wave on top
    static constexpr uint16_t spawn_min_d2 = 100 * 100; // keep spawns >= 100px away
    static constexpr float bullet_speed = 320.0f;
    static constexpr float fire_range = 160.0f;
    static constexpr float hit_dist = 5.0f;
    static constexpr float contact_dist = 9.0f;
    static constexpr uint32_t damage_cd_ms = 500;

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
    static uint32_t _fire_cd(weapon w);
    static uint8_t _base_dmg(weapon w);
    static uint8_t _eff_dmg(uint8_t base, uint8_t lvl); // base*(1+0.25*lvl), half-up, min 1
    static float _spd_mult(uint8_t lvl);                // 1+0.08*lvl
    static uint8_t _zombie_hp(actor_kind kind, uint8_t wave); // normal 2+w/2, runner 1+w/6, boss 20+w
    static uint8_t _zombie_dmg(actor_kind kind);        // boss 2, rest 1
    static float _zombie_speed(actor_kind kind);        // 40 / 70 / 30
    static uint32_t _kill_reward(actor_kind kind, uint8_t wave); // normal 10+2w, runner 15+2w, boss 150+10w
    static uint8_t _wave_total(uint8_t wave); // min(wave+3, MAX_ZOMBIES)
    static uint8_t _wave_runners(uint8_t wave, uint8_t total); // 0 on wave 1, else up to half
    static bool _wave_boss(uint8_t wave);     // every 5th wave steals slot 0
    // roulette odds over r = rand % 100: SMG 40, pistol 15, shotgun 30, rifle 15.
    // SMG and shotgun hit more often; the pistol can come back as the booby prize.
    static weapon _roll_weapon(uint8_t r);
    static uint8_t _fire_one(uint32_t now, float dx, float dy, uint8_t dmg);
};
