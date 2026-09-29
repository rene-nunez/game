#pragma once

#include <cstdint>

// Best points, lifetime kills and the last runs, in RTC memory so they survive a deep
// sleep, mirrored to /z32.json on the microSD (same TFT SPI bus, CS 22) so they survive
// a power loss. Only the storage inside Points.cpp changes, the calls below do not.
// Best is the wallet at death: earned points minus everything spent in the shops.
class points {
  public:
    static constexpr uint8_t HISTORY_N = 4; // recent runs kept, most recent first

    struct run {
      uint32_t pts;
      uint32_t kills;
      uint8_t wave;
    };

    static void load(); // boot, magic guarded
    static void add_run(uint32_t kills, uint32_t wallet, uint8_t wave); // fold a run in and save
    static uint32_t best();
    static uint32_t total_kills();
    static const run* history(); // HISTORY_N slots, recent first
    static uint8_t history_len(); // runs actually stored, 0..HISTORY_N
};
