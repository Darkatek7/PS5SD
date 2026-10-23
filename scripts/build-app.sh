#!/usr/bin/env bash
# Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
#
# Builds PS5SD's native app: stable-diffusion.cpp with ggml-vulkan on RADV, as
# a title folder (eboot.bin, sce_sys/param.json, sce_module/libc.prx) for
# ShadowMountPlus, in build/app-ps5/title/<TITLE_ID>. The icon and the home
# screen background come from ps5/app/sce_sys (the background is a placeholder for now).
#
# Needs PS5_Vulkan next to its forks (.deps/src: PS5_Vulkan, PS5_Mesa,
# PS5_PayloadSDK), set up by scripts/setup-deps.sh (PS5_Vulkan's
# tools/setup-native-dependencies.sh, tools/build-radv.sh release and
# `make app`, for the host tool and libc.prx). Runs on x86-64 Linux; CI runs it
# in docker/Dockerfile's Arch image.
#
# The screen (ps5-homebrew-ui drawn through ps5-opengl) needs the ps5-opengl
# SDK: PS5_OPENGL_SDK, or the kit's pinned download. PS5SD_DISPLAY says what
# to do without it:
#
#   PS5SD_DISPLAY=auto (default)  the screen when the SDK is there, else none
#   PS5SD_DISPLAY=1               the screen, or fail
#   PS5SD_DISPLAY=0               no screen: main.cpp with PS5SD_NO_DISPLAY,
#                                 status in app.log, klog and notifications;
#                                 no ps5-opengl, no ps5-homebrew-ui, no font
#   PS5SD_WEBM=ON (default)       videos as WebM (libwebp, libwebm); OFF: AVI
#
#   scripts/build-app.sh            patch, configure, build, link and package
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
vk=${PS5_VULKAN_ROOT:-$root/.deps/src/PS5_Vulkan}
sdk="$vk/.deps/native/ps5-payload-sdk"
radv="$vk/.deps/native/radv-release"
archive="$radv/lib/libvulkan_radeon.ps5.a"
tool="$vk/build/host/ps5-native-tool"
sd="$root/third_party/stable-diffusion.cpp"
ggml="$sd/ggml"
kit="$root/third_party/ps5-homebrew-ui"
ftpsrv="$root/third_party/ftpsrv"
app_src="$root/ps5/app"
out="$root/build/app-ps5"
headers="$root/.deps/ps5-vk-headers"
sce_sys="$app_src/sce_sys"
param="$sce_sys/param.json"

# version.hpp's PS5SD_VERSION and param.json's contentVersion name one
# version (0.1.0 is 01.001.000: major+1, minor, patch); a release needs both.
version=$(sed -n 's/^#define PS5SD_VERSION "\(.*\)"$/\1/p' "$app_src/version.hpp")
content=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["contentVersion"])' "$param")
expected=$(echo "$version" | awk -F. '{ printf "%02d.%03d.%03d", $1 + 1, $2, $3 }')
[ "$content" = "$expected" ] || {
    echo "build-app: version.hpp says $version, so param.json's contentVersion must be $expected, not $content" >&2
    exit 2
}

webm=${PS5SD_WEBM:-ON}
case $webm in ON|OFF) ;; *) echo "build-app: PS5SD_WEBM is ON or OFF, not '$webm'" >&2; exit 2 ;; esac
[ -f "$sd/CMakeLists.txt" ] && [ -f "$ggml/CMakeLists.txt" ] && [ -d "$kit/src" ] && [ -f "$ftpsrv/Makefile.ps5" ] &&
    { [ "$webm" = OFF ] || { [ -f "$sd/thirdparty/libwebp/CMakeLists.txt" ] && [ -f "$sd/thirdparty/libwebm/CMakeLists.txt" ]; }; } || {
    echo "build-app: submodules missing; run: git submodule update --init third_party/stable-diffusion.cpp" \
        "third_party/ps5-homebrew-ui third_party/ftpsrv && git -C third_party/stable-diffusion.cpp submodule update --init" \
        "ggml thirdparty/libwebp thirdparty/libwebm" >&2
    exit 2
}
# ps5-opengl: PS5_OPENGL_SDK (scripts/setup-deps.sh sets one up in
# .deps/ps5-opengl-sdk), else pinned and sha256-checked through the kit's own
# script.
display=${PS5SD_DISPLAY:-auto}
case $display in auto|0|1) ;; *) echo "build-app: PS5SD_DISPLAY is auto, 0 or 1, not '$display'" >&2; exit 2 ;; esac
glsdk=
if [ "$display" != 0 ]; then
    if [ -n "${PS5_OPENGL_SDK:-}" ]; then
        glsdk=$PS5_OPENGL_SDK
    elif [ -f "$root/.deps/ps5-opengl-sdk/lib/libPS5OpenGL.a" ]; then
        glsdk=$root/.deps/ps5-opengl-sdk
    else
        glsdk=$(bash "$kit/tools/fetch-opengl-sdk.sh") || glsdk=
    fi
    if [ -n "$glsdk" ] && [ -f "$glsdk/lib/libPS5OpenGL.a" ]; then
        display=1
    elif [ "$display" = 1 ]; then
        echo "build-app: PS5SD_DISPLAY=1 but no ps5-opengl SDK (${glsdk:-none}); set PS5_OPENGL_SDK" \
            "or run scripts/setup-deps.sh" >&2
        exit 2
    else
        echo "build-app: WARNING: no ps5-opengl SDK; building without a display (PS5SD_DISPLAY=0)" >&2
        display=0
    fi
fi
echo "build-app: display: $([ "$display" = 1 ] && echo "ps5-opengl at $glsdk" || echo none)"

for f in "$archive" "$tool" "$vk/runtime/libc.prx" "$sdk/bin/prospero-lld"; do
    [ -e "$f" ] || { echo "build-app: missing $f (run scripts/setup-deps.sh)" >&2; exit 2; }
done
export PS5_PAYLOAD_SDK="$sdk" PS5_VULKAN_ROOT="$vk"
mkdir -p "$out"
# mbedTLS for the model downloader's HTTPS (scripts/build-mbedtls.sh; skipped
# when built already).
bash "$root/scripts/build-mbedtls.sh"
mbedtls="$root/.deps/mbedtls-ps5"

# radv-link.sh takes Clang's builtins from <resource dir>/lib/linux/, where
# Debian and Arch put them; Fedora keeps them under the target triple. A clang
# that reports a resource dir with that layout covers both.
builtins=$(clang --print-file-name=libclang_rt.builtins.a)
[ -f "$builtins" ] || builtins="$(clang --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a"
shim="$root/.deps/clang-shim"
mkdir -p "$shim/resource/lib/linux"
ln -sf "$builtins" "$shim/resource/lib/linux/libclang_rt.builtins-x86_64.a"
printf '#!/bin/sh\n[ "$1" = --print-resource-dir ] && { echo "%s"; exit 0; }\nexec clang "$@"\n' \
    "$shim/resource" > "$shim/clang"
chmod +x "$shim/clang"

# The Vulkan and SPIR-V headers, copied out of the host's /usr/include so the
# cross build never sees the host's libc headers next to them.
if [ ! -f "$headers/include/vulkan/vulkan.hpp" ]; then
    mkdir -p "$headers/include" "$headers/cmake"
    cp -r /usr/include/vulkan /usr/include/vk_video /usr/include/spirv "$headers/include/"
    cat > "$headers/cmake/SPIRV-HeadersConfig.cmake" <<EOF
add_library(SPIRV-Headers::SPIRV-Headers INTERFACE IMPORTED)
set_target_properties(SPIRV-Headers::SPIRV-Headers PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "$headers/include")
set(SPIRV-Headers_FOUND TRUE)
EOF
fi

# ---- the PS5 changes ----------------------------------------------------------
# patches/ggml onto the ggml submodule, patches/sdcpp onto stable-diffusion.cpp
# (docs/PORTING.md says what each one does). Each series is checked as a whole,
# since later patches touch the context of earlier ones (the server's 005x),
# by comparing trees at the paths it touches: the pinned commit with the series
# applied (built in a scratch index), and what the working tree holds. Equal:
# in place already. The working tree still at the pinned commit there: apply.
# Anything else is a tree changed by hand, which is left alone.
apply_series() { # apply_series <repo> <name> <patch>...
    local repo=$1 name=$2
    shift 2
    [ $# -gt 0 ] || return 0
    local tmp p path paths=() patched current pinned
    tmp=$(mktemp -d)
    for path in $(for p in "$@"; do git -C "$repo" apply --numstat "$p"; done | cut -f3 | sort -u); do
        # git add needs each path on disk or in the index.
        if [ -e "$repo/$path" ] || git -C "$repo" cat-file -e "HEAD:$path" 2> /dev/null; then paths+=("$path"); fi
    done
    GIT_INDEX_FILE="$tmp/patched" git -C "$repo" read-tree HEAD
    for p in "$@"; do
        GIT_INDEX_FILE="$tmp/patched" git -C "$repo" apply --cached "$p" ||
            { echo "build-app: $name: $p does not apply to the pinned commit" >&2; exit 1; }
    done
    patched=$(GIT_INDEX_FILE="$tmp/patched" git -C "$repo" write-tree)
    GIT_INDEX_FILE="$tmp/current" git -C "$repo" read-tree HEAD
    [ ${#paths[@]} -eq 0 ] || GIT_INDEX_FILE="$tmp/current" git -C "$repo" add -A -- "${paths[@]}"
    current=$(GIT_INDEX_FILE="$tmp/current" git -C "$repo" write-tree)
    pinned=$(git -C "$repo" rev-parse "HEAD^{tree}")
    rm -rf "$tmp"
    if [ "$current" = "$patched" ]; then
        echo "build-app: $name: $# patch(es) already applied"
    elif [ "$current" = "$pinned" ]; then
        for p in "$@"; do git -C "$repo" apply "$p"; done
        echo "build-app: $name: applied $# patch(es)"
    else
        echo "build-app: $name: the files the patches touch are neither pinned nor patched; reset $repo" \
            "(git checkout . && git clean -fd) and run this again" >&2
        exit 1
    fi
}
shopt -s nullglob
apply_series "$ggml" ggml "$root"/patches/ggml/*.patch
apply_series "$sd" stable-diffusion.cpp "$root"/patches/sdcpp/*.patch
apply_series "$ftpsrv" ftpsrv "$root"/patches/ftpsrv/*.patch
shopt -u nullglob

# ---- ftpsrv, PS5SD's copy ------------------------------------------------------
# A payload the title sends to elfldr when no ftpsrv answers (connect_ftp_writer
# in ps5/app/ftp_writer.hpp): payloads write to /data at full speed, the title
# does not after 1.4 GB. patches/ftpsrv puts it on 127.0.0.1:2122 only.
ftpsrv_elf="$out/ftpsrv/ps5sd-ftpsrv.elf"
mkdir -p "$out/ftpsrv"
make -C "$ftpsrv" -f Makefile.ps5 PS5_PAYLOAD_SDK="$sdk" ELF="$ftpsrv_elf" > "$out/ftpsrv.build.log" 2>&1 ||
    { tail -30 "$out/ftpsrv.build.log" >&2; exit 1; }

# ---- stable-diffusion.cpp and ggml, as static libraries -----------------------
flags="-I$headers/include"
# sd.cpp and ggml throw, catch and use dynamic_cast; the PS5 target turns
# exceptions and RTTI off by default.
cxxflags="$flags -fexceptions -fcxx-exceptions -frtti"
# Zen 2: AVX2, FMA, F16C and BMI2, no AVX-512. One static image: no backend
# modules (GGML_BACKEND_DL), no OpenMP (ggml's own thread pool), no libm (the
# math functions are in Sony's libc). WebP and WebM (thirdparty/libwebp and
# libwebm) for the videos of vid_gen; with PS5SD_WEBM=OFF a video is an MJPEG
# AVI, which browsers do not play.
# The HTTP server as the library sd-server-lib (patches/sdcpp/0050, main.cpp
# calls sd_server()), serving the web UI at / instead of the pnpm frontend: the
# parts in ps5/webui/src put together into one page (scripts/bundle-webui.sh).
index_h="$out/index_html.h"
sh "$root/scripts/bundle-webui.sh" "$out/index.html"
sh "$root/scripts/embed-file.sh" "$out/index.html" sd_server_index_html "$index_h.new"
cmp -s "$index_h.new" "$index_h" 2> /dev/null && rm -f "$index_h.new" || mv -f "$index_h.new" "$index_h"
cmake -S "$sd" -B "$out/sd" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$app_src/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$cxxflags" \
    -DBUILD_SHARED_LIBS=OFF -DSD_BUILD_SHARED_LIBS=OFF -DSD_BUILD_EXAMPLES=ON \
    -DSD_SERVER_LIBRARY=ON -DSD_SERVER_BUILD_FRONTEND=OFF -DSD_SERVER_EMBED_INDEX="$index_h" \
    -DSD_VULKAN=ON -DSD_WEBP="$webm" -DSD_WEBM="$webm" \
    -DGGML_BACKEND_DL=OFF -DGGML_NATIVE=OFF -DGGML_CPU_ALL_VARIANTS=OFF \
    -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON -DGGML_AVX512=OFF \
    -DGGML_OPENMP=OFF -DGGML_CCACHE=OFF \
    -DVulkan_INCLUDE_DIR="$headers/include" -DVulkan_LIBRARY="$archive" \
    -DVulkan_GLSLC_EXECUTABLE="$(command -v glslc)" -DSPIRV-Headers_DIR="$headers/cmake" \
    -DMATH_LIBRARY=SceLibcInternal > "$out/sd.configure.log" 2>&1 ||
    { tail -30 "$out/sd.configure.log" >&2; exit 1; }
cmake --build "$out/sd" --target stable-diffusion sd-server-lib

# ---- the app's own objects, and the title's start code from PS5_Vulkan ---------
obj="$out/obj"
mkdir -p "$obj" "$out/stubs"
app_cc() { "$app_src/ps5cc" -O2 "$@"; }
cxx() { "$app_src/ps5cxx" -std=c++20 -O2 -ffunction-sections -fdata-sections "$@"; }
eh=(-fexceptions -fcxx-exceptions -frtti)
sdinc=(-I "$sd/include" -I "$ggml/include" -I "$sd/thirdparty")
if [ "$display" = 1 ]; then
    kitinc=(-I "$kit/src" -I "$kit/third_party" -I "$glsdk/include" -DGL_GLEXT_PROTOTYPES=1)
else
    kitinc=(-DPS5SD_NO_DISPLAY=1)
fi
cxx "${eh[@]}" -I "$headers/include" "${sdinc[@]}" "${kitinc[@]}" -I "$app_src" \
    -c "$app_src/main.cpp" -o "$obj/main.o"
cxx "${eh[@]}" "${sdinc[@]}" -c "$app_src/gen_args.cpp" -o "$obj/gen_args.o"
cxx "${eh[@]}" -c "$app_src/app_status.cpp" -o "$obj/app_status.o"
cxx -std=c++17 "${eh[@]}" -I "$sd/thirdparty" -c "$app_src/console_stats.cpp" -o "$obj/console_stats.o"
# The model library (presets, checkpoints) and its JSON, with sd.cpp's nlohmann json; C++17,
# where path::u8string() is a std::string (C++20 makes it std::u8string).
cxx -std=c++17 "${eh[@]}" -I "$sd/thirdparty" -c "$app_src/models.cpp" -o "$obj/models.o"
cxx -std=c++17 "${eh[@]}" -I "$sd/thirdparty" -c "$app_src/net_family.cpp" -o "$obj/net_family.o"
cxx -std=c++17 "${eh[@]}" -I "$sd/thirdparty" -c "$app_src/mem_fit.cpp" -o "$obj/mem_fit.o"
cxx -std=c++17 "${eh[@]}" -I "$sd/thirdparty" -c "$app_src/gallery_labels.cpp" -o "$obj/gallery_labels.o"
cxx -std=c++17 "${eh[@]}" -I "$sd/thirdparty" -c "$app_src/prompt_tags.cpp" -o "$obj/prompt_tags.o"
# The model downloader (downloader.hpp): its pure part, the queue and routes, and the HTTPS
# client over mbedTLS with the Mozilla CA list (ps5/app/cacert.pem) built in.
ca_h="$out/cacert_pem.h"
sh "$root/scripts/embed-file.sh" "$app_src/cacert.pem" ps5sd_cacert_pem "$ca_h.new"
cmp -s "$ca_h.new" "$ca_h" 2> /dev/null && rm -f "$ca_h.new" || mv -f "$ca_h.new" "$ca_h"
cxx -std=c++17 "${eh[@]}" -I "$sd/thirdparty" -c "$app_src/download_core.cpp" -o "$obj/download_core.o"
cxx -std=c++17 "${eh[@]}" -I "$sd/thirdparty" -c "$app_src/downloader.cpp" -o "$obj/downloader.o"
cxx -std=c++17 "${eh[@]}" -I "$mbedtls/include" -I "$out" -c "$app_src/https_client.cpp" -o "$obj/https_client.o"
cxx -std=c++17 "${eh[@]}" -c "$app_src/ftp_writer.cpp" -o "$obj/ftp_writer.o"
cxx -std=c++17 "${eh[@]}" -I "$sd/thirdparty" -c "$app_src/upload.cpp" -o "$obj/upload.o"
if [ "$display" = 1 ]; then
    for src in display dashboard qrcodegen; do
        cxx "${eh[@]}" "${kitinc[@]}" -I "$app_src" -c "$app_src/$src.cpp" -o "$obj/$src.o"
    done
    # The dashboard decodes the finished PNGs with sd.cpp's stb_image (private to this object).
    cxx "${eh[@]}" -I "$sd/thirdparty" -I "$app_src" -c "$app_src/image_file.cpp" -o "$obj/image_file.o"
fi
app_cc -c "$app_src/compat_app.c" -o "$obj/compat_app.o"
# In an archive, so --exclude-libs keeps them local: a stub module defines some
# of the same names, and lld would otherwise export ours to interpose them,
# which the title converter refuses.
rm -f "$obj/libps5app_compat.a"
"$sdk/bin/llvm-ar" rcs "$obj/libps5app_compat.a" "$obj/compat_app.o"
cxx -fno-exceptions -fno-rtti -c "$vk/tooling/native/app_crt.cpp" -o "$obj/app_crt.o"
cxx -fexceptions -fcxx-exceptions -c "$app_src/cpp_runtime.cpp" -o "$obj/app_cpp_runtime.o"
app_objects=("$obj/app_crt.o" "$obj/app_cpp_runtime.o" "$obj/main.o" "$obj/gen_args.o" "$obj/app_status.o" "$obj/console_stats.o"
    "$obj/models.o" "$obj/net_family.o" "$obj/mem_fit.o" "$obj/gallery_labels.o" "$obj/prompt_tags.o" "$obj/download_core.o" "$obj/downloader.o" "$obj/https_client.o" "$obj/ftp_writer.o" "$obj/upload.o")
[ "$display" = 1 ] && app_objects+=("$obj/display.o" "$obj/dashboard.o" "$obj/qrcodegen.o" "$obj/image_file.o")

# ---- the screen (display builds only) --------------------------------------------
gl=
if [ "$display" = 1 ]; then
    # ps5-opengl (EGL, OpenGL 4.6) is a Mesa of its own, and shares thousands of
    # symbol names with RADV's. Linked into one object with only the GL and EGL API
    # left global, the two never meet. Rebuilt when the SDK changes.
    gl="$out/gl/gl_api.o"
    if [ ! -f "$gl" ] || [ "$glsdk/lib/libPS5OpenGL.a" -nt "$gl" ]; then
        mkdir -p "$out/gl"
        gllibs=()
        for l in $(sed -n '/GROUP (/,/)/p' "$glsdk/lib/libPS5OpenGL.a" | grep -oE 'lib[a-z0-9_.]+\.a'); do
            gllibs+=("$glsdk/lib/$l")
        done
        "$sdk/bin/llvm-nm" --defined-only -g "$glsdk/lib/libps5_opengl_core33.a" "$glsdk/lib/libglapi_bridge.a" \
            "$glsdk/lib/libglapi.a" 2>/dev/null | awk 'NF>=3 && $2 ~ /[TW]/ {print $3}' |
            grep -E '^(gl|egl)[A-Z]|^ps5_opengl' | sort -u > "$out/gl/api.txt"
        us=()
        while read -r name; do us+=(-u "$name"); done < "$out/gl/api.txt"
        "$sdk/bin/ld.lld" -r "${us[@]}" -u ps5_agc_gate2_run --start-group "${gllibs[@]}" --end-group \
            -o "$out/gl/gl_all.o"
        "$sdk/bin/llvm-objcopy" --keep-global-symbols="$out/gl/api.txt" "$out/gl/gl_all.o" "$gl"
        rm -f "$out/gl/gl_all.o" "$out/gl/libps5sd_gl.a"
        # In an archive, so --exclude-libs keeps the API local: SDK stub modules
        # define EGL names too, and lld would export ours to interpose them.
        "$sdk/bin/llvm-ar" rcs "$out/gl/libps5sd_gl.a" "$gl"
    fi
    # ps5-homebrew-ui's kit (gfx, ui, core, audio and its console platform layer,
    # without its shell, designs or demo), drawn through ps5-opengl. The status
    # screen uses a little of it; the archive only gives the link what it needs.
    kitflags=(-O2 -ffunction-sections -fdata-sections "${kitinc[@]}" -I "$app_src")
    mkdir -p "$obj/kit"
    mapfile -t kit_sources < <(find "$kit/src/gfx" "$kit/src/ui" "$kit/src/core" "$kit/src/audio" "$kit/src/third_party" \
        "$kit/src/platform/ps5" -type f \( -name '*.cpp' -o -name '*.c' \) ! -name display_egl.cpp | sort)
    kit_objects=()
    for src in "${kit_sources[@]}"; do
        o="$obj/kit/$(echo "${src#"$kit/src/"}" | tr '/' '_').o"
        kit_objects+=("$o")
        [ -f "$o" ] && [ "$o" -nt "$src" ] && continue
        if [[ $src == *.c ]]; then
            "$app_src/ps5cc" -std=c11 -O2 -w -I "$kit/src" -c "$src" -o "$o" &
        else
            cxx "${kitflags[@]}" -w -c "$src" -o "$o" &
        fi
        (( $(jobs -r | wc -l) >= $(nproc) )) && wait -n
    done
    wait
    rm -f "$obj/libps5sd_kit.a"
    "$sdk/bin/llvm-ar" rcs "$obj/libps5sd_kit.a" "${kit_objects[@]}"
fi

# ---- AGC ---------------------------------------------------------------------------
# AGC comes from system modules; these stubs only name the imports, every one
# either driver (the GL object, RADV; RADV alone without a display) makes.
agc_imports=$("$sdk/bin/llvm-nm" -u $gl "$archive" 2>/dev/null | awk '{print $NF}' | grep -E '^sceAgc' | sort -u)
for lib in libSceAgc libSceAgcDriver; do
    stub_c="$out/stubs/$lib.c"
    : > "$stub_c"
    for name in $agc_imports; do
        case $lib:$name in
            libSceAgcDriver:sceAgcDriver*) echo "void $name(void) {}" >> "$stub_c" ;;
            libSceAgc:sceAgcDriver*) ;;
            libSceAgc:*) echo "void $name(void) {}" >> "$stub_c" ;;
        esac
    done
    "$app_src/ps5cc" -O2 -fPIC -c "$stub_c" -o "$obj/${lib}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "$lib.prx" -o "$out/stubs/$lib.so" "$obj/${lib}_stub.o"
done
stubs=("$out/stubs/libSceAgc.so" "$out/stubs/libSceAgcDriver.so")

# ---- the link recipe -------------------------------------------------------------
# shellcheck source=/dev/null
source "$vk/tools/radv-link.sh"
PS5_CLANG="$shim/clang" radv_link_recipe "$vk" "$sdk" "$archive" || exit 2
# The platform's getaddrinfo always fails; ps5/app/compat_app.c has a numeric one.
link_flags=()
for f in "${radv_link_flags[@]}"; do
    case $f in --defsym=getaddrinfo=*|--defsym=freeaddrinfo=*) ;; *) link_flags+=("$f") ;; esac
done
# Both drivers initialise AGC; display.cpp's wrap lets the first call do it.
# Without a display RADV is the only caller, once, and needs no wrap.
[ "$display" = 1 ] && link_flags+=(--wrap=sceAgcInit)
mapfile -t sd_libs < <(find "$out/sd" -name '*.a' | sort)
local_archives=("${sd_libs[@]}" "$obj/libps5app_compat.a")
# mbedTLS, in the archive group so --exclude-libs keeps it local.
for l in mbedtls mbedx509 mbedcrypto everest p256m; do
    [ -f "$mbedtls/lib/lib$l.a" ] && local_archives+=("$mbedtls/lib/lib$l.a")
done
[ "$display" = 1 ] && local_archives+=("$obj/libps5sd_kit.a" "$out/gl/libps5sd_gl.a")
sdk_stubs=("$sdk"/target/lib/*.so)

# ---- missing symbols report --------------------------------------------------------
# Names the link inputs reference (U, not weak) that nothing defines: not an
# object or archive of the link, not the SDK's stub modules, not the AGC stubs,
# not a --defsym or --wrap of the recipe. Archive members the link does not pull
# in count too, so not every name here is an error; when the link fails on an
# undefined symbol, the cause is in this list. Written to missing-symbols.txt.
missing_symbols_report() {
    local nm="$sdk/bin/llvm-nm" files=() f
    for f in "${radv_link_inputs[@]}"; do [[ $f != -* && -f $f ]] && files+=("$f"); done
    local refs=("${app_objects[@]}" "${local_archives[@]}" "$archive")
    {
        "$nm" -g --defined-only "${app_objects[@]}" "${local_archives[@]}" "${files[@]}" 2>/dev/null |
            awk 'NF>=3 {print $NF}'
        "$nm" -D --defined-only "${sdk_stubs[@]}" "${stubs[@]}" 2>/dev/null | awk 'NF>=3 {print $NF}'
        for f in "${link_flags[@]}"; do
            case $f in
                --defsym=*) f=${f#--defsym=}; echo "${f%%=*}" ;;
                --wrap=*) echo "__wrap_${f#--wrap=}" ;;
            esac
        done
    } | sort -u > "$out/defined-symbols.txt"
    "$nm" -u "${refs[@]}" 2>/dev/null | awk '$1 == "U" {print $2}' | sed 's/^__real_//' | sort -u |
        comm -23 - "$out/defined-symbols.txt" > "$out/missing-symbols.txt"
    local n
    n=$(wc -l < "$out/missing-symbols.txt")
    if [ "$n" -gt 0 ]; then
        echo "build-app: $n name(s) referenced by the link inputs and defined nowhere" \
            "(unused archive members count too; full list in $out/missing-symbols.txt):"
        head -60 "$out/missing-symbols.txt" | sed 's/^/  /'
    else
        echo "build-app: every referenced name has a definition"
    fi
}
missing_symbols_report

# ---- link ---------------------------------------------------------------------------
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${link_flags[@]}" \
    --version-script "$app_src/app-symbols.map" --exclude-libs=ALL \
    -e _start -o "$out/llvm-pie.elf" \
    "${app_objects[@]}" \
    --start-group "${local_archives[@]}" --end-group \
    "${stubs[@]}" \
    "${radv_link_inputs[@]}" \
    --as-needed "${sdk_stubs[@]}"

# Imports that only libScePosixForWebKit or libkernel_sys define among the SDK's
# stubs: those modules give a title nothing, so each is a call through NULL at
# run time (PS5LM found isatty, getnameinfo, mkstemp, readlink, ... this way).
# Each one wants a definition in ps5/app/compat_app.c.
null_imports_report() {
    local nm="$sdk/bin/llvm-nm" null_libs=() other_libs=() f
    for f in "${sdk_stubs[@]}"; do
        case $(basename "$f") in
            libScePosixForWebKit*|libkernel_sys*) null_libs+=("$f") ;;
            *) other_libs+=("$f") ;;
        esac
    done
    [ ${#null_libs[@]} -gt 0 ] || return 0
    "$nm" -D --defined-only "${other_libs[@]}" "${stubs[@]}" 2>/dev/null | awk 'NF>=3 {print $NF}' | sort -u \
        > "$out/other-stub-symbols.txt"
    "$nm" -D --defined-only "${null_libs[@]}" 2>/dev/null | awk 'NF>=3 {print $NF}' | sort -u |
        comm -23 - "$out/other-stub-symbols.txt" > "$out/null-stub-symbols.txt"
    "$nm" -D -u "$out/llvm-pie.elf" 2>/dev/null | awk '{print $NF}' | sort -u |
        comm -12 - "$out/null-stub-symbols.txt" > "$out/null-imports.txt"
    if [ -s "$out/null-imports.txt" ]; then
        echo "build-app: WARNING: imports only a module a title does not load defines (null at run time):"
        sed 's/^/  /' "$out/null-imports.txt"
    fi
}
null_imports_report

"$tool" link --in "$out/llvm-pie.elf" --out "$out/eboot.elf" \
    --stub-dir "$sdk/target/lib" --stub "$out/stubs/libSceAgc.so" \
    --stub "$out/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf

# ---- the title folder ---------------------------------------------------------------
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$param")
app="$out/title/$title_id"
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$out/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$param" "$app/sce_sys/param.json"
# The home screen backgrounds (3840x2160 BC7 DDS) are made from
# sce_sys/source/background.png by PS5_Vulkan's asset tools, and only when it
# changes. The icon was made on the console with FLUX.3 Klein (docs/PORTING.md);
# TODO: the background is still a generated placeholder (ShadowMountPlus copies
# both at registration).
assets="$out/assets"
if [ ! -f "$assets/pic0.dds" ] || [ "$sce_sys/source/background.png" -nt "$assets/pic0.dds" ] ||
    [ "$sce_sys/icon0.png" -nt "$assets/pic0.dds" ]; then
    mkdir -p "$assets"
    (cd "$vk" && bash tools/setup-asset-dependencies.sh > /dev/null &&
        bash tools/prepare-assets.sh --icon "$sce_sys/icon0.png" --background "$sce_sys/source/background.png" \
            --output-directory "$assets" > /dev/null)
fi
# icon0.dds (BC7, 512x512, one mip) is what the home screen draws; icon0.png
# is the package's. Both are committed; after a new icon0.png, make the .dds
# again on Windows: texconv -f BC7_UNORM -m 1 -y -o ps5/app/sce_sys ps5/app/sce_sys/icon0.png
# Only for an icon0.png that differs from the committed one: a fresh checkout
# (CI) gives both files checkout times, in either order.
if git -C "$root" diff --quiet HEAD -- "$sce_sys/icon0.png" 2> /dev/null; then
    :
elif [ "$sce_sys/icon0.png" -nt "$sce_sys/icon0.dds" ]; then
    echo "build-app: sce_sys/icon0.dds is older than icon0.png; make it again (texconv, see above)" >&2
    exit 2
fi
cp "$sce_sys/icon0.png" "$sce_sys/icon0.dds" "$assets/pic0.dds" "$assets/pic1.dds" "$app/sce_sys/"
cp "$vk/runtime/libc.prx" "$app/sce_module/libc.prx"
# The dashboard's fonts (ps5-homebrew-ui's Inter, Montserrat and DejaVu Sans Mono), with their
# licences. main.cpp passes the DejaVu path; Dashboard::load reads the others from its folder.
if [ "$display" = 1 ]; then
    mkdir -p "$app/assets/fonts"
    cp "$kit/assets/fonts/"{inter-regular,inter-semibold,montserrat-medium,dejavu-sans-mono}.huifont \
        "$kit/assets/fonts/"{Inter,Montserrat,DejaVu}-LICENSE.txt "$app/assets/fonts/"
fi
# ftpsrv (GPL-3.0+) with its licence and where its source is.
# The docs for the web UI's Help tab (GET /ps5sd/v1/app/docs/<name>).
mkdir -p "$app/docs"
cp "$root/docs/MODELS.md" "$root/docs/API.md" "$app/docs/"
mkdir -p "$app/ftpsrv"
cp "$ftpsrv_elf" "$app/ftpsrv/ps5sd-ftpsrv.elf"
cp "$ftpsrv/LICENSE" "$app/ftpsrv/LICENSE.txt"
printf '%s\n' "ftpsrv by John Törnblom and drakmor, GPL-3.0-or-later." \
    "Source: https://github.com/drakmor/ftpsrv at $(git -C "$ftpsrv" rev-parse HEAD) (1.16-ng-stable)," \
    "with PS5SD's patches/ftpsrv (https://github.com/Darkatek7/PS5SD): 127.0.0.1:2122 only." \
    > "$app/ftpsrv/SOURCE.txt"
# ps5-exporter (Marice, GPL-3.0): a title may not read the temperature, fan or SoC power, a
# payload may (console_stats.hpp). The app sends it to the ELF loader when it does not answer.
# Pinned by version and hash, downloaded once into .deps (the same file PS5LM carries).
exporter="$root/.deps/ps5-exporter-0.2.0.elf"
exporter_sha=0d19f7a293eeb30178ba562a3712abfdc50f6b6ebbe180ceaad92dc557948ba8
if [ ! -f "$exporter" ]; then
    curl -sSfL -o "$exporter.part" https://github.com/Marice/ps5-exporter/releases/download/v0.2.0/ps5-exporter.elf
    mv "$exporter.part" "$exporter"
fi
echo "$exporter_sha  $exporter" | sha256sum -c --quiet || { echo "build-app: ps5-exporter hash mismatch" >&2; exit 1; }
mkdir -p "$app/payloads"
cp "$exporter" "$app/payloads/ps5-exporter.elf"
printf '%s\n' "ps5-exporter v0.2.0 by Marice, GPL-3.0-or-later: https://github.com/Marice/ps5-exporter" \
    "Source: https://github.com/Marice/ps5-exporter/tree/v0.2.0" > "$app/payloads/ps5-exporter-NOTICE.txt"
# libwebp and libwebm (BSD-3-Clause), linked for the WebM videos: their licences go with the binary.
rm -rf "$app/licenses"
if [ "$webm" = ON ]; then
    mkdir -p "$app/licenses"
    cp "$sd/thirdparty/libwebp/COPYING" "$app/licenses/libwebp-COPYING.txt"
    cp "$sd/thirdparty/libwebp/PATENTS" "$app/licenses/libwebp-PATENTS.txt"
    cp "$sd/thirdparty/libwebm/LICENSE.TXT" "$app/licenses/libwebm-LICENSE.txt"
fi
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
echo "build-app: $app ($(stat -c %s "$app/eboot.bin") bytes, $([ "$display" = 1 ] && echo "with" || echo "without") a display)"
