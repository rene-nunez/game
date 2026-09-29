#include <Arduino.h>
#include <cmath>

#include <Input.h>
#include <map.h>

#include "Sim.h"

sim::state sim::_s;
uint32_t sim::_last_ms = 0;
uint32_t sim::_last_shot = 0;
uint32_t sim::_last_damage = 0;
int16_t sim::_path_tx = -1;
int16_t sim::_path_ty = -1;

constexpr int8_t sim::nbr_x[8];
constexpr int8_t sim::nbr_y[8];

void sim::reset() {
  for (uint8_t i = 0; i < MAX_BULLETS; ++i) {
    _s.bullets[i].active = false;
  }

  _s.kills = 0;
  _s.wave = 0;
  _s.points = 0;
  _s.gun = weapon::pistol;
  _s.dmg_lvl = 0;
  _s.spd_lvl = 0;
  _s.last_event = event::none;
  _last_shot = 0;
  _last_damage = 0;
  _s.player_hp = PLAYER_HP_MAX;

  _s.player = {
      (float)tilemap::spawn_px - PLAYER_SIZE / 2.0f,
      (float)tilemap::spawn_py - PLAYER_SIZE / 2.0f,
  };

  _path_tx = -1; // force a fresh field at the new spawn
  _path_ty = -1;

  _spawn_wave();
}

void sim::_move_entity(float& x, float& y, float dx, float dy, uint8_t size) {
  x = constrain(x, 0.0f, (float)(tilemap::WORLD_W - size));
  y = constrain(y, 0.0f, (float)(tilemap::WORLD_H - size));

  const float nx = x + dx;
  if (!tilemap::solid_rect((int16_t)nx, (int16_t)y, size, size)) {
    x = nx;
  }
  const float ny = y + dy;
  if (!tilemap::solid_rect((int16_t)x, (int16_t)ny, size, size)) {
    y = ny;
  }
}

bool sim::_step_zombie(uint8_t z, float ddx, float ddy, float dt) {
  const float d = sqrtf(ddx * ddx + ddy * ddy);
  if (d <= 0.5f) {
    return false;
  }
  const float bx = _s.zombies[z].x, by = _s.zombies[z].y;
  // _move_entity takes references, so it has to get the real members, not copies
  _move_entity(_s.zombies[z].x, _s.zombies[z].y, ddx / d * zombie_speed * dt, ddy / d * zombie_speed * dt,
               ZOMBIE_SIZE);
  return _s.zombies[z].x != bx || _s.zombies[z].y != by;
}

void sim::_zombie_steer(uint8_t z, float pcx, float pcy, float dt) {
  const float zcx = _s.zombies[z].x + ZOMBIE_SIZE / 2.0f;
  const float zcy = _s.zombies[z].y + ZOMBIE_SIZE / 2.0f;
  int16_t ztx = (int16_t)(zcx / tilemap::TILE);
  int16_t zty = (int16_t)(zcy / tilemap::TILE);

  if (ztx < 0) { // keep every field[] read provably in bounds
    ztx = 0;
  } else if (ztx >= tilemap::COLS) {
    ztx = tilemap::COLS - 1;
  }
  if (zty < 0) {
    zty = 0;
  } else if (zty >= tilemap::ROWS) {
    zty = tilemap::ROWS - 1;
  }

  const uint16_t here = tilemap::field[zty][ztx];

  if (here == 0) {
    return; // on the player's tile, the contact check lands the hit
  }
  if (here == tilemap::UNREACHABLE) { // walled off from the player: straight chase as a fallback
    _step_zombie(z, pcx - zcx, pcy - zcy, dt);
    return;
  }

  uint8_t tried = 0;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) { // retry the next best tile if one is blocked
    int8_t best = -1;
    uint16_t best_d = here;
    for (uint8_t k = 0; k < 8; ++k) {
      if (tried & (1u << k)) {
        continue;
      }
      const int16_t nx = (int16_t)(ztx + nbr_x[k]);
      const int16_t ny = (int16_t)(zty + nbr_y[k]);
      if (nx < 0 || nx >= tilemap::COLS || ny < 0 || ny >= tilemap::ROWS) {
        continue;
      }
      const uint16_t d = tilemap::field[ny][nx];
      if (d < best_d) {
        best_d = d;
        best = (int8_t)k;
      }
    }
    if (best < 0) {
      return;
    }
    tried = (uint8_t)(tried | (1u << best));

    // aim at the centre of the chosen tile, so a diagonal reads as drift not a shuffle
    const float tx_c = (float)(ztx + nbr_x[best]) * tilemap::TILE + tilemap::TILE / 2.0f;
    const float ty_c = (float)(zty + nbr_y[best]) * tilemap::TILE + tilemap::TILE / 2.0f;
    if (_step_zombie(z, tx_c - zcx, ty_c - zcy, dt)) {
      return;
    }
  }
}


void sim::_spawn_wave() {
  ++_s.wave;
  for (uint8_t i = 0; i < MAX_ZOMBIES; ++i) {
    _s.zombies[i].active = false;
  }

  const uint8_t count = (_s.wave + 3u > MAX_ZOMBIES) ? MAX_ZOMBIES : (_s.wave + 3u);
  const float px = _s.player.x + PLAYER_SIZE / 2.0f;
  const float py = _s.player.y + PLAYER_SIZE / 2.0f;
  const uint16_t total = (uint16_t)(tilemap::COLS * tilemap::ROWS);
  const int16_t off = (int16_t)((tilemap::TILE - ZOMBIE_SIZE) / 2);

  for (uint8_t i = 0; i < count; ++i) {
    // scan every tile from a random offset, so a spot is always found
    const uint16_t start = (uint16_t)(esp_random() % total);
    for (uint16_t k = 0; k < total; ++k) {
      const uint16_t idx = (uint16_t)((start + k) % total);
      const int16_t tx = (int16_t)(idx % tilemap::COLS);
      const int16_t ty = (int16_t)(idx / tilemap::COLS);
      if (tilemap::solid(tx, ty)) {
        continue;
      }
      const float x = (float)(tx * tilemap::TILE + off);
      const float y = (float)(ty * tilemap::TILE + off);
      const float dx = x - px;
      const float dy = y - py;
      if (dx * dx + dy * dy < (float)spawn_min_d2 && k + 1 < total) {
        continue; // too close to the player, keep looking
      }
      _s.zombies[i] = { x, y, _zombie_hp(_s.wave), true, actor_kind::normal };
      break;
    }
  }
  _s.last_event = event::wave;
}

uint8_t sim::_zombie_hp(uint8_t wave) {
  return (uint8_t)(2u + (uint16_t)wave / 2u); // tougher every two waves
}

uint8_t sim::_eff_dmg(uint8_t base, uint8_t lvl) {
  const float dmg = (float)base * (1.0f + 0.25f * (float)lvl);
  const uint8_t eff = (uint8_t)(dmg + 0.5f); // half-up: 1-dmg guns step up at lvl 2 and 4
  return eff < 1 ? 1 : eff;
}

uint32_t sim::_kill_reward(uint8_t wave) {
  return 10u + 2u * (uint32_t)wave; // base income with a mild wave slope
}

float sim::_spd_mult(uint8_t lvl) {
  return 1.0f + 0.08f * (float)lvl; // +8%/level, +40% at max like the old buff
}

uint32_t sim::_fire_cd(weapon w) {
  switch (w) {
    case weapon::smg: return 180;
    case weapon::shotgun: return 900;
    case weapon::rifle: return 800;
    default: return 500; // pistol
  }
}

uint8_t sim::_base_dmg(weapon w) {
  switch (w) {
    case weapon::rifle: return 4; // anti-boss punch
    default: return 1; // pistol, smg and each shotgun pellet
  }
}

uint8_t sim::_fire_one(uint32_t now, float dx, float dy, uint8_t dmg) {
  const float bx = _s.player.x + PLAYER_SIZE / 2.0f;
  const float by = _s.player.y + PLAYER_SIZE / 2.0f;
  for (uint8_t i = 0; i < MAX_BULLETS; ++i) {
    if (!_s.bullets[i].active) {
      _s.bullets[i] = {
          bx,
          by,
          dx * bullet_speed,
          dy * bullet_speed,
          dmg,
          true,
      };
      _last_shot = now;
      return 1;
    }
  }
  return 0; // rack is full: keep the cooldown so the next press retries
}

void sim::_do_fire(uint32_t now) {
  if (now - _last_shot < _fire_cd(_s.gun)) {
    return;
  }

  int16_t best = -1;
  float best_d = fire_range * fire_range;
  for (uint8_t i = 0; i < MAX_ZOMBIES; ++i) {
    if (!_s.zombies[i].active) {
      continue;
    }
    const float dx = _s.zombies[i].x - _s.player.x;
    const float dy = _s.zombies[i].y - _s.player.y;
    const float d = dx * dx + dy * dy;
    if (d <= best_d) {
      best_d = d;
      best = (int16_t)i;
    }
  }
  if (best < 0) {
    return;
  }

  const float bx = _s.player.x + PLAYER_SIZE / 2.0f;
  const float by = _s.player.y + PLAYER_SIZE / 2.0f;
  float dx = _s.zombies[best].x + ZOMBIE_SIZE / 2.0f - bx;
  float dy = _s.zombies[best].y + ZOMBIE_SIZE / 2.0f - by;
  const float len = sqrtf(dx * dx + dy * dy);
  dx /= len;
  dy /= len;

  const uint8_t dmg = _eff_dmg(_base_dmg(_s.gun), _s.dmg_lvl);
  uint8_t fired = 0;
  if (_s.gun == weapon::shotgun) {
    // 3 pellets fanned around the aim: straight, -0.15rad, +0.15rad
    constexpr float c = 0.988771f; // cos(0.15)
    constexpr float s = 0.149438f; // sin(0.15)
    fired += _fire_one(now, dx, dy, dmg);
    fired += _fire_one(now, dx * c - dy * s, dx * s + dy * c, dmg);
    fired += _fire_one(now, dx * c + dy * s, -dx * s + dy * c, dmg);
  } else {
    fired = _fire_one(now, dx, dy, dmg);
  }
  if (fired > 0) {
    _s.last_event = event::shoot;
  }
}

bool sim::step(uint32_t now) {
  float dt = (float)(now - _last_ms) / 1000.0f;
  if (dt > 0.05f) { // clamp big gaps (serial pauses, menu)
    dt = 0.05f;
  }
  _last_ms = now;
  _s.last_event = event::none; // buys after step() overwrite this for their frame

  float dx = input::jx();
  float dy = input::jy();

  const float len = sqrtf(dx * dx + dy * dy);
  if (len > 1.0f) { // keep diagonal speed equal
    dx /= len;
    dy /= len;
  }

  const float spd = player_speed * _spd_mult(_s.spd_lvl);
  _move_entity(_s.player.x, _s.player.y, dx * spd * dt, dy * spd * dt, PLAYER_SIZE);

  if (input::fire_pressed()) {
    _do_fire(now);
  }

  const float pcx = _s.player.x + PLAYER_SIZE / 2.0f;
  const float pcy = _s.player.y + PLAYER_SIZE / 2.0f;

  for (uint8_t i = 0; i < MAX_BULLETS; ++i) {
    if (!_s.bullets[i].active) {
      continue;
    }
    _s.bullets[i].x += _s.bullets[i].vx * dt;
    _s.bullets[i].y += _s.bullets[i].vy * dt;
    if (_s.bullets[i].x < 0.0f || _s.bullets[i].x > (float)(tilemap::WORLD_W - BULLET_SIZE) ||
        _s.bullets[i].y < 0.0f || _s.bullets[i].y > (float)(tilemap::WORLD_H - BULLET_SIZE) ||
        tilemap::solid_rect((int16_t)_s.bullets[i].x, (int16_t)_s.bullets[i].y, BULLET_SIZE, BULLET_SIZE)) {
      _s.bullets[i].active = false;
      continue;
    }
    for (uint8_t z = 0; z < MAX_ZOMBIES; ++z) {
      if (!_s.zombies[z].active) {
        continue;
      }
      const float zcx = _s.zombies[z].x + ZOMBIE_SIZE / 2.0f;
      const float zcy = _s.zombies[z].y + ZOMBIE_SIZE / 2.0f;
      const float hdx = _s.bullets[i].x - zcx;
      const float hdy = _s.bullets[i].y - zcy;
      if (hdx * hdx + hdy * hdy <= hit_dist * hit_dist) {
        _s.bullets[i].active = false;
        const uint8_t dmg = _s.bullets[i].dmg;
        if (dmg >= _s.zombies[z].hp) {
          _s.zombies[z].hp = 0;
          _s.zombies[z].active = false;
          ++_s.kills;
          _s.points += _kill_reward(_s.wave);
        } else {
          _s.zombies[z].hp = (uint8_t)(_s.zombies[z].hp - dmg);
        }
        break;
      }
    }
  }

  // the distance field only depends on the player's tile, so rebuild it when that changes
  const int16_t ptx = (int16_t)pcx / tilemap::TILE;
  const int16_t pty = (int16_t)pcy / tilemap::TILE;
  if (ptx != _path_tx || pty != _path_ty) {
    _path_tx = ptx;
    _path_ty = pty;
    tilemap::build_field(ptx, pty);
  }

  for (uint8_t z = 0; z < MAX_ZOMBIES; ++z) {
    if (!_s.zombies[z].active) {
      continue;
    }
    const float zcx = _s.zombies[z].x + ZOMBIE_SIZE / 2.0f;
    const float zcy = _s.zombies[z].y + ZOMBIE_SIZE / 2.0f;

    _zombie_steer(z, pcx, pcy, dt);

    const float cdx = pcx - zcx;
    const float cdy = pcy - zcy;
    if (cdx * cdx + cdy * cdy <= contact_dist * contact_dist &&
        now - _last_damage >= damage_cd_ms) {
      _last_damage = now;
      if (_s.player_hp > 0) {
        --_s.player_hp;
        _s.last_event = event::hurt;
      }
    }
  }

  if (_s.player_hp == 0) {
    _s.last_event = event::over;
    return false; // the caller raises the game over screen, sim never touches it
  }

  bool any = false;
  for (uint8_t i = 0; i < MAX_ZOMBIES; ++i) {
    any |= _s.zombies[i].active;
  }
  if (!any) {
    _spawn_wave();
  }
  return true;
}

uint32_t sim::price_for(uint32_t base, uint8_t lvl) {
  return base + LVL_PRICE_STEP * (uint32_t)lvl;
}

bool sim::buy_heal(uint32_t now) {
  (void)now;
  if (_s.player_hp >= PLAYER_HP_MAX || _s.points < PRICE_HEAL) {
    _s.last_event = event::denied; // full HP or broke
    return false;
  }
  _s.points -= PRICE_HEAL; // exact points pay
  _s.player_hp = (uint8_t)(_s.player_hp + 2 > PLAYER_HP_MAX ? PLAYER_HP_MAX : _s.player_hp + 2);
  _s.last_event = event::buy_heal;
  return true;
}

bool sim::buy_damage(uint32_t now) {
  (void)now;
  if (_s.dmg_lvl >= MAX_LVL) {
    _s.last_event = event::denied; // capped, like a full HP bar
    return false;
  }
  const uint32_t price = price_for(PRICE_DMG, _s.dmg_lvl);
  if (_s.points < price) {
    _s.last_event = event::denied;
    return false;
  }
  _s.points -= price;
  ++_s.dmg_lvl; // permanent, part of the character
  _s.last_event = event::buy_dmg;
  return true;
}

bool sim::buy_speed(uint32_t now) {
  (void)now;
  if (_s.spd_lvl >= MAX_LVL) {
    _s.last_event = event::denied; // capped, like a full HP bar
    return false;
  }
  const uint32_t price = price_for(PRICE_SPD, _s.spd_lvl);
  if (_s.points < price) {
    _s.last_event = event::denied;
    return false;
  }
  _s.points -= price;
  ++_s.spd_lvl; // permanent, part of the character
  _s.last_event = event::buy_spd;
  return true;
}

bool sim::roll_roulette(uint32_t now) {
  (void)now;
  if (_s.points < PRICE_ROLL) {
    _s.last_event = event::denied;
    return false;
  }
  _s.points -= PRICE_ROLL;
  _s.gun = _roll_weapon((uint8_t)(esp_random() % 100u));
  _s.last_event = event::roulette;
  return true;
}

sim::weapon sim::_roll_weapon(uint8_t r) {
  if (r < 40) {
    return weapon::smg;
  }
  if (r < 55) {
    return weapon::pistol;
  }
  if (r < 85) {
    return weapon::shotgun;
  }
  return weapon::rifle;
}

const char* sim::gun_name(weapon w) {
  switch (w) {
    case weapon::smg: return "SMG";
    case weapon::shotgun: return "SHOTGUN";
    case weapon::rifle: return "RIFLE";
    default: return "PISTOL";
  }
}

const char* sim::gun_name() {
  return gun_name(_s.gun);
}
