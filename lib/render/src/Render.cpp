#include <cstdio>

#include <Display.h>
#include <map.h>
#include <Sim.h>

#include "Render.h"

int16_t render::_cam_x = 0;
int16_t render::_cam_y = 0;
uint8_t render::_focus = 0;
int16_t render::_paint_y = render::ARENA_H;
const char* render::_prompt = nullptr;

void render::prompt(const char* msg) {
  _prompt = msg;
}

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

void render::set_focus(uint8_t p) {
  _focus = (p < sim::NUM_PLAYERS) ? p : 0;
  _cam_x = -1; // force the camera to snap on the next update
  _cam_y = -1;
}

void render::update_camera() {
  const int16_t aw = (int16_t)display::width();
  const sim::state& v = sim::view();
  // each board frames its own player, so co-op splits across districts freely. A downed
  // focus still frames its body (spectate the rescue); only an inactive focus falls back.
  // A bled-out body (active, hp 0, not downed) spectates the living partner until the
  // next wave respawns it: the rule is per frame, so the camera returns on its own.
  uint8_t f = _focus;
  const uint8_t o = (f == 0) ? 1 : 0;
  const bool f_dead = v.players[f].active && !v.players[f].downed && v.players[f].hp == 0;
  const bool o_out = v.players[o].active && !v.players[o].downed && v.players[o].hp > 0;
  if (f_dead && o_out) {
    f = o;
  }
  if (!v.players[f].active) {
    f = (f == 0) ? 1 : 0;
  }
  if (!v.players[f].active) {
    f = 0;
  }
  const int16_t pcx = (int16_t)(v.players[f].x + sim::PLAYER_SIZE / 2);
  const int16_t pcy = (int16_t)(v.players[f].y + sim::PLAYER_SIZE / 2);

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
  _erase_world_area(wx, wy, size, size);
}

void render::_erase_world_area(int16_t wx, int16_t wy, int16_t w, int16_t h) {
  for (int16_t dy = 0; dy < h; ++dy) {
    const int16_t wyy = wy + dy;
    const int16_t sy = wyy - _cam_y + HUD_H;
    if (sy < HUD_H || sy >= ARENA_BOTTOM) {
      continue;
    }

    int16_t run_x = wx;
    uint16_t run_col = tilemap::color_at(wx, wyy);
    for (int16_t dx = 1; dx < w; ++dx) {
      const uint16_t col = tilemap::color_at(wx + dx, wyy);
      if (col != run_col) {
        _fill_world_run(run_x, sy, (int16_t)(wx + dx - run_x), run_col);
        run_x = wx + dx;
        run_col = col;
      }
    }
    _fill_world_run(run_x, sy, (int16_t)(wx + w - run_x), run_col);
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
  for (uint8_t p = 0; p < sim::NUM_PLAYERS; ++p) {
    if (v.players[p].active) {
      _erase_world_rect((int16_t)v.players[p].x, (int16_t)v.players[p].y, sim::PLAYER_SIZE);
    }
  }
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
  _shop_labels(true); // erase last frame's price tags at the old camera
  // erase the prompt strip at the old camera: it is screen-fixed, so its world
  // rect moves with the camera and a camera cut would strand it otherwise
  _erase_world_area(_cam_x, _cam_y + ARENA_H - _prompt_h, (int16_t)display::width(), _prompt_h);
}

// price tags over the shop machines, anchored to the world so they pan with the
// camera. HEAL/ROLL are fixed; DMG/SPD show the live next-level price (or MAX).
// Drawn every frame after the terrain, erased via the tilemap like sprites. The
// erase always covers the widest tag (8 chars): a buy can shrink the text and a
// tight erase would strand the old pixels for a frame.
void render::_shop_labels(bool erase) {
  struct _tag {
    uint8_t tile;
    const char* text;
    uint16_t col;
  };
  char dmg_buf[12], spd_buf[12];
  const sim::state& v = sim::view();
  if (v.dmg_lvl >= sim::MAX_LVL) {
    snprintf(dmg_buf, sizeof(dmg_buf), "DMG MAX");
  } else {
    snprintf(dmg_buf, sizeof(dmg_buf), "DMG %lu",
             (unsigned long)sim::price_for(sim::PRICE_DMG, v.dmg_lvl));
  }
  if (v.spd_lvl >= sim::MAX_LVL) {
    snprintf(spd_buf, sizeof(spd_buf), "SPD MAX");
  } else {
    snprintf(spd_buf, sizeof(spd_buf), "SPD %lu",
             (unsigned long)sim::price_for(sim::PRICE_SPD, v.spd_lvl));
  }
  const _tag tags[] = {
      {tilemap::VENDING, "HEAL 100", colour::green},
      {tilemap::V_DMG, dmg_buf, colour::red},
      {tilemap::V_SPD, spd_buf, colour::cyan},
      {tilemap::ROULETTE, "ROLL 100", colour::yellow},
  };
  for (uint8_t ti = 0; ti < 4; ++ti) {
    const uint8_t want = tags[ti].tile;
    for (uint8_t r = 0; r < tilemap::ROWS; ++r) {
      for (uint8_t c = 0; c < tilemap::COLS; ++c) {
        if (tilemap::tiles[r][c] != want) {
          continue;
        }
        // top-left tile of the 2x2 block only, so the tag paints once per machine
        if (c > 0 && tilemap::tiles[r][c - 1] == want) {
          continue;
        }
        if (r > 0 && tilemap::tiles[r - 1][c] == want) {
          continue;
        }
        uint8_t len = 0;
        while (tags[ti].text[len] != '\0') {
          ++len;
        }
        // the block centre never moves, so erase and draw share it; only the
        // width differs (erase always covers the widest tag, see above)
        const int16_t cx = (int16_t)c * tilemap::TILE + tilemap::TILE;
        const int16_t wy = (int16_t)r * tilemap::TILE - 10; // 8px glyph + 2px gap
        if (erase) {
          _erase_world_area(cx - _tag_max_w / 2, wy, _tag_max_w, 8);
          continue;
        }
        const int16_t tw = (int16_t)len * 6; // size-1 glyphs are 6px wide
        const int16_t wx = cx - tw / 2;
        const int16_t sx = wx - _cam_x;
        const int16_t sy = wy - _cam_y + HUD_H;
        if (sx < 0 || sy < HUD_H || sx + tw > (int16_t)display::width() ||
            sy + 8 > ARENA_BOTTOM) {
          continue; // partially off-arena: skip rather than bleed into hud/panel
        }
        display::text(tags[ti].text, sx, sy, tags[ti].col, 1);
      }
    }
  }
}

// flat actors: one box per entity, nothing to ghost on erase.
// The HUD carries no render text: game draws only the role badge up there,
// stats live in the panel.
void render::draw() {
  repaint_step(); // terrain first, so a cut never paints over a live sprite

  const sim::state& v = sim::view();
  for (uint8_t p = 0; p < sim::NUM_PLAYERS; ++p) {
    if (!v.players[p].active) {
      continue;
    }
    if (v.players[p].downed) {
      _fill_world_box((int16_t)v.players[p].x, (int16_t)v.players[p].y, sim::PLAYER_SIZE,
                      colour::yellow); // body to rescue
    } else if (v.players[p].hp > 0) {
      _fill_world_box((int16_t)v.players[p].x, (int16_t)v.players[p].y, sim::PLAYER_SIZE,
                      p == 0 ? colour::blue : colour::cyan);
    }
  }
  for (uint8_t i = 0; i < sim::MAX_ZOMBIES; ++i) {
    if (v.zombies[i].active) {
      uint16_t col = colour::red;
      if (v.zombies[i].kind == sim::actor_kind::runner) {
        col = colour::orange;
      } else if (v.zombies[i].kind == sim::actor_kind::boss) {
        col = colour::purple;
      }
      _fill_world_box((int16_t)v.zombies[i].x, (int16_t)v.zombies[i].y, sim::ZOMBIE_SIZE, col);
    }
  }
  for (uint8_t i = 0; i < sim::MAX_BULLETS; ++i) {
    if (v.bullets[i].active) {
      _fill_world_box((int16_t)v.bullets[i].x, (int16_t)v.bullets[i].y, sim::BULLET_SIZE,
                      colour::white);
    }
  }
  _shop_labels(false);
  if (_prompt != nullptr && _prompt[0] != '\0') {
    uint8_t len = 0;
    while (_prompt[len] != '\0') {
      ++len;
    }
    const int16_t tw = (int16_t)len * 6; // size-1 glyphs are 6px wide
    const int16_t sw = (int16_t)display::width();
    const int16_t sx = (sw - tw) / 2; // centred; the strip is always fully on screen
    display::text(_prompt, sx < 0 ? 0 : sx, ARENA_BOTTOM - _prompt_h + 1, colour::yellow, 1);
  }
}
