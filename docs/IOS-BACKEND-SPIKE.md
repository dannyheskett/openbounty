# iOS backend spike — the complete raylib surface

Goal: ship **Glory of Rome** on iOS **without raylib** (raylib has no official
iOS support). This inventories every raylib API the shell uses, maps each to
its iOS-native replacement, and sizes the work. `engine/`, `demo/` and
`autoplay/` are untouched — they already build with no raylib at all (see
`engine/headless/raylib_stub.h`, the 368-line shim the headless builds resolve
`raylib.h` to). Only `src/` gets a second backend.

The sibling project `openblocks` has done this once already: `src/gfx.h` with
`gfx_raylib.c` and `ios/gfx_metal.mm` behind it, a `CAMetalLayer` view driven
by `CADisplayLink`, AVAudioEngine for sound, and a hand-assembled `.app`/`.ipa`
built by `clang` from the Makefile with no Xcode project. That structure is the
model; this document is where openbounty differs.

## Scope reduction

iOS is a **touch + landscape** target that ships one pack. Eleven translation
units come out of the build:

    recorder.c  encode_mp4.c  encode_mp4_h264.c  encode_mp4_mux.c
    encode_dialog.c  screenshot.c  shell_gallery.c  pack_select.c
    shell_demo.c  shell_autoplay.c  combat_replay.c

That is the video recorder, the screenshot/gallery paths, the pack picker (the
app ships exactly one pack, as on Android), and the demo/autoplay drivers.
Dropping them removes **13 raylib functions entirely**: `DirectoryExists`,
`DrawRectangleLinesEx`, `DrawTextEx`, `ExportImage`, `ExportImageToMemory`,
`FileExists`, `ImageFlipVertical`, `ImageFormat`, `LoadFontFromMemory`,
`LoadImage`, `LoadImageFromTexture`, `MeasureTextEx`, `MemFree`.

It also means `main.c` needs the same treatment it got for Android: the modes
those files serve are unreachable, so their entry points must compile out
rather than be stubbed one by one.

**What remains: 85 raylib functions, 429 call sites.**

## The surface, by subsystem

Counts are call sites in `src/` with the eleven excluded files removed.

### A. App / window / lifecycle → UIKit + CADisplayLink — 22 functions, 50 calls

`GetScreenWidth`(11) `GetScreenHeight`(9) `CloseWindow`(4) `GetTime`(3)
`PollInputEvents`(3) `WindowShouldClose`(2) `ToggleFullscreen`(2)
`IsWindowFullscreen`(2) `InitWindow` `SetConfigFlags` `SetTargetFPS`
`SetWindowSize` `SetWindowMinSize` `IsWindowMaximized` `GetCurrentMonitor`
`GetMonitorWidth` `GetMonitorHeight` `HideCursor` `SetExitKey`
`SetTraceLogLevel` `GetFrameTime` `WaitTime`

| raylib | iOS | Effort |
|---|---|---|
| `InitWindow` / `CloseWindow` | `UIApplication` + `UIView` backed by `CAMetalLayer` | S |
| `GetScreenWidth/Height` | view `bounds.size` × `contentScaleFactor` (device pixels) | S |
| `WindowShouldClose` | never true; the OS owns the lifetime | trivial |
| fullscreen / monitor / min-size / cursor / exit-key | no-ops on iOS | trivial |
| `GetTime` / `GetFrameTime` / `WaitTime` / `SetTargetFPS` | `CACurrentMediaTime()`, `CADisplayLink` | S |
| `PollInputEvents` | UIKit delivers events; drain the queue the touch handler fills | S |

### B. Input → UIKit touches — 10 functions, 41 calls

`IsGamepadButtonPressed`(19) `GetKeyPressed`(5) `IsGamepadAvailable`(4)
`GetGamepadAxisMovement`(4) `GetCharPressed`(3) `IsKeyPressed`(2) `IsKeyDown`
`GetTouchPointCount` `GetTouchPosition` `GetGamepadButtonPressed`

The keyboard and gamepad calls are already funnelled through `src/input_host.c`
and `src/input.c`; on iOS they answer "no device" and every screen falls back to
the touch layer, which is complete (`src/touch.c`, and Rome's gallery tap check
passes 62/62). Only the two touch functions need real implementations:
`touchesBegan/Moved/Ended` maintaining an active-point list. GameController.
framework can light the gamepad path up later; it is not needed to ship.

**Effort: S.** This is the cheapest subsystem because the work was done for
Android and the web.

### C. Frame + primitives → Metal — 15 functions, 180 calls

`DrawRectangle`(122) `DrawRectangleLines`(20) `ClearBackground`(8)
`EndDrawing`(6) `DrawTriangle`(6) `DrawRectangleRounded`(3)
`DrawRectangleRoundedLines`(3) `BeginDrawing`(2) `BeginScissorMode`(2)
`EndScissorMode`(2) `MeasureText`(2) `BeginMode2D`(1) `EndMode2D`(1)
`DrawCircle`(1) `DrawText`(1)

All of it is 2D: coloured quads, four thin quads for an outline, a triangle
fan, a scissor rect, and a 2D camera transform. **One pipeline, one shader**
(position + colour + optional UV). `DrawText`/`MeasureText` are raylib's
built-in font and are used twice, both in `src/touch.c` chrome — replaceable
with the game's own font or dropped.

**Effort: M** (the pipeline itself), then trivial per primitive.

### D. Textures + render targets → Metal — 10 functions, 117 calls

`UnloadTexture`(56) `DrawTexturePro`(32) `SetTextureFilter`(8)
`UnloadRenderTexture`(7) `LoadTextureFromImage`(4) `LoadRenderTexture`(4)
`BeginTextureMode`(2) `EndTextureMode`(2) `SetTextureWrap`(1)
`DrawTextureRec`(1)

**This is the biggest difference from openblocks**, whose `gfx.h` has no
textures at all. Every tile, sprite, portrait and backdrop is a texture, and
`DrawTexturePro` (src rect → dst rect, with tint) is the workhorse. Needed:
`MTLTexture` creation from decoded RGBA, point filtering and clamp addressing
(the pack is pixel art — `TEXTURE_FILTER_POINT` and `WRAP_CLAMP` are what keep
edges hard), and a textured-quad draw.

The **render target** is the game's whole-frame buffer: `src/present.c` renders
into a `RenderTexture2D` and blits it once, scaled, into the window
(`present_scaled`). On iOS that maps to an offscreen `MTLTexture` with its own
render pass — or the pass can be skipped entirely by drawing straight to the
drawable with a scale transform. Keeping the offscreen buffer is the
conservative choice: it preserves the integer-scale presentation and the
touch-coordinate mapping (`present_window_to_screen`) exactly as they are.

**Effort: M.**

### E. Images (CPU) → stb_image — 5 functions, 9 calls

`UnloadImage`(4) `LoadImageFromMemory`(2) `GenImageColor` `ImageDraw`
`ImageDrawRectangle`

Every pack asset is decoded from memory (`src/assets.c` reads bytes through
`pack_stack_read`, never a path), so this is PNG decode plus two trivial CPU
blits. Vendor `stb_image.h` — raylib uses it internally anyway.

**Effort: S.**

### F. Fonts → stb_truetype — 4 functions, 7 calls

`LoadFontData` `GenImageFontAtlas` `UnloadFontData` `UnloadFont`

openbounty has **two** text systems and both must work:

- `src/bfont.c` — a bitmap font *strip* from the pack, drawn as textured
  quads. Free once textures exist.
- `src/text.c` — a TrueType atlas built at runtime from **the pack's own
  font** (`art/font/PressStart2P-Regular.ttf`, declared in `game.json`), at a
  zoom the layout reads back for its line height.

openblocks could pre-bake an atlas because its face is compiled in. Here the
**pack chooses the face**, so a pre-baked atlas would freeze it and break any
other pack. Vendor `stb_truetype.h` and bake the same atlas at load: same
glyph set, same metrics, so the layout numbers do not move.

**Effort: M**, and the place where a mistake shows up as text that is a pixel
off everywhere.

### G. Audio → AVAudioEngine + stb_vorbis — 17 functions, 22 calls

`InitAudioDevice` `CloseAudioDevice` `IsAudioDeviceReady` `LoadWaveFromMemory`
`LoadSoundFromWave` `UnloadWave` `UnloadSound` `PlaySound` `IsSoundPlaying`
`SetSoundVolume` `LoadMusicStreamFromMemory` `UnloadMusicStream`
`PlayMusicStream` `StopMusicStream` `IsMusicStreamPlaying` `UpdateMusicStream`
`SetMusicVolume`

Two kinds: four short `.wav` tunes (decode once, play as PCM buffers) and two
**streamed `.ogg`** tracks — openworld and combat. openblocks synthesised its
effects and needed no decoder; here the music needs `stb_vorbis` feeding an
`AVAudioPlayerNode`, with `UpdateMusicStream` becoming the buffer top-up.

**Effort: M** — small API, but the streaming loop is real work.

### H. Files → the bundle — 2 functions, 3 calls

`LoadFileData` `UnloadFileData`, both in `src/plat_android.c`. iOS gets the
equivalent `src/plat_ios.c`: read the bundled pack with `NSBundle` +
`NSData`, hand it to `pack_open_mem` (already in the engine, tested), and point
`SavePathSetDirOverride` at `NSDocumentDirectory`.

**Effort: S** — the Android work already carved this seam.

## The loop — the one real architectural decision

openblocks mapped onto `CADisplayLink` directly because `frame_step()` was
already factored out of its main loop. **openbounty has no such function.** It
has **15 blocking loops across five files** — `src/main.c`, `src/combat_loop.c`
(`RunCombat` plus the end-of-combat dwell), `src/startup.c`,
`src/end_cartoon.c`, `src/encode_dialog.c` — each of the shape
`while (!frame_host_should_close()) { ...draw...; frame_host_end_frame(); }`,
and prompts block inside those. The web build only works because **ASYNCIFY**
unwinds the wasm stack at `frame_host_yield()`; iOS has no equivalent.

Two ways out:

1. **Run the game on its own thread** (recommended). The loops stay exactly as
   they are. The game thread records a draw list per frame and blocks on a
   semaphore in `frame_host_end_frame()`; `CADisplayLink`, on the main thread,
   submits the last complete list to Metal and signals the semaphore. UIKit
   touches are pushed into the same queue `src/input_host.c` already drains.
   Cost: a draw-list buffer and a strict rule that only the display link talks
   to `MTLCommandQueue`.
2. **Rewrite every loop into a step machine.** Touches the entire shell,
   changes King's Bounty's code paths, and risks the byte-determinism the
   project holds. Not recommended.

## Proposed structure

```
src/gfx.h            primitives + textures + render target (the seam)
src/gfx_raylib.c     wraps today's calls — desktop, web, Android
src/ob_types.h       Color/Vector2/Rectangle/Texture handles without raylib
src/audio.h          the audio seam (src/audio.c keeps the logic)
src/plat_ios.c       bundle pack + save dir (mirrors src/plat_android.c)

ios/gfx_metal.mm     one textured-quad pipeline, one shader
ios/audio_ios.mm     AVAudioEngine + stb_vorbis
ios/plat_ios.mm      touches, screen size in device pixels, time
ios/ios_main.mm      UIApplication + CAMetalLayer + CADisplayLink + game thread
ios/Info.plist       landscape-only (UISupportedInterfaceOrientations)
third_party/stb/     stb_image.h, stb_truetype.h, stb_vorbis.c
```

Funnel work before any of that: **114 direct raylib draw calls** still sit
outside `src/ui.c`, concentrated in `src/lattice.c` (37), `src/startup.c` (23),
`src/ui.c` (10), `src/combat_render.c`, `src/touch.c`, `src/screens/*`.
Everything else already draws through `ui_*`.

## Effort summary

| Piece | Size | Risk |
|---|---|---|
| `gfx.h` seam + `gfx_raylib.c` (desktop, no behaviour change) | M | low |
| App shell: UIKit view, CADisplayLink, game thread | M | **med** — the loop |
| Touch input | S | low |
| Metal 2D renderer: primitives + textures + offscreen target | M | **med** |
| Fonts: stb_truetype atlas matching `text.c` metrics | M | med |
| Images: stb_image | S | low |
| Audio: AVAudioEngine + stb_vorbis streaming | M | med |
| Bundle pack + save dir | S | low |
| Build / sign / package (`.app`, `.ipa`), CI | M | med |

No single hard part. The loop and the Metal renderer are where this goes wrong
if it goes wrong.

## Spike order

Each step is a checkpoint; **stop and reassess after 2**.

1. **Build shell.** `make ios-sim` produces a Simulator `.app` that opens a
   `CAMetalLayer` view and clears to a colour. Proves the toolchain with no
   Xcode project, on a macOS CI runner.
2. **Renderer.** Implement `gfx_*` in Metal and draw one real screen — the
   title — with the pack's own font. Proves the pipeline, the texture path and
   the atlas together.
3. **Game thread.** Drive the existing loops from the display link; the map
   renders and the game runs.
4. **Touch.** UIKit touches into `input_host`; playable.
5. **Audio.** Tunes, then the streamed music.
6. **Package.** Unsigned `.ipa` for a device farm, signed for TestFlight, both
   from the Makefile, plus the CI jobs alongside the Android ones.

## Progress

The C side is done and is checked on every build.

- `src/gfx.h` + `src/gfx_raylib.c` -- all drawing. `src/ob_types.h` carries the
  types, the colours, and the KEY_* / GAMEPAD_* ids for a build with no raylib.
- `src/frame_host.h` -- the window, the display, timing, event polling.
- `src/input_host.h` -- keys, characters, touch, the pad.
- `src/audio_backend.h` + `src/audio_raylib.c` -- device, one-shots, streams.
  `src/audio.c` keeps every decision (ducking, master volume, mute toggles).
- `src/font_backend.h` + `src/font_raylib.c` -- baking the pack's own face.
  `src/text.c` keeps the metrics policy.
- Desktop-only headers (`screenshot.h`, `encode_dialog.h`, `shell_gallery.h`)
  compile to inline no-ops under `PLATFORM_IOS`, so their call sites stay put.

**`make` runs the iOS purity check** (`build/ios-purity.stamp`): all 59 shell
files the iOS build will compile are type-checked with `-DPLATFORM_IOS` and no
raylib include path at all. It fails the build if a raylib call comes back.

Throughout: King's Bounty's gallery stayed 86/86 byte-identical, Rome's 93/93,
`detcheck.sh` clean, 366 tests green.

**Checkpoints 1 and 2 are done** (PR #38, `macos-15` runner): the app builds
with no Xcode project, installs, launches on a Simulator, and draws. The
runtime-compiled Metal shader works, and the self-test's fills, outlines,
rounded panels with borders, triangle and circle all render correctly with the
safe-area inset respected. Three real bugs came out of that first run, each of
which would have been invisible without it:

- `setVertexBytes` is capped at 4 KB; a frame is far more, and it drew garbage
  rather than failing. Vertices now go through an `MTLBuffer`.
- A vertex has to join the batch decided *before* it is appended: deciding
  after left every batch starting one vertex late.
- The Simulator boots portrait and a plist-only orientation did not hold, so
  the view controller states landscape as well.

The pieces, all first compiled on that runner:

- `ios/gfx_metal.mm` -- the whole of `src/gfx.h` in one pipeline and one
  shader, compiled from source at runtime so the build needs no offline Metal
  compiler. Two passes per frame, never nested: the game's offscreen buffer,
  then the drawable. The safe-area origin applies to the drawable only.
- `ios/ios_main.mm` -- `UIApplication` + `CAMetalLayer` + `CADisplayLink`,
  landscape, one touch contact published in device pixels, and the renderer
  self-test the CI screenshot captures.
- `ios/plat_ios.{h,mm}` -- what UIKit publishes and the game reads: safe-area
  size, touch, foreground state, the frame clock.
- `ios/Info.plist` -- landscape-only, iOS 15, `com.danheskett.gloryofrome`.
- `make ios-sim` / `make ios`, and the `ios` CI job that boots a Simulator,
  installs, launches and uploads a screenshot.

Still to write: `src/plat_ios.c` (bundle pack + save dir), `ios/image_ios.c`
(stb_image behind `gfx_image_*`), `ios/font_ios.c` (stb_truetype behind
`font_backend.h`), `ios/audio_ios.mm`, the game thread, and `gfx_label`'s
atlas.

## What must not change

- `engine/`, `demo/`, `autoplay/` compile untouched — they have no raylib.
- King's Bounty stays byte-identical on desktop: the gallery comparison and
  `tools/detcheck.sh` are the checks, and every step above is additive or
  behind the `gfx.h` seam.
- `make test` (366 tests) stays green at every checkpoint.
