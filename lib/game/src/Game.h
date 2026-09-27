#pragma once

#include <cstdint>

#include <Handler.h>
#include <Input.h>

#include "map.h"

class game {
  public:
    static bool begin(uint8_t role);
    static void update();

  private:
    enum class _screens : uint8_t {
      menu,
      mode,
      scores,
      playing,
      pause,
      game_over,
    };

    static constexpr float _player_speed = 110.0f;
    static constexpr uint8_t _player_size = 8;
    static constexpr uint32_t _frame_ms = 33; // ~30 fps

    // screen layout: hud strip, arena (world rows), bottom panel
    static constexpr uint8_t _hud_h = 10;
    static constexpr uint8_t _arena_h = 160;
    static constexpr uint8_t _panel_h = 70;
    static constexpr int16_t _arena_bottom = _hud_h + _arena_h; // panel starts here
    static constexpr uint8_t _paint_chunk = 80;                // arena rows repainted per frame

    // minimap: 2px per tile, drawn once, then only blips change
    static constexpr uint8_t _mm_scale = 2;
    static constexpr int16_t _mm_w = (int16_t)tilemap::COLS * _mm_scale;
    static constexpr int16_t _mm_h = (int16_t)tilemap::ROWS * _mm_scale;
    static constexpr int16_t _mm_gap = 4;
    static constexpr int16_t _mm_y = _arena_bottom + 5;

    static constexpr uint8_t _zombie_size = 6;
    static constexpr float _zombie_speed = 40.0f;
    static constexpr uint8_t _zombie_hp = 2;
    static constexpr uint8_t _max_zombies = 8;
    static constexpr uint16_t _spawn_min_d2 = 100 * 100; // keep spawns >= 100px away

    static constexpr uint8_t _bullet_size = 4;
    static constexpr float _bullet_speed = 320.0f;
    static constexpr uint8_t _max_bullets = 8;
    static constexpr float _fire_range = 160.0f;
    static constexpr uint32_t _fire_cd_ms = 500;
    static constexpr float _hit_dist = 5.0f;

    static constexpr float _contact_dist = 9.0f;
    static constexpr uint32_t _damage_cd_ms = 500;
    static constexpr uint8_t _player_hp_max = 5;
    static constexpr uint32_t _score_per_kill = 10;

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

    // 8-neighbourhood, cardinals first: the zombie aims at the best neighbour's centre,
    // so a diagonal step reads as smooth drift instead of a tile-by-tile shuffle
    static constexpr int8_t _nbr_x[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int8_t _nbr_y[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    struct _bullet {
      float x;
      float y;
      float vx;
      float vy;
      bool active;
    };

    static int16_t _cam_x;
    static int16_t _cam_y;
    static int16_t _path_tx; // player tile the BFS field was last built for, -1 = none
    static int16_t _path_ty;
    static int16_t _paint_y; // next arena row to repaint, _arena_h when idle
    static _player_data _player;
    static uint32_t _last_ms;
    static uint32_t _last_frame_ms;

    static _zombie _zombies[_max_zombies];
    static _bullet _bullets[_max_bullets];
    static uint8_t _menu_scr; // screen the menu chrome was last painted for, 0xFF = none
    static uint8_t _menu_sel; // selection currently on screen
    static int16_t _mm_px[1 + _max_zombies]; // minimap blips drawn last frame
    static int16_t _mm_py[1 + _max_zombies];
    static uint8_t _mm_n;
    static int16_t _mm_ctx; // camera cell tile whose frame is on the minimap, -1 = none yet
    static int16_t _mm_cty;
    static uint8_t _player_hp;
    static uint8_t _wave;
    static uint8_t _kills;
    static uint32_t _last_shot;
    static uint32_t _last_damage;

    static uint32_t _score;
    static uint32_t _best;
    static uint32_t _total_kills;

    static _screens _scr;
    static uint8_t _sel;
    static int8_t _nav_dir;

    static handler _handler;
    static uint32_t _tick;
    static uint32_t _peer_tick;

    static void _on_heartbeat(const uint8_t* data, size_t len);
    static void _spawn_wave();
    static void _do_fire(uint32_t now);
    static void _restart();
    static void _paint_view();
    static void _paint_step();
    static void _update_camera();
    static int16_t _cell_cam(int16_t p, int16_t step, int16_t max_cam);
    static void _move_entity(float& x, float& y, float dx, float dy, uint8_t size);
    static bool _step_zombie(uint8_t z, float ddx, float ddy, float dt);
    static void _zombie_steer(uint8_t z, float pcx, float pcy, float dt);
    static void _erase_world_rect(int16_t wx, int16_t wy, uint8_t size);
    static void _fill_world_run(int16_t wx, int16_t sy, int16_t w, uint16_t col);
    static void _fill_world_box(int16_t wx, int16_t wy, uint8_t size, uint16_t col);
    static void _sim(float dt, uint32_t now);
    static void _render_clear();
    static void _render_draw();
    static void _panel_init();
    static void _draw_panel();
    static void _minimap_blips();
    static int16_t _mm_x();
    static void _mm_dot(int16_t wx, int16_t wy, uint16_t col);
    static void _mm_restore_row(int16_t tx0, int16_t tx1, int16_t ty);
    static void _mm_restore_col(int16_t tx, int16_t ty0, int16_t ty1);
    static void _mm_frame();

    static int8_t _nav_edge();
    static void _sleep();
    static void _draw_menu(const char* title, const char* const* items, uint8_t count);
    static void _draw_scores();
    static void _draw_game_over();
    static bool _menu_entered();
    static void _menu_item(const char* const* items, uint8_t i, bool selected, int16_t x, int16_t y0);
    static void _menu_cursor(const char* const* items, uint8_t count, int16_t x, int16_t y0);
    static void _start_game();
    static void _enter_menu();
    static void _enter_game_over();

    static void _update_menu();
    static void _update_mode();
    static void _update_scores();
    static void _update_playing();
    static void _update_pause();
    static void _update_game_over();
};
