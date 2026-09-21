#!/usr/bin/env bash
# Install the emulator APK, launch it, and prove it is still running.
#
# This is a file rather than inline YAML because android-emulator-runner runs
# each LINE of its `script:` as its own `sh -c`: shell variables do not carry
# from one line to the next and a multi-line `if` is a syntax error.
#
# Called from .github/workflows/ci.yml with the APK path as $1.

set -eux

APK="${1:?usage: android_smoke.sh <apk>}"
PKG=com.danheskett.gloryofrome
ACT="$PKG/$PKG.GloryOfRomeActivity"

adb install -r "$APK"
adb logcat -c
adb shell am start -n "$ACT"
sleep 20

adb exec-out screencap -p > android-shot.png || true

echo "--- logcat (everything since launch) ---"
adb logcat -d > android-logcat.txt || true
tail -300 android-logcat.txt

# Alive, not merely launched: a native crash shows up as no pid at all.
pid="$(adb shell pidof "$PKG" | tr -d '\r' || true)"
echo "pid: ${pid:-none}"
if [ -z "$pid" ]; then
    echo "::error::$PKG is not running 20s after launch"
    exit 1
fi
echo "[android] the app is alive 20s after launch (pid $pid)"

# Alive is not the same as drawing. A failed EGL context leaves every GL call
# with nowhere to go and the screen black, while the process sits there
# perfectly happily -- which is what the emulator did the first time.
if grep -qE "EGL_BAD_CONFIG|Failed to create EGL|no current context" android-logcat.txt; then
    echo "::warning::the app started but EGL reported an error; the frame is probably black"
    grep -E "EGL|raylib" android-logcat.txt | tail -20
fi
