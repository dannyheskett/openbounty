# Submitting Glory of Rome to the App Store and Google Play

The mobile app is **Glory of Rome only** — one pack, no picker, bundle id
`com.danheskett.gloryofrome` on both stores. Desktop OpenBounty is unaffected
by everything here.

This document is the checklist for an actual submission and, just as
importantly, the honest list of what is **not** done yet.

---

## 1. What the build already does

Both platforms build from the Makefile, with no Xcode project and no Gradle.

| | Android | iOS |
|---|---|---|
| Target | `make android` (APK), `make android-play` (AAB) | `make ios` (device `.ipa`), `make ios-sim` (Simulator `.app`) |
| Renderer | raylib, as on desktop | native Metal (`ios/gfx_metal.mm`), no raylib anywhere in the build |
| Pack | inside the APK/AAB assets | inside the `.app` bundle |
| Saves | app internal storage | the app's `Documents/saves` |
| Orientation | landscape-locked in the manifest | landscape-only in `Info.plist` |
| Presentation | fixed 800×532 buffer at the largest whole-number scale that fits the safe area | same |
| Safe area | display cutout insets over JNI | `safeAreaInsets` in device pixels |
| Network | none — no permission is even requested | none |

CI builds both on every pull request: the Android job asserts the APK carries
the pack, the `.so` and `classes.dex`; the iOS job builds a Simulator app,
boots a simulator, launches it and captures screenshots at 10 s and 30 s, which
is how the renderer was brought up in the first place.

The release workflow produces an upload-signed AAB and an App Store-signed
`.ipa` when the signing secrets are present, pushes the AAB to Play's internal
track, and uploads the `.ipa` to App Store Connect (see
`docs/RELEASE-PROCESS.md` for the job list and the exact secret names).

---

## 2. What is missing before either store will take it

Blocking, in rough order of effort:

1. **The app icon.** Nothing is drawn yet.
   - iOS needs exactly one file: `ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png`,
     1024×1024, opaque. `actool` derives every other size. `make ios` with a
     signing identity set **fails** rather than building an iconless bundle.
   - Android needs `android/res/mipmap-*/ic_launcher.png` plus
     `android:icon="@mipmap/ic_launcher"` in the manifest (the manifest
     comment marks the spot), and Play's listing needs `icon-512.png`.
2. **Store screenshots.** Landscape captures of the real frame. The gallery
   (`--gallery`) already produces exact frames; they need to be captured at a
   size each store accepts (Play: ≥1080 long edge; Apple: 6.9" iPhone at
   2868×1320, plus 13" iPad unless `UIDeviceFamily` drops the iPad entry).
   The cheapest honest route is the Simulator: `xcrun simctl io <udid>
   screenshot` on an iPhone 16 Pro Max captures exactly 2868×1320 of the real
   app, which is what the CI iOS job already does for its diagnostics, so no
   image is ever scaled or composited to hit a store's size.
3. **A Play feature graphic**, 1024×500.
4. **Store records.** Neither app exists in App Store Connect or the Play
   Console yet. Both have to be created by hand once: bundle id, SKU, name,
   category, the content-rating questionnaire and the data-safety form (all
   answers are "none"; the copy to paste is in `android/play-assets/LISTING.md`
   and `ios/app-store-assets/LISTING.md`).
5. **A published privacy policy URL.** The text is written
   (`android/play-assets/PRIVACY.md`); both stores want it at a public URL.
6. **The signing secrets**, which do not exist yet: an Apple Distribution
   certificate and App Store provisioning profile, an App Store Connect API
   key, a Play upload keystore and a Play service account. Until they are set,
   the release builds an unsigned `.ipa` and no AAB, and uploads nothing.

---

## 3. What is built but not yet proven on real hardware

Stated plainly, because "it compiles" is not "it works":

- **The Android APK has never been run**, on a device or an emulator. It is
  assembled and its contents are asserted in CI, and it is the same raylib
  build as the desktop game, but nobody has watched it start.
- **iOS touch input is unverified.** The renderer is confirmed from CI
  screenshots — the title screen draws correctly, the app is alive 30 s in —
  but no tap has been delivered to the app. The touch *mapping* is shared
  code that the Rome gallery tapcheck covers (62 checks, 0 failures); what is
  unproven is the iOS end of it: `ios/plat_ios.mm` publishing a contact and
  `present_window_to_screen` undoing the new whole-number scale.
- **iOS audio is unverified.** `ios/audio_ios.mm` is written and links; no one
  has heard it.
- **Nothing has been run on an iPad**, though `UIDeviceFamily` claims one.

None of this is hard to close — it is a device, an hour, and a walk through
title → class → map → a fight → save → load on each platform. It just has not
happened, and no store submission should go out before it does.
