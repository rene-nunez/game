#include <Game.h>
#include "config.h"

void setup() {
  game::begin(DEVICE_ROLE);
}

void loop() {
  game::update();
}
