#!/usr/bin/env bash
# check_maps.sh [pack-dir] -- every zone of a pack built from art/maps:
#   - the shipped .dat is exactly what `romeart.py map build` makes from the
#     zone's hand-drawn source (art/maps/<zone>.txt), so nobody edits a .dat
#     by hand and a builder change that would alter a map shows up here;
#   - `map check`: objects on walkable ground, docks, the hero's reach;
#   - `map lint`: no terrain shape the edge art draws badly, in the source or
#     the built .dat, beyond the zone's baseline, and the hero's reach exactly
#     as recorded in art/maps/<zone>_reach.json;
#   - `map sanity`: one sea, no occupied pockets, the object budget.
# Then the art record: `romeart.py provenance check --rebuild` -- every shipped
# art file named by a job, a recipe or a kept source, and every deterministic
# recipe re-run against the pack, pixel for pixel.
# Exits non-zero on the first failure.
set -euo pipefail
cd "$(dirname "$0")/.."
PACK="${1:-assets/glory-of-rome}"
RA="python3 tools/romeart.py"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
$RA map zones "$PACK" > "$tmp/zones.txt"
while read -r zone map size; do
  src="art/maps/$zone.txt"
  if [ -f "$src" ]; then
    $RA map build "$PACK" "$zone" "$src" "$tmp/$zone.dat" --strict > "$tmp/build.log" \
      || { cat "$tmp/build.log"; exit 1; }
    if ! cmp -s "$tmp/$zone.dat" "$PACK/$map"; then
      echo "FAIL: $PACK/$map is not what $src builds; run tools/romeart.py map build"
      exit 1
    fi
    echo "ok:   $zone builds from $src"
  fi
  $RA map check "$PACK" "$zone" "$PACK/$map" | tail -1
  if [ -f "$src" ]; then
    $RA map lint "$PACK" "$zone" > "$tmp/lint.log" || { cat "$tmp/lint.log"; exit 1; }
    head -1 "$tmp/lint.log"
  fi
  $RA map sanity "$PACK" "$PACK/$map" "$size" "$zone" | tail -1
done < "$tmp/zones.txt"
$RA provenance check --rebuild
