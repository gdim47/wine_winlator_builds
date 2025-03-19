#!/usr/bin/env bash

if [ -z "${VK_LIB_NAME}" ]; then
    echo "Specify lib name to build"
    exit 1
fi

export VK_LIB_TAG="${VK_LIB_TAG:-main}"
export VK_LIB_REF="${VK_LIB_REF:-$(git rev-parse HEAD)}"
export VK_LIB_SRC_DIR="${VK_LIB_SRC_DIR:-/workdir/${VK_LIB_NAME}}"
export VK_LIB_BUILD_DIR="${VK_LIB_BUILD_DIR:-/workdir/${VK_LIB_NAME}-build}"
export VK_LIB_PREFIX_DIR="${VK_LIB_PREFIX_DIR:-/workdir/prefix-${VK_LIB_NAME}-${VK_LIB_TAG}-${VK_LIB_REF}}"

export CONFIG_OPTIONS="
    --cross-file /toolchains/arm64ec-w64-mingw32.txt \
    --buildtype debugoptimized \
"

echo "Build environment vars"
env

echo "Configuring ${VK_LIB_NAME} arm64ec build"
meson setup ${CONFIG_OPTIONS} --prefix ${VK_LIB_PREFIX_DIR} ${VK_LIB_SRC_DIR} "${VK_LIB_BUILD_DIR}/build-arm64ec"
cd "${VK_LIB_BUILD_DIR}/build-arm64ec"
echo "Building ${VK_LIB_NAME} arm64ec"
ninja -j$(nproc)
ninja install

echo "${VK_LIB_NAME} build finished"
