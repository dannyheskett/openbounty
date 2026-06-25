#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
make

args=()
if [[ "${1:-}" == "--record" || "${1:-}" == "-r" ]]; then
    dir="$(mktemp -d -t openbounty-record-XXXXXX)"
    echo "[run.sh] recording to $dir"
    args+=(--record "$dir")
    shift
fi
if [[ "${1:-}" == "--encode" || "${1:-}" == "-e" ]]; then
    args+=(--encode-movie)
    shift
fi

exec ./build/debug/openbounty "${args[@]}" "$@"
