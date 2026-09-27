#include <Arduino.h>
#include <cstring>

#include <Display.h>
#include <Scores.h>
#include <Sim.h>

#include "Screens.h"

namespace {
  // menu rows: the full paint and the cursor repaint must agree on where a row is, so both
  // read these. _item takes an absolute y; nothing may add the row pitch twice
  constexpr int16_t _menu_x = 16, _menu_y = 56, _menu_row = 16;
  constexpr int16_t _over_x = 24, _over_y = 110; // the game over screen is indented

  const char* const _menu_items[] = { "Start Game", "Scores", "Exit" };
  const char* const _mode_items[] = { "Solo", "Multiplayer", "Back" };
  const char* const _pause_items[] = { "Continue", "Restart", "Exit to Menu" };
  const char* const _over_items[] = { "Restart", "Menu" };

  struct _list {
    const char* title; // null when the screen has no chrome at all, i.e. playing
    const char* const* items;
    uint8_t count;
    bool indented; // game over sits further right
  };

  // indexed by screens::id, so the order here is the enum order
  const _list _tables[] = {
    {"ZOMBIES", _menu_items, 3, false},   // menu
    {"GAME MODE", _mode_items, 3, false}, // mode
    {"SCORES", nullptr, 0, false},        // scores
    {nullptr, nullptr, 0, false},         // playing
    {"PAUSED", _pause_items, 3, false},   // pause
    {"GAME OVER", _over_items, 2, true},  // game over
  };
  static_assert(sizeof(_tables) / sizeof(_tables[0]) == 6, "one row per screens::id");

  const _list& _table(screens::id scr) {
    return _tables[(uint8_t)scr];
  }

  int16_t _row_x(const _list& t) {
    return t.indented ? _over_x : _menu_x;
  }

  int16_t _row_y0(const _list& t) {
    return t.indented ? _over_y : _menu_y;
  }

  uint8_t _painted_scr = 0xFF; // screen the chrome was last painted for, 0xFF = none
  uint8_t _painted_sel = 0;    // selection currently on screen

  void _item(const char* const* items, uint8_t i, bool selected, int16_t x, int16_t y) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%c %s", selected ? '>' : ' ', items[i]);
    display::text(buf, x, y, selected ? colour::green : colour::white, 1);
  }

  void _background(const char* title, uint16_t col) {
    display::fill_rect(0, 0, display::width(), display::height(), colour::black);
    display::text(title, (display::width() - 6 * (int16_t)strlen(title) * 2) / 2, 24, col, 2);
  }

  void _list_rows(const _list& t, uint8_t sel) {
    for (uint8_t i = 0; i < t.count; ++i) {
      _item(t.items, i, i == sel, _row_x(t), _row_y0(t) + (int16_t)i * _menu_row);
    }
    display::text("JOY: move   FIRE: select", _row_x(t), _row_y0(t) + (int16_t)t.count * _menu_row + 24,
                  colour::white, 1);
  }

  void _full_menu(const _list& t, uint8_t sel) {
    _background(t.title, colour::yellow);
    _list_rows(t, sel);
  }

  void _full_scores() {
    _background("SCORES", colour::yellow);

    char buf[32];
    snprintf(buf, sizeof(buf), "Best score: %lu", scores::best());
    display::text(buf, 16, 60, colour::white, 1);
    snprintf(buf, sizeof(buf), "Total kills: %lu", scores::total_kills());
    display::text(buf, 16, 76, colour::white, 1);

    display::text("FIRE/PAUSE: back", 16, 120, colour::white, 1);
  }

  void _full_game_over(uint8_t sel) {
    _background("GAME OVER", colour::red);

    char buf[32];
    const sim::state& v = sim::view();
    snprintf(buf, sizeof(buf), "Score: %lu   Best: %lu", v.score, scores::best());
    display::text(buf, 24, 60, colour::white, 1);
    snprintf(buf, sizeof(buf), "Wave: %u  Kills: %u", v.wave, v.kills);
    display::text(buf, 24, 76, colour::white, 1);

    _list_rows(_table(screens::id::game_over), sel);
  }
} // namespace

uint8_t screens::count(id scr) {
  return _table(scr).count;
}

void screens::invalidate() {
  _painted_scr = 0xFF;
}

// A full-screen fill is 320*240*2 = 153600 bytes, ~31ms of SPI at 40MHz, so repainting it every
// frame both blew the 33ms budget and tore against the panel scan-out: that was the line
// sweeping corner to corner. Paint the chrome once per screen entry, then only the two cursor
// lines when the selection moves.
void screens::paint(id scr, uint8_t sel) {
  const _list& t = _table(scr);
  if (!t.title) {
    return; // playing draws the arena, not a menu
  }
  if (t.count > 0 && sel >= t.count) {
    return; // count guards sel
  }

  if (_painted_scr != (uint8_t)scr) {
    switch (scr) {
      case id::scores: _full_scores(); break;
      case id::game_over: _full_game_over(sel); break;
      default: _full_menu(t, sel); break;
    }
    _painted_scr = (uint8_t)scr;
    _painted_sel = sel;
    return;
  }

  if (sel == _painted_sel) {
    return; // nothing moved
  }

  _item(t.items, _painted_sel, false, _row_x(t), _row_y0(t) + (int16_t)_painted_sel * _menu_row);
  _item(t.items, sel, true, _row_x(t), _row_y0(t) + (int16_t)sel * _menu_row);
  _painted_sel = sel;
}
