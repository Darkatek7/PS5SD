#!/usr/bin/env bash
# Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
#
# Builds mbedTLS for the PS5 title into .deps/mbedtls-ps5 (include/, lib/): the
# TLS stack of the model downloader (ps5/app/https_client.cpp). PS5LM builds
# the same version for its payload with prospero-cmake; here it is built with
# the title's compilers (ps5/app/toolchain.cmake), like stable-diffusion.cpp.
#
# Incremental: a stamp holds the version and the configuration, and the build
# is skipped when they have not changed. scripts/build-app.sh calls it, with
# PS5_PAYLOAD_SDK and PS5_VULKAN_ROOT set; it can also run on its own after
# scripts/setup-deps.sh.
set -euo pipefail

VERSION=3.6.7
SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
deps="$root/.deps"
tarball="$deps/mbedtls-$VERSION.tar.bz2"
src="$deps/mbedtls-$VERSION"
build="$deps/mbedtls-build"
out="$deps/mbedtls-ps5"
export PS5_VULKAN_ROOT=${PS5_VULKAN_ROOT:-$deps/src/PS5_Vulkan}
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-$PS5_VULKAN_ROOT/.deps/native/ps5-payload-sdk}

# The downloader fetches over 16 connections at once, and TLS 1.3 handshakes
# share PSA's global key store, which is only thread-safe with these two
# (mbedTLS docs/architecture/psa-thread-safety; PS5LM and pacbrew's PS5
# package turn them on too). Entropy: the SDK is FreeBSD 11 (no getrandom),
# so mbedTLS takes sysctl(KERN_ARND), which libkernel exports.
config="THREADING_C THREADING_PTHREAD"
stamp="$out/.ps5sd-stamp"
want="$VERSION $SHA256 $config $(sha256sum "$root/ps5/app/toolchain.cmake" | cut -d' ' -f1)"
if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ] && [ -f "$out/lib/libmbedtls.a" ]; then
    echo "build-mbedtls: mbedTLS $VERSION in $out (up to date)"
    exit 0
fi

mkdir -p "$deps"
if [ ! -f "$tarball" ]; then
    curl -fsSL --retry 3 -o "$tarball.tmp" \
        "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$VERSION/mbedtls-$VERSION.tar.bz2"
    mv "$tarball.tmp" "$tarball"
fi
actual=$(sha256sum "$tarball" | cut -d' ' -f1)
[ "$actual" = "$SHA256" ] || { echo "build-mbedtls: checksum mismatch for $tarball" >&2; exit 1; }
rm -rf "$src" "$build"
if command -v bzip2 > /dev/null; then
    tar xjf "$tarball" -C "$deps"
else
    python3 -c 'import sys, tarfile; tarfile.open(sys.argv[1]).extractall(sys.argv[2])' "$tarball" "$deps"
fi
for option in $config; do
    python3 "$src/scripts/config.py" -f "$src/include/mbedtls/mbedtls_config.h" set "MBEDTLS_$option"
done

cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/ps5/app/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$out" \
    -DENABLE_PROGRAMS=OFF \
    -DENABLE_TESTING=OFF \
    -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
    -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
    -DMBEDTLS_FATAL_WARNINGS=OFF > "$deps/mbedtls-configure.log" 2>&1 ||
    { tail -30 "$deps/mbedtls-configure.log" >&2; exit 1; }
cmake --build "$build"
rm -rf "$out"
cmake --install "$build" > /dev/null
echo "$want" > "$stamp"
echo "build-mbedtls: mbedTLS $VERSION in $out"
