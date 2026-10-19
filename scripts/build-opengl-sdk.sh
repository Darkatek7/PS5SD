#!/usr/bin/env bash
# Sets up the ps5-opengl SDK (EGL and OpenGL 4.6 for the PS5) that the screen
# of scripts/build-app.sh links, in .deps/ps5-opengl-sdk, and prints that path.
#
# ps5-homebrew-ui pins the SDK as a release download (ps5-opengl 1.0.0 on
# github.com/blackbearreloaded/ps5-opengl); that account was deleted in
# October 2026 and the download is gone. So, in this order:
#
#   1. .deps/ps5-opengl-sdk already holds an SDK made by this script (the
#      stamp names the same source) whose manifest still verifies: kept.
#   2. The original release archive, accepted only with its pinned sha256
#      (and then its manifest's): if it ever comes back, nothing is built.
#   3. Built from source: leolobato/ps5-opengl, a fork with the history of the
#      original, at 1.0.0 plus the original authors' 1.0.1 fixes (see the pin
#      below), the way that repository's release workflow built 1.0.0
#      (payload SDK v0.42, the "dynamic" display
#      profile: 2160p120 capacity, 1080p60 at start, the mode chosen at run
#      time through ps5_opengl_display_modes.h; GPU present, multi-draw and
#      deferred draw batching on).
#
# The source build fetches Mesa 26.2.0 (archive.mesa3d.org, sha256-pinned),
# PS4-OpenGNM's opengnm and opengnm-psbc, SPIRV-Headers, Vulkan-Headers,
# imgui, nanovg and sokol at the commits ps5-opengl pins (its
# tools/fetch-sources.py checks them), and the payload SDK release. It needs
# the host packages of docker/packages.txt (clang/llvm with llvm-config, gcc,
# meson, python-mako, python-yaml, bison, flex, patch) and takes 30 to 60
# minutes; CI keeps the result in its .deps cache.
#
#   scripts/build-opengl-sdk.sh          prints the SDK prefix
#   PS5_OPENGL_FORCE_SOURCE=1 ...        skip the release download (testing)
set -euo pipefail

# The original release (the pin of ps5-homebrew-ui's tools/fetch-opengl-sdk.sh).
RELEASE_URL=https://github.com/blackbearreloaded/ps5-opengl/releases/download/v1.0.0/ps5-opengl-sdk-1.0.0.tar.gz
RELEASE_SHA256=f93643c04c843d56143b00951df1f8042ea7706ae9f19e4e9abf158e1ead77c5
RELEASE_MANIFEST_SHA256=f4b91f672be037fbac3f82494f1225deaf4c227a03f37ac3ffa56abb213b943f
# The source: 217da45 (2026-10-04, BlackBearReloaded), ten commits after the
# v1.0.0 tag (dce7491): 1.0.0 plus the 1.0.1 vertex-buffer reference fix and
# "Store vertex binding alignments per byte in the compiler". The tag itself
# does not build with Arch's clang 23: its opengnm-psbc patch writes a
# uint32_t through a pointer into radv's uint8_t vertex_binding_align[], an
# incompatible-pointer-types error since clang 23 (a warning for the clang 21
# 1.0.0 was released with), and that commit is the upstream fix. The diff
# from 1.0.0 is that fix, ps5_screen.c's buffer-reference fixes, tests and
# docs; no API change. The fork's main adds more (a varying-linkage fix, the
# Gallium HUD, an SDL2 bridge) that nothing here needs.
PS5_OPENGL_URL=${PS5_OPENGL_URL:-https://github.com/leolobato/ps5-opengl}
PS5_OPENGL_REV=${PS5_OPENGL_REV:-217da45c6f95cf0e8c8ba8019c4b5f3d2cc74e0f}
# What ps5-opengl's release workflow and dependencies.json use.
PAYLOAD_SDK_URL=https://github.com/ps5-payload-dev/sdk/releases/download/v0.42/ps5-payload-sdk.zip
PAYLOAD_SDK_SHA256=8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="$root/.deps/ps5-opengl-sdk"
stamp="$root/.deps/ps5-opengl-sdk.stamp"
# Scratch: the release download, or the source build (several GiB with Mesa and
# both opengnm-psbc builds). Removed once the SDK is in place, so the CI cache
# of .deps only keeps the SDK; kept after a failure, to look at.
work="$root/.deps/ps5-opengl-work"
src="$work/src"

log() { printf '==> [opengl] %s\n' "$*" >&2; }
die() { printf 'build-opengl-sdk: %s\n' "$*" >&2; exit 1; }
sha256_of() { sha256sum "$1" | cut -d' ' -f1; }
manifest_ok() { [ -f "$1/manifest.sha256" ] && (cd "$1" && sha256sum --check --strict --quiet manifest.sha256); }

# What the SDK in $prefix must have been made from to be kept.
want_release="release $RELEASE_SHA256"
want_source="source $PS5_OPENGL_URL $PS5_OPENGL_REV dynamic payload-sdk $PAYLOAD_SDK_SHA256"

# ---- 1. in place -----------------------------------------------------------------
if [ -f "$stamp" ] && manifest_ok "$prefix" && [ -f "$prefix/lib/libPS5OpenGL.a" ]; then
    have=$(cat "$stamp")
    if [ "$have" = "$want_source" ] || { [ "$have" = "$want_release" ] && [ "${PS5_OPENGL_FORCE_SOURCE:-0}" != 1 ]; }; then
        log "SDK in place ($(cut -d' ' -f1 <<< "$have")): $prefix"
        printf '%s\n' "$prefix"
        exit 0
    fi
fi

mkdir -p "$root/.deps" "$work"

# ---- 2. the original release ------------------------------------------------------
if [ "${PS5_OPENGL_FORCE_SOURCE:-0}" != 1 ]; then
    archive="$work/ps5-opengl-sdk-1.0.0.tar.gz"
    log "trying the original release archive"
    if curl -fsSL --retry 2 -o "$archive.part" "$RELEASE_URL" 2> /dev/null; then
        if [ "$(sha256_of "$archive.part")" = "$RELEASE_SHA256" ]; then
            rm -rf -- "$work/release" "$prefix"
            mkdir -p "$work/release"
            tar -xzf "$archive.part" -C "$work/release" ps5-opengl-sdk-1.0.0/sdk
            if manifest_ok "$work/release/ps5-opengl-sdk-1.0.0/sdk" &&
                [ "$(sha256_of "$work/release/ps5-opengl-sdk-1.0.0/sdk/manifest.sha256")" = "$RELEASE_MANIFEST_SHA256" ]; then
                mv -- "$work/release/ps5-opengl-sdk-1.0.0/sdk" "$prefix"
                printf '%s\n' "$want_release" > "$stamp"
                rm -rf -- "$work"
                log "release 1.0.0 verified: $prefix"
                printf '%s\n' "$prefix"
                exit 0
            fi
            log "the release archive's manifest does not verify; building from source"
        else
            log "the release archive's sha256 is not the pinned one; building from source"
        fi
    else
        log "the release archive is not available; building from source"
    fi
    rm -f -- "$archive.part"
fi

# ---- 3. from source ----------------------------------------------------------------
for c in git curl unzip python3 make gcc g++ meson ninja patch bison flex ar nm llvm-config; do
    command -v "$c" > /dev/null || die "missing $c (install docker/packages.txt, or use docker/Dockerfile)"
done
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || die "the build needs Linux x86-64; use docker/Dockerfile"

# The source, at the pinned commit. ps5-opengl's fetch-sources.py never resets
# the trees it patched, so a checkout of another commit starts over.
if [ -d "$src/.git" ] && [ "$(git -C "$src" rev-parse -q --verify HEAD 2> /dev/null || true)" != "$PS5_OPENGL_REV" ]; then
    rm -rf -- "$src"
fi
mkdir -p "$src"
[ -d "$src/.git" ] || git -C "$src" init -q
git -C "$src" remote get-url origin > /dev/null 2>&1 && git -C "$src" remote set-url origin "$PS5_OPENGL_URL" ||
    git -C "$src" remote add origin "$PS5_OPENGL_URL"
if ! git -C "$src" cat-file -e "$PS5_OPENGL_REV^{commit}" 2> /dev/null; then
    log "fetching $PS5_OPENGL_URL at ${PS5_OPENGL_REV:0:12}"
    git -C "$src" fetch -q --depth 1 origin "$PS5_OPENGL_REV" || die "cannot fetch $PS5_OPENGL_REV from $PS5_OPENGL_URL"
fi
if [ "$(git -C "$src" rev-parse -q --verify HEAD 2> /dev/null || true)" != "$PS5_OPENGL_REV" ]; then
    git -C "$src" checkout -q -f --detach "$PS5_OPENGL_REV"
fi

# The payload SDK release ps5-opengl builds with (its clang wrappers use the
# host's LLVM through llvm-config).
psdk="$work/ps5-payload-sdk"
if [ ! -x "$psdk/bin/prospero-clang" ]; then
    log "fetching payload SDK v0.42"
    curl -fL --retry 3 -o "$work/payload-sdk.zip" "$PAYLOAD_SDK_URL"
    [ "$(sha256_of "$work/payload-sdk.zip")" = "$PAYLOAD_SDK_SHA256" ] || die "payload SDK v0.42 checksum mismatch"
    rm -rf -- "$psdk"
    unzip -q "$work/payload-sdk.zip" -d "$work"
    rm -f -- "$work/payload-sdk.zip"
fi
"$psdk/bin/prospero-clang" --version > /dev/null || die "the payload SDK's clang does not run (llvm-config?)"

log "fetching ps5-opengl's pinned sources"
(cd "$src" && python3 tools/fetch-sources.py >&2)

# What `make sdk-gl46` runs, without its host-only compiler test
# (tests/ps5/test_shader_cache.py, which calls clang-18 by that name): the
# host build of opengnm-psbc (it also generates sources the PS5 build needs),
# its PS5 build, Mesa's GL front end, then the runtime and the install.
export PS5_PAYLOAD_SDK="$psdk" PS5_CCACHE=0
export PS5_SCANOUT_HEIGHT=2160 PS5_SCANOUT_FPS=120 PS5_DYNAMIC_SCANOUT=1
export PS5_GPU_PRESENT_BATCH=1 PS5_DRAW_PROFILE=1 PS5_MULTIDRAW_BATCH=1 PS5_DEFERRED_DRAW_BATCH=1
build_log="$work/build.log"
log "building (log: $build_log)"
# One && chain: `set -e` does not hold in a subshell on the left of ||.
(
    cd "$src" &&
        bash toolchain/build-opengnm-psbc.sh &&
        bash toolchain/build-opengnm-psbc-ps5.sh &&
        PS5_MESA_CROSS_FILE="$psdk/toolchain/prospero.ini" bash toolchain/build-mesa-ps5.sh &&
        rm -rf -- "$work/sdk" &&
        bash toolchain/install-ps5-opengl-gl46.sh "$work/sdk" &&
        python3 tests/ps5/verify_gl46_link_surface.py
) > "$build_log" 2>&1 || { tail -80 "$build_log" >&2; die "the ps5-opengl build failed (full log: $build_log)"; }
tail -3 "$build_log" >&2

manifest_ok "$work/sdk" || die "the built SDK's manifest does not verify"
for f in include/EGL/egl.h include/GL/glcorearb.h include/ps5_opengl_display.h include/ps5_opengl_display_modes.h \
    lib/libPS5OpenGL.a lib/libps5_opengl_core33.a lib/libglapi_bridge.a lib/libglapi.a lib/libSceAgc.so \
    lib/libSceAgcDriver.so; do
    [ -f "$work/sdk/$f" ] || die "the built SDK has no $f"
done
rm -rf -- "$prefix"
mv -- "$work/sdk" "$prefix"
printf '%s\n' "$want_source" > "$stamp"
cp "$build_log" "$root/.deps/ps5-opengl-sdk.build.log"
rm -rf -- "$work"
log "built from ${PS5_OPENGL_REV:0:12}: $prefix"
printf '%s\n' "$prefix"
