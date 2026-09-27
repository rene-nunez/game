#include <Arduino.h>

#include "Scores.h"

namespace {
  constexpr uint32_t _MAGIC = 0x5A3C21EDu;

  struct _rtc_scores {
    uint32_t magic;
    uint32_t best;
    uint32_t total_kills;
  };

  RTC_NOINIT_ATTR _rtc_scores _rtc;

  uint32_t _best = 0;
  uint32_t _total_kills = 0;
}

void scores::load() {
  if (_rtc.magic == _MAGIC) {
    _best = _rtc.best;
    _total_kills = _rtc.total_kills;
  } else {
    _best = 0;
    _total_kills = 0;
    _rtc.magic = _MAGIC;
    _rtc.best = 0;
    _rtc.total_kills = 0;
  }
}

void scores::add_run(uint32_t kills, uint32_t score) {
  _total_kills += kills;
  if (score > _best) {
    _best = score;
  }
  _rtc.best = _best;
  _rtc.total_kills = _total_kills;
}

uint32_t scores::best() {
  return _best;
}

uint32_t scores::total_kills() {
  return _total_kills;
}
