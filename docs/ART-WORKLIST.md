# Glory of Rome — art inventory and commands

116 items, 299 files. Every entry carries the files it produces, the
state those files are in today, the prompt, and the commands to run. The process
is described in `ART-PIPELINE.md`.

```sh
TOKEN=$(cat ~/.config/retrodiffusion/token)
```

Output lands under `build/art/<id>/`. The last step of each entry copies the
approved files into `assets/glory-of-rome/art/`; nothing before it writes into
the pack.

## Certified

Verified 2026-09-03 with `check_cost: true`, the API's free dry run, which
validates a request and generates nothing:

- **123 generation payloads** accepted, $11.21 to run them all once
- **57 animation payloads** accepted, $7.98
- **Total to generate the pack once: $19.19**
- Every shell block parses under `bash -n`
- Every `convert` crop checked against a real sheet
- **299 copy targets**, every one a real path in the pack except the 48 hero
  and boat directional files, which are new slots with no art yet

## Seven routes

**1. Troops and villains — 42 items, $0.32 each.** Still through
`user__glory_of_rome_troops_bac676cd` on a subject-only prompt, accepted by eye,
then `rd_advanced_animation__attack` at `frames_duration: 4` on the approved
still, and the returned 192x192 sheet cut into four 96x96 cells.

**2. Hero and boat — 12 items.** The troop style forces a standing figure in
profile, which no mounted rider or vessel can satisfy, so the first still uses
`rd_plus__classic`. Once one is accepted it becomes the reference image for a
second custom style, and the other eleven go through that, so hero, boat and
troops belong to one set. Animation is `rd_advanced_animation__walking` for the
four walking facings and `__idle` for the four at rest and the four boat views.

**3. Base terrain and tiled ground — 9 items, $0.038 each.** `rd_plus__low_res`
with `tile_x`/`tile_y`, opaque, prompts describing a *pattern* rather than a
subject. Keep that wording exactly as it is.

**4. Screens, backdrops and class portraits — 15 items.** `rd_plus__environment`
— "one-point perspective scenes with outlines and strong shapes" — at full
design size x2, up to 640x400. Opaque.

**5. The fortress — 1 item.** Generated once as a whole 288x192 structure and
cut into six 96x96 cells. Generated separately the six pieces would not line up.

**6. Four-frame loops — 3 items.** The siege engine, the oracle flame and the
target ring are one animation each, not four unrelated pictures: still, then
`rd_advanced_animation__idle`, then cut.

**7. Map tiles, icons and combat pieces — 34 items, $0.038 each.**
`rd_plus__classic` up to 192px, `rd_plus__default` above it. Map tiles are
opaque with the ground baked in, because `map_render.c` draws each tile alone
over a black fill and only puts terrain under a wandering-army sprite.

## Rules

- **96x96** for anything in a map or combat cell. Screen art at design size x2.
- **Subject only in the prompt.** The style carries the rendering.
- **No scenery in a figure prompt**, and a named background colour must be one
  that cannot occur in the subject. The style uses bright magenta.
- **No `input_palette`.**
- **`async: true`** on every call, so the task id exists before the charge.
- Decode from `.result.base64_images[0]`, then `file` the output: it must report
  PNG image data.
- **One call per item.**
- Terrain edges (48 files) are composited from the finished base and grass
  tiles using the reference edge's alpha mask, never generated.

---

## classes_barbarian

**Replaces:** `classes/barbarian.png`

**State:** placeholder. Design size 96x102 → generate at 192x204.

**Prompt**

> a weathered frontier commander in mail over furs with a wolf-pelt cloak and iron torc, standing on a rock at a forest frontier

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/classes_barbarian
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a weathered frontier commander in mail over furs with a wolf-pelt cloak and iron torc, standing on a rock at a forest frontier", "prompt_style": "rd_plus__environment", "width": 192, "height": 204, "num_images": 1, "seed": 1045, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/classes_barbarian/01_raw.png
file build/art/classes_barbarian/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a weathered frontier commander in mail over furs with a wolf-pelt cloak and iron torc is present and readable at 1:1
- the figure is complete head to foot
- the scene behind the figure is deliberate, not an accident of framing
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/classes_barbarian/01_raw.png assets/glory-of-rome/art/classes/barbarian.png
```


## classes_knight

**Replaces:** `classes/knight.png`

**State:** placeholder. Design size 96x102 → generate at 192x204.

**Prompt**

> a Roman general in a muscled bronze cuirass with lion-head shoulder pieces, red paludamentum cloak, crested helmet under one arm, standing before a distant fortified camp at sunset

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/classes_knight
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman general in a muscled bronze cuirass with lion-head shoulder pieces, red paludamentum cloak, crested helmet under one arm, standing before a distant fortified camp at sunset", "prompt_style": "rd_plus__environment", "width": 192, "height": 204, "num_images": 1, "seed": 1042, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/classes_knight/01_raw.png
file build/art/classes_knight/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red paludamentum cloak is present and readable at 1:1
- the crested helmet under one arm is present and readable at 1:1
- the figure is complete head to foot
- the scene behind the figure is deliberate, not an accident of framing
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/classes_knight/01_raw.png assets/glory-of-rome/art/classes/knight.png
```


## classes_paladin

**Replaces:** `classes/paladin.png`

**State:** placeholder. Design size 96x102 → generate at 192x204.

**Prompt**

> a Praetorian guardsman in an ornate scorpion-embossed cuirass with a tall black transverse crest, kneeling at a candlelit altar in a marble shrine

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/classes_paladin
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Praetorian guardsman in an ornate scorpion-embossed cuirass with a tall black transverse crest, kneeling at a candlelit altar in a marble shrine", "prompt_style": "rd_plus__environment", "width": 192, "height": 204, "num_images": 1, "seed": 1043, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/classes_paladin/01_raw.png
file build/art/classes_paladin/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the kneeling at a candlelit altar in a marble shrine is present and readable at 1:1
- the figure is complete head to foot
- the scene behind the figure is deliberate, not an accident of framing
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/classes_paladin/01_raw.png assets/glory-of-rome/art/classes/paladin.png
```


## classes_sorceress

**Replaces:** `classes/sorceress.png`

**State:** placeholder. Design size 96x102 → generate at 192x204.

**Prompt**

> a veiled Vestal oracle-priestess in white robes with a gold fillet, holding a laurel sprig, standing in a temple interior lit by a sacred flame

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/classes_sorceress
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a veiled Vestal oracle-priestess in white robes with a gold fillet, holding a laurel sprig, standing in a temple interior lit by a sacred flame", "prompt_style": "rd_plus__environment", "width": 192, "height": 204, "num_images": 1, "seed": 1044, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/classes_sorceress/01_raw.png
file build/art/classes_sorceress/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a veiled Vestal oracle-priestess in white robes with a gold fillet is present and readable at 1:1
- the figure is complete head to foot
- the scene behind the figure is deliberate, not an accident of framing
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/classes_sorceress/01_raw.png assets/glory-of-rome/art/classes/sorceress.png
```


## combat_castle_spike

**Replaces:** `combat/castle_spike.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a row of sharpened wooden stakes driven into the ground at an angle

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/combat_castle_spike
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a row of sharpened wooden stakes driven into the ground at an angle", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1077, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_castle_spike/01_raw.png
file build/art/combat_castle_spike/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a row of sharpened wooden stakes driven into the ground at an angle is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 on the battlefield

**Step 3 — copy into the pack.**


```sh
cp build/art/combat_castle_spike/01_raw.png assets/glory-of-rome/art/combat/castle_spike.png
```


## combat_castle_wall_01_06

**Replaces:** `combat/castle_wall_01.png`, `combat/castle_wall_02.png`, `combat/castle_wall_03.png`, `combat/castle_wall_04.png`, `combat/castle_wall_05.png`, `combat/castle_wall_06.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> Roman fortress wall segments in ashlar stone with crenellations, six varied sections

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/combat_castle_wall_01_06
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "Roman fortress wall segments in ashlar stone with crenellations, six varied sections", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1076, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_castle_wall_01_06/01_raw.png
file build/art/combat_castle_wall_01_06/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the six varied sections is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 on the battlefield

**Steps 1b..6 — the other 5 images.** $0.038 each, one call per file.


```sh
mkdir -p build/art/combat_castle_wall_01_06
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "Roman fortress wall segments in ashlar stone with crenellations, six varied sections", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1077, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_castle_wall_01_06/02_raw.png
file build/art/combat_castle_wall_01_06/02_raw.png   # must say: PNG image data
```

```sh
mkdir -p build/art/combat_castle_wall_01_06
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "Roman fortress wall segments in ashlar stone with crenellations, six varied sections", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1078, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_castle_wall_01_06/03_raw.png
file build/art/combat_castle_wall_01_06/03_raw.png   # must say: PNG image data
```

```sh
mkdir -p build/art/combat_castle_wall_01_06
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "Roman fortress wall segments in ashlar stone with crenellations, six varied sections", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1079, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_castle_wall_01_06/04_raw.png
file build/art/combat_castle_wall_01_06/04_raw.png   # must say: PNG image data
```

```sh
mkdir -p build/art/combat_castle_wall_01_06
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "Roman fortress wall segments in ashlar stone with crenellations, six varied sections", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1080, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_castle_wall_01_06/05_raw.png
file build/art/combat_castle_wall_01_06/05_raw.png   # must say: PNG image data
```

```sh
mkdir -p build/art/combat_castle_wall_01_06
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "Roman fortress wall segments in ashlar stone with crenellations, six varied sections", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1081, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_castle_wall_01_06/06_raw.png
file build/art/combat_castle_wall_01_06/06_raw.png   # must say: PNG image data
```

**Copy into the pack.**

```sh
cp build/art/combat_castle_wall_01_06/01_raw.png assets/glory-of-rome/art/combat/castle_wall_01.png
cp build/art/combat_castle_wall_01_06/02_raw.png assets/glory-of-rome/art/combat/castle_wall_02.png
cp build/art/combat_castle_wall_01_06/03_raw.png assets/glory-of-rome/art/combat/castle_wall_03.png
cp build/art/combat_castle_wall_01_06/04_raw.png assets/glory-of-rome/art/combat/castle_wall_04.png
cp build/art/combat_castle_wall_01_06/05_raw.png assets/glory-of-rome/art/combat/castle_wall_05.png
cp build/art/combat_castle_wall_01_06/06_raw.png assets/glory-of-rome/art/combat/castle_wall_06.png
```


## combat_cursor_01_04

**Replaces:** `combat/cursor_01.png`, `combat/cursor_02.png`, `combat/cursor_03.png`, `combat/cursor_04.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a thin bronze laurel-wreath ring outline, hollow centre

**Step 1 — the still.** $0.038. These four files are one loop, not four
pictures, so they come from one animation rather than four calls.

```sh
mkdir -p build/art/combat_cursor_01_04
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a thin bronze laurel-wreath ring outline, hollow centre", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1078, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_cursor_01_04/01_still.png
file build/art/combat_cursor_01_04/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the hollow centre is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent

**Step 3 — animate it into four frames.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/combat_cursor_01_04/01_still.png)" \
  '{prompt: "the ring glinting as the light travels round it, the ring itself still",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/combat_cursor_01_04/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/combat_cursor_01_04/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_cursor_01_04/02_sheet.png
file build/art/combat_cursor_01_04/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/combat_cursor_01_04/02_sheet.png -crop 96x96 +repage build/art/combat_cursor_01_04/frame_%02d.png
identify -format "%f %wx%h\n" build/art/combat_cursor_01_04/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/combat_cursor_01_04/frame_00.png assets/glory-of-rome/art/combat/cursor_01.png
cp build/art/combat_cursor_01_04/frame_01.png assets/glory-of-rome/art/combat/cursor_02.png
cp build/art/combat_cursor_01_04/frame_02.png assets/glory-of-rome/art/combat/cursor_03.png
cp build/art/combat_cursor_01_04/frame_03.png assets/glory-of-rome/art/combat/cursor_04.png
```


## combat_field_grass

**Replaces:** `combat/field_grass.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat trampled olive-green ground with small bare patches of brown earth scattered evenly through it, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/combat_field_grass
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat trampled olive-green ground with small bare patches of brown earth scattered evenly through it, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1074, "async": true, "tile_x": true, "tile_y": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_field_grass/01_raw.png
file build/art/combat_field_grass/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the paper is printed all over with an endless repeating pattern: a flat trampled olive-green ground with small bare patches of brown earth scattered evenly through it is present and readable at 1:1
- the the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere is present and readable at 1:1
- it tiles with no seam: check a 3x3 montage, never a single tile
- no feature large enough to be noticed repeating across a field of tiles
- even brightness corner to corner, no vignette and no gradient
- it reads as the right ground at 1:1, not only enlarged

**Step 3 — copy into the pack.**


```sh
cp build/art/combat_field_grass/01_raw.png assets/glory-of-rome/art/combat/field_grass.png
```


## combat_obstacle_01_03

**Replaces:** `combat/obstacle_01.png`, `combat/obstacle_02.png`, `combat/obstacle_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a mossy boulder / a dry thorn scrub bush / a fallen broken column drum

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/combat_obstacle_01_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a mossy boulder / a dry thorn scrub bush / a fallen broken column drum", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1075, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_obstacle_01_03/01_raw.png
file build/art/combat_obstacle_01_03/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a mossy boulder / a dry thorn scrub bush / a fallen broken column drum is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 on the battlefield

**Steps 1b..3 — the other 2 images.** $0.038 each, one call per file.


```sh
mkdir -p build/art/combat_obstacle_01_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a mossy boulder / a dry thorn scrub bush / a fallen broken column drum", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1076, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_obstacle_01_03/02_raw.png
file build/art/combat_obstacle_01_03/02_raw.png   # must say: PNG image data
```

```sh
mkdir -p build/art/combat_obstacle_01_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a mossy boulder / a dry thorn scrub bush / a fallen broken column drum", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1077, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/combat_obstacle_01_03/03_raw.png
file build/art/combat_obstacle_01_03/03_raw.png   # must say: PNG image data
```

**Copy into the pack.**

```sh
cp build/art/combat_obstacle_01_03/01_raw.png assets/glory-of-rome/art/combat/obstacle_01.png
cp build/art/combat_obstacle_01_03/02_raw.png assets/glory-of-rome/art/combat/obstacle_02.png
cp build/art/combat_obstacle_01_03/03_raw.png assets/glory-of-rome/art/combat/obstacle_03.png
```


## sprites_boat_east_00_03

**Replaces:** `sprites/boat_east_00.png`, `sprites/boat_east_01.png`, `sprites/boat_east_02.png`, `sprites/boat_east_03.png`

**State:** file does not exist yet; size taken from sprites/boat_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman liburnian galley in full profile facing right, square sail with a painted eagle, bank of oars

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_boat_east_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman liburnian galley in full profile facing right, square sail with a painted eagle, bank of oars", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1052, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_boat_east_00_03/01_still.png
file build/art/sprites_boat_east_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the square sail with a painted eagle is present and readable at 1:1
- the bank of oars is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__idle`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_boat_east_00_03/01_still.png)" \
  '{prompt: "riding the swell, the sail and pennant stirring",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_boat_east_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_boat_east_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_boat_east_00_03/02_sheet.png
file build/art/sprites_boat_east_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_boat_east_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_boat_east_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_boat_east_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_boat_east_00_03/frame_00.png assets/glory-of-rome/art/sprites/boat_east_00.png
cp build/art/sprites_boat_east_00_03/frame_01.png assets/glory-of-rome/art/sprites/boat_east_01.png
cp build/art/sprites_boat_east_00_03/frame_02.png assets/glory-of-rome/art/sprites/boat_east_02.png
cp build/art/sprites_boat_east_00_03/frame_03.png assets/glory-of-rome/art/sprites/boat_east_03.png
```


## sprites_boat_north_00_03

**Replaces:** `sprites/boat_north_00.png`, `sprites/boat_north_01.png`, `sprites/boat_north_02.png`, `sprites/boat_north_03.png`

**State:** file does not exist yet; size taken from sprites/boat_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman liburnian galley seen from astern, steering oar and square sail from behind, oars out

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_boat_north_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman liburnian galley seen from astern, steering oar and square sail from behind, oars out", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1054, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_boat_north_00_03/01_still.png
file build/art/sprites_boat_north_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the steering oar and square sail from behind is present and readable at 1:1
- the oars out is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__idle`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_boat_north_00_03/01_still.png)" \
  '{prompt: "riding the swell, the sail and pennant stirring",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_boat_north_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_boat_north_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_boat_north_00_03/02_sheet.png
file build/art/sprites_boat_north_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_boat_north_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_boat_north_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_boat_north_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_boat_north_00_03/frame_00.png assets/glory-of-rome/art/sprites/boat_north_00.png
cp build/art/sprites_boat_north_00_03/frame_01.png assets/glory-of-rome/art/sprites/boat_north_01.png
cp build/art/sprites_boat_north_00_03/frame_02.png assets/glory-of-rome/art/sprites/boat_north_02.png
cp build/art/sprites_boat_north_00_03/frame_03.png assets/glory-of-rome/art/sprites/boat_north_03.png
```


## sprites_boat_south_00_03

**Replaces:** `sprites/boat_south_00.png`, `sprites/boat_south_01.png`, `sprites/boat_south_02.png`, `sprites/boat_south_03.png`

**State:** file does not exist yet; size taken from sprites/boat_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman liburnian galley seen bow-on, single square sail with a painted eagle, an eye painted on the prow, oars out

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_boat_south_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman liburnian galley seen bow-on, single square sail with a painted eagle, an eye painted on the prow, oars out", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1051, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_boat_south_00_03/01_still.png
file build/art/sprites_boat_south_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the single square sail with a painted eagle is present and readable at 1:1
- the an eye painted on the prow is present and readable at 1:1
- the oars out is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__idle`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_boat_south_00_03/01_still.png)" \
  '{prompt: "riding the swell, the sail and pennant stirring",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_boat_south_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_boat_south_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_boat_south_00_03/02_sheet.png
file build/art/sprites_boat_south_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_boat_south_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_boat_south_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_boat_south_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_boat_south_00_03/frame_00.png assets/glory-of-rome/art/sprites/boat_south_00.png
cp build/art/sprites_boat_south_00_03/frame_01.png assets/glory-of-rome/art/sprites/boat_south_01.png
cp build/art/sprites_boat_south_00_03/frame_02.png assets/glory-of-rome/art/sprites/boat_south_02.png
cp build/art/sprites_boat_south_00_03/frame_03.png assets/glory-of-rome/art/sprites/boat_south_03.png
```


## sprites_boat_west_00_03

**Replaces:** `sprites/boat_west_00.png`, `sprites/boat_west_01.png`, `sprites/boat_west_02.png`, `sprites/boat_west_03.png`

**State:** file does not exist yet; size taken from sprites/boat_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman liburnian galley in full profile facing left, square sail with a painted eagle, bank of oars

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_boat_west_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman liburnian galley in full profile facing left, square sail with a painted eagle, bank of oars", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1053, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_boat_west_00_03/01_still.png
file build/art/sprites_boat_west_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the square sail with a painted eagle is present and readable at 1:1
- the bank of oars is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__idle`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_boat_west_00_03/01_still.png)" \
  '{prompt: "riding the swell, the sail and pennant stirring",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_boat_west_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_boat_west_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_boat_west_00_03/02_sheet.png
file build/art/sprites_boat_west_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_boat_west_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_boat_west_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_boat_west_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_boat_west_00_03/frame_00.png assets/glory-of-rome/art/sprites/boat_west_00.png
cp build/art/sprites_boat_west_00_03/frame_01.png assets/glory-of-rome/art/sprites/boat_west_01.png
cp build/art/sprites_boat_west_00_03/frame_02.png assets/glory-of-rome/art/sprites/boat_west_02.png
cp build/art/sprites_boat_west_00_03/frame_03.png assets/glory-of-rome/art/sprites/boat_west_03.png
```


## sprites_hero_idle_east_00_03

**Replaces:** `sprites/hero_idle_east_00.png`, `sprites/hero_idle_east_01.png`, `sprites/hero_idle_east_02.png`, `sprites/hero_idle_east_03.png`

**State:** file does not exist yet; size taken from sprites/hero_walk_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman commander on horseback at rest, red cloak, crested helmet, gilded cuirass, in profile facing right, the horse standing still

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_hero_idle_east_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on horseback at rest, red cloak, crested helmet, gilded cuirass, in profile facing right, the horse standing still", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1064, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_idle_east_00_03/01_still.png
file build/art/sprites_hero_idle_east_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red cloak is present and readable at 1:1
- the crested helmet is present and readable at 1:1
- the gilded cuirass is present and readable at 1:1
- the in profile facing right is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__idle`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_hero_idle_east_00_03/01_still.png)" \
  '{prompt: "walking, steady steps, the legs moving",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_hero_idle_east_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_hero_idle_east_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_idle_east_00_03/02_sheet.png
file build/art/sprites_hero_idle_east_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_hero_idle_east_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_hero_idle_east_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_hero_idle_east_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_hero_idle_east_00_03/frame_00.png assets/glory-of-rome/art/sprites/hero_idle_east_00.png
cp build/art/sprites_hero_idle_east_00_03/frame_01.png assets/glory-of-rome/art/sprites/hero_idle_east_01.png
cp build/art/sprites_hero_idle_east_00_03/frame_02.png assets/glory-of-rome/art/sprites/hero_idle_east_02.png
cp build/art/sprites_hero_idle_east_00_03/frame_03.png assets/glory-of-rome/art/sprites/hero_idle_east_03.png
```


## sprites_hero_idle_north_00_03

**Replaces:** `sprites/hero_idle_north_00.png`, `sprites/hero_idle_north_01.png`, `sprites/hero_idle_north_02.png`, `sprites/hero_idle_north_03.png`

**State:** file does not exist yet; size taken from sprites/hero_walk_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman commander on horseback at rest, red cloak, crested helmet, gilded cuirass, seen from behind, cloak and crest from the rear, the horse standing still

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_hero_idle_north_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on horseback at rest, red cloak, crested helmet, gilded cuirass, seen from behind, cloak and crest from the rear, the horse standing still", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1057, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_idle_north_00_03/01_still.png
file build/art/sprites_hero_idle_north_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red cloak is present and readable at 1:1
- the crested helmet is present and readable at 1:1
- the gilded cuirass is present and readable at 1:1
- the cloak and crest from the rear is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__idle`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_hero_idle_north_00_03/01_still.png)" \
  '{prompt: "walking, steady steps, the legs moving",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_hero_idle_north_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_hero_idle_north_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_idle_north_00_03/02_sheet.png
file build/art/sprites_hero_idle_north_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_hero_idle_north_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_hero_idle_north_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_hero_idle_north_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_hero_idle_north_00_03/frame_00.png assets/glory-of-rome/art/sprites/hero_idle_north_00.png
cp build/art/sprites_hero_idle_north_00_03/frame_01.png assets/glory-of-rome/art/sprites/hero_idle_north_01.png
cp build/art/sprites_hero_idle_north_00_03/frame_02.png assets/glory-of-rome/art/sprites/hero_idle_north_02.png
cp build/art/sprites_hero_idle_north_00_03/frame_03.png assets/glory-of-rome/art/sprites/hero_idle_north_03.png
```


## sprites_hero_idle_south_00_03

**Replaces:** `sprites/hero_idle_south_00.png`, `sprites/hero_idle_south_01.png`, `sprites/hero_idle_south_02.png`, `sprites/hero_idle_south_03.png`

**State:** file does not exist yet; size taken from sprites/hero_walk_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman commander on horseback at rest, red cloak, crested helmet, gilded cuirass, seen from the front, riding toward the viewer, the horse standing still

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_hero_idle_south_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on horseback at rest, red cloak, crested helmet, gilded cuirass, seen from the front, riding toward the viewer, the horse standing still", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1050, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_idle_south_00_03/01_still.png
file build/art/sprites_hero_idle_south_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red cloak is present and readable at 1:1
- the crested helmet is present and readable at 1:1
- the gilded cuirass is present and readable at 1:1
- the the horse standing still is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__idle`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_hero_idle_south_00_03/01_still.png)" \
  '{prompt: "walking, steady steps, the legs moving",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_hero_idle_south_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_hero_idle_south_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_idle_south_00_03/02_sheet.png
file build/art/sprites_hero_idle_south_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_hero_idle_south_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_hero_idle_south_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_hero_idle_south_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_hero_idle_south_00_03/frame_00.png assets/glory-of-rome/art/sprites/hero_idle_south_00.png
cp build/art/sprites_hero_idle_south_00_03/frame_01.png assets/glory-of-rome/art/sprites/hero_idle_south_01.png
cp build/art/sprites_hero_idle_south_00_03/frame_02.png assets/glory-of-rome/art/sprites/hero_idle_south_02.png
cp build/art/sprites_hero_idle_south_00_03/frame_03.png assets/glory-of-rome/art/sprites/hero_idle_south_03.png
```


## sprites_hero_idle_west_00_03

**Replaces:** `sprites/hero_idle_west_00.png`, `sprites/hero_idle_west_01.png`, `sprites/hero_idle_west_02.png`, `sprites/hero_idle_west_03.png`

**State:** file does not exist yet; size taken from sprites/hero_walk_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman commander on horseback at rest, red cloak, crested helmet, gilded cuirass, in profile facing left, the horse standing still

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_hero_idle_west_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on horseback at rest, red cloak, crested helmet, gilded cuirass, in profile facing left, the horse standing still", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1071, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_idle_west_00_03/01_still.png
file build/art/sprites_hero_idle_west_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red cloak is present and readable at 1:1
- the crested helmet is present and readable at 1:1
- the gilded cuirass is present and readable at 1:1
- the in profile facing left is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__idle`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_hero_idle_west_00_03/01_still.png)" \
  '{prompt: "walking, steady steps, the legs moving",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_hero_idle_west_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_hero_idle_west_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_idle_west_00_03/02_sheet.png
file build/art/sprites_hero_idle_west_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_hero_idle_west_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_hero_idle_west_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_hero_idle_west_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_hero_idle_west_00_03/frame_00.png assets/glory-of-rome/art/sprites/hero_idle_west_00.png
cp build/art/sprites_hero_idle_west_00_03/frame_01.png assets/glory-of-rome/art/sprites/hero_idle_west_01.png
cp build/art/sprites_hero_idle_west_00_03/frame_02.png assets/glory-of-rome/art/sprites/hero_idle_west_02.png
cp build/art/sprites_hero_idle_west_00_03/frame_03.png assets/glory-of-rome/art/sprites/hero_idle_west_03.png
```


## sprites_hero_walk_east_00_03

**Replaces:** `sprites/hero_walk_east_00.png`, `sprites/hero_walk_east_01.png`, `sprites/hero_walk_east_02.png`, `sprites/hero_walk_east_03.png`

**State:** file does not exist yet; size taken from sprites/hero_walk_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman commander on horseback in profile facing right, red cloak, crested helmet, gilded cuirass

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_hero_walk_east_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on horseback in profile facing right, red cloak, crested helmet, gilded cuirass", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1047, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_east_00_03/01_still.png
file build/art/sprites_hero_walk_east_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red cloak is present and readable at 1:1
- the crested helmet is present and readable at 1:1
- the gilded cuirass is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__walking`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_hero_walk_east_00_03/01_still.png)" \
  '{prompt: "walking, steady steps, the legs moving",
    prompt_style: "rd_advanced_animation__walking",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_hero_walk_east_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_hero_walk_east_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_east_00_03/02_sheet.png
file build/art/sprites_hero_walk_east_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_hero_walk_east_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_hero_walk_east_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_hero_walk_east_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_hero_walk_east_00_03/frame_00.png assets/glory-of-rome/art/sprites/hero_walk_east_00.png
cp build/art/sprites_hero_walk_east_00_03/frame_01.png assets/glory-of-rome/art/sprites/hero_walk_east_01.png
cp build/art/sprites_hero_walk_east_00_03/frame_02.png assets/glory-of-rome/art/sprites/hero_walk_east_02.png
cp build/art/sprites_hero_walk_east_00_03/frame_03.png assets/glory-of-rome/art/sprites/hero_walk_east_03.png
```


## sprites_hero_walk_north_00_03

**Replaces:** `sprites/hero_walk_north_00.png`, `sprites/hero_walk_north_01.png`, `sprites/hero_walk_north_02.png`, `sprites/hero_walk_north_03.png`

**State:** file does not exist yet; size taken from sprites/hero_walk_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman commander on horseback seen from behind, red cloak and crested helmet from the rear, gilded cuirass, riding away from the viewer

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_hero_walk_north_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on horseback seen from behind, red cloak and crested helmet from the rear, gilded cuirass, riding away from the viewer", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1049, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_north_00_03/01_still.png
file build/art/sprites_hero_walk_north_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red cloak and crested helmet from the rear is present and readable at 1:1
- the gilded cuirass is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__walking`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_hero_walk_north_00_03/01_still.png)" \
  '{prompt: "walking, steady steps, the legs moving",
    prompt_style: "rd_advanced_animation__walking",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_hero_walk_north_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_hero_walk_north_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_north_00_03/02_sheet.png
file build/art/sprites_hero_walk_north_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_hero_walk_north_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_hero_walk_north_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_hero_walk_north_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_hero_walk_north_00_03/frame_00.png assets/glory-of-rome/art/sprites/hero_walk_north_00.png
cp build/art/sprites_hero_walk_north_00_03/frame_01.png assets/glory-of-rome/art/sprites/hero_walk_north_01.png
cp build/art/sprites_hero_walk_north_00_03/frame_02.png assets/glory-of-rome/art/sprites/hero_walk_north_02.png
cp build/art/sprites_hero_walk_north_00_03/frame_03.png assets/glory-of-rome/art/sprites/hero_walk_north_03.png
```


## sprites_hero_walk_south_00_03

**Replaces:** `sprites/hero_walk_south_00.png`, `sprites/hero_walk_south_01.png`, `sprites/hero_walk_south_02.png`, `sprites/hero_walk_south_03.png`

**State:** file does not exist yet; size taken from sprites/hero_walk_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman commander on horseback seen from the front, red cloak, crested helmet, gilded cuirass, riding toward the viewer

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_hero_walk_south_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on horseback seen from the front, red cloak, crested helmet, gilded cuirass, riding toward the viewer", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1046, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_south_00_03/01_still.png
file build/art/sprites_hero_walk_south_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red cloak is present and readable at 1:1
- the crested helmet is present and readable at 1:1
- the gilded cuirass is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__walking`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_hero_walk_south_00_03/01_still.png)" \
  '{prompt: "walking, steady steps, the legs moving",
    prompt_style: "rd_advanced_animation__walking",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_hero_walk_south_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_hero_walk_south_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_south_00_03/02_sheet.png
file build/art/sprites_hero_walk_south_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_hero_walk_south_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_hero_walk_south_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_hero_walk_south_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_hero_walk_south_00_03/frame_00.png assets/glory-of-rome/art/sprites/hero_walk_south_00.png
cp build/art/sprites_hero_walk_south_00_03/frame_01.png assets/glory-of-rome/art/sprites/hero_walk_south_01.png
cp build/art/sprites_hero_walk_south_00_03/frame_02.png assets/glory-of-rome/art/sprites/hero_walk_south_02.png
cp build/art/sprites_hero_walk_south_00_03/frame_03.png assets/glory-of-rome/art/sprites/hero_walk_south_03.png
```


## sprites_hero_walk_west_00_03

**Replaces:** `sprites/hero_walk_west_00.png`, `sprites/hero_walk_west_01.png`, `sprites/hero_walk_west_02.png`, `sprites/hero_walk_west_03.png`

**State:** file does not exist yet; size taken from sprites/hero_walk_00.png. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman commander on horseback in profile facing left, red cloak, crested helmet, gilded cuirass

**Step 1 — the still.** $0.038, `rd_plus__classic`. The troop style is not used here: it forces every prompt into a standing figure in profile, which no mounted rider or vessel can satisfy.

The first of these nine to be accepted becomes the reference for a second custom style, `POST /v1/styles` with that PNG as `reference_images`, `force_bg_removal: true` and a `user_prompt_template` of `{prompt}, game sprite, solid bright magenta background`. The remaining eight then use that style at $0.18 so the hero, the boat and the troops belong to one set.

```sh
mkdir -p build/art/sprites_hero_walk_west_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on horseback in profile facing left, red cloak, crested helmet, gilded cuirass", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1048, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_west_00_03/01_still.png
file build/art/sprites_hero_walk_west_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red cloak is present and readable at 1:1
- the crested helmet is present and readable at 1:1
- the gilded cuirass is present and readable at 1:1
- the subject is complete and inside the frame
- the background is fully transparent
- it faces the direction this file is for

**Step 3 — animate the approved still.** $0.14, `rd_advanced_animation__walking`.

```sh
jq -n --arg img "$(base64 -w0 build/art/sprites_hero_walk_west_00_03/01_still.png)" \
  '{prompt: "walking, steady steps, the legs moving",
    prompt_style: "rd_advanced_animation__walking",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/sprites_hero_walk_west_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/sprites_hero_walk_west_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_west_00_03/02_sheet.png
file build/art/sprites_hero_walk_west_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/sprites_hero_walk_west_00_03/02_sheet.png -crop 96x96 +repage build/art/sprites_hero_walk_west_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/sprites_hero_walk_west_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/sprites_hero_walk_west_00_03/frame_00.png assets/glory-of-rome/art/sprites/hero_walk_west_00.png
cp build/art/sprites_hero_walk_west_00_03/frame_01.png assets/glory-of-rome/art/sprites/hero_walk_west_01.png
cp build/art/sprites_hero_walk_west_00_03/frame_02.png assets/glory-of-rome/art/sprites/hero_walk_west_02.png
cp build/art/sprites_hero_walk_west_00_03/frame_03.png assets/glory-of-rome/art/sprites/hero_walk_west_03.png
```


## tiles_artifact_chest

**Replaces:** `tiles/artifact_chest.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> an ornate gilded reliquary casket with glowing seams, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_artifact_chest
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an ornate gilded reliquary casket with glowing seams, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1068, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_artifact_chest/01_raw.png
file build/art/tiles_artifact_chest/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_artifact_chest/01_raw.png assets/glory-of-rome/art/tiles/artifact_chest.png
```


## tiles_artifact_ring

**Replaces:** `tiles/artifact_ring.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a golden ring resting on a small stone plinth, radiating light, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_artifact_ring
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden ring resting on a small stone plinth, radiating light, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1069, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_artifact_ring/01_raw.png
file build/art/tiles_artifact_ring/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the radiating light is present and readable at 1:1
- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_artifact_ring/01_raw.png assets/glory-of-rome/art/tiles/artifact_ring.png
```


## tiles_bridge_h

**Replaces:** `tiles/bridge_h.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman stone arch bridge spanning left to right, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_bridge_h
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman stone arch bridge spanning left to right, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1071, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_bridge_h/01_raw.png
file build/art/tiles_bridge_h/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_bridge_h/01_raw.png assets/glory-of-rome/art/tiles/bridge_h.png
```


## tiles_bridge_v

**Replaces:** `tiles/bridge_v.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman stone arch bridge spanning top to bottom, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_bridge_v
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman stone arch bridge spanning top to bottom, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1072, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_bridge_v/01_raw.png
file build/art/tiles_bridge_v/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_bridge_v/01_raw.png assets/glory-of-rome/art/tiles/bridge_v.png
```


## tiles_castle_png

**Replaces:** `tiles/castle_br.png`, `tiles/castle_gate.png`, `tiles/castle_ml.png`, `tiles/castle_mr.png`, `tiles/castle_tl.png`, `tiles/castle_tr.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman legionary fortress with ashlar stone walls, square corner towers, red tile roofs and an arched gate in the middle of the front wall, seen from a high angle, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate the whole fortress once**, 288x192, $0.074. The six pack
files are cells of one structure; generated separately they would not line up.

```sh
mkdir -p build/art/tiles_castle_png
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman legionary fortress with ashlar stone walls, square corner towers, red tile roofs and an arched gate in the middle of the front wall, seen from a high angle, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__environment", "width": 288, "height": 192, "num_images": 1, "seed": 1061, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_castle_png/01_raw.png
file build/art/tiles_castle_png/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the gate is in the middle of the bottom row of cells and reads as walkable
- the other five cells read as solid wall or tower
- the grass runs to every edge and matches `tiles/grass.png`
- the image is fully opaque

**Step 3 — cut it into the six 96x96 cells**, left to right, top to bottom.

```sh
convert build/art/tiles_castle_png/01_raw.png -crop 96x96 +repage build/art/tiles_castle_png/cell_%02d.png
identify -format "%f %wx%h\n" build/art/tiles_castle_png/cell_0*.png   # six files, each 96x96
```

**Step 4 — copy into the pack.**

```sh
cp build/art/tiles_castle_png/cell_00.png assets/glory-of-rome/art/tiles/castle_br.png
cp build/art/tiles_castle_png/cell_01.png assets/glory-of-rome/art/tiles/castle_gate.png
cp build/art/tiles_castle_png/cell_02.png assets/glory-of-rome/art/tiles/castle_ml.png
cp build/art/tiles_castle_png/cell_03.png assets/glory-of-rome/art/tiles/castle_mr.png
cp build/art/tiles_castle_png/cell_04.png assets/glory-of-rome/art/tiles/castle_tl.png
cp build/art/tiles_castle_png/cell_05.png assets/glory-of-rome/art/tiles/castle_tr.png
```


## tiles_chest

**Replaces:** `tiles/chest.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman strongbox, an iron-banded wooden arca with bronze studs, lid closed, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_chest
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman strongbox, an iron-banded wooden arca with bronze studs, lid closed, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1067, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_chest/01_raw.png
file build/art/tiles_chest/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the an iron-banded wooden arca with bronze studs is present and readable at 1:1
- the lid closed is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_chest/01_raw.png assets/glory-of-rome/art/tiles/chest.png
```


## tiles_desert

**Replaces:** `tiles/desert.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat pale sand ground covered with many small short horizontal streaks of darker tan and near-white in strong contrast, the streaks spread evenly and randomly with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_desert
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat pale sand ground covered with many small short horizontal streaks of darker tan and near-white in strong contrast, the streaks spread evenly and randomly with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1060, "async": true, "tile_x": true, "tile_y": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_desert/01_raw.png
file build/art/tiles_desert/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the paper is printed all over with an endless repeating pattern: a flat pale sand ground covered with many small short horizontal streaks of darker tan and near-white in strong contrast is present and readable at 1:1
- the the streaks spread evenly and randomly with the same density everywhere is present and readable at 1:1
- it tiles with no seam: check a 3x3 montage, never a single tile
- no feature large enough to be noticed repeating across a field of tiles
- even brightness corner to corner, no vignette and no gradient
- it reads as the right ground at 1:1, not only enlarged

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_desert/01_raw.png assets/glory-of-rome/art/tiles/desert.png
```


## tiles_dwelling_dungeon

**Replaces:** `tiles/dwelling_dungeon.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman columbarium crypt entrance, stone doorway flanked by funerary urns, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_dwelling_dungeon
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman columbarium crypt entrance, stone doorway flanked by funerary urns, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1066, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_dwelling_dungeon/01_raw.png
file build/art/tiles_dwelling_dungeon/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the stone doorway flanked by funerary urns is present and readable at 1:1
- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_dwelling_dungeon/01_raw.png assets/glory-of-rome/art/tiles/dwelling_dungeon.png
```


## tiles_dwelling_forest

**Replaces:** `tiles/dwelling_forest.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a stone shrine and altar in a sacred grove, surrounded by trees, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_dwelling_forest
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a stone shrine and altar in a sacred grove, surrounded by trees, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1064, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_dwelling_forest/01_raw.png
file build/art/tiles_dwelling_forest/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the surrounded by trees is present and readable at 1:1
- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_dwelling_forest/01_raw.png assets/glory-of-rome/art/tiles/dwelling_forest.png
```


## tiles_dwelling_hills

**Replaces:** `tiles/dwelling_hills.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a cave mouth in a rocky hillside with a carved stone lintel, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_dwelling_hills
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a cave mouth in a rocky hillside with a carved stone lintel, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1065, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_dwelling_hills/01_raw.png
file build/art/tiles_dwelling_hills/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_dwelling_hills/01_raw.png assets/glory-of-rome/art/tiles/dwelling_hills.png
```


## tiles_dwelling_plains

**Replaces:** `tiles/dwelling_plains.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman villa rustica farmstead with a tiled roof and a walled paddock, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_dwelling_plains
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman villa rustica farmstead with a tiled roof and a walled paddock, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1063, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_dwelling_plains/01_raw.png
file build/art/tiles_dwelling_plains/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_dwelling_plains/01_raw.png assets/glory-of-rome/art/tiles/dwelling_plains.png
```


## tiles_forest

**Replaces:** `tiles/forest.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat dark green ground covered with many small rounded clumps of mid green and near-black green in strong contrast, the clumps packed evenly and randomly with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_forest
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat dark green ground covered with many small rounded clumps of mid green and near-black green in strong contrast, the clumps packed evenly and randomly with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1057, "async": true, "tile_x": true, "tile_y": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_forest/01_raw.png
file build/art/tiles_forest/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the paper is printed all over with an endless repeating pattern: a flat dark green ground covered with many small rounded clumps of mid green and near-black green in strong contrast is present and readable at 1:1
- the the clumps packed evenly and randomly with the same density everywhere is present and readable at 1:1
- it tiles with no seam: check a 3x3 montage, never a single tile
- no feature large enough to be noticed repeating across a field of tiles
- even brightness corner to corner, no vignette and no gradient
- it reads as the right ground at 1:1, not only enlarged

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_forest/01_raw.png assets/glory-of-rome/art/tiles/forest.png
```


## tiles_grass

**Replaces:** `tiles/grass.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat medium green ground covered with many small short specks of bright yellow-green and of dark green in strong contrast, the specks evenly and randomly spread with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_grass
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat medium green ground covered with many small short specks of bright yellow-green and of dark green in strong contrast, the specks evenly and randomly spread with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1055, "async": true, "tile_x": true, "tile_y": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_grass/01_raw.png
file build/art/tiles_grass/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the paper is printed all over with an endless repeating pattern: a flat medium green ground covered with many small short specks of bright yellow-green and of dark green in strong contrast is present and readable at 1:1
- the the specks evenly and randomly spread with the same density everywhere is present and readable at 1:1
- it tiles with no seam: check a 3x3 montage, never a single tile
- no feature large enough to be noticed repeating across a field of tiles
- even brightness corner to corner, no vignette and no gradient
- it reads as the right ground at 1:1, not only enlarged

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_grass/01_raw.png assets/glory-of-rome/art/tiles/grass.png
```


## tiles_grass_variant

**Replaces:** `tiles/grass_variant.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat medium green ground covered with small specks of yellow-green and dark green, with a few slightly darker scrub flecks mixed evenly through it, everything spread with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_grass_variant
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat medium green ground covered with small specks of yellow-green and dark green, with a few slightly darker scrub flecks mixed evenly through it, everything spread with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1056, "async": true, "tile_x": true, "tile_y": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_grass_variant/01_raw.png
file build/art/tiles_grass_variant/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the paper is printed all over with an endless repeating pattern: a flat medium green ground covered with small specks of yellow-green and dark green is present and readable at 1:1
- the with a few slightly darker scrub flecks mixed evenly through it is present and readable at 1:1
- it tiles with no seam: check a 3x3 montage, never a single tile
- no feature large enough to be noticed repeating across a field of tiles
- even brightness corner to corner, no vignette and no gradient
- it reads as the right ground at 1:1, not only enlarged

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_grass_variant/01_raw.png assets/glory-of-rome/art/tiles/grass_variant.png
```


## tiles_mountain

**Replaces:** `tiles/mountain.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat mid grey ground covered with many small angular chips of light grey and dark grey in strong contrast, the chips spread evenly and randomly with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_mountain
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat mid grey ground covered with many small angular chips of light grey and dark grey in strong contrast, the chips spread evenly and randomly with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1058, "async": true, "tile_x": true, "tile_y": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_mountain/01_raw.png
file build/art/tiles_mountain/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the paper is printed all over with an endless repeating pattern: a flat mid grey ground covered with many small angular chips of light grey and dark grey in strong contrast is present and readable at 1:1
- the the chips spread evenly and randomly with the same density everywhere is present and readable at 1:1
- it tiles with no seam: check a 3x3 montage, never a single tile
- no feature large enough to be noticed repeating across a field of tiles
- even brightness corner to corner, no vignette and no gradient
- it reads as the right ground at 1:1, not only enlarged

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_mountain/01_raw.png assets/glory-of-rome/art/tiles/mountain.png
```


## tiles_sign

**Replaces:** `tiles/sign.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman milestone, a cylindrical stone column with carved lettering, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_sign
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman milestone, a cylindrical stone column with carved lettering, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1070, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_sign/01_raw.png
file build/art/tiles_sign/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a cylindrical stone column with carved lettering is present and readable at 1:1
- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_sign/01_raw.png assets/glory-of-rome/art/tiles/sign.png
```


## tiles_town

**Replaces:** `tiles/town.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small walled Roman town with terracotta roofs, a temple pediment and a column, seen from a high angle, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_town
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small walled Roman town with terracotta roofs, a temple pediment and a column, seen from a high angle, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1062, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_town/01_raw.png
file build/art/tiles_town/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a temple pediment and a column is present and readable at 1:1
- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_town/01_raw.png assets/glory-of-rome/art/tiles/town.png
```


## tiles_wandering_army

**Replaces:** `tiles/wandering_army.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a barbarian war standard planted in the ground, a spear hung with skulls and a horsehair tuft, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_wandering_army
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a barbarian war standard planted in the ground, a spear hung with skulls and a horsehair tuft, standing on short dry Mediterranean grass, the grass filling the whole picture right to every edge behind it", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1073, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_wandering_army/01_raw.png
file build/art/tiles_wandering_army/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a spear hung with skulls and a horsehair tuft is present and readable at 1:1
- the the grass filling the whole picture right to every edge behind it is present and readable at 1:1
- the subject sits on grass that fills the frame to every edge
- the grass matches `tiles/grass.png` closely enough to sit beside it
- the image is fully opaque: the map draws this tile alone, with no ground behind it
- the subject is complete and not cut by any edge

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_wandering_army/01_raw.png assets/glory-of-rome/art/tiles/wandering_army.png
```


## tiles_water

**Replaces:** `tiles/water.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat medium blue ground covered with many small short horizontal dashes of bright pale cyan in strong contrast, the dashes evenly and randomly spread with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/tiles_water
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat medium blue ground covered with many small short horizontal dashes of bright pale cyan in strong contrast, the dashes evenly and randomly spread with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1059, "async": true, "tile_x": true, "tile_y": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/tiles_water/01_raw.png
file build/art/tiles_water/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the paper is printed all over with an endless repeating pattern: a flat medium blue ground covered with many small short horizontal dashes of bright pale cyan in strong contrast is present and readable at 1:1
- the the dashes evenly and randomly spread with the same density everywhere is present and readable at 1:1
- it tiles with no seam: check a 3x3 montage, never a single tile
- no feature large enough to be noticed repeating across a field of tiles
- even brightness corner to corner, no vignette and no gradient
- it reads as the right ground at 1:1, not only enlarged

**Step 3 — copy into the pack.**


```sh
cp build/art/tiles_water/01_raw.png assets/glory-of-rome/art/tiles/water.png
```


## troops_archers_00_03

**Replaces:** `troops/archers_00.png`, `troops/archers_01.png`, `troops/archers_02.png`, `troops/archers_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Roman velite skirmisher in a wolfskin headdress over a helmet, small round parma shield, throwing a javelin

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_archers_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman velite skirmisher in a wolfskin headdress over a helmet, small round parma shield, throwing a javelin", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1008, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_archers_00_03/01_still.png
file build/art/troops_archers_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the small round parma shield is present and readable at 1:1
- the throwing a javelin is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_archers_00_03/01_still.png)" \
  '{prompt: "hurling the javelin overarm, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_archers_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_archers_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_archers_00_03/02_sheet.png
file build/art/troops_archers_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_archers_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_archers_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_archers_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_archers_00_03/frame_00.png assets/glory-of-rome/art/troops/archers_00.png
cp build/art/troops_archers_00_03/frame_01.png assets/glory-of-rome/art/troops/archers_01.png
cp build/art/troops_archers_00_03/frame_02.png assets/glory-of-rome/art/troops/archers_02.png
cp build/art/troops_archers_00_03/frame_03.png assets/glory-of-rome/art/troops/archers_03.png
```


## troops_archmages_00_03

**Replaces:** `troops/archmages_00.png`, `troops/archmages_01.png`, `troops/archmages_02.png`, `troops/archmages_03.png`

**State:** still generated 2026-09-03 at seed 1020 and accepted (96x96, 37 colours, figure on rows 7-88); the four pack files are still placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a winged Fury, a gaunt female spirit with dark feathered wings, snakes in her hair, clutching a burning torch, hovering

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_archmages_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a winged Fury, a gaunt female spirit with dark feathered wings, snakes in her hair, clutching a burning torch, hovering", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1020, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_archmages_00_03/01_still.png
file build/art/troops_archmages_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the a gaunt female spirit with dark feathered wings is present and readable at 1:1
- the snakes in her hair is present and readable at 1:1
- the clutching a burning torch is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_archmages_00_03/01_still.png)" \
  '{prompt: "surging forward to strike, wings beating, then drifting back",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_archmages_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_archmages_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_archmages_00_03/02_sheet.png
file build/art/troops_archmages_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_archmages_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_archmages_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_archmages_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_archmages_00_03/frame_00.png assets/glory-of-rome/art/troops/archmages_00.png
cp build/art/troops_archmages_00_03/frame_01.png assets/glory-of-rome/art/troops/archmages_01.png
cp build/art/troops_archmages_00_03/frame_02.png assets/glory-of-rome/art/troops/archmages_02.png
cp build/art/troops_archmages_00_03/frame_03.png assets/glory-of-rome/art/troops/archmages_03.png
```


## troops_barbarians_00_03

**Replaces:** `troops/barbarians_00.png`, `troops/barbarians_01.png`, `troops/barbarians_02.png`, `troops/barbarians_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Sarmatian steppe warrior in full scale armour of overlapping plates, conical helmet, long lance, fur-trimmed cloak

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_barbarians_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Sarmatian steppe warrior in full scale armour of overlapping plates, conical helmet, long lance, fur-trimmed cloak", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1016, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_barbarians_00_03/01_still.png
file build/art/troops_barbarians_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the conical helmet is present and readable at 1:1
- the long lance is present and readable at 1:1
- the fur-trimmed cloak is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_barbarians_00_03/01_still.png)" \
  '{prompt: "couching the long lance and driving it forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_barbarians_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_barbarians_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_barbarians_00_03/02_sheet.png
file build/art/troops_barbarians_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_barbarians_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_barbarians_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_barbarians_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_barbarians_00_03/frame_00.png assets/glory-of-rome/art/troops/barbarians_00.png
cp build/art/troops_barbarians_00_03/frame_01.png assets/glory-of-rome/art/troops/barbarians_01.png
cp build/art/troops_barbarians_00_03/frame_02.png assets/glory-of-rome/art/troops/barbarians_02.png
cp build/art/troops_barbarians_00_03/frame_03.png assets/glory-of-rome/art/troops/barbarians_03.png
```


## troops_cavalry_00_03

**Replaces:** `troops/cavalry_00.png`, `troops/cavalry_01.png`, `troops/cavalry_02.png`, `troops/cavalry_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Roman cavalryman on a galloping horse, oval shield and crested helmet, couched lance, cloak streaming behind

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_cavalry_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman cavalryman on a galloping horse, oval shield and crested helmet, couched lance, cloak streaming behind", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1018, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_cavalry_00_03/01_still.png
file build/art/troops_cavalry_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the oval shield and crested helmet is present and readable at 1:1
- the couched lance is present and readable at 1:1
- the cloak streaming behind is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_cavalry_00_03/01_still.png)" \
  '{prompt: "spurring the horse forward and striking with the lance, then reining back",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_cavalry_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_cavalry_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_cavalry_00_03/02_sheet.png
file build/art/troops_cavalry_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_cavalry_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_cavalry_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_cavalry_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_cavalry_00_03/frame_00.png assets/glory-of-rome/art/troops/cavalry_00.png
cp build/art/troops_cavalry_00_03/frame_01.png assets/glory-of-rome/art/troops/cavalry_01.png
cp build/art/troops_cavalry_00_03/frame_02.png assets/glory-of-rome/art/troops/cavalry_02.png
cp build/art/troops_cavalry_00_03/frame_03.png assets/glory-of-rome/art/troops/cavalry_03.png
```


## troops_demons_00_03

**Replaces:** `troops/demons_00.png`, `troops/demons_01.png`, `troops/demons_02.png`, `troops/demons_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a shape-shifting devourer of Hecate, a lean winged demon with one bronze leg, bat wings, swinging a scythe

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_demons_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a shape-shifting devourer of Hecate, a lean winged demon with one bronze leg, bat wings, swinging a scythe", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1023, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_demons_00_03/01_still.png
file build/art/troops_demons_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the a lean winged demon with one bronze leg is present and readable at 1:1
- the bat wings is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_demons_00_03/01_still.png)" \
  '{prompt: "swinging the scythe across, wings beating",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_demons_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_demons_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_demons_00_03/02_sheet.png
file build/art/troops_demons_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_demons_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_demons_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_demons_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_demons_00_03/frame_00.png assets/glory-of-rome/art/troops/demons_00.png
cp build/art/troops_demons_00_03/frame_01.png assets/glory-of-rome/art/troops/demons_01.png
cp build/art/troops_demons_00_03/frame_02.png assets/glory-of-rome/art/troops/demons_02.png
cp build/art/troops_demons_00_03/frame_03.png assets/glory-of-rome/art/troops/demons_03.png
```


## troops_dragons_00_03

**Replaces:** `troops/dragons_00.png`, `troops/dragons_01.png`, `troops/dragons_02.png`, `troops/dragons_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a great scaled dragon, wings spread, long serpentine neck, jaws open, standing on clawed feet

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_dragons_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a great scaled dragon, wings spread, long serpentine neck, jaws open, standing on clawed feet", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1024, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_dragons_00_03/01_still.png
file build/art/troops_dragons_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the wings spread is present and readable at 1:1
- the long serpentine neck is present and readable at 1:1
- the jaws open is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_dragons_00_03/01_still.png)" \
  '{prompt: "lunging forward and striking with its jaws, then settling back",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_dragons_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_dragons_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_dragons_00_03/02_sheet.png
file build/art/troops_dragons_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_dragons_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_dragons_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_dragons_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_dragons_00_03/frame_00.png assets/glory-of-rome/art/troops/dragons_00.png
cp build/art/troops_dragons_00_03/frame_01.png assets/glory-of-rome/art/troops/dragons_01.png
cp build/art/troops_dragons_00_03/frame_02.png assets/glory-of-rome/art/troops/dragons_02.png
cp build/art/troops_dragons_00_03/frame_03.png assets/glory-of-rome/art/troops/dragons_03.png
```


## troops_druids_00_03

**Replaces:** `troops/druids_00.png`, `troops/druids_01.png`, `troops/druids_02.png`, `troops/druids_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Celtic druid in a long white robe, oak-leaf wreath, holding a golden sickle and a gnarled staff

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_druids_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Celtic druid in a long white robe, oak-leaf wreath, holding a golden sickle and a gnarled staff", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1019, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_druids_00_03/01_still.png
file build/art/troops_druids_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the oak-leaf wreath is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_druids_00_03/01_still.png)" \
  '{prompt: "raising the staff and casting, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_druids_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_druids_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_druids_00_03/02_sheet.png
file build/art/troops_druids_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_druids_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_druids_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_druids_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_druids_00_03/frame_00.png assets/glory-of-rome/art/troops/druids_00.png
cp build/art/troops_druids_00_03/frame_01.png assets/glory-of-rome/art/troops/druids_01.png
cp build/art/troops_druids_00_03/frame_02.png assets/glory-of-rome/art/troops/druids_02.png
cp build/art/troops_druids_00_03/frame_03.png assets/glory-of-rome/art/troops/druids_03.png
```


## troops_dwarves_00_03

**Replaces:** `troops/dwarves_00.png`, `troops/dwarves_01.png`, `troops/dwarves_02.png`, `troops/dwarves_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a stocky Ligurian mountain tribesman, thick beard, fur cloak over a leather cuirass, wielding a heavy axe

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_dwarves_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a stocky Ligurian mountain tribesman, thick beard, fur cloak over a leather cuirass, wielding a heavy axe", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1012, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_dwarves_00_03/01_still.png
file build/art/troops_dwarves_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the thick beard is present and readable at 1:1
- the fur cloak over a leather cuirass is present and readable at 1:1
- the wielding a heavy axe is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_dwarves_00_03/01_still.png)" \
  '{prompt: "swinging the weapon overhead and striking down, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_dwarves_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_dwarves_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_dwarves_00_03/02_sheet.png
file build/art/troops_dwarves_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_dwarves_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_dwarves_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_dwarves_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_dwarves_00_03/frame_00.png assets/glory-of-rome/art/troops/dwarves_00.png
cp build/art/troops_dwarves_00_03/frame_01.png assets/glory-of-rome/art/troops/dwarves_01.png
cp build/art/troops_dwarves_00_03/frame_02.png assets/glory-of-rome/art/troops/dwarves_02.png
cp build/art/troops_dwarves_00_03/frame_03.png assets/glory-of-rome/art/troops/dwarves_03.png
```


## troops_elves_00_03

**Replaces:** `troops/elves_00.png`, `troops/elves_01.png`, `troops/elves_02.png`, `troops/elves_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a forest spirit archer in bark-toned robes with leaf-patterned cloak, drawing a longbow, antlered circlet

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_elves_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a forest spirit archer in bark-toned robes with leaf-patterned cloak, drawing a longbow, antlered circlet", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1009, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_elves_00_03/01_still.png
file build/art/troops_elves_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the antlered circlet is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_elves_00_03/01_still.png)" \
  '{prompt: "drawing the bow and loosing an arrow, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_elves_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_elves_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_elves_00_03/02_sheet.png
file build/art/troops_elves_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_elves_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_elves_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_elves_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_elves_00_03/frame_00.png assets/glory-of-rome/art/troops/elves_00.png
cp build/art/troops_elves_00_03/frame_01.png assets/glory-of-rome/art/troops/elves_01.png
cp build/art/troops_elves_00_03/frame_02.png assets/glory-of-rome/art/troops/elves_02.png
cp build/art/troops_elves_00_03/frame_03.png assets/glory-of-rome/art/troops/elves_03.png
```


## troops_ghosts_00_03

**Replaces:** `troops/ghosts_00.png`, `troops/ghosts_01.png`, `troops/ghosts_02.png`, `troops/ghosts_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a translucent ancestral shade, a hollow robed figure with no legs, trailing into vapour, faintly glowing

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_ghosts_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a translucent ancestral shade, a hollow robed figure with no legs, trailing into vapour, faintly glowing", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1013, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_ghosts_00_03/01_still.png
file build/art/troops_ghosts_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the a hollow robed figure with no legs is present and readable at 1:1
- the trailing into vapour is present and readable at 1:1
- the faintly glowing is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_ghosts_00_03/01_still.png)" \
  '{prompt: "surging forward to strike, then drifting back",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_ghosts_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_ghosts_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_ghosts_00_03/02_sheet.png
file build/art/troops_ghosts_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_ghosts_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_ghosts_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_ghosts_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_ghosts_00_03/frame_00.png assets/glory-of-rome/art/troops/ghosts_00.png
cp build/art/troops_ghosts_00_03/frame_01.png assets/glory-of-rome/art/troops/ghosts_01.png
cp build/art/troops_ghosts_00_03/frame_02.png assets/glory-of-rome/art/troops/ghosts_02.png
cp build/art/troops_ghosts_00_03/frame_03.png assets/glory-of-rome/art/troops/ghosts_03.png
```


## troops_giants_00_03

**Replaces:** `troops/giants_00.png`, `troops/giants_01.png`, `troops/giants_02.png`, `troops/giants_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a towering giant with serpent-scaled legs, wild hair and beard, raising a boulder overhead to throw

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_giants_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a towering giant with serpent-scaled legs, wild hair and beard, raising a boulder overhead to throw", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1022, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_giants_00_03/01_still.png
file build/art/troops_giants_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the wild hair and beard is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_giants_00_03/01_still.png)" \
  '{prompt: "hurling the boulder forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_giants_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_giants_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_giants_00_03/02_sheet.png
file build/art/troops_giants_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_giants_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_giants_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_giants_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_giants_00_03/frame_00.png assets/glory-of-rome/art/troops/giants_00.png
cp build/art/troops_giants_00_03/frame_01.png assets/glory-of-rome/art/troops/giants_01.png
cp build/art/troops_giants_00_03/frame_02.png assets/glory-of-rome/art/troops/giants_02.png
cp build/art/troops_giants_00_03/frame_03.png assets/glory-of-rome/art/troops/giants_03.png
```


## troops_gnomes_00_03

**Replaces:** `troops/gnomes_00.png`, `troops/gnomes_01.png`, `troops/gnomes_02.png`, `troops/gnomes_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a small woodland faun, goat legs and curling horns, shaggy pelt, holding a crooked branch, mischievous

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_gnomes_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small woodland faun, goat legs and curling horns, shaggy pelt, holding a crooked branch, mischievous", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1006, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_gnomes_00_03/01_still.png
file build/art/troops_gnomes_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the goat legs and curling horns is present and readable at 1:1
- the shaggy pelt is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_gnomes_00_03/01_still.png)" \
  '{prompt: "swinging the crooked branch, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_gnomes_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_gnomes_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_gnomes_00_03/02_sheet.png
file build/art/troops_gnomes_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_gnomes_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_gnomes_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_gnomes_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_gnomes_00_03/frame_00.png assets/glory-of-rome/art/troops/gnomes_00.png
cp build/art/troops_gnomes_00_03/frame_01.png assets/glory-of-rome/art/troops/gnomes_01.png
cp build/art/troops_gnomes_00_03/frame_02.png assets/glory-of-rome/art/troops/gnomes_02.png
cp build/art/troops_gnomes_00_03/frame_03.png assets/glory-of-rome/art/troops/gnomes_03.png
```


## troops_knights_00_03

**Replaces:** `troops/knights_00.png`, `troops/knights_01.png`, `troops/knights_02.png`, `troops/knights_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> an elite Praetorian guardsman in polished ornate muscled cuirass, tall transverse-crested helmet, oval shield with scorpion emblem, gladius drawn

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_knights_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an elite Praetorian guardsman in polished ornate muscled cuirass, tall transverse-crested helmet, oval shield with scorpion emblem, gladius drawn", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1014, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_knights_00_03/01_still.png
file build/art/troops_knights_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the tall transverse-crested helmet is present and readable at 1:1
- the oval shield with scorpion emblem is present and readable at 1:1
- the gladius drawn is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_knights_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_knights_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_knights_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_knights_00_03/02_sheet.png
file build/art/troops_knights_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_knights_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_knights_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_knights_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_knights_00_03/frame_00.png assets/glory-of-rome/art/troops/knights_00.png
cp build/art/troops_knights_00_03/frame_01.png assets/glory-of-rome/art/troops/knights_01.png
cp build/art/troops_knights_00_03/frame_02.png assets/glory-of-rome/art/troops/knights_02.png
cp build/art/troops_knights_00_03/frame_03.png assets/glory-of-rome/art/troops/knights_03.png
```


## troops_militia_00_03

**Replaces:** `troops/militia_00.png`, `troops/militia_01.png`, `troops/militia_02.png`, `troops/militia_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a raw Roman recruit in a plain undyed tunic and simple leather cap, holding a short spear and a small round shield, standing stiffly

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_militia_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a raw Roman recruit in a plain undyed tunic and simple leather cap, holding a short spear and a small round shield, standing stiffly", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1002, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_militia_00_03/01_still.png
file build/art/troops_militia_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the a raw Roman recruit in a plain undyed tunic and simple leather cap is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_militia_00_03/01_still.png)" \
  '{prompt: "levelling the spear and thrusting it forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_militia_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_militia_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_militia_00_03/02_sheet.png
file build/art/troops_militia_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_militia_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_militia_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_militia_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_militia_00_03/frame_00.png assets/glory-of-rome/art/troops/militia_00.png
cp build/art/troops_militia_00_03/frame_01.png assets/glory-of-rome/art/troops/militia_01.png
cp build/art/troops_militia_00_03/frame_02.png assets/glory-of-rome/art/troops/militia_02.png
cp build/art/troops_militia_00_03/frame_03.png assets/glory-of-rome/art/troops/militia_03.png
```


## troops_nomads_00_03

**Replaces:** `troops/nomads_00.png`, `troops/nomads_01.png`, `troops/nomads_02.png`, `troops/nomads_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Numidian light horseman riding bareback, no saddle or bridle, bare-chested in a leopard skin, hurling a javelin

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_nomads_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Numidian light horseman riding bareback, no saddle or bridle, bare-chested in a leopard skin, hurling a javelin", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1011, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_nomads_00_03/01_still.png
file build/art/troops_nomads_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the no saddle or bridle is present and readable at 1:1
- the bare-chested in a leopard skin is present and readable at 1:1
- the hurling a javelin is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_nomads_00_03/01_still.png)" \
  '{prompt: "urging the horse forward and hurling the javelin, then reining back",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_nomads_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_nomads_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_nomads_00_03/02_sheet.png
file build/art/troops_nomads_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_nomads_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_nomads_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_nomads_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_nomads_00_03/frame_00.png assets/glory-of-rome/art/troops/nomads_00.png
cp build/art/troops_nomads_00_03/frame_01.png assets/glory-of-rome/art/troops/nomads_01.png
cp build/art/troops_nomads_00_03/frame_02.png assets/glory-of-rome/art/troops/nomads_02.png
cp build/art/troops_nomads_00_03/frame_03.png assets/glory-of-rome/art/troops/nomads_03.png
```


## troops_ogres_00_03

**Replaces:** `troops/ogres_00.png`, `troops/ogres_01.png`, `troops/ogres_02.png`, `troops/ogres_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a huge one-eyed cave giant, single central eye, bare muscled torso, a heavy leather smith apron, swinging a massive hammer

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_ogres_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a huge one-eyed cave giant, single central eye, bare muscled torso, a heavy leather smith apron, swinging a massive hammer", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1015, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_ogres_00_03/01_still.png
file build/art/troops_ogres_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the single central eye is present and readable at 1:1
- the bare muscled torso is present and readable at 1:1
- the a heavy leather smith apron is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_ogres_00_03/01_still.png)" \
  '{prompt: "swinging the weapon overhead and striking down, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_ogres_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_ogres_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_ogres_00_03/02_sheet.png
file build/art/troops_ogres_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_ogres_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_ogres_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_ogres_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_ogres_00_03/frame_00.png assets/glory-of-rome/art/troops/ogres_00.png
cp build/art/troops_ogres_00_03/frame_01.png assets/glory-of-rome/art/troops/ogres_01.png
cp build/art/troops_ogres_00_03/frame_02.png assets/glory-of-rome/art/troops/ogres_02.png
cp build/art/troops_ogres_00_03/frame_03.png assets/glory-of-rome/art/troops/ogres_03.png
```


## troops_orcs_00_03

**Replaces:** `troops/orcs_00.png`, `troops/orcs_01.png`, `troops/orcs_02.png`, `troops/orcs_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Balearic slinger, bare-chested and wiry, whirling a leather sling above his head, pouch of stones at his hip

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_orcs_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Balearic slinger, bare-chested and wiry, whirling a leather sling above his head, pouch of stones at his hip", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1007, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_orcs_00_03/01_still.png
file build/art/troops_orcs_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the bare-chested and wiry is present and readable at 1:1
- the pouch of stones at his hip is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_orcs_00_03/01_still.png)" \
  '{prompt: "whirling the sling and releasing the stone, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_orcs_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_orcs_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_orcs_00_03/02_sheet.png
file build/art/troops_orcs_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_orcs_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_orcs_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_orcs_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_orcs_00_03/frame_00.png assets/glory-of-rome/art/troops/orcs_00.png
cp build/art/troops_orcs_00_03/frame_01.png assets/glory-of-rome/art/troops/orcs_01.png
cp build/art/troops_orcs_00_03/frame_02.png assets/glory-of-rome/art/troops/orcs_02.png
cp build/art/troops_orcs_00_03/frame_03.png assets/glory-of-rome/art/troops/orcs_03.png
```


## troops_peasants_00_03

**Replaces:** `troops/peasants_00.png`, `troops/peasants_01.png`, `troops/peasants_02.png`, `troops/peasants_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a ragged Roman tenant farmer in a torn dirty tunic, barefoot, holding a wooden pitchfork, stooped and unarmoured

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_peasants_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a ragged Roman tenant farmer in a torn dirty tunic, barefoot, holding a wooden pitchfork, stooped and unarmoured", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_peasants_00_03/01_still.png
file build/art/troops_peasants_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the stooped and unarmoured is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_peasants_00_03/01_still.png)" \
  '{prompt: "jabbing the pitchfork forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_peasants_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_peasants_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_peasants_00_03/02_sheet.png
file build/art/troops_peasants_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_peasants_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_peasants_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_peasants_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_peasants_00_03/frame_00.png assets/glory-of-rome/art/troops/peasants_00.png
cp build/art/troops_peasants_00_03/frame_01.png assets/glory-of-rome/art/troops/peasants_01.png
cp build/art/troops_peasants_00_03/frame_02.png assets/glory-of-rome/art/troops/peasants_02.png
cp build/art/troops_peasants_00_03/frame_03.png assets/glory-of-rome/art/troops/peasants_03.png
```


## troops_pikemen_00_03

**Replaces:** `troops/pikemen_00.png`, `troops/pikemen_01.png`, `troops/pikemen_02.png`, `troops/pikemen_03.png`

**State:** done, pending your approval. Still at
`build/art/hastati_minimal/run02/01_raw.png`; four attack frames at
`build/art/hastati_custom4/run01/frame_00..03.png`. The four pack files are
still placeholder.

**Prompt** (subject only; the style supplies framing, pose and background)

> A Roman legionary with a red crested helmet and a tall rectangular red and gold shield, holding a spear, stocky and broad shouldered

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_pikemen_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "A Roman legionary with a red crested helmet and a tall rectangular red and gold shield, holding a spear, stocky and broad shouldered", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1101, "remove_bg": true, "return_non_bg_removed": true, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_pikemen_00_03/01_still.png
file build/art/troops_pikemen_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the banded lorica segmentata reads as bands at 1:1
- the crested galea is present, crest clear of the top row
- the tall rectangular scutum is on his left arm, the spear in his right hand
- both feet are drawn and he stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.25, `rd_advanced_animation__custom_action`
at **`frames_duration: 4`**. `__attack` was tried four times on this subject and
chose to raise the shield every time; `custom_action` takes the motion as
described. Four frames, not six: six loses the spear entirely in the middle of
the swing.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_pikemen_00_03/01_still.png)" \
  '{prompt: "the spear swings down from upright to horizontal and drives forward past the shield, both feet stay planted, the shield stays still",
    prompt_style: "rd_advanced_animation__custom_action",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_pikemen_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_pikemen_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_pikemen_00_03/02_sheet.png
file build/art/troops_pikemen_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — accept the animation by eye.** The silhouette must widen frame to
frame as the spear comes down and extends. The accepted run measured 47, 60, 62
and 81 pixels wide; the reference pikeman's thrust is 80. The spear must be
present in every frame and both feet planted throughout.

**Step 5 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_pikemen_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_pikemen_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_pikemen_00_03/frame_0*.png   # four files, each 96x96
```

**Step 6 — copy into the pack.**

```sh
cp build/art/troops_pikemen_00_03/frame_00.png assets/glory-of-rome/art/troops/pikemen_00.png
cp build/art/troops_pikemen_00_03/frame_01.png assets/glory-of-rome/art/troops/pikemen_01.png
cp build/art/troops_pikemen_00_03/frame_02.png assets/glory-of-rome/art/troops/pikemen_02.png
cp build/art/troops_pikemen_00_03/frame_03.png assets/glory-of-rome/art/troops/pikemen_03.png
```

## troops_skeletons_00_03

**Replaces:** `troops/skeletons_00.png`, `troops/skeletons_01.png`, `troops/skeletons_02.png`, `troops/skeletons_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a walking human skeleton in a rotted Roman tunic, hollow eye sockets, carrying a rusted short sword

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_skeletons_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a walking human skeleton in a rotted Roman tunic, hollow eye sockets, carrying a rusted short sword", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1004, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_skeletons_00_03/01_still.png
file build/art/troops_skeletons_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the hollow eye sockets is present and readable at 1:1
- the carrying a rusted short sword is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_skeletons_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_skeletons_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_skeletons_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_skeletons_00_03/02_sheet.png
file build/art/troops_skeletons_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_skeletons_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_skeletons_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_skeletons_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_skeletons_00_03/frame_00.png assets/glory-of-rome/art/troops/skeletons_00.png
cp build/art/troops_skeletons_00_03/frame_01.png assets/glory-of-rome/art/troops/skeletons_01.png
cp build/art/troops_skeletons_00_03/frame_02.png assets/glory-of-rome/art/troops/skeletons_02.png
cp build/art/troops_skeletons_00_03/frame_03.png assets/glory-of-rome/art/troops/skeletons_03.png
```


## troops_sprites_00_03

**Replaces:** `troops/sprites_00.png`, `troops/sprites_01.png`, `troops/sprites_02.png`, `troops/sprites_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a tiny glowing household spirit, a small floating robed figure with a faint halo, translucent and weightless, hovering above the ground

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_sprites_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a tiny glowing household spirit, a small floating robed figure with a faint halo, translucent and weightless, hovering above the ground", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1001, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_sprites_00_03/01_still.png
file build/art/troops_sprites_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the a small floating robed figure with a faint halo is present and readable at 1:1
- the translucent and weightless is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_sprites_00_03/01_still.png)" \
  '{prompt: "darting forward to strike, then drifting back",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_sprites_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_sprites_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_sprites_00_03/02_sheet.png
file build/art/troops_sprites_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_sprites_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_sprites_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_sprites_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_sprites_00_03/frame_00.png assets/glory-of-rome/art/troops/sprites_00.png
cp build/art/troops_sprites_00_03/frame_01.png assets/glory-of-rome/art/troops/sprites_01.png
cp build/art/troops_sprites_00_03/frame_02.png assets/glory-of-rome/art/troops/sprites_02.png
cp build/art/troops_sprites_00_03/frame_03.png assets/glory-of-rome/art/troops/sprites_03.png
```


## troops_trolls_00_03

**Replaces:** `troops/trolls_00.png`, `troops/trolls_01.png`, `troops/trolls_02.png`, `troops/trolls_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a hulking earth-giant, hunched, skin caked in soil and moss, enormous hands, regenerating wounds glowing faintly

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_trolls_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a hulking earth-giant, hunched, skin caked in soil and moss, enormous hands, regenerating wounds glowing faintly", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1017, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_trolls_00_03/01_still.png
file build/art/troops_trolls_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the skin caked in soil and moss is present and readable at 1:1
- the enormous hands is present and readable at 1:1
- the regenerating wounds glowing faintly is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_trolls_00_03/01_still.png)" \
  '{prompt: "swinging both fists down, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_trolls_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_trolls_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_trolls_00_03/02_sheet.png
file build/art/troops_trolls_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_trolls_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_trolls_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_trolls_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_trolls_00_03/frame_00.png assets/glory-of-rome/art/troops/trolls_00.png
cp build/art/troops_trolls_00_03/frame_01.png assets/glory-of-rome/art/troops/trolls_01.png
cp build/art/troops_trolls_00_03/frame_02.png assets/glory-of-rome/art/troops/trolls_02.png
cp build/art/troops_trolls_00_03/frame_03.png assets/glory-of-rome/art/troops/trolls_03.png
```


## troops_vampires_00_03

**Replaces:** `troops/vampires_00.png`, `troops/vampires_01.png`, `troops/vampires_02.png`, `troops/vampires_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a screech-owl vampire, a hunched winged creature with owl features and a human face, bloodied talons, membranous wings spread

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_vampires_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a screech-owl vampire, a hunched winged creature with owl features and a human face, bloodied talons, membranous wings spread", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1021, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_vampires_00_03/01_still.png
file build/art/troops_vampires_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the a hunched winged creature with owl features and a human face is present and readable at 1:1
- the bloodied talons is present and readable at 1:1
- the membranous wings spread is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_vampires_00_03/01_still.png)" \
  '{prompt: "diving forward and raking with its talons, wings spread",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_vampires_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_vampires_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_vampires_00_03/02_sheet.png
file build/art/troops_vampires_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_vampires_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_vampires_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_vampires_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_vampires_00_03/frame_00.png assets/glory-of-rome/art/troops/vampires_00.png
cp build/art/troops_vampires_00_03/frame_01.png assets/glory-of-rome/art/troops/vampires_01.png
cp build/art/troops_vampires_00_03/frame_02.png assets/glory-of-rome/art/troops/vampires_02.png
cp build/art/troops_vampires_00_03/frame_03.png assets/glory-of-rome/art/troops/vampires_03.png
```


## troops_wolves_00_03

**Replaces:** `troops/wolves_00.png`, `troops/wolves_01.png`, `troops/wolves_02.png`, `troops/wolves_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a lean grey wolf, snarling, head low, in mid-prowl, seen from the side

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_wolves_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a lean grey wolf, snarling, head low, in mid-prowl, seen from the side", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1003, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_wolves_00_03/01_still.png
file build/art/troops_wolves_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the head low is present and readable at 1:1
- the in mid-prowl is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_wolves_00_03/01_still.png)" \
  '{prompt: "lunging forward and striking with its jaws, then settling back",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_wolves_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_wolves_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_wolves_00_03/02_sheet.png
file build/art/troops_wolves_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_wolves_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_wolves_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_wolves_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_wolves_00_03/frame_00.png assets/glory-of-rome/art/troops/wolves_00.png
cp build/art/troops_wolves_00_03/frame_01.png assets/glory-of-rome/art/troops/wolves_01.png
cp build/art/troops_wolves_00_03/frame_02.png assets/glory-of-rome/art/troops/wolves_02.png
cp build/art/troops_wolves_00_03/frame_03.png assets/glory-of-rome/art/troops/wolves_03.png
```


## troops_zombies_00_03

**Replaces:** `troops/zombies_00.png`, `troops/zombies_01.png`, `troops/zombies_02.png`, `troops/zombies_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a shambling rotted corpse in grave wrappings, arms hanging, hunched and slow, grey-green flesh

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/troops_zombies_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a shambling rotted corpse in grave wrappings, arms hanging, hunched and slow, grey-green flesh", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1005, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_zombies_00_03/01_still.png
file build/art/troops_zombies_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the arms hanging is present and readable at 1:1
- the hunched and slow is present and readable at 1:1
- the grey-green flesh is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/troops_zombies_00_03/01_still.png)" \
  '{prompt: "lurching forward and clawing, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/troops_zombies_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/troops_zombies_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/troops_zombies_00_03/02_sheet.png
file build/art/troops_zombies_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/troops_zombies_00_03/02_sheet.png -crop 96x96 +repage build/art/troops_zombies_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/troops_zombies_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/troops_zombies_00_03/frame_00.png assets/glory-of-rome/art/troops/zombies_00.png
cp build/art/troops_zombies_00_03/frame_01.png assets/glory-of-rome/art/troops/zombies_01.png
cp build/art/troops_zombies_00_03/frame_02.png assets/glory-of-rome/art/troops/zombies_02.png
cp build/art/troops_zombies_00_03/frame_03.png assets/glory-of-rome/art/troops/zombies_03.png
```


## ui_backdrop_castle

**Replaces:** `ui/backdrop_castle.png`

**State:** placeholder. Design size 240x102 → generate at 480x204.

**Prompt**

> a fortress hall interior with hanging legionary standards, a brazier and stone arches

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_backdrop_castle
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a fortress hall interior with hanging legionary standards, a brazier and stone arches", "prompt_style": "rd_plus__environment", "width": 480, "height": 204, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_backdrop_castle/01_raw.png
file build/art/ui_backdrop_castle/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a brazier and stone arches is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_backdrop_castle/01_raw.png assets/glory-of-rome/art/ui/backdrop_castle.png
```


## ui_backdrop_dungeon

**Replaces:** `ui/backdrop_dungeon.png`

**State:** placeholder. Design size 240x102 → generate at 480x204.

**Prompt**

> a crypt interior with columbarium niches and torchlight

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_backdrop_dungeon
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a crypt interior with columbarium niches and torchlight", "prompt_style": "rd_plus__environment", "width": 480, "height": 204, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_backdrop_dungeon/01_raw.png
file build/art/ui_backdrop_dungeon/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a crypt interior with columbarium niches and torchlight is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_backdrop_dungeon/01_raw.png assets/glory-of-rome/art/ui/backdrop_dungeon.png
```


## ui_backdrop_forest

**Replaces:** `ui/backdrop_forest.png`

**State:** placeholder. Design size 240x102 → generate at 480x204.

**Prompt**

> the interior of a dense sacred grove with shafts of light through the canopy

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_backdrop_forest
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the interior of a dense sacred grove with shafts of light through the canopy", "prompt_style": "rd_plus__environment", "width": 480, "height": 204, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_backdrop_forest/01_raw.png
file build/art/ui_backdrop_forest/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the interior of a dense sacred grove with shafts of light through the canopy is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_backdrop_forest/01_raw.png assets/glory-of-rome/art/ui/backdrop_forest.png
```


## ui_backdrop_hillcave

**Replaces:** `ui/backdrop_hillcave.png`

**State:** placeholder. Design size 240x102 → generate at 480x204.

**Prompt**

> a cave mouth in rocky hills with oracle smoke drifting from the opening

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_backdrop_hillcave
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a cave mouth in rocky hills with oracle smoke drifting from the opening", "prompt_style": "rd_plus__environment", "width": 480, "height": 204, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_backdrop_hillcave/01_raw.png
file build/art/ui_backdrop_hillcave/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a cave mouth in rocky hills with oracle smoke drifting from the opening is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_backdrop_hillcave/01_raw.png assets/glory-of-rome/art/ui/backdrop_hillcave.png
```


## ui_backdrop_plains

**Replaces:** `ui/backdrop_plains.png`

**State:** placeholder. Design size 240x102 → generate at 480x204.

**Prompt**

> open Italian countryside with cypress trees and distant blue hills

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_backdrop_plains
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "open Italian countryside with cypress trees and distant blue hills", "prompt_style": "rd_plus__environment", "width": 480, "height": 204, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_backdrop_plains/01_raw.png
file build/art/ui_backdrop_plains/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the open Italian countryside with cypress trees and distant blue hills is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_backdrop_plains/01_raw.png assets/glory-of-rome/art/ui/backdrop_plains.png
```


## ui_backdrop_town

**Replaces:** `ui/backdrop_town.png`

**State:** placeholder. Design size 240x102 → generate at 480x204.

**Prompt**

> a Roman forum street with a colonnade, market awnings and townspeople, wide view

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_backdrop_town
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman forum street with a colonnade, market awnings and townspeople, wide view", "prompt_style": "rd_plus__environment", "width": 480, "height": 204, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_backdrop_town/01_raw.png
file build/art/ui_backdrop_town/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the market awnings and townspeople is present and readable at 1:1
- the wide view is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_backdrop_town/01_raw.png assets/glory-of-rome/art/ui/backdrop_town.png
```


## ui_class_select_highlight

**Replaces:** `ui/class_select_highlight.png`

**State:** placeholder. Design size 42x44 → generate at 84x88.

**Prompt**

> a golden laurel selection frame, hollow centre

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_class_select_highlight
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden laurel selection frame, hollow centre", "prompt_style": "rd_plus__classic", "width": 84, "height": 88, "num_images": 1, "seed": 1110, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_class_select_highlight/01_raw.png
file build/art/ui_class_select_highlight/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the hollow centre is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_class_select_highlight/01_raw.png assets/glory-of-rome/art/ui/class_select_highlight.png
```


## ui_class_select_picker

**Replaces:** `ui/class_select_picker.png`

**State:** placeholder. Design size 288x184 → generate at 576x368.

**Prompt**

> four Roman figures posed together in a landscape -- a general, a praetorian, a veiled priestess and a fur-clad frontier commander -- in one illustrated scene

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_class_select_picker
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "four Roman figures posed together in a landscape -- a general, a praetorian, a veiled priestess and a fur-clad frontier commander -- in one illustrated scene", "prompt_style": "rd_plus__environment", "width": 576, "height": 368, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_class_select_picker/01_raw.png
file build/art/ui_class_select_picker/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a praetorian is present and readable at 1:1
- the a veiled priestess and a fur-clad frontier commander -- in one illustrated scene is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_class_select_picker/01_raw.png assets/glory-of-rome/art/ui/class_select_picker.png
```


## ui_end_carpet

**Replaces:** `ui/end_carpet.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: flat grey rectangular flagstones in regular courses with thin dark joints between them, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_end_carpet
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: flat grey rectangular flagstones in regular courses with thin dark joints between them, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1105, "async": true, "tile_x": true, "tile_y": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_end_carpet/01_raw.png
file build/art/ui_end_carpet/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the paper is printed all over with an endless repeating pattern: flat grey rectangular flagstones in regular courses with thin dark joints between them is present and readable at 1:1
- the the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere is present and readable at 1:1
- it tiles with no seam: check a 3x3 montage, never a single tile
- no feature large enough to be noticed repeating across a field of tiles
- even brightness corner to corner, no vignette and no gradient
- it reads as the right ground at 1:1, not only enlarged

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_end_carpet/01_raw.png assets/glory-of-rome/art/ui/end_carpet.png
```


## ui_end_grass

**Replaces:** `ui/end_grass.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat green ground covered with small even specks of lighter and darker green, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_end_grass
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat green ground covered with small even specks of lighter and darker green, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1104, "async": true, "tile_x": true, "tile_y": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_end_grass/01_raw.png
file build/art/ui_end_grass/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the the paper is printed all over with an endless repeating pattern: a flat green ground covered with small even specks of lighter and darker green is present and readable at 1:1
- the the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere is present and readable at 1:1
- it tiles with no seam: check a 3x3 montage, never a single tile
- no feature large enough to be noticed repeating across a field of tiles
- even brightness corner to corner, no vignette and no gradient
- it reads as the right ground at 1:1, not only enlarged

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_end_grass/01_raw.png assets/glory-of-rome/art/ui/end_grass.png
```


## ui_end_hero

**Replaces:** `ui/end_hero.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman commander on a white horse in profile facing right, red cloak, on transparency

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_end_hero
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on a white horse in profile facing right, red cloak, on transparency", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1106, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_end_hero/01_raw.png
file build/art/ui_end_hero/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the red cloak is present and readable at 1:1
- the on transparency is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_end_hero/01_raw.png assets/glory-of-rome/art/ui/end_hero.png
```


## ui_end_lose_screen

**Replaces:** `ui/end_lose_screen.png`

**State:** placeholder. Design size 144x170 → generate at 288x340.

**Prompt**

> a broken Roman eagle standard fallen in mud with a burning frontier fort behind

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_end_lose_screen
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a broken Roman eagle standard fallen in mud with a burning frontier fort behind", "prompt_style": "rd_plus__environment", "width": 288, "height": 340, "num_images": 1, "seed": 1112, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_end_lose_screen/01_raw.png
file build/art/ui_end_lose_screen/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a broken Roman eagle standard fallen in mud with a burning frontier fort behind is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_end_lose_screen/01_raw.png assets/glory-of-rome/art/ui/end_lose_screen.png
```


## ui_end_win_screen

**Replaces:** `ui/end_win_screen.png`

**State:** placeholder. Design size 144x170 → generate at 288x340.

**Prompt**

> a Roman general crowned with laurel raising the recovered golden eagle standard, guards flanking, triumphal hall

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_end_win_screen
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman general crowned with laurel raising the recovered golden eagle standard, guards flanking, triumphal hall", "prompt_style": "rd_plus__environment", "width": 288, "height": 340, "num_images": 1, "seed": 1111, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_end_win_screen/01_raw.png
file build/art/ui_end_win_screen/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the guards flanking is present and readable at 1:1
- the triumphal hall is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_end_win_screen/01_raw.png assets/glory-of-rome/art/ui/end_win_screen.png
```


## ui_hud_contract_silhouette

**Replaces:** `ui/hud_contract_silhouette.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a blank dark rolled scroll, flat silhouette, no interior detail

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_hud_contract_silhouette
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a blank dark rolled scroll, flat silhouette, no interior detail", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1099, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_hud_contract_silhouette/01_raw.png
file build/art/ui_hud_contract_silhouette/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the flat silhouette is present and readable at 1:1
- the no interior detail is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_hud_contract_silhouette/01_raw.png assets/glory-of-rome/art/ui/hud_contract_silhouette.png
```


## ui_hud_gold_purse

**Replaces:** `ui/hud_gold_purse.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a leather coin purse spilling gold aurei

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_hud_gold_purse
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a leather coin purse spilling gold aurei", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1097, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_hud_gold_purse/01_raw.png
file build/art/ui_hud_gold_purse/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a leather coin purse spilling gold aurei is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_hud_gold_purse/01_raw.png assets/glory-of-rome/art/ui/hud_gold_purse.png
```


## ui_hud_magic_00_03

**Replaces:** `ui/hud_magic_00.png`, `ui/hud_magic_01.png`, `ui/hud_magic_02.png`, `ui/hud_magic_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a golden oracle flame burning above a bronze tripod

**Step 1 — the still.** $0.038. These four files are one loop, not four
pictures, so they come from one animation rather than four calls.

```sh
mkdir -p build/art/ui_hud_magic_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden oracle flame burning above a bronze tripod", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1103, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_hud_magic_00_03/01_still.png
file build/art/ui_hud_magic_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a golden oracle flame burning above a bronze tripod is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent

**Step 3 — animate it into four frames.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/ui_hud_magic_00_03/01_still.png)" \
  '{prompt: "the flame flickering and rising, the tripod still",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/ui_hud_magic_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/ui_hud_magic_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_hud_magic_00_03/02_sheet.png
file build/art/ui_hud_magic_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/ui_hud_magic_00_03/02_sheet.png -crop 96x96 +repage build/art/ui_hud_magic_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/ui_hud_magic_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/ui_hud_magic_00_03/frame_00.png assets/glory-of-rome/art/ui/hud_magic_00.png
cp build/art/ui_hud_magic_00_03/frame_01.png assets/glory-of-rome/art/ui/hud_magic_01.png
cp build/art/ui_hud_magic_00_03/frame_02.png assets/glory-of-rome/art/ui/hud_magic_02.png
cp build/art/ui_hud_magic_00_03/frame_03.png assets/glory-of-rome/art/ui/hud_magic_03.png
```


## ui_hud_magic_silhouette

**Replaces:** `ui/hud_magic_silhouette.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a dark flat silhouette of an eight-pointed star, no interior detail

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_hud_magic_silhouette
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a dark flat silhouette of an eight-pointed star, no interior detail", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1102, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_hud_magic_silhouette/01_raw.png
file build/art/ui_hud_magic_silhouette/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the no interior detail is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_hud_magic_silhouette/01_raw.png assets/glory-of-rome/art/ui/hud_magic_silhouette.png
```


## ui_hud_puzzle_grid

**Replaces:** `ui/hud_puzzle_grid.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> an ornate carved stone frame enclosing an empty square panel

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_hud_puzzle_grid
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an ornate carved stone frame enclosing an empty square panel", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1098, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_hud_puzzle_grid/01_raw.png
file build/art/ui_hud_puzzle_grid/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the an ornate carved stone frame enclosing an empty square panel is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_hud_puzzle_grid/01_raw.png assets/glory-of-rome/art/ui/hud_puzzle_grid.png
```


## ui_hud_siege_00_03

**Replaces:** `ui/hud_siege_00.png`, `ui/hud_siege_01.png`, `ui/hud_siege_02.png`, `ui/hud_siege_03.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a Roman onager catapult firing, arm swinging forward

**Step 1 — the still.** $0.038. These four files are one loop, not four
pictures, so they come from one animation rather than four calls.

```sh
mkdir -p build/art/ui_hud_siege_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman onager catapult firing, arm swinging forward", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1101, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_hud_siege_00_03/01_still.png
file build/art/ui_hud_siege_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the arm swinging forward is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent

**Step 3 — animate it into four frames.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/ui_hud_siege_00_03/01_still.png)" \
  '{prompt: "the throwing arm swinging forward and back, the frame still",
    prompt_style: "rd_advanced_animation__idle",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/ui_hud_siege_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/ui_hud_siege_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_hud_siege_00_03/02_sheet.png
file build/art/ui_hud_siege_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet.**

```sh
convert build/art/ui_hud_siege_00_03/02_sheet.png -crop 96x96 +repage build/art/ui_hud_siege_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/ui_hud_siege_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/ui_hud_siege_00_03/frame_00.png assets/glory-of-rome/art/ui/hud_siege_00.png
cp build/art/ui_hud_siege_00_03/frame_01.png assets/glory-of-rome/art/ui/hud_siege_01.png
cp build/art/ui_hud_siege_00_03/frame_02.png assets/glory-of-rome/art/ui/hud_siege_02.png
cp build/art/ui_hud_siege_00_03/frame_03.png assets/glory-of-rome/art/ui/hud_siege_03.png
```


## ui_hud_siege_silhouette

**Replaces:** `ui/hud_siege_silhouette.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a dark flat silhouette of a siege tower, no interior detail

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_hud_siege_silhouette
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a dark flat silhouette of a siege tower, no interior detail", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1100, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_hud_siege_silhouette/01_raw.png
file build/art/ui_hud_siege_silhouette/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the no interior detail is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_hud_siege_silhouette/01_raw.png assets/glory-of-rome/art/ui/hud_siege_silhouette.png
```


## ui_inventory_artifact_amulet

**Replaces:** `ui/inventory_artifact_amulet.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a gold locket amulet on a cord, embossed with a lightning bolt

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_artifact_amulet
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a gold locket amulet on a cord, embossed with a lightning bolt", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1083, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_amulet/01_raw.png
file build/art/ui_inventory_artifact_amulet/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the embossed with a lightning bolt is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_artifact_amulet/01_raw.png assets/glory-of-rome/art/ui/inventory_artifact_amulet.png
```


## ui_inventory_artifact_anchor

**Replaces:** `ui/inventory_artifact_anchor.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a bronze anchor with a trident-shaped crossbar

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_artifact_anchor
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a bronze anchor with a trident-shaped crossbar", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1086, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_anchor/01_raw.png
file build/art/ui_inventory_artifact_anchor/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a bronze anchor with a trident-shaped crossbar is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_artifact_anchor/01_raw.png assets/glory-of-rome/art/ui/inventory_artifact_anchor.png
```


## ui_inventory_artifact_articles

**Replaces:** `ui/inventory_artifact_articles.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a bronze inscribed tablet with a hanging wax seal

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_artifact_articles
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a bronze inscribed tablet with a hanging wax seal", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1082, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_articles/01_raw.png
file build/art/ui_inventory_artifact_articles/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a bronze inscribed tablet with a hanging wax seal is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_artifact_articles/01_raw.png assets/glory-of-rome/art/ui/inventory_artifact_articles.png
```


## ui_inventory_artifact_book

**Replaces:** `ui/inventory_artifact_book.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a torn scrap of ancient papyrus with faded illegible script

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_artifact_book
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a torn scrap of ancient papyrus with faded illegible script", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1085, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_book/01_raw.png
file build/art/ui_inventory_artifact_book/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a torn scrap of ancient papyrus with faded illegible script is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_artifact_book/01_raw.png assets/glory-of-rome/art/ui/inventory_artifact_book.png
```


## ui_inventory_artifact_crown

**Replaces:** `ui/inventory_artifact_crown.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a golden laurel wreath crown

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_artifact_crown
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden laurel wreath crown", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1081, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_crown/01_raw.png
file build/art/ui_inventory_artifact_crown/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a golden laurel wreath crown is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_artifact_crown/01_raw.png assets/glory-of-rome/art/ui/inventory_artifact_crown.png
```


## ui_inventory_artifact_ring

**Replaces:** `ui/inventory_artifact_ring.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a heavy gold equestrian signet ring

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_artifact_ring
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heavy gold equestrian signet ring", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1084, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_ring/01_raw.png
file build/art/ui_inventory_artifact_ring/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a heavy gold equestrian signet ring is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_artifact_ring/01_raw.png assets/glory-of-rome/art/ui/inventory_artifact_ring.png
```


## ui_inventory_artifact_shield

**Replaces:** `ui/inventory_artifact_shield.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a curved rectangular Roman shield bearing a Trojan palladium device

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_artifact_shield
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a curved rectangular Roman shield bearing a Trojan palladium device", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1080, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_shield/01_raw.png
file build/art/ui_inventory_artifact_shield/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a curved rectangular Roman shield bearing a Trojan palladium device is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_artifact_shield/01_raw.png assets/glory-of-rome/art/ui/inventory_artifact_shield.png
```


## ui_inventory_artifact_sword

**Replaces:** `ui/inventory_artifact_sword.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> an ornate gladius short sword with a ruby pommel and flame etching on the blade

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_artifact_sword
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an ornate gladius short sword with a ruby pommel and flame etching on the blade", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1079, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_sword/01_raw.png
file build/art/ui_inventory_artifact_sword/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the an ornate gladius short sword with a ruby pommel and flame etching on the blade is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_artifact_sword/01_raw.png assets/glory-of-rome/art/ui/inventory_artifact_sword.png
```


## ui_inventory_zone_archipelia

**Replaces:** `ui/inventory_zone_archipelia.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a heraldic emblem of a palm and an elephant above a coastal strip and dunes

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_zone_archipelia
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heraldic emblem of a palm and an elephant above a coastal strip and dunes", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1089, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_zone_archipelia/01_raw.png
file build/art/ui_inventory_zone_archipelia/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a heraldic emblem of a palm and an elephant above a coastal strip and dunes is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_zone_archipelia/01_raw.png assets/glory-of-rome/art/ui/inventory_zone_archipelia.png
```


## ui_inventory_zone_continentia

**Replaces:** `ui/inventory_zone_continentia.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a heraldic emblem of the Italian peninsula with a she-wolf and laurel

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_zone_continentia
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heraldic emblem of the Italian peninsula with a she-wolf and laurel", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1087, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_zone_continentia/01_raw.png
file build/art/ui_inventory_zone_continentia/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a heraldic emblem of the Italian peninsula with a she-wolf and laurel is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_zone_continentia/01_raw.png assets/glory-of-rome/art/ui/inventory_zone_continentia.png
```


## ui_inventory_zone_forestria

**Replaces:** `ui/inventory_zone_forestria.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a heraldic emblem of forested hills with a Gallic cockerel and a river bridge

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_zone_forestria
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heraldic emblem of forested hills with a Gallic cockerel and a river bridge", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1088, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_zone_forestria/01_raw.png
file build/art/ui_inventory_zone_forestria/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a heraldic emblem of forested hills with a Gallic cockerel and a river bridge is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_zone_forestria/01_raw.png assets/glory-of-rome/art/ui/inventory_zone_forestria.png
```


## ui_inventory_zone_saharia

**Replaces:** `ui/inventory_zone_saharia.png`

**State:** placeholder. Design size 48x34 → generate at 96x96.

**Prompt**

> a heraldic emblem of a domed eastern skyline with a palm and a sun rising over mountains

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_inventory_zone_saharia
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heraldic emblem of a domed eastern skyline with a palm and a sun rising over mountains", "prompt_style": "rd_plus__classic", "width": 96, "height": 96, "num_images": 1, "seed": 1090, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_inventory_zone_saharia/01_raw.png
file build/art/ui_inventory_zone_saharia/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a heraldic emblem of a domed eastern skyline with a palm and a sun rising over mountains is present and readable at 1:1
- the subject is complete and inside the frame
- the surround is fully transparent
- it reads at 1:1 in its panel slot

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_inventory_zone_saharia/01_raw.png assets/glory-of-rome/art/ui/inventory_zone_saharia.png
```


## ui_splash_logo

**Replaces:** `ui/splash_logo.png`

**State:** placeholder. Design size 320x84 → generate at 640x168.

**Prompt**

> a publisher logo mark on a plain dark field, centred

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_splash_logo
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a publisher logo mark on a plain dark field, centred", "prompt_style": "rd_plus__environment", "width": 640, "height": 168, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_splash_logo/01_raw.png
file build/art/ui_splash_logo/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a publisher logo mark on a plain dark field is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_splash_logo/01_raw.png assets/glory-of-rome/art/ui/splash_logo.png
```


## ui_splash_title

**Replaces:** `ui/splash_title.png`

**State:** placeholder. Design size 320x200 → generate at 640x400.

**Prompt**

> a golden Roman legionary eagle standard with spread wings on a tall decorated pole, a laurel wreath ring below the eagle, an engraved SPQR plate on the shaft, standing against a deep royal purple field inside an ornate gilded border, wide empty space across the top third

**Step 1 — generate.** $0.038.

```sh
mkdir -p build/art/ui_splash_title
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden Roman legionary eagle standard with spread wings on a tall decorated pole, a laurel wreath ring below the eagle, an engraved SPQR plate on the shaft, standing against a deep royal purple field inside an ornate gilded border, wide empty space across the top third", "prompt_style": "rd_plus__environment", "width": 640, "height": 400, "num_images": 1, "seed": 1000, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/ui_splash_title/01_raw.png
file build/art/ui_splash_title/01_raw.png   # must say: PNG image data
```

**Step 2 — accept it by eye.**

- the a laurel wreath ring below the eagle is present and readable at 1:1
- the an engraved SPQR plate on the shaft is present and readable at 1:1
- the composition is complete and nothing important sits under the frame edge
- no rendered text anywhere: lettering is composited by the engine
- the image is fully opaque

**Step 3 — copy into the pack.**


```sh
cp build/art/ui_splash_title/01_raw.png assets/glory-of-rome/art/ui/splash_title.png
```


## villains_alaric_00_03

**Replaces:** `villains/alaric_00.png`, `villains/alaric_01.png`, `villains/alaric_02.png`, `villains/alaric_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Visigoth king in looted Roman armour over furs, iron crown, heavy broadsword

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_alaric_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Visigoth king in looted Roman armour over furs, iron crown, heavy broadsword", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1040, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_alaric_00_03/01_still.png
file build/art/villains_alaric_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the iron crown is present and readable at 1:1
- the heavy broadsword is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_alaric_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_alaric_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_alaric_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_alaric_00_03/02_sheet.png
file build/art/villains_alaric_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_alaric_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_alaric_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_alaric_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_alaric_00_03/frame_00.png assets/glory-of-rome/art/villains/alaric_00.png
cp build/art/villains_alaric_00_03/frame_01.png assets/glory-of-rome/art/villains/alaric_01.png
cp build/art/villains_alaric_00_03/frame_02.png assets/glory-of-rome/art/villains/alaric_02.png
cp build/art/villains_alaric_00_03/frame_03.png assets/glory-of-rome/art/villains/alaric_03.png
```


## villains_arminius_00_03

**Replaces:** `villains/arminius_00.png`, `villains/arminius_01.png`, `villains/arminius_02.png`, `villains/arminius_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Cheruscan chieftain in a bearskin over Roman mail, wolf-skull helmet, framea spear

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_arminius_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Cheruscan chieftain in a bearskin over Roman mail, wolf-skull helmet, framea spear", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1033, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_arminius_00_03/01_still.png
file build/art/villains_arminius_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the wolf-skull helmet is present and readable at 1:1
- the framea spear is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_arminius_00_03/01_still.png)" \
  '{prompt: "levelling the spear and thrusting it forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_arminius_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_arminius_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_arminius_00_03/02_sheet.png
file build/art/villains_arminius_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_arminius_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_arminius_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_arminius_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_arminius_00_03/frame_00.png assets/glory-of-rome/art/villains/arminius_00.png
cp build/art/villains_arminius_00_03/frame_01.png assets/glory-of-rome/art/villains/arminius_01.png
cp build/art/villains_arminius_00_03/frame_02.png assets/glory-of-rome/art/villains/arminius_02.png
cp build/art/villains_arminius_00_03/frame_03.png assets/glory-of-rome/art/villains/arminius_03.png
```


## villains_attila_00_03

**Replaces:** `villains/attila_00.png`, `villains/attila_01.png`, `villains/attila_02.png`, `villains/attila_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> the Hun warlord, wiry and fierce with a thin braided beard, lamellar armour and fur, composite bow and horsehair standard

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_attila_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the Hun warlord, wiry and fierce with a thin braided beard, lamellar armour and fur, composite bow and horsehair standard", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1041, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_attila_00_03/01_still.png
file build/art/villains_attila_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the wiry and fierce with a thin braided beard is present and readable at 1:1
- the lamellar armour and fur is present and readable at 1:1
- the composite bow and horsehair standard is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_attila_00_03/01_still.png)" \
  '{prompt: "drawing the bow and loosing an arrow, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_attila_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_attila_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_attila_00_03/02_sheet.png
file build/art/villains_attila_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_attila_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_attila_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_attila_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_attila_00_03/frame_00.png assets/glory-of-rome/art/villains/attila_00.png
cp build/art/villains_attila_00_03/frame_01.png assets/glory-of-rome/art/villains/attila_01.png
cp build/art/villains_attila_00_03/frame_02.png assets/glory-of-rome/art/villains/attila_02.png
cp build/art/villains_attila_00_03/frame_03.png assets/glory-of-rome/art/villains/attila_03.png
```


## villains_boudica_00_03

**Replaces:** `villains/boudica_00.png`, `villains/boudica_01.png`, `villains/boudica_02.png`, `villains/boudica_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a tall Iceni warrior queen with long red hair, heavy torc at her throat, checked cloak, spear in hand, fierce

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_boudica_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a tall Iceni warrior queen with long red hair, heavy torc at her throat, checked cloak, spear in hand, fierce", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1029, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_boudica_00_03/01_still.png
file build/art/villains_boudica_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the heavy torc at her throat is present and readable at 1:1
- the checked cloak is present and readable at 1:1
- the spear in hand is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_boudica_00_03/01_still.png)" \
  '{prompt: "levelling the spear and thrusting it forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_boudica_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_boudica_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_boudica_00_03/02_sheet.png
file build/art/villains_boudica_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_boudica_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_boudica_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_boudica_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_boudica_00_03/frame_00.png assets/glory-of-rome/art/villains/boudica_00.png
cp build/art/villains_boudica_00_03/frame_01.png assets/glory-of-rome/art/villains/boudica_01.png
cp build/art/villains_boudica_00_03/frame_02.png assets/glory-of-rome/art/villains/boudica_02.png
cp build/art/villains_boudica_00_03/frame_03.png assets/glory-of-rome/art/villains/boudica_03.png
```


## villains_brennus_00_03

**Replaces:** `villains/brennus_00.png`, `villains/brennus_01.png`, `villains/brennus_02.png`, `villains/brennus_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Gallic warchief with a drooping moustache and lime-spiked hair, bare-chested with a heavy gold torc, long sword raised

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_brennus_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Gallic warchief with a drooping moustache and lime-spiked hair, bare-chested with a heavy gold torc, long sword raised", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1031, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_brennus_00_03/01_still.png
file build/art/villains_brennus_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the bare-chested with a heavy gold torc is present and readable at 1:1
- the long sword raised is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_brennus_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_brennus_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_brennus_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_brennus_00_03/02_sheet.png
file build/art/villains_brennus_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_brennus_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_brennus_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_brennus_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_brennus_00_03/frame_00.png assets/glory-of-rome/art/villains/brennus_00.png
cp build/art/villains_brennus_00_03/frame_01.png assets/glory-of-rome/art/villains/brennus_01.png
cp build/art/villains_brennus_00_03/frame_02.png assets/glory-of-rome/art/villains/brennus_02.png
cp build/art/villains_brennus_00_03/frame_03.png assets/glory-of-rome/art/villains/brennus_03.png
```


## villains_catiline_00_03

**Replaces:** `villains/catiline_00.png`, `villains/catiline_01.png`, `villains/catiline_02.png`, `villains/catiline_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a disgraced Roman senator in a stained toga, gaunt and hollow-eyed, dagger half-hidden in the folds, conspiratorial

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_catiline_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a disgraced Roman senator in a stained toga, gaunt and hollow-eyed, dagger half-hidden in the folds, conspiratorial", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1025, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_catiline_00_03/01_still.png
file build/art/villains_catiline_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the gaunt and hollow-eyed is present and readable at 1:1
- the dagger half-hidden in the folds is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_catiline_00_03/01_still.png)" \
  '{prompt: "drawing the dagger and stabbing forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_catiline_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_catiline_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_catiline_00_03/02_sheet.png
file build/art/villains_catiline_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_catiline_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_catiline_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_catiline_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_catiline_00_03/frame_00.png assets/glory-of-rome/art/villains/catiline_00.png
cp build/art/villains_catiline_00_03/frame_01.png assets/glory-of-rome/art/villains/catiline_01.png
cp build/art/villains_catiline_00_03/frame_02.png assets/glory-of-rome/art/villains/catiline_02.png
cp build/art/villains_catiline_00_03/frame_03.png assets/glory-of-rome/art/villains/catiline_03.png
```


## villains_civilis_00_03

**Replaces:** `villains/civilis_00.png`, `villains/civilis_01.png`, `villains/civilis_02.png`, `villains/civilis_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Batavian auxiliary commander, one eye missing, Roman mail worn over Germanic trousers, long blond hair, holding a Roman standard

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_civilis_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Batavian auxiliary commander, one eye missing, Roman mail worn over Germanic trousers, long blond hair, holding a Roman standard", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1030, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_civilis_00_03/01_still.png
file build/art/villains_civilis_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the one eye missing is present and readable at 1:1
- the Roman mail worn over Germanic trousers is present and readable at 1:1
- the long blond hair is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_civilis_00_03/01_still.png)" \
  '{prompt: "thrusting the standard forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_civilis_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_civilis_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_civilis_00_03/02_sheet.png
file build/art/villains_civilis_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_civilis_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_civilis_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_civilis_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_civilis_00_03/frame_00.png assets/glory-of-rome/art/villains/civilis_00.png
cp build/art/villains_civilis_00_03/frame_01.png assets/glory-of-rome/art/villains/civilis_01.png
cp build/art/villains_civilis_00_03/frame_02.png assets/glory-of-rome/art/villains/civilis_02.png
cp build/art/villains_civilis_00_03/frame_03.png assets/glory-of-rome/art/villains/civilis_03.png
```


## villains_gildo_00_03

**Replaces:** `villains/gildo_00.png`, `villains/gildo_01.png`, `villains/gildo_02.png`, `villains/gildo_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Moorish count in flowing white desert robes over Roman officer armour, curved blade, dark-skinned and imposing

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_gildo_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Moorish count in flowing white desert robes over Roman officer armour, curved blade, dark-skinned and imposing", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1034, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_gildo_00_03/01_still.png
file build/art/villains_gildo_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the curved blade is present and readable at 1:1
- the dark-skinned and imposing is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_gildo_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_gildo_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_gildo_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_gildo_00_03/02_sheet.png
file build/art/villains_gildo_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_gildo_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_gildo_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_gildo_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_gildo_00_03/frame_00.png assets/glory-of-rome/art/villains/gildo_00.png
cp build/art/villains_gildo_00_03/frame_01.png assets/glory-of-rome/art/villains/gildo_01.png
cp build/art/villains_gildo_00_03/frame_02.png assets/glory-of-rome/art/villains/gildo_02.png
cp build/art/villains_gildo_00_03/frame_03.png assets/glory-of-rome/art/villains/gildo_03.png
```


## villains_hannibal_00_03

**Replaces:** `villains/hannibal_00.png`, `villains/hannibal_01.png`, `villains/hannibal_02.png`, `villains/hannibal_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Carthaginian general in a crested Punic helmet, one eye scarred and blind, purple cloak, curved falcata sword

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_hannibal_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Carthaginian general in a crested Punic helmet, one eye scarred and blind, purple cloak, curved falcata sword", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1037, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_hannibal_00_03/01_still.png
file build/art/villains_hannibal_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the one eye scarred and blind is present and readable at 1:1
- the purple cloak is present and readable at 1:1
- the curved falcata sword is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_hannibal_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_hannibal_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_hannibal_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_hannibal_00_03/02_sheet.png
file build/art/villains_hannibal_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_hannibal_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_hannibal_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_hannibal_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_hannibal_00_03/frame_00.png assets/glory-of-rome/art/villains/hannibal_00.png
cp build/art/villains_hannibal_00_03/frame_01.png assets/glory-of-rome/art/villains/hannibal_01.png
cp build/art/villains_hannibal_00_03/frame_02.png assets/glory-of-rome/art/villains/hannibal_02.png
cp build/art/villains_hannibal_00_03/frame_03.png assets/glory-of-rome/art/villains/hannibal_03.png
```


## villains_jugurtha_00_03

**Replaces:** `villains/jugurtha_00.png`, `villains/jugurtha_01.png`, `villains/jugurtha_02.png`, `villains/jugurtha_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Numidian king in an ornate gold circlet and rich embroidered robes over a cuirass, arms folded, imperious

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_jugurtha_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Numidian king in an ornate gold circlet and rich embroidered robes over a cuirass, arms folded, imperious", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1028, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_jugurtha_00_03/01_still.png
file build/art/villains_jugurtha_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the arms folded is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_jugurtha_00_03/01_still.png)" \
  '{prompt: "unfolding the arms and striking forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_jugurtha_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_jugurtha_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_jugurtha_00_03/02_sheet.png
file build/art/villains_jugurtha_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_jugurtha_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_jugurtha_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_jugurtha_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_jugurtha_00_03/frame_00.png assets/glory-of-rome/art/villains/jugurtha_00.png
cp build/art/villains_jugurtha_00_03/frame_01.png assets/glory-of-rome/art/villains/jugurtha_01.png
cp build/art/villains_jugurtha_00_03/frame_02.png assets/glory-of-rome/art/villains/jugurtha_02.png
cp build/art/villains_jugurtha_00_03/frame_03.png assets/glory-of-rome/art/villains/jugurtha_03.png
```


## villains_mithridates_00_03

**Replaces:** `villains/mithridates_00.png`, `villains/mithridates_01.png`, `villains/mithridates_02.png`, `villains/mithridates_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> an eastern king in a Persian tiara and richly patterned robes, jewelled scabbard, holding a phial of poison

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_mithridates_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an eastern king in a Persian tiara and richly patterned robes, jewelled scabbard, holding a phial of poison", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1036, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_mithridates_00_03/01_still.png
file build/art/villains_mithridates_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the jewelled scabbard is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_mithridates_00_03/01_still.png)" \
  '{prompt: "raising the phial and hurling its contents forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_mithridates_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_mithridates_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_mithridates_00_03/02_sheet.png
file build/art/villains_mithridates_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_mithridates_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_mithridates_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_mithridates_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_mithridates_00_03/frame_00.png assets/glory-of-rome/art/villains/mithridates_00.png
cp build/art/villains_mithridates_00_03/frame_01.png assets/glory-of-rome/art/villains/mithridates_01.png
cp build/art/villains_mithridates_00_03/frame_02.png assets/glory-of-rome/art/villains/mithridates_02.png
cp build/art/villains_mithridates_00_03/frame_03.png assets/glory-of-rome/art/villains/mithridates_03.png
```


## villains_pyrrhus_00_03

**Replaces:** `villains/pyrrhus_00.png`, `villains/pyrrhus_01.png`, `villains/pyrrhus_02.png`, `villains/pyrrhus_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Hellenistic king in a plumed Corinthian helmet and gilded muscled cuirass, round hoplite shield, long sarissa spear

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_pyrrhus_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Hellenistic king in a plumed Corinthian helmet and gilded muscled cuirass, round hoplite shield, long sarissa spear", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1035, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_pyrrhus_00_03/01_still.png
file build/art/villains_pyrrhus_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the round hoplite shield is present and readable at 1:1
- the long sarissa spear is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_pyrrhus_00_03/01_still.png)" \
  '{prompt: "levelling the spear and thrusting it forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_pyrrhus_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_pyrrhus_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_pyrrhus_00_03/02_sheet.png
file build/art/villains_pyrrhus_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_pyrrhus_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_pyrrhus_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_pyrrhus_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_pyrrhus_00_03/frame_00.png assets/glory-of-rome/art/villains/pyrrhus_00.png
cp build/art/villains_pyrrhus_00_03/frame_01.png assets/glory-of-rome/art/villains/pyrrhus_01.png
cp build/art/villains_pyrrhus_00_03/frame_02.png assets/glory-of-rome/art/villains/pyrrhus_02.png
cp build/art/villains_pyrrhus_00_03/frame_03.png assets/glory-of-rome/art/villains/pyrrhus_03.png
```


## villains_shapur_00_03

**Replaces:** `villains/shapur_00.png`, `villains/shapur_01.png`, `villains/shapur_02.png`, `villains/shapur_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Sasanian shah in a towering jewelled korymbos crown, full cataphract scale armour, long straight sword, regal

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_shapur_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Sasanian shah in a towering jewelled korymbos crown, full cataphract scale armour, long straight sword, regal", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1039, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_shapur_00_03/01_still.png
file build/art/villains_shapur_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the full cataphract scale armour is present and readable at 1:1
- the long straight sword is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_shapur_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_shapur_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_shapur_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_shapur_00_03/02_sheet.png
file build/art/villains_shapur_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_shapur_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_shapur_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_shapur_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_shapur_00_03/frame_00.png assets/glory-of-rome/art/villains/shapur_00.png
cp build/art/villains_shapur_00_03/frame_01.png assets/glory-of-rome/art/villains/shapur_01.png
cp build/art/villains_shapur_00_03/frame_02.png assets/glory-of-rome/art/villains/shapur_02.png
cp build/art/villains_shapur_00_03/frame_03.png assets/glory-of-rome/art/villains/shapur_03.png
```


## villains_spartacus_00_03

**Replaces:** `villains/spartacus_00.png`, `villains/spartacus_01.png`, `villains/spartacus_02.png`, `villains/spartacus_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Thracian gladiator, bare-chested and scarred, gladiator helmet with a grille visor, short curved sica sword and small square shield

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_spartacus_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Thracian gladiator, bare-chested and scarred, gladiator helmet with a grille visor, short curved sica sword and small square shield", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1026, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_spartacus_00_03/01_still.png
file build/art/villains_spartacus_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the bare-chested and scarred is present and readable at 1:1
- the gladiator helmet with a grille visor is present and readable at 1:1
- the short curved sica sword and small square shield is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_spartacus_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_spartacus_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_spartacus_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_spartacus_00_03/02_sheet.png
file build/art/villains_spartacus_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_spartacus_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_spartacus_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_spartacus_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_spartacus_00_03/frame_00.png assets/glory-of-rome/art/villains/spartacus_00.png
cp build/art/villains_spartacus_00_03/frame_01.png assets/glory-of-rome/art/villains/spartacus_01.png
cp build/art/villains_spartacus_00_03/frame_02.png assets/glory-of-rome/art/villains/spartacus_02.png
cp build/art/villains_spartacus_00_03/frame_03.png assets/glory-of-rome/art/villains/spartacus_03.png
```


## villains_tacfarinas_00_03

**Replaces:** `villains/tacfarinas_00.png`, `villains/tacfarinas_01.png`, `villains/tacfarinas_02.png`, `villains/tacfarinas_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Numidian deserter chieftain in a Roman military cloak over desert robes, javelins across his back, sun-darkened

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_tacfarinas_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Numidian deserter chieftain in a Roman military cloak over desert robes, javelins across his back, sun-darkened", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1027, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_tacfarinas_00_03/01_still.png
file build/art/villains_tacfarinas_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the javelins across his back is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_tacfarinas_00_03/01_still.png)" \
  '{prompt: "hurling a javelin overarm, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_tacfarinas_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_tacfarinas_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_tacfarinas_00_03/02_sheet.png
file build/art/villains_tacfarinas_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_tacfarinas_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_tacfarinas_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_tacfarinas_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_tacfarinas_00_03/frame_00.png assets/glory-of-rome/art/villains/tacfarinas_00.png
cp build/art/villains_tacfarinas_00_03/frame_01.png assets/glory-of-rome/art/villains/tacfarinas_01.png
cp build/art/villains_tacfarinas_00_03/frame_02.png assets/glory-of-rome/art/villains/tacfarinas_02.png
cp build/art/villains_tacfarinas_00_03/frame_03.png assets/glory-of-rome/art/villains/tacfarinas_03.png
```


## villains_vercingetorix_00_03

**Replaces:** `villains/vercingetorix_00.png`, `villains/vercingetorix_01.png`, `villains/vercingetorix_02.png`, `villains/vercingetorix_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Gallic king in a horned helmet and mail shirt, long moustache, oval shield with a swirling Celtic device, longsword

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_vercingetorix_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Gallic king in a horned helmet and mail shirt, long moustache, oval shield with a swirling Celtic device, longsword", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1032, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_vercingetorix_00_03/01_still.png
file build/art/villains_vercingetorix_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the long moustache is present and readable at 1:1
- the oval shield with a swirling Celtic device is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_vercingetorix_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_vercingetorix_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_vercingetorix_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_vercingetorix_00_03/02_sheet.png
file build/art/villains_vercingetorix_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_vercingetorix_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_vercingetorix_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_vercingetorix_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_vercingetorix_00_03/frame_00.png assets/glory-of-rome/art/villains/vercingetorix_00.png
cp build/art/villains_vercingetorix_00_03/frame_01.png assets/glory-of-rome/art/villains/vercingetorix_01.png
cp build/art/villains_vercingetorix_00_03/frame_02.png assets/glory-of-rome/art/villains/vercingetorix_02.png
cp build/art/villains_vercingetorix_00_03/frame_03.png assets/glory-of-rome/art/villains/vercingetorix_03.png
```


## villains_zenobia_00_03

**Replaces:** `villains/zenobia_00.png`, `villains/zenobia_01.png`, `villains/zenobia_02.png`, `villains/zenobia_03.png`

**State:** no KB counterpart. Design size 48x34 → generate at 96x96.

**Prompt** (subject only; the style supplies framing, pose and background)

> a Palmyrene warrior queen in gilded scale armour and an eastern diadem, dark braided hair, ornate curved sword

**Step 1 — the still.** $0.18.

```sh
mkdir -p build/art/villains_zenobia_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Palmyrene warrior queen in gilded scale armour and an eastern diadem, dark braided hair, ornate curved sword", "prompt_style": "user__glory_of_rome_troops_bac676cd", "width": 96, "height": 96, "num_images": 1, "seed": 1038, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_zenobia_00_03/01_still.png
file build/art/villains_zenobia_00_03/01_still.png   # must say: PNG image data
```

**Step 2 — accept it by eye.** At 1:1 and at 8x. For this subject:

- the dark braided hair is present and readable at 1:1
- the ornate curved sword is present and readable at 1:1
- the whole figure is inside the frame, with clear rows above the head
- both feet are drawn and the figure stands on them
- it reads at 1:1 over grass, forest and desert, not only enlarged

**Step 3 — animate the approved still.** $0.14.

```sh
jq -n --arg img "$(base64 -w0 build/art/villains_zenobia_00_03/01_still.png)" \
  '{prompt: "swinging the sword forward and striking, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/villains_zenobia_00_03/anim_request.json
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d @build/art/villains_zenobia_00_03/anim_request.json | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.result.base64_images[0]' | base64 -d > build/art/villains_zenobia_00_03/02_sheet.png
file build/art/villains_zenobia_00_03/02_sheet.png   # must say: PNG image data
```

**Step 4 — cut the sheet** (192x192, a 2x2 grid read left to right, top to bottom).

```sh
convert build/art/villains_zenobia_00_03/02_sheet.png -crop 96x96 +repage build/art/villains_zenobia_00_03/frame_%02d.png
identify -format "%f %wx%h\n" build/art/villains_zenobia_00_03/frame_0*.png   # four files, each 96x96
```

**Step 5 — copy into the pack.**

```sh
cp build/art/villains_zenobia_00_03/frame_00.png assets/glory-of-rome/art/villains/zenobia_00.png
cp build/art/villains_zenobia_00_03/frame_01.png assets/glory-of-rome/art/villains/zenobia_01.png
cp build/art/villains_zenobia_00_03/frame_02.png assets/glory-of-rome/art/villains/zenobia_02.png
cp build/art/villains_zenobia_00_03/frame_03.png assets/glory-of-rome/art/villains/zenobia_03.png
```
