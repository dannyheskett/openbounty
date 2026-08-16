#!/usr/bin/env bash
# Capture harness. Drives a running OpenBounty window with xdotool and grabs
# frames with `import`.
#
#   tools/capture.sh start <pack> [w] [h] [out]  launch and size the window
#   tools/capture.sh shot  <label>               capture to <out>/<label>.png
#   tools/capture.sh type  <text>                type a string
#   tools/capture.sh key   <keys...>             send keys, one xdotool key per arg
#   tools/capture.sh stop
#
# Three things must be right or the captures lie:
#
#   1. Keys go to the window with `key --window`, NOT to the focused window.
#      This X server has no window manager, so `windowactivate` fails and every
#      key is dropped silently -- the capture then returns the same frame over
#      and over and the screens look verified when nothing was.
#   2. --delay 200 holds the key down across at least one 60fps poll. At the
#      xdotool default the press and release land in the same frame and
#      IsKeyPressed never fires.
#   3. The window must sit at the origin, or `import` silently returns a
#      clipped image when part of it hangs off the display.
set -u
DIR="$(cd "$(dirname "$0")/.." && pwd)"
STATE="${TMPDIR:-/tmp}/openbounty-capture"
OUT() { cat "$STATE/out" 2>/dev/null || echo "$DIR/screenshots"; }
WIN() { cat "$STATE/wid" 2>/dev/null; }

case "${1:-}" in
start)
  pack="${2:-glory-of-rome}"; w="${3:-1920}"; h="${4:-1040}"
  out="${5:-$DIR/screenshots}"
  pkill -x openbounty; sleep 1
  mkdir -p "$STATE" "$out"; echo "$out" > "$STATE/out"
  case "$pack" in glory-of-rome) title="Glory of Rome";;
                  *)             title="King's Bounty";; esac
  cd "$DIR" || exit 1
  setsid nohup ./build/debug/openbounty --pack "$pack" \
      > "$STATE/log" 2>&1 < /dev/null &
  sleep 6
  wid=$(xdotool search --name "$title" | tail -1)
  [ -z "$wid" ] && { echo "no window"; cat "$STATE/log"; exit 1; }
  echo "$wid" > "$STATE/wid"
  xdotool windowmove "$wid" 0 0; sleep 1
  xdotool windowsize "$wid" "$w" "$h"; sleep 2
  echo "$wid"
  ;;
key)
  shift
  wid=$(WIN)
  for k in "$@"; do
    xdotool key --window "$wid" --clearmodifiers --delay 200 "$k"; sleep 0.8
  done
  ;;
type)
  wid=$(WIN)
  xdotool type --window "$wid" --delay 150 "${2:-}"; sleep 1
  ;;
shot)
  label="${2:-shot}"; out=$(OUT); sleep 0.5
  import -window "$(WIN)" "$out/$label.png" 2>/dev/null \
    && echo "$out/$label.png" || { echo "MISS $label"; exit 1; }
  ;;
stop)
  pkill -x openbounty; rm -rf "$STATE"
  ;;
*) echo "usage: capture.sh start|key|type|shot|stop"; exit 1;;
esac
