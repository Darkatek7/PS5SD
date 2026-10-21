#!/usr/bin/env bash
# Renders the title's dashboard on the PC, no console and no GPU: ps5-homebrew-ui
# on Mesa's surfaceless EGL (llvmpipe), as the kit's own tools/host-snapshots.sh
# does, drawing a few made-up states of AppStatus (loading, ready, generating
# with a preview at 40%, done, error, the log) at 1920x1080 into
# build/app-preview/*.png.
#
# It compiles the same files the title does (ps5/app/dashboard.cpp,
# app_status.cpp, image_file.cpp) against the same kit subset (gfx, ui, core,
# audio and its third-party code), as a static archive so the link pulls only
# what is used, as on the console. Only the display, the platform layer and
# the pictures come from the host (host/platform_host.cpp, the preview program).
#
#   scripts/preview-app-ui.sh [output dir] [width height]
#
# CI runs it inside docker/Dockerfile's image (mesa, clang, ninja, binutils)
# and uploads the PNGs. Locally, from Windows:
#   MSYS_NO_PATHCONV=1 docker run --rm -v "$PWD:/work" -w /work ps5sd-build \
#       bash -c 'git config --global --add safe.directory "*" && scripts/preview-app-ui.sh'
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
kit="$root/third_party/ps5-homebrew-ui"
sd="$root/third_party/stable-diffusion.cpp"
app="$root/ps5/app"
build="$root/build/app-preview-obj"
output=${1:-"$root/build/app-preview"}
size=("${@:2}")

[ -d "$kit/src" ] && [ -f "$sd/thirdparty/stb_image.h" ] || {
    echo "preview-app-ui: submodules missing; run: git submodule update --init third_party/ps5-homebrew-ui" \
        "third_party/stable-diffusion.cpp" >&2
    exit 2
}
# No ccache in the image; the kit's ninja helper wants one unless told not to.
export USE_CCACHE=${USE_CCACHE:-0}
# shellcheck source=/dev/null
source "$kit/tools/ninja-build.sh"
cxx=$(command -v "${HOST_CXX:-clang++}")
cc=$(command -v "${HOST_CC:-clang}")
ar=$(command -v "${HOST_AR:-ar}")

# What the no-display build compiles of this: AppStatus on its own, with no
# kit, GL or sd.cpp header on the include path.
"$cxx" -std=c++17 -Wall -Wextra -Werror -fsyntax-only -I "$app" "$app/app_status.cpp"
echo "preview-app-ui: app_status.cpp compiles without the kit (the no-display build)"

ninja_begin "$build/build.ninja"

# The kit as the console build has it: gfx, ui, core, audio, third_party; the
# platform layer is host/platform_host.cpp instead of src/platform/ps5.
kit_objects=()
while IFS= read -r -d '' source; do
    relative=${source#"$kit/"}
    object="$build/kit/${relative//\//_}.o"
    if [[ $source == *.c ]]; then
        ninja_inputs=("$source" "$cc")
        ninja_edge CC "$object" "${compiler_cache[@]}" "$cc" -std=c11 -O2 -w -I"$kit/src" \
            -MD -MF "$object.d" -c "$source" -o "$object"
    else
        ninja_inputs=("$source" "$cxx")
        ninja_edge CXX "$object" "${compiler_cache[@]}" "$cxx" -std=c++20 -O2 -w -DGL_GLEXT_PROTOTYPES=1 \
            -I"$kit/src" -I"$kit/third_party" -MD -MF "$object.d" -c "$source" -o "$object"
    fi
    kit_objects+=("$object")
done < <(find "$kit/src/gfx" "$kit/src/ui" "$kit/src/core" "$kit/src/audio" "$kit/src/third_party" \
    -type f \( -name '*.cpp' -o -name '*.c' \) -print0 | sort -z)
host_object="$build/kit/platform_host.o"
ninja_inputs=("$kit/host/platform_host.cpp" "$cxx")
ninja_edge CXX "$host_object" "${compiler_cache[@]}" "$cxx" -std=c++20 -O2 -w -I"$kit/src" \
    -MD -MF "$host_object.d" -c "$kit/host/platform_host.cpp" -o "$host_object"
kit_objects+=("$host_object")
archive="$build/libkit.a"
ninja_inputs=("${kit_objects[@]}" "$ar")
ninja_edge AR "$archive" bash -c 'rm -f "$1" && shift && "$@"' _ "$archive" "$ar" rcs "$archive" "${kit_objects[@]}"

# The dashboard's own files, warnings on.
app_objects=()
for source in "$app/dashboard.cpp" "$app/qrcodegen.cpp" "$app/app_status.cpp" "$app/image_file.cpp" "$app/preview/preview_main.cpp"; do
    object="$build/app/$(basename "$source").o"
    ninja_inputs=("$source" "$cxx")
    ninja_edge CXX "$object" "${compiler_cache[@]}" "$cxx" -std=c++20 -O2 -Wall -Wextra -DGL_GLEXT_PROTOTYPES=1 \
        -I"$app" -I"$kit/src" -I"$kit/third_party" -isystem "$sd/thirdparty" -MD -MF "$object.d" -c "$source" -o "$object"
    app_objects+=("$object")
done
exe="$build/preview_app"
ninja_inputs=("${app_objects[@]}" "$archive")
ninja_edge LINK "$exe" "$cxx" "${app_objects[@]}" "$archive" -lEGL -lGL -lm -o "$exe"
if ! ninja_run > "$build/build.log" 2>&1; then
    grep -E 'error|FAILED|undefined' -A6 "$build/build.log" | head -80 >&2
    exit 1
fi
grep -E 'warning' -A4 "$build/build.log" | head -40 >&2 || true

rm -rf "$output"
mkdir -p "$output"
# The newest picture is the one the first console image made, when the file is there.
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
    "$exe" "$kit/assets/fonts" "$output" "$root/build/app-preview-work" "$root/docs/images/first-image.png" "${size[@]}"
echo "preview-app-ui: $(ls "$output"/*.png | wc -l) pictures in $output"
