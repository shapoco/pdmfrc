#!/bin/bash
# Usage: ./build.sh [board] [display] [cmake options...]
#   board:   pico (RP2040, default) / pico2 (RP2350)
#   display: tg12864b (default) / sg12864aslb / aqm1248a / nokia5110
set -eu
cd "$(dirname "$0")"
board=${1:-pico}
shift || true
display=tg12864b
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then
  display=$1
  shift
fi
cmake -S . -B build/$board-$display -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=$board \
  -DPDMFRC_DISPLAY=$display "$@"
ninja -C build/$board-$display
