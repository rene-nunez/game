#pragma once

#include <cstdint>

namespace tilemap {
  constexpr uint8_t COLS = 60;
  constexpr uint8_t ROWS = 30;
  constexpr uint8_t TILE = 16;
  constexpr uint16_t WORLD_W = COLS * TILE;
  constexpr uint16_t WORLD_H = ROWS * TILE;

  constexpr uint8_t FLOOR = 0;  // grass, walkable (the only walkable tile)
  constexpr uint8_t WALL = 1;   // maze wall, solid
  constexpr uint8_t VENDING = 2;// vending machine (2x2), solid, visual only
  constexpr uint8_t ROULETTE = 3;// prize wheel (2x2), solid, visual only
  // NB: 'P' in _art is the player spawn; it parses straight to FLOOR.

  // Super-minimal green village: 60x30 all-grass with a maze interconnecting
  // every district, including the center, which holds a 2x2 vending machine
  // and a 2x2 prize wheel. 2-tile clear lanes on the ring double as the camera
  // cut lines, so a hard cut always lands on open grass there; interior cuts
  // may land on maze wall. Buying comes later (visual now).
  // Legend: # wall  . grass  V vending  R roulette  P spawn
  constexpr char _art[ROWS][COLS + 1] = {
"############################################################",
"#..........................................................#",
"#..........................................................#",
"#..........................................................#",
"#........########.......########...####....########...###..#",
"#....P...#...................#..................#.......#..#",
"#........#.###..#.......###..#.########....####.#.#######..#",
"#........#...#..#.......#........#.........#............#..#",
"#..#####.#.###..#....#######...########..#######...######..#",
"#..........................................................#",
"#..........................................................#",
"#..........................................................#",
"#.......###..###........############........##########.....#",
"#....####......#........#..........#........#..........#...#",
"#....#......#..#...........VV..RR..#........######.....#...#",
"#....#..#####..#...........VV..RR...........#..........#...#",
"#....#..#......#........#..........#........#..#...#####...#",
"#.......................####...#####...........#.....#.#...#",
"#...#########..............................#######...#.....#",
"#..........................................................#",
"#..........................................................#",
"#..........................................................#",
"#....########...........########...###.......########.#....#",
"#....#..........#............#...............#........#....#",
"#....#.########.#.......####.#.#######.......#.########....#",
"#....#..........#.......#........#...........#........#....#",
"#..#######......#.......#######..#####.....#######....#....#",
"#..........................................................#",
"#..........................................................#",
"############################################################",
  };

  extern uint8_t tiles[ROWS][COLS];
  extern uint16_t spawn_px;
  extern uint16_t spawn_py;

  // BFS distance field to a target tile, in tile steps, over walkable tiles only.
  // UNREACHABLE marks the tiles the target cannot reach. Every reachable tile with
  // d > 0 has a 4-neighbour with d - 1, so walking downhill never gets stuck.
  constexpr uint16_t UNREACHABLE = 0xFFFF;
  extern uint16_t field[ROWS][COLS];

  void init();
  bool solid(int16_t tx, int16_t ty);
  bool solid_rect(int16_t x, int16_t y, uint8_t w, uint8_t h);
  uint8_t tile_at(int16_t wx, int16_t wy);
  uint16_t color(uint8_t tile);
  uint16_t color_at(int16_t wx, int16_t wy);
  void build_field(int16_t tx, int16_t ty);
  uint16_t dist_at(int16_t wx, int16_t wy);
}
