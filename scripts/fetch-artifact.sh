#!/usr/bin/env bash
# Fetches what CI built, so the PC needs no toolchain: the title folder (artefact
# ps5sd-title) into build/app-ps5/title/ and the launch payload (ps5sd-tools) into
# build/tools/. Needs the GitHub CLI, logged in (gh auth login).
#
#   scripts/fetch-artifact.sh            the latest successful build run on main
#   scripts/fetch-artifact.sh 1234567    that run (the number in the run's URL)
#
# PS5SD_REPO overrides the repository (Darkatek7/PS5SD), PS5SD_BRANCH the branch (main).
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
repo=${PS5SD_REPO:-Darkatek7/PS5SD}
branch=${PS5SD_BRANCH:-main}

command -v gh > /dev/null 2>&1 || { echo "fetch-artifact: the GitHub CLI (gh) is required: brew install gh" >&2; exit 1; }

run=${1:-}
if [ -z "$run" ]; then
    run=$(gh run list -R "$repo" --workflow build.yml --branch "$branch" --status success --limit 1 \
        --json databaseId --jq '.[0].databaseId')
    [ -n "$run" ] && [ "$run" != null ] ||
        { echo "fetch-artifact: no successful build run on $branch of $repo" >&2; exit 1; }
fi
echo "fetch-artifact: run $run of $repo"

title_dir="$root/build/app-ps5/title"
tools_dir="$root/build/tools"
mkdir -p "$title_dir" "$tools_dir"

gh run download "$run" -R "$repo" -n ps5sd-title -D "$title_dir"
if gh run download "$run" -R "$repo" -n ps5sd-tools -D "$tools_dir"; then :; else
    echo "fetch-artifact: no ps5sd-tools artefact in run $run (older than the launcher step?)" >&2
fi

echo "fetch-artifact: title  -> $title_dir"
ls "$title_dir" | sed 's/^/    /'
echo "fetch-artifact: tools  -> $tools_dir"
ls "$tools_dir" | sed 's/^/    /'
