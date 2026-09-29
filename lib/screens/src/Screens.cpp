#include <Arduino.h>
#include <cstring>

#include <Display.h>
#include <Points.h>
#include <Sim.h>

#include "Screens.h"

namespace {
  // menu rows: the full paint and the cursor repaint must agree on where a row is, so both
  // read these. _item takes an absolute y; nothing may add the row pitch twice.
  // The chrome sits centred: title at 64, first row at 96 on the 240px glass.
  constexpr int16_t _menu_x = 16, _menu_y = 96, _menu_row = 16;
  constexpr int16_t _over_x = 24, _over_y = 128; // the game over screen is indented

  const char* const _menu_items[] = { "Start Game", "Points", "Exit" };
  const char* const _mode_items[] = { "Solo", "Multiplayer", "Back" };
  const char* const _pause_items[] = { "Continue", "Restart", "Exit to Menu" };
  const char* const _over_items[] = { "Restart", "Menu" };
  // ASCII only: the TFT font has no glyphs for accents or ñ
  const char* const _team_names[] = {
    "Rene Nunez", "David Fajardo", "Angel Terriquez", "Mario Vargas", "Victor Castillo",
  };

  struct _list {
    const char* title; // null when the screen has no chrome at all, i.e. playing
    const char* const* items;
    uint8_t count;
    bool indented; // game over sits further right
  };

  // indexed by screens::id, so the order here is the enum order
  const _list _tables[] = {
    {"z32", nullptr, 0, false},            // logo (custom big paint, no items)
    {"TEAM", nullptr, 0, false},           // team (custom name list, no items)
    {"z32", _menu_items, 3, false},        // menu
    {"GAME MODE", _mode_items, 3, false},  // mode
    {"POINTS", nullptr, 0, false},         // points
    {nullptr, nullptr, 0, false},          // playing
    {"PAUSED", _pause_items, 3, false},  // pause
    {"GAME OVER", _over_items, 2, true},   // game over
    {"WAITING", nullptr, 0, false},        // waiting (custom peer text, no items)
  };
  static_assert(sizeof(_tables) / sizeof(_tables[0]) == 9, "one row per screens::id");

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
    display::text(title, (display::width() - 6 * (int16_t)strlen(title) * 2) / 2, 64, col, 2);
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

  void _full_logo() {
    display::fill_rect(0, 0, display::width(), display::height(), colour::black);
    // "z32" huge and centred: 3 glyphs of 6x8 at size 6 -> 108x48
    constexpr uint8_t size = 6;
    display::text("z32", (display::width() - 3 * 6 * size) / 2, (display::height() - 8 * size) / 2,
                  colour::red, size);
  }

  void _full_team() {
    _background("TEAM", colour::cyan);

    constexpr uint8_t names = sizeof(_team_names) / sizeof(_team_names[0]);
    for (uint8_t i = 0; i < names; ++i) {
      const int16_t w = 6 * (int16_t)strlen(_team_names[i]);
      display::text(_team_names[i], (display::width() - w) / 2, _menu_y + (int16_t)i * _menu_row,
                    colour::white, 1);
    }
    // no footer: both intro screens advance alone (FIRE just hurries them)
  }

  void _full_points() {
    _background("POINTS", colour::yellow);

    char buf[32];
    snprintf(buf, sizeof(buf), "Best points: %lu", points::best());
    display::text(buf, 16, 100, colour::white, 1);
    snprintf(buf, sizeof(buf), "Total kills: %lu", points::total_kills());
    display::text(buf, 16, 116, colour::white, 1);

    const uint8_t n = points::history_len();
    const points::run* h = points::history();
    if (n == 0) {
      display::text("no runs yet", 16, 132, colour::white, 1);
    }
    for (uint8_t i = 0; i < n && i < points::HISTORY_N; ++i) {
      snprintf(buf, sizeof(buf), "R%u P%lu K%lu W%u", i + 1, h[i].pts, h[i].kills, h[i].wave);
      display::text(buf, 16, (int16_t)(132 + i * 16), colour::white, 1);
    }

    display::text("FIRE/PAUSE: back", 16, 200, colour::white, 1);
  }

  void _full_waiting() {
    _background("WAITING", colour::cyan);
    display::text("for peer ...", 16, 100, colour::white, 1);
    display::text("FIRE: solo   PAUSE: back", 16, 160, colour::white, 1);
  }

  void _full_game_over(uint8_t sel) {
    _background("GAME OVER", colour::red);

    char buf[32];
    const sim::state& v = sim::view();
    snprintf(buf, sizeof(buf), "Points: %lu   Best: %lu", v.points, points::best());
    display::text(buf, 36, 96, colour::white, 1);
    snprintf(buf, sizeof(buf), "Wave: %u  Kills: %u", v.wave, v.kills);
    display::text(buf, 36, 112, colour::white, 1);

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
      case id::logo: _full_logo(); break;
      case id::team: _full_team(); break;
      case id::points: _full_points(); break;
      case id::game_over: _full_game_over(sel); break;
      case id::waiting: _full_waiting(); break;
      default: _full_menu(t, sel); break;
    }
    _painted_scr = (uint8_t)scr;
    _painted_sel = sel;
    return;
  }

  if (sel == _painted_sel || t.count == 0) {
    return; // nothing moved, or a chrome screen with no cursor (logo/team/points)
  }

  _item(t.items, _painted_sel, false, _row_x(t), _row_y0(t) + (int16_t)_painted_sel * _menu_row);
  _item(t.items, sel, true, _row_x(t), _row_y0(t) + (int16_t)sel * _menu_row);
  _painted_sel = sel;
}
