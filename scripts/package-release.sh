#!/bin/bash
# Packs the built title for a release and the PS5 homebrew catalog:
#
#   dist/PPSA99576.zip    the title folder, PPSA99576/ at the top of the archive
#   dist/PPSA99576.json   the catalog record (github.com/darkxex/ps5-homebrew-catalog,
#                         apps/PPSA99576.json), with the zip's SHA-256
#
#   scripts/package-release.sh [title folder]   (default build/app-ps5/title/PPSA99576)
#
# Nothing is uploaded. The release tag is v<version> from ps5/app/version.hpp
# (v0.1.0); param.json's contentVersion (01.001.000) is what consoles compare,
# and build-app.sh keeps the two in step. The icon URL is pinned to the tag.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
title=${1:-$root/build/app-ps5/title/PPSA99576}
repo=https://github.com/Darkatek7/PS5SD
id=PPSA99576

for f in eboot.bin sce_sys/param.json sce_sys/icon0.png sce_sys/icon0.dds sce_sys/pic0.dds sce_sys/pic1.dds sce_module/libc.prx; do
    [ -f "$title/$f" ] || { echo "package-release: $title has no $f (scripts/build-app.sh first)" >&2; exit 1; }
done
[ -d "$title/assets/fonts" ] || { echo "package-release: $title has no assets/fonts (a display build is needed)" >&2; exit 1; }

version=$(sed -n 's/^#define PS5SD_VERSION "\(.*\)"$/\1/p' "$root/ps5/app/version.hpp")
tag=v$version
python3 - "$title" "$root/dist" "$id" "$repo" "$tag" "$version" "$root/ps5/app/sce_sys/param.json" << 'EOF'
import hashlib, json, os, sys, zipfile
title, dist, tid, repo, tag, version, source_param = sys.argv[1:]
param = json.load(open(os.path.join(title, "sce_sys", "param.json")))
if param["titleId"] != tid:
    sys.exit("package-release: param.json's titleId is %s, not %s" % (param["titleId"], tid))
if param["contentVersion"] != json.load(open(source_param))["contentVersion"]:
    sys.exit("package-release: the built param.json is not the one in ps5/app/sce_sys (build again)")
os.makedirs(dist, exist_ok=True)
path = os.path.join(dist, tid + ".zip")
with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for base, dirs, files in os.walk(title):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(base, name)
            z.write(full, os.path.join(tid, os.path.relpath(full, title)).replace(os.sep, "/"))
sha = hashlib.sha256(open(path, "rb").read()).hexdigest()
description = ("Stable Diffusion, SDXL and FLUX image generation on the PS5 GPU, with a web UI, "
               "A1111 and OpenAI compatible APIs and a built-in model downloader.")
assert len(description) <= 200, len(description)
record = {
    "titleid": tid,
    "name": "PS5SD",
    "kind": "app",
    "category": "ai",
    "description": description,
    "license": "GPL-3.0",
    "author": "Darkatek7",
    "version": version,
    "source_repo": repo,
    "artifact_url": "%s/releases/download/%s/%s.zip" % (repo, tag, tid),
    "sha256": sha,
    "icon_url": "https://raw.githubusercontent.com/%s/%s/ps5/app/sce_sys/icon0.png" % (repo[len("https://github.com/"):], tag),
}
with open(os.path.join(dist, tid + ".json"), "w", newline="\n") as f:
    json.dump(record, f, indent=2)
    f.write("\n")
print("package: %s (%d bytes)\nsha256:  %s\ntag:     %s (contentVersion %s)\nrecord:  %s"
      % (path, os.path.getsize(path), sha, tag, param["contentVersion"], os.path.join(dist, tid + ".json")))
EOF
