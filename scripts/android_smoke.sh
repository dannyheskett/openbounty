#!/usr/bin/env bash
# Install the emulator APK, launch it, and prove it is still running.
#
# What this proves and what it does not: the APK installs, the activity starts,
# the pack is read out of the APK, and the process is alive 20s later. It does
# NOT prove the game draws -- the CI emulator's software GL does not always
# give raylib an EGL configuration it can use, and a failed context leaves the
# screen black with the process perfectly healthy. The screenshot and the full
# logcat are kept as artifacts so that case is visible rather than silent.
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

# Android shows a "Viewing full screen / swipe down to exit" dialog the first
# time an app goes immersive, and that dialog TAKES FOCUS: raylib stops
# rendering when the window is not focused, so the game freezes on whatever
# frame it had reached and every screenshot after it is that same frame.
adb shell settings put secure immersive_mode_confirmations confirmed || true

adb install -r "$APK"

pid=""
for attempt in 1 2 3; do
    adb logcat -c
    adb shell am start -n "$ACT"
    sleep 20
    pid="$(adb shell pidof "$PKG" | tr -d '\r' || true)"
    [ -n "$pid" ] && break

    # The runner is a shared, memory-tight machine and the emulator is the
    # heaviest thing on it. When it is out of room, the app never starts at
    # all: "ZygoteStartFailedEx: fork() failed", which says nothing about the
    # app. Retry that; treat anything else as a real failure straight away.
    adb logcat -d > android-logcat.txt || true
    if grep -q "Starting VM process through Zygote failed" android-logcat.txt; then
        echo "attempt $attempt: the emulator could not fork a process; retrying"
        continue
    fi
    break
done

adb exec-out screencap -p > android-shot.png || true

# The first 20s is the intro sequence: a small logo magnified to fill the
# frame, which says nothing about how the game proper looks. Take a second
# shot once it has reached the title menu.
sleep 20
adb exec-out screencap -p > android-shot-late.png || true

echo "--- the game's own stdout, piped into logcat ---"
adb logcat -d -s openbounty:* || true

echo "--- logcat (everything since launch) ---"
adb logcat -d > android-logcat.txt || true
tail -300 android-logcat.txt

echo "pid: ${pid:-none}"
if [ -z "$pid" ]; then
    echo "::error::$PKG is not running 20s after launch"
    exit 1
fi
echo "[android] the app is alive 20s after launch (pid $pid)"

# Alive is not the same as drawing. A failed EGL context leaves every GL call
# with nowhere to go and the screen black, while the process sits there
# perfectly happily -- which is what the emulator did the first time.
if grep -qE "EGL_BAD_CONFIG|Failed to create EGL|Failed to choose an EGL|no current context" android-logcat.txt; then
    echo "::warning::the app started but EGL reported an error; the frame is probably black"
    grep -E "EGL|raylib" android-logcat.txt | tail -20
fi
