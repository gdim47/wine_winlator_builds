#!/usr/bin/env bash

export WINE_TAG="${WINE_TAG:-wine}"
export WINE_BRANCH="${WINE_BRANCH:-stable}"
export WINE_PATCHES="${WINE_PATCHES}"
export WINE_ARCH="${WINE_ARCH:-x86}"
export WINE_SRC_DIR="${WINE_SRC_DIR:-/workdir/wine}"
export WINE_BUILD_DIR="${WINE_BUILD_DIR:-/workdir/wine-build}"
export WINE_PREFIX_DIR="${WINE_PREFIX_DIR:-/workdir/prefix-${WINE_TAG}-${WINE_ARCH}}"

export CONFIG_OPTIONS="
    --disable-winemenubuilder \
    --with-gstreamer --with-vulkan --with-x --without-osmesa \
    --with-freetype --with-fontconfig \
    --without-opengl --without-coreaudio \
    --without-capi --without-dbus --without-gphoto \
    --without-inotify --without-krb5 --without-opencl \
    --without-oss --without-pcap --without-gphoto \
    --without-sane --without-sdl --without-udev --without-usb \
    --without-v4l2 --without-wayland --without-cups \
    --without-pcsclite \
    --disable-win16 --disable-tests --enable-build-id \
"

echo "Preparing wine git repo"
cd "${WINE_SRC_DIR}"
./tools/make_requests
./tools/make_specfiles
./dlls/winevulkan/make_vulkan
# ./tools/make_makefiles
autoreconf -f

# detect build machine triplet
export BUILD_TRIPLET="$(clang -dumpmachine)"

case "${BUILD_TRIPLET}" in
    x86_64-*-linux-gnu|x86_64-linux-gnu)
        export WINE_TOOLS_CONFIG_TARGET_OPTIONS="--enable-archs=i386,x86_64"
        ;;
    *)
        echo "Unsupported build triplet: ${BUILD_TRIPLET}"
        exit 1
        ;;
esac

echo "Configuring wine tools build"
mkdir -p "${WINE_BUILD_DIR}/build-tools-${WINE_TAG}-${WINE_ARCH}"
cd "${WINE_BUILD_DIR}/build-tools-${WINE_TAG}-${WINE_ARCH}"
${WINE_SRC_DIR}/configure "${WINE_TOOLS_CONFIG_TARGET_OPTIONS}"
echo "Building wine tools"
make -j$(nproc) __tooldeps__

# copy locale.nls
make -j$(nproc) -C nls all

case "${WINE_ARCH}" in
    "x86")
        export CONFIG_TARGET_OPTIONS="
            --host x86_64-linux-gnu --enable-archs=i386,x86_64 \
            --with-mingw=x86_64-w64-mingw32-clang \
        " 
        export CFLAGS="${CFLAGS} -target x86_64-linux-gnu -I/usr/local/gstreamer-1.0-amd64/include"
        export CXXFLAGS="${CXXFLAGS} -target x86_64-linux-gnu -I/usr/local/gstreamer-1.0-amd64/include"
        export LDFLAGS="${LDFLAGS} -target x86_64-linux-gnu -fuse-ld=lld"
        export PKG_CONFIG_LIBDIR="/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
        ;;
    "arm64ec")
        export CONFIG_TARGET_OPTIONS="
            --host aarch64-linux-gnu --enable-archs=i386,x86_64,aarch64 \
            --with-mingw=arm64ec-w64-mingw32-clang \
        "
        export CFLAGS="${CFLAGS} -target aarch64-linux-gnu -I/usr/local/gstreamer-1.0-arm64/include"
        export CXXFLAGS="${CXXFLAGS} -target aarch64-linux-gnu -I/usr/local/gstreamer-1.0-arm64/include"
        export LDFLAGS="${LDFLAGS} -target aarch64-linux-gnu -fuse-ld=lld"
        export PKG_CONFIG_LIBDIR="/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
        ;;
    *)
        echo "Unsupported wine build architecture: ${WINE_ARCH}"
        exit 1
        ;;
esac

echo "Preparing wine git repo"
cd "$WINE_SRC_DIR"
./tools/make_requests
./tools/make_specfiles
./dlls/winevulkan/make_vulkan
# ./tools/make_makefiles
autoreconf -f

echo "Build environment vars"
env

echo "Configuring wine build"
mkdir -p "${WINE_BUILD_DIR}/build-${WINE_TAG}-${WINE_ARCH}"
cd "${WINE_BUILD_DIR}/build-${WINE_TAG}-${WINE_ARCH}"
${WINE_SRC_DIR}/configure ${CONFIG_TARGET_OPTIONS} ${CONFIG_OPTIONS} --prefix ${WINE_PREFIX_DIR} \
    --with-wine-tools="${WINE_BUILD_DIR}/build-tools-${WINE_TAG}-${WINE_ARCH}"
echo "Building wine"
make -j$(nproc) 
make install
echo "Wine build finished"
