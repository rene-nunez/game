#include <config.h>
#include <Game.h>

void setup() {
  game::begin(DEVICE_ROLE);
}

void loop() {
  game::update();
}
