# Glory of Rome — art pipeline

How the pack's art is generated. `ART-WORKLIST.md` is this same process written
out as runnable commands for all 113 items.

---

## 1. The custom style

Every figure goes through one custom style, so the roster reads as one set. It
is created once, free, and reused.

```
POST /v1/styles
```

| field | value |
|---|---|
| `reference_images` | one approved sprite (`build/art/hastati_style_cs1`) |
| `reference_caption` | a plain description of that sprite |
| `llm_instructions` | full-length standing figure in profile, whole body inside the frame, a solid flat bright magenta field that never appears on the character, no scenery |
| `user_prompt_template` | `{prompt}, full length, standing in profile facing right, game sprite, solid bright magenta background` |
| `force_bg_removal` | true |
| `force_palette` | false |

Current style: `user__glory_of_rome_troops_bac676cd`.

The style carries the framing, the pose and the look, so **per-asset prompts are
subject only**: *"A Roman legionary with a red crested helmet and a tall
rectangular red and gold shield, holding a spear, stocky and broad shouldered."*

---

## 2. Four steps per figure

### Step 1 — the still, $0.18

```
prompt_style  user__glory_of_rome_troops_bac676cd
width/height  96 x 96
flags         async
```

Submit, poll `/v1/inferences/tasks/{id}` until `succeeded`, then decode from
**`.result.base64_images[0]`** and confirm what landed:

```sh
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/<id>/01_still.png
file build/art/<id>/01_still.png   # must say: PNG image data
```

The submit step prints a request id; `GET /v1/inferences/requests/{id}` serves
that output for 24 hours.

### Step 2 — look at it

Open it at 1:1 and at 8x against `assets/kings-bounty/art/troops/pikemen_00.png`
and judge it by eye. Accept when:

- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- the armour, clothing and equipment named in the prompt are all present
- the shield is on the same arm as every other troop
- it reads at 1:1 over grass, forest and desert, not only when enlarged

Metrics are a floor, never the verdict. If a still is not right, record what was
wrong and move to the next asset.

### Step 3 — animate the approved still, $0.14

The still is uploaded with its alpha intact, which is what makes the returned
frames transparent, and padded onto a larger transparent canvas first. The
vendor's rule: *"a sprite whose opaque pixels touch the canvas edge animates
badly -- pad it onto a larger transparent canvas first."* `pad_to` in the job
does this without resampling a pixel.

```sh
jq -n --arg img "$(base64 -w0 build/art/<id>/01_still.png)" \
  '{prompt: "levelling the spear and thrusting it forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 6, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/<id>/anim_request.json
```

The motion line describes that subject's own action: a spear thrust, a bow drawn
and loosed, a sling released, jaws lunging.

### Step 4 — the frames

`rdgen` writes them. The grid is derived from the returned image -- cols =
sheet width / requested width, rows = sheet height / requested height -- and a
sheet that does not divide evenly is refused rather than cut. At 4 frames the
sheet is 2x2; at 6 it is 3x2. A GIF response is read frame by frame and not cut
at all.

`frames_duration` takes 4, 6, 8, 10, 12 or 16, and applies only to the
`rd_advanced_animation__*` family. Six is the vendor's recommendation for a
single action; eight for a loop.

rdgen also prints the silhouette growth across the frames and fails it below
25%. Real attacks measure 28-68%; anything that reads as walking measures under
7%.

---

## 3. Rules

- **Source size is free.** `blit()` (`src/ui.c:14`) stretches a troop texture
  into the combat cell, so the PNG's own dimensions are a quality choice, not a
  layout constraint. Generate at whatever size the chosen style is native to.
- **Style size ranges**, from the vendor's own catalogue: RD Pro 12-256, RD
  Plus 64-384 (`rd_plus__classic` 32-192, `rd_plus__low_res` 16-128), advanced
  animations 32-256 matching the start frame. The prompt-driven `rd_animation__*`
  styles are fixed: 32, 48, 64, 80 or 128, one size each.
- **Subject only in the prompt.** The style carries the rendering; prompts carry
  no framing, background or rendering words.
- **Name a background colour that cannot occur in the subject.** The background
  remover works on tone, so a light neutral field behind silver armour takes
  part of the armour with it.
- **Base terrain describes a pattern, never a subject** — "a small crop cut from
  the middle of a much larger sheet of wrapping paper, printed with..." — on
  `rd_plus__low_res` with `tile_x`/`tile_y`.
- **Terrain edges (48 files) are composited**, not generated: take the alpha
  mask from the corresponding reference edge and fill it from the new base and
  grass tiles.
- `async: true` on every call, so the task id exists before the charge.
- Token from `~/.config/retrodiffusion/token`. No environment variables.
- Nothing writes into `assets/`. Approved finals are copied across by hand.
- **Map tiles are opaque with their ground baked in.** `map_render.c` draws each
  tile alone over a black fill and only puts terrain underneath a
  wandering-army sprite, so a transparent object tile shows black.
- **Hero and boat do not use the troop style**, which would force every facing
  into a standing profile. They take `rd_plus__classic` with `remove_bg`, then
  `rd_advanced_animation__walking` or `__idle`.
- One call per asset.

---

## 4. Costs

| route | per item | items | total |
|---|---|---|---|
| troops and villains: still + attack animation | $0.32 | 42 | $13.44 |
| hero and boat: still + walk or idle animation | $0.178 | 9 | $1.60 |
| base terrain and tiled ground | $0.038 | 9 | $0.34 |
| screens, backdrops and class portraits | $0.074-$0.153 | 15 | $1.44 |
| map tiles, icons and combat pieces | $0.038 | 38 | $1.94 |
| **all 113 items, once** | | | **$18.76** |

Custom styles bill at RD Pro rates. The flat `rd_plus__*` styles are $0.038 at
96x96.

## 5. Validated

| route | status |
|---|---|
| figure still through the custom style | validated on the Hastati and the Furiae, one call each |
| custom style holds a set together | validated across three seeds: same armour, palette, shield arm |
| attack animation from an approved still | validated: four transparent frames, floor row identical across all four |
| sheet cut into four frames | validated pixel-identical against an independent cut |
| base terrain | validated on water |
| cell and screen art on `rd_plus__classic` / `rd_plus__default` | not yet run |

---

## 6. Plan

### Phase A — close out the Hastati

1. Set the style's background colour to one that cannot occur in Roman kit.
   `PATCH /v1/styles/{id}`, free.
2. Regenerate the one still that showed mask damage, $0.18.
3. Install the four attack frames as
   `assets/glory-of-rome/art/troops/pikemen_00..03.png` and run Rome.
4. Flip the manifest to `tile_h: 96`, `ui_scale: 2`.

### Phase B — the remaining routes

5. One object through `rd_plus__classic` and one through a second custom style
   built from an approved object; choose between them. $0.22. Objects and troops
   must come from the same style family or they will not match.
6. Decide the nine screen assets that exceed 384: author them, generate the
   subject alone and compose the screen in code as the title screen already does
   for the Aquila, or generate at 1x design size.

### Phase C — the roster, about $13.50

7. All 25 troops and 17 villains, still plus animation, in batches of five, each
   batch looked at before the next begins. Villain `_00` doubles as the contract
   portrait, so its pose must read as a portrait as well as a sprite.

### Phase D — the rest, about $2.40

8. Base terrain: grass, forest, mountain, desert, grass_variant. Water is done.
9. Terrain edges composited from the finished base tiles.
10. Objects, icons, HUD, backdrops and screens on whichever route Phase B picks.
