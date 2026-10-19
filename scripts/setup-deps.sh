#!/usr/bin/env bash
# Sets up the native build dependencies in .deps/src, the layout scripts/build-app.sh
# expects (the same as PS5LM's):
#
#   .deps/src/PS5_Vulkan                                   Mihawk-99's PS5 Vulkan project
#     .deps/native/ps5-payload-sdk                         the PS5 payload SDK (PS5_PayloadSDK fork)
#     .deps/native/radv-release/lib/libvulkan_radeon.ps5.a RADV (PS5_Mesa + patches/mesa)
#     build/host/ps5-native-tool, runtime/libc.prx         from `make app`
#   .deps/src/PS5_Mesa, .deps/src/PS5_PayloadSDK           its forks, next to it
#   .deps/src/ps5-homebrew-dev-protocol                    the launch payload the deploy script uses
#
# Every repository is fetched at a pinned commit (override with the environment
# variables below). Re-runnable: what is in place is kept, and RADV is only rebuilt
# when the Mesa revision or the patches change. Needs Arch Linux x86-64 with the
# packages of docker/packages.txt (see docker/Dockerfile and docs/BUILDING.md).
#
#   scripts/setup-deps.sh               fetch, patch, build everything
#   scripts/setup-deps.sh --fetch-only  only clone and patch (any host with git)
set -euo pipefail

# PS5_Vulkan main at 2026-10-13 (PS5LM v0.9.0 builds against it). Its own
# tools/build-radv.sh pins PS5_Mesa 1ba9874 and tools/setup-native-dependencies.sh
# pins PS5_PayloadSDK b83202b at this commit.
PS5_VULKAN_URL=${PS5_VULKAN_URL:-https://github.com/mihawk-99/PS5_Vulkan}
PS5_VULKAN_REV=${PS5_VULKAN_REV:-f410e67a0241b3e8edcdbcd2a9b285dfc4a412ed}
PS5_MESA_URL=${PS5_MESA_URL:-https://github.com/mihawk-99/PS5_Mesa}
PS5_MESA_REV=${PS5_MESA_REV:-1ba9874f478657ccf6da72ce55f0260164cc0bd0}
PS5_PAYLOADSDK_URL=${PS5_PAYLOADSDK_URL:-https://github.com/mihawk-99/PS5_PayloadSDK}
PS5_PAYLOADSDK_REV=${PS5_PAYLOADSDK_REV:-b83202be73e930050e00de0f1b4d9ec46c0391bf}
# The launch payload's scripts (PS5LM's scripts/ps5lm-app.sh uses the same repository).
PS5_PROTOCOL_URL=${PS5_PROTOCOL_URL:-https://github.com/manoma080/ps5-homebrew-dev-protocol}
PS5_PROTOCOL_REV=${PS5_PROTOCOL_REV:-51dc33243fd8494a2e38b1d0aa96f7d817fc4df4}

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/.deps/src"
vk="$src/PS5_Vulkan"
mesa="$src/PS5_Mesa"
sdk_fork="$src/PS5_PayloadSDK"
protocol="$src/ps5-homebrew-dev-protocol"

fetch_only=false
case ${1:-} in
    --fetch-only) fetch_only=true ;;
    "") ;;
    *) echo "usage: scripts/setup-deps.sh [--fetch-only]" >&2; exit 2 ;;
esac

log() { printf '==> [setup-deps] %s\n' "$*"; }
die() { printf 'setup-deps: %s\n' "$*" >&2; exit 1; }

sha256_of() { # stdin -> hex digest
    if command -v sha256sum > /dev/null; then sha256sum | cut -d' ' -f1; else shasum -a 256 | cut -d' ' -f1; fi
}

# fetch_rev <dir> <url> <rev>: makes <rev> available in <dir>, shallow (PS5_Mesa
# is the whole of Mesa). GitHub serves a commit by its id.
fetch_rev() {
    local dir=$1 url=$2 rev=$3 try
    mkdir -p "$dir"
    [ -d "$dir/.git" ] || git -C "$dir" init -q
    if git -C "$dir" remote get-url origin > /dev/null 2>&1; then
        git -C "$dir" remote set-url origin "$url"
    else
        git -C "$dir" remote add origin "$url"
    fi
    git -C "$dir" cat-file -e "$rev^{commit}" 2> /dev/null && return 0
    log "fetching $url at ${rev:0:12}"
    for try in 1 2 3; do
        git -C "$dir" fetch -q --depth 1 origin "$rev" && return 0
        echo "setup-deps: fetch of $url failed (attempt $try)" >&2
        sleep $((try * 5))
    done
    die "cannot fetch $rev from $url"
}

# checkout_rev <dir> <rev>: detached at <rev>; untracked files (builds) stay.
checkout_rev() {
    local dir=$1 rev=$2
    [ "$(git -C "$dir" rev-parse -q --verify HEAD 2> /dev/null || true)" = "$rev" ] ||
        git -C "$dir" checkout -q -f --detach "$rev"
}

mkdir -p "$src"

# --- sources ---------------------------------------------------------------
fetch_rev "$vk" "$PS5_VULKAN_URL" "$PS5_VULKAN_REV"
checkout_rev "$vk" "$PS5_VULKAN_REV"
fetch_rev "$sdk_fork" "$PS5_PAYLOADSDK_URL" "$PS5_PAYLOADSDK_REV"
checkout_rev "$sdk_fork" "$PS5_PAYLOADSDK_REV"
fetch_rev "$protocol" "$PS5_PROTOCOL_URL" "$PS5_PROTOCOL_REV"
checkout_rev "$protocol" "$PS5_PROTOCOL_REV"
fetch_rev "$mesa" "$PS5_MESA_URL" "$PS5_MESA_REV"

# --- PS5_Mesa + patches/mesa -------------------------------------------------
# `git am` onto the pinned revision, once: a stamp (base revision and the patches'
# content, then the resulting HEAD) says what is in place. The committer date is the
# author date, so the patched HEAD is the same commit id on every machine and
# PS5_Vulkan's build-radv.sh does not rebuild RADV for it.
shopt -s nullglob
patches=("$root"/patches/mesa/*.patch)
shopt -u nullglob
stamp_file="$mesa/.git/ps5sd-patches"
want=$({ echo "$PS5_MESA_REV"; [ ${#patches[@]} -gt 0 ] && cat "${patches[@]}"; } | sha256_of)
head=$(git -C "$mesa" rev-parse -q --verify HEAD 2> /dev/null || true)
if [ -f "$stamp_file" ] && [ "$(cut -d' ' -f1 "$stamp_file")" = "$want" ] &&
    [ "$(cut -d' ' -f2 "$stamp_file")" = "$head" ] && [ -z "$(git -C "$mesa" status --porcelain --untracked-files=no)" ]; then
    log "PS5_Mesa: ${#patches[@]} patch(es) already applied at ${head:0:12}"
else
    log "PS5_Mesa: applying ${#patches[@]} patch(es) onto ${PS5_MESA_REV:0:12}"
    git -C "$mesa" am --abort > /dev/null 2>&1 || true
    git -C "$mesa" checkout -q -f --detach "$PS5_MESA_REV"
    if [ ${#patches[@]} -gt 0 ]; then
        git -C "$mesa" -c user.name=ps5sd -c user.email=ps5sd@localhost am -q --committer-date-is-author-date \
            "${patches[@]}" || { git -C "$mesa" am --abort || true; die "patches/mesa do not apply to PS5_Mesa ${PS5_MESA_REV:0:12}"; }
    fi
    printf '%s %s\n' "$want" "$(git -C "$mesa" rev-parse HEAD)" > "$stamp_file"
fi
mesa_head=$(git -C "$mesa" rev-parse HEAD)

if $fetch_only; then
    log "fetched and patched; PS5_Mesa at $mesa_head"
    exit 0
fi

# --- build -----------------------------------------------------------------
for c in clang meson ninja rsync wget unzip flock make python3; do
    command -v "$c" > /dev/null || die "missing $c (install docker/packages.txt, or use docker/Dockerfile)"
done
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] ||
    die "the PS5 toolchain needs Linux x86-64; use docker/Dockerfile (docker build --platform linux/amd64)"

sdk="$vk/.deps/native/ps5-payload-sdk"
archive="$vk/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
tool="$vk/build/host/ps5-native-tool"
libc="$vk/runtime/libc.prx"

# The payload SDK (the fork, exported at its pinned revision) and static zlib.
log "PS5_Vulkan: tools/setup-native-dependencies.sh"
(cd "$vk" && bash tools/setup-native-dependencies.sh > /dev/null)

# RADV, release build, from PS5_Mesa as patched. Skips itself when the install
# already is this revision. Takes a long time the first time (cache .deps in CI).
log "PS5_Vulkan: tools/build-radv.sh release at ${mesa_head:0:12}"
(cd "$vk" && PS5_MESA_REVISION="$mesa_head" bash tools/build-radv.sh release)

# The host tool and libc.prx. `make app` also links PS5_Vulkan's own demo title,
# which may fail at this revision; that does not matter here (checked below).
if [ -f "$tool" ] && [ -f "$libc" ]; then
    log "PS5_Vulkan: host tool and libc.prx already built"
else
    log "PS5_Vulkan: make app"
    (cd "$vk" && make app) || echo "setup-deps: 'make app' failed; fine if the outputs below exist" >&2
fi

missing=0
for f in "$sdk/bin/prospero-lld" "$archive" "$tool" "$libc"; do
    if [ -e "$f" ]; then :; else echo "setup-deps: missing $f" >&2; missing=1; fi
done
[ "$missing" = 0 ] || die "the build did not produce everything scripts/build-app.sh needs"

# the deploy script builds the launch payload with $root/.deps/ps5-payload-sdk.
if [ ! -e "$root/.deps/ps5-payload-sdk" ]; then
    ln -s "$sdk" "$root/.deps/ps5-payload-sdk"
fi

echo
echo "setup-deps: done"
echo "  PS5_Vulkan   $vk @ ${PS5_VULKAN_REV:0:12}"
echo "  PS5_Mesa     $mesa @ ${mesa_head:0:12} (${PS5_MESA_REV:0:12} + ${#patches[@]} patches)"
echo "  payload SDK  $sdk"
echo "  RADV         $archive"
echo "  host tool    $tool"
echo "  libc.prx     $libc"
echo "  protocol     $protocol @ ${PS5_PROTOCOL_REV:0:12}"
