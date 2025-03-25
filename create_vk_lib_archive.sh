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
export VK_LIB_LIB_DIR="${VK_LIB_PREFIX_DIR}/lib/wine/aarch64-windows"

export CONFIG_OPTIONS="
    --cross-file /toolchains/arm64ec-w64-mingw32.txt \
    --buildtype debugoptimized \
    --bindir lib/wine/aarch64-windows \
"

# hack: add lib src dir to git's safe.directory
git config --global --add safe.directory ${VK_LIB_SRC_DIR}

echo "Build environment vars"
env

echo "Configuring ${VK_LIB_NAME} arm64ec build"
meson setup ${CONFIG_OPTIONS} --prefix ${VK_LIB_PREFIX_DIR} ${VK_LIB_SRC_DIR} "${VK_LIB_BUILD_DIR}/build-arm64ec"
cd "${VK_LIB_BUILD_DIR}/build-arm64ec"
echo "Building ${VK_LIB_NAME} arm64ec"
ninja -j$(nproc)
ninja install

echo "Add wine builtin tag to generated dlls"
for f in "${VK_LIB_LIB_DIR}"/*.dll; do
    if [ -f "$f" ] && [ ! -L "$f" ]; then
        dd bs=32 count=1 seek=2 conv=notrunc if=/toolchains/wine_builtin.bin of="$f"
    fi
done

echo "${VK_LIB_NAME} build finished"
