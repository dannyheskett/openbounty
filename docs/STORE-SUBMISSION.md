# Submitting Glory of Rome to the App Store and Google Play

The mobile app has been **Glory of Rome only** — one pack, no picker, bundle id
`com.danheskett.gloryofrome` on both stores. Desktop OpenBounty has been
unaffected by everything here.

This has been the procedure and the reference: how the two apps have been
built, what each store has required, and which parts have been scripted.

---

## 1. How the apps have been built

Both platforms have built from the Makefile, with no Xcode project and no
Gradle.

| | Android | iOS |
|---|---|---|
| Target | `make android` (APK), `make android-play` (AAB) | `make ios` (device `.ipa`), `make ios-sim` (Simulator `.app`) |
| Renderer | raylib, as on desktop | native Metal (`ios/gfx_metal.mm`), no raylib in the build |
| Pack | inside the APK/AAB assets | inside the `.app` bundle |
| Saves | app internal storage | the app's `Documents/saves` |
| Orientation | landscape-locked in the manifest | landscape-only in `Info.plist` |
| Presentation | an 800x532 buffer at the largest whole-number scale that fits the safe area; the world map has grown into what is left (REQ-528) | same |
| Safe area | display cutout insets over JNI | `safeAreaInsets` in device pixels |
| Network | none — no permission requested | none |
| Logs | the game's stdout piped into logcat | and into `os_log` |

The shipped ABI has been arm64-v8a. `ANDROID_ABI=x86_64` has built a second
APK for the CI emulator; it has never shipped.

`scripts/build_raylib_android.sh` has applied
`scripts/raylib-android-eglconfig.patch`: raylib's Android backend has ignored
what `eglChooseConfig` returns, so when no configuration has matched its
request it has created a context against an unset one and failed with
`EGL_BAD_CONFIG` and no diagnosis. The patch has walked colour and depth down,
and failing that has picked a window-capable ES2 configuration by hand.

---

## 2. What each store has required

**Assets**

| File | Store | Spec |
|---|---|---|
| `ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png` | Apple | 1024x1024, opaque, no rounded corners. `actool` has derived every other size. A signed `make ios` has failed without it. |
| `android/res/mipmap-*/ic_launcher.png` + `android:icon` in the manifest | Play | the launcher icon |
| `android/play-assets/icon-512.png` | Play | 512x512, 32-bit, no transparency |
| `android/play-assets/feature-graphic-1024x500.png` | Play | 1024x500 |
| `ios/app-store-assets/screenshots/iphone-6.9/` | Apple | landscape; the 6.9" slot has taken 2868x1320 or 2796x1290 (the five here are 2796x1290). `xcrun simctl io <udid> screenshot` on a Simulator of that size has captured the real app, so no image has been scaled or composited to hit a store's size. |
| `ios/app-store-assets/screenshots/ipad-13/` | Apple | 2732x2048 landscape; required while `UIDeviceFamily` has included iPad |
| `android/play-assets/screenshots/` | Play | landscape, at least 1080 on the long edge |

**Text** — `android/play-assets/LISTING.md` and `ios/app-store-assets/LISTING.md`
have held every field, in fenced blocks that `scripts/store_listing.py` has
parsed. `android/play-assets/PRIVACY.md` has been the privacy policy; both
stores have wanted it at a public URL.

**Console work, which neither store has exposed an API for**

- Apple: creating the app record, and the App Privacy nutrition label.
- Play: everything — the app record, the Data safety form, the IARC content
  rating, the target-audience declaration, and the closed-testing gate for
  production access. The answers have all been "none"; the copy to paste has
  been in `LISTING.md`.

**Secrets** — `docs/RELEASE-PROCESS.md` has listed them by name and said what
each one unlocks. `ios/app-store-assets/TESTFLIGHT.md` has been the
step-by-step for creating them without a Mac.

---

## 3. What has been scripted

| Script | What it has done |
|---|---|
| `scripts/store_listing.py` | parsed both LISTING.md files, enforced each store's length limits, and banned a listing that names another store or the original game. CI has run `--check` on every PR. |
| `scripts/asc_setup.py` | one-time Apple setup: registered the App ID, created the App Store provisioning profile bound to the team certificate, then set category, content rights, age rating, privacy-policy URL, support/marketing URLs, a free price, and availability in every territory except mainland China. |
| `scripts/asc_release.py` | `status`, `listing` (text + screenshots), `release --build N [--submit]`. |
| `scripts/asc_next_build.py` | printed the next build number App Store Connect has not seen, so branch and release builds have never collided. |
| `scripts/testflight_notes.py` | waited out Apple's processing window and written "What to Test" onto the build TestFlight has just received. |
| `scripts/devicefarm_run.py` | uploaded the APK or `.ipa` to AWS Device Farm and fuzz-tested it on real phones. |
| `scripts/android_smoke.sh` | installed the emulator APK in CI, launched it, and failed unless the process has been alive 20 s later. |

| Workflow | Trigger |
|---|---|
| `asc-setup.yml` | manual; one verb per run, `dry_run` on by default |
| `store-release.yml` | manual; the listing pushed and a chosen build submitted, `dry_run` on by default |
| `testflight.yml` | manual; the chosen branch built, signed and sent to TestFlight, with no tag and no release |
| `devicefarm.yml` | manual; real-device fuzz test of both apps (AWS OIDC, no stored keys) |
| `release.yml` — `build-ios`, `publish-testflight`, `testflight-notes`, `submit-appstore`, `build-android`, `publish-play` | every merge to `main`, each gated on its own secrets; `submit-appstore` only when asked for |

Play has had no automation beyond the internal-track upload.
