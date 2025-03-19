#!/usr/bin/env bash

export FEX_TAG="${FEX_TAG:-main}"
export FEX_REF="${FEX_REF:-$(git rev-parse HEAD)}"
export FEX_SRC_DIR="${FEX_SRC_DIR:-/workdir/FEX}"
export FEX_BUILD_DIR="${FEX_BUILD_DIR:-/workdir/FEX-build}"
export FEX_PREFIX_DIR="${FEX_PREFIX_DIR:-/workdir/prefix-fex-${FEX_TAG}-${FEX_REF}}"

export CONFIG_OPTIONS="
    -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_TOOLCHAIN_FILE=/toolchains/arm64ec_mingw_toolchain.cmake \
    -DENABLE_LTO=False -DBUILD_TESTS=False -DENABLE_JEMALLOC_GLIBC_ALLOC=False \
    -DCMAKE_INSTALL_LIBDIR=${FEX_PREFIX_DIR}/lib/wine/aarch64-windows \
"

echo "Build environment vars"
env

echo "Configuring FEX arm64ec build"
mkdir -p "${FEX_BUILD_DIR}/build-arm64ec"
cd "${FEX_BUILD_DIR}/build-arm64ec"
cmake ${CONFIG_OPTIONS} -DMINGW_TRIPLE=arm64ec-w64-mingw32 -DCMAKE_INSTALL_PREFIX=${FEX_PREFIX_DIR} ${FEX_SRC_DIR}
echo "Building FEX arm64ec"
ninja -j$(nproc)
ninja install

echo "Configuring FEX wow64 build"
mkdir -p "${FEX_BUILD_DIR}/build-wow64"
cd "${FEX_BUILD_DIR}/build-wow64"
cmake ${CONFIG_OPTIONS} -DMINGW_TRIPLE=aarch64-w64-mingw32 -DCMAKE_INSTALL_PREFIX=${FEX_PREFIX_DIR} ${FEX_SRC_DIR}
echo "Building FEX wow64"
ninja -j$(nproc)
ninja install

echo "FEX build finished"
