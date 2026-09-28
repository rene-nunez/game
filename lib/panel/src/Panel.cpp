#include <Arduino.h>

#include <Display.h>
#include <map.h>
#include <Render.h>
#include <Sim.h>

#include "Panel.h"

int16_t panel::_mm_px[1 + sim::MAX_ZOMBIES] = {0};
int16_t panel::_mm_py[1 + sim::MAX_ZOMBIES] = {0};
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

void panel::draw() {
  // stats live on the left of the panel; the HUD carries only the role badge.
  const sim::state& v = sim::view();
  char buf[32];

  snprintf(buf, sizeof(buf), "SCORE %lu", v.score);
  display::text(buf, 4, render::ARENA_BOTTOM + 4, colour::yellow, 1);
  snprintf(buf, sizeof(buf), "WAVE %u", v.wave);
  display::text(buf, 4, render::ARENA_BOTTOM + 16, colour::white, 1);
  snprintf(buf, sizeof(buf), "KILLS %u", v.kills);
  display::text(buf, 4, render::ARENA_BOTTOM + 28, colour::white, 1);

  display::text("HP", 4, render::ARENA_BOTTOM + 40, colour::white, 1);
  const uint16_t live = (v.player_hp <= 2) ? colour::red : colour::green;
  const uint16_t spent = display::rgb565(40, 40, 40);
  for (uint8_t i = 0; i < sim::PLAYER_HP_MAX; ++i) {
    display::fill_rect(22 + (int16_t)i * 10, render::ARENA_BOTTOM + 40, 8, 8, (i < v.player_hp) ? live : spent);
  }
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
  if (_mm_n >= (uint8_t)(1 + sim::MAX_ZOMBIES)) {
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
  _mm_dot((int16_t)v.player.x, (int16_t)v.player.y, colour::white);
  for (uint8_t i = 0; i < sim::MAX_ZOMBIES; ++i) {
    if (v.zombies[i].active) {
      _mm_dot((int16_t)v.zombies[i].x, (int16_t)v.zombies[i].y, colour::red);
    }
  }
}
