#!/bin/sh
# Puts the web UI together: ps5/webui/src/page.html with each line
# "@@include <file>" replaced by that file of ps5/webui/src (app.css, js/*.js),
# as one page, which the build embeds into sd-server (scripts/embed-file.sh).
# The parts of the script share one closure (js/00-start.js opens it,
# js/23-start-up.js closes it), so they only work put together.
#
# usage: scripts/bundle-webui.sh [<output-file>]   (no output-file: stdout)
#
# POSIX sh + awk only, like embed-file.sh. scripts/webui-dev.py does the same in
# Python for the PC.

set -eu

src=$(cd "$(dirname "$0")/../ps5/webui/src" && pwd)
out=${1:-}

bundle() {
    awk -v src="$src" '
        /^@@include / {
            file = src "/" substr($0, 11)
            if ((getline line < file) <= 0) { print "bundle-webui: no such file: " file > "/dev/stderr"; exit 1 }
            do print line; while ((getline line < file) > 0)
            close(file)
            next
        }
        { print }
    ' "$src/page.html"
}

if [ -n "$out" ]; then
    bundle > "$out.new"
    mv -f "$out.new" "$out"
else
    bundle
fi
