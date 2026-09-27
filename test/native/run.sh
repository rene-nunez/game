#!/bin/sh
# Native checks for the tilemap and the world/camera invariants. No hardware and
# no Arduino needed; the binary lands in $TMPDIR so the tree stays clean.
set -e
cd "$(dirname "$0")/../.."

out="${TMPDIR:-/tmp}/game_native_map_test"
g++ -O2 -std=c++17 -Wall -I lib/display/src -o "$out" test/native/map_test.cpp
"$out"
