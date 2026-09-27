#pragma once

#include <cstdint>

#include <Handler.h>
#include <Input.h>

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

    // menu rows: the full paint and the cursor repaint must agree on where a row is, so
    // both read these. _menu_item takes an absolute y; nothing may add the row pitch twice.
    static constexpr int16_t _menu_x = 16, _menu_y = 56, _menu_row = 16;
    static constexpr int16_t _over_x = 24, _over_y = 110; // the game over screen is indented

    static uint32_t _last_frame_ms;

    static uint8_t _menu_scr; // screen the menu chrome was last painted for, 0xFF = none
    static uint8_t _menu_sel; // selection currently on screen

    static _screens _scr;
    static uint8_t _sel;
    static int8_t _nav_dir;

    static handler _handler;
    static uint32_t _tick;
    static uint32_t _peer_tick;

    static void _on_heartbeat(const uint8_t* data, size_t len);

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
