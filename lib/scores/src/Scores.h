#pragma once

#include <cstdint>

// Best score and lifetime kills, in RTC memory so they survive a deep sleep. This is also
// the seam the microSD persistence of P4 slots behind: only the storage inside Scores.cpp
// changes, the calls below do not.
class scores {
  public:
    static void load(); // boot, magic guarded
    static void add_run(uint32_t kills, uint32_t score); // fold a finished run in and save
    static uint32_t best();
    static uint32_t total_kills();
};
