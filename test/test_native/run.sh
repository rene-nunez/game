#!/bin/sh
# Native checks for the tilemap and the world/camera invariants. No hardware and
# no Arduino needed; the binary lands in $TMPDIR so the tree stays clean. The test_
# prefix is PlatformIO's suite naming, so pio test ignores this dir instead of
# trying to build it for the ESP32 (see test_ignore in platformio.ini).
set -e
cd "$(dirname "$0")/../.."

out="${TMPDIR:-/tmp}/game_native_map_test"
g++ -O2 -std=c++17 -Wall -I lib/display/src -o "$out" test/test_native/map_test.cpp
"$out"
