#!/usr/bin/env bash
# Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
#
# Builds PS5SD's native app: stable-diffusion.cpp with ggml-vulkan on RADV, as
# a title folder (eboot.bin, sce_sys/param.json, sce_module/libc.prx) for
# ShadowMountPlus, in build/app-ps5/title/<TITLE_ID>. The icon and the home
# screen background come from ps5/app/sce_sys (placeholders for now).
#
# Needs PS5_Vulkan next to its forks (.deps/src: PS5_Vulkan, PS5_Mesa,
# PS5_PayloadSDK), set up by scripts/setup-deps.sh (PS5_Vulkan's
# tools/setup-native-dependencies.sh, tools/build-radv.sh release and
# `make app`, for the host tool and libc.prx). Runs on x86-64 Linux; CI runs it
# in docker/Dockerfile's Arch image.
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
app_src="$root/ps5/app"
out="$root/build/app-ps5"
headers="$root/.deps/ps5-vk-headers"
sce_sys="$app_src/sce_sys"
param="$sce_sys/param.json"

[ -f "$sd/CMakeLists.txt" ] && [ -f "$ggml/CMakeLists.txt" ] && [ -d "$kit/src" ] || {
    echo "build-app: submodules missing; run: git submodule update --init third_party/stable-diffusion.cpp" \
        "third_party/ps5-homebrew-ui && git -C third_party/stable-diffusion.cpp submodule update --init ggml" >&2
    exit 2
}
# ps5-opengl comes pinned and sha256-checked through the kit's own script.
glsdk=${PS5_OPENGL_SDK:-$(bash "$kit/tools/fetch-opengl-sdk.sh")}

for f in "$archive" "$tool" "$vk/runtime/libc.prx" "$sdk/bin/prospero-lld" "$glsdk/lib/libPS5OpenGL.a"; do
    [ -e "$f" ] || { echo "build-app: missing $f (run scripts/setup-deps.sh)" >&2; exit 2; }
done
export PS5_PAYLOAD_SDK="$sdk" PS5_VULKAN_ROOT="$vk"
mkdir -p "$out"

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
shopt -u nullglob

# ---- stable-diffusion.cpp and ggml, as static libraries -----------------------
flags="-I$headers/include"
# sd.cpp and ggml throw, catch and use dynamic_cast; the PS5 target turns
# exceptions and RTTI off by default.
cxxflags="$flags -fexceptions -fcxx-exceptions -frtti"
# Zen 2: AVX2, FMA, F16C and BMI2, no AVX-512. One static image: no backend
# modules (GGML_BACKEND_DL), no OpenMP (ggml's own thread pool), no libm (the
# math functions are in Sony's libc), no WebP/WebM (thirdparty submodules).
# TODO(server): add -DSD_BUILD_EXAMPLES=ON -DSD_SERVER_LIBRARY=ON
# -DSD_SERVER_BUILD_FRONTEND=OFF and the sd-server-lib target once main.cpp
# starts sd_server() (patches/sdcpp/0050).
cmake -S "$sd" -B "$out/sd" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$app_src/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$cxxflags" \
    -DBUILD_SHARED_LIBS=OFF -DSD_BUILD_SHARED_LIBS=OFF -DSD_BUILD_EXAMPLES=OFF \
    -DSD_VULKAN=ON -DSD_WEBP=OFF -DSD_WEBM=OFF \
    -DGGML_BACKEND_DL=OFF -DGGML_NATIVE=OFF -DGGML_CPU_ALL_VARIANTS=OFF \
    -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON -DGGML_AVX512=OFF \
    -DGGML_OPENMP=OFF -DGGML_CCACHE=OFF \
    -DVulkan_INCLUDE_DIR="$headers/include" -DVulkan_LIBRARY="$archive" \
    -DVulkan_GLSLC_EXECUTABLE="$(command -v glslc)" -DSPIRV-Headers_DIR="$headers/cmake" \
    -DMATH_LIBRARY=SceLibcInternal > "$out/sd.configure.log" 2>&1 ||
    { tail -30 "$out/sd.configure.log" >&2; exit 1; }
cmake --build "$out/sd" --target stable-diffusion

# ---- the app's own objects, and the title's start code from PS5_Vulkan ---------
obj="$out/obj"
mkdir -p "$obj" "$out/stubs"
app_cc() { "$app_src/ps5cc" -O2 "$@"; }
cxx() { "$app_src/ps5cxx" -std=c++20 -O2 -ffunction-sections -fdata-sections "$@"; }
eh=(-fexceptions -fcxx-exceptions -frtti)
sdinc=(-I "$sd/include" -I "$ggml/include" -I "$sd/thirdparty")
kitinc=(-I "$kit/src" -I "$kit/third_party" -I "$glsdk/include" -DGL_GLEXT_PROTOTYPES=1)
cxx "${eh[@]}" -I "$headers/include" "${sdinc[@]}" "${kitinc[@]}" -I "$app_src" \
    -c "$app_src/main.cpp" -o "$obj/main.o"
cxx "${eh[@]}" "${sdinc[@]}" -c "$app_src/gen_args.cpp" -o "$obj/gen_args.o"
for src in display status_view; do
    cxx "${eh[@]}" "${kitinc[@]}" -I "$app_src" -c "$app_src/$src.cpp" -o "$obj/$src.o"
done
app_cc -c "$app_src/compat_app.c" -o "$obj/compat_app.o"
# In an archive, so --exclude-libs keeps them local: a stub module defines some
# of the same names, and lld would otherwise export ours to interpose them,
# which the title converter refuses.
rm -f "$obj/libps5app_compat.a"
"$sdk/bin/llvm-ar" rcs "$obj/libps5app_compat.a" "$obj/compat_app.o"
cxx -fno-exceptions -fno-rtti -c "$vk/tooling/native/app_crt.cpp" -o "$obj/app_crt.o"
cxx -fexceptions -fcxx-exceptions -c "$app_src/cpp_runtime.cpp" -o "$obj/app_cpp_runtime.o"
app_objects=("$obj/app_crt.o" "$obj/app_cpp_runtime.o" "$obj/main.o" "$obj/gen_args.o" "$obj/display.o"
    "$obj/status_view.o")

# ---- the screen ----------------------------------------------------------------
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

# ---- AGC ---------------------------------------------------------------------------
# AGC comes from system modules; these stubs only name the imports, every one
# either driver (the GL object, RADV) makes.
agc_imports=$("$sdk/bin/llvm-nm" -u "$gl" "$archive" 2>/dev/null | awk '{print $NF}' | grep -E '^sceAgc' | sort -u)
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
link_flags+=(--wrap=sceAgcInit)
mapfile -t sd_libs < <(find "$out/sd" -name '*.a' | sort)
local_archives=("${sd_libs[@]}" "$obj/libps5sd_kit.a" "$obj/libps5app_compat.a" "$out/gl/libps5sd_gl.a")
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
# changes. TODO: the icon and the background are generated placeholders;
# replace them with PS5SD artwork (ShadowMountPlus copies them at registration).
assets="$out/assets"
if [ ! -f "$assets/pic0.dds" ] || [ "$sce_sys/source/background.png" -nt "$assets/pic0.dds" ] ||
    [ "$sce_sys/icon0.png" -nt "$assets/pic0.dds" ]; then
    mkdir -p "$assets"
    (cd "$vk" && bash tools/setup-asset-dependencies.sh > /dev/null &&
        bash tools/prepare-assets.sh --icon "$sce_sys/icon0.png" --background "$sce_sys/source/background.png" \
            --output-directory "$assets" > /dev/null)
fi
cp "$sce_sys/icon0.png" "$assets/pic0.dds" "$assets/pic1.dds" "$app/sce_sys/"
cp "$vk/runtime/libc.prx" "$app/sce_module/libc.prx"
# The status screen's font (ps5-homebrew-ui's DejaVu Sans Mono), with its licence.
mkdir -p "$app/assets/fonts"
cp "$kit/assets/fonts/"{dejavu-sans-mono.huifont,DejaVu-LICENSE.txt} "$app/assets/fonts/"
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
echo "build-app: $app ($(stat -c %s "$app/eboot.bin") bytes)"
