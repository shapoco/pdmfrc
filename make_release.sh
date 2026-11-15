#!/bin/bash

set -eu

VERSION=$(cat version.txt)
echo "Releasing version ${VERSION}"
release_name="pdmfrc_v${VERSION}"
dest_dir=`pwd`"/releases/${release_name}"

rm -f "releases/${release_name}.zip"
rm -rf "${dest_dir}"
mkdir -p "${dest_dir}"

function build_rp2 {
  local target=$1
  local board=$2
  local display=tg12864b
  pushd "examples/rp2/${target}" > /dev/null
    echo "Building ${target} for ${board} (${display})"
    ./build.sh "${board}" "${display}" > "build.log" 2>&1
    mkdir -p "${dest_dir}/rp2/${target}/${board}"
    cp build/${board}-${display}/${target}.uf2 "${dest_dir}/rp2/${target}/${board}/"
  popd > /dev/null
}

build_rp2 gradient pico
build_rp2 gradient pico2
build_rp2 image pico
build_rp2 image pico2
build_rp2 gfx3d pico
build_rp2 gfx3d pico2

cp README.md "${dest_dir}/"
cp LICENSE "${dest_dir}/"
cp image/wiring_rp2_tg12864b.png "${dest_dir}/rp2/"

pushd releases > /dev/null
  zip -r "${release_name}.zip" "${release_name}"
popd > /dev/null
