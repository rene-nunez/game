#include <Arduino.h>
#include <esp_system.h>
#include <cstring>
#include <cmath>

#include <Display.h>
#include "Game.h"

handler game::_handler;
uint32_t game::_tick = 0;
uint32_t game::_peer_tick = 0;

uint16_t game::_grass = 0;
game::_player_data game::_player;
uint32_t game::_last_ms = 0;

game::_zombie game::_zombies[game::_max_zombies];
game::_bullet game::_bullets[game::_max_bullets];
uint8_t game::_player_hp = 0;
uint8_t game::_wave = 0;
uint8_t game::_kills = 0;
uint32_t game::_last_shot = 0;
uint32_t game::_last_damage = 0;
bool game::_game_over = false;

bool game::begin(uint8_t role) {
  Serial.begin(115200);
  delay(200);

  if (!_handler.begin(role)) {
    Serial.println("[game] network init failed, resetting...");
    delay(1000);
    ESP.restart();
    return false;
  }

  _handler.on_message(msg_type::heartbeat, _on_heartbeat);

  if (!display::begin()) {
    Serial.println("[game] display init failed, resetting...");
    delay(1000);
    ESP.restart();
    return false;
  }

  if (!input::begin()) {
    Serial.println("[game] input init failed, resetting...");
    delay(1000);
    ESP.restart();
    return false;
  }

  const uint16_t iw = display::width();
  const uint16_t ih = display::height();

  _grass = display::rgb565(38, 62, 38);

  display::fill_rect(0, 0, iw, _hud_h, colour::black);    // hud strip
  display::fill_rect(0, _hud_h, iw, ih - _hud_h, _grass); // arena

  _restart();
  _last_ms = millis();

  Serial.println("[game] ready");
  return true;
}

void game::update() {
  input::update();

  heartbeat_msg hb;
  hb.tick = _tick++;
  hb.role = role();
  _handler.send(&hb, sizeof(hb));

  const uint32_t now = millis();
  float dt = (float)(now - _last_ms) / 1000.0f;
  if (dt > 0.05f) { // clamp big gaps (serial pauses, etc)
    dt = 0.05f;
  }
  _last_ms = now;

  _render_clear();
  _sim(dt, now);
  _render_draw();

  delay(33); // ~30 fps
}

uint8_t game::role() {
  return _handler.role();
}

void game::_on_heartbeat(const uint8_t* data, size_t len) {
  if (len < sizeof(heartbeat_msg)) {
    return;
  }

  heartbeat_msg hb;
  memcpy(&hb, data, sizeof(hb));
  _peer_tick = hb.tick;
  Serial.printf("[game] heartbeat from peer: tick=%lu role=%u\n", hb.tick, hb.role);
}

void game::_restart() {
  for (uint8_t i = 0; i < _max_bullets; ++i) {
    _bullets[i].active = false;
  }

  _kills = 0;
  _wave = 0;
  _last_shot = 0;
  _last_damage = 0;
  _player_hp = _player_hp_max;
  _game_over = false;

  _player = {
      (float)(display::width() - _player_size) / 2.0f,
      (float)(_hud_h + display::height() - _player_size) / 2.0f,
  };

  display::fill_rect(0, _hud_h, display::width(), display::height() - _hud_h, _grass);
  _spawn_wave();
}

void game::_spawn_wave() {
  ++_wave;
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    _zombies[i].active = false;
  }

  const uint16_t iw = display::width();
  const uint16_t ih = display::height();
  const uint8_t count = (_wave + 3u > _max_zombies) ? _max_zombies : (_wave + 3u);

  for (uint8_t i = 0; i < count; ++i) {
    float x = 0.0f;
    float y = 0.0f;
    for (uint8_t attempt = 0; attempt < 10; ++attempt) {
      const uint16_t m = _zombie_margin;
      switch (esp_random() % 4) {
        case 0:
          x = m + (float)(esp_random() % (iw - 2 * m));
          y = _hud_h + m;
          break;
        case 1:
          x = m + (float)(esp_random() % (iw - 2 * m));
          y = ih - m;
          break;
        case 2:
          x = m;
          y = _hud_h + m + (float)(esp_random() % (ih - _hud_h - 2 * m));
          break;
        default:
          x = iw - m;
          y = _hud_h + m + (float)(esp_random() % (ih - _hud_h - 2 * m));
          break;
      }
      const float dx = x - _player.x;
      const float dy = y - _player.y;
      if (dx * dx + dy * dy >= 3600.0f) { // >= 60px from player
        break;
      }
    }
    _zombies[i] = { x, y, _zombie_hp, true };
  }
}

void game::_do_fire(uint32_t now) {
  if (now - _last_shot < _fire_cd_ms) {
    return;
  }

  int16_t best = -1;
  float best_d = _fire_range * _fire_range;
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    if (!_zombies[i].active) {
      continue;
    }
    const float dx = _zombies[i].x - _player.x;
    const float dy = _zombies[i].y - _player.y;
    const float d = dx * dx + dy * dy;
    if (d <= best_d) {
      best_d = d;
      best = (int16_t)i;
    }
  }
  if (best < 0) {
    return;
  }

  const float bx = _player.x + _player_size / 2.0f;
  const float by = _player.y + _player_size / 2.0f;
  float dx = _zombies[best].x + _zombie_size / 2.0f - bx;
  float dy = _zombies[best].y + _zombie_size / 2.0f - by;
  const float len = sqrtf(dx * dx + dy * dy);
  dx /= len;
  dy /= len;

  for (uint8_t i = 0; i < _max_bullets; ++i) {
    if (!_bullets[i].active) {
      _bullets[i] = {
          bx,
          by,
          dx * _bullet_speed,
          dy * _bullet_speed,
          true,
      };
      _last_shot = now;
      break;
    }
  }
}

void game::_sim(float dt, uint32_t now) {
  if (_game_over) {
    if (input::fire_pressed()) {
      _restart();
    }
    return;
  }

  float dx = input::jx();
  float dy = input::jy();

  const float len = sqrtf(dx * dx + dy * dy);
  if (len > 1.0f) { // keep diagonal speed equal
    dx /= len;
    dy /= len;
  }

  _player.x += dx * _player_speed * dt;
  _player.y += dy * _player_speed * dt;

  _player.x = constrain(_player.x, 0.0f, (float)(display::width() - _player_size));
  _player.y = constrain(_player.y, (float)_hud_h, (float)(display::height() - _player_size));

  if (input::fire_pressed()) {
    _do_fire(now);
  }

  const float pcx = _player.x + _player_size / 2.0f;
  const float pcy = _player.y + _player_size / 2.0f;

  for (uint8_t i = 0; i < _max_bullets; ++i) {
    if (!_bullets[i].active) {
      continue;
    }
    _bullets[i].x += _bullets[i].vx * dt;
    _bullets[i].y += _bullets[i].vy * dt;
    if (_bullets[i].x < 0.0f || _bullets[i].x > (float)display::width() ||
        _bullets[i].y < _hud_h || _bullets[i].y > (float)display::height()) {
      _bullets[i].active = false;
      continue;
    }
    for (uint8_t z = 0; z < _max_zombies; ++z) {
      if (!_zombies[z].active) {
        continue;
      }
      const float zcx = _zombies[z].x + _zombie_size / 2.0f;
      const float zcy = _zombies[z].y + _zombie_size / 2.0f;
      const float hdx = _bullets[i].x - zcx;
      const float hdy = _bullets[i].y - zcy;
      if (hdx * hdx + hdy * hdy <= _hit_dist * _hit_dist) {
        _bullets[i].active = false;
        if (--_zombies[z].hp == 0) {
          _zombies[z].active = false;
          ++_kills;
        }
        break;
      }
    }
  }

  for (uint8_t z = 0; z < _max_zombies; ++z) {
    if (!_zombies[z].active) {
      continue;
    }
    const float zcx = _zombies[z].x + _zombie_size / 2.0f;
    const float zcy = _zombies[z].y + _zombie_size / 2.0f;

    float ddx = pcx - zcx;
    float ddy = pcy - zcy;
    const float d = sqrtf(ddx * ddx + ddy * ddy);
    if (d > 0.5f) {
      ddx /= d;
      ddy /= d;
      _zombies[z].x += ddx * _zombie_speed * dt;
      _zombies[z].y += ddy * _zombie_speed * dt;
    }
    _zombies[z].x = constrain(_zombies[z].x, 0.0f, (float)(display::width() - _zombie_size));
    _zombies[z].y = constrain(_zombies[z].y, (float)_hud_h, (float)(display::height() - _zombie_size));

    const float cdx = pcx - zcx;
    const float cdy = pcy - zcy;
    if (cdx * cdx + cdy * cdy <= _contact_dist * _contact_dist &&
        now - _last_damage >= _damage_cd_ms) {
      _last_damage = now;
      if (_player_hp > 0) {
        --_player_hp;
      }
    }
  }

  if (_player_hp == 0) {
    _game_over = true;
    return;
  }

  bool any = false;
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    any |= _zombies[i].active;
  }
  if (!any) {
    _spawn_wave();
  }
}

void game::_render_clear() {
  display::fill_rect((int16_t)_player.x, (int16_t)_player.y, _player_size, _player_size, _grass);
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    if (_zombies[i].active) {
      display::fill_rect((int16_t)_zombies[i].x, (int16_t)_zombies[i].y, _zombie_size, _zombie_size, _grass);
    }
  }
  for (uint8_t i = 0; i < _max_bullets; ++i) {
    if (_bullets[i].active) {
      display::fill_rect((int16_t)_bullets[i].x, (int16_t)_bullets[i].y, _bullet_size, _bullet_size, _grass);
    }
  }
}

void game::_render_draw() {
  char buf[32];
  snprintf(buf, sizeof(buf), "hp:%u wave:%u kills:%u %s",
           _player_hp, _wave, _kills, role() == ROLE_HOST ? "host" : "client");
  display::text(buf, 4, 1, colour::yellow, 1);

  if (_game_over) {
    display::text("GAME OVER", display::width() / 2 - 32, 100, colour::red, 2);
    display::text("FIRE=reiniciar", display::width() / 2 - 40, 122, colour::white, 1);
  }

  display::fill_rect((int16_t)_player.x, (int16_t)_player.y, _player_size, _player_size, colour::blue);
  for (uint8_t i = 0; i < _max_zombies; ++i) {
    if (_zombies[i].active) {
      display::fill_rect((int16_t)_zombies[i].x, (int16_t)_zombies[i].y, _zombie_size, _zombie_size, colour::red);
    }
  }
  for (uint8_t i = 0; i < _max_bullets; ++i) {
    if (_bullets[i].active) {
      display::fill_rect((int16_t)_bullets[i].x, (int16_t)_bullets[i].y, _bullet_size, _bullet_size, colour::white);
    }
  }
}
