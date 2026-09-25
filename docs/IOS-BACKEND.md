# The iOS backend

Glory of Rome has run on iOS **without raylib**. The engine (`engine/`), the
demo agent (`demo/`) and the autoplay oracle (`autoplay/`) have had no raylib
at all; the shell (`src/`) has reached the platform through five seams, and
iOS has implemented each of them natively in `ios/`. Every other platform has
implemented the same seams with raylib.

## The seams

| Seam | What it has covered | raylib side | iOS side |
|---|---|---|---|
| `src/gfx.h` | 2D primitives, textures, the offscreen frame buffer, labels | `src/gfx_raylib.c` | `ios/gfx_metal.mm` |
| `src/frame_host.h` | window, display size, time, frame pacing | `src/frame_host.c` | `ios/host_ios.c` |
| `src/input_host.h` | keys, characters, touch, gamepad | `src/input_host.c` | `ios/host_ios.c` |
| `src/audio_backend.h` | audio device, one-shot sounds, music streams | `src/audio_raylib.c` | `ios/audio_ios.mm` |
| `src/font_backend.h` | baking the pack's own TrueType face | `src/font_raylib.c` | `ios/font_ios.c` |

`src/ob_types.h` has carried the types, colours and `KEY_*` / `GAMEPAD_*` ids
so shared code has compiled with no raylib header. Image decoding for the iOS
build has been `ios/image_ios.c` (stb_image); the music decoder has been
stb_vorbis, compiled once in `ios/vorbis_impl.c`.

**The split has been enforced on every build.** `make` has type-checked every
iOS-bound shell file with `-DPLATFORM_IOS` and no raylib include path
(`build/ios-purity.stamp`), so a raylib call reaching shared code has failed
the build on any machine, Xcode or not.

## What the iOS build has left out

The Makefile's `IOS_SKIP` has removed the raylib seam implementations and the
desktop-only subsystems: the movie recorder and its MP4 encoder
(`src/recorder.c`, `src/encode_mp4*.c`, `src/encode_dialog.c`), screenshots
and the gallery (`src/screenshot.c`, `src/shell_gallery.c`), and the pack
picker (`src/pack_select.c`) -- the app has shipped exactly one pack. Their
headers have compiled to inline no-ops under `PLATFORM_IOS`, so call sites
have needed no guards. The demo agent and autoplay have been in the build.

## The app shell -- `ios/ios_main.mm`

- `UIApplication` with a view backed by a `CAMetalLayer`, landscape only
  (`ios/Info.plist`, and the view controller has stated it too). The drawable
  has been sized in device pixels and the safe-area insets have been
  published to the game.
- **The game has run on its own thread** with a 16 MB stack, and its blocking
  loops have stayed exactly as they are on every other platform: a
  `CADisplayLink` on the main thread has only advanced the clock, and the
  game thread has paced itself in `frame_host_yield` (`ios/host_ios.c`). This
  has been the one structural difference from the web build, which has
  unwound the same loops with ASYNCIFY.
- The game thread has started the game with no arguments
  (`shell_run_game(1, {"gloryofrome"})`): the app has taken no launch flags.
- The game's stdout has been piped into the unified log with `os_log`
  (`plat_ios_log_stdout`, `ios/plat_ios.mm`). stderr has been left alone,
  because `NSLog` writes there and would feed itself.

## What UIKit has published -- `ios/plat_ios.mm`

Screen size and safe-area origin, one touch contact, foreground state and the
frame clock. Each value has been a word-sized store; the touch position and
its down state have been published together under a sequence counter so the
game thread has never read half an update. A contact has been measured from
the safe-area origin, the same origin the frame is drawn at. The view has
published the touch it was given, never one read out of the event's whole
set, so a finger resting elsewhere on the glass has not been mistaken for
the contact after a tap lifted. A press the game has not sampled yet has been
kept until a read has seen it, so a tap that began and ended between two of
the game's once-a-frame looks has still arrived as a one-frame contact.

`src/plat_ios.c` has been the shell half: the save root has been the app's
`Documents/saves`, and the pack (`glory-of-rome.openbounty`, a bundle
resource) has been read into memory and opened with `pack_open_mem`, exactly
as on Android.

## Rendering -- `ios/gfx_metal.mm`

- **One pipeline, one shader**, compiled from Metal source at run time
  (`newLibraryWithSource`), so the build has needed no offline Metal
  compiler. Everything has been a textured or untextured quad with a colour.
- **Two passes per frame, never nested**: the game's offscreen frame buffer,
  then the drawable, where `present_scaled` has blitted the buffer at the
  largest whole-number scale that fits. The safe-area origin has applied to
  the drawable pass only.
- Each pass has had its **own vertex buffer**. A buffer shared across a
  frame's passes would be overwritten while the GPU still reads it.
- **Source rectangles have followed raylib's convention**: a negative width
  or height has mirrored that axis. A Metal render target has already been
  stored top-down, so a target's texture has ignored the vertical flip raylib
  callers pass for it.
- Up to 4,096 live textures (`TEX_MAX`); exhausting the table has been logged
  loudly rather than returning id 0 silently.
- Point filtering and clamped addressing throughout: the pack has been pixel
  art.

## Text, images and audio

- **Fonts** (`ios/font_ios.c`): the pack has chosen its face, so the atlas has
  been baked at load time with stb_truetype, with metrics that mirror raylib's
  `LoadFontData` so line heights and positions have matched every other
  platform.
- **Images** (`ios/image_ios.c`): stb_image has decoded the pack's PNGs from
  memory.
- **Audio** (`ios/audio_ios.mm`): `AVAudioEngine` with one player node per
  voice. The pack's `.wav` tunes have been decoded once into PCM buffers; the
  two `.ogg` music tracks have been decoded whole with stb_vorbis and looped
  as a single buffer, so there has been no streaming top-up. The session
  category has been Ambient: the game has mixed with other audio and
  respected the ring switch.

## Input

There has been no keyboard or gamepad on iOS; the key and pad queries have
answered "no device", and every screen has worked by touch (`src/touch.c`). A
tap has become a keypress through the injected-key queue in `ios/host_ios.c`,
so each screen has kept its existing key handling. Touch controls have been
sized in physical units (REQ-530), naming the hero has used the in-game
letter grid (REQ-531), and the menus have had no Exit row (REQ-529).

## Building and shipping

```
make mac                                        # the pack tool (the default build links X11)
make ios-sim PACK_TOOL=build/openbounty-mac     # Simulator .app
make ios     PACK_TOOL=build/openbounty-mac     # device .ipa
```

Everything has been compiled by `clang` from the Makefile; there has been no
Xcode project. `make ios` has produced an App Store-shaped bundle -- device
platform keys, the icon compiled by `actool` from a single 1024x1024 PNG, the
toolchain provenance keys App Store Connect checks, and entitlements -- and
signed it when `IOS_SIGN_IDENTITY`, `IOS_PROFILE` and `IOS_TEAM_ID` have been
given. All of it has run on GitHub's macOS runners: the `ios` CI job has built
the Simulator app, booted a Simulator, launched it and captured screenshots,
then built and signed the device `.ipa`; the release has uploaded that `.ipa`
to App Store Connect; `testflight.yml` has uploaded any branch on demand.
`docs/RELEASE-PROCESS.md` and `docs/STORE-SUBMISSION.md` have covered the
pipeline.
