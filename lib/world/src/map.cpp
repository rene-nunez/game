#include <Display.h>
#include "map.h"

namespace tilemap {
  uint8_t tiles[ROWS][COLS];
  uint16_t spawn_px = 0;
  uint16_t spawn_py = 0;
  uint16_t field[ROWS][COLS];

  static bool _walkable_tile(uint8_t t) {
    return t == FLOOR;
  }

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
          case 'V':
            t = VENDING;
            break;
          case 'R':
            t = ROULETTE;
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
    return !_walkable_tile(tiles[ty][tx]);
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
        return display::rgb565(150, 120, 85); // maze timber
      case VENDING:
        return display::rgb565(40, 150, 200); // vending teal
      case ROULETTE:
        return display::rgb565(220, 200, 60); // prize wheel gold
      default:
        return display::rgb565(46, 95, 55); // meadow grass
    }
  }

  // scale an rgb565 colour by num/den with clamping; used for sprite shading.
  static uint16_t _shade(uint16_t col, uint8_t num, uint8_t den) {
    uint16_t r = (uint16_t)((col >> 11) & 0x1F);
    uint16_t g = (uint16_t)((col >> 5) & 0x3F);
    uint16_t b = (uint16_t)(col & 0x1F);
    r = r * num / den;
    g = g * num / den;
    b = b * num / den;
    if (r > 0x1F) r = 0x1F;
    if (g > 0x3F) g = 0x3F;
    if (b > 0x1F) b = 0x1F;
    return (uint16_t)((r << 11) | (g << 5) | b);
  }

  // Super-minimal: flat grass, 1px darker edge on plain solids. The shop
  // furniture gets real 16px sprites so it reads as what it is.
  uint16_t color_at(int16_t wx, int16_t wy) {
    const uint8_t t = tile_at(wx, wy);
    const uint16_t base = color(t);
    if (t == FLOOR) {
      return base;
    }
    if (wx < 0 || wy < 0 || wx >= (int16_t)WORLD_W || wy >= (int16_t)WORLD_H) {
      return base;
    }
    const int16_t px = wx & 15;
    const int16_t py = wy & 15;

    switch (t) {
      case VENDING: {
        // 2x2 vending machine (32x32): local coords from the same-type
        // neighbours above/left, so every quadrant draws its own quarter.
        const int16_t gx = (tile_at(wx - TILE, wy) == VENDING ? 16 : 0) + px;
        const int16_t gy = (tile_at(wx, wy - TILE) == VENDING ? 16 : 0) + py;
        const bool edge_x = (gx == 0 || gx == 31);
        const bool edge_y = (gy == 0 || gy == 31);
        if (edge_x || edge_y) {
          return display::rgb565(15, 40, 55); // cabinet frame
        }
        if (gy <= 6) {
          if (gx >= 8 && gx <= 13 && gy >= 2 && gy <= 4) {
            return display::rgb565(10, 25, 35); // logo dots
          }
          return display::rgb565(210, 240, 250); // header light
        }
        if (gy >= 28) {
          return display::rgb565(20, 50, 65); // kick plate
        }
        if (gx >= 25) {
          if (gx >= 26 && gx <= 27 && gy >= 9 && gy <= 13) {
            return display::rgb565(10, 20, 25); // coin slot
          }
          if (gx >= 25 && gx <= 30 && gy >= 19 && gy <= 24) {
            return display::rgb565(10, 20, 25); // pickup flap
          }
          return _shade(base, 3, 4); // side panel
        }
        if (gy == 12 || gy == 18 || gy == 24) {
          return display::rgb565(200, 220, 230); // shelf rails
        }
        if (gy == 10 || gy == 11 || gy == 16 || gy == 17 || gy == 22 || gy == 23) {
          // stocked goods: red / yellow / green / orange cans
          if (gx >= 4 && gx <= 6) return display::rgb565(220, 60, 50);
          if (gx >= 9 && gx <= 11) return display::rgb565(230, 200, 60);
          if (gx >= 14 && gx <= 16) return display::rgb565(70, 200, 90);
          if (gx >= 19 && gx <= 21) return display::rgb565(230, 130, 40);
        }
        return display::rgb565(25, 60, 75); // dark glass
      }
      case ROULETTE: {
        // 2x2 prize wheel (32x32): rim + 8 segments + pegs + hub + pointer.
        const int16_t gx = (tile_at(wx - TILE, wy) == ROULETTE ? 16 : 0) + px;
        const int16_t gy = (tile_at(wx, wy - TILE) == ROULETTE ? 16 : 0) + py;
        const int16_t dx = gx - 16;
        const int16_t dy = gy - 13;
        const int16_t d2 = dx * dx + dy * dy;
        if (gy >= 27) {
          if (gx >= 14 && gx <= 17) return display::rgb565(90, 60, 35); // post
          if (gy >= 29 && gx >= 10 && gx <= 21) {
            return display::rgb565(70, 45, 25); // feet
          }
          return color(FLOOR); // grass shows through around the stand
        }
        if (gy <= 3 && gx >= 14 && gx <= 17) {
          return display::rgb565(220, 50, 45); // pointer tip
        }
        if (d2 > 196) {
          return color(FLOOR); // grass shows through outside the wheel
        }
        if (d2 > 169) {
          return display::rgb565(140, 40, 35); // rim
        }
        if (d2 <= 9) {
          return display::rgb565(250, 245, 220); // hub
        }
        int16_t ax = dx < 0 ? -dx : dx;
        int16_t ay = dy < 0 ? -dy : dy;
        uint8_t zone;
        if (ax > (ay << 1)) {
          zone = dx > 0 ? 0 : 4;
        } else if (ay > (ax << 1)) {
          zone = dy > 0 ? 2 : 6;
        } else if (dx > 0) {
          zone = dy > 0 ? 1 : 7;
        } else {
          zone = dy > 0 ? 3 : 5;
        }
        if (d2 >= 121 && d2 <= 144 && (((gx + gy) & 3) == 0)) {
          return display::rgb565(245, 245, 245); // pegs
        }
        return (zone & 1) ? display::rgb565(200, 60, 55) : base;
      }
      default: {
        if (px == 0 || px == 15 || py == 0 || py == 15) {
          return _shade(base, 4, 5); // 1px darker edge on plain solids
        }
        return base;
      }
    }
  }

  static const int8_t _step_x[4] = {1, -1, 0, 0};
  static const int8_t _step_y[4] = {0, 0, 1, -1};
  static uint16_t _queue[COLS * ROWS];

  void build_field(int16_t tx, int16_t ty) {
    for (uint8_t r = 0; r < ROWS; ++r) {
      for (uint8_t c = 0; c < COLS; ++c) {
        field[r][c] = UNREACHABLE;
      }
    }
    if (tx < 0 || tx >= (int16_t)COLS || ty < 0 || ty >= (int16_t)ROWS || solid(tx, ty)) {
      return; // nothing to spread from: every tile stays UNREACHABLE
    }

    uint16_t head = 0, tail = 0; // each tile is queued at most once, so tail <= COLS * ROWS
    field[ty][tx] = 0;
    _queue[tail++] = (uint16_t)(ty * COLS + tx);

    while (head < tail) {
      const uint16_t idx = _queue[head++];
      const int16_t cx = (int16_t)(idx % COLS), cy = (int16_t)(idx / COLS);
      const uint16_t nd = (uint16_t)(field[cy][cx] + 1);

      for (uint8_t k = 0; k < 4; ++k) {
        const int16_t nx = (int16_t)(cx + _step_x[k]), ny = (int16_t)(cy + _step_y[k]);
        if (nx < 0 || nx >= (int16_t)COLS || ny < 0 || ny >= (int16_t)ROWS) continue;
        if (field[ny][nx] != UNREACHABLE || solid(nx, ny)) continue;
        field[ny][nx] = nd;
        _queue[tail++] = (uint16_t)(ny * COLS + nx);
      }
    }
  }

  uint16_t dist_at(int16_t wx, int16_t wy) {
    if (wx < 0 || wy < 0 || wx >= (int16_t)WORLD_W || wy >= (int16_t)WORLD_H) {
      return UNREACHABLE;
    }
    return field[wy / TILE][wx / TILE];
  }
}
