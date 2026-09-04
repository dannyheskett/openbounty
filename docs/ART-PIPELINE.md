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
- `frames_duration: 4` — the pack's cycle length
- `input_image_keep_alpha: true` — what makes the frames transparent

The motion line names the path and its two endpoints, then what stays still:
"from upright beside his head forward and down until his arm is straight out in
front at shoulder height", then "both feet stay planted, the shield stays where
it is".

---

## 4. The frames

The response is a 192x192 sheet, a 2x2 grid of 96x96 cells. `rdgen` writes them
as `frame_00..03.png`. Watch the animation, then:

```
cp build/art/<id>/run01/frame_0N.png assets/glory-of-rome/art/troops/<name>_0N.png
```

and record both prompts in `ART-WORKLIST.md`.

---

## Other kinds of art

- **A small unit** — generate *and* animate at 64x64, then composite each frame
  into a 96x96 transparent canvas at offset (16, 32). Two thirds height, by
  construction.
- **Class portraits** — 192x204, `rd_pro__default`, opaque, no `remove_bg`.
- **The class-select picker** — 256x164, `rd_pro__default`, with all four
  approved portraits passed as `reference_images` and the prompt written as "the
  same general in the bronze cuirass…". The four figures sit one per column,
  left to right, in manifest order.
- **Base terrain** — `rd_plus__low_res` with `tile_x` and `tile_y`, opaque, the
  prompt describing a pattern rather than a subject: "a small crop cut from the
  middle of a much larger sheet of wrapping paper, printed all over with…".
- **Object tiles** (the 1x1 castle) — `rd_plus__low_res`, 96x96, `figure: false`,
  `remove_bg: true` with the magenta background named in the prompt and the
  subject framed as "an isolated cut-out game sprite … drawn without any
  ground plane". The ground is not in the art: the renderer draws the terrain
  tile beneath every object tile. Asking for "no ground" alone did not work;
  naming what lies below the walls (the background) and changing the seed did.
- **Terrain edges** — composited from the finished base and grass tiles using
  the reference edge's alpha mask.

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
