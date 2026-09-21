#!/bin/bash
# Determinism check: rebuild the engine lib + statedump, regenerate, compare
# with a reference tree.  usage: tools/detcheck.sh <ref-dir> [out-dir]
cd "$(dirname "$0")/.." || exit 1
REF="${1:?usage: detcheck.sh <ref-dir> [out-dir]}"
OUT="${2:-/tmp/detcheck_cur}"
make build/libobengine.a >/dev/null 2>&1 || { echo "lib build failed"; exit 1; }
gcc -std=c99 -Wall -Wextra -O2 -Iengine/headless -Iengine/include -Ithird_party/cjson \
    tests/library/statedump.c engine/host_noop.c \
    -Wl,--whole-archive build/libobengine.a -Wl,--no-whole-archive \
    -o build/statedump -lm -lpthread || exit 1
rm -rf "$OUT"
for p in glory-of-rome kings-bounty; do
  mkdir -p "$OUT/$p" && ./build/statedump "assets/$p" "$OUT/$p" >/dev/null || { echo "statedump failed on $p"; exit 1; }
done
if [ ! -d "$REF" ]; then cp -r "$OUT" "$REF"; echo "DETERMINISM: recorded reference in $REF"; exit 0; fi
if diff -rq "$REF" "$OUT"; then echo "DETERMINISM: all files byte-identical"; else echo "DETERMINISM: DIFFERENT"; exit 1; fi
