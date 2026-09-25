#include <Display.h>
#include "map.h"

namespace tilemap {
  uint8_t tiles[ROWS][COLS];
  uint16_t spawn_px = 0;
  uint16_t spawn_py = 0;

  void init() {
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

  bool solid(uint8_t tx, uint8_t ty) {
    return tiles[ty][tx] != FLOOR;
  }

  uint8_t tile_at(float wx, float wy) {
    int16_t tx = (int16_t)(wx / TILE);
    int16_t ty = (int16_t)(wy / TILE);
    if (tx < 0) {
      tx = 0;
    } else if (tx > (int16_t)COLS - 1) {
      tx = (int16_t)COLS - 1;
    }
    if (ty < 0) {
      ty = 0;
    } else if (ty > (int16_t)ROWS - 1) {
      ty = (int16_t)ROWS - 1;
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

  uint16_t color_at(float wx, float wy) {
    return color(tile_at(wx, wy));
  }
}