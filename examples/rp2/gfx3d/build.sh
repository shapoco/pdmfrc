#!/bin/bash
# Usage: ./build.sh [board] [cmake options...]   board: pico (RP2040, default) / pico2 (RP2350)
set -eu
cd "$(dirname "$0")"
board=${1:-pico}
shift || true
cmake -S . -B build/$board -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=$board "$@"
ninja -C build/$board
