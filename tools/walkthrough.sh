#!/usr/bin/env bash
# Drive a fresh game from the character-select screen to the overworld, then
# capture every view a key can reach. Screens behind a location (town, castle,
# dwelling) need play to reach; use --autoplay for those.
#
#   tools/walkthrough.sh <pack> <out-dir> [w] [h]
set -u
DIR="$(cd "$(dirname "$0")/.." && pwd)"
CAP="$DIR/tools/capture.sh"
pack="${1:-glory-of-rome}"; out="${2:-$DIR/screenshots}"
w="${3:-1920}"; h="${4:-1040}"

"$CAP" start "$pack" "$w" "$h" "$out" > /dev/null || exit 1

"$CAP" shot 00_char_select > /dev/null
"$CAP" key a
"$CAP" type "Marcus"
"$CAP" key Return              # name -> difficulty
"$CAP" shot 01_difficulty > /dev/null
"$CAP" key Return              # difficulty -> overworld
sleep 2
"$CAP" key space space space space   # clear the opening dialogs
"$CAP" shot 02_overworld > /dev/null

for pair in "v:03_character" "a:04_army" "i:05_contract" "p:06_puzzle" \
            "m:07_worldmap" "u:08_spells" "c:09_controls" "o:10_options"; do
  key="${pair%%:*}"; name="${pair##*:}"
  "$CAP" key "$key"
  "$CAP" shot "$name" > /dev/null
  "$CAP" key Escape
done

"$CAP" stop
ls "$out"/*.png | wc -l
