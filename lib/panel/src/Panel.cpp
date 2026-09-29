#include <Arduino.h>

#include <Display.h>
#include <map.h>
#include <Render.h>
#include <Sim.h>

#include "Panel.h"

int16_t panel::_mm_px[2 + sim::MAX_ZOMBIES] = {0};
int16_t panel::_mm_py[2 + sim::MAX_ZOMBIES] = {0};
uint8_t panel::_mm_n = 0;
int16_t panel::_mm_ctx = -1; // camera cell tile whose frame is on the minimap, -1 = none yet
int16_t panel::_mm_cty = -1;

int16_t panel::_mm_x() {
  return (int16_t)display::width() - _mm_w - _mm_gap;
}

void panel::init() {
  const int16_t mx = _mm_x();
  _mm_ctx = -1; // the base repaint wipes the frame and the blips
  _mm_n = 0;
  display::fill_rect(0, render::ARENA_BOTTOM, (int16_t)display::width(), _panel_h, colour::black);

  for (uint8_t r = 0; r < tilemap::ROWS; ++r) { // minimap terrain, same-colour runs
    // flat base colours for the minimap; only the machines have sprite detail
    const int16_t sy = _mm_y + (int16_t)r * _mm_scale;
    const int16_t wy = (int16_t)r * tilemap::TILE;
    int16_t run_x = 0;
    uint16_t run_col = tilemap::color(tilemap::tile_at(0, wy));

    for (uint8_t c = 1; c < tilemap::COLS; ++c) {
      const uint16_t col = tilemap::color(tilemap::tile_at((int16_t)c * tilemap::TILE, wy));
      if (col != run_col) {
        display::fill_rect(mx + run_x * _mm_scale, sy, (int16_t)(c - run_x) * _mm_scale, _mm_scale,
                           run_col);
        run_x = c;
        run_col = col;
      }
    }
    display::fill_rect(mx + run_x * _mm_scale, sy, (int16_t)(tilemap::COLS - run_x) * _mm_scale,
                       _mm_scale, run_col);
  }

  const uint16_t grid = display::rgb565(80, 80, 80); // 3x3 camera cell separators
  for (uint8_t i = 1; i < 3; ++i) {
    display::fill_rect(mx + (int16_t)(i * (tilemap::COLS / 3)) * _mm_scale, _mm_y, 1, _mm_h, grid);
    display::fill_rect(mx, _mm_y + (int16_t)(i * (tilemap::ROWS / 3)) * _mm_scale, _mm_w, 1, grid);
  }
  _mm_n = 0;
}

void panel::_pip_row(int16_t y, const char* label, uint8_t lvl, uint8_t max, uint16_t col) {
  display::text(label, 4, y, colour::white, 1);
  const uint16_t spent = display::rgb565(40, 40, 40);
  for (uint8_t i = 0; i < max; ++i) {
    display::fill_rect(28 + (int16_t)i * 10, y, 8, 8, (i < lvl) ? col : spent);
  }
}

void panel::draw() {
  // stats live on the left of the panel; the HUD carries only the role badge.
  // Every text row is cleared first: numbers and names shrink (POINTS 200 -> 50,
  // SHOTGUN -> SMG) and overpainting alone would leave ghost digits behind.
  const sim::state& v = sim::view();
  const int16_t mx = _mm_x();
  char buf[32];

  display::fill_rect(0, render::ARENA_BOTTOM + 4, mx, 8, colour::black);
  snprintf(buf, sizeof(buf), "POINTS %lu", v.points);
  display::text(buf, 4, render::ARENA_BOTTOM + 4, colour::yellow, 1);

  display::fill_rect(0, render::ARENA_BOTTOM + 14, mx, 8, colour::black);
  snprintf(buf, sizeof(buf), "W%u K%u", v.wave, v.kills);
  display::text(buf, 4, render::ARENA_BOTTOM + 14, colour::white, 1);

  display::fill_rect(0, render::ARENA_BOTTOM + 24, mx, 8, colour::black);
  snprintf(buf, sizeof(buf), "GUN %s", sim::gun_name());
  display::text(buf, 4, render::ARENA_BOTTOM + 24, colour::white, 1);

  // co-op squeezes the rows (8px pitch) to fit the second HP line; solo keeps 10px
  const bool p2 = v.players[1].active;
  const int16_t hp_y = p2 ? render::ARENA_BOTTOM + 32 : render::ARENA_BOTTOM + 34;
  const int16_t tail_y = hp_y + (p2 ? 16 : 10);

  const uint16_t hp_col = (v.players[0].hp <= 2) ? colour::red : colour::green;
  _pip_row(hp_y, "HP", v.players[0].hp, sim::PLAYER_HP_MAX, hp_col);
  if (p2) {
    const uint16_t h2_col = (v.players[1].hp <= 2) ? colour::red : colour::cyan;
    _pip_row(hp_y + 8, "H2", v.players[1].hp, sim::PLAYER_HP_MAX, h2_col);
  }
  _pip_row(tail_y, "DMG", v.dmg_lvl, sim::MAX_LVL, colour::red);
  _pip_row(tail_y + (p2 ? 8 : 10), "SPD", v.spd_lvl, sim::MAX_LVL,
           display::rgb565(60, 130, 230));
}

void panel::_mm_restore_row(int16_t tx0, int16_t tx1, int16_t ty) {
  const int16_t mx = _mm_x();
  const int16_t sy = _mm_y + ty * _mm_scale;
  int16_t run_x = tx0;
  uint16_t run_col = tilemap::color(tilemap::tile_at(tx0 * tilemap::TILE, ty * tilemap::TILE));

  for (int16_t tx = tx0 + 1; tx <= tx1; ++tx) {
    const uint16_t col = tilemap::color(tilemap::tile_at(tx * tilemap::TILE, ty * tilemap::TILE));
    if (col != run_col) {
      display::fill_rect(mx + run_x * _mm_scale, sy, (tx - run_x) * _mm_scale, _mm_scale, run_col);
      run_x = tx;
      run_col = col;
    }
  }
  display::fill_rect(mx + run_x * _mm_scale, sy, (tx1 - run_x + 1) * _mm_scale, _mm_scale, run_col);
}

void panel::_mm_restore_col(int16_t tx, int16_t ty0, int16_t ty1) {
  const int16_t mx = _mm_x() + tx * _mm_scale;
  int16_t run_y = ty0;
  uint16_t run_col = tilemap::color(tilemap::tile_at(tx * tilemap::TILE, ty0 * tilemap::TILE));

  for (int16_t ty = ty0 + 1; ty <= ty1; ++ty) {
    const uint16_t col = tilemap::color(tilemap::tile_at(tx * tilemap::TILE, ty * tilemap::TILE));
    if (col != run_col) {
      display::fill_rect(mx, _mm_y + run_y * _mm_scale, _mm_scale, (ty - run_y) * _mm_scale, run_col);
      run_y = ty;
      run_col = col;
    }
  }
  display::fill_rect(mx, _mm_y + run_y * _mm_scale, _mm_scale, (ty1 - run_y + 1) * _mm_scale, run_col);
}

void panel::_mm_dot(int16_t wx, int16_t wy, uint16_t col) {
  if (_mm_n >= (uint8_t)(2 + sim::MAX_ZOMBIES)) {
    return;
  }
  display::fill_rect(_mm_x() + (wx / tilemap::TILE) * _mm_scale, _mm_y + (wy / tilemap::TILE) * _mm_scale,
                     _mm_scale, _mm_scale, col);
  _mm_px[_mm_n] = wx;
  _mm_py[_mm_n] = wy;
  ++_mm_n;
}

void panel::_mm_frame() {
  // the camera snaps to a whole cell, so the frame always lands on even minimap pixels
  const int16_t cell_w = (int16_t)(display::width() / tilemap::TILE);
  const int16_t cell_h = (int16_t)(render::ARENA_H / tilemap::TILE);
  const int16_t ctx = (int16_t)(render::cam_x() / tilemap::TILE);
  const int16_t cty = (int16_t)(render::cam_y() / tilemap::TILE);

  if (_mm_ctx >= 0) { // put the terrain back under the frame drawn last frame
    _mm_restore_row(_mm_ctx, _mm_ctx + cell_w - 1, _mm_cty);
    _mm_restore_row(_mm_ctx, _mm_ctx + cell_w - 1, _mm_cty + cell_h - 1);
    _mm_restore_col(_mm_ctx, _mm_cty, _mm_cty + cell_h - 1);
    _mm_restore_col(_mm_ctx + cell_w - 1, _mm_cty, _mm_cty + cell_h - 1);
  }

  const int16_t fx = _mm_x() + ctx * _mm_scale;
  const int16_t fy = _mm_y + cty * _mm_scale;
  const int16_t fw = cell_w * _mm_scale;
  const int16_t fh = cell_h * _mm_scale;

  display::fill_rect(fx, fy, fw, 1, colour::yellow);
  display::fill_rect(fx, fy + fh - 1, fw, 1, colour::yellow);
  display::fill_rect(fx, fy, 1, fh, colour::yellow);
  display::fill_rect(fx + fw - 1, fy, 1, fh, colour::yellow);

  _mm_ctx = ctx;
  _mm_cty = cty;
}

void panel::blips() {
  for (uint8_t i = 0; i < _mm_n; ++i) { // restore the terrain under last frame's dots
    const int16_t tx = _mm_px[i] / tilemap::TILE;
    const int16_t ty = _mm_py[i] / tilemap::TILE;
    display::fill_rect(_mm_x() + tx * _mm_scale, _mm_y + ty * _mm_scale, _mm_scale, _mm_scale,
                       tilemap::color(tilemap::tile_at(_mm_px[i], _mm_py[i])));
  }
  _mm_n = 0;

  _mm_frame();

  const sim::state& v = sim::view();
  _mm_dot((int16_t)v.players[0].x, (int16_t)v.players[0].y, colour::white);
  if (v.players[1].active) {
    _mm_dot((int16_t)v.players[1].x, (int16_t)v.players[1].y, colour::cyan);
  }
  for (uint8_t i = 0; i < sim::MAX_ZOMBIES; ++i) {
    if (v.zombies[i].active) {
      uint16_t col = colour::red;
      if (v.zombies[i].kind == sim::actor_kind::runner) {
        col = colour::orange;
      } else if (v.zombies[i].kind == sim::actor_kind::boss) {
        col = colour::purple;
      }
      _mm_dot((int16_t)v.zombies[i].x, (int16_t)v.zombies[i].y, col);
    }
  }
}
