#pragma once

#include <cstdint>

// Best points and lifetime kills, in RTC memory so they survive a deep sleep. This is also
// the seam the microSD persistence of P4 slots behind: only the storage inside Points.cpp
// changes, the calls below do not. Best is the wallet at death: earned points minus
// everything spent in the shops.
class points {
  public:
    static void load(); // boot, magic guarded
    static void add_run(uint32_t kills, uint32_t wallet); // fold a finished run in and save
    static uint32_t best();
    static uint32_t total_kills();
};
