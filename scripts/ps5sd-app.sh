#!/usr/bin/env bash
# Drives PS5SD on the console from the PC (macOS or Linux, bash 3.2 is enough; needs curl,
# nc, and nothing else). Needs ftpsrv (2121), elfldr (9021) and klogsrv (3232) on the console.
#
#   scripts/fetch-artifact.sh                  get CI's title folder and launch payload first
#   scripts/ps5sd-app.sh status                ports, /data/homebrew/PPSA99576, /data/PS5SD
#   scripts/ps5sd-app.sh deploy [--force]      upload build/app-ps5/title/PPSA99576 (changed files only)
#   scripts/ps5sd-app.sh appmeta [--force]     only the home screen's copy of the icon and backgrounds
#                                              (/user/appmeta/PPSA99576; deploy does this too)
#   scripts/ps5sd-app.sh launch                start the title (build/tools/launch.elf to elfldr)
#   scripts/ps5sd-app.sh close                 ask the app to end itself (/data/PS5SD/quit), wait
#   scripts/ps5sd-app.sh cycle                 close, deploy, launch
#   scripts/ps5sd-app.sh log [-f]              print /data/PS5SD/app.log; -f streams klogsrv instead
#   scripts/ps5sd-app.sh model <file|name>     upload a model (local path) and select it (model.txt)
#   scripts/ps5sd-app.sh args <file>           upload app-args.txt (one sd-cli argument a line; PS5SD_ONESHOT=1)
#   scripts/ps5sd-app.sh server-args <file>    upload server-args.txt (one sd-server argument a line)
#   scripts/ps5sd-app.sh env <file>            upload env.txt (NAME=value a line)
#   scripts/ps5sd-app.sh outputs [dir]         download the PNGs of /data/PS5SD/outputs (default ./out)
#
# PS5_HOST is the console (default 10.0.0.101). The app is never killed: it polls
# /data/PS5SD/quit and leaves through the system's own exit. Nothing is deleted on the
# console except that quit file, which is the app's own request file.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=env.sh
. "$root/scripts/env.sh"

: "${PS5_HOST:=10.0.0.101}"
host=$PS5_HOST
elf_port=${PS5_PORT:-9021}
ftp_port=${PS5_FTP_PORT:-2121}
klog_port=${PS5_KLOG_PORT:-3232}
title=${PS5SD_TITLE_ID:-PPSA99576}
data=${PS5SD_DATA:-/data/PS5SD}
ftp="ftp://$host:$ftp_port"
app="$root/build/app-ps5/title/$title"
launcher=${PS5SD_LAUNCHER:-$root/build/tools/launch.elf}
homebrew="/data/homebrew/$title"

say() { printf 'ps5sd-app: %s\n' "$*"; }
die() { printf 'ps5sd-app: %s\n' "$*" >&2; exit 1; }

# --- FTP helpers (curl; the paths are absolute on the console) ----------------------
urlpath() { # percent-encode what would break a curl URL
    printf '%s' "$1" | sed -e 's/%/%25/g' -e 's/ /%20/g' -e 's/#/%23/g' -e 's/?/%3F/g'
}
fcurl() { curl -g -sS --connect-timeout 5 "$@"; }
ftp_url() { printf '%s%s' "$ftp" "$(urlpath "$1")"; }

ftp_list() { # <dir>: a directory listing, fails if there is none
    fcurl --max-time 20 "$(ftp_url "${1%/}/")" | tr -d '\r'
}
ftp_size() { # <file>: size in bytes, empty if it does not exist
    curl -g -sI --connect-timeout 5 --max-time 20 "$(ftp_url "$1")" 2> /dev/null |
        tr -d '\r' | awk 'tolower($1) == "content-length:" { print $2 }' || true
}
ftp_put() { # <local> <remote> [extra curl args]
    local src=$1 dst=$2
    shift 2
    fcurl --ftp-create-dirs "$@" -T "$src" "$(ftp_url "$dst")"
}
ftp_put_text() { # <remote> <text>
    printf '%s\n' "$2" | fcurl --ftp-create-dirs -T - "$(ftp_url "$1")"
}
ftp_mkdir() { # <dir>: MKD, failure (it exists) ignored
    curl -g -s --connect-timeout 5 --max-time 20 -o /dev/null -Q "-MKD $1" "$ftp/" || true
}

port_open() { nc -z -w 3 "$host" "$1" > /dev/null 2>&1; }

ensure_data_dirs() {
    if ! ftp_list "$data" > /dev/null 2>&1; then
        say "creating $data"
        ftp_mkdir "$data"
    fi
    local d
    for d in models outputs; do
        ftp_list "$data/$d" > /dev/null 2>&1 || { say "creating $data/$d"; ftp_mkdir "$data/$d"; }
    done
    ftp_list "$data/outputs" > /dev/null 2>&1 || die "cannot create $data/{models,outputs} over FTP"
}

# --- commands ------------------------------------------------------------------------
status() {
    say "console $host"
    local name p
    for p in "elfldr:$elf_port" "ftpsrv:$ftp_port" "klogsrv:$klog_port"; do
        name=${p%%:*}
        if port_open "${p##*:}"; then
            printf '  %-8s %5s  open\n' "$name" "${p##*:}"
        else
            printf '  %-8s %5s  CLOSED\n' "$name" "${p##*:}"
        fi
    done
    local listing
    echo
    echo "$homebrew:"
    if listing=$(ftp_list "$homebrew" 2> /dev/null); then
        printf '%s\n' "$listing" | sed 's/^/  /'
    else
        echo "  (not there: run deploy)"
    fi
    echo
    echo "$data:"
    if listing=$(ftp_list "$data" 2> /dev/null); then
        printf '%s\n' "$listing" | sed 's/^/  /'
        if printf '%s\n' "$listing" | grep -q ' quit$'; then
            echo
            say "a quit request is pending (the app is not running, or about to end)"
        fi
    else
        echo "  (not there: created by deploy)"
    fi
    local model
    model=$(fcurl --max-time 10 "$(ftp_url "$data/model.txt")" 2> /dev/null | head -n 1 || true)
    [ -z "$model" ] || { echo; say "model.txt: $model"; }
}

sha_of() { shasum -a 256 "$1" | cut -d' ' -f1; }

deploy() {
    local force=false
    [ "${1:-}" != --force ] || force=true
    [ -f "$app/eboot.bin" ] ||
        die "no $app/eboot.bin: scripts/fetch-artifact.sh (or scripts/build-app.sh on Linux) first"
    port_open "$ftp_port" || die "ftpsrv ($host:$ftp_port) is not reachable"

    mkdir -p "$root/build/app-ps5"
    local manifest="$root/build/app-ps5/.deployed-$host-$title" newman
    newman=$(mktemp)
    touch "$manifest"
    local total=0 sent=0 skipped=0 f rel sha size rsize old
    while IFS= read -r f; do
        rel=${f#./}
        total=$((total + 1))
        size=$(wc -c < "$app/$rel" | tr -d ' ')
        sha=$(sha_of "$app/$rel")
        # Unchanged since the last upload to this console, and still the same size there.
        old=$(awk -v p="$rel" '{ n = $0; sub(/^[^ ]+ [^ ]+ /, "", n); if (n == p) { print $1 " " $2; exit } }' "$manifest")
        if ! $force && [ "$old" = "$sha $size" ]; then
            rsize=$(ftp_size "$homebrew/$rel")
            if [ "$rsize" = "$size" ]; then
                skipped=$((skipped + 1))
                printf '%s %s %s\n' "$sha" "$size" "$rel" >> "$newman"
                continue
            fi
        fi
        say "upload $rel ($size bytes)"
        if ! ftp_put "$app/$rel" "$homebrew/$rel"; then
            cp "$newman" "$manifest.partial" 2> /dev/null || true
            rm -f "$newman"
            die "upload of $rel failed (is the app still running? scripts/ps5sd-app.sh close)"
        fi
        sent=$((sent + 1))
        printf '%s %s %s\n' "$sha" "$size" "$rel" >> "$newman"
    done < <(cd "$app" && find . -type f ! -name '.DS_Store' | LC_ALL=C sort)
    mv "$newman" "$manifest"
    rm -f "$manifest.partial"

    ensure_data_dirs
    sync_appmeta "$force"
    say "deployed $title: $sent uploaded, $skipped unchanged of $total files"
}

# The home screen shows the icon and the backgrounds from /user/appmeta/<title>,
# which the system copies once, when ShadowMountPlus registers the title; a
# new icon0.png in /data/homebrew alone never shows. Keeps that copy in step,
# when the title is registered (its param.json is there).
sync_appmeta() { # [true: upload even if unchanged]
    local meta="/user/appmeta/$title" manifest="$root/build/app-ps5/.appmeta-$host-$title" m sha size
    [ -n "$(ftp_size "$meta/param.json")" ] || { say "not registered yet: no $meta to update"; return 0; }
    touch "$manifest"
    for m in icon0.png icon0.dds pic0.dds pic1.dds; do
        [ -f "$app/sce_sys/$m" ] || continue
        sha=$(sha_of "$app/sce_sys/$m")
        size=$(wc -c < "$app/sce_sys/$m" | tr -d ' ')
        if [ "${1:-false}" != true ] && grep -qx "$sha $m" "$manifest" && [ "$(ftp_size "$meta/$m")" = "$size" ]; then
            continue
        fi
        say "upload $meta/$m ($size bytes)"
        ftp_put "$app/sce_sys/$m" "$meta/$m" || die "upload of $meta/$m failed"
        { grep -v " $m\$" "$manifest" || true; printf '%s %s\n' "$sha" "$m"; } > "$manifest.new"
        mv "$manifest.new" "$manifest"
    done
}

launch() {
    [ -f "$launcher" ] || die "no $launcher: scripts/fetch-artifact.sh (the ps5sd-tools artefact)"
    port_open "$elf_port" || die "elfldr ($host:$elf_port) is not reachable"
    # A quit request left behind would end the title the moment it starts.
    if [ -n "$(ftp_size "$data/quit")" ]; then
        say "removing the stale $data/quit"
        curl -g -s --connect-timeout 5 --max-time 20 -o /dev/null -Q "-DELE $data/quit" "$ftp/" || true
    fi
    PS5_HOST=$host PS5_PORT=$elf_port bash "$root/scripts/send-elf.sh" "$launcher"
    say "launch sent for $title; watch with: scripts/ps5sd-app.sh log -f"
}

close() {
    port_open "$ftp_port" || die "ftpsrv ($host:$ftp_port) is not reachable"
    if ! ftp_list "$data" > /dev/null 2>&1; then
        say "no $data on the console: the app never ran"
        return 0
    fi
    ftp_put_text "$data/quit" quit || die "cannot write $data/quit"
    say "quit requested; waiting up to 20 s for the app to take it"
    local i
    for i in $(seq 1 20); do
        sleep 1
        if [ -z "$(ftp_size "$data/quit")" ]; then
            # The app removes the file as it takes the request, then asks the system to end it.
            say "the app took the request; waiting for the system to close it"
            sleep 6
            say "closed"
            return 0
        fi
    done
    # Only the app's own request file is ever removed, so it cannot end a later launch.
    curl -g -s --connect-timeout 5 --max-time 20 -o /dev/null -Q "-DELE $data/quit" "$ftp/" || true
    say "the app did not take the quit request in 20 s (not running?); request withdrawn"
}

cycle() {
    close
    deploy
    launch
}

log() {
    if [ "${1:-}" = -f ]; then
        say "klogsrv $host:$klog_port (Ctrl-C to stop)"
        exec nc "$host" "$klog_port"
    fi
    [ -n "$(ftp_size "$data/app.log")" ] || die "no $data/app.log yet"
    fcurl --max-time 60 "$(ftp_url "$data/app.log")"
}

model() {
    local arg=${1:?usage: model <local file | name or path on the console>} remote lsize rsize
    if [ -f "$arg" ]; then
        remote="$data/models/$(basename "$arg")"
        lsize=$(wc -c < "$arg" | tr -d ' ')
        ensure_data_dirs
        rsize=$(ftp_size "$remote")
        if [ "$rsize" = "$lsize" ]; then
            say "$remote is already there ($lsize bytes)"
        else
            say "uploading $arg ($lsize bytes) to $remote"
            curl -g -S --progress-bar --connect-timeout 5 --ftp-create-dirs -T "$arg" "$(ftp_url "$remote")" ||
                die "upload failed"
            rsize=$(ftp_size "$remote")
            [ "$rsize" = "$lsize" ] || die "upload incomplete: $rsize of $lsize bytes on the console"
        fi
    else
        case $arg in
            /*) remote=$arg ;;
            *) remote="$data/models/$arg" ;;
        esac
        [ -n "$(ftp_size "$remote")" ] || die "$remote is not on the console (and $arg is not a local file)"
    fi
    ftp_put_text "$data/model.txt" "$remote" || die "cannot write $data/model.txt"
    say "model.txt -> $remote"
}

put_config() { # <local file> <remote name>
    [ -f "${1:-}" ] || die "usage: $0 ${2%.txt} <file>"
    ensure_data_dirs
    ftp_put "$1" "$data/$2" || die "upload failed"
    say "$data/$2 uploaded from $1"
}

outputs() {
    local dest=${1:-$root/out} n=0 name
    [ -n "${1:-}" ] || dest=./out
    ftp_list "$data/outputs" > /dev/null 2>&1 || die "no $data/outputs on the console"
    mkdir -p "$dest"
    while IFS= read -r name; do
        case $name in *.png | *.PNG) ;; *) continue ;; esac
        say "download $name"
        fcurl --max-time 0 -o "$dest/$name" "$(ftp_url "$data/outputs/$name")" || die "download of $name failed"
        n=$((n + 1))
    done < <(ftp_list "$data/outputs" | awk '/^-/ { n = $0; sub(/^([^ ]+ +){8}/, "", n); print n }')
    say "$n image(s) in $dest"
}

case ${1:-} in
    status) status ;;
    deploy) deploy "${2:-}" ;;
    appmeta) port_open "$ftp_port" || die "ftpsrv ($host:$ftp_port) is not reachable"; sync_appmeta "$([ "${2:-}" = --force ] && echo true || echo false)" ;;
    launch) launch ;;
    close) close ;;
    cycle) cycle ;;
    log) log "${2:-}" ;;
    model) model "${2:-}" ;;
    args) put_config "${2:-}" app-args.txt ;;
    server-args) put_config "${2:-}" server-args.txt ;;
    env) put_config "${2:-}" env.txt ;;
    outputs) outputs "${2:-}" ;;
    *)
        sed -n '2,/^set -euo/p' "$0" | sed -e '/^set -euo/d' -e 's/^# \{0,1\}//' >&2
        exit 2
        ;;
esac
