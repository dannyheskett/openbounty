#!/usr/bin/env bash
# Phase 0 capture harness. Drives a running OpenBounty window with xdotool and
# pulls frames out through the built-in backtick screenshot key, which exports
# the render target (the design-space buffer) rather than the scaled window.
#
#   tools/capture.sh start <pack> [w] [h]   launch and size the window
#   tools/capture.sh shot  <label>          capture, rename to <label>.png
#   tools/capture.sh key   <keys...>        send keys, one xdotool key per arg
#   tools/capture.sh stop
set -u
DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$DIR/screenshots"
WIN() { xdotool search --name "$(cat "$OUT/.title" 2>/dev/null || echo Bounty)" | tail -1; }

case "${1:-}" in
start)
  pack="${2:-glory-of-rome}"; w="${3:-1920}"; h="${4:-1040}"
  pkill -x openbounty; sleep 1
  mkdir -p "$OUT"; rm -f "$OUT"/*.png
  case "$pack" in glory-of-rome) echo "Glory of Rome" > "$OUT/.title";;
                  *)             echo "King's Bounty" > "$OUT/.title";; esac
  cd "$DIR" || exit 1
  setsid nohup ./build/debug/openbounty --pack "$pack" \
      > "$OUT/.log" 2>&1 < /dev/null &
  sleep 5
  wid=$(WIN); [ -z "$wid" ] && { echo "no window"; exit 1; }
  # Pin to the origin first: a window hanging off the display gets
  # clipped by import and the capture silently comes back short.
  xdotool windowmove "$wid" 0 0; sleep 1
  xdotool windowsize "$wid" "$w" "$h"; sleep 2
  xdotool windowactivate --sync "$wid"; sleep 1
  echo "$wid"
  ;;
key)
  shift
  wid=$(WIN); xdotool windowactivate --sync "$wid" 2>/dev/null
  # --delay holds the key down across at least one 60fps poll; at the
  # xdotool default the press and release land in the same frame and
  # IsKeyPressed never fires.
  for k in "$@"; do xdotool key --clearmodifiers --delay 200 "$k"; sleep 0.8; done
  sleep 1
  ;;
shot)
  # Grab the window itself rather than the render target: the startup screens
  # run their own loop and never reach the backtick handler, and the window is
  # what the player actually sees, scaling included.
  label="${2:-shot}"
  wid=$(WIN); xdotool windowactivate --sync "$wid" 2>/dev/null; sleep 0.5
  import -window "$wid" "$OUT/$label.png" 2>/dev/null \
    && echo "$OUT/$label.png" || { echo "MISS $label"; exit 1; }
  ;;
stop)
  pkill -x openbounty
  ;;
*) echo "usage: capture.sh start|key|shot|stop"; exit 1;;
esac
