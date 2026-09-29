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
    static constexpr int16_t _shop_r = 40;    // INTERACT reach, px from a machine centre

    static uint32_t _last_frame_ms;

    static uint8_t _sel;
    static int8_t _nav_dir;

    static handler _handler;
    static uint32_t _tick;
    static uint32_t _peer_tick;

    static int16_t _shop_hx, _shop_hy; // heal vending centre, world px (-1 = missing)
    static int16_t _shop_dx, _shop_dy; // damage vending centre
    static int16_t _shop_sx, _shop_sy; // speed vending centre
    static int16_t _shop_rx, _shop_ry; // roulette centre
    static char _hint_buf[28];         // transient result text ("NEED 100", "GUN: SMG")
    static uint32_t _hint_until;       // result visible while millis() < this

    static void _on_heartbeat(const uint8_t* data, size_t len);

    static int8_t _nav_edge();
    static void _nav_step(); // nav edge + wrap, using the screen's own item count
    static void _sleep();
    static screens::id _scr;

    static void _start_game();
    static void _enter_menu();
    static void _enter_game_over();

    static void _scan_shops();       // cache the 2x2 machine centres, once per run
    static void _shop_update(uint32_t now); // INTERACT buys + panel prompt, after sim::step

    static void _update_menu();
    static void _update_mode();
    static void _update_points();
    static void _update_playing();
    static void _update_pause();
    static void _update_game_over();
};
