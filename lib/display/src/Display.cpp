#include <Arduino.h>
#include <TFT_eSPI.h>
#include <pgmspace.h>

#include "Display.h"
#include "pins.h"

static TFT_eSPI _tft;

bool display::begin() {
  _tft.init();

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  _tft.setRotation(1); // landscape 320x240
  _tft.fillScreen(colour::black);

  return true;
}

uint16_t display::width() {
  return _tft.width();
}

uint16_t display::height() {
  return _tft.height();
}

void display::clear(uint16_t color) {
  _tft.fillScreen(color);
}

void display::fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  _tft.fillRect(x, y, w, h, color);
}

void display::pixel(int16_t x, int16_t y, uint16_t color) {
  _tft.drawPixel(x, y, color);
}

void display::text(const char* s, int16_t x, int16_t y, uint16_t color, uint8_t size, uint16_t bg) {
  _tft.setCursor(x, y);
  _tft.setTextColor(color, bg);
  _tft.setTextSize(size);
  _tft.print(s);
}

void display::backlight(bool on) {
  digitalWrite(TFT_BL, on ? HIGH : LOW);
}

void display::draw_sprite(int16_t x, int16_t y, uint8_t w, uint8_t h, const uint16_t* data) {
  // same-colour runs straight to the glass; fillRect clips off-arena rows itself
  for (uint8_t row = 0; row < h; ++row) {
    const int16_t sy = y + (int16_t)row;
    int16_t run_x = -1;
    uint16_t run_col = 0;
    for (uint8_t col = 0; col <= w; ++col) {
      const uint16_t c = (col < w) ? pgm_read_word(data + (uint16_t)row * w + col) : 0x0000;
      if (run_x >= 0 && c == run_col) {
        continue; // same colour extends (the transparent sentinel always flushes)
      }
      if (run_x >= 0) {
        _tft.fillRect(x + run_x, sy, (int16_t)col - run_x, 1, run_col);
      }
      if (c != 0x0000) {
        run_x = (int16_t)col;
        run_col = c;
      } else {
        run_x = -1;
      }
    }
  }
}

uint16_t display::rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
