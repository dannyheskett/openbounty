# Glory of Rome — art pipeline

The route that works. Two calls per troop.

`ART-WORKLIST.md` holds the prompt for each of the 116 artworks. This file is
how to run them.

---

## The style

Every figure goes through one custom style so the roster reads as one set:
`user__glory_of_rome_troops_bac676cd`. It appends to every prompt

```
, full length, standing in profile facing right, game sprite, solid magenta background
```

and removes the background itself. Magenta, because the remover takes any part
of the figure that matches the background's tone, and nothing in Roman kit is
magenta.

The prompt is the subject only.

---

## 1. The still

```json
{
  "id": "velites",
  "prompt": "a lean Roman velite skirmisher in a wolfskin headdress over a helmet, a small round parma shield on his left arm, a javelin held upright in his right hand",
  "style": "user__glory_of_rome_troops_bac676cd",
  "width": 96, "height": 96,
  "remove_bg": true, "return_non_bg_removed": true,
  "bypass_prompt_expansion": true,
  "raw_only": true, "figure": true, "target": [96, 96]
}
```

```
python3 tools/rdgen.py run art/jobs/velites.json
```

Describe a neutral stance with the weapon at rest, not the action: "a javelin
held upright in his right hand", not "throwing a javelin". The upright weapon is
the travel the animation spends.

---

## 2. Look at it

Open the contact sheet. Accept it if it is the character you asked for and it
reads at 1:1 over grass.

---

## 3. The animation

```json
{
  "id": "velites_attack",
  "prompt": "the javelin travels from upright beside his head forward and down until his arm is straight out in front at shoulder height, both feet stay planted, the shield stays where it is",
  "style": "rd_advanced_animation__custom_action",
  "width": 96, "height": 96,
  "frames_duration": 4,
  "return_spritesheet": true,
  "input_image_path": "build/art/velites/run01/01_raw.png",
  "input_image_keep_alpha": true,
  "bypass_prompt_expansion": true,
  "raw_only": true, "figure": false, "target": [96, 96]
}
```

- `rd_advanced_animation__custom_action`
- 96x96 — the size the frames come back at, and the size a troop file is
- `frames_duration` — the frame count is per troop, not fixed: the pack's
  `anim` list for the troop is the cycle (1 to 16 frames, `OB_ANIM_FRAMES_MAX`),
  so install however many frames the run returns and declare them. Four was
  the count for every set through 2026-09-05; the API guide gives **six for a
  single action** and eight for a breathing loop, and the Sarmatae swing at
  four ended with an empty hand where six had room for the return. Use six
  for attacks from now on.
- `bypass_prompt_expansion` — **leave expansion on** (`false`) for
  animations; see "Prompt expansion" below. The example above is the
  pre-2026-09-05 form.
- `input_image_keep_alpha: true` — what makes the frames transparent

The motion line names the path and its two endpoints, then what stays still:
"from upright beside his head forward and down until his arm is straight out in
front at shoulder height", then "both feet stay planted, the shield stays where
it is".

---

## 4. The frames

The response is a sheet of frame cells, 2x2 for four frames; `rdgen` derives
the grid from the image and writes `frame_00..03.png`. When the job was padded
to 128 for motion room (`pad_to`), the frames come back 128x128 and go down to
96x96 through the API's k-centroid tool, `/edit/tools/k_centroid_downscale`
(`rdgen.k_centroid`, free, area-weighted), not a local resample and not a
crop. The tool flattens the frame onto white, so the alpha is put back from
the 128 frame's own mask, area-averaged to 96 and thresholded at half. Watch
the animation, then:

```
cp build/art/<id>/run01/frame_0N.png assets/glory-of-rome/art/troops/<name>_0N.png
```

and record both prompts in `ART-WORKLIST.md`.

---

## Other kinds of art

- **A small unit** — generate *and* animate at 64x64, then composite each frame
  into a 96x96 transparent canvas at offset (16, 32). Two thirds height, by
  construction. The Lares went this way. If the 64 animation mangles the
  weapon (the Fauni's branch doubled twice), generate and animate at 96 like
  the men and scale the four frames to 66% together through the k-centroid
  tool, one shared offset, feet on row 88.
- **Screen-shaped art** — the class portraits, the class-select picker, the
  title and the six location backdrops all come from one engine,
  `rd_pro__default`: opaque, no `remove_bg`, `bypass_prompt_expansion`,
  `raw_only`, the prompt from the worklist row and nothing appended. They are
  authored at the design size and the shell scales them by `ui_scale`, an exact
  2x, so they all scale alike. RD Pro caps a side at 256 (see the caps below),
  which is why design x2 is not the rule here: it fits only the portraits
  (96x102 -> 192x204, how they were made); the backdrops are 240x102; the title
  and picker, whose design sizes are wider than 256, were made at 256x164 and
  are stretched to their rectangle.
- **The class-select picker** — additionally passes all four approved portraits
  as `reference_images`, the prompt written as "the same general in the bronze
  cuirass…". The four figures sit one per column, left to right, in manifest
  order.
- **Location backdrops** — `figure: false`, `target [240, 102]`; the job is
  `art/jobs/backdrop_castle.json`, the other five differ only in id, prompt and
  seed.
- **Base terrain** (grass, grass_variant, forest, mountain, desert) —
  `rd_tile__single_tile`, the API's purpose-built seamless tile style (cap 64;
  its craft guide sizes single tiles at 16 to 32), at **48x48**, laid 2x2 by
  `tools/tile2x2.py` into the 96x96 pack tile at native pixel density, so the
  repeat period is 48. The terrain is described plainly, "seen from directly
  above ... the same everywhere". Settled 2026-09-05 after nine runs on the
  earlier route (`rd_plus__low_res` with `tile_x`/`tile_y` and a prompt
  describing a crop of printed wrapping paper): that route holds the wrap but
  the model shades each tile's interior, which repeats as a lattice across a
  field (a rim on grass, a diamond on forest), and no wording, seed or
  prompt-expansion setting removed it. The tile style does not shade the
  interior. Judge every terrain as a 4x4 field at 1:1
  (`build/art/terrain_review.py`), never a single tile zoomed. No
  post-processing of terrain (Dan, 2026-09-05).
  - **Water is the exception**: the installed tile is the earlier low_res
    route (seed 3107), kept because the tile style drew water as a bevelled
    block face with a lit rim on two runs with different wording. It is flat,
    seamless and approved, so it stays.
- **Object tiles** (the per-zone towns, the castle, the four dwellings) — `rd_pro__topdown`,
  96x96, `figure: false`, `remove_bg: true` with the magenta background named in
  the prompt, **no reference image**. Settled on 2026-09-05 after an engine test
  on one prompt across `rd_plus__low_res`, `rd_tile__tile_object`,
  `rd_plus__topdown_asset` and `rd_pro__topdown`; only the last read as a town
  with a facing and no slab. What the runs taught, in order of weight:
  - A facing cue is required. "Seen from a high angle" alone gives an
    isometric diorama on a plinth, every time. "The buildings seen from the
    front and above with their doors facing the viewer" gives the game's view.
  - A reference image (the castle tile) made the model fill the frame edge to
    edge, three runs out of three; dropping it fixed the framing in one. The
    docs say references "re-imagine" the source, so use them for a character
    that must recur, not for palette.
  - Generating smaller (RD Pro goes down to 12px) does not make a margin; the
    model fills whatever canvas it gets. Margin wording is ignored too. The
    seed is the lever for framing.
  - Freestanding is wording: "no wall, fence or gate, only the flat magenta
    background between and below the buildings". A road drawn "from the bottom
    edge" makes the model fill and crop the frame; "a short stub of paved road
    between the middle buildings, the road the only ground drawn" keeps the
    framing about half the time, so budget two seeds per tile with a road.
  - Prompt expansion has no effect on `rd_plus__low_res` (byte-identical
    output either way).
  The ground is not in the art: the renderer draws the terrain tile beneath
  every object tile.
- **Terrain edges** (48 files) — not generated. `tools/tileedges.py`
  composites each from the installed base and grass tiles: the original
  48x34 edge tile under `art/reference/edges/` is read as a shape (each pixel
  is terrain or grass by which original base's colours it is nearest), the
  mask is resized to the pack tile and filled with the new bases, so every
  edge seams with its neighbours by construction. Re-run it whenever a base
  changes; with a tile-set argument it writes a zone's folder.
- **Villain portraits** (`art/villains/<name>_00..07.png`) — villains are not
  sprites: they are opaque head-and-shoulders portraits drawn as faces in the
  contract view, the HUD contract chip and the puzzle grid. Still:
  `rd_pro__default` at 96x96, opaque, the four class portraits as
  `reference_images`, the prompt "a head-and-shoulders portrait, the face
  filling the frame, of ..." with a setting behind the head. Loop:
  `rd_advanced_animation__custom_action` on the untouched still, **eight
  frames**, **prompt expansion left on** (`bypass_prompt_expansion: false`),
  a short tag-form prompt in the engine maker's shape: "snarling face, static
  background, smooth loop". Settled 2026-09-05 on Hannibal, measured as
  pixels changed against frame 0 outside the face: custom action with
  expansion on, 1217 over seven frames and all of it on the helmet brow and
  chin strap; the same engine with expansion off at four frames redrew the
  whole figure (1600 on one frame); the idle style moved the body on every
  frame (9700) with expansion on or off, because it is built for a standing
  figure. Judge a loop by that measurement and the 3x gif, not a single
  frame. No processing of the still or the frames.
- **Prompt expansion** — every job before 2026-09-05 set
  `bypass_prompt_expansion: true`, on the strength of one measurement on a
  low_res still where it changed nothing (and the town test on the same
  style was byte-identical). On the animation engine it is the difference
  between a held background and a redrawn one. Leave it on for animations.
  The installed troop loops were made with it off and stay as approved; any
  re-run or new troop tests expansion on first.
- **Inventory icons** (`art/ui/inventory_artifact_*`, `inventory_zone_*`) —
  opaque framed cards like the original pack's, drawn in the inventory belt
  and the puzzle grid at the tile size: `rd_pro__default`, 96x96, no
  references, the object "painted as a small game inventory icon ... inside a
  thin gold frame", and **no writing, lettering, banner or ribbon** named in
  the prompt, because the model otherwise invents captions ("COASTAL PALMS")
  and rune-like inscriptions. The four zone icons are declared in
  `sprites.ui.view_icons_extra` in zone order; without that key the map
  grid of the inventory draws nothing.
- **The publisher splash** (`art/ui/splash_logo.png`, 320x84, transparent) —
  composed, not generated whole, because generated lettering garbles. The
  words are rendered locally from C059 Bold at 1-bit, white with the old
  logo's red shading offset below and right; only the 44x44 emblem is
  generated (`rd_pro__default`, `remove_bg`, magenta named in the prompt) and
  pasted where the old globe sat; coins and sparkles are drawn. The
  composition is `tools/splashlogo.py`. The emblem is a Mediterranean globe in
  a laurel wreath; an earlier eagle emblem was dropped because it read as a
  Reich eagle.
- **Making room for a motion** — when a still already holds its weapon out
  (the Coloni fork ended 6 px from the edge), or a finished set is too big for
  its cell (the Lupi), scale it to 80% through the k-centroid tool (black
  flatten, alpha from coverage), then place it with the feet on row 88, the
  ground line the men stand on, using one offset for every frame of a set so
  the loop does not jitter. Animate the scaled still at 96 with no padding;
  the padded 128 route shrinks a figure to three quarters (Manes, Dracones).
- **Recolouring** — when a set reads fine but its colours vanish against the
  grass (the Antaei's moss and soil), recolour the approved frames locally in
  HSV rather than regenerating: every opaque pixel except the pale highlights
  takes the new hue, and the pose and motion stay exactly as approved.

---

## Engines and their size caps

One engine per kind of art, so each kind reads as one set. The caps are per
side, per style, and they come from the API's own catalogue:

```
curl -H "X-RD-Token: $(cat ~/.config/retrodiffusion/token)" \
  "https://api.retrodiffusion.ai/v2/styles/selector?model=rd_pro"
```

| kind | engine | authored at | cap (2026-09-04) |
|---|---|---|---|
| troop stills | `user__glory_of_rome_troops_bac676cd` | 96x96 | RD Pro template; not listed by the selector |
| troop animation | `rd_advanced_animation__custom_action`, six frames, expansion on | 96 or 128 | 32 to 256 |
| villain still | `rd_pro__default`, class portraits as references | 96x96 opaque | 12 to 256 |
| villain loop | `rd_advanced_animation__custom_action`, eight frames, expansion on | 96x96 | 32 to 256 |
| terrain tiles | `rd_tile__single_tile` (water: `rd_plus__low_res`) | 48x48 laid 2x2 | 16 to 64 |
| object tiles (towns) | `rd_pro__topdown` | 96x96 | 12 to 256 |
| screen-shaped art | `rd_pro__default` | design size (portraits x2) | 12 to 256 |

Check the selector before promising a size. The cost check does not validate
size: `rdgen cost` (and the `check_cost` call inside `run`) accepts and prices
an oversize request, and the task then fails at inference with
`inference_failed`, "Unable to run inference.", and no charge. That is what
480x204 and 480x208 on `rd_pro__default` did on 2026-09-04. Only two styles
reach 512 (`rd_plus__environment` and the internal `rd_plus__no_style`); they
are a different engine and are not used for anything, so no piece changes look
against its neighbours.

---

## Running rdgen

```
python3 tools/rdgen.py run       art/jobs/<id>.json
python3 tools/rdgen.py reprocess art/jobs/<id>.json     # re-cut and re-check, no new call
```

- Every call goes through rdgen, so the request is saved beside the result.
- Output lands in `build/art/<id>/runNN/`, a new directory per run; nothing is
  overwritten.
- The task id is written to disk before polling.
- `raw_only: true` delivers the image exactly as returned.
- Token from `~/.config/retrodiffusion/token`. No environment variables.
- Nothing writes into `assets/`; approved finals are copied by hand.
- Review on a page: `art.html` at the repo root shows the whole pack and
  refreshes from disk.
