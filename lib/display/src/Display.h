#pragma once

#include <cstdint>

namespace colour {
  constexpr uint16_t black = 0x0000;
  constexpr uint16_t white = 0xFFFF;
  constexpr uint16_t red = 0xF800;
  constexpr uint16_t green = 0x07E0;
  constexpr uint16_t blue = 0x001F;
  constexpr uint16_t yellow = 0xFFE0;
  constexpr uint16_t cyan = 0x07FF;
  constexpr uint16_t orange = 0xFD20; // runner
  constexpr uint16_t purple = 0x781F; // boss
  constexpr uint16_t gray = 0xAD55;   // dim menu rows and hints
  constexpr uint16_t lime = 0xB7E0;   // toxic menu titles and cursor
}

class display {
  public:
    static bool begin();
    static uint16_t width();
    static uint16_t height();
    static void clear(uint16_t color);
    static void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    static void pixel(int16_t x, int16_t y, uint16_t color);
    static void text(const char* s, int16_t x, int16_t y, uint16_t color, uint8_t size = 1, uint16_t bg = colour::black);
    static void draw_sprite(int16_t x, int16_t y, uint8_t w, uint8_t h,
                            const uint16_t* data); // PROGMEM RGB565, 0x0000 transparent
    static void draw_sprite_hflip(int16_t x, int16_t y, uint8_t w, uint8_t h,
                                  const uint16_t* data); // mirrored columns, same format
    static void backlight(bool on);
    static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
};
