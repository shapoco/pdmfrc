#!/bin/bash
# Builds and runs the host test (g++, Python3 + Pillow + NumPy)
set -eu

cd "$(dirname "$0")/.."
out=test/build
mkdir -p $out

python3 - "$out" <<'EOF'
import sys
sys.path.insert(0, "tools")
import colorlib
pal = colorlib.load_palette("assets/palettes/tg12864b.png")
open(sys.argv[1] + "/lut565.bin", "wb").write(colorlib.Lut565(pal).table.tobytes())
open(sys.argv[1] + "/lutgray8.bin", "wb").write(colorlib.lut_gray8(pal).tobytes())
EOF

for std in c++11 c++17; do
  g++ -std=$std -O2 -Wall -Wextra -Werror -Iinclude test/host_test.cpp src/*.cpp -o $out/host_test_$std
done
# The UQSUB8 path of the anti-ghost (Arm DSP) with an emulated instruction
g++ -std=c++11 -O2 -Wall -Wextra -Werror -DPDMFRC_EMULATE_UQSUB8 -Iinclude test/host_test.cpp \
  src/*.cpp -o $out/host_test_uqsub8
$out/host_test_c++11 $out/lut565.bin $out/lutgray8.bin
$out/host_test_c++17 $out/lut565.bin $out/lutgray8.bin
$out/host_test_uqsub8
