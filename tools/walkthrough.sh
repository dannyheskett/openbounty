#!/usr/bin/env bash
# Drive a fresh game from the character-select screen to the overworld, then
# capture every view that a key can reach. Screens behind a location (town,
# castle, dwelling) need play to reach and are not covered here.
#
#   tools/walkthrough.sh <pack> <prefix> [w] [h]
set -u
DIR="$(cd "$(dirname "$0")/.." && pwd)"
CAP="$DIR/tools/capture.sh"
pack="${1:-glory-of-rome}"; pre="${2:-x}"; w="${3:-1920}"; h="${4:-1040}"

K() { xdotool key --clearmodifiers --delay 200 "$1"; sleep 1.2; }

"$CAP" start "$pack" "$w" "$h" > /dev/null || exit 1
# The first key after launch is swallowed while the window settles, so the
# character pick is sent twice -- picking A twice is the same as picking it once.
sleep 3
xdotool windowactivate "$(xdotool search --name . | tail -1)" 2>/dev/null
"$CAP" shot "${pre}_00_char_select" > /dev/null
K a; K a
"$CAP" shot "${pre}_01_new_game" > /dev/null
xdotool type --delay 150 "Marcus"; sleep 1
K Return                     # name -> difficulty
K Return                     # difficulty -> overworld
sleep 2
K space; K space; K space; K space   # clear the opening dialogs
"$CAP" shot "${pre}_02_overworld" > /dev/null

for pair in "v:03_character" "a:04_army" "i:05_contract" "p:06_puzzle" \
            "m:07_worldmap" "u:08_spells" "c:09_controls" "o:10_options"; do
  key="${pair%%:*}"; name="${pair##*:}"
  K "$key"; "$CAP" shot "${pre}_${name}" > /dev/null; K Escape
done
ls "$DIR/screenshots/${pre}"_*.png | wc -l
