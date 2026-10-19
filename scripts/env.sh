# Source this file: . scripts/env.sh
# Sets up the environment variables used by every PS5SD script.

PS5SD_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-${(%):-%x}}")/.." && pwd)"
export PS5SD_ROOT

export PS5SD_DEPS="$PS5SD_ROOT/.deps"
# ps5-opengl for the screen, once scripts/setup-deps.sh has set it up.
if [ -z "${PS5_OPENGL_SDK:-}" ] && [ -f "$PS5SD_DEPS/ps5-opengl-sdk/lib/libPS5OpenGL.a" ]; then
    export PS5_OPENGL_SDK="$PS5SD_DEPS/ps5-opengl-sdk"
fi

export PS5_HOST="${PS5_HOST:-}"
export PS5_PORT=9021
export PS5_FTP_PORT=2121
export PS5_KLOG_PORT=3232

export PS5SD_TITLE_ID=PPSA99576
export PS5SD_DATA=/data/PS5SD
