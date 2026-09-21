# Submitting Glory of Rome to the App Store and Google Play

The mobile app is **Glory of Rome only** — one pack, no picker, bundle id
`com.danheskett.gloryofrome` on both stores. Desktop OpenBounty is unaffected
by everything here.

This is the procedure and the reference: how the two apps are built, what each
store requires, and which parts are scripted.

---

## 1. How the apps are built

Both platforms build from the Makefile, with no Xcode project and no Gradle.

| | Android | iOS |
|---|---|---|
| Target | `make android` (APK), `make android-play` (AAB) | `make ios` (device `.ipa`), `make ios-sim` (Simulator `.app`) |
| Renderer | raylib, as on desktop | native Metal (`ios/gfx_metal.mm`), no raylib in the build |
| Pack | inside the APK/AAB assets | inside the `.app` bundle |
| Saves | app internal storage | the app's `Documents/saves` |
| Orientation | landscape-locked in the manifest | landscape-only in `Info.plist` |
| Presentation | fixed 800x532 buffer at the largest whole-number scale that fits the safe area | same |
| Safe area | display cutout insets over JNI | `safeAreaInsets` in device pixels |
| Network | none — no permission is requested | none |
| Logs | the game's stdout is piped into logcat | and into `os_log` |

The shipped ABI is arm64-v8a. `ANDROID_ABI=x86_64` builds a second APK for the
CI emulator; it is never shipped.

`scripts/raylib-android-eglconfig.patch` is applied by
`scripts/build_raylib_android.sh`: raylib's Android backend ignores what
`eglChooseConfig` returns, so when no configuration matches its request it
creates a context against an unset one and fails with `EGL_BAD_CONFIG` and no
diagnosis. The patch walks colour and depth down, and failing that picks a
window-capable ES2 configuration by hand.

---

## 2. What each store requires

**Assets**

| File | Store | Spec |
|---|---|---|
| `ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png` | Apple | 1024x1024, opaque, no rounded corners. `actool` derives every other size. A signed `make ios` fails without it. |
| `android/res/mipmap-*/ic_launcher.png` + `android:icon` in the manifest | Play | the launcher icon |
| `android/play-assets/icon-512.png` | Play | 512x512, 32-bit, no transparency |
| `android/play-assets/feature-graphic-1024x500.png` | Play | 1024x500 |
| `ios/app-store-assets/screenshots/iphone-6.9/` | Apple | 2868x1320 landscape. `xcrun simctl io <udid> screenshot` on an iPhone 16 Pro Max captures exactly that from the real app, so no image is ever scaled or composited to hit a store's size. |
| `ios/app-store-assets/screenshots/ipad-13/` | Apple | required while `UIDeviceFamily` includes iPad |
| `android/play-assets/screenshots/` | Play | landscape, at least 1080 on the long edge |

**Text** — `android/play-assets/LISTING.md` and `ios/app-store-assets/LISTING.md`
hold every field, in fenced blocks that `scripts/store_listing.py` parses.
`android/play-assets/PRIVACY.md` is the privacy policy; both stores want it at
a public URL.

**Console work, which neither store exposes an API for**

- Apple: creating the app record, and the App Privacy nutrition label.
- Play: everything — the app record, the Data safety form, the IARC content
  rating, the target-audience declaration, and the closed-testing gate for
  production access. The answers are all "none"; the copy to paste is in
  `LISTING.md`.

**Secrets** — `docs/RELEASE-PROCESS.md` lists them by name and says what each
one unlocks. `ios/app-store-assets/TESTFLIGHT.md` is the step-by-step for
creating them without a Mac.

---

## 3. What is scripted

| Script | What it does |
|---|---|
| `scripts/store_listing.py` | parses both LISTING.md files, enforces each store's length limits, and bans a listing that names another store or the original game. CI runs `--check` on every PR. |
| `scripts/asc_setup.py` | one-time Apple setup: register the App ID, create the App Store provisioning profile bound to the team certificate, then set category, content rights, age rating, privacy-policy URL, support/marketing URLs, a free price, and availability in every territory except mainland China. |
| `scripts/asc_release.py` | `status`, `listing` (text + screenshots), `release --build N [--submit]`. |
| `scripts/testflight_notes.py` | waits out Apple's processing window and writes "What to Test" onto the build TestFlight just received. |
| `scripts/devicefarm_run.py` | uploads the APK or `.ipa` to AWS Device Farm and fuzz-tests it on real phones. |
| `scripts/android_smoke.sh` | installs the emulator APK in CI, launches it, and fails unless the process is alive 20 s later. |

| Workflow | Trigger |
|---|---|
| `asc-setup.yml` | manual; one verb per run, `dry_run` on by default |
| `store-release.yml` | manual; push the listing and submit a chosen build, `dry_run` on by default |
| `devicefarm.yml` | manual; real-device fuzz test of both apps (AWS OIDC, no stored keys) |
| `release.yml` — `build-ios`, `publish-testflight`, `testflight-notes`, `submit-appstore`, `build-android`, `publish-play` | every merge to `main`, each gated on its own secrets |

Play has no equivalent automation beyond the internal-track upload.
