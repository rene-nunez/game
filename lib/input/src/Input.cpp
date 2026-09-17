#include <Arduino.h>

#include <pins.h>
#include <Input.h>

constexpr uint16_t _adc_center = 2047;
constexpr float _deadzone = 0.08f;
constexpr float _smoothing = 0.5f;
constexpr uint32_t _debounce_ms = 50;

float input::_jx = 0.0f;
float input::_jy = 0.0f;

input::_button input::_fire;
input::_button input::_reload;
input::_button input::_interact;
input::_button input::_pause;

bool input::begin() {
  pinMode(JOY_X, INPUT);
  pinMode(JOY_Y, INPUT);

  pinMode(BTN_FIRE, INPUT_PULLUP);
  pinMode(BTN_RELOAD, INPUT_PULLUP);
  pinMode(BTN_INTERACT, INPUT_PULLUP);
  pinMode(BTN_PAUSE, INPUT_PULLUP);
  return true;
}

void input::update() {
  _jx = _jx * _smoothing + _axis(JOY_X) * (1.0f - _smoothing);
  _jy = _jy * _smoothing + _axis(JOY_Y) * (1.0f - _smoothing);

  _poll_button(_fire, BTN_FIRE);
  _poll_button(_reload, BTN_RELOAD);
  _poll_button(_interact, BTN_INTERACT);
  _poll_button(_pause, BTN_PAUSE);
}

float input::jx() {
  return _jx;
}

float input::jy() {
  return _jy;
}

bool input::fire_pressed() {
  return _fire.edge;
}

bool input::fire_down() {
  return _fire.level;
}

bool input::reload_pressed() {
  return _reload.edge;
}

bool input::reload_down() {
  return _reload.level;
}

bool input::interact_pressed() {
  return _interact.edge;
}

bool input::interact_down() {
  return _interact.level;
}

bool input::pause_pressed() {
  return _pause.edge;
}

bool input::pause_down() {
  return _pause.level;
}

float input::_axis(uint8_t pin) {
  const float v = (float)(analogRead(pin) - _adc_center) / (float)_adc_center;
  if (v > -_deadzone && v < _deadzone) {
    return 0.0f;
  }
  return v > 0.0f ? (v - _deadzone) / (1.0f - _deadzone)
                  : (v + _deadzone) / (1.0f - _deadzone);
}

void input::_poll_button(input::_button& b, uint8_t pin) {
  const bool raw = (digitalRead(pin) == LOW);  // active low, pull-up
  b.edge = false;

  if (raw != b.raw) {
    b.raw = raw;
    b.since = millis();
    return;
  }

  if (raw != b.level && (uint32_t)(millis() - b.since) >= _debounce_ms) {
    b.level = raw;
    b.edge = raw;
  }
}