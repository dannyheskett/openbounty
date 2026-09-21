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
   Console yet. Apple's app record and its App Privacy label have no API and
   have to be created by hand; everything else on the Apple side is scripted
   (below). Play is all console work: bundle id, the content-rating
   questionnaire and the data-safety form (all answers are "none"; the copy to
   paste is in `android/play-assets/LISTING.md`).
5. **A published privacy policy URL.** The text is written
   (`android/play-assets/PRIVACY.md`); both stores want it at a public URL.
6. **The signing secrets**, which do not exist yet: an Apple Distribution
   certificate and App Store provisioning profile, an App Store Connect API
   key, a Play upload keystore and a Play service account. Until they are set,
   the release builds an unsigned `.ipa` and no AAB, and uploads nothing.

---

## 3. What is built but not yet proven on real hardware

Stated plainly, because "it compiles" is not "it works":

- **The Android app has never drawn a frame.** CI now installs the APK on an
  emulator, launches it and checks it is still alive 20 s later, and it is:
  the activity starts, the pack is read out of the APK, the process is
  healthy. But the emulator's software GL does not reliably give raylib an
  EGL configuration -- `eglChooseConfig` matches nothing, even with the depth
  buffer dropped to zero -- so `eglCreateContext` fails and every frame goes
  nowhere. The screenshot artifact is black.

  Whether this is emulator-only is **unknown**. The same raylib configuration
  ships in the other `open*` games, which are tested on real hardware through
  a device farm rather than an emulator. Two ways to settle it: sideload the
  arm64 APK from the CI artifact onto a phone, or run the **devicefarm**
  workflow, which is ported here and fuzz-tests both apps on real hardware --
  it needs `AWS_ROLE_ARN` and `DEVICEFARM_PROJECT_ARN`, the same secrets the
  other repositories use.
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

---

## 4. What is scripted on the Apple side

Ported from the other `open*` games, so both stores are driven the same way:

| Script | What it does |
|---|---|
| `scripts/store_listing.py` | parses both LISTING.md files, enforces each store's length limits, and bans a listing that names another store or the original game. CI runs `--check` on every PR. |
| `scripts/asc_setup.py` | one-time setup: register the App ID, create the App Store provisioning profile bound to the team certificate, then set category, content rights, age rating, privacy-policy URL, support/marketing URLs, a free price, and availability in every territory except mainland China. |
| `scripts/asc_release.py` | `status`, `listing` (text + screenshots), `release --build N [--submit]` — creates the version, attaches an uploaded build, pushes the listing, and submits to App Review. |
| `scripts/testflight_notes.py` | waits out Apple's processing window and writes "What to Test" onto the build TestFlight just received. |

| Workflow | Trigger |
|---|---|
| `.github/workflows/asc-setup.yml` | manual; one verb per run, `dry_run` on by default |
| `.github/workflows/devicefarm.yml` | manual; fuzz-tests the real APK and .ipa on real phones (AWS OIDC, no stored keys) |
| `.github/workflows/store-release.yml` | manual; push the listing and submit a chosen build, `dry_run` on by default |
| `release.yml` `publish-testflight` / `testflight-notes` / `submit-appstore` | every merge to `main`, all gated on the `ASC_*` secrets |

`ios/app-store-assets/TESTFLIGHT.md` is the step-by-step for the parts only a
human can do: the certificate, the app record, the API key, and the seven
repository secrets.

Nothing equivalent exists for Play beyond the internal-track upload: the Data
safety form, the IARC rating and the closed-testing gate are console work that
Google exposes no API for.
