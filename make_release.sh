#!/bin/bash

set -eu

VERSION=$(cat version.txt)
echo "Releasing version ${VERSION}"
release_name="pdmfrc_v${VERSION}"
dest_dir=`pwd`"/releases/${release_name}"

rm -f "releases/${release_name}.zip"
rm -rf "${dest_dir}"
mkdir -p "${dest_dir}"

# rp2/<display>/<example>/<board>/<example>.uf2
function build_rp2 {
  local target=$1
  local board=$2
  local display=$3
  pushd "examples/rp2/${target}" > /dev/null
    echo "Building ${target} for ${board} (${display})"
    ./build.sh "${board}" "${display}" > "build-${board}-${display}.log" 2>&1
    mkdir -p "${dest_dir}/rp2/${display}/${target}/${board}"
    cp build/${board}-${display}/${target}.uf2 "${dest_dir}/rp2/${display}/${target}/${board}/"
  popd > /dev/null
}

# build_display <display> <example>...
function build_display {
  local display=$1
  shift
  for target in "$@"; do
    for board in pico pico2; do
      build_rp2 "${target}" "${board}" "${display}"
    done
  done
}

build_display tg12864b gradient image gfx3d
build_display sg12864aslb gradient image gfx3d
build_display aqm1248a gradient image gfx3d
build_display nokia5110 gradient image gfx3d

cp README.md README.ja.md LICENSE "${dest_dir}/"
# Images referenced by the READMEs
cp -r image "${dest_dir}/"

pushd releases > /dev/null
  zip -r "${release_name}.zip" "${release_name}"
popd > /dev/null
