#pragma once

#include <cstdint>

#include <Handler.h>
#include <Input.h>
#include <Screens.h>

#include <Sim.h>

class game {
  public:
    static bool begin(uint8_t role);
    static void update();

  private:
    static constexpr uint32_t _frame_ms = 33; // ~30 fps

    static uint32_t _last_frame_ms;

    static uint8_t _sel;
    static int8_t _nav_dir;

    static handler _handler;
    static uint32_t _tick;
    static uint32_t _peer_tick;

    static void _on_heartbeat(const uint8_t* data, size_t len);

    static int8_t _nav_edge();
    static void _nav_step(); // nav edge + wrap, using the screen's own item count
    static void _sleep();
    static screens::id _scr;

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
