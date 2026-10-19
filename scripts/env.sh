# Source this file: . scripts/env.sh
# Sets up the environment variables used by every PS5SD script.

PS5SD_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-${(%):-%x}}")/.." && pwd)"
export PS5SD_ROOT

export PS5SD_DEPS="$PS5SD_ROOT/.deps"

export PS5_HOST="${PS5_HOST:-}"
export PS5_PORT=9021
export PS5_FTP_PORT=2121
export PS5_KLOG_PORT=3232

export PS5SD_TITLE_ID=PPSA99576
export PS5SD_DATA=/data/PS5SD
