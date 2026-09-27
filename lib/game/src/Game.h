#pragma once

#include <cstdint>

#include <Handler.h>
#include <Input.h>

#include <map.h>
#include <Sim.h>

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

    static constexpr uint32_t _frame_ms = 33; // ~30 fps

    // screen layout: hud strip, arena (world rows), bottom panel
    static constexpr uint8_t _hud_h = 10;
    static constexpr uint8_t _arena_h = 160;
    static constexpr uint8_t _panel_h = 70;
    static constexpr int16_t _arena_bottom = _hud_h + _arena_h; // panel starts here
    static constexpr uint8_t _paint_chunk = 80;                // arena rows repainted per frame

    // menu rows: the full paint and the cursor repaint must agree on where a row is, so
    // both read these. _menu_item takes an absolute y; nothing may add the row pitch twice.
    static constexpr int16_t _menu_x = 16, _menu_y = 56, _menu_row = 16;
    static constexpr int16_t _over_x = 24, _over_y = 110; // the game over screen is indented

    // minimap: 2px per tile, drawn once, then only blips change
    static constexpr uint8_t _mm_scale = 2;
    static constexpr int16_t _mm_w = (int16_t)tilemap::COLS * _mm_scale;
    static constexpr int16_t _mm_h = (int16_t)tilemap::ROWS * _mm_scale;
    static constexpr int16_t _mm_gap = 4;
    static constexpr int16_t _mm_y = _arena_bottom + 5;



    static int16_t _cam_x;
    static int16_t _cam_y;
    static int16_t _paint_y; // next arena row to repaint, _arena_h when idle
    static uint32_t _last_frame_ms;

    static uint8_t _menu_scr; // screen the menu chrome was last painted for, 0xFF = none
    static uint8_t _menu_sel; // selection currently on screen
    static int16_t _mm_px[1 + sim::MAX_ZOMBIES]; // minimap blips drawn last frame
    static int16_t _mm_py[1 + sim::MAX_ZOMBIES];
    static uint8_t _mm_n;
    static int16_t _mm_ctx; // camera cell tile whose frame is on the minimap, -1 = none yet
    static int16_t _mm_cty;

    static uint32_t _best;
    static uint32_t _total_kills;

    static _screens _scr;
    static uint8_t _sel;
    static int8_t _nav_dir;

    static handler _handler;
    static uint32_t _tick;
    static uint32_t _peer_tick;

    static void _on_heartbeat(const uint8_t* data, size_t len);
    static void _paint_view();
    static void _paint_step();
    static void _update_camera();
    static int16_t _cell_cam(int16_t p, int16_t step, int16_t max_cam);
    static void _erase_world_rect(int16_t wx, int16_t wy, uint8_t size);
    static void _fill_world_run(int16_t wx, int16_t sy, int16_t w, uint16_t col);
    static void _fill_world_box(int16_t wx, int16_t wy, uint8_t size, uint16_t col);
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
    static void _menu_invalidate();
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
