#include <Display.h>
#include "map.h"

namespace tilemap {
  uint8_t tiles[ROWS][COLS];
  uint16_t spawn_px = 0;
  uint16_t spawn_py = 0;

  void init() {
    spawn_px = 0;
    spawn_py = 0;

    for (uint8_t r = 0; r < ROWS; ++r) {
      for (uint8_t c = 0; c < COLS; ++c) {
        const char ch = _art[r][c];
        uint8_t t = FLOOR;
        switch (ch) {
          case '#':
            t = WALL;
            break;
          case '~':
            t = WATER;
            break;
          case '=':
            t = BUS;
            break;
          case 'S':
            t = SHOP_SMG;
            break;
          case '!':
            t = SHOP_SHOTGUN;
            break;
          case '^':
            t = SHOP_RIFLE;
            break;
          case '+':
            t = SHOP_HEAL;
            break;
          case 'P':
            t = FLOOR;
            spawn_px = (uint16_t)c * TILE + TILE / 2;
            spawn_py = (uint16_t)r * TILE + TILE / 2;
            break;
          default:
            break;
        }
        tiles[r][c] = t;
      }
    }

    if (spawn_px == 0 && spawn_py == 0) { // safety fallback = world center
      spawn_px = WORLD_W / 2;
      spawn_py = WORLD_H / 2;
    }
  }

  bool solid(int16_t tx, int16_t ty) {
    if (tx < 0 || tx >= (int16_t)COLS || ty < 0 || ty >= (int16_t)ROWS) {
      return true; // outside the map is wall
    }
    return tiles[ty][tx] != FLOOR;
  }

  bool solid_rect(int16_t x, int16_t y, uint8_t w, uint8_t h) {
    const int16_t tx0 = x / TILE;
    const int16_t ty0 = y / TILE;
    const int16_t tx1 = (x + (int16_t)w - 1) / TILE;
    const int16_t ty1 = (y + (int16_t)h - 1) / TILE;

    for (int16_t ty = ty0; ty <= ty1; ++ty) {
      for (int16_t tx = tx0; tx <= tx1; ++tx) {
        if (solid(tx, ty)) {
          return true;
        }
      }
    }
    return false;
  }

  uint8_t tile_at(int16_t wx, int16_t wy) {
    const int16_t tx = wx / TILE;
    const int16_t ty = wy / TILE;
    if (tx < 0 || tx >= (int16_t)COLS || ty < 0 || ty >= (int16_t)ROWS) {
      return WALL;
    }
    return tiles[ty][tx];
  }

  uint16_t color(uint8_t t) {
    switch (t) {
      case WALL:
        return display::rgb565(96, 96, 96);
      case WATER:
        return display::rgb565(20, 70, 160);
      case SHOP_SMG:
        return display::rgb565(200, 150, 0);
      case SHOP_SHOTGUN:
        return display::rgb565(215, 110, 10);
      case SHOP_RIFLE:
        return display::rgb565(0, 160, 210);
      case SHOP_HEAL:
        return display::rgb565(205, 50, 50);
      case BUS:
        return display::rgb565(70, 100, 130);
      default:
        return display::rgb565(38, 62, 38); // floor
    }
  }

  uint16_t color_at(int16_t wx, int16_t wy) {
    return color(tile_at(wx, wy));
  }
}
