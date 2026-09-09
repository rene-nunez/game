#include <Arduino.h>
#include <TFT_eSPI.h>

#include <Display.h>

static TFT_eSPI _tft;

bool display::begin() {
  _tft.init();
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

void display::text(const char* s, int16_t x, int16_t y, uint16_t color,
                   uint8_t size, uint16_t bg) {
  _tft.setCursor(x, y);
  _tft.setTextColor(color, bg);
  _tft.setTextSize(size);
  _tft.print(s);
}

uint16_t display::rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
