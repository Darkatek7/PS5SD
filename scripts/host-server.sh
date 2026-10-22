#!/usr/bin/env bash
# Builds sd-server natively on this machine (macOS arm64: Metal or CPU; Linux: CPU)
# and starts it on 127.0.0.1:7860, so the web UI (ps5/webui/src) can be
# developed without a console.
#
# What it does:
#   1. copies third_party/stable-diffusion.cpp to a scratch directory (the
#      submodule checkout stays clean) and applies patches/sdcpp/005*.patch and later
#      (PATCHES=all applies every patches/sdcpp/*.patch)
#   2. puts the web UI together (scripts/bundle-webui.sh) and embeds it with
#      scripts/embed-file.sh
#   3. builds it twice:
#        SD_SERVER_LIBRARY=OFF  ->  sd-server              (the plain executable)
#        SD_SERVER_LIBRARY=ON   ->  sd-server-lib + sd-server-host, a ten-line
#                                   driver around sd_server()/sd_server_terminate()
#                                   with printing activity hooks: the shape of the
#                                   PS5 app, on the host
#   4. runs one of them with the model you give it
#
# usage: scripts/host-server.sh [options] <model.gguf|.safetensors> [-- extra sd-server args]
#   -b, --build-dir DIR   scratch/build directory (default: $PS5SD_ROOT/.deps/hostserver)
#       --cpu             no Metal (default on macOS arm64: Metal)
#       --lib             run the library-mode driver instead of the plain sd-server
#       --build-only      build both, do not start anything
#       --only exe|lib    build just one of the two
#       --port N          listen port (default 7860; env PS5SD_PORT)
#       --dry-run         print the commands, run nothing
#   -h, --help
#
# Needs cmake (brew install cmake), git, rsync and a C++17 compiler. The page is
# embedded when building: after editing ps5/webui/src run this again (only the
# server is rebuilt) or, quicker, use scripts/webui-dev.py, which serves the
# parts put together on every request and passes the API through to a server.

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$HERE/.." && pwd)"
PS5SD_ROOT="${PS5SD_ROOT:-$ROOT}"

WORK="${PS5SD_HOSTSERVER_DIR:-$PS5SD_ROOT/.deps/hostserver}"
PORT="${PS5SD_PORT:-7860}"
USE_METAL=auto
RUN=exe
BUILD_ONLY=0
ONLY=both
DRY=0
MODEL=""
EXTRA=()

usage() { sed -n '2,/^set -e/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        -b|--build-dir) WORK="$2"; shift 2 ;;
        --cpu) USE_METAL=no; shift ;;
        --lib) RUN=lib; shift ;;
        --build-only) BUILD_ONLY=1; shift ;;
        --only) ONLY="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        --) shift; EXTRA=("$@"); break ;;
        -*) echo "host-server: unknown option $1" >&2; usage >&2; exit 2 ;;
        *) if [ -z "$MODEL" ]; then MODEL="$1"; else echo "host-server: one model only (extra arguments go after --)" >&2; exit 2; fi; shift ;;
    esac
done

case "$ONLY" in both|exe|lib) ;; *) echo "host-server: --only is exe or lib" >&2; exit 2 ;; esac
if [ "$ONLY" = exe ] && [ "$RUN" = lib ]; then echo "host-server: --lib needs the library build" >&2; exit 2; fi
if [ "$ONLY" = lib ] && [ "$RUN" = exe ]; then RUN=lib; fi

run() {
    if [ "$DRY" = 1 ]; then printf '+'; printf ' %q' "$@"; printf '\n'; else "$@"; fi
}

if [ "$DRY" = 0 ]; then
    for tool in cmake git rsync; do
        command -v "$tool" >/dev/null 2>&1 || {
            echo "host-server: $tool not found" >&2
            [ "$tool" = cmake ] && echo "             macOS: brew install cmake" >&2
            exit 1
        }
    done
fi
if [ "$BUILD_ONLY" = 0 ] && [ -z "$MODEL" ]; then
    echo "host-server: give a model file (or --build-only)" >&2
    usage >&2
    exit 2
fi
if [ -n "$MODEL" ] && [ "$DRY" = 0 ] && [ ! -f "$MODEL" ]; then
    echo "host-server: no such model file: $MODEL" >&2
    exit 1
fi

SD="$PS5SD_ROOT/third_party/stable-diffusion.cpp"
[ -f "$SD/ggml/CMakeLists.txt" ] || {
    echo "host-server: $SD/ggml is empty; run: git submodule update --init third_party/stable-diffusion.cpp" >&2
    exit 1
}

if [ "$USE_METAL" = auto ]; then
    if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ]; then USE_METAL=yes; else USE_METAL=no; fi
fi
GPU_FLAGS=()
[ "$USE_METAL" = yes ] && GPU_FLAGS=(-DSD_METAL=ON)

JOBS="$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4) | head -n1 )"

SRC="$WORK/src"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
SRC="$WORK/src"

# 1. scratch copy + patches -------------------------------------------------
echo "== copying stable-diffusion.cpp to $SRC"
run rsync -a --delete --exclude .git --exclude assets --exclude docs --exclude examples/server/frontend "$SD/" "$SRC/"

if [ "${PATCHES:-}" = all ]; then
    PATCH_GLOB=("$PS5SD_ROOT"/patches/sdcpp/0*.patch)
else
    PATCH_GLOB=("$PS5SD_ROOT"/patches/sdcpp/00[5-9]*.patch)
fi
for p in "${PATCH_GLOB[@]}"; do
    [ -f "$p" ] || continue
    echo "== applying $(basename "$p")"
    # GIT_CEILING_DIRECTORIES: the scratch copy may sit inside another repository
    # (.deps/ of this one); without it git apply would take the patch paths
    # relative to that repository and skip every file silently.
    if [ "$DRY" = 1 ]; then echo "+ (cd $SRC && git apply $p)"; else (cd "$SRC" && GIT_CEILING_DIRECTORIES="$WORK" git apply "$p"); fi
done
if [ "$DRY" = 0 ] && [ "${#PATCH_GLOB[@]}" -gt 0 ] && [ ! -f "$SRC/examples/server/server_hooks.h" ]; then
    echo "host-server: the sd-server patches did not apply (examples/server/server_hooks.h is missing)" >&2
    exit 1
fi

# 2. embed the page -----------------------------------------------------------
INDEX_H="$WORK/index_html.h"
echo "== embedding the web UI (ps5/webui/src)"
run sh "$PS5SD_ROOT/scripts/bundle-webui.sh" "$WORK/index.html"
run "$PS5SD_ROOT/scripts/embed-file.sh" "$WORK/index.html" sd_server_index_html "$INDEX_H"

COMMON_FLAGS=(-DCMAKE_BUILD_TYPE=Release -DSD_BUILD_EXAMPLES=ON -DSD_SERVER_BUILD_FRONTEND=OFF "-DSD_SERVER_EMBED_INDEX=$INDEX_H" ${GPU_FLAGS[@]+"${GPU_FLAGS[@]}"})

# 3a. SD_SERVER_LIBRARY=OFF: the plain executable ---------------------------------
if [ "$ONLY" != lib ]; then
    echo "== building sd-server (SD_SERVER_LIBRARY=OFF)"
    run cmake -S "$SRC" -B "$WORK/build-exe" "${COMMON_FLAGS[@]}" -DSD_SERVER_LIBRARY=OFF
    run cmake --build "$WORK/build-exe" --target sd-server -j "$JOBS"
fi

# 3b. SD_SERVER_LIBRARY=ON: sd-server-lib + a host driver around it -------------------
if [ "$ONLY" != exe ]; then
    echo "== building sd-server-lib + sd-server-host (SD_SERVER_LIBRARY=ON)"
    DRIVER="$WORK/driver"
    if [ "$DRY" = 1 ]; then
        echo "+ write $DRIVER/{CMakeLists.txt,host_main.cpp}"
    else
        mkdir -p "$DRIVER"
        cat > "$DRIVER/host_main.cpp" <<'CPP'
// Host stand-in for the PS5 app: calls sd_server() like the app does, and
// defines the activity hooks (strong definitions replace the server's weak no-ops).
#include <csignal>
#include <cstdint>
#include <cstdio>

extern "C" {
int sd_server(int argc, char** argv);
void sd_server_terminate(void);

void sd_activity_job_begin(const char* kind, const char* prompt, int w, int h, int steps) {
    fprintf(stderr, "[hook] begin %s %dx%d steps=%d \"%s\"\n", kind, w, h, steps, prompt);
}
void sd_activity_progress(int step, int steps, float secs) {
    fprintf(stderr, "[hook] progress %d/%d (%.2fs)\n", step, steps, secs);
}
void sd_activity_preview(const uint8_t*, int w, int h) {
    fprintf(stderr, "[hook] preview %dx%d\n", w, h);
}
void sd_activity_job_end(int ok, const char* path) {
    fprintf(stderr, "[hook] end ok=%d path=%s\n", ok, path);
}
}

static void on_signal(int) { sd_server_terminate(); }

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    return sd_server(argc, argv);
}
CPP
        cat > "$DRIVER/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.12)
project(sd-server-host CXX)
set(SD_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)
set(SD_SERVER_LIBRARY ON CACHE BOOL "" FORCE)
set(SD_SERVER_BUILD_FRONTEND OFF CACHE BOOL "" FORCE)
set(SD_SERVER_EMBED_INDEX "$INDEX_H" CACHE FILEPATH "" FORCE)
add_subdirectory("$SRC" sdcpp)
add_executable(sd-server-host host_main.cpp)
target_link_libraries(sd-server-host PRIVATE sd-server-lib)
CMAKE
    fi
    run cmake -S "$DRIVER" -B "$WORK/build-lib" -DCMAKE_BUILD_TYPE=Release ${GPU_FLAGS[@]+"${GPU_FLAGS[@]}"}
    run cmake --build "$WORK/build-lib" --target sd-server-host -j "$JOBS"
fi

[ "$BUILD_ONLY" = 1 ] && { echo "== built in $WORK"; exit 0; }

# 4. run ----------------------------------------------------------------------------
BIN="$WORK/build-exe/bin/sd-server"
[ "$RUN" = lib ] && BIN="$WORK/build-lib/bin/sd-server-host"
mkdir -p "$WORK/output"
echo "== http://127.0.0.1:$PORT/   (binary: $BIN, images: $WORK/output)"
if [ "$DRY" = 1 ]; then
    echo "+ $BIN -m $MODEL -l 127.0.0.1 --listen-port $PORT --output-dir $WORK/output ${EXTRA[*]:-}"
    exit 0
fi
exec "$BIN" -m "$MODEL" -l 127.0.0.1 --listen-port "$PORT" --output-dir "$WORK/output" ${EXTRA[@]+"${EXTRA[@]}"}
