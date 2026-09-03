# Glory of Rome — art pipeline, as built

Written 2026-09-03, from a session that produced the first usable troop sprite
and the first working attack animation. Twelve paid calls, $0.98.

This file is the authority on **method** and the evidence behind it.
`ART-WORKLIST.md` is the work list: the same method, written out as runnable
commands for all 113 items. The two agree; if they ever stop agreeing, this file
is the one that was measured.

---

## 1. The recipe

Four steps per figure. Step 2 is a stop, not a formality.

### Step 1 — the still ($0.18)

```
style   user__glory_of_rome_troops_bac676cd     (RD Pro rates)
size    96x96 native
prompt  SUBJECT ONLY. No framing, no background, no rendering words.
flags   async
```

The custom style carries everything that used to be repeated in each prompt. It
was created free via `POST /v1/styles` with:

- **reference_images**: one approved sprite (`build/art/hastati_style_cs1`)
- **reference_caption**: a plain description of that sprite
- **llm_instructions**: forbid scenery outright, demand a full-length standing
  figure in profile, whole body inside the frame, flat background
- **user_prompt_template**: `{prompt}, full length, standing in profile facing
  right, game sprite, solid neutral grey background`
- **force_bg_removal**: true
- **force_palette**: deliberately OFF — it hard-constrains output to the
  reference palette and collapsed structure both times it was tried

So a troop prompt is now just: *"A Roman legionary with a red crested helmet and
a tall rectangular red and gold shield, holding a spear, stocky and broad
shouldered."*

### Step 2 — look at it

At 1:1 and at 8x against the King's Bounty reference, before anything is
animated or accepted. `rdgen.py`'s QA thresholds were written for a 48-wide tile
— its silhouette, colour-count and feet checks are meaningless at 96, and its
"feet on bottom row" check passed a sprite whose legs ended in a stump. Metrics
are a floor, never a verdict.

If the still is wrong, record what was wrong and move on. Do not re-roll it.

### Step 3 — animate the approved still ($0.14)

```
style   rd_advanced_animation__attack
size    96x96, matching the start frame
input   the approved still, uploaded untouched with alpha intact
flags   frames_duration 4, return_spritesheet true, async
```

Building the payload with `jq` keeps the base64 out of the shell:

```sh
jq -n --arg img "$(base64 -w0 build/art/<id>/01_still.png)" \
  '{prompt: "levelling the spear and thrusting it forward, feet planted",
    prompt_style: "rd_advanced_animation__attack",
    width: 96, height: 96, num_images: 1,
    frames_duration: 4, return_spritesheet: true,
    input_image: $img, async: true}' > build/art/<id>/anim_request.json
```

### Decoding the poll response

The task poll returns `{status, task_id, result: {...}}` — the image is at
**`.result.base64_images[0]`**, not `.base64_images[0]`. The work list carried
the shallow path for three weeks and it silently wrote a 3-byte file: `jq`
prints `null`, `base64 -d` decodes it to nothing, and the shell reports no
error. The call had already been charged. Every command now ends with a `file`
check that must say `PNG image data`.

Recovery, if it happens again: the request id is printed by the submit step, and
`GET /v1/inferences/requests/{id}` returns a signed URL for 24 hours.

### Step 4 — cut the sheet

It returns a **192x192 PNG: a 2x2 grid of 96x96 cells**, transparent, binary
alpha, read left to right then top to bottom.

```sh
convert build/art/<id>/02_sheet.png -crop 96x96 +repage build/art/<id>/frame_%02d.png
```

Verified pixel-identical to cutting the same sheet in Pillow. This is decoding a
container, not editing pixels.

### Cost

**$0.32 per animated troop.** 42 troops and villains ≈ $13.50. The other 71
items are $0.038 each on the flat route, but see the warning below.

### What is NOT proven

Only the figure route above has been run end to end. The 56 cell-and-screen
items in the work list use `rd_plus__classic` and `rd_plus__default`, chosen off
published size ranges and **never generated**. Worse, a different style means
they will not match the troops — the exact coherence failure the custom style
exists to fix. Assume a second custom style is needed for objects, and possibly
a third for screens, and budget a probe before committing to those 56.

**Nine screen assets cannot be generated at their stated size at all**: they run
from 480x204 to 640x400, and no public style accepts a dimension above 384.
Either author them, or generate the subject small and compose the screen in code
as the title-screen plan already does for the Aquila, or drop the design-size-x2
rule for screen art.

### Rules that survive from earlier work

- `async: true` always, task id to disk before polling.
- Never `input_palette`.
- Terrain describes a **pattern**, never the subject — naming "sea" or "grass"
  bakes in a lighting gradient or an edge vignette that tiles as a lattice.
- `tile_x`/`tile_y` on base terrain only.
- Token from `~/.config/retrodiffusion/token`. No environment variables.
- Nothing is written into `assets/`. Approved finals are copied by hand.

---

## 2. The sequence that got here

Every run changed exactly one thing from the one before it. `rd_plus__classic`,
96x96, seed 1101 unless stated. Height and width are the figure's bounding box
as a fraction of the 96x96 frame; the King's Bounty pikeman occupies 97% and
83% of its own tile.

| run | the one change | h | w | result |
|---|---|---|---|---|
| 01 | baseline, no background wording | 89% | — | **painted scene**: foliage, sand, cast shadow. `remove_bg` stripped the outer field and kept the scenery, because a segmenter treats drawn ground as subject |
| 02 | `+ game sprite, solid neutral grey background` | 64% | 49% | **isolated, perfectly.** 1,894 opaque pixels, no scenery. Cost a quarter of the figure's height |
| 03 | `+ A full length ... standing` | 82% | 49% | height recovered. Good armour, feet, 20 colours |
| 04 | `spear level` → `spear horizontally across his body` | 64% | **71%** | spear finally horizontal — after four runs I had wrongly called this a refusal by the model |
| 05 | `side view` → `in profile` (on run04) | 81% | **79%** | true profile, best geometry — **and the worst drawing**: no feet, no armour, floating shield, spear clipped at both edges |
| 06 | `side view` → `in profile` (on run03) | 82% | 49% | **the good one.** Full segmentata, pteruges, boots, shield on the left arm, spear upright, 18 colours |
| 07 | run06, seed 2202 | 60% | 29% | gold helmet, muddy, shield on the **right** |
| 08 | run06, seed 3303 | 73% | 34% | green skirt, **sandals**, shield on the right, spear in the left hand |

Then the same prompt through the custom style, at the same three seeds:

| seed | h | w | result |
|---|---|---|---|
| 1101 | 82% | 50% | silver segmentata, red skirt and boots, shield left, spear upright |
| 2202 | 79% | 51% | same kit, same palette, same shield arm |
| 3303 | 88% | 68% | same kit — plus a vexillum banner and a lion device, and **mask damage** |

Then one animation call on the seed-1101 still.

---

## 3. Findings

**1. The style decides whether you get a scene.** `rd_plus__classic`'s own
example prompt, from the live selector, is *"A raven standing on a branch with
appalachian forests and mountains in the background."* It is a scene style, and
it drew a scene. The catalogue's isolated-asset styles say so in their
descriptions — `rd_plus__topdown_item` is *"assets with no background"* — and
their examples end in the tag `neutral grey background`.

**2. `remove_bg` is a segmenter, not a colour key.** It removed 4,621 pixels
from run01 and still left the foliage, because painted ground is subject
matter. Give it one flat field and it takes the field whole.

**3. A flat background can only be shown *around* the subject; a painted one
sits *behind* it.** That is why asking for visible grey shrank the figure by a
quarter. Subject-extent wording (`a full length ... standing`) is the
counter-lever and recovered it.

**4. Two failures I blamed on the model were my word choice.**
`level` reads as plumb — an upright spear *is* level, so four "ignored"
instructions were honoured. `horizontally across his body` worked first time.
Likewise `side view` produced a frontal body five times; `in profile`, the term
an artist would use, produced a profile immediately. RD's own FAQ says to use
*"industry standard terms instead of trying to describe the idea using common
words."*

**5. The horizontal-spear pose costs anatomy.** Run05 (profile + horizontal)
lost the armour and the feet; run06 (profile + upright spear) kept both, same
seed. The neutral upright stance is also what generalises across 25 troops —
"spear held level" is meaningless for wolves, Cyclopes or dragons.

**6. Without a style, the prompt is a lottery.** Three seeds, one prompt, word
for word: apparent size varied by 1.7x, the shield changed arms, the skirt
changed colour, boots became sandals, the helmet changed metal. The subject was
stable; everything that makes 25 sprites look like one army was not.

**7. A custom style fixes it, at Pro prices.** Same three seeds through the
style: silver segmentata, red skirt, red boots, shield on the left arm, spear
upright — in all three. The residual variance is rank and ornament, not style.
Cost is **$0.18 not $0.038**, because custom styles are an RD Pro template.
Colour counts rise to 34–43 against the flat route's 18 and the reference's 14:
Pro shades more.

**8. The background colour must not appear in the subject.** `solid neutral
grey` next to silver armour let the remover eat the pauldron. Measured: light
greyish pixels sitting on the transparent boundary were 1, 0 and **61** across
the three styled runs, and at 10x the third has ragged notches chewed out of
the shoulder. The template should name a colour that cannot occur in Roman kit.
Not yet fixed.

**9. Transparency carries through animation.** This refutes the 15 August
record that "animation output is always opaque, input must be RGB." The sheet
came back with alpha 0 and 255 only, zero partial, transparent corners — after
sending the start frame RGBA rather than flattened onto white. Flattening would
have made the claim untestable and the route unusable.

**10. Animation registration is far better than predicted.** The floor row is
**identical at 84 in all four frames** and the crest top varies by 1px, against
the 9.5px body drift measured in July. Horizontally the figure slides ~5px into
the thrust, which reads as weight shift. Four distinct poses, no duplicates.

**11. The animation re-synthesises rather than transforms.** It kept the kit,
palette and build but drew the man frontal where the start frame was in
profile, and changed the shield device. The four frames are coherent with each
other, not with the still that seeded them. Treat the animation output as the
whole set for a troop rather than trying to make a separate approved still
agree with it.

**12. Two `rdgen.py` defects, both found by paying for them.** It silently
dropped job keys it did not forward, so a call went out without
`return_non_bg_removed` and could not answer the question it was made to
answer; and it kept only `base64_images[0]`, which would have destroyed the
second paid image the moment that flag worked. Both fixed: unknown keys are now
refused before submit, and every returned image is written.

**13. What the engine actually needs.** One `anim[]` array per troop, count
pack-declared up to 16, used everywhere — combat, wandering armies, roster,
location screens. In combat only the **active** unit animates; every other
stack holds **frame 0**. The AI side is drawn mirrored from the same art, which
is why a shield that changes arms between sprites is a real defect and not a
matter of taste. `sprites_frame()` returns 0 for a count of 1, so a pack may
legitimately ship single-frame troops today.

**14. What the King's Bounty reference actually is.** Four slots, three
drawings: frame 3 is frame 1 with one pixel different, at (38,33). Rest →
wind-up → thrust → wind-up. The body never moves more than 2px, the feet stay
planted on row 33, and the spear is **never vertical** — it is horizontal at
rest and extends. Rest is 40 columns wide, the thrust 41, so the tile is sized
for a pose that barely changes width.

---

## 4. Corrections to earlier records

- **"Animation output is always opaque"** (15 Aug) — no longer true, see #9.
- **`ART-WORKLIST.md` figure prompts** carry 60-word style tails
  (*"bold flat blocks of colour, few colours, hard dark outline..."*) and use
  `rd_plus__low_res`. RD's docs say never to write rendering words, the style
  carries them; and `low_res` has no outline in its description. Superseded by
  §1 for every figure. The inventory and the subject wording remain good.
- **`input_palette`** remains a dead end. Nothing today changes that.
- **`negative`** remains a documented no-op.

---

## 5. Plan

Costed, gated, and ordered so the cheap unknowns die first.

### Phase A — close out the Hastati (≈ $0.36)

1. **Fix the style template** to name a background colour that cannot occur in
   Roman kit — magenta or chroma green rather than neutral grey. `PATCH
   /v1/styles/{id}`, free.
2. **Re-run seed 3303 only** ($0.18) to confirm the mask damage is gone. Single
   variable, and 3303 is the seed that failed.
3. **Install the four attack frames** into `assets/glory-of-rome/art/troops/`
   as `pikemen_00..03.png` and run Rome. Free, and the first time any of this
   is judged in the game rather than on a web page.
4. **Flip the manifest** to `tile_h: 96` with `ui_scale: 2`. One line. The
   remaining 48x34 art will stretch until replaced, which is expected.

### Phase A2 — the 56 unproven items

Before any of the cell-and-screen art is scheduled, two things must be settled,
and neither is a generation:

- **Do objects need their own style?** A chest generated on `rd_plus__classic`
  will not match a troop generated on the custom style. Either build a second
  custom style from an approved object, or accept the mismatch deliberately.
  One probe item answers it, $0.038 plus $0.18 for the styled comparison.
- **What happens to the nine oversize screen assets?** 480x204 to 640x400
  against a 384 ceiling. Author, compose in code, or generate at 1x.

### Phase B — prove the recipe generalises (≈ $1.30)

5. **Four contrasting subjects, one call each** ($0.72): Furiae (winged, female,
   non-human), Cyclopes (huge, one-eyed), Lupi (a quadruped, no armour at all),
   Manes (translucent, legless). If the style holds across those four it holds
   across the roster; if it fails, it fails on a $0.72 sample rather than a $20
   one.
6. **Animate whichever two look best** ($0.28) to confirm the animation route
   is not legionary-specific.
7. **Contact sheet all of them over real terrain at 1:1**, plus in-game.

**Gate:** if four contrasting subjects come back coherent, commit to the style
for the whole pack. If not, the answer is that the API gives good individual
sprites but not a matching set, and the pack needs a different plan — worth
knowing for $1.30.

### Phase C — the troop roster (≈ $8)

8. **All 25 troops**, still plus animation, $0.32 each. One call per asset, no
   re-rolls: if a subject fails, record the failure and move on rather than
   grinding. Batch them in fives and look at every image before the next five.
9. Frame counts stay at 4 to match the engine's array and KB's cycle.

### Phase D — villains and the rest (≈ $12)

10. **17 villains** ($3.06 stills; `_00` doubles as the contract portrait, so
    the pose must read as a portrait too).
11. **Terrain**: water is already accepted from August; grass, forest,
    mountain, desert, grass_variant remain. Terrain edges (48 files) are
    composited from the base tiles, never generated.
12. **Screen art** — backdrops, class portraits, title, chrome — at design size
    x2 per the inventory. These are the items most likely to need their own
    style, since a 1280x800 chrome frame has nothing in common with a troop.

### Standing rules for all of it

- One call at a time. Look at every image at 1:1 **and** at 8x against the
  reference before the next call. Metrics are a floor, never a verdict —
  `rdgen.py`'s QA thresholds are still written for a 48-wide tile and its
  silhouette, colour and feet checks are meaningless at 96.
- No re-rolling to fix a bad prompt. Change the prompt or accept the result.
- Every finding goes into the job file and into this document, not into
  conversation. The `input_palette` lesson was learned twice, two days apart,
  because the first time it was only ever said out loud.
