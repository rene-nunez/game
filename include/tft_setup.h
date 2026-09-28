#pragma once

#include <pins.h>

#define ST7789_DRIVER
#define TFT_INVERSION_OFF
#define TFT_RGB_ORDER TFT_BGR // this glass is BGR: without it R and B swap
                              // (cyan role reads yellow, brass roulette blue,
// timber walls blue-grey, teal lake yellow)
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define LOAD_GLCD
#define SPI_FREQUENCY 40000000
