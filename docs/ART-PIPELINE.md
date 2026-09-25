# Glory of Rome — art pipeline

Every prompt behind the pack, with its engine and settings, has been collected
in **`docs/ROME-ART.md`**, generated from `art/jobs/*.json` by
`tools/romeart.py prompts`. This file has been the *routes* (which engine,
which settings, and why); that file has been the *record*.

A troop has been two calls: the still, then its animation. `ROME-ART.md` has
held the prompt for each artwork; this file has been how to run them.

---

## The style

Every figure has gone through one custom style so the roster reads as one
set: `user__glory_of_rome_troops_bac676cd`. It has appended to every prompt

```
, full length, standing in profile facing right, game sprite, solid magenta background
```

and removed the background itself. Magenta, because the remover has taken any
part of the figure that matches the background's tone, and nothing in Roman
kit has been magenta.

The prompt has been the subject only.

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
held upright in his right hand", not "throwing a javelin". The upright weapon
has been the travel the animation spends.

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
  "frames_duration": 6,
  "return_spritesheet": true,
  "input_image_path": "build/art/velites/run01/01_raw.png",
  "input_image_keep_alpha": true,
  "bypass_prompt_expansion": false,
  "raw_only": true, "figure": false, "target": [96, 96]
}
```

- `rd_advanced_animation__custom_action`
- 96x96 — the size the frames have come back at, and the size a troop file
  has been
- `frames_duration` — the frame count has been per troop, not fixed: the
  pack's `anim` list for the troop has been the cycle (the list's length,
  with no ceiling), so install however many frames the run returns and
  declare them. The API guide has given **six for a single action** and eight
  for a breathing loop; an attack at four frames has ended with an empty hand
  where six has had room for the return. Use six for attacks. The installed
  `art/jobs/velites_attack.json` has carried four frames and expansion off;
  the recipe above is the settled one.
- `bypass_prompt_expansion` — **leave expansion on** (`false`) for
  animations; see "Prompt expansion" below.
- `input_image_keep_alpha: true` — what has made the frames transparent

The motion line has named the path and its two endpoints, then what stays
still: "from upright beside his head forward and down until his arm is
straight out in front at shoulder height", then "both feet stay planted, the
shield stays where it is".

---

## 4. The frames

The response has been a sheet of frame cells; `rdgen` has derived the grid
from the image and written `frame_00.png` onward. When the job is padded to
128 for motion room (`pad_to`), the frames have come back 128x128 and gone
down to 96x96 through the API's k-centroid tool,
`/edit/tools/k_centroid_downscale` (`rdgen.k_centroid`, free, area-weighted),
not a local resample and not a crop. The tool has flattened the frame onto
white, so the alpha has been put back from the 128 frame's own mask,
area-averaged to 96 and thresholded at half. Watch the animation, then:

```
cp build/art/<id>/run01/frame_0N.png assets/glory-of-rome/art/troops/<name>_0N.png
```

and keep both job files under `art/jobs/`, which `ROME-ART.md` has been
generated from.

---

## Other kinds of art

- **A small unit** — generate *and* animate at 64x64, then composite each
  frame into a 96x96 transparent canvas at offset (16, 32). Two thirds
  height, by construction. If the 64 animation mangles the weapon (a branch
  drawn double), generate and animate at 96 like the men and scale the frames
  to 66% together through the k-centroid tool, one shared offset, feet on
  row 88.
- **Screen-shaped art** — the class portraits, the class-select picker, the
  title and the location backdrops have all come from one engine,
  `rd_pro__default`: opaque, no `remove_bg`, `bypass_prompt_expansion`,
  `raw_only`, the prompt from the worklist row and nothing appended. RD Pro
  has capped a side at 256 (see the caps below): the portraits have been
  192x204 (design x2), the backdrops 240x102 (design size), and the title and
  picker, whose design sizes are wider than 256, 256x164. The shell has drawn
  each at the largest whole multiple that fits its slot (ART-SPEC §1).
- **The class-select picker** — has additionally passed all four approved
  portraits as `reference_images`, the prompt written as "the same general in
  the bronze cuirass…". The four figures have sat one per column, left to
  right, in manifest order.
- **Location backdrops** — `figure: false`, `target [240, 102]`; the job has
  been `art/jobs/backdrop_castle.json`, and the others have differed only in
  id, prompt and seed.
- **Base terrain** (grass, grass_variant, forest, mountain, desert, water) —
  `rd_tile__single_tile`, the API's purpose-built seamless tile style (cap 64;
  its craft guide sizes single tiles at 16 to 32), at **48x48**, laid 2x2 by
  `tools/romeart.py tile2x2` into the 96x96 pack tile at native pixel density,
  so the repeat period has been 48. The terrain has been described plainly,
  "seen from directly above ... the same everywhere". Not `rd_plus__low_res`
  with `tile_x`/`tile_y`: it has held the wrap but shaded each tile's
  interior, which has repeated as a lattice across a field (a rim on grass, a
  diamond on forest), and no wording, seed or prompt-expansion setting has
  removed it. The tile style has not shaded the interior. Judge every terrain
  as a 4x4 field at 1:1, never a single tile zoomed. No post-processing of
  terrain.
  - **Water** has been described flat, with no waves or bands: asked for
    waves, the tile style has drawn a block face with a lit top edge.
- **Object tiles** (the per-zone towns, the castle, the four dwellings) —
  `rd_pro__topdown`, 96x96, `figure: false`, `remove_bg: true` with the
  magenta background named in the prompt, **no reference image**. Of
  `rd_plus__low_res`, `rd_tile__tile_object`, `rd_plus__topdown_asset` and
  `rd_pro__topdown` on one prompt, only the last has read as a town with a
  facing and no slab. The rules, in order of weight:
  - A facing cue has been required. "Seen from a high angle" alone has given
    an isometric diorama on a plinth, every time. "The buildings seen from
    the front and above with their doors facing the viewer" has given the
    game's view.
  - A reference image has made the model fill the frame edge to edge. The API
    docs have said references "re-imagine" the source, so use them for a
    character that must recur, not for palette.
  - Generating smaller (RD Pro goes down to 12px) has not made a margin; the
    model has filled whatever canvas it gets. Margin wording has been ignored
    too. The seed has been the lever for framing.
  - Freestanding has been wording: "no wall, fence or gate, only the flat
    magenta background between and below the buildings". A road drawn "from
    the bottom edge" has made the model fill and crop the frame; "a short
    stub of paved road between the middle buildings, the road the only ground
    drawn" has kept the framing about half the time, so budget two seeds per
    tile with a road.
  - Prompt expansion has had no effect on `rd_plus__low_res` (byte-identical
    output either way).
  The ground has not been in the art: the renderer has drawn the terrain tile
  beneath every object tile.
- **Bridges** (`bridge_h`, `bridge_v`) — a road, as in the original pack: an
  opaque square of grey stone paving with a lighter kerb along the two edges
  the road does not cross, so tiles stack end to end. Object engine
  (`rd_pro__topdown`), no background removal, no water in the picture: the
  original tile has had none, and a transparent deck over water has not come
  back usable.
- **Terrain edges** — not generated. `tools/romeart.py edges` has
  composited each from the installed base and grass tiles: the original
  48x34 edge tile under `art/reference/edges/` has been read as a shape (each
  pixel is terrain or grass by which original base's colours it is nearest),
  the mask resized to the pack tile and filled with the new bases, so every
  edge has seamed with its neighbours by construction. Re-run it whenever a
  base changes; with a tile-set argument it has written a zone's folder.
- **Villain portraits** (`art/villains/<name>_00..07.png`) — villains have
  not been sprites: they have been opaque head-and-shoulders portraits drawn
  as faces in the contract view, the HUD contract chip and the puzzle grid.
  Still: `rd_pro__default` at **128x128**, opaque, no reference images, the
  prompt "a head-and-shoulders portrait, the face filling the frame, of ..."
  with a setting behind the head. At 96 and at 104 the engine has painted a
  frame round the picture on most seeds regardless of the prompt, with or
  without references and with "no frame, no border" in the prompt; at 128 it
  has not. Measure with a 1 to 8 pixel edge-ring check, not a 6 pixel strip,
  or thin frames pass. Loop: `rd_advanced_animation__custom_action` on the
  untouched 128 still at 128, **eight frames**, **prompt expansion left on**
  (`bypass_prompt_expansion: false`), a short tag-form prompt in the engine
  maker's shape: "snarling face, static background, smooth loop". Measured as
  pixels changed against frame 0 outside the face, custom action with
  expansion on has moved only the face and its edges (about 1200 pixels over
  seven frames); with expansion off it has redrawn the whole figure (1600 on
  one frame); the idle style has moved the body on every frame (9700),
  because it has been built for a standing figure. Judge a loop by that
  measurement and the 3x gif, not a single frame. The only processing has
  been the last step, for villain portraits only: each returned 128 frame has
  been centre-cropped to 96 with `tools/romeart.py crop`. Crop after the
  loop, never before, so the motion is made on the same picture the crop is
  taken from.
- **Prompt expansion** — on a `rd_plus__low_res` still it has changed nothing
  (the output has been byte-identical either way). On the animation engine it
  has been the difference between a held background and a redrawn one. Leave
  it on for animations. Some installed troop loops have been made with it
  off; test expansion on first for a re-run or a new troop.
- **Inventory icons** (`art/ui/inventory_artifact_*`, `inventory_zone_*`) —
  opaque cards **with no frame in the art** (none has been drawn round them
  either, see "Frames" below), drawn in the inventory belt and the puzzle grid at the
  tile size: `rd_pro__default`, 96x96, no references, the object "painted as
  a small game inventory icon ... inside a thin gold frame", and **no
  writing, lettering, banner or ribbon** named in the prompt, because the
  model has otherwise invented captions ("COASTAL PALMS") and rune-like
  inscriptions. The four zone icons have been declared in
  `sprites.ui.view_icons_extra` in zone order; without that key the map grid
  of the inventory has drawn nothing.
- **HUD panels** (`art/ui/hud_*`) — the inventory icon route for the seven
  stills (opaque cards, no frame in the art, `rd_pro__default`, no
  lettering); the siege and magic cycles have been the villain loop route on
  the still (custom action, eight frames for the siege and four for the
  magic, expansion on, "static background, smooth loop"). The sidebar has cycled a loop at two frames a
  second, so the magic loop has been a colour change (gold to violet), not a
  flicker, or the change would be invisible at that rate.
- **Frames** — no generated piece has carried a painted frame or border. The
  model has drawn a different frame every run (gold, thin, missing), so no
  piece has had one: a modern screen has drawn no panel frame either (its
  columns are joined by the lattice), and `sprites.ui.panel_frame` has been
  read by legacy screens alone. Prompts for those pieces have said "filling the whole
  picture edge to edge, no frame, no border". The one allowed edge treatment
  has been a villain portrait's flat colour bar, and only when it is
  identical on all eight frames of that villain.
- **Combat set** (`art/combat/*`) — not generated. `tools/siegewalls.py` has
  remade the original 48x34 pieces at 96: each original pixel classified
  into a material, the map scaled to the cell, and every material re-rendered
  at pixel scale (three-tone brick courses over a grout, two-tone merlons over
  the black shadow band, a lighter top face, the moat as a smooth shape with a
  clean dark bank, rubble scattered at the breach), so layout and features
  have matched the original exactly. It has also drawn the top-down back wall
  band the shell places above the siege board (`sprites.ui.siege_back_wall`,
  `_left`, `_right`), with the moat turning the corners on a curve and the
  wall bands mitred. The API has not been able to make generated wall pieces
  that join, and a whole-board picture has been too coarse under the 256 cap.
  Rome has drawn its siege from the grid below instead, so it has shipped
  none of these pieces.
- **Siege grid** (`art/combat/siege/cell_<x>_<y>.png`, 36 cells at 32) —
  the whole siege board plus its back band as one picture, sliced into cells
  (`sprites.ui.siege_grid`, REQ-165c). Route: `rd_plus__topdown_map` (the
  only style that has drawn a true overhead plan; `rd_plus__environment` has
  composed a perspective scene every time) at 384, first from a prompt to get
  the castle, then **img2img** on the 6x6 board composed from the sliced
  pieces (`tools/siegeslice.py` recipe mode, the band included, k-centroid to
  384x384, `strength` 0.55) so the layout has been fixed by the source and the
  engine has repainted one continuous field over it
  (`art/jobs/siege_scene_grass_a.json`, returned at 192). Then
  `tools/siegeslice.py --grid` has written the 36 cells untouched at 32; the
  shell has scaled each to the 96 cell. Why not pieces: a per-code piece
  repeats in every cell of its code, so a gatehouse, two different broken
  ends and a moat under the bottom wall only have not been drawable that way.
- **Field grid** (`art/combat/field/<zone>_<x>_<y>.png`, 30 cells at 96) —
  the ground of an open fight, one picture per continent (a zone's
  `field_grid`, REQ-165e). Italia's has come from a supplied 1254 x 1254
  picture kept at `art/fields/italia.png`, a light meadow painted on white:
  `tools/siegeslice.py --field` has taken the largest 6:5 rectangle of
  content centred in it (726 x 605, the white kept out), scaled it to
  576 x 480 with Lanczos and cut the thirty cells. The ground has been kept
  lighter and plainer than the troops, which have to read against it. The
  other three continents have no field picture yet and draw the hero's map
  tile.
- **The title screen** (`art/ui/splash_title.png`, 256x164) — the eagle has
  been generated (screen route, no border); the words have been drawn by
  `tools/splashtitle.py` from C059 Bold, gold with dark shading, title above
  the eagle and subtitle across the pole, for the same reason as the
  publisher splash: generated lettering has garbled.
- **The publisher splash** (`art/ui/splash_logo.png`, 320x84, transparent) —
  composed, not generated whole, because generated lettering has garbled. The
  words have been rendered locally from C059 Bold at 1-bit, white with the
  original logo's red shading offset below and right; only the 44x44 emblem
  has been generated (`rd_pro__default`, `remove_bg`, magenta named in the
  prompt) and pasted where the original's globe sits; coins and sparkles have
  been drawn. The composition has been `tools/splashlogo.py`. The emblem has
  been a Mediterranean globe in a laurel wreath, not an eagle, which reads as
  a Reich eagle.
- **An archer** (Sagittarii) — the still has had to name the string: Silvani's
  "with the string slack and no arrow on it", or the bow has come back a bare
  arc and no animation has been able to draw a string the still does not
  have. Leave out "standing square": it has turned the chest to the viewer,
  and a front-on figure has not been able to bring the drawing hand to the
  cheek. Name the kit's colours, or a new seed has invented new ones (gold
  scale has come back as grey mail). No reference images: on a still they
  have copied the reference's pose, and `custom_action` has not taken them.
  The animation has been the Sarmatae job -- six frames, expansion on, a tag
  prompt -- with only the still, seed and tag different. Its frame 1 has been
  the idle move; a separate idle call has barely moved.
- **Making room for a motion** — when a still already holds its weapon out
  near the edge, or a finished set is too big for its cell, scale it to 80%
  through the k-centroid tool (black flatten, alpha from coverage), then place
  it with the feet on row 88, the ground line the men stand on, using one
  offset for every frame of a set so the loop does not jitter. Animate the
  scaled still at 96 with no padding; the padded 128 route has shrunk a
  figure to three quarters.
- **Recolouring** — when a set reads fine but its colours vanish against the
  grass, recolour the approved frames locally in HSV rather than
  regenerating: every opaque pixel except the pale highlights takes the new
  hue, and the pose and motion stay exactly as approved.

---

## Engines and their size caps

One engine per kind of art, so each kind has read as one set. The caps have
been per side, per style, and they have come from the API's own catalogue:

```
curl -H "X-RD-Token: $(cat ~/.config/retrodiffusion/token)" \
  "https://api.retrodiffusion.ai/v2/styles/selector?model=rd_pro"
```

| kind | engine | authored at | cap |
|---|---|---|---|
| troop stills | `user__glory_of_rome_troops_bac676cd` | 96x96 | RD Pro template; not listed by the selector |
| troop animation | `rd_advanced_animation__custom_action`, six frames, expansion on | 96 or 128 | 32 to 256 |
| villain still | `rd_pro__default`, no references | 128x128 opaque, cropped to 96 after the loop | 12 to 256 |
| villain loop | `rd_advanced_animation__custom_action`, eight frames, expansion on | 128x128 | 32 to 256 |
| terrain tiles | `rd_tile__single_tile` | 48x48 laid 2x2 | 16 to 64 |
| object tiles (towns) | `rd_pro__topdown` | 96x96 | 12 to 256 |
| screen-shaped art | `rd_pro__default` | design size (portraits x2) | 12 to 256 |

Check the selector before promising a size. The cost check has not validated
size: `rdgen cost` (and the `check_cost` call inside `run`) has accepted and
priced an oversize request, and the task has then failed at inference with
`inference_failed`, "Unable to run inference.", and no charge (480x204 on
`rd_pro__default`, for one). Only two styles have reached 512
(`rd_plus__environment` and the internal `rd_plus__no_style`); they have been
a different engine and have not been used for anything, so no piece has
changed look against its neighbours.

---

## Running rdgen

```
python3 tools/rdgen.py run       art/jobs/<id>.json
python3 tools/rdgen.py reprocess art/jobs/<id>.json     # re-cut and re-check, no new call
```

- Every call has gone through rdgen, so the request has been saved beside the
  result.
- Output has landed in `build/art/<id>/runNN/`, a new directory per run;
  nothing has been overwritten.
- The task id has been written to disk before polling.
- `raw_only: true` has delivered the image exactly as returned.
- Token from `~/.config/retrodiffusion/token`. No environment variables.
- Nothing has written into `assets/`; approved finals have been copied by
  hand.
- Review on a page: `art.html` at the repo root has shown the whole pack and
  refreshed from disk.

---

## Terrain sets: the other API

Retro Diffusion has made every figure, object and screen in the pack. It has
not made the **terrain** that one surface fades into another over. That has
come from PixelLab, and the two have been separate routes with separate
tokens and separate drivers. One engine per kind of art has still held: RD
has owned sprites and screens, PixelLab has owned terrain sets.

```
python3 tools/pltileset.py  build/art/<id> art/jobs/<id>.json   # create-tileset
python3 tools/pltilespro.py ...                                 # Tiles Pro sets
```

- Token from `~/.config/pixellab/token`. No environment variables.
- A `create-tileset` job has named a **lower** terrain and an **upper** one
  and the transition between them, and returned 16 tiles: the two plain
  surfaces and every corner combination. `tile_size` has been 16 or 32.
- `lower_base_tile_id` has chained the set to a terrain some earlier set has
  already produced, so two sets have shared one grass instead of each
  inventing its own. The terrain ids have been printed on every run;
  `art/jobs/t32_cobble_203.json` has chained to `d1de924b`.
- **The seed has not reproduced a set.** Re-running a job unchanged has
  returned different pixels, down to a different brown with no colour in
  common. So a set in the pack has not been rebuildable from its job file:
  every job file has carried a `_note` saying what it produced, and the
  `_note` has been the only record there is.

### Roads

Roads have not been a terrain set the game loads. `tools/romeart.py sweep`
has **swept** the set into the 24 road pieces the pack ships:

```
python3 tools/romeart.py sweep <set-dir> <out-dir> --sweep [--rim N] [--rim-shade F]
```

Every piece has been a signed-distance shape -- a straight band, a true
quarter circle, a 45 degree diagonal, or an end that tapers away -- filled
with the set's plain **upper** tile and left as the pack's own `grass.png`
outside, with a periodic value noise on the boundary so the edge is ragged but
continuous across a tile line. Two consequences worth knowing before writing
a prompt:

- Only the plain upper tile has reached the game. The set's transition tiles,
  and any kerb or edging the prompt asked for, have been discarded. A border
  along the road has had to come from `--rim` / `--rim-shade`, which paint it
  after the fact.
- Every straight exit has been the same 32 px band and every diagonal the
  same corner triangle, so any piece has joined any other. The sweep has
  checked that contract on every run and printed how many sides carry an
  unexpected pattern, and that count has had to be `0`.
