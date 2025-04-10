#!/usr/bin/env bash

if [ -z "${VK_LIB_NAME}" ]; then
    echo "Specify lib name to build"
    exit 1
fi

export VK_LIB_ARCH="${VK_LIB_ARCH}"
export VK_LIB_TAG="${VK_LIB_TAG:-main}"
export VK_LIB_REF="${VK_LIB_REF:-$(git rev-parse HEAD)}"
export VK_LIB_SRC_DIR="${VK_LIB_SRC_DIR:-/workdir/${VK_LIB_NAME}}"
export VK_LIB_BUILD_DIR="${VK_LIB_BUILD_DIR:-/workdir/${VK_LIB_NAME}-build}"
export VK_LIB_PREFIX_DIR="${VK_LIB_PREFIX_DIR:-/workdir/prefix-${VK_LIB_NAME}-${VK_LIB_ARCH}-${VK_LIB_TAG}-${VK_LIB_REF}}"

export CONFIG_OPTIONS="
    --buildtype debugoptimized \
"

case "${VK_LIB_ARCH}" in
    "x86")
        export CONFIG_ARCH_SPECIFIC="
            --cross-file /toolchains/x86_64-w64-mingw32.txt \
            --bindir lib/wine/x86_64-windows \
        "
        export VK_LIB_LIB_DIR="${VK_LIB_PREFIX_DIR}/lib/wine/x86_64-windows"
        ;;
    "arm64ec")
        export CONFIG_ARCH_SPECIFIC="
            --cross-file /toolchains/arm64ec-w64-mingw32.txt \
            --bindir lib/wine/aarch64-windows \
        "
        export VK_LIB_LIB_DIR="${VK_LIB_PREFIX_DIR}/lib/wine/aarch64-windows"
        ;;
    *)
        echo "Unsupported build architecture: ${VK_LIB_ARCH}"
        exit 1
        ;;
esac

# hack: add lib src dir to git's safe.directory
git config --global --add safe.directory ${VK_LIB_SRC_DIR}

echo "Build environment vars"
env

echo "Configuring ${VK_LIB_NAME} ${VK_LIB_ARCH} build"
meson setup ${CONFIG_OPTIONS} ${CONFIG_ARCH_SPECIFIC} --prefix ${VK_LIB_PREFIX_DIR} ${VK_LIB_SRC_DIR} "${VK_LIB_BUILD_DIR}/build-${VK_LIB_ARCH}"
cd "${VK_LIB_BUILD_DIR}/build-${VK_LIB_ARCH}"
echo "Building ${VK_LIB_NAME} ${VK_LIB_ARCH}"
ninja -j$(nproc)
ninja install

echo "Add wine builtin tag to generated dlls"
for f in "${VK_LIB_LIB_DIR}"/*.dll; do
    if [ -f "$f" ] && [ ! -L "$f" ]; then
        dd bs=32 count=1 seek=2 conv=notrunc if=/toolchains/wine_builtin.bin of="$f"
    fi
done

echo "${VK_LIB_NAME} build finished"
