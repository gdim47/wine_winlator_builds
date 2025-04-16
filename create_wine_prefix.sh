#!/usr/bin/env bash

set -e

export WINE_TMP_PATH="${WINE_TMP_PATH:-/workdir/wine-prefix-tmp}"
export WINE_RESULT_PATH="${WINE_RESULT_PATH:-/workdir}"

echo "Run this script inside container"

if [ -z "${WINE_PREFIX_PATH}" ]; then
    echo "Specify path to wine distrib in WINE_PREFIX_PATH env var"
    exit 1
fi

if [ -n "${WINEARCH}" ]; then
    echo "Will use arch for prefix: ${WINEARCH}"
fi

echo "Recreate winlator rootfs compatible paths"
WINLATOR_PREFIX="/data/data/com.winlator/files/rootfs"
mkdir -p "${WINLATOR_PREFIX}/home"
mkdir -p "${WINLATOR_PREFIX}/tmp"
ln -s -f /usr "${WINLATOR_PREFIX}/usr"
ln -s -f /etc "${WINLATOR_PREFIX}/etc"
ln -s -f /lib64 "${WINLATOR_PREFIX}/lib64"
ln -s -f "${WINLATOR_PREFIX}/usr/bin" "${WINLATOR_PREFIX}/bin"
ln -s -f "${WINLATOR_PREFIX}/usr/lib" "${WINLATOR_PREFIX}/lib"

mkdir -p "${WINLATOR_PREFIX}/usr/share"
ln -s "${WINE_PREFIX_PATH}/share/wine" "${WINLATOR_PREFIX}/usr/share/wine"

echo "Init wine prefix with wineboot"
WINE_PREFIX_DIR_NAME=".wine"
mkdir -p ${WINE_TMP_PATH}
WINEDLLOVERRIDES="winegstreamer=,mscoree,mshtml=d,winemenubuilder.exe=d" WINEPREFIX="${WINE_TMP_PATH}/${WINE_PREFIX_DIR_NAME}" ${WINE_PREFIX_PATH}/bin/wineboot --init

echo "Shutdown wine prefix"
WINEDLLOVERRIDES="winegstreamer=,mscoree,mshtml=d,winemenubuilder.exe=d" WINEPREFIX="${WINE_TMP_PATH}/${WINE_PREFIX_DIR_NAME}" ${WINE_PREFIX_PATH}/bin/wineboot --shutdown
WINEPREFIX="${WINE_TMP_PATH}/${WINE_PREFIX_DIR_NAME}" ${WINE_PREFIX_PATH}/bin/wineserver -k

echo "Create wine prefix archive"
cd ${WINE_TMP_PATH}
tar --remove-files -I "xz -T 0" -cf "${WINE_RESULT_PATH}" "${WINE_PREFIX_DIR_NAME}"
echo "Wine prefix archive path: ${WINE_RESULT_PATH}"

echo "Clean winlator rootfs paths and tmp files"
rm -r ${WINE_TMP_PATH}
rm "${WINLATOR_PREFIX}/usr/share/wine"
rm -r /data

echo "Generate wine prefix finished"
