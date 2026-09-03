# Glory of Rome — art prompts and commands

Everything needed to generate the pack, in one file. No scripts.

113 items. Each entry lists the files it replaces, the prompt, and the
commands to generate it and write the PNG.

## Before you start

```sh
TOKEN=$(cat ~/.config/retrodiffusion/token)
```

Every item below follows the same three steps: POST to get a task id, poll until
it succeeds, decode the base64 to a PNG. Cost is $0.038 per image.

## Rules these commands already follow

- **96x96** for anything in a map or combat cell; screen art at its design size x2.
- **`tile_x`/`tile_y`** on base terrain only, for the seamless wrap.
- **No `input_palette`.** It hard-constrains the whole image to the supplied palette
  and collapses structure — it returned 3-colour all-green artifact tiles.
- **Terrain prompts describe a pattern, never the subject.** Naming "sea" or "grass"
  makes the model light a scene and bake in a gradient or an edge vignette that reads
  as a lattice when tiled.
- **`async: true`** so the task id is returned before any charge can be lost.

- Terrain edges (48 files) are **not** generated — composite them from the new base
  and grass tiles using the reference edge's alpha mask.

---


# 5.1 Troops -- 25 items, 100 files, 48x34, 4 frames


## troops_archers_00_03

**Replaces:** `troops/archers_00.png`, `troops/archers_01.png`, `troops/archers_02.png`, `troops/archers_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman velite skirmisher in a wolfskin headdress over a helmet, small round parma shield, throwing a javelin, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman velite skirmisher in a wolfskin headdress over a helmet, small round parma shield, throwing a javelin*

```sh
mkdir -p build/art/troops_archers_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman velite skirmisher in a wolfskin headdress over a helmet, small round parma shield, throwing a javelin, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1008, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_archers_00_03/01_raw.png
echo wrote build/art/troops_archers_00_03/01_raw.png
```

## troops_archmages_00_03

**Replaces:** `troops/archmages_00.png`, `troops/archmages_01.png`, `troops/archmages_02.png`, `troops/archmages_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a winged Fury, a gaunt female spirit with dark feathered wings, snakes in her hair, clutching a burning torch, hovering, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a winged Fury, a gaunt female spirit with dark feathered wings, snakes in her hair, clutching a burning torch, hovering*

```sh
mkdir -p build/art/troops_archmages_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a winged Fury, a gaunt female spirit with dark feathered wings, snakes in her hair, clutching a burning torch, hovering, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1020, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_archmages_00_03/01_raw.png
echo wrote build/art/troops_archmages_00_03/01_raw.png
```

## troops_barbarians_00_03

**Replaces:** `troops/barbarians_00.png`, `troops/barbarians_01.png`, `troops/barbarians_02.png`, `troops/barbarians_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Sarmatian steppe warrior in full scale armour of overlapping plates, conical helmet, long lance, fur-trimmed cloak, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Sarmatian steppe warrior in full scale armour of overlapping plates, conical helmet, long lance, fur-trimmed cloak*

```sh
mkdir -p build/art/troops_barbarians_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Sarmatian steppe warrior in full scale armour of overlapping plates, conical helmet, long lance, fur-trimmed cloak, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1016, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_barbarians_00_03/01_raw.png
echo wrote build/art/troops_barbarians_00_03/01_raw.png
```

## troops_cavalry_00_03

**Replaces:** `troops/cavalry_00.png`, `troops/cavalry_01.png`, `troops/cavalry_02.png`, `troops/cavalry_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman cavalryman on a galloping horse, oval shield and crested helmet, couched lance, cloak streaming behind, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman cavalryman on a galloping horse, oval shield and crested helmet, couched lance, cloak streaming behind*

```sh
mkdir -p build/art/troops_cavalry_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman cavalryman on a galloping horse, oval shield and crested helmet, couched lance, cloak streaming behind, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1018, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_cavalry_00_03/01_raw.png
echo wrote build/art/troops_cavalry_00_03/01_raw.png
```

## troops_demons_00_03

**Replaces:** `troops/demons_00.png`, `troops/demons_01.png`, `troops/demons_02.png`, `troops/demons_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a shape-shifting devourer of Hecate, a lean winged demon with one bronze leg, bat wings, swinging a scythe, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a shape-shifting devourer of Hecate, a lean winged demon with one bronze leg, bat wings, swinging a scythe*

```sh
mkdir -p build/art/troops_demons_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a shape-shifting devourer of Hecate, a lean winged demon with one bronze leg, bat wings, swinging a scythe, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1023, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_demons_00_03/01_raw.png
echo wrote build/art/troops_demons_00_03/01_raw.png
```

## troops_dragons_00_03

**Replaces:** `troops/dragons_00.png`, `troops/dragons_01.png`, `troops/dragons_02.png`, `troops/dragons_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a great scaled dragon, wings spread, long serpentine neck, jaws open, standing on clawed feet, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a great scaled dragon, wings spread, long serpentine neck, jaws open, standing on clawed feet*

```sh
mkdir -p build/art/troops_dragons_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a great scaled dragon, wings spread, long serpentine neck, jaws open, standing on clawed feet, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1024, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_dragons_00_03/01_raw.png
echo wrote build/art/troops_dragons_00_03/01_raw.png
```

## troops_druids_00_03

**Replaces:** `troops/druids_00.png`, `troops/druids_01.png`, `troops/druids_02.png`, `troops/druids_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Celtic druid in a long white robe, oak-leaf wreath, holding a golden sickle and a gnarled staff, mist at his feet, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Celtic druid in a long white robe, oak-leaf wreath, holding a golden sickle and a gnarled staff, mist at his feet*

```sh
mkdir -p build/art/troops_druids_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Celtic druid in a long white robe, oak-leaf wreath, holding a golden sickle and a gnarled staff, mist at his feet, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1019, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_druids_00_03/01_raw.png
echo wrote build/art/troops_druids_00_03/01_raw.png
```

## troops_dwarves_00_03

**Replaces:** `troops/dwarves_00.png`, `troops/dwarves_01.png`, `troops/dwarves_02.png`, `troops/dwarves_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a stocky Ligurian mountain tribesman, thick beard, fur cloak over a leather cuirass, wielding a heavy axe, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a stocky Ligurian mountain tribesman, thick beard, fur cloak over a leather cuirass, wielding a heavy axe*

```sh
mkdir -p build/art/troops_dwarves_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a stocky Ligurian mountain tribesman, thick beard, fur cloak over a leather cuirass, wielding a heavy axe, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1012, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_dwarves_00_03/01_raw.png
echo wrote build/art/troops_dwarves_00_03/01_raw.png
```

## troops_elves_00_03

**Replaces:** `troops/elves_00.png`, `troops/elves_01.png`, `troops/elves_02.png`, `troops/elves_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a forest spirit archer in bark-toned robes with leaf-patterned cloak, drawing a longbow, antlered circlet, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a forest spirit archer in bark-toned robes with leaf-patterned cloak, drawing a longbow, antlered circlet*

```sh
mkdir -p build/art/troops_elves_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a forest spirit archer in bark-toned robes with leaf-patterned cloak, drawing a longbow, antlered circlet, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1009, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_elves_00_03/01_raw.png
echo wrote build/art/troops_elves_00_03/01_raw.png
```

## troops_ghosts_00_03

**Replaces:** `troops/ghosts_00.png`, `troops/ghosts_01.png`, `troops/ghosts_02.png`, `troops/ghosts_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a translucent ancestral shade, a hollow robed figure with no legs, trailing into vapour, faintly glowing, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a translucent ancestral shade, a hollow robed figure with no legs, trailing into vapour, faintly glowing*

```sh
mkdir -p build/art/troops_ghosts_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a translucent ancestral shade, a hollow robed figure with no legs, trailing into vapour, faintly glowing, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1013, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_ghosts_00_03/01_raw.png
echo wrote build/art/troops_ghosts_00_03/01_raw.png
```

## troops_giants_00_03

**Replaces:** `troops/giants_00.png`, `troops/giants_01.png`, `troops/giants_02.png`, `troops/giants_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a towering giant with serpent-scaled legs, wild hair and beard, raising a boulder overhead to throw, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a towering giant with serpent-scaled legs, wild hair and beard, raising a boulder overhead to throw*

```sh
mkdir -p build/art/troops_giants_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a towering giant with serpent-scaled legs, wild hair and beard, raising a boulder overhead to throw, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1022, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_giants_00_03/01_raw.png
echo wrote build/art/troops_giants_00_03/01_raw.png
```

## troops_gnomes_00_03

**Replaces:** `troops/gnomes_00.png`, `troops/gnomes_01.png`, `troops/gnomes_02.png`, `troops/gnomes_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a small woodland faun, goat legs and curling horns, shaggy pelt, holding a crooked branch, mischievous, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a small woodland faun, goat legs and curling horns, shaggy pelt, holding a crooked branch, mischievous*

```sh
mkdir -p build/art/troops_gnomes_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small woodland faun, goat legs and curling horns, shaggy pelt, holding a crooked branch, mischievous, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1006, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_gnomes_00_03/01_raw.png
echo wrote build/art/troops_gnomes_00_03/01_raw.png
```

## troops_knights_00_03

**Replaces:** `troops/knights_00.png`, `troops/knights_01.png`, `troops/knights_02.png`, `troops/knights_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> an elite Praetorian guardsman in polished ornate muscled cuirass, tall transverse-crested helmet, oval shield with scorpion emblem, gladius drawn, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: an elite Praetorian guardsman in polished ornate muscled cuirass, tall transverse-crested helmet, oval shield with scorpion emblem, gladius drawn*

```sh
mkdir -p build/art/troops_knights_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an elite Praetorian guardsman in polished ornate muscled cuirass, tall transverse-crested helmet, oval shield with scorpion emblem, gladius drawn, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1014, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_knights_00_03/01_raw.png
echo wrote build/art/troops_knights_00_03/01_raw.png
```

## troops_militia_00_03

**Replaces:** `troops/militia_00.png`, `troops/militia_01.png`, `troops/militia_02.png`, `troops/militia_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a raw Roman recruit in a plain undyed tunic and simple leather cap, holding a short spear and a small round shield, standing stiffly, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a raw Roman recruit in a plain undyed tunic and simple leather cap, holding a short spear and a small round shield, standing stiffly*

```sh
mkdir -p build/art/troops_militia_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a raw Roman recruit in a plain undyed tunic and simple leather cap, holding a short spear and a small round shield, standing stiffly, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1002, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_militia_00_03/01_raw.png
echo wrote build/art/troops_militia_00_03/01_raw.png
```

## troops_nomads_00_03

**Replaces:** `troops/nomads_00.png`, `troops/nomads_01.png`, `troops/nomads_02.png`, `troops/nomads_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Numidian light horseman riding bareback, no saddle or bridle, bare-chested in a leopard skin, hurling a javelin, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Numidian light horseman riding bareback, no saddle or bridle, bare-chested in a leopard skin, hurling a javelin*

```sh
mkdir -p build/art/troops_nomads_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Numidian light horseman riding bareback, no saddle or bridle, bare-chested in a leopard skin, hurling a javelin, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1011, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_nomads_00_03/01_raw.png
echo wrote build/art/troops_nomads_00_03/01_raw.png
```

## troops_ogres_00_03

**Replaces:** `troops/ogres_00.png`, `troops/ogres_01.png`, `troops/ogres_02.png`, `troops/ogres_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a huge one-eyed cave giant, single central eye, bare muscled torso, blacksmith's leather apron, swinging a massive hammer, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a huge one-eyed cave giant, single central eye, bare muscled torso, blacksmith's leather apron, swinging a massive hammer*

```sh
mkdir -p build/art/troops_ogres_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a huge one-eyed cave giant, single central eye, bare muscled torso, blacksmith's leather apron, swinging a massive hammer, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1015, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_ogres_00_03/01_raw.png
echo wrote build/art/troops_ogres_00_03/01_raw.png
```

## troops_orcs_00_03

**Replaces:** `troops/orcs_00.png`, `troops/orcs_01.png`, `troops/orcs_02.png`, `troops/orcs_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Balearic slinger, bare-chested and wiry, whirling a leather sling above his head, pouch of stones at his hip, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Balearic slinger, bare-chested and wiry, whirling a leather sling above his head, pouch of stones at his hip*

```sh
mkdir -p build/art/troops_orcs_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Balearic slinger, bare-chested and wiry, whirling a leather sling above his head, pouch of stones at his hip, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1007, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_orcs_00_03/01_raw.png
echo wrote build/art/troops_orcs_00_03/01_raw.png
```

## troops_peasants_00_03

**Replaces:** `troops/peasants_00.png`, `troops/peasants_01.png`, `troops/peasants_02.png`, `troops/peasants_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a ragged Roman tenant farmer in a torn dirty tunic, barefoot, holding a wooden pitchfork, stooped and unarmoured, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a ragged Roman tenant farmer in a torn dirty tunic, barefoot, holding a wooden pitchfork, stooped and unarmoured*

```sh
mkdir -p build/art/troops_peasants_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a ragged Roman tenant farmer in a torn dirty tunic, barefoot, holding a wooden pitchfork, stooped and unarmoured, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1000, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_peasants_00_03/01_raw.png
echo wrote build/art/troops_peasants_00_03/01_raw.png
```

## troops_pikemen_00_03

**Replaces:** `troops/pikemen_00.png`, `troops/pikemen_01.png`, `troops/pikemen_02.png`, `troops/pikemen_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman hastatus legionary in banded lorica segmentata, crested galea helmet, tall rectangular red and gold scutum, thrusting a spear forward, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman hastatus legionary in banded lorica segmentata, crested galea helmet, tall rectangular red and gold scutum, thrusting a spear forward*

```sh
mkdir -p build/art/troops_pikemen_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman hastatus legionary in banded lorica segmentata, crested galea helmet, tall rectangular red and gold scutum, thrusting a spear forward, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1010, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_pikemen_00_03/01_raw.png
echo wrote build/art/troops_pikemen_00_03/01_raw.png
```

## troops_skeletons_00_03

**Replaces:** `troops/skeletons_00.png`, `troops/skeletons_01.png`, `troops/skeletons_02.png`, `troops/skeletons_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a walking human skeleton in a rotted Roman tunic, hollow eye sockets, carrying a rusted short sword, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a walking human skeleton in a rotted Roman tunic, hollow eye sockets, carrying a rusted short sword*

```sh
mkdir -p build/art/troops_skeletons_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a walking human skeleton in a rotted Roman tunic, hollow eye sockets, carrying a rusted short sword, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1004, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_skeletons_00_03/01_raw.png
echo wrote build/art/troops_skeletons_00_03/01_raw.png
```

## troops_sprites_00_03

**Replaces:** `troops/sprites_00.png`, `troops/sprites_01.png`, `troops/sprites_02.png`, `troops/sprites_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a tiny glowing household spirit, a small floating robed figure with a faint halo, translucent and weightless, hovering above the ground, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a tiny glowing household spirit, a small floating robed figure with a faint halo, translucent and weightless, hovering above the ground*

```sh
mkdir -p build/art/troops_sprites_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a tiny glowing household spirit, a small floating robed figure with a faint halo, translucent and weightless, hovering above the ground, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1001, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_sprites_00_03/01_raw.png
echo wrote build/art/troops_sprites_00_03/01_raw.png
```

## troops_trolls_00_03

**Replaces:** `troops/trolls_00.png`, `troops/trolls_01.png`, `troops/trolls_02.png`, `troops/trolls_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a hulking earth-giant, hunched, skin caked in soil and moss, enormous hands, regenerating wounds glowing faintly, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a hulking earth-giant, hunched, skin caked in soil and moss, enormous hands, regenerating wounds glowing faintly*

```sh
mkdir -p build/art/troops_trolls_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a hulking earth-giant, hunched, skin caked in soil and moss, enormous hands, regenerating wounds glowing faintly, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1017, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_trolls_00_03/01_raw.png
echo wrote build/art/troops_trolls_00_03/01_raw.png
```

## troops_vampires_00_03

**Replaces:** `troops/vampires_00.png`, `troops/vampires_01.png`, `troops/vampires_02.png`, `troops/vampires_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a screech-owl vampire, a hunched winged creature with owl features and a human face, bloodied talons, membranous wings spread, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a screech-owl vampire, a hunched winged creature with owl features and a human face, bloodied talons, membranous wings spread*

```sh
mkdir -p build/art/troops_vampires_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a screech-owl vampire, a hunched winged creature with owl features and a human face, bloodied talons, membranous wings spread, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1021, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_vampires_00_03/01_raw.png
echo wrote build/art/troops_vampires_00_03/01_raw.png
```

## troops_wolves_00_03

**Replaces:** `troops/wolves_00.png`, `troops/wolves_01.png`, `troops/wolves_02.png`, `troops/wolves_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a lean grey wolf, snarling, head low, in mid-prowl, seen from the side, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a lean grey wolf, snarling, head low, in mid-prowl, seen from the side*

```sh
mkdir -p build/art/troops_wolves_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a lean grey wolf, snarling, head low, in mid-prowl, seen from the side, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1003, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_wolves_00_03/01_raw.png
echo wrote build/art/troops_wolves_00_03/01_raw.png
```

## troops_zombies_00_03

**Replaces:** `troops/zombies_00.png`, `troops/zombies_01.png`, `troops/zombies_02.png`, `troops/zombies_03.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a shambling rotted corpse in grave wrappings, arms hanging, hunched and slow, grey-green flesh, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a shambling rotted corpse in grave wrappings, arms hanging, hunched and slow, grey-green flesh*

```sh
mkdir -p build/art/troops_zombies_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a shambling rotted corpse in grave wrappings, arms hanging, hunched and slow, grey-green flesh, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1005, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/troops_zombies_00_03/01_raw.png
echo wrote build/art/troops_zombies_00_03/01_raw.png
```

# 5.2 Villains -- 17 items, 68 files, 48x34, 4 frames


## villains_alaric_00_03

**Replaces:** `villains/alaric_00.png`, `villains/alaric_01.png`, `villains/alaric_02.png`, `villains/alaric_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Visigoth king in looted Roman armour over furs, iron crown, heavy broadsword, a sacked city's smoke behind him, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Visigoth king in looted Roman armour over furs, iron crown, heavy broadsword, a sacked city's smoke behind him*

```sh
mkdir -p build/art/villains_alaric_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Visigoth king in looted Roman armour over furs, iron crown, heavy broadsword, a sacked city's smoke behind him, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1040, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_alaric_00_03/01_raw.png
echo wrote build/art/villains_alaric_00_03/01_raw.png
```

## villains_arminius_00_03

**Replaces:** `villains/arminius_00.png`, `villains/arminius_01.png`, `villains/arminius_02.png`, `villains/arminius_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Cheruscan chieftain in a bearskin over Roman mail, wolf-skull helmet, framea spear, standing among forest shadow, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Cheruscan chieftain in a bearskin over Roman mail, wolf-skull helmet, framea spear, standing among forest shadow*

```sh
mkdir -p build/art/villains_arminius_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Cheruscan chieftain in a bearskin over Roman mail, wolf-skull helmet, framea spear, standing among forest shadow, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1033, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_arminius_00_03/01_raw.png
echo wrote build/art/villains_arminius_00_03/01_raw.png
```

## villains_attila_00_03

**Replaces:** `villains/attila_00.png`, `villains/attila_01.png`, `villains/attila_02.png`, `villains/attila_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> the Hun warlord, wiry and fierce with a thin braided beard, lamellar armour and fur, composite bow and horsehair standard, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: the Hun warlord, wiry and fierce with a thin braided beard, lamellar armour and fur, composite bow and horsehair standard*

```sh
mkdir -p build/art/villains_attila_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the Hun warlord, wiry and fierce with a thin braided beard, lamellar armour and fur, composite bow and horsehair standard, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1041, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_attila_00_03/01_raw.png
echo wrote build/art/villains_attila_00_03/01_raw.png
```

## villains_boudica_00_03

**Replaces:** `villains/boudica_00.png`, `villains/boudica_01.png`, `villains/boudica_02.png`, `villains/boudica_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a tall Iceni warrior queen with long red hair, heavy torc at her throat, checked cloak, spear in hand, fierce, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a tall Iceni warrior queen with long red hair, heavy torc at her throat, checked cloak, spear in hand, fierce*

```sh
mkdir -p build/art/villains_boudica_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a tall Iceni warrior queen with long red hair, heavy torc at her throat, checked cloak, spear in hand, fierce, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1029, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_boudica_00_03/01_raw.png
echo wrote build/art/villains_boudica_00_03/01_raw.png
```

## villains_brennus_00_03

**Replaces:** `villains/brennus_00.png`, `villains/brennus_01.png`, `villains/brennus_02.png`, `villains/brennus_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Gallic warchief with a drooping moustache and lime-spiked hair, bare-chested with a heavy gold torc, long sword raised, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Gallic warchief with a drooping moustache and lime-spiked hair, bare-chested with a heavy gold torc, long sword raised*

```sh
mkdir -p build/art/villains_brennus_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Gallic warchief with a drooping moustache and lime-spiked hair, bare-chested with a heavy gold torc, long sword raised, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1031, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_brennus_00_03/01_raw.png
echo wrote build/art/villains_brennus_00_03/01_raw.png
```

## villains_catiline_00_03

**Replaces:** `villains/catiline_00.png`, `villains/catiline_01.png`, `villains/catiline_02.png`, `villains/catiline_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a disgraced Roman senator in a stained toga, gaunt and hollow-eyed, dagger half-hidden in the folds, conspiratorial, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a disgraced Roman senator in a stained toga, gaunt and hollow-eyed, dagger half-hidden in the folds, conspiratorial*

```sh
mkdir -p build/art/villains_catiline_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a disgraced Roman senator in a stained toga, gaunt and hollow-eyed, dagger half-hidden in the folds, conspiratorial, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1025, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_catiline_00_03/01_raw.png
echo wrote build/art/villains_catiline_00_03/01_raw.png
```

## villains_civilis_00_03

**Replaces:** `villains/civilis_00.png`, `villains/civilis_01.png`, `villains/civilis_02.png`, `villains/civilis_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Batavian auxiliary commander, one eye missing, Roman mail worn over Germanic trousers, long blond hair, holding a Roman standard, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Batavian auxiliary commander, one eye missing, Roman mail worn over Germanic trousers, long blond hair, holding a Roman standard*

```sh
mkdir -p build/art/villains_civilis_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Batavian auxiliary commander, one eye missing, Roman mail worn over Germanic trousers, long blond hair, holding a Roman standard, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1030, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_civilis_00_03/01_raw.png
echo wrote build/art/villains_civilis_00_03/01_raw.png
```

## villains_gildo_00_03

**Replaces:** `villains/gildo_00.png`, `villains/gildo_01.png`, `villains/gildo_02.png`, `villains/gildo_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Moorish count in flowing white desert robes over Roman officer armour, curved blade, dark-skinned and imposing, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Moorish count in flowing white desert robes over Roman officer armour, curved blade, dark-skinned and imposing*

```sh
mkdir -p build/art/villains_gildo_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Moorish count in flowing white desert robes over Roman officer armour, curved blade, dark-skinned and imposing, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1034, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_gildo_00_03/01_raw.png
echo wrote build/art/villains_gildo_00_03/01_raw.png
```

## villains_hannibal_00_03

**Replaces:** `villains/hannibal_00.png`, `villains/hannibal_01.png`, `villains/hannibal_02.png`, `villains/hannibal_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Carthaginian general in a crested Punic helmet, one eye scarred and blind, purple cloak, curved falcata sword, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Carthaginian general in a crested Punic helmet, one eye scarred and blind, purple cloak, curved falcata sword*

```sh
mkdir -p build/art/villains_hannibal_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Carthaginian general in a crested Punic helmet, one eye scarred and blind, purple cloak, curved falcata sword, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1037, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_hannibal_00_03/01_raw.png
echo wrote build/art/villains_hannibal_00_03/01_raw.png
```

## villains_jugurtha_00_03

**Replaces:** `villains/jugurtha_00.png`, `villains/jugurtha_01.png`, `villains/jugurtha_02.png`, `villains/jugurtha_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Numidian king in an ornate gold circlet and rich embroidered robes over a cuirass, arms folded, imperious, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Numidian king in an ornate gold circlet and rich embroidered robes over a cuirass, arms folded, imperious*

```sh
mkdir -p build/art/villains_jugurtha_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Numidian king in an ornate gold circlet and rich embroidered robes over a cuirass, arms folded, imperious, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1028, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_jugurtha_00_03/01_raw.png
echo wrote build/art/villains_jugurtha_00_03/01_raw.png
```

## villains_mithridates_00_03

**Replaces:** `villains/mithridates_00.png`, `villains/mithridates_01.png`, `villains/mithridates_02.png`, `villains/mithridates_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> an eastern king in a Persian tiara and richly patterned robes, jewelled scabbard, holding a phial of poison, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: an eastern king in a Persian tiara and richly patterned robes, jewelled scabbard, holding a phial of poison*

```sh
mkdir -p build/art/villains_mithridates_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an eastern king in a Persian tiara and richly patterned robes, jewelled scabbard, holding a phial of poison, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1036, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_mithridates_00_03/01_raw.png
echo wrote build/art/villains_mithridates_00_03/01_raw.png
```

## villains_pyrrhus_00_03

**Replaces:** `villains/pyrrhus_00.png`, `villains/pyrrhus_01.png`, `villains/pyrrhus_02.png`, `villains/pyrrhus_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Hellenistic king in a plumed Corinthian helmet and gilded muscled cuirass, round hoplite shield, long sarissa spear, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Hellenistic king in a plumed Corinthian helmet and gilded muscled cuirass, round hoplite shield, long sarissa spear*

```sh
mkdir -p build/art/villains_pyrrhus_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Hellenistic king in a plumed Corinthian helmet and gilded muscled cuirass, round hoplite shield, long sarissa spear, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1035, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_pyrrhus_00_03/01_raw.png
echo wrote build/art/villains_pyrrhus_00_03/01_raw.png
```

## villains_shapur_00_03

**Replaces:** `villains/shapur_00.png`, `villains/shapur_01.png`, `villains/shapur_02.png`, `villains/shapur_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Sasanian shah in a towering jewelled korymbos crown, full cataphract scale armour, long straight sword, regal, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Sasanian shah in a towering jewelled korymbos crown, full cataphract scale armour, long straight sword, regal*

```sh
mkdir -p build/art/villains_shapur_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Sasanian shah in a towering jewelled korymbos crown, full cataphract scale armour, long straight sword, regal, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1039, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_shapur_00_03/01_raw.png
echo wrote build/art/villains_shapur_00_03/01_raw.png
```

## villains_spartacus_00_03

**Replaces:** `villains/spartacus_00.png`, `villains/spartacus_01.png`, `villains/spartacus_02.png`, `villains/spartacus_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Thracian gladiator, bare-chested and scarred, gladiator helmet with a grille visor, short curved sica sword and small square shield, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Thracian gladiator, bare-chested and scarred, gladiator helmet with a grille visor, short curved sica sword and small square shield*

```sh
mkdir -p build/art/villains_spartacus_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Thracian gladiator, bare-chested and scarred, gladiator helmet with a grille visor, short curved sica sword and small square shield, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1026, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_spartacus_00_03/01_raw.png
echo wrote build/art/villains_spartacus_00_03/01_raw.png
```

## villains_tacfarinas_00_03

**Replaces:** `villains/tacfarinas_00.png`, `villains/tacfarinas_01.png`, `villains/tacfarinas_02.png`, `villains/tacfarinas_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Numidian deserter chieftain in a Roman military cloak over desert robes, javelins across his back, sun-darkened, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Numidian deserter chieftain in a Roman military cloak over desert robes, javelins across his back, sun-darkened*

```sh
mkdir -p build/art/villains_tacfarinas_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Numidian deserter chieftain in a Roman military cloak over desert robes, javelins across his back, sun-darkened, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1027, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_tacfarinas_00_03/01_raw.png
echo wrote build/art/villains_tacfarinas_00_03/01_raw.png
```

## villains_vercingetorix_00_03

**Replaces:** `villains/vercingetorix_00.png`, `villains/vercingetorix_01.png`, `villains/vercingetorix_02.png`, `villains/vercingetorix_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Gallic king in a horned helmet and mail shirt, long moustache, oval shield with a swirling Celtic device, longsword, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Gallic king in a horned helmet and mail shirt, long moustache, oval shield with a swirling Celtic device, longsword*

```sh
mkdir -p build/art/villains_vercingetorix_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Gallic king in a horned helmet and mail shirt, long moustache, oval shield with a swirling Celtic device, longsword, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1032, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_vercingetorix_00_03/01_raw.png
echo wrote build/art/villains_vercingetorix_00_03/01_raw.png
```

## villains_zenobia_00_03

**Replaces:** `villains/zenobia_00.png`, `villains/zenobia_01.png`, `villains/zenobia_02.png`, `villains/zenobia_03.png`
  
**Currently:** 48x34, already replaced → **generate at 96x96**

**Prompt**

> a Palmyrene warrior queen in gilded scale armour and an eastern diadem, dark braided hair, ornate curved sword, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Palmyrene warrior queen in gilded scale armour and an eastern diadem, dark braided hair, ornate curved sword*

```sh
mkdir -p build/art/villains_zenobia_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Palmyrene warrior queen in gilded scale armour and an eastern diadem, dark braided hair, ornate curved sword, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1038, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/villains_zenobia_00_03/01_raw.png
echo wrote build/art/villains_zenobia_00_03/01_raw.png
```

# 5.3 Class portraits -- 4 items, 4 files, 96x102


## classes_barbarian

**Replaces:** `classes/barbarian.png`
  
**Currently:** 96x102, placeholder (identical to King's Bounty) → **generate at 192x204**

**Prompt**

> a weathered frontier commander in mail over furs with a wolf-pelt cloak and iron torc, standing on a rock at a forest frontier, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a weathered frontier commander in mail over furs with a wolf-pelt cloak and iron torc, standing on a rock at a forest frontier*

```sh
mkdir -p build/art/classes_barbarian
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a weathered frontier commander in mail over furs with a wolf-pelt cloak and iron torc, standing on a rock at a forest frontier, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 192, "height": 204, "num_images": 1, "seed": 1045, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/classes_barbarian/01_raw.png
echo wrote build/art/classes_barbarian/01_raw.png
```

## classes_knight

**Replaces:** `classes/knight.png`
  
**Currently:** 96x102, placeholder (identical to King's Bounty) → **generate at 192x204**

**Prompt**

> a Roman general in a muscled bronze cuirass with lion-head shoulder pieces, red paludamentum cloak, crested helmet under one arm, standing before a distant fortified camp at sunset, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman general in a muscled bronze cuirass with lion-head shoulder pieces, red paludamentum cloak, crested helmet under one arm, standing before a distant fortified camp at sunset*

```sh
mkdir -p build/art/classes_knight
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman general in a muscled bronze cuirass with lion-head shoulder pieces, red paludamentum cloak, crested helmet under one arm, standing before a distant fortified camp at sunset, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 192, "height": 204, "num_images": 1, "seed": 1042, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/classes_knight/01_raw.png
echo wrote build/art/classes_knight/01_raw.png
```

## classes_paladin

**Replaces:** `classes/paladin.png`
  
**Currently:** 96x102, placeholder (identical to King's Bounty) → **generate at 192x204**

**Prompt**

> a Praetorian guardsman in an ornate scorpion-embossed cuirass with a tall black transverse crest, kneeling at a candlelit altar in a marble shrine, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Praetorian guardsman in an ornate scorpion-embossed cuirass with a tall black transverse crest, kneeling at a candlelit altar in a marble shrine*

```sh
mkdir -p build/art/classes_paladin
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Praetorian guardsman in an ornate scorpion-embossed cuirass with a tall black transverse crest, kneeling at a candlelit altar in a marble shrine, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 192, "height": 204, "num_images": 1, "seed": 1043, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/classes_paladin/01_raw.png
echo wrote build/art/classes_paladin/01_raw.png
```

## classes_sorceress

**Replaces:** `classes/sorceress.png`
  
**Currently:** 96x102, placeholder (identical to King's Bounty) → **generate at 192x204**

**Prompt**

> a veiled Vestal oracle-priestess in white robes with a gold fillet, holding a laurel sprig, standing in a temple interior lit by a sacred flame, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a veiled Vestal oracle-priestess in white robes with a gold fillet, holding a laurel sprig, standing in a temple interior lit by a sacred flame*

```sh
mkdir -p build/art/classes_sorceress
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a veiled Vestal oracle-priestess in white robes with a gold fillet, holding a laurel sprig, standing in a temple interior lit by a sacred flame, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 192, "height": 204, "num_images": 1, "seed": 1044, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/classes_sorceress/01_raw.png
echo wrote build/art/classes_sorceress/01_raw.png
```

# 5.4 Hero -- 16 walk + 16 idle files, 48x34


## sprites_hero_idle_facing_00_03

**Replaces:** `sprites/hero_idle_<facing>_00.png`, `sprites/hero_idle_<facing>_01.png`, `sprites/hero_idle_<facing>_02.png`, `sprites/hero_idle_<facing>_03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> the same rider at rest, horse shifting weight, cloak stirring, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: the same rider at rest, horse shifting weight, cloak stirring*

```sh
mkdir -p build/art/sprites_hero_idle_facing_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the same rider at rest, horse shifting weight, cloak stirring, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1050, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/sprites_hero_idle_facing_00_03/01_raw.png
echo wrote build/art/sprites_hero_idle_facing_00_03/01_raw.png
```

## sprites_hero_walk_east_00_03

**Replaces:** `sprites/hero_walk_east_00.png`, `sprites/hero_walk_east_01.png`, `sprites/hero_walk_east_02.png`, `sprites/hero_walk_east_03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> the same rider in profile facing right, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: the same rider in profile facing right*

```sh
mkdir -p build/art/sprites_hero_walk_east_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the same rider in profile facing right, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1047, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_east_00_03/01_raw.png
echo wrote build/art/sprites_hero_walk_east_00_03/01_raw.png
```

## sprites_hero_walk_north_00_03

**Replaces:** `sprites/hero_walk_north_00.png`, `sprites/hero_walk_north_01.png`, `sprites/hero_walk_north_02.png`, `sprites/hero_walk_north_03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> the same rider seen from behind, cloak and crest from the rear, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: the same rider seen from behind, cloak and crest from the rear*

```sh
mkdir -p build/art/sprites_hero_walk_north_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the same rider seen from behind, cloak and crest from the rear, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1049, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_north_00_03/01_raw.png
echo wrote build/art/sprites_hero_walk_north_00_03/01_raw.png
```

## sprites_hero_walk_south_00_03

**Replaces:** `sprites/hero_walk_south_00.png`, `sprites/hero_walk_south_01.png`, `sprites/hero_walk_south_02.png`, `sprites/hero_walk_south_03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> a Roman commander on horseback seen from the front, red cloak, crested helmet, gilded cuirass, riding toward the viewer, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman commander on horseback seen from the front, red cloak, crested helmet, gilded cuirass, riding toward the viewer*

```sh
mkdir -p build/art/sprites_hero_walk_south_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on horseback seen from the front, red cloak, crested helmet, gilded cuirass, riding toward the viewer, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1046, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_south_00_03/01_raw.png
echo wrote build/art/sprites_hero_walk_south_00_03/01_raw.png
```

## sprites_hero_walk_west_00_03

**Replaces:** `sprites/hero_walk_west_00.png`, `sprites/hero_walk_west_01.png`, `sprites/hero_walk_west_02.png`, `sprites/hero_walk_west_03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> the same rider in profile facing left, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: the same rider in profile facing left*

```sh
mkdir -p build/art/sprites_hero_walk_west_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the same rider in profile facing left, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1048, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/sprites_hero_walk_west_00_03/01_raw.png
echo wrote build/art/sprites_hero_walk_west_00_03/01_raw.png
```

# 5.5 Boat -- 4 items, 16 files, 48x34


## sprites_boat_east_00_03

**Replaces:** `sprites/boat_east_00.png`, `sprites/boat_east_01.png`, `sprites/boat_east_02.png`, `sprites/boat_east_03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> the same vessel in full profile facing right, sail and oar bank visible, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: the same vessel in full profile facing right, sail and oar bank visible*

```sh
mkdir -p build/art/sprites_boat_east_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the same vessel in full profile facing right, sail and oar bank visible, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1052, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/sprites_boat_east_00_03/01_raw.png
echo wrote build/art/sprites_boat_east_00_03/01_raw.png
```

## sprites_boat_north_00_03

**Replaces:** `sprites/boat_north_00.png`, `sprites/boat_north_01.png`, `sprites/boat_north_02.png`, `sprites/boat_north_03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> the same vessel from astern, steering oar and sail from behind, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: the same vessel from astern, steering oar and sail from behind*

```sh
mkdir -p build/art/sprites_boat_north_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the same vessel from astern, steering oar and sail from behind, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1054, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/sprites_boat_north_00_03/01_raw.png
echo wrote build/art/sprites_boat_north_00_03/01_raw.png
```

## sprites_boat_south_00_03

**Replaces:** `sprites/boat_south_00.png`, `sprites/boat_south_01.png`, `sprites/boat_south_02.png`, `sprites/boat_south_03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> a Roman liburnian galley seen bow-on, single square sail with a painted eagle, eye painted on the prow, oars out, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman liburnian galley seen bow-on, single square sail with a painted eagle, eye painted on the prow, oars out*

```sh
mkdir -p build/art/sprites_boat_south_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman liburnian galley seen bow-on, single square sail with a painted eagle, eye painted on the prow, oars out, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1051, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/sprites_boat_south_00_03/01_raw.png
echo wrote build/art/sprites_boat_south_00_03/01_raw.png
```

## sprites_boat_west_00_03

**Replaces:** `sprites/boat_west_00.png`, `sprites/boat_west_01.png`, `sprites/boat_west_02.png`, `sprites/boat_west_03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> the same vessel in profile facing left, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: the same vessel in profile facing left*

```sh
mkdir -p build/art/sprites_boat_west_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the same vessel in profile facing left, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1053, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/sprites_boat_west_00_03/01_raw.png
echo wrote build/art/sprites_boat_west_00_03/01_raw.png
```

# 5.6 Base terrain -- 6 items, 6 files, 48x34, opaque, seamless


## tiles_desert

**Replaces:** `tiles/desert.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat pale sand ground covered with many small short horizontal streaks of darker tan and near-white in strong contrast, the streaks spread evenly and randomly with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

*Original brief wording: a fine texture of rippled sand, uniform wind ripples, no dune silhouette, seamless*

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
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_desert/01_raw.png
echo wrote build/art/tiles_desert/01_raw.png
```

## tiles_forest

**Replaces:** `tiles/forest.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat dark green ground covered with many small rounded clumps of mid green and near-black green in strong contrast, the clumps packed evenly and randomly with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

*Original brief wording: a fine dense texture of treetop canopy foliage from directly above, uniform grain, no individual tree readable, seamless*

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
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_forest/01_raw.png
echo wrote build/art/tiles_forest/01_raw.png
```

## tiles_grass

**Replaces:** `tiles/grass.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**
  
**Already generated:** 5 run(s) at `build/art/terrain_grass/` (run05 is newest)

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat medium green ground covered with many small short specks of bright yellow-green and of dark green in strong contrast, the specks evenly and randomly spread with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

*Original brief wording: a fine even texture of short dry Mediterranean grass, uniform speckled detail, no distinct plants or objects, seamless*

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
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_grass/01_raw.png
echo wrote build/art/tiles_grass/01_raw.png
```

## tiles_grass_variant

**Replaces:** `tiles/grass_variant.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat medium green ground covered with small specks of yellow-green and dark green, with a few slightly darker scrub flecks mixed evenly through it, everything spread with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

*Original brief wording: the same grass texture with a few scattered darker scrub patches*

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
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_grass_variant/01_raw.png
echo wrote build/art/tiles_grass_variant/01_raw.png
```

## tiles_mountain

**Replaces:** `tiles/mountain.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat mid grey ground covered with many small angular chips of light grey and dark grey in strong contrast, the chips spread evenly and randomly with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

*Original brief wording: a fine texture of broken grey limestone scree and rock, uniform grain, no distinct peak or feature, seamless*

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
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_mountain/01_raw.png
echo wrote build/art/tiles_mountain/01_raw.png
```

## tiles_water

**Replaces:** `tiles/water.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**
  
**Already generated:** 8 run(s) at `build/art/terrain_water/` (run08 is newest)

**Prompt**

> a small crop cut from the middle of a much larger sheet of wrapping paper, the paper is printed all over with an endless repeating pattern: a flat medium blue ground covered with many small short horizontal dashes of bright pale cyan in strong contrast, the dashes evenly and randomly spread with the same density everywhere, the crop is taken from deep inside the sheet so no edge or border of the paper is visible anywhere, the pattern simply continues past all four sides, flat colours, hard edges, no blur, no gradient, no shading, no light, no depth

*Original brief wording: a fine even texture of open sea with small repeating wave crests, no horizon, no shore, seamless*

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
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_water/01_raw.png
echo wrote build/art/tiles_water/01_raw.png
```

# 5.8 Structures -- 6 items, 11 files, 48x34


## tiles_castle_png

**Replaces:** `tiles/castle_br.png`, `tiles/castle_gate.png`, `tiles/castle_ml.png`, `tiles/castle_mr.png`, `tiles/castle_tl.png`, `tiles/castle_tr.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman legionary fortress with ashlar stone walls, square corner towers, red tile roofs and an arched gate, seen from a high angle, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman legionary fortress with ashlar stone walls, square corner towers, red tile roofs and an arched gate, seen from a high angle*

```sh
mkdir -p build/art/tiles_castle_png
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman legionary fortress with ashlar stone walls, square corner towers, red tile roofs and an arched gate, seen from a high angle, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1061, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_castle_png/01_raw.png
echo wrote build/art/tiles_castle_png/01_raw.png
```

## tiles_dwelling_dungeon

**Replaces:** `tiles/dwelling_dungeon.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman columbarium crypt entrance, stone doorway flanked by funerary urns, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman columbarium crypt entrance, stone doorway flanked by funerary urns*

```sh
mkdir -p build/art/tiles_dwelling_dungeon
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman columbarium crypt entrance, stone doorway flanked by funerary urns, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1066, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_dwelling_dungeon/01_raw.png
echo wrote build/art/tiles_dwelling_dungeon/01_raw.png
```

## tiles_dwelling_forest

**Replaces:** `tiles/dwelling_forest.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a stone shrine and altar in a sacred grove, surrounded by trees, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a stone shrine and altar in a sacred grove, surrounded by trees*

```sh
mkdir -p build/art/tiles_dwelling_forest
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a stone shrine and altar in a sacred grove, surrounded by trees, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1064, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_dwelling_forest/01_raw.png
echo wrote build/art/tiles_dwelling_forest/01_raw.png
```

## tiles_dwelling_hills

**Replaces:** `tiles/dwelling_hills.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a cave mouth in a rocky hillside with a carved stone lintel, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a cave mouth in a rocky hillside with a carved stone lintel*

```sh
mkdir -p build/art/tiles_dwelling_hills
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a cave mouth in a rocky hillside with a carved stone lintel, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1065, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_dwelling_hills/01_raw.png
echo wrote build/art/tiles_dwelling_hills/01_raw.png
```

## tiles_dwelling_plains

**Replaces:** `tiles/dwelling_plains.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman villa rustica farmstead with a tiled roof and a walled paddock, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman villa rustica farmstead with a tiled roof and a walled paddock*

```sh
mkdir -p build/art/tiles_dwelling_plains
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman villa rustica farmstead with a tiled roof and a walled paddock, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1063, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_dwelling_plains/01_raw.png
echo wrote build/art/tiles_dwelling_plains/01_raw.png
```

## tiles_town

**Replaces:** `tiles/town.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a small walled Roman town with terracotta roofs, a temple pediment and a column, seen from a high angle, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a small walled Roman town with terracotta roofs, a temple pediment and a column, seen from a high angle*

```sh
mkdir -p build/art/tiles_town
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a small walled Roman town with terracotta roofs, a temple pediment and a column, seen from a high angle, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1062, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_town/01_raw.png
echo wrote build/art/tiles_town/01_raw.png
```

# 5.9 Map objects -- 7 items, 7 files, 48x34, transparent surrounds


## tiles_artifact_chest

**Replaces:** `tiles/artifact_chest.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**
  
**Already generated:** 1 run(s) at `build/art/artifact_chest/` (run01 is newest)

**Prompt**

> an ornate gilded reliquary casket with glowing seams, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: an ornate gilded reliquary casket with glowing seams*

```sh
mkdir -p build/art/tiles_artifact_chest
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an ornate gilded reliquary casket with glowing seams, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1068, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_artifact_chest/01_raw.png
echo wrote build/art/tiles_artifact_chest/01_raw.png
```

## tiles_artifact_ring

**Replaces:** `tiles/artifact_ring.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**
  
**Already generated:** 1 run(s) at `build/art/artifact_ring/` (run01 is newest)

**Prompt**

> a golden ring resting on a small stone plinth, radiating light, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a golden ring resting on a small stone plinth, radiating light*

```sh
mkdir -p build/art/tiles_artifact_ring
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden ring resting on a small stone plinth, radiating light, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1069, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_artifact_ring/01_raw.png
echo wrote build/art/tiles_artifact_ring/01_raw.png
```

## tiles_bridge_h

**Replaces:** `tiles/bridge_h.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman stone arch bridge spanning left to right, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman stone arch bridge spanning left to right*

```sh
mkdir -p build/art/tiles_bridge_h
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman stone arch bridge spanning left to right, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1071, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_bridge_h/01_raw.png
echo wrote build/art/tiles_bridge_h/01_raw.png
```

## tiles_bridge_v

**Replaces:** `tiles/bridge_v.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman stone arch bridge spanning top to bottom, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman stone arch bridge spanning top to bottom*

```sh
mkdir -p build/art/tiles_bridge_v
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman stone arch bridge spanning top to bottom, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1072, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_bridge_v/01_raw.png
echo wrote build/art/tiles_bridge_v/01_raw.png
```

## tiles_chest

**Replaces:** `tiles/chest.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman strongbox, an iron-banded wooden arca with bronze studs, lid closed, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman strongbox, an iron-banded wooden arca with bronze studs, lid closed*

```sh
mkdir -p build/art/tiles_chest
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman strongbox, an iron-banded wooden arca with bronze studs, lid closed, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1067, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_chest/01_raw.png
echo wrote build/art/tiles_chest/01_raw.png
```

## tiles_sign

**Replaces:** `tiles/sign.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman milestone, a cylindrical stone column with carved lettering, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman milestone, a cylindrical stone column with carved lettering*

```sh
mkdir -p build/art/tiles_sign
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman milestone, a cylindrical stone column with carved lettering, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1070, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_sign/01_raw.png
echo wrote build/art/tiles_sign/01_raw.png
```

## tiles_wandering_army

**Replaces:** `tiles/wandering_army.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a barbarian war standard planted in the ground, a spear hung with skulls and a horsehair tuft, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a barbarian war standard planted in the ground, a spear hung with skulls and a horsehair tuft*

```sh
mkdir -p build/art/tiles_wandering_army
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a barbarian war standard planted in the ground, a spear hung with skulls and a horsehair tuft, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1073, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/tiles_wandering_army/01_raw.png
echo wrote build/art/tiles_wandering_army/01_raw.png
```

# 5.10 Combat arena -- 5 items, 15 files, 48x34


## combat_castle_spike

**Replaces:** `combat/castle_spike.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a row of sharpened wooden stakes driven into the ground at an angle, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a row of sharpened wooden stakes driven into the ground at an angle*

```sh
mkdir -p build/art/combat_castle_spike
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a row of sharpened wooden stakes driven into the ground at an angle, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1077, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/combat_castle_spike/01_raw.png
echo wrote build/art/combat_castle_spike/01_raw.png
```

## combat_castle_wall_01_06

**Replaces:** `combat/castle_wall_01..06.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> Roman fortress wall segments in ashlar stone with crenellations, six varied sections, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: Roman fortress wall segments in ashlar stone with crenellations, six varied sections*

```sh
mkdir -p build/art/combat_castle_wall_01_06
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "Roman fortress wall segments in ashlar stone with crenellations, six varied sections, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1076, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/combat_castle_wall_01_06/01_raw.png
echo wrote build/art/combat_castle_wall_01_06/01_raw.png
```

## combat_cursor_01_04

**Replaces:** `combat/cursor_01..04.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> a thin bronze laurel-wreath ring outline, hollow centre, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a thin bronze laurel-wreath ring outline, hollow centre*

```sh
mkdir -p build/art/combat_cursor_01_04
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a thin bronze laurel-wreath ring outline, hollow centre, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1078, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/combat_cursor_01_04/01_raw.png
echo wrote build/art/combat_cursor_01_04/01_raw.png
```

## combat_field_grass

**Replaces:** `combat/field_grass.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> trampled battlefield turf with bare patches of earth, seen from above (tile route, seamless), the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: trampled battlefield turf with bare patches of earth, seen from above (tile route, seamless)*

```sh
mkdir -p build/art/combat_field_grass
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "trampled battlefield turf with bare patches of earth, seen from above (tile route, seamless), the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1074, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/combat_field_grass/01_raw.png
echo wrote build/art/combat_field_grass/01_raw.png
```

## combat_obstacle_01_03

**Replaces:** `combat/obstacle_01..03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> a mossy boulder / a dry thorn scrub bush / a fallen broken column drum, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a mossy boulder / a dry thorn scrub bush / a fallen broken column drum*

```sh
mkdir -p build/art/combat_obstacle_01_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a mossy boulder / a dry thorn scrub bush / a fallen broken column drum, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1075, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/combat_obstacle_01_03/01_raw.png
echo wrote build/art/combat_obstacle_01_03/01_raw.png
```

# 5.11 Artifact icons -- 8 items, 8 files, 48x34


## ui_inventory_artifact_amulet

**Replaces:** `ui/inventory_artifact_amulet.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a gold locket amulet on a cord, embossed with a lightning bolt, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a gold locket amulet on a cord, embossed with a lightning bolt*

```sh
mkdir -p build/art/ui_inventory_artifact_amulet
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a gold locket amulet on a cord, embossed with a lightning bolt, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1083, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_amulet/01_raw.png
echo wrote build/art/ui_inventory_artifact_amulet/01_raw.png
```

## ui_inventory_artifact_anchor

**Replaces:** `ui/inventory_artifact_anchor.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a bronze anchor with a trident-shaped crossbar, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a bronze anchor with a trident-shaped crossbar*

```sh
mkdir -p build/art/ui_inventory_artifact_anchor
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a bronze anchor with a trident-shaped crossbar, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1086, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_anchor/01_raw.png
echo wrote build/art/ui_inventory_artifact_anchor/01_raw.png
```

## ui_inventory_artifact_articles

**Replaces:** `ui/inventory_artifact_articles.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a bronze inscribed tablet with a hanging wax seal, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a bronze inscribed tablet with a hanging wax seal*

```sh
mkdir -p build/art/ui_inventory_artifact_articles
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a bronze inscribed tablet with a hanging wax seal, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1082, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_articles/01_raw.png
echo wrote build/art/ui_inventory_artifact_articles/01_raw.png
```

## ui_inventory_artifact_book

**Replaces:** `ui/inventory_artifact_book.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a torn scrap of ancient papyrus with faded illegible script, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a torn scrap of ancient papyrus with faded illegible script*

```sh
mkdir -p build/art/ui_inventory_artifact_book
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a torn scrap of ancient papyrus with faded illegible script, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1085, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_book/01_raw.png
echo wrote build/art/ui_inventory_artifact_book/01_raw.png
```

## ui_inventory_artifact_crown

**Replaces:** `ui/inventory_artifact_crown.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a golden laurel wreath crown, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a golden laurel wreath crown*

```sh
mkdir -p build/art/ui_inventory_artifact_crown
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden laurel wreath crown, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1081, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_crown/01_raw.png
echo wrote build/art/ui_inventory_artifact_crown/01_raw.png
```

## ui_inventory_artifact_ring

**Replaces:** `ui/inventory_artifact_ring.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a heavy gold equestrian signet ring, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a heavy gold equestrian signet ring*

```sh
mkdir -p build/art/ui_inventory_artifact_ring
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heavy gold equestrian signet ring, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1084, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_ring/01_raw.png
echo wrote build/art/ui_inventory_artifact_ring/01_raw.png
```

## ui_inventory_artifact_shield

**Replaces:** `ui/inventory_artifact_shield.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a curved rectangular Roman shield bearing a Trojan palladium device, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a curved rectangular Roman shield bearing a Trojan palladium device*

```sh
mkdir -p build/art/ui_inventory_artifact_shield
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a curved rectangular Roman shield bearing a Trojan palladium device, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1080, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_shield/01_raw.png
echo wrote build/art/ui_inventory_artifact_shield/01_raw.png
```

## ui_inventory_artifact_sword

**Replaces:** `ui/inventory_artifact_sword.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> an ornate gladius short sword with a ruby pommel and flame etching on the blade, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: an ornate gladius short sword with a ruby pommel and flame etching on the blade*

```sh
mkdir -p build/art/ui_inventory_artifact_sword
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an ornate gladius short sword with a ruby pommel and flame etching on the blade, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1079, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_artifact_sword/01_raw.png
echo wrote build/art/ui_inventory_artifact_sword/01_raw.png
```

# 5.12 Continent emblems -- 4 items, 4 files, 48x34


## ui_inventory_zone_archipelia

**Replaces:** `ui/inventory_zone_archipelia.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a heraldic emblem of a palm and an elephant above a coastal strip and dunes, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a heraldic emblem of a palm and an elephant above a coastal strip and dunes*

```sh
mkdir -p build/art/ui_inventory_zone_archipelia
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heraldic emblem of a palm and an elephant above a coastal strip and dunes, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1089, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_zone_archipelia/01_raw.png
echo wrote build/art/ui_inventory_zone_archipelia/01_raw.png
```

## ui_inventory_zone_continentia

**Replaces:** `ui/inventory_zone_continentia.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a heraldic emblem of the Italian peninsula with a she-wolf and laurel, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a heraldic emblem of the Italian peninsula with a she-wolf and laurel*

```sh
mkdir -p build/art/ui_inventory_zone_continentia
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heraldic emblem of the Italian peninsula with a she-wolf and laurel, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1087, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_zone_continentia/01_raw.png
echo wrote build/art/ui_inventory_zone_continentia/01_raw.png
```

## ui_inventory_zone_forestria

**Replaces:** `ui/inventory_zone_forestria.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a heraldic emblem of forested hills with a Gallic cockerel and a river bridge, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a heraldic emblem of forested hills with a Gallic cockerel and a river bridge*

```sh
mkdir -p build/art/ui_inventory_zone_forestria
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heraldic emblem of forested hills with a Gallic cockerel and a river bridge, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1088, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_zone_forestria/01_raw.png
echo wrote build/art/ui_inventory_zone_forestria/01_raw.png
```

## ui_inventory_zone_saharia

**Replaces:** `ui/inventory_zone_saharia.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a heraldic emblem of a domed eastern skyline with a palm and a sun rising over mountains, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a heraldic emblem of a domed eastern skyline with a palm and a sun rising over mountains*

```sh
mkdir -p build/art/ui_inventory_zone_saharia
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a heraldic emblem of a domed eastern skyline with a palm and a sun rising over mountains, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1090, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_inventory_zone_saharia/01_raw.png
echo wrote build/art/ui_inventory_zone_saharia/01_raw.png
```

# 5.13 Location backdrops -- 6 items, 6 files, 240x102


## ui_backdrop_castle

**Replaces:** `ui/backdrop_castle.png`
  
**Currently:** 240x102, placeholder (identical to King's Bounty) → **generate at 480x204**

**Prompt**

> a fortress hall interior with hanging legionary standards, a brazier and stone arches, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a fortress hall interior with hanging legionary standards, a brazier and stone arches*

```sh
mkdir -p build/art/ui_backdrop_castle
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a fortress hall interior with hanging legionary standards, a brazier and stone arches, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 480, "height": 204, "num_images": 1, "seed": 1092, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_backdrop_castle/01_raw.png
echo wrote build/art/ui_backdrop_castle/01_raw.png
```

## ui_backdrop_dungeon

**Replaces:** `ui/backdrop_dungeon.png`
  
**Currently:** 240x102, placeholder (identical to King's Bounty) → **generate at 480x204**

**Prompt**

> a crypt interior with columbarium niches and torchlight, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a crypt interior with columbarium niches and torchlight*

```sh
mkdir -p build/art/ui_backdrop_dungeon
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a crypt interior with columbarium niches and torchlight, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 480, "height": 204, "num_images": 1, "seed": 1096, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_backdrop_dungeon/01_raw.png
echo wrote build/art/ui_backdrop_dungeon/01_raw.png
```

## ui_backdrop_forest

**Replaces:** `ui/backdrop_forest.png`
  
**Currently:** 240x102, placeholder (identical to King's Bounty) → **generate at 480x204**

**Prompt**

> the interior of a dense sacred grove with shafts of light through the canopy, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: the interior of a dense sacred grove with shafts of light through the canopy*

```sh
mkdir -p build/art/ui_backdrop_forest
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "the interior of a dense sacred grove with shafts of light through the canopy, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 480, "height": 204, "num_images": 1, "seed": 1094, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_backdrop_forest/01_raw.png
echo wrote build/art/ui_backdrop_forest/01_raw.png
```

## ui_backdrop_hillcave

**Replaces:** `ui/backdrop_hillcave.png`
  
**Currently:** 240x102, placeholder (identical to King's Bounty) → **generate at 480x204**

**Prompt**

> a cave mouth in rocky hills with oracle smoke drifting from the opening, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a cave mouth in rocky hills with oracle smoke drifting from the opening*

```sh
mkdir -p build/art/ui_backdrop_hillcave
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a cave mouth in rocky hills with oracle smoke drifting from the opening, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 480, "height": 204, "num_images": 1, "seed": 1095, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_backdrop_hillcave/01_raw.png
echo wrote build/art/ui_backdrop_hillcave/01_raw.png
```

## ui_backdrop_plains

**Replaces:** `ui/backdrop_plains.png`
  
**Currently:** 240x102, placeholder (identical to King's Bounty) → **generate at 480x204**

**Prompt**

> open Italian countryside with cypress trees and distant blue hills, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: open Italian countryside with cypress trees and distant blue hills*

```sh
mkdir -p build/art/ui_backdrop_plains
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "open Italian countryside with cypress trees and distant blue hills, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 480, "height": 204, "num_images": 1, "seed": 1093, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_backdrop_plains/01_raw.png
echo wrote build/art/ui_backdrop_plains/01_raw.png
```

## ui_backdrop_town

**Replaces:** `ui/backdrop_town.png`
  
**Currently:** 240x102, placeholder (identical to King's Bounty) → **generate at 480x204**

**Prompt**

> a Roman forum street with a colonnade, market awnings and townspeople, wide view, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman forum street with a colonnade, market awnings and townspeople, wide view*

```sh
mkdir -p build/art/ui_backdrop_town
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman forum street with a colonnade, market awnings and townspeople, wide view, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 480, "height": 204, "num_images": 1, "seed": 1091, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_backdrop_town/01_raw.png
echo wrote build/art/ui_backdrop_town/01_raw.png
```

# 5.14 Sidebar HUD -- 7 items, 14 files


## ui_hud_contract_silhouette

**Replaces:** `ui/hud_contract_silhouette.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a blank dark rolled scroll, flat silhouette, no interior detail, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a blank dark rolled scroll, flat silhouette, no interior detail*

```sh
mkdir -p build/art/ui_hud_contract_silhouette
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a blank dark rolled scroll, flat silhouette, no interior detail, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1099, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_hud_contract_silhouette/01_raw.png
echo wrote build/art/ui_hud_contract_silhouette/01_raw.png
```

## ui_hud_gold_purse

**Replaces:** `ui/hud_gold_purse.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a leather coin purse spilling gold aurei, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a leather coin purse spilling gold aurei*

```sh
mkdir -p build/art/ui_hud_gold_purse
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a leather coin purse spilling gold aurei, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1097, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_hud_gold_purse/01_raw.png
echo wrote build/art/ui_hud_gold_purse/01_raw.png
```

## ui_hud_magic_00_03

**Replaces:** `ui/hud_magic_00..03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> a golden oracle flame burning above a bronze tripod, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a golden oracle flame burning above a bronze tripod*

```sh
mkdir -p build/art/ui_hud_magic_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden oracle flame burning above a bronze tripod, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1103, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_hud_magic_00_03/01_raw.png
echo wrote build/art/ui_hud_magic_00_03/01_raw.png
```

## ui_hud_magic_silhouette

**Replaces:** `ui/hud_magic_silhouette.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a dark flat silhouette of an eight-pointed star, no interior detail, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a dark flat silhouette of an eight-pointed star, no interior detail*

```sh
mkdir -p build/art/ui_hud_magic_silhouette
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a dark flat silhouette of an eight-pointed star, no interior detail, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1102, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_hud_magic_silhouette/01_raw.png
echo wrote build/art/ui_hud_magic_silhouette/01_raw.png
```

## ui_hud_puzzle_grid

**Replaces:** `ui/hud_puzzle_grid.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> an ornate carved stone frame enclosing an empty square panel, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: an ornate carved stone frame enclosing an empty square panel*

```sh
mkdir -p build/art/ui_hud_puzzle_grid
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "an ornate carved stone frame enclosing an empty square panel, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1098, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_hud_puzzle_grid/01_raw.png
echo wrote build/art/ui_hud_puzzle_grid/01_raw.png
```

## ui_hud_siege_00_03

**Replaces:** `ui/hud_siege_00..03.png`
  
**Currently:** ?, missing → **generate at 96x96**

**Prompt**

> a Roman onager catapult firing, arm swinging forward, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman onager catapult firing, arm swinging forward*

```sh
mkdir -p build/art/ui_hud_siege_00_03
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman onager catapult firing, arm swinging forward, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1101, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_hud_siege_00_03/01_raw.png
echo wrote build/art/ui_hud_siege_00_03/01_raw.png
```

## ui_hud_siege_silhouette

**Replaces:** `ui/hud_siege_silhouette.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a dark flat silhouette of a siege tower, no interior detail, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a dark flat silhouette of a siege tower, no interior detail*

```sh
mkdir -p build/art/ui_hud_siege_silhouette
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a dark flat silhouette of a siege tower, no interior detail, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1100, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_hud_siege_silhouette/01_raw.png
echo wrote build/art/ui_hud_siege_silhouette/01_raw.png
```

# 5.15 Victory cartoon -- 3 items, 3 files, 48x34


## ui_end_carpet

**Replaces:** `ui/end_carpet.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a grey stone flagstone pavement texture, regular blocks, seamless, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a grey stone flagstone pavement texture, regular blocks, seamless*

```sh
mkdir -p build/art/ui_end_carpet
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a grey stone flagstone pavement texture, regular blocks, seamless, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1105, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_end_carpet/01_raw.png
echo wrote build/art/ui_end_carpet/01_raw.png
```

## ui_end_grass

**Replaces:** `ui/end_grass.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a fine even texture of green turf, uniform, seamless, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a fine even texture of green turf, uniform, seamless*

```sh
mkdir -p build/art/ui_end_grass
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a fine even texture of green turf, uniform, seamless, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1104, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_end_grass/01_raw.png
echo wrote build/art/ui_end_grass/01_raw.png
```

## ui_end_hero

**Replaces:** `ui/end_hero.png`
  
**Currently:** 48x34, placeholder (identical to King's Bounty) → **generate at 96x96**

**Prompt**

> a Roman commander on a white horse in profile facing right, red cloak, on transparency, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman commander on a white horse in profile facing right, red cloak, on transparency*

```sh
mkdir -p build/art/ui_end_hero
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman commander on a white horse in profile facing right, red cloak, on transparency, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 96, "height": 96, "num_images": 1, "seed": 1106, "async": true, "remove_bg": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_end_hero/01_raw.png
echo wrote build/art/ui_end_hero/01_raw.png
```

# 5.16 Screens and logos -- 6 items, 6 files


## ui_class_select_highlight

**Replaces:** `ui/class_select_highlight.png`
  
**Currently:** 42x44, placeholder (identical to King's Bounty) → **generate at 84x88**

**Prompt**

> a golden laurel selection frame, hollow centre, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a golden laurel selection frame, hollow centre*

```sh
mkdir -p build/art/ui_class_select_highlight
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden laurel selection frame, hollow centre, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 84, "height": 88, "num_images": 1, "seed": 1110, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_class_select_highlight/01_raw.png
echo wrote build/art/ui_class_select_highlight/01_raw.png
```

## ui_class_select_picker

**Replaces:** `ui/class_select_picker.png`
  
**Currently:** 288x184, placeholder (identical to King's Bounty) → **generate at 576x368**

**Prompt**

> four Roman figures posed together in a landscape -- a general, a praetorian, a veiled priestess and a fur-clad frontier commander -- in one illustrated scene, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: four Roman figures posed together in a landscape -- a general, a praetorian, a veiled priestess and a fur-clad frontier commander -- in one illustrated scene*

```sh
mkdir -p build/art/ui_class_select_picker
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "four Roman figures posed together in a landscape -- a general, a praetorian, a veiled priestess and a fur-clad frontier commander -- in one illustrated scene, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 576, "height": 368, "num_images": 1, "seed": 1109, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_class_select_picker/01_raw.png
echo wrote build/art/ui_class_select_picker/01_raw.png
```

## ui_end_lose_screen

**Replaces:** `ui/end_lose_screen.png`
  
**Currently:** 144x170, placeholder (identical to King's Bounty) → **generate at 288x340**

**Prompt**

> a broken Roman eagle standard fallen in mud with a burning frontier fort behind, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a broken Roman eagle standard fallen in mud with a burning frontier fort behind*

```sh
mkdir -p build/art/ui_end_lose_screen
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a broken Roman eagle standard fallen in mud with a burning frontier fort behind, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 288, "height": 340, "num_images": 1, "seed": 1112, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_end_lose_screen/01_raw.png
echo wrote build/art/ui_end_lose_screen/01_raw.png
```

## ui_end_win_screen

**Replaces:** `ui/end_win_screen.png`
  
**Currently:** 144x170, placeholder (identical to King's Bounty) → **generate at 288x340**

**Prompt**

> a Roman general crowned with laurel raising the recovered golden eagle standard, guards flanking, triumphal hall, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a Roman general crowned with laurel raising the recovered golden eagle standard, guards flanking, triumphal hall*

```sh
mkdir -p build/art/ui_end_win_screen
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a Roman general crowned with laurel raising the recovered golden eagle standard, guards flanking, triumphal hall, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 288, "height": 340, "num_images": 1, "seed": 1111, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_end_win_screen/01_raw.png
echo wrote build/art/ui_end_win_screen/01_raw.png
```

## ui_splash_logo

**Replaces:** `ui/splash_logo.png`
  
**Currently:** 320x84, placeholder (identical to King's Bounty) → **generate at 640x168**

**Prompt**

> a publisher logo mark on a plain dark field, centred, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a publisher logo mark on a plain dark field, centred*

```sh
mkdir -p build/art/ui_splash_logo
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a publisher logo mark on a plain dark field, centred, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 640, "height": 168, "num_images": 1, "seed": 1108, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_splash_logo/01_raw.png
echo wrote build/art/ui_splash_logo/01_raw.png
```

## ui_splash_title

**Replaces:** `ui/splash_title.png`
  
**Currently:** 320x200, placeholder (identical to King's Bounty) → **generate at 640x400**

**Prompt**

> a golden Roman legionary eagle standard with spread wings on a decorated pole, centred, deep royal purple background, ornate gilded border, wide empty space across the top third, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text

*Original brief wording: a golden Roman legionary eagle standard with spread wings on a decorated pole, centred, deep royal purple background, ornate gilded border, wide empty space across the top third*

```sh
mkdir -p build/art/ui_splash_title
TASK=$(curl -sS -X POST https://api.retrodiffusion.ai/v1/inferences \
  -H "X-RD-Token: $TOKEN" -H "Content-Type: application/json" \
  -d '{"prompt": "a golden Roman legionary eagle standard with spread wings on a decorated pole, centred, deep royal purple background, ornate gilded border, wide empty space across the top third, the whole subject complete and well inside the picture with clear space on every side, nothing touching or cut off by any edge, bold flat blocks of colour, few colours, hard dark outline, strong contrast, no soft shading, no gradients, no blur, no text", "prompt_style": "rd_plus__low_res", "width": 640, "height": 400, "num_images": 1, "seed": 1107, "async": true}' | jq -r .task_id)
echo "task $TASK"
while :; do
  R=$(curl -sS https://api.retrodiffusion.ai/v1/inferences/tasks/$TASK -H "X-RD-Token: $TOKEN")
  S=$(echo "$R" | jq -r .status); echo "$S"
  [ "$S" = succeeded ] && break
  [ "$S" = failed ] && { echo "$R"; break; }
  sleep 5
done
echo "$R" | jq -r '.base64_images[0]' | base64 -d > build/art/ui_splash_title/01_raw.png
echo wrote build/art/ui_splash_title/01_raw.png
```
