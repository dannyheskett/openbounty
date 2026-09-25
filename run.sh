#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
make

args=()
if [[ "${1:-}" == "--record" || "${1:-}" == "-r" ]]; then
    dir="$(mktemp -d -t openbounty-record-XXXXXX)"
    echo "[run.sh] recording to $dir/movie.mp4"
    args+=(--movie "$dir/movie.mp4")
    shift
fi

exec ./build/debug/openbounty "${args[@]}" "$@"
