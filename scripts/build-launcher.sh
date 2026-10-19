#!/usr/bin/env bash
# Builds the launch payload of ps5-homebrew-dev-protocol for this title with the payload
# SDK in .deps (run in the Linux container, after scripts/setup-deps.sh), so the PC that
# deploys never needs the SDK. The payload starts PPSA99576 for the foreground user when
# elfldr runs it; the title id is compiled in. It is the same payload PS5LM's
# scripts/ps5lm-app.sh builds, with the protocol's own scripts/send-controller.sh
# (PS5_CONTROLLER_COMPILE_ONLY=1), which compiles controllers/launch.c with
# prospero-clang -Wall -Werror -DBOOTSTRAP_TITLE_ID=... -lSceSystemService -lSceUserService.
#
#   scripts/build-launcher.sh      -> build/tools/launch.elf
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=env.sh
. "$root/scripts/env.sh"

title=${PS5SD_TITLE_ID:-PPSA99576}
protocol=${PS5_PROTOCOL:-$root/.deps/src/ps5-homebrew-dev-protocol}
sdk=${PS5_PAYLOAD_SDK:-$root/.deps/ps5-payload-sdk}
out="$root/build/tools"

[ -f "$protocol/scripts/send-controller.sh" ] ||
    { echo "build-launcher: $protocol missing (run scripts/setup-deps.sh)" >&2; exit 2; }
[ -x "$sdk/bin/prospero-clang" ] ||
    { echo "build-launcher: $sdk/bin/prospero-clang missing (run scripts/setup-deps.sh)" >&2; exit 2; }

mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

elf=$(TMPDIR="$tmp" PS5_PAYLOAD_SDK="$sdk" PS5_CONTROLLER_COMPILE_ONLY=1 \
    bash "$protocol/scripts/send-controller.sh" launch "$title" localhost 9021 | tail -n 1)
[ -f "$elf" ] || { echo "build-launcher: the launch payload was not built" >&2; exit 1; }

cp "$elf" "$out/launch.elf"
echo "build-launcher: $out/launch.elf ($(wc -c < "$out/launch.elf" | tr -d ' ') bytes, title $title)"
