#!/usr/bin/env bash

set -e

export BOX64_TAG="${BOX64_TAG:-main}"
export BOX64_SRC_DIR="${BOX64_SRC_DIR:-/workdir/box64}"
export BOX64_REF="${BOX64_REF:-$(git --git-dir=${BOX64_SRC_DIR}/.git --work-tree=${BOX64_SRC_DIR} rev-parse HEAD)}"
export BOX64_BUILD_DIR="${BOX64_BUILD_DIR:-/workdir/box64-build}"
export BOX64_PREFIX_DIR="${BOX64_PREFIX_DIR:-/workdir/prefix-box64-${BOX64_TAG}-${BOX64_REF}}"

export CONFIG_OPTIONS="
    -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DWOW64=1 -DARM_DYNAREC=ON \
"

echo "Build environment vars"
env

echo "Configuring box64 wow64 build"
mkdir -p "${BOX64_BUILD_DIR}/build-wow64"
cd "${BOX64_BUILD_DIR}/build-wow64"
cmake ${CONFIG_OPTIONS} -DCMAKE_C_COMPILER=aarch64-w64-mingw32-gcc \
    -DCMAKE_INSTALL_PREFIX=${BOX64_PREFIX_DIR} ${BOX64_SRC_DIR}
echo "Building box64 wow64"
ninja -j$(nproc) wowbox64


mkdir -p "${BOX64_PREFIX_DIR}/lib/wine/aarch64-windows"
cp "${BOX64_BUILD_DIR}/build-wow64/wowbox64-prefix/src/wowbox64-build/libwowbox64.dll" "${BOX64_PREFIX_DIR}/lib/wine/aarch64-windows"

echo "box64 build finished"
