#include <config.h>
#include <game.h>

void setup() {
  game::begin(DEVICE_ROLE);
}

void loop() {
  game::update();
}