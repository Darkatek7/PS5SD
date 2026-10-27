#!/usr/bin/env bash
# Sends a payload ELF to the console's ELF loader (elfldr, port 9021).
#
#   PS5_HOST=10.0.0.101 scripts/send-elf.sh build/tools/launch.elf
#
# Uses socat if it is installed, else BSD/GNU nc, else python3's sockets (Git Bash on Windows, which
# has no nc). PS5_SEND_METHOD (socat, nc or python) forces one. Works with bash 3.2 (macOS).
set -euo pipefail

elf=${1:?usage: send-elf.sh FILE.elf}
host=${PS5_HOST:?set PS5_HOST to the console IP}
port=${PS5_PORT:-9021}

[ -f "$elf" ] || { echo "send-elf: $elf not found" >&2; exit 1; }

# The first python that runs Python 3 (on Windows "python3" can be the Store's stub); empty when none does.
find_python() {
    local p
    for p in python3 python; do
        if command -v "$p" > /dev/null 2>&1 && "$p" -c 'import sys; sys.exit(sys.version_info[0] != 3)' > /dev/null 2>&1; then
            printf '%s' "$p"
            return
        fi
    done
}

method=${PS5_SEND_METHOD:-}
py=$(find_python)
if [ -z "$method" ]; then
    if command -v socat > /dev/null 2>&1; then
        method=socat
    elif command -v nc > /dev/null 2>&1; then
        method=nc
    elif [ -n "$py" ]; then
        method=python
    else
        echo "send-elf: need socat, nc or python3" >&2
        exit 1
    fi
fi

case $method in
    socat)
        socat -u "FILE:$elf" "TCP:$host:$port"
        ;;
    nc)
        # -w: the connect timeout and the wait for the end of the exchange.
        nc -w 5 "$host" "$port" < "$elf"
        ;;
    python)
        [ -n "$py" ] || { echo "send-elf: no python3 found" >&2; exit 1; }
        "$py" -I - "$host" "$port" "$elf" <<'PY'
import socket, sys
host, port, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
with open(path, "rb") as f, socket.create_connection((host, port), timeout=10) as s:
    while True:
        chunk = f.read(65536)
        if not chunk:
            break
        s.sendall(chunk)
    s.shutdown(socket.SHUT_WR)
    s.settimeout(3)
    try:
        while s.recv(4096):
            pass
    except OSError:
        pass
PY
        ;;
    *)
        echo "send-elf: unknown PS5_SEND_METHOD '$method' (socat, nc, python)" >&2
        exit 2
        ;;
esac
echo "send-elf: sent $(basename "$elf") ($(wc -c < "$elf" | tr -d ' ') bytes) to $host:$port with $method"
