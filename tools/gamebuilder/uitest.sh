#!/usr/bin/env bash
# Drive GameBuilder's real UI with real X events, and assert on what it did.
#
# Why this way rather than a mocked input layer. raygui reads the mouse
# straight from raylib, so shimming input would leave every button in the app
# untested -- which is precisely the gap that let white-on-white text and a
# missing base-pack picker survive 237 passing unit tests and a screenshot
# review. These are genuine X events into the shipped Linux binary.
#
# Assertions are made on OBSERVABLE side effects -- the manifest on disk, the
# .dat bytes, the archive appearing -- not on internal state, because those are
# the things a user would notice being wrong.
#
#   tools/gamebuilder/uitest.sh        run every case
#   tools/gamebuilder/uitest.sh -k     keep the scratch pack afterwards
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"
BIN=build/openbounty-gamebuilder
WORK="$ROOT/build/uitest-pack"
SHOTS=screenshots/uitest
DISP=99
KEEP=0; [ "${1:-}" = "-k" ] && KEEP=1

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); printf '  \033[32mPASS\033[0m  %s\n' "$1"; }
bad() { FAIL=$((FAIL+1)); printf '  \033[31mFAIL\033[0m  %s\n' "$1"; }

[ -x "$BIN" ] || { echo "build first: make gamebuilder"; exit 2; }
command -v xdotool >/dev/null || { echo "needs xdotool"; exit 2; }
mkdir -p "$SHOTS"; rm -f "$SHOTS"/*.png

# A scratch copy, so a failing run cannot damage a real pack.
rm -rf "$WORK"; mkdir -p "$WORK/maps"
cp assets/glory-of-rome/game.json "$WORK/"
cp assets/glory-of-rome/maps/*.dat "$WORK/maps/"

# Seed the recent list so the pack is one click from the start screen. The app
# takes no arguments by design, so this is the supported way in.
RECENT="${XDG_DATA_HOME:-$HOME/.local/share}/openbounty/gamebuilder-recent.txt"
mkdir -p "$(dirname "$RECENT")"
[ -f "$RECENT" ] && cp "$RECENT" "$RECENT.uitest-bak"
echo "$WORK" > "$RECENT"

Xvfb ":$DISP" -screen 0 1400x900x24 >/dev/null 2>&1 & XVFB=$!
cleanup() {
  kill "${APP:-}" 2>/dev/null; kill $XVFB 2>/dev/null
  [ -f "$RECENT.uitest-bak" ] && mv "$RECENT.uitest-bak" "$RECENT"
  [ $KEEP -eq 0 ] && rm -rf "$WORK"
  return 0
}
trap cleanup EXIT
sleep 1; export DISPLAY=":$DISP"

"$BIN" >"$SHOTS/app.log" 2>&1 & APP=$!
sleep 2
WIN=$(xdotool search --name "GameBuilder" | head -1)
[ -n "$WIN" ] || { bad "window never appeared"; sed 's/^/    /' "$SHOTS/app.log"; exit 1; }
ok "window opened"
xdotool windowactivate "$WIN" 2>/dev/null
xdotool windowfocus "$WIN" 2>/dev/null
sleep 0.5

# Absolute coordinates from the window's real geometry. `mousemove --window`
# and `click` were being delivered inconsistently -- the button would take
# focus but never see the cursor over it at press time, so nothing fired.
eval "$(xdotool getwindowgeometry --shell "$WIN")"
WX=${X:-0}; WY=${Y:-0}
abs() { AX=$((WX + $1)); AY=$((WY + $2)); }

# 60fps: move and press must land on separate frames, so they are separate
# xdotool calls with a real gap, not one chained command.
# Press and release must land on DIFFERENT frames. A plain `xdotool click`
# does both within a millisecond, inside a single 16ms frame, and the app
# never sees the button at all -- which is why the first run of this harness
# reported everything as broken when only the harness was.
click(){ abs "$1" "$2"; xdotool mousemove "$AX" "$AY"; sleep 0.25
         xdotool mousedown 1; sleep 0.25
         xdotool mouseup 1;   sleep 0.4; }
# Same reasoning for keys: hold briefly so the down edge is seen.
key()  { xdotool keydown "$1"; sleep 0.2; xdotool keyup "$1"; sleep 0.4; }
drag() { abs "$1" "$2"; xdotool mousemove "$AX" "$AY"; sleep 0.2
         xdotool mousedown 1; sleep 0.2
         abs "$3" "$4"; xdotool mousemove "$AX" "$AY"; sleep 0.25
         xdotool mouseup 1; sleep 0.4; }
shot() { import -window "$WIN" "$SHOTS/$1.png" 2>/dev/null; }
alive(){ kill -0 $APP 2>/dev/null; }

MODE_Y=16
M_MAPS=33; M_OBJ=95; M_CAT=163; M_STR=232; M_ART=287; M_PAL=343; M_VAL=413; M_PKG=487

# ---------------------------------------------------------------- open a pack
shot 00_start
click 700 220            # Open Pack...
sleep 1.0
shot 01a_browser
# Type the path straight into the browser's path field, the way someone who
# knows where their pack is would.
click 665 239            # the path field
xdotool key --clearmodifiers ctrl+a 2>/dev/null; sleep 0.2
xdotool type --delay 25 "$WORK"; sleep 0.4
click 974 239            # Go
sleep 0.8
click 832 669            # Choose this folder
sleep 1.5
shot 01_opened
# Opening a pack with no art of its own raises the base-pack offer; dismiss it.
key Escape; sleep 0.6
# Assert on what the APP says it did. Checking the file exists would pass even
# if nothing opened, which is exactly how this test lied the first time.
if grep -q "gamebuilder: opened" "$SHOTS/app.log"; then
  ok "opened a pack from Recent"
else
  bad "pack did not open (still on the start screen?)"
fi

# ------------------------------------------------------------------- paint
click $M_MAPS $MODE_Y; sleep 0.4
tiles_hash() { grep -v '^#' "$1" | md5sum | cut -d' ' -f1; }
BEFORE=$(tiles_hash "$WORK/maps/italia.dat")
click 1186 526           # Paint tool (left column)
sleep 0.3
click 1180 416           # terrain: forest
sleep 0.3
drag 400 500 460 540     # paint a stroke on the canvas
shot 02_painted
key ctrl+s; sleep 1.2
AFTER=$(tiles_hash "$WORK/maps/italia.dat")
if [ "$BEFORE" != "$AFTER" ]; then ok "painting changed the map and Ctrl+S saved it"
else bad "painting did not change maps/italia.dat"; fi

# -------------------------------------------------------------------- undo
# One stroke is one undo step, so a single Ctrl+Z must restore the file
# exactly. Before stroke coalescing this undid a fraction of the drag.
key ctrl+z; sleep 0.6
key ctrl+s; sleep 1.2
UNDONE=$(tiles_hash "$WORK/maps/italia.dat")
if [ "$UNDONE" = "$BEFORE" ]; then ok "Ctrl+Z reverted the paint exactly"
else bad "undo did not restore the original bytes"; fi

# ------------------------------------------------------------- view/inspect
key v; sleep 0.3
click 500 450
shot 03_inspect
alive && ok "View tool inspects without crashing" || bad "crashed inspecting"

# ------------------------------------------------------------ place an object
count_objs() { python3 -c "
import json;d=json.load(open('$WORK/game.json'))
n=sum(len(z.get(k,[])) for z in d['zones']
      for k in ('chests','signs','dwellings','armies'))
print(n)"; }
OBJ_BEFORE=$(count_objs)
click $M_OBJ $MODE_Y; sleep 0.5
click 1186 194           # 'chest' -- left column of the PLACE list
sleep 0.7
key ctrl+s; sleep 1.4
OBJ_AFTER=$(count_objs)
if [ "$OBJ_AFTER" -gt "$OBJ_BEFORE" ]; then
  ok "placed an object through the UI ($OBJ_BEFORE -> $OBJ_AFTER placements)"
else bad "object placement did not reach game.json"; fi
shot 04_objects

# ------------------------------------------------------------------ catalog
click $M_CAT $MODE_Y; sleep 0.5
shot 05_catalog
click 130 200            # a troop in the entry list
sleep 0.4
alive && ok "catalog lists and selects entries" || bad "crashed in catalog"

# ------------------------------------------------------------------ strings
click $M_STR $MODE_Y; sleep 0.5
shot 06_strings
alive && ok "strings tab renders" || bad "crashed in strings"

# ---------------------------------------------------------------------- art
click $M_ART $MODE_Y; sleep 0.6
shot 07_art
alive && ok "art tab renders" || bad "crashed in art"

# ------------------------------------------------------------------ palette
click $M_PAL $MODE_Y; sleep 0.5
click 100 300            # pick a palette entry
shot 08_palette
alive && ok "palette tab accepts a selection" || bad "crashed in palette"

# ----------------------------------------------------------------- validate
click $M_VAL $MODE_Y; sleep 0.5
click 68 40              # Run checks
sleep 1.5
shot 09_validate
if grep -q "gamebuilder: validated" "$SHOTS/app.log"; then
  ok "validation ran from the UI ($(grep -o 'validated -- .*' "$SHOTS/app.log" | tail -1))"
else bad "validation did not run"; fi

# ------------------------------------------------------------------ package
rm -f "$WORK"/*.openbounty
click $M_PKG $MODE_Y; sleep 0.5
shot 10_package
click 92 245             # Build .openbounty
sleep 2.5
shot 11_packaged
if grep -q "gamebuilder: packaged" "$SHOTS/app.log" && \
   ls "$WORK"/*.openbounty >/dev/null 2>&1; then
  ok "packaged a .openbounty from the UI ($(ls -1 "$WORK"/*.openbounty | head -1 | xargs basename))"
else bad "no archive was produced"; fi

# ------------------------------------------------------------------ survived
if alive; then ok "still running after every mode"; else bad "app died during the run"; fi

kill $APP 2>/dev/null; wait $APP 2>/dev/null

echo
echo "  $PASS passed, $FAIL failed"
echo "  screenshots: $SHOTS"
[ $FAIL -eq 0 ]
