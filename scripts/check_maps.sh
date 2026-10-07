#!/usr/bin/env bash
# check_maps.sh [pack-dir] -- every zone of a pack built from art/maps:
#   - the shipped .dat is exactly what tools/mapbuild.py builds from the zone's
#     hand-drawn source (art/maps/<zone>.txt), so nobody edits a .dat by hand
#     and a builder change that would alter a map shows up here;
#   - tools/mapbuild.py check (objects on walkable ground, docks, reach);
#   - tools/mapcheck.py (one sea, no occupied pockets, the object budget).
# Exits non-zero on the first failure.
set -euo pipefail
cd "$(dirname "$0")/.."
PACK="${1:-assets/glory-of-rome}"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
python3 - "$PACK" > "$tmp/zones.txt" <<'PY'
import json, sys
for z in json.load(open(sys.argv[1] + "/game.json"))["zones"]:
    print(z["id"], z["map"], f'{z["width"]}x{z["height"]}')
PY
while read -r zone map size; do
  src="art/maps/$zone.txt"
  if [ -f "$src" ]; then
    python3 tools/mapbuild.py build "$PACK" "$zone" "$src" "$tmp/$zone.dat" > "$tmp/build.log" \
      || { cat "$tmp/build.log"; exit 1; }
    if ! cmp -s "$tmp/$zone.dat" "$PACK/$map"; then
      echo "FAIL: $PACK/$map is not what $src builds; run tools/mapbuild.py build"
      exit 1
    fi
    echo "ok:   $zone builds from $src"
  fi
  python3 tools/mapbuild.py check "$PACK" "$zone" "$PACK/$map" | tail -1
  python3 tools/mapcheck.py "$PACK" "$PACK/$map" "$size" "$zone" | tail -1
done < "$tmp/zones.txt"
