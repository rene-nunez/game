#include <Display.h>
#include <map.h>
#include <Sim.h>

#include "Render.h"

int16_t render::_cam_x = 0;
int16_t render::_cam_y = 0;
int16_t render::_paint_y = render::ARENA_H;

int16_t render::cam_x() {
  return _cam_x;
}

int16_t render::cam_y() {
  return _cam_y;
}

void render::repaint() {
  display::fill_rect(0, 0, (int16_t)display::width(), HUD_H, colour::black); // hud strip
  _paint_y = 0; // the arena repaint runs from here, PAINT_CHUNK rows per frame
}

void render::repaint_step() {
  if (_paint_y >= ARENA_H) {
    return; // nothing pending
  }

  const int16_t sw = (int16_t)display::width();
  const int16_t end = (_paint_y + PAINT_CHUNK < ARENA_H) ? _paint_y + PAINT_CHUNK : ARENA_H;

  for (int16_t ry = _paint_y; ry < end; ++ry) { // merge same-colour runs per row
    const int16_t wy = _cam_y + ry;
    const int16_t sy = HUD_H + ry;
    int16_t run_x = 0;
    uint16_t run_col = tilemap::color_at(_cam_x, wy);

    for (int16_t rx = 1; rx < sw; ++rx) {
      const uint16_t col = tilemap::color_at(_cam_x + rx, wy);
      if (col != run_col) {
        display::fill_rect(run_x, sy, rx - run_x, 1, run_col);
        run_x = rx;
        run_col = col;
      }
    }
    display::fill_rect(run_x, sy, sw - run_x, 1, run_col);
  }

  _paint_y = end;
}

int16_t render::_cell_cam(int16_t p, int16_t step, int16_t max_cam) {
  int16_t cam = (int16_t)((p / step) * step);
  if (cam < 0) {
    cam = 0;
  } else if (cam > max_cam) {
    cam = max_cam;
  }
  return cam;
}

void render::update_camera() {
  const int16_t aw = (int16_t)display::width();
  const sim::state& v = sim::view();
  const int16_t pcx = (int16_t)(v.player.x + sim::PLAYER_SIZE / 2);
  const int16_t pcy = (int16_t)(v.player.y + sim::PLAYER_SIZE / 2);

  const int16_t cx = _cell_cam(pcx, aw, (int16_t)(tilemap::WORLD_W - aw));
  const int16_t cy = _cell_cam(pcy, ARENA_H, (int16_t)(tilemap::WORLD_H - ARENA_H));

  if (cx == _cam_x && cy == _cam_y) {
    return;
  }
  _cam_x = cx;
  _cam_y = cy;
  repaint(); // the new screen must be repainted from the tilemap
}

void render::_fill_world_run(int16_t wx, int16_t sy, int16_t w, uint16_t col) {
  if (w <= 0 || sy < HUD_H || sy >= ARENA_BOTTOM) {
    return;
  }
  const int16_t sw = (int16_t)display::width();
  int16_t x0 = wx - _cam_x;
  int16_t x1 = x0 + w;
  if (x1 <= 0 || x0 >= sw) {
    return; // fully off-viewport
  }
  if (x0 < 0) {
    x0 = 0;
  }
  if (x1 > sw) {
    x1 = sw;
  }
  display::fill_rect(x0, sy, x1 - x0, 1, col);
}

void render::_erase_world_rect(int16_t wx, int16_t wy, uint8_t size) {
  for (int16_t dy = 0; dy < (int16_t)size; ++dy) {
    const int16_t wyy = wy + dy;
    const int16_t sy = wyy - _cam_y + HUD_H;
    if (sy < HUD_H || sy >= ARENA_BOTTOM) {
      continue;
    }

    int16_t run_x = wx;
    uint16_t run_col = tilemap::color_at(wx, wyy);
    for (int16_t dx = 1; dx < (int16_t)size; ++dx) {
      const uint16_t col = tilemap::color_at(wx + dx, wyy);
      if (col != run_col) {
        _fill_world_run(run_x, sy, (int16_t)(wx + dx - run_x), run_col);
        run_x = wx + dx;
        run_col = col;
      }
    }
    _fill_world_run(run_x, sy, (int16_t)(wx + size - run_x), run_col);
  }
}

void render::_fill_world_box(int16_t wx, int16_t wy, uint8_t size, uint16_t col) {
  const int x0 = wx - _cam_x;
  const int y0 = wy - _cam_y + HUD_H;
  // clip to the arena so a half-sprite never bleeds into the hud strip nor the panel
  const int cx0 = x0 > 0 ? x0 : 0;
  const int cy0 = y0 > HUD_H ? y0 : HUD_H;
  const int cx1 = x0 + size < (int)display::width() ? x0 + size : (int)display::width();
  const int cy1 = y0 + size < (int)ARENA_BOTTOM ? y0 + size : (int)ARENA_BOTTOM;

  if (cx0 < cx1 && cy0 < cy1) {
    display::fill_rect(cx0, cy0, cx1 - cx0, cy1 - cy0, col);
  }
}

void render::clear() {
  const sim::state& v = sim::view();
  _erase_world_rect((int16_t)v.player.x, (int16_t)v.player.y, sim::PLAYER_SIZE);
  for (uint8_t i = 0; i < sim::MAX_ZOMBIES; ++i) {
    if (v.zombies[i].active) {
      _erase_world_rect((int16_t)v.zombies[i].x, (int16_t)v.zombies[i].y, sim::ZOMBIE_SIZE);
    }
  }
  for (uint8_t i = 0; i < sim::MAX_BULLETS; ++i) {
    if (v.bullets[i].active) {
      _erase_world_rect((int16_t)v.bullets[i].x, (int16_t)v.bullets[i].y, sim::BULLET_SIZE);
    }
  }
}

// flat actors: one box per entity, nothing to ghost on erase.
// The HUD carries no render text: game draws only the role badge up there,
// stats live in the panel.
void render::draw() {
  repaint_step(); // terrain first, so a cut never paints over a live sprite

  const sim::state& v = sim::view();
  _fill_world_box((int16_t)v.player.x, (int16_t)v.player.y, sim::PLAYER_SIZE, colour::blue);
  for (uint8_t i = 0; i < sim::MAX_ZOMBIES; ++i) {
    if (v.zombies[i].active) {
      _fill_world_box((int16_t)v.zombies[i].x, (int16_t)v.zombies[i].y, sim::ZOMBIE_SIZE,
                      colour::red);
    }
  }
  for (uint8_t i = 0; i < sim::MAX_BULLETS; ++i) {
    if (v.bullets[i].active) {
      _fill_world_box((int16_t)v.bullets[i].x, (int16_t)v.bullets[i].y, sim::BULLET_SIZE,
                      colour::white);
    }
  }
}
