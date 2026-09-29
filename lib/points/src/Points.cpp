#include <Arduino.h>

#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>

#include <pins.h>

#include "Points.h"

namespace {
  constexpr uint32_t _MAGIC = 0x5A3C21EDu;
  constexpr const char* _PATH = "/z32.json";

  struct _rtc_points {
    uint32_t magic;
    uint32_t best;
    uint32_t total_kills;
  };

  RTC_NOINIT_ATTR _rtc_points _rtc;

  uint32_t _best = 0;
  uint32_t _total_kills = 0;
  bool _sd_ready = false;

  // shares the TFT SPI bus (23/19/18, default VSPI pins) with the dedicated CS 22.
  // Best-effort: a missing card only logs, the RTC mirror keeps the game going.
  bool _sd_mount() {
    if (_sd_ready) {
      return true;
    }
    _sd_ready = SD.begin(SD_CS);
    if (!_sd_ready) {
      Serial.println("[points] no sd, rtc only");
    }
    return _sd_ready;
  }

  void _sd_load_merge() {
    if (!_sd_mount()) {
      return;
    }
    File f = SD.open(_PATH, FILE_READ);
    if (!f) {
      Serial.println("[points] no save file");
      return;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.println("[points] save corrupt");
      return;
    }
    const uint32_t sb = doc["best"] | 0u;
    const uint32_t sk = doc["total_kills"] | 0u;
    if (sb > _best) {
      _best = sb;
    }
    if (sk > _total_kills) {
      _total_kills = sk;
    }
  }

  void _sd_save() {
    if (!_sd_mount()) {
      return;
    }
    SD.remove(_PATH); // FILE_WRITE appends, so truncate first
    File f = SD.open(_PATH, FILE_WRITE);
    if (!f) {
      Serial.println("[points] save open failed");
      return;
    }
    JsonDocument doc;
    doc["best"] = _best;
    doc["total_kills"] = _total_kills;
    if (!serializeJson(doc, f)) {
      Serial.println("[points] save write failed");
    }
    f.close();
  }
}

void points::load() {
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
  _sd_load_merge(); // power-loss recovery: the card only ever merges upward
  _rtc.best = _best;
  _rtc.total_kills = _total_kills;
}

void points::add_run(uint32_t kills, uint32_t wallet) {
  _total_kills += kills;
  if (wallet > _best) {
    _best = wallet;
  }
  _rtc.best = _best;
  _rtc.total_kills = _total_kills;
  _sd_save(); // once per death, cheap enough to mount+write here
}

uint32_t points::best() {
  return _best;
}

uint32_t points::total_kills() {
  return _total_kills;
}
