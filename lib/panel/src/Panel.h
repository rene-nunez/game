#pragma once

#include <cstdint>

#include <Render.h>
#include <Sim.h>
#include <map.h>

// The bottom panel: minimap plus the status text. The minimap outlines the camera cell, so it
// reads render for the arena origin and the camera, and sim for the blips.
class panel {
  public:
    static void init();  // once per run: black panel, minimap terrain, cell separators
    static void draw();  // per frame: best, wave, kills and the HP pips
    static void blips(); // per frame: camera cell frame and the entity dots
    static void hint(const char* msg); // transient shop prompt, painted by draw()

  private:
    // minimap: 2px per tile, drawn once, then only blips change
    static constexpr uint8_t _mm_scale = 2;
    static constexpr uint8_t _panel_h = 70;
    static constexpr int16_t _mm_w = (int16_t)tilemap::COLS * _mm_scale;
    static constexpr int16_t _mm_h = (int16_t)tilemap::ROWS * _mm_scale;
    static constexpr int16_t _mm_gap = 4;
    static constexpr int16_t _mm_y = render::ARENA_BOTTOM + 5;

    static int16_t _mm_px[1 + sim::MAX_ZOMBIES]; // minimap blips drawn last frame
    static int16_t _mm_py[1 + sim::MAX_ZOMBIES];
    static uint8_t _mm_n;
    static int16_t _mm_ctx; // camera cell tile whose frame is on the minimap, -1 = none yet
    static int16_t _mm_cty;
    static const char* _hint; // shop prompt for this frame, null = none (set by game)

    static int16_t _mm_x();
    static void _mm_restore_row(int16_t tx0, int16_t tx1, int16_t ty);
    static void _mm_restore_col(int16_t tx, int16_t ty0, int16_t ty1);
    static void _mm_dot(int16_t wx, int16_t wy, uint16_t col);
    static void _mm_frame();
};
