# The Glory of Rome — art pipeline

Every prompt behind the pack, with its engine and settings, has been collected
in **`docs/ROME-ART.md`**, generated from `art/jobs/**/*.json` by
`tools/romeart.py prompts`. This file has been the *routes* (which engine,
which settings, and why); that file has been the *record*.

Every step has run through one script, `tools/romeart.py` (#143, #226):
generation, compositing, the maps, review and the record;
`romeart.py <command> --help` has documented each command. Its one paid
command, `rdgen run`, has been the only one to reach the network for a
charge, and it has posted only when given `--run`. Every shipped art file has
had a recorded way to be made -- a job, a recipe or a kept source -- and
`romeart.py provenance check --rebuild` has held the pack to it (see
[The record](#the-record)).

A troop has been two calls: the still, then its animation. `ROME-ART.md` has
held the prompt for each artwork; this file has been how to run them.

---

## The style

Every figure has gone through one custom style so the roster has read as one
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
python3 tools/romeart.py rdgen run art/jobs/troops/velites_still.json --run
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
  `art/jobs/troops/velites.json` has carried four frames and expansion off;
  the recipe above has been the settled one.
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
`/edit/tools/k_centroid_downscale` (`romeart.py rdgen`'s `k_centroid`, free, area-weighted),
not a local resample and not a crop. The tool has flattened the frame onto
white, so the alpha has been put back from the 128 frame's own mask,
area-averaged to 96 and thresholded at half. Watch the animation
(`romeart.py loopreview <run>`, or `romeart.py review <out> <runs...>` for a
page of several), then install the frames and the portrait:

```
python3 tools/romeart.py troop install <id> build/art/troops/<id>/runNN [build/art/troops/<id>_portrait/runNN]
```

It has written `art/troops/<id>_NN.png` and the 96 px `art/troops/<id>_portrait.png`, pointed the
troop's `sprite`, `portrait` and `anim` at them in `game.json`, and named
those files in the jobs' `"pack"` lists. Keep the job files under
`art/jobs/`, which `ROME-ART.md` has been generated from.

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
  been `art/jobs/ui/backdrop_castle.json`, and the others have differed only in
  id, prompt and seed.
- **Base terrain** (grass, grass_v1, forest, desert, water; not the
  mountain, see below) —
  `rd_tile__single_tile`, the API's purpose-built seamless tile style (cap 64;
  its craft guide has sized single tiles at 16 to 32), at **48x48**, laid 2x2 by
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
  - **Mountain, Italia** — not a texture, since a 48 px tile interior has
    read as rounded domes with a dark split (#67). Italia's mountain has been
    built the way the other continents' have: eight PixelLab rock sprites
    (`art/jobs/primitives/italia_rocks.json`, made before PixelLab was retired)
    composed by the lattice into the interior and the 19 edges with a
    searched slot arrangement (`romeart.py slots`,
    `art/primitives/italia/rock_slots.json`), and the river bands carried
    onto the new interior (`romeart.py rebank`). The layout is committed as
    `art/layouts/mountain96.json`, so `romeart.py compose` rebuilds the
    shipped tiles; all recorded in `art/primitives/italia/BUILD.md`.
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
  - Generating smaller (RD Pro has gone down to 12px) has not made a margin; the
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
- **Bridges** (`bridge_ew`, `bridge_ns`) — a road, as in the original pack: an
  opaque square of grey stone paving with a lighter kerb along the two edges
  the road does not cross, so tiles have stacked end to end. Object engine
  (`rd_pro__topdown`), no background removal, no water in the picture: the
  original tile has had none, and a transparent deck over water has not come
  back usable.
- **River bridges** (`bridge_river_ew`, `bridge_river_ns`) — not generated.
  `tools/romeart.py bridge <tiles-dir> <out>` has built them from a set's
  installed `road_*`, `river_*` and `grass` tiles: the road's own pixels laid
  across the river piece, so the deck has joined the road by construction; over
  the water a straight deck between two 5 px parapets (the road's stone mixed
  40/60 with pale travertine, a dark outer line, a joint every 8 px); and the
  deck's shadow on the water, 3 px at 0.6. `zone` has built them for every
  province.
- **Terrain edges** — not generated. `tools/romeart.py edges` has
  composited each from the installed base and grass tiles: the original
  48x34 edge tile under `art/reference/edges/` has been read as a shape (each
  pixel has been terrain or grass by which original base's colours it is nearest),
  the mask resized to the pack tile and filled with the new bases, so every
  edge has seamed with its neighbours by construction. Re-run it whenever a
  base changes; with a tile-set argument it has written a zone's folder.
- **Mountain edges end raggedly** (#63) — `romeart.py lattice --ragged S,A,B`:
  on a mountain edge piece the rocks within A or B px of an open side
  (alternately) are taken out and the middle edge crag left out, so a range
  ends in an uneven outline instead of a straight line; strips and spits keep
  their rows, straddlers keep their places (seamcheck 0), the plain tile is
  unchanged. Forest edges are left as they were: thinned the same way, a wood
  came out crenellated.
- **Mountain edges set back and shadowed** (#63, plan step 3) — `--ragged
  S,A,B,T,U`: the west and east edge crags set back T px (the top one) and U px
  (the bottom one) from the open line, so a range's side steps in and out
  (zone sets 12 and 28, Italia 0 and 28); and `compose`'s layout key
  `"shadow": [dx, dy, alpha]` (`MOUNTAIN_SHADOW`, 2, 3, 0.35) lays a soft
  contact shadow under an edge piece's sprites, faded to nothing within 4 px
  of every tile line so it never makes a seam, and never on the plain tile, so
  what is built from it still matches. `zone` builds both, and the mountain-
  and forest-on-sand pieces (`<terrain>_sand_edge_NN`) as the same lattice over
  the set's desert.
- **Irrigated fields** (#63, plan step 5) — Africa's and Oriens' farmland
  is a green irrigated field: `art/jobs/primitives/africa_fields_irrigated.json`
  (`rd_tile__single_tile`, 48 px, run 2, doubled to 96), kept as
  `art/primitives/africa/pieces/fields_wheat.png` and taken by both sets'
  `zone` as their `fields_wheat`, so the map's `w` code draws it with no new
  tile code; its soft edges built with `romeart.py edges <pack> <set> --as
  fields_wheat=desert`. Oriens' own job read as dark grass on its bright
  grass and was not installed.
- **Italia's forest from sprites** (#63) — eight Retro Diffusion trees
  (`art/jobs/primitives/italia_tree_*.json`), colour-matched to the old forest by hand
  (`romeart.py colormatch` has been that step since) and kept in
  `art/primitives/italia/trees`; the forest is a lattice of one of them like
  the other sets', its layout committed as `art/layouts/forest96_italia.json`
  (`art/primitives/italia/BUILD.md`).
- **Inner-corner fills** (#63) — `romeart.py fills`: where a grass or sand
  cell has a wood or range on two adjacent sides, the shell (modern) and
  `romeart.py map render --tiles` have drawn `<terrain>[_sand]_fill_<ne|nw|se|sw>` into the
  corner: the set's own lattice sprites continued into the cell, on a 288 px
  canvas with the cell in the middle, so a concave corner has rounded off and
  a staircase has read as a slope. Italia's forest, whose sprites are not
  kept, has taken its island piece's clump.
- **Interior variants** (#63) — `romeart.py interiors`: `forest_v1/v2` and
  `mountain_v1/v2` per set, the plain tile's straddlers kept and its inner
  sprites flipped or nudged, listed as the code's `variants` so a mass has
  stopped repeating one arrangement.
- **Mountain side variants** (#63) — `romeart.py edgevars`:
  `mountain_edge_e/w_v1/_v2` per set, listed as those codes' `variants`, so
  a long west or east side of a range stops repeating one tile: as the
  interiors, every rock straddling the tile's lines kept, the rocks inside it
  flipped and nudged and one in three near the open side left out.
- **Aprons** (#63) — `romeart.py aprons`: a plain grass or sand cell beside a
  wood or range has drawn, for that side, one of three
  `<forest|mountain>_apron_<n|e|s|w>_<1..3>` or none (picked per cell by
  `tilevar_pick` with seed `0xA960` + side, five ways, 1-3 drawn), so a
  straight side has stopped reading as a cut-out line: one or two of the
  set's own whole sprites (rocks not drawn to their frame edge; only the trees
  the plain wood is made of) at full size, straddling the shared line, most of
  each on the grass. Same 288 px canvas as the fills, drawn before them; every
  object, set piece and landmark has been drawn again on top so none is
  hidden. Roads, rivers, fields and pieces have drawn none. Thinning the edge
  pieces' own west and east sides could not do it: their outer column is
  rocks straddling the tile lines, which the neighbours draw too.
- **Small detail** (#63) — `romeart.py details`: `detail_v1..v4` per set, a
  bush, two bushes, a stone, two stones (the set's own whole sprites at about
  half size, only the trees its woods are made of; Italia's bush from its
  island clump), drawn by the shell and
  `map render --tiles` on about one plain grass or sand cell in twelve with no wood,
  range or sea beside it (`tilevar_pick` seed `0xD7A1`, 48 ways, 1-4 drawn).
  Cosmetic: the cell stays walkable grass.
- **Settlement set pieces** (#63) — `art/jobs/primitives/dock_deck.json and art/jobs/objects/piece_{farmstead,ruin,shrine,well}.json`
  (`rd_pro__topdown`, 96 px): a farmhouse, a ruin, a shrine and a well stand
  on grass as solid tiles (`blocks_foot`, map chars `n u h e`); the jetty
  (`j`) is sea off a straight grass shore, `dock_<n|e|s|w>` drawn over that
  side's water edge piece. `romeart.py dock` has built the four from the
  generated deck kept as `art/primitives/pieces/dock_deck.png`: keyed off its
  magenta (`romeart.py key`: red and blue both over green by 80), cropped to
  its ink, centred along the shore and set 6 px from the land side.
  Placed round towns and castles and along roads, each kept only where the
  build, `map lint` and the reach baseline all pass.
- **Sand shores** (`water_sand_*`, #63) — not generated. Each zone
  set's matching water edge piece has been read pixel by pixel as sea or shore (nearest
  to the set's water or grass colours), and the shore part filled with the
  set's desert, so a sea whose coast is all sand has drawn a sand shore line
  instead of a grass one (`romeart.py shore`, every water piece but the four spits, the master
  set and every set with desert; `zone` runs it). `tools/romeart.py map build` has picked them; sand
  meeting the sea has kept its own ground to the coast, and forest and rock
  their grass fringe.
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
  columns have been joined by the lattice), and `sprites.ui.panel_frame` has been
  read by legacy screens alone. Prompts for those pieces have said "filling the whole
  picture edge to edge, no frame, no border". The one allowed edge treatment
  has been a villain portrait's flat colour bar, and only when it is
  identical on all eight frames of that villain.
- **Combat set** (`art/combat/castle_spike.png`, `cursor_01..04.png`) — not
  generated. `romeart.py combat` has redrawn the reference pack's 48x34
  burst and cursors at 96: each original pixel classified into a material,
  the map scaled to the cell and re-rendered at pixel scale, the boundaries
  between colours dithered so nothing reads as 2x blocks; the cursors' rings
  redrawn from their ink boxes. The wall pieces it once drew are gone: the
  API could not make generated wall pieces that join, so Rome has drawn its
  siege from the grid below. The obstacles have been generated
  (`art/jobs/combat/obstacle_0N.json`).
- **Siege grid** (`art/combat/siege/cell_<x>_<y>.png`, 36 cells at 32) —
  the whole siege board plus its back band as one picture, sliced into cells
  (`sprites.ui.siege_grid`, REQ-165c). Route: `rd_plus__topdown_map` (the
  only style that has drawn a true overhead plan; `rd_plus__environment` has
  composed a perspective scene every time) at 384, first from a prompt to get
  the castle, then **img2img** on the 6x6 board composed from the sliced
  pieces (by `siegeslice`'s since-removed recipe mode, the band included,
  k-centroid to 384x384, `strength` 0.55) so the layout has been fixed by the source and the
  engine has repainted one continuous field over it
  (`art/jobs/combat/siege/cell.json`, returned at 192). Then
  `romeart.py siegeslice --grid` has written the 36 cells untouched at 32; the
  shell has scaled each to the 96 cell. Why not pieces: a per-code piece
  has repeated in every cell of its code, so a gatehouse, two different broken
  ends and a moat under the bottom wall only have not been drawable that way.
- **Field grid** (`art/combat/field/<zone>_<x>_<y>.png`, 30 cells at 96) —
  the ground of an open fight, one picture per continent (a zone's
  `field_grid`, REQ-165e). Italia's has come from a supplied 1254 x 1254
  picture kept at `art/fields/italia.png`, a light meadow painted on white,
  calmed first by `romeart.py fieldcalm --level strong` into
  `art/fields/italia_calm.png`: colour and size thresholds have masked the
  boulders, small rocks, ferns, dark clumps, clover rosettes, pale moss and
  brown earth patches, and G'MIC's patch-based inpainting has filled them
  from the painting's own grass, so the tufts, tiny flowers and tonal
  mottling have stayed and nothing large has distracted from the troops. That
  has been preparation of a supplied source, not post-processing of generated
  terrain; the fill has not been deterministic, so the calmed picture that
  has shipped has been kept beside the source. `romeart.py siegeslice --field` has then taken
  the largest 6:5 rectangle of content centred in it (726 x 605, the white
  kept out), scaled it to 576 x 480 with Lanczos and cut the thirty cells.
  Generating this ground has been tried and rejected: Retro
  Diffusion's tile styles have tiled seamlessly but drawn flat game-green,
  its RD Pro top-down style has drawn the look but has stopped at 256 and its
  joins have not blended, and variation tiles and pasted objects have read
  as squares and stickers; the painting route has been the only one that
  has kept the look. Galliae, Africa and Oriens (#64) have been
  derived from the calmed Italia painting rather than painted: each has been a
  696 x 580 window of `art/fields/italia_calm.png` whose edge is all
  content, taken off centre (top left, bottom right, top right), flipped,
  and colour-graded for its land by `romeart.py fieldgrade` (Galliae hue
  +6, saturation x1.2, value x0.86, a deeper cooler green for forest and
  moor; Africa hue -12, saturation x0.9, value x1.05, 12% tan, dry coastal
  grass against desert; Oriens hue -5, saturation x0.62, value x1.02, 10%
  grey-beige, a sun-bleached plateau), kept as `art/fields/<zone>.png` and
  sliced the same way. The exact calls have been in `art/fields/BUILD.md`; the
  grade has been deterministic, so the kept paintings have been what the calls
  produce. One painting recoloured three ways has shared its tufts across the four
  fields; a painting per continent in the same style would replace a
  derived one with no other change.
- **The title screen** (`art/ui/splash_title.png`, 256x164) — the eagle has
  been generated (screen route, no border); the words have been drawn by
  `romeart.py splashtitle` from C059 Bold, gold with dark shading, title above
  the eagle and subtitle across the pole, for the same reason as the
  publisher splash: generated lettering has garbled.
- **The publisher splash** (`art/ui/splash_logo.png`, 320x84, transparent) —
  composed, not generated whole, because generated lettering has garbled. The
  words have been rendered locally from C059 Bold at 1-bit, white with the
  original logo's red shading offset below and right; only the 44x44 emblem
  has been generated (`rd_pro__default`, `remove_bg`, magenta named in the
  prompt) and pasted where the original's globe sits; coins and sparkles have
  been drawn. The composition has been `romeart.py splashlogo`. The emblem has
  been a Mediterranean globe in a laurel wreath, not an eagle, which has read as
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
- **The Introduction** (#154, `art/intro/`, the jobs `art/jobs/intro/*.json`)
  — backgrounds, sprites and text, the way the 80s and 90s intros were built.
  Every set has been a 240x102 backdrop on the screen route with "no people" in
  the prompt, except the Curia, whose senators have been painted on their benches;
  the existing `backdrop_palace_welcome` and the four province vistas
  (`scenes/treasure_*`, under the province cards) have been reused. Trajan has sat on
  a curule stool, not a throne: the audience hall's dais (`bg_hall`) and the
  close shot's (`bg_throne_close`) have been painted bare and empty. Figures have been made on the
  figure route and animated with `custom_action`, alpha kept. The troop
  style has refused any canvas under 64 (`invalid_style_dimensions`, no
  charge), so a small figure has been generated at 64 and animated at its
  final size: `custom_action` has redrawn the still at the output canvas
  (32 to 256), a 64 still animated at 56 standing about 51 px tall. That has been
  how the crier (56), the four heroes, the legion and Trajan on his stool
  (48) have been made without scaling; both palace shots have used the same
  48 px Trajan, so he has been to scale with the dais, and the Curia has had his
  Audience figure (`emperor_traianus_figure`) animated again at 64. The tribesman has appeared
  only where he attacks the eagle bearer. The heroes on foot have been prompted from their class
  portraits; a kneel animated straight from a standing still has come back
  as the move down, not a held loop, so it has played once and held its
  last frame. Their turn to face the camera has been animated from the last
  frame of the walk, so the walk has run straight into it. Props
  (the plinth, the hourglass) have been
  `rd_pro__default` cut-outs, magenta named and removed. The crier's face
  has been the villain portrait route (128, cropped to 96). Only frame 00 of
  the crowd loop has been installed, at Dan's order: the engine has drawn it still, as
  cropped columns at staggered heights. Rain and lightning have been drawn
  by the engine, not painted. The theme has been synthesised by
  `romeart.py introtheme`, and every sound effect has been a CC0
  recording from Freesound, trimmed and made mono WAV (`assets/glory-of-rome/audio/CREDITS.txt`).
- **Recolouring** — when a set reads fine but its colours vanish against the
  grass, recolour the approved frames locally in HSV rather than
  regenerating: `romeart.py colormatch <src> <ref> <out> [--keep-hue LO,HI]`
  moves every opaque pixel's mean hue, saturation and value to a reference's,
  leaving a hue range (a trunk's browns) alone, so the pose and motion stay
  exactly as approved.

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
size: `romeart.py rdgen cost` (and the `check_cost` call inside `run`) has accepted and
priced an oversize request, and the task has then failed at inference with
`inference_failed`, "Unable to run inference.", and no charge (480x204 on
`rd_pro__default`, for one). Only two styles have reached 512
(`rd_plus__environment` and the internal `rd_plus__no_style`); they have been
a different engine and have not been used for anything, so no piece has
changed look against its neighbours.

---

## Running rdgen

```
python3 tools/romeart.py rdgen cost      art/jobs/<id>.json        # the price, nothing submitted
python3 tools/romeart.py rdgen run       art/jobs/<id>.json        # the price again; still nothing submitted
python3 tools/romeart.py rdgen run       art/jobs/<id>.json --run  # submitted and charged
python3 tools/romeart.py rdgen reprocess art/jobs/<id>.json        # re-cut and re-check, no new generation
```

- Nothing has been charged without `--run` (#143): `run` alone has quoted the
  cost and stopped, and only `run --run` has made a run directory.
- `reprocess` has made no generation call; a job that is not `raw_only` and
  not at its target size has still called the free `k_centroid` downscale.

- Every call has gone through rdgen, so the request has been saved beside the
  result.
- Output has landed in `build/art/<id>/runNN/`, a new directory per run;
  nothing has been overwritten.
- The task id has been written to disk before polling.
- `raw_only: true` has delivered the image exactly as returned.
- Token from `~/.config/retrodiffusion/token`. No environment variables.
- Every paid run has been appended to its job file's `"runs"` (the run
  folder, the request id, the cost, the date).
- `rdgen` has written nothing into `assets/`: approved finals have gone in
  through `romeart.py troop install` (or, for other art, as the job's notes
  say), and the job's `"pack"` list has named what it made.
- Review on a page: `romeart.py review <out> <runs...>` for chosen runs;
  `art.html` at the repo root has shown the whole pack and refreshed from disk.

---

## Terrain sets: PixelLab (retired)

Retro Diffusion has made every figure, object and screen in the pack. The
**terrain** that one surface fades into another over -- the 16 px corner sets
under `art/primitives/<zone>/` (sea, desert, cobble, river, grass) and the
tree and rock sprite batches -- came from PixelLab, which is no longer used;
its commands have been removed from `romeart.py`. The primitives it made
have been committed, so every zone set has rebuilt from them (`zone`), and
its job files (`create-tileset`'s lower and upper terrains and their
transition; the sprite batches) have stayed under `art/jobs/` as the record
of how they were made. Its seed did not reproduce a set -- a re-run returned
different pixels -- so the kept primitives, not the jobs, have been the
inputs. A new terrain set would be a Retro Diffusion corner set in the same
`tiles_meta.json` shape.

### Roads

Roads have not been a terrain set the game loads. `tools/romeart.py sweep`
has **swept** the set into the 24 road pieces the pack ships:

```
python3 tools/romeart.py sweep <set-dir> <out-dir> [--prefix road|river|...] [--grass GROUND] [--rim N] [--rim-shade F]
```

Every piece has been a signed-distance shape -- a straight band, a true
quarter circle, a 45 degree diagonal, or an end that tapers away -- filled
with the set's plain **upper** tile and left as the pack's own `grass.png`
outside, with a periodic value noise on the boundary so the edge has been ragged but
continuous across a tile line. Two consequences worth knowing before writing
a prompt:

- Only the plain upper tile has reached the game. The set's transition tiles,
  and any kerb or edging the prompt asked for, have been discarded. A border
  along the road has had to come from `--rim` / `--rim-shade`, which have painted it
  after the fact.
- Every straight exit has been the same 32 px band and every diagonal the
  same corner triangle, so any piece has joined any other, by construction:
  at every border each shape's distance has equalled the straight band's.

---

## Naming

One rule names every piece of art and the job that made it:

- **Art:** `art/<kind>/<subject>[_<variant>][_NN].png`. The kind is the folder
  (`troops`, `villains`, `characters`, `classes`, `intro`, `ui`, `scenes`,
  `combat`, `sprites`); the subject is the game's own id (a troop, villain,
  class or character id); a kind's main art takes no variant (a troop's frames
  `hastati_NN`, a villain's face `alaric_NN`, a character's face
  `pontifex_galliae_NN`), anything else does (`hastati_portrait`, `dux_hero`,
  `dux_walk_NN`, `dux_portrait`). `_NN` numbers frames.
- **Terrain** (`art/tiles/`, and a zone's set folder) is named by shape, with
  direction letters always in n, e, s, w order: `forest_edge_n` (open to the
  north), `water_edge_ne` (a corner), `mountain_edge_ns` (a strip),
  `desert_edge_nesw` (an island), `forest_inner_es` (a lone open diagonal),
  `road_nw` (joins north and west), `road_diag_ne` (the NE-SW diagonal),
  `river_mouth_ne` (the sea to its east and north), `bridge_ew` (the road
  runs east-west). A variant is `_v1`, `_v2`: `grass_v1`,
  `forest_apron_n_v2`, `mountain_edge_e_v1`. The full table is PACK-FORMAT
  §4.3; `romeart.py`'s `edge_name()` writes the edge names.
- **Map objects** (`art/objects/`) keep the thing's own name: `chest`,
  `town_galliae`, `landmark_sibyl`, `castle_gate`. game.json declares every
  terrain and object name the game draws (`tile_codes` and `map_art`).
- **Jobs:** `art/jobs/<kind>/<name>.json`, named after what they make: a series
  by its stem (`troops/hastati.json` makes `troops/hastati_NN.png`), a single
  file by its whole name (`combat/obstacle_01.json`). A still an animation
  starts from is `<name>_still.json`; an input to kept art (a primitive, an
  emblem a recipe composes) sits under `art/jobs/primitives/` or beside the art
  it feeds; a job whose output does not ship is under a `history/` folder and
  claims nothing. A job's `id` is its path under `art/jobs`, and its runs land
  in `build/art/<id>/runNN/`.

`romeart.py provenance check` fails on a job claiming a file not named after
it, and on a file two jobs claim.

---

## The record

Every file under `assets/glory-of-rome/art/` has been accounted for one of
four ways, and `romeart.py provenance check` (run by `scripts/check_maps.sh`,
so by `make check-maps` and CI) has failed on any file none of them names and
on any claim naming a file that is not there:

- **a job** -- a job file's `"pack"` list has named it: that generation's
  output, downscaled, cropped or framed as the job's notes say
  (`provenance normalise` has filled the lists from the older free-text
  `_pack_path`, which stays for reading);
- **a recipe** -- a command has composited it from committed inputs:
  `zone`/`install` for the zone sets, `compose` of the committed Italia
  layouts, `fills`, `aprons`, `details`, `interiors`, `edgevars`, `shore`,
  `dock`, `edges --as` for the farmland, `siegeslice`, `combat`,
  `classpicker`, `splashtitle --words`, `pingpong`; `--rebuild` has re-run
  every deterministic one into a copy of the pack and compared every pixel;
- **a source** -- kept as made, its input lost, with the reason (the master
  set's PixelLab-era sea, desert, roads and rivers; the art made for #38
  before the job record);
- **external** -- the font, its licence beside it.

`romeart.py provenance list` has printed each file and what claims it.
