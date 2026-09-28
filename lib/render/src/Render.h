#pragma once

#include <cstdint>

// The arena view: camera, the incremental terrain repaint and the entity boxes. It reads the
// simulation and nothing else, so what it paints is a function of sim state plus geometry.
// The panel sits next to it and needs the arena origin and the camera, hence those are public.
class render {
  public:
    // screen layout: hud strip (role badge only), arena (world rows), bottom panel
    static constexpr uint8_t HUD_H = 10;
    static constexpr uint8_t ARENA_H = 160;
    static constexpr int16_t ARENA_BOTTOM = HUD_H + ARENA_H; // panel starts here

    static void repaint();     // schedule a full arena repaint from the tilemap
    static void repaint_step(); // paint up to PAINT_CHUNK pending arena rows
    static void update_camera();
    static void clear();       // erase the entities through the tilemap colours
    static void draw();        // terrain, hud score and the entity boxes

    static int16_t cam_x();
    static int16_t cam_y();

  private:
    static constexpr uint8_t PAINT_CHUNK = 80; // arena rows repainted per frame

    static int16_t _cam_x, _cam_y;
    static int16_t _paint_y; // next arena row to repaint, ARENA_H when idle

    static int16_t _cell_cam(int16_t p, int16_t step, int16_t max_cam);
    static void _fill_world_run(int16_t wx, int16_t sy, int16_t w, uint16_t col);
    static void _erase_world_rect(int16_t wx, int16_t wy, uint8_t size);
    static void _fill_world_box(int16_t wx, int16_t wy, uint8_t size, uint16_t col);
};
