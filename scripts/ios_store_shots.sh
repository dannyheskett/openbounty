#!/usr/bin/env bash
# Capture App Store screenshots from the app playing itself on a Simulator.
#
# There is no supported way to send a tap to a Simulator, so the game is
# launched with --demo (ios/ios_main.mm forwards launch arguments to it) and
# screencapped while it plays. Every frame is the real app; nothing is scaled,
# padded or composited to reach a store size.
#
# Apple's largest iPhone slot is APP_IPHONE_67: 1290x2796 portrait, so
# 2796x1290 landscape. That is a Plus or Pro Max of the 15/16 generation --
# the 16 Pro Max is 1320x2868 and does NOT fit the slot. Which devices a
# runner image carries changes with Xcode, so this takes the first of a
# preference list and reports what it actually captured.
#
#   scripts/ios_store_shots.sh <app-bundle> <outdir> [shots] [seconds-apart]

set -euo pipefail

APP="${1:?usage: ios_store_shots.sh <app> <outdir> [shots] [gap]}"
OUT="${2:?usage: ios_store_shots.sh <app> <outdir> [shots] [gap]}"
SHOTS="${3:-10}"
GAP="${4:-12}"

echo "--- iPhone simulators on this image ---"
xcrun simctl list devices available | sed -n 's/^ *\(iPhone[^(]*\).*/\1/p' | sort -u

# `python3 - <<EOF` reads the SCRIPT from stdin, which leaves nothing for the
# device list to arrive on -- it died on an empty JSON parse the first time.
# So the list goes to a file and the chooser reads that.
xcrun simctl list devices available -j > "${TMPDIR:-/tmp}/simdevices.json"
UDID=$(python3 -c "
import json, sys
want = ['iPhone 16 Plus', 'iPhone 15 Plus', 'iPhone 14 Plus',
        'iPhone 17 Plus', 'iPhone 16 Pro Max', 'iPhone 17 Pro Max']
have = {x['name']: x['udid'] for r in json.load(open(sys.argv[1]))['devices'].values() for x in r}
for n in want:
    if n in have:
        print(have[n]); break
else:
    print(next(iter(have.values())))
" "${TMPDIR:-/tmp}/simdevices.json")
echo "device: $UDID"

xcrun simctl boot "$UDID"
xcrun simctl bootstatus "$UDID" -b
xcrun simctl install "$UDID" "$APP"
bundle=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$APP/Info.plist")
xcrun simctl launch "$UDID" "$bundle" --demo

mkdir -p "$OUT"
for i in $(seq 1 "$SHOTS"); do
    sleep "$GAP"
    xcrun simctl io "$UDID" screenshot "$OUT/shot-$i.png"
done

# simctl captures the DEVICE, which stays portrait while the app is
# landscape-locked, so each shot is rotated a quarter turn anticlockwise.
for f in "$OUT"/shot-*.png; do sips -r 270 "$f" >/dev/null; done

w=$(sips -g pixelWidth  "$OUT/shot-1.png" | awk '/pixelWidth/{print $2}')
h=$(sips -g pixelHeight "$OUT/shot-1.png" | awk '/pixelHeight/{print $2}')
echo "captured ${w}x${h}"
if [ "$w" != "2796" ] || [ "$h" != "1290" ]; then
    echo "::warning::captured ${w}x${h}, which is not Apple's 2796x1290 APP_IPHONE_67 size"
fi
