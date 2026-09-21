# Rome art: every prompt and setting

**Generated** by `tools/romeart.py prompts` from `art/jobs/*.json`. Do not
edit by hand: change the job file and run the tool again.

361 jobs in all. **INSTALLED** marks a job whose output is in the pack;
everything else is the record of what was tried, which is why the notes matter —
they say what a run produced and what it superseded. The routes themselves (which
engine, which settings, and why) are in `docs/ART-PIPELINE.md`.

Notes written before 2026-09-20 name the separate compositing tools of the
time (`tile2x2.py`, `roadtile.py`, `cropcentre.py` and the rest). Every one of
them is now a subcommand of `tools/romeart.py`, with the same algorithm: read
`tile2x2.py` as `romeart.py tile2x2`, `roadtile.py --sweep` as `romeart.py
sweep --sweep`, `cropcentre.py` as `romeart.py crop`.

## Animations

### boat_row

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 7832)
- **Pack path:** `art/sprites/boat_00..03.png`
- **prompt:** the oars sweep together from angled forward to angled back, hull, mast and sail still, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/boat/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=7832`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Boat loop, reset 2026-09-07: custom action on the still, four frames, expansion on, one stroke named, hull and sail told to stay still.

### charontes_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6224)
- **prompt:** he takes the rhomphaia in both hands and chops the long blade down and forward in a wide arc to the right, the way he faces, from upright beside his shoulder until the blade is out in front of him at knee height, then lifts it back to upright, the same weapon the whole time, both feet stay planted and the wings stay spread, smooth loop, no motion blur and no streaks
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/charontes/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6224`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Attack loop for the new vampires-slot troop (charontes.json seed 6220), 2026-09-20. Four frames, expansion on, the motion line naming the path, both endpoints, the direction and what stays still; the blur clause is the lesson from the baleares loop. run02: run01 never swung -- he only tilted the staff, and the hook head changed shape and colour from frame to frame. Replaced the wide arc with the straight thrust that worked on the lares and sarmatae loops, and named the hook as the same one throughout. run03: on the rhomphaia still (charontes.json run02, seed 6223), and two-handed -- the two-handed chop is what moved the ligures and cyclopes, who are the same heavy build.

## Hero classes

### hero_dux  — INSTALLED

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 7431)
- **Pack path:** `art/classes/dux_hero.png`
- **prompt:** a barbarian warlord on horseback in profile facing right, a bearskin cloak over mail, a long axe across his shoulder, a shaggy dark horse standing still with all four feet on the ground
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7431`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Per-class mounted hero still (2026-09-06), troop route: the win-cartoon hero tile for this class and the start frame of its map walk. The Legatus is the earlier hero still (seed 7401).

### hero_dux_walk

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 7481)
- **Pack path:** `art/classes/dux_walk_00..03.png`
- **prompt:** horse walking to the right, steady steps, rider still, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=assets/glory-of-rome/art/classes/dux_hero.png`, `raw_only=true`, `return_spritesheet=true`, `seed=7481`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Class hero walk (2026-09-06): custom action on the class's mounted still, four frames, prompt expansion on, tag prompt. Chosen over the named walking action on a Legatus test: the legs step more clearly. Declared in the class's hero.walk; the flat array mirrors when facing west.

### hero_legatus

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 7401)
- **Pack path:** `art/classes/legatus_hero.png (and ending.hero_tile as art/ui/end_hero.png)`
- **prompt:** a Roman commander on horseback in profile facing right, red cloak, crested helmet, gilded cuirass, the horse standing still with all four feet on the ground
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7401`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The hero still (2026-09-06): troop route, the worklist's hero prompt. Installed as the win-cartoon hero tile and kept as the start frame for the hero walk animation.

### hero_legatus_walk

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 7451)
- **Pack path:** `art/classes/legatus_walk_00..03.png`
- **prompt:** horse walking to the right, steady steps, rider still, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=assets/glory-of-rome/art/classes/legatus_hero.png`, `raw_only=true`, `return_spritesheet=true`, `seed=7451`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Class hero walk (2026-09-06): custom action on the class's mounted still, four frames, prompt expansion on, tag prompt. Chosen over the named walking action on a Legatus test: the legs step more clearly. Declared in the class's hero.walk; the flat array mirrors when facing west.

### hero_praetorianus  — INSTALLED

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 7411)
- **Pack path:** `art/classes/praetorianus_hero.png`
- **prompt:** a Roman praetorian officer on horseback in profile facing right, white cloak with a purple border, gilded scale cuirass, plumed helmet, the horse standing still with all four feet on the ground
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7411`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Per-class mounted hero still (2026-09-06), troop route: the win-cartoon hero tile for this class and the start frame of its map walk. The Legatus is the earlier hero still (seed 7401).

### hero_praetorianus_walk

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 7461)
- **Pack path:** `art/classes/praetorianus_walk_00..03.png`
- **prompt:** horse walking to the right, steady steps, rider still, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=assets/glory-of-rome/art/classes/praetorianus_hero.png`, `raw_only=true`, `return_spritesheet=true`, `seed=7461`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Class hero walk (2026-09-06): custom action on the class's mounted still, four frames, prompt expansion on, tag prompt. Chosen over the named walking action on a Legatus test: the legs step more clearly. Declared in the class's hero.walk; the flat array mirrors when facing west.

### hero_sibylla  — INSTALLED

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 7421)
- **Pack path:** `art/classes/sibylla_hero.png`
- **prompt:** a Roman priestess on horseback in profile facing right, white robes and a veil, a laurel branch in her hand, a pale grey horse standing still with all four feet on the ground
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7421`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Per-class mounted hero still (2026-09-06), troop route: the win-cartoon hero tile for this class and the start frame of its map walk. The Legatus is the earlier hero still (seed 7401).

### hero_sibylla_walk

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 7471)
- **Pack path:** `art/classes/sibylla_walk_00..03.png`
- **prompt:** horse walking to the right, steady steps, rider still, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=assets/glory-of-rome/art/classes/sibylla_hero.png`, `raw_only=true`, `return_spritesheet=true`, `seed=7471`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Class hero walk (2026-09-06): custom action on the class's mounted still, four frames, prompt expansion on, tag prompt. Chosen over the named walking action on a Legatus test: the legs step more clearly. Declared in the class's hero.walk; the flat array mirrors when facing west.

## Map tiles and terrain

### africa_t32_cobble

- **Engine:** PixelLab create-tileset (32 px, seed 3134)
- **lower:** dry savanna grass, short and sun-scorched, dusty olive and khaki green with patches of pale straw
- **upper:** pale grey limestone paving slabs, even and flat
- **where they meet:** a single edging course of darker set stones where the paving meets the grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=3309ecf2-1b0c-4379-85c8-1600cb48a6b8`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=3134`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Africa road, fourth run (2026-09-19): words only, a colour far from the khaki grass so the plain tile does not collapse into the grass. Earlier runs kept: cobble_miss (slabs as a tile grid), cobble_miss2 (plain tile came back as the grass), cobble_ref3 (upper_reference_image of Italia's cobble: the plain tile copied the reference). Output kept in art/primitives/africa/cobble.

### africa_t32_desert

- **Engine:** PixelLab create-tileset (32 px, seed 3153)
- **lower:** dry savanna grass, short and sun-scorched, dusty olive and khaki green with patches of pale straw
- **upper:** pale cream-white sand dunes with soft rippled crests, no grass
- **where they meet:** dry sandy earth with sparse tufts of grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=3309ecf2-1b0c-4379-85c8-1600cb48a6b8`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=3153`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Africa desert (2026-09-19): t32_desert_203 chained to the Africa grass (3309ecf2), descriptions as in the review table. Second run: the first ('warm orange-gold Saharan sand with soft rippled dunes', seed 3103) missed; kept in art/primitives/africa/desert_miss. Next run: the previous ('deep orange Saharan sand dunes with rippled crests, no grass', seed 3113: flat saturated orange) is kept in art/primitives/africa/desert_orange. Only the desert wording and seed changed. Output kept in art/primitives/africa/desert.

### africa_t32_grass

- **Engine:** PixelLab create-tileset (32 px, seed 3101)
- **lower:** dry savanna grass, short and sun-scorched, dusty olive and khaki green with patches of pale straw
- **upper:** the same dry grass with a few small clumps of thorny scrub
- **Settings:** `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=3101`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Africa grass (2026-09-19): t32_moss_a (the Italia grass) with the two descriptions and seed changed and no colour swatch. Output kept in art/primitives/africa/grass.

### africa_t32_river

- **Engine:** PixelLab create-tileset (32 px, seed 3165)
- **lower:** dry savanna grass, short and sun-scorched, dusty olive and khaki green with patches of pale straw
- **upper:** calm deep blue river water, a little silty, with a gentle current and soft ripples, seen from above
- **where they meet:** a narrow muddy bank where the river meets the meadow grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=3309ecf2-1b0c-4379-85c8-1600cb48a6b8`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=3165`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Africa river (2026-09-19): t32_river_203 chained to the Africa grass (3309ecf2), descriptions as in the review table. Third run (Dan: neither the mud nor the teal): only the river wording and seed changed; earlier runs kept in river_mud and river_teal. Output kept in art/primitives/africa/river.

### africa_t32_sea

- **Engine:** PixelLab create-tileset (32 px, seed 3142)
- **lower:** dry savanna grass, short and sun-scorched, dusty olive and khaki green with patches of pale straw
- **upper:** calm deep blue Mediterranean sea water with gentle waves
- **where they meet:** shoreline with pale shallow water and white sand
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=3309ecf2-1b0c-4379-85c8-1600cb48a6b8`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=3142`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Africa sea (2026-09-19): t32_water_203 chained to the Africa grass (3309ecf2), descriptions as in the review table. Second run: the first ('clear turquoise Mediterranean sea water with gentle waves', seed 3102) was too turquoise (Dan); kept in art/primitives/africa/sea_turquoise. Only the sea wording and seed changed. Output kept in art/primitives/africa/sea.

### alcove_precinct  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7402)
- **Pack path:** `art/tiles/alcove.png`
- **prompt:** an isolated cut-out game sprite of a small open-air Roman augural precinct on a low rocky outcrop, a square stone platform reached by three steps with a low parapet of pale ashlar blocks around it and a plain square stone altar at its centre, no roof of any kind so the whole platform is open to the sky, a tall bronze-topped post at one corner with a black raven perched on it, seen from the front and above with the steps facing the viewer, a freestanding structure with no ground under it: the lowest row of stones is the bottom of the sprite and only the flat magenta background shows below and around it, the whole structure complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7402`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** The magic alcove's map tile (2026-09-12), replacing the hills-dwelling sprite it borrowed. Map-object route, the same as the towns and dwellings: rd_pro__topdown, 96x96, magenta named and removed, no reference image, a facing cue, freestanding with no ground under it, expansion bypassed. The subject is an AUGURACULUM, the augur's marked precinct -- deliberately roofless, because an augur reads a quartered region of sky and a roof would close the one thing the place is for. Distinct at a glance from dwelling_hills, which is a cave mouth in a mound.

### artifact_chest  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7221)
- **Pack path:** `art/tiles/artifact_chest.png`
- **prompt:** an isolated cut-out game sprite of an ornate gilded reliquary casket with glowing seams, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7221`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the object route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference, freestanding with no ground.

### artifact_ring  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7231)
- **Pack path:** `art/tiles/artifact_ring.png`
- **prompt:** an isolated cut-out game sprite of a golden ring resting on a small stone plinth, radiating light, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7231`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the object route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference, freestanding with no ground.

### bridge_h  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7621)
- **Pack path:** `art/tiles/bridge_h.png`
- **prompt:** a square tile of grey stone paving in even rows of rectangular blocks, filling the whole picture edge to edge, with a raised kerb of lighter stone running along the full top edge and the full bottom edge, the left and right edges open so the paving continues past them, seen from directly above, flat, opaque
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7621`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Bridge tile (2026-09-06): a road, as in the original pack. Opaque square of stone paving with a kerb along the two edges the road does not cross, so tiles stack end to end. Object engine, no background removal, no water.

### bridge_river_h

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7802)
- **Pack path:** `art/tiles/bridge_river_h.png`
- **prompt:** an isolated cut-out game sprite of a small Roman stone bridge seen from directly above, a narrow straight deck of grey paving stones with a low parapet wall of lighter stone along each long side, the deck running straight from the very left edge of the picture to the very right edge, about a third of the picture tall and centred top to bottom, flat magenta background filling the picture above and below the bridge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7802`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** River bridge, crossing east-west (2026-09-16). As bridge_river_v, turned; its own call because the art is not rotated after generation.

### bridge_river_v

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7801)
- **Pack path:** `art/tiles/bridge_river_v.png`
- **prompt:** an isolated cut-out game sprite of a small Roman stone bridge seen from directly above, a narrow straight deck of grey paving stones with a low parapet wall of lighter stone along each long side, the deck running straight from the very top edge of the picture to the very bottom edge, about a third of the picture wide and centred left to right, flat magenta background filling the picture on both sides of the bridge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7801`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** River bridge, crossing north-south (2026-09-16). Object route as chest.json: rd_pro__topdown 96x96, magenta removed by the service. Drawn over a river piece at runtime (the tile's ground), so the river shows either side.

### bridge_v  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7631)
- **Pack path:** `art/tiles/bridge_v.png`
- **prompt:** a square tile of grey stone paving in even rows of rectangular blocks, filling the whole picture edge to edge, with a raised kerb of lighter stone running along the full left edge and the full right edge, the top and bottom edges open so the paving continues past them, seen from directly above, flat, opaque
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7631`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Bridge tile (2026-09-06): a road, as in the original pack. Opaque square of stone paving with a kerb along the two edges the road does not cross, so tiles stack end to end. Object engine, no background removal, no water.

### castle  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7162)
- **Pack path:** `art/tiles/castle.png`
- **prompt:** an isolated cut-out game sprite of a small compact Roman fortress, gleaming white marble walls in ashlar courses, square corner towers with flat red tile roofs, crenellated battlements, a monumental gatehouse framed by columns under a triangular pediment, a golden legionary eagle standard above the gate, seen from the front and above with the gate facing the viewer, a freestanding building with no ground under it: the lowest row of stones is the bottom of the sprite and only the flat magenta background shows below and around it, the whole building complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7162`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the town route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference image, a facing cue, freestanding with no ground. Expansion bypassed as for the towns (the expansion finding is for the animation engine). run02: run01 filled the frame to the top and left with the corner towers cut off; seed incremented, the fortress described smaller in the picture.

### castle_palatium  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7161)
- **Pack path:** `art/tiles/castle_palatium.png`
- **prompt:** an isolated cut-out game sprite of the imperial palace on the Palatine hill of Rome, a grand white marble palace with a long facade of tall Corinthian columns, a great central audience hall under a gilded bronze dome, purple imperial banners hanging between the columns, a golden eagle standard above the entrance, red terracotta roofs on the wings, the palace seen from the front and above with its great doors facing the viewer, a short stretch of paved road in front of the doors that starts and ends at the palace, the road the only ground drawn, no wall, fence or gate, only the flat magenta background around and below the palace, the whole palace complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7161`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** The Emperor's palace map tile for Roma's castle (2026-09-13), drawn beside the town_rome tile. The per-zone town tile route exactly (rd_pro__topdown, 96x96, magenta removed, no reference, a facing cue, freestanding with a short road as the only ground), in the same white marble, terracotta and gilded palette as town_rome so the two read as one city.

### chest  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7211)
- **Pack path:** `art/tiles/chest.png`
- **prompt:** an isolated cut-out game sprite of a Roman strongbox, an iron-banded wooden arca with bronze studs, lid closed, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7211`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the object route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference, freestanding with no ground.

### desert  — INSTALLED

- **Engine:** Retro Diffusion rd_tile__single_tile (48x48, seed 3442)
- **Pack path:** `art/tiles/desert.png`
- **prompt:** dry desert sand seen from directly above, pale tan ground with many short streaks of darker tan and near-white in clear contrast, the same everywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=48`, `raw_only=true`, `seed=3442`, `style=rd_tile__single_tile`, `target=[48, 48]`, `width=48`
- **Note:** Base terrain, route settled 2026-09-05: rd_tile__single_tile (the API's purpose-built seamless tile style, cap 64, its craft guide sizes single tiles at 16-32) at 48x48, laid 2x2 by tools/tile2x2.py into the 96x96 pack tile at native pixel density (repeat period 48). Replaces rd_plus__low_res with tile_x/tile_y and the wrapping-paper prompt: that route held the wrap but the model shaded each tile's interior, which repeated as a lattice across a field (grass rim, forest diamond) and no prompt wording, seed or expansion setting removed it; the tile style does not shade the interior at all. The terrain is described plainly, seen from directly above, 'the same everywhere'. No post-processing. run03: run02 was near-flat cream (contrast 15), streaks asked for in clear contrast.

### dwelling_dungeon  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7201)
- **Pack path:** `art/tiles/dwelling_dungeon.png`
- **prompt:** an isolated cut-out game sprite of a Roman columbarium crypt entrance, a stone doorway flanked by funerary urns with steps going down, seen from the front and above with the gate facing the viewer, a freestanding building with no ground under it: the lowest row of stones is the bottom of the sprite and only the flat magenta background shows below and around it, the whole building complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7201`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the town route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference image, a facing cue, freestanding with no ground. Expansion bypassed as for the towns (the expansion finding is for the animation engine).

### dwelling_forest  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7181)
- **Pack path:** `art/tiles/dwelling_forest.png`
- **prompt:** an isolated cut-out game sprite of a small stone shrine and altar in a sacred grove, a few dark trees close around it, seen from the front and above with the gate facing the viewer, a freestanding building with no ground under it: the lowest row of stones is the bottom of the sprite and only the flat magenta background shows below and around it, the whole building complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7181`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the town route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference image, a facing cue, freestanding with no ground. Expansion bypassed as for the towns (the expansion finding is for the animation engine).

### dwelling_hills  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7192)
- **Pack path:** `art/tiles/dwelling_hills.png`
- **prompt:** an isolated cut-out game sprite of a cave mouth in a small rocky mound with a carved stone lintel over the opening, seen from the front and above with the gate facing the viewer, a freestanding building with no ground under it: the lowest row of stones is the bottom of the sprite and only the flat magenta background shows below and around it, the whole building complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7192`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the town route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference image, a facing cue, freestanding with no ground. Expansion bypassed as for the towns (the expansion finding is for the animation engine). run02: run01 ran off both sides; seed incremented, the hillside described as a small mound.

### dwelling_plains  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7172)
- **Pack path:** `art/tiles/dwelling_plains.png`
- **prompt:** an isolated cut-out game sprite of a Roman villa rustica farmstead, a low whitewashed farmhouse with a red tiled roof, a haystack and a wooden cart in the open yard beside it, seen from the front and above with the gate facing the viewer, a freestanding building with no ground under it: the lowest row of stones is the bottom of the sprite and only the flat magenta background shows below and around it, the whole building complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7172`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the town route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference image, a facing cue, freestanding with no ground. Expansion bypassed as for the towns (the expansion finding is for the animation engine). run02: the API's remove_bg left magenta around run01's walled paddock; the paddock is now an open yard, seed incremented.

### forest  — INSTALLED

- **Engine:** Retro Diffusion rd_tile__single_tile (48x48, seed 3341)
- **Pack path:** `art/tiles/forest.png`
- **prompt:** dense forest canopy seen from directly above, many tiny round tree tops of mid green over very dark green shadow, the same everywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=48`, `raw_only=true`, `seed=3341`, `style=rd_tile__single_tile`, `target=[48, 48]`, `width=48`
- **Note:** Base terrain, route settled 2026-09-05: rd_tile__single_tile (the API's purpose-built seamless tile style, cap 64, its craft guide sizes single tiles at 16-32) at 48x48, laid 2x2 by tools/tile2x2.py into the 96x96 pack tile at native pixel density (repeat period 48). Replaces rd_plus__low_res with tile_x/tile_y and the wrapping-paper prompt: that route held the wrap but the model shaded each tile's interior, which repeated as a lattice across a field (grass rim, forest diamond) and no prompt wording, seed or expansion setting removed it; the tile style does not shade the interior at all. The terrain is described plainly, seen from directly above, 'the same everywhere'. No post-processing.

### galliae_t32_cobble

- **Engine:** PixelLab create-tileset (32 px, seed 2123)
- **lower:** open prairie grass, long and windswept, soft sage and olive green with pale straw-coloured tips
- **upper:** cobblestone road paved with small close-set rounded grey stones, dark joints between them, even and flat
- **where they meet:** a single edging course of darker set stones where the paving meets the grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=83f91c7d-6ce0-46cf-8c4f-937131c464ca`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=2123`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Galliae cobble (2026-09-19): t32_cobble_203 with the grass chained to the Galliae grass (011fafa4) and the descriptions in the review table. Re-run on the approved prairie grass (83f91c7d); the run on the teal grass is kept in art/primitives/galliae/cobble_teal. Output kept in art/primitives/galliae/cobble.

### galliae_t32_grass

- **Engine:** PixelLab create-tileset (32 px, seed 2121)
- **lower:** open prairie grass, long and windswept, soft sage and olive green with pale straw-coloured tips
- **upper:** the same prairie grass with a few small tufts of wildflowers
- **Settings:** `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=2121`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Galliae grass, third run (2026-09-19). Earlier: 'lush cool blue-green meadow grass, damp and dense' seed 2101 (teal; art/primitives/galliae/grass_teal) and 'lush deep green meadow grass, damp and dense' seed 2111 (too close to Italia; grass_deep). t32_moss_a otherwise, no colour swatch.

### galliae_t32_river

- **Engine:** PixelLab create-tileset (32 px, seed 2124)
- **lower:** open prairie grass, long and windswept, soft sage and olive green with pale straw-coloured tips
- **upper:** clear cool river water, grey-blue, with a gentle current and soft ripples, seen from above
- **where they meet:** a narrow muddy bank where the river meets the meadow grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=83f91c7d-6ce0-46cf-8c4f-937131c464ca`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=2124`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Galliae river (2026-09-19): t32_river_203 with the grass chained to the Galliae grass (011fafa4) and the descriptions in the review table. Re-run on the approved prairie grass (83f91c7d); the run on the teal grass is kept in art/primitives/galliae/river_teal. Output kept in art/primitives/galliae/river.

### galliae_t32_sea

- **Engine:** PixelLab create-tileset (32 px, seed 2122)
- **lower:** open prairie grass, long and windswept, soft sage and olive green with pale straw-coloured tips
- **upper:** cold dark grey-blue northern sea with gentle waves
- **where they meet:** shoreline with pale shallow water and wet shingle
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=83f91c7d-6ce0-46cf-8c4f-937131c464ca`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=2122`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Galliae sea (2026-09-19): t32_water_203 with the grass chained to the Galliae grass (011fafa4) and the descriptions in the review table. Re-run on the approved prairie grass (83f91c7d); the run on the teal grass is kept in art/primitives/galliae/sea_teal. Output kept in art/primitives/galliae/sea.

### grass  — INSTALLED

- **Engine:** Retro Diffusion rd_tile__single_tile (48x48, seed 3411)
- **Pack path:** `art/tiles/grass.png`
- **prompt:** muted moss green grass seen from directly above, dull and slightly grey with only a few short specks of paler olive and dark bottle green, low contrast, the same everywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=48`, `raw_only=true`, `seed=3411`, `style=rd_tile__single_tile`, `target=[48, 48]`, `width=48`
- **Note:** Base terrain, route settled 2026-09-05: rd_tile__single_tile (the API's purpose-built seamless tile style, cap 64, its craft guide sizes single tiles at 16-32) at 48x48, laid 2x2 by tools/tile2x2.py into the 96x96 pack tile at native pixel density (repeat period 48). Replaces rd_plus__low_res with tile_x/tile_y and the wrapping-paper prompt: that route held the wrap but the model shaded each tile's interior, which repeated as a lattice across a field (grass rim, forest diamond) and no prompt wording, seed or expansion setting removed it; the tile style does not shade the interior at all. The terrain is described plainly, seen from directly above, 'the same everywhere'. No post-processing.

### grass16_base_1

- **Engine:** Retro Diffusion rd_tile__single_tile (16x16, seed 7101)
- **prompt:** dark mossy meadow grass seen from directly above, dense short blades, even, the same everywhere, no objects
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=16`, `raw_only=true`, `seed=7101`, `style=rd_tile__single_tile`, `target=[16, 16]`, `width=16`
- **Note:** 2026-09-11: 16 px base grass on rd_tile__single_tile. The style rejects tile_x/tile_y (inference fails); it is seamless by itself.

### grass16_base_2

- **Engine:** Retro Diffusion rd_tile__single_tile (16x16, seed 7102)
- **prompt:** dark mossy meadow grass seen from directly above, dense short blades, even, the same everywhere, no objects
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=16`, `raw_only=true`, `seed=7102`, `style=rd_tile__single_tile`, `target=[16, 16]`, `width=16`
- **Note:** 2026-09-11: 16 px base grass on rd_tile__single_tile. The style rejects tile_x/tile_y (inference fails); it is seamless by itself.

### grass16_base_3

- **Engine:** Retro Diffusion rd_tile__single_tile (16x16, seed 7103)
- **prompt:** dark mossy meadow grass seen from directly above, dense short blades, even, the same everywhere, no objects
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=16`, `raw_only=true`, `seed=7103`, `style=rd_tile__single_tile`, `target=[16, 16]`, `width=16`
- **Note:** 2026-09-11: 16 px base grass on rd_tile__single_tile. The style rejects tile_x/tile_y (inference fails); it is seamless by itself.

### grass16_base_4

- **Engine:** Retro Diffusion rd_tile__single_tile (16x16, seed 7104)
- **prompt:** dark mossy meadow grass seen from directly above, dense short blades, even, the same everywhere, no objects
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=16`, `raw_only=true`, `seed=7104`, `style=rd_tile__single_tile`, `target=[16, 16]`, `width=16`
- **Note:** 2026-09-11: 16 px base grass on rd_tile__single_tile. The style rejects tile_x/tile_y (inference fails); it is seamless by itself.

### grass16_tileset

- **Engine:** Retro Diffusion rd_tile__tileset (16x16, seed 7201)
- **prompt:** dark mossy meadow grass, and the same grass with scattered weeds, small grey pebbles and tiny wildflowers
- **Settings:** `figure=false`, `height=16`, `raw_only=true`, `seed=7201`, `style=rd_tile__tileset`, `target=[64, 80]`, `width=16`
- **Note:** 2026-09-11: rd_tile__tileset at 16 px, one wang-style sheet (4x5 tiles per the documented example), $0.10. Cells classified by corner afterwards.

### grass16_variations

- **Engine:** PixelLab tiles (16 px, seed 8101)
- **description:** mossy meadow grass, mostly flat and even, low contrast, a few short blade clusters and slightly darker patches, seen from directly above
- **Settings:** `outline_mode=segmentation`, `seed=8101`, `style_images=[{"base64": "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAACi0lEQVR4nD2S23LiRhgGew6SRkiAOHjx7mInVUnlIfL+L5ICDMTmFA9ohOaQC1x799dXf/VNt/jr7z/Tz+WAt7eWHz9KjifHoNR4n7i1HoA8E9iz53lZsVtfefljyPqfK7N5iZ7Nc3a7Finh3ifaNtC2gdl0gIie5WtN8IGQ7gAUpeJ06JgvCnYbizyfOprGoLXicnSkmGgmBcejI8RI1wW2by0pwH7XEhKEBJ+XHq0VspkU7HeWus6494nhuKAe5MQYaCYFHx+O7z9rOud5WpTECClEtJb07o601jNflByPjuVLjRKwWv2HMRqAq+1xzqNU4uPdEWNgMiuw9k4zN8i6fjwCKCWo64z53PzaykqTFZKui0ynBiEFWkuM0VjrkWWVcT7cqYaaW+sJKXE89r8ATWMgCpQWWNuRZZL2GhmNC4JPyO3mhqk0VanZbix9FxiNNM56nIvcrh3rlUVKhVQwKDVaw7/7K1mukTGCEpHTwfHb70Oc85SVRuYKAO+hqiWmFEwnhvbqEQqM0aSYkIvvhuG4oA/gfaLvEudzz3Sa4X0EwLmE0gqtIMSHgTyH4Ug+LHgfyXPB6eDoA1RlhrWepil4ehog1QO+299ZvozYbm7EKIkRZJEJPi+BmBJJwutrzefF0TQFzj1SXnyr6NrA83PJdmOZzw3eQ98lZNcn6lpiCkH08csEnA6Ous5IKdLdAykmzudHnQD265bng6OsMqph8WjhK4vpN0NIifXaIgUURtM0Bqklx5OjbnKUlMhxU7BZ3QB4eh7weenICoXWkve9Q0iBtY8uthuLiCCFQCjJcGyQi0VJpqDvAu+7B6jvAuuVhRSpBgqI1HVGSDCZGZqmoGsD3nv+B8MyY6+J+iOkAAAAAElFTkSuQmCC", "width": 16, "height": 16}]`, `style_options={"color_palette": true, "outline": false, "detail": false, "shading": false}`, `tile_size=16`, `tile_type=square_topdown`, `tile_view=top-down`

### grass32_variations

- **Engine:** PixelLab tiles (32 px, seed 8102)
- **description:** dark mossy meadow grass, mostly flat and even, low contrast, a few short blade clusters and slightly darker patches, seen from directly above
- **Settings:** `outline_mode=segmentation`, `seed=8102`, `tile_size=32`, `tile_type=square_topdown`, `tile_view=top-down`

### grass_variant  — INSTALLED

- **Engine:** Retro Diffusion rd_tile__single_tile (48x48, seed 3421)
- **Pack path:** `art/tiles/grass_variant.png`
- **prompt:** muted moss green grass seen from directly above, dull and slightly grey, with a few darker scrub tufts scattered through it, low contrast, the same everywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=48`, `raw_only=true`, `seed=3421`, `style=rd_tile__single_tile`, `target=[48, 48]`, `width=48`
- **Note:** Base terrain, route settled 2026-09-05: rd_tile__single_tile (the API's purpose-built seamless tile style, cap 64, its craft guide sizes single tiles at 16-32) at 48x48, laid 2x2 by tools/tile2x2.py into the 96x96 pack tile at native pixel density (repeat period 48). Replaces rd_plus__low_res with tile_x/tile_y and the wrapping-paper prompt: that route held the wrap but the model shaded each tile's interior, which repeated as a lattice across a field (grass rim, forest diamond) and no prompt wording, seed or expansion setting removed it; the tile style does not shade the interior at all. The terrain is described plainly, seen from directly above, 'the same everywhere'. No post-processing.

### mountain  — INSTALLED

- **Engine:** Retro Diffusion rd_tile__single_tile (48x48, seed 3431)
- **Pack path:** `art/tiles/mountain.png`
- **prompt:** bare rocky mountain ground seen from directly above, small angular grey-brown rocks with dark crevices between them, the same everywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=48`, `raw_only=true`, `seed=3431`, `style=rd_tile__single_tile`, `target=[48, 48]`, `width=48`
- **Note:** Base terrain, route settled 2026-09-05: rd_tile__single_tile (the API's purpose-built seamless tile style, cap 64, its craft guide sizes single tiles at 16-32) at 48x48, laid 2x2 by tools/tile2x2.py into the 96x96 pack tile at native pixel density (repeat period 48). Replaces rd_plus__low_res with tile_x/tile_y and the wrapping-paper prompt: that route held the wrap but the model shaded each tile's interior, which repeated as a lattice across a field (grass rim, forest diamond) and no prompt wording, seed or expansion setting removed it; the tile style does not shade the interior at all. The terrain is described plainly, seen from directly above, 'the same everywhere'. No post-processing.

### oriens_t32_cobble

- **Engine:** PixelLab create-tileset (32 px, seed 4114)
- **lower:** tall grass like a rice paddy, long upright blades standing in dense even rows, fresh yellow-green, no water
- **upper:** road paved with dark grey basalt blocks, even and flat
- **where they meet:** a single edging course of darker set stones where the paving meets the grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=d584193b-ab45-4afb-8667-377f4df2da68`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=4114`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Oriens cobble (2026-09-19): t32_cobble_203 chained to the Oriens grass (d584193b); the other surface worded in a colour far from the grass so it does not collapse into it. Output kept in art/primitives/oriens/cobble.

### oriens_t32_desert

- **Engine:** PixelLab create-tileset (32 px, seed 4123)
- **lower:** tall grass like a rice paddy, long upright blades standing in dense even rows, fresh yellow-green, no water
- **upper:** warm tan sand dunes with rippled crests and soft shadows, no grass
- **where they meet:** dry sandy earth with sparse tufts of grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=d584193b-ab45-4afb-8667-377f4df2da68`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=4123`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Oriens desert (2026-09-19): t32_desert_203 chained to the Oriens grass (d584193b); the other surface worded in a colour far from the grass so it does not collapse into it. Re-roll: previous ('pale pinkish-ochre Arabian sand with soft rippled dunes, no grass') kept in art/primitives/oriens/desert_pink. Output kept in art/primitives/oriens/desert.

### oriens_t32_grass

- **Engine:** PixelLab create-tileset (32 px, seed 4111)
- **lower:** tall grass like a rice paddy, long upright blades standing in dense even rows, fresh yellow-green, no water
- **upper:** the same tall grass with a few small darker clumps
- **Settings:** `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=4111`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Oriens grass, second run (2026-09-19, Dan: tall grass like a rice paddy). The first ('Anatolian steppe grass, short and dry, silvery grey-green with small patches of ochre earth', seed 4101: close to Galliae, a busy 32 px repeat) is kept in art/primitives/oriens/grass_steppe. Output kept in art/primitives/oriens/grass.

### oriens_t32_river

- **Engine:** PixelLab create-tileset (32 px, seed 4125)
- **lower:** tall grass like a rice paddy, long upright blades standing in dense even rows, fresh yellow-green, no water
- **upper:** clear bright blue river water with pale ripples and a gentle current, seen from above
- **where they meet:** a narrow muddy bank where the river meets the meadow grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=d584193b-ab45-4afb-8667-377f4df2da68`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=4125`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Oriens river (2026-09-19): t32_river_203 chained to the Oriens grass (d584193b); the other surface worded in a colour far from the grass so it does not collapse into it. Re-roll: previous ('calm deep blue river water, a little silty, with a gentle current and soft ripples, seen from above') kept in art/primitives/oriens/river_dark. Output kept in art/primitives/oriens/river.

### oriens_t32_sea

- **Engine:** PixelLab create-tileset (32 px, seed 4112)
- **lower:** tall grass like a rice paddy, long upright blades standing in dense even rows, fresh yellow-green, no water
- **upper:** deep blue Aegean sea water with gentle waves
- **where they meet:** shoreline with pale shallow water and wet sand
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=d584193b-ab45-4afb-8667-377f4df2da68`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=4112`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Oriens sea (2026-09-19): t32_water_203 chained to the Oriens grass (d584193b); the other surface worded in a colour far from the grass so it does not collapse into it. Output kept in art/primitives/oriens/sea.

### pharos  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7401)
- **Pack path:** `art/tiles/pharos.png`
- **prompt:** an isolated cut-out game sprite of the Pharos lighthouse of Alexandria, a tall three-stage tower of pale stone, a square base, an eight-sided middle stage and a round top with a fire burning in it and a thin plume of smoke, a small walled courtyard at its foot, seen from the front and above, the tower complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, no wall, fence or gate, only the flat magenta background around it, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7401`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** The Pharos map tile (REQ-221b vista trigger on Africa's shore beside Alexandria). Object route, the same as every town/castle tile: rd_pro__topdown 96x96 with the magenta background removed, no reference image.

### sign  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7244)
- **Pack path:** `art/tiles/sign.png`
- **prompt:** an isolated cut-out game sprite of a wooden signpost, a tall post with a plain pointed board nailed to it, the board carved with the four capital letters SPQR in a clear Roman inscription, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7244`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the object route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference, freestanding with no ground. run02: run01 was a milestone drum that did not read as a sign, seed incremented. run03: no letters on the board (Dan), seed incremented. run04: SPQR on the board (Dan), seed incremented.

### t16_dark_base

- **Engine:** PixelLab create-tileset (16 px, seed 121)
- **lower:** dark mossy meadow grass
- **upper:** the same grass with a few small patches of darker moss
- **Settings:** `color_image=<image>`, `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=121`, `shading=detailed shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_dark_flowers

- **Engine:** PixelLab create-tileset (16 px, seed 132)
- **lower:** dark mossy meadow grass
- **upper:** the same grass with a few small white and yellow wildflowers
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=490576ec-9573-449f-b580-673e4e1c2505`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=132`, `shading=detailed shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_dark_litter

- **Engine:** PixelLab create-tileset (16 px, seed 134)
- **lower:** dark mossy meadow grass
- **upper:** the same grass with a few fallen leaves, twigs and a small bare dirt spot
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=490576ec-9573-449f-b580-673e4e1c2505`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=134`, `shading=detailed shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_dark_moss

- **Engine:** PixelLab create-tileset (16 px, seed 131)
- **lower:** dark mossy meadow grass
- **upper:** the same grass with a few small patches of slightly darker moss
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=490576ec-9573-449f-b580-673e4e1c2505`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=131`, `shading=detailed shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_dark_pebbles

- **Engine:** PixelLab create-tileset (16 px, seed 133)
- **lower:** dark mossy meadow grass
- **upper:** the same grass with a few small grey pebbles
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=490576ec-9573-449f-b580-673e4e1c2505`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=133`, `shading=detailed shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_dirt

- **Engine:** PixelLab create-tileset (16 px, seed 41)
- **lower:** short green meadow grass, even medium green, seen from above
- **upper:** packed dirt road, dry dusty brown earth, even, seen from above
- **where they meet:** worn ragged edge where bare dirt meets the grass, a few small stones
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=8de72f5c-9d4d-432a-9058-39d365aede04`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=41`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Tried and NOT shipped. 16 px dirt over meadow grass; the roads that shipped came from t32_dirt_203.json instead. Re-run 2026-09-12 on the assumption it was the shipped set and it did not reproduce it: the dirt came back mean (169,119,92) over 6 colours against the installed (131,89,64) over 19, with no colour in common, so the PixelLab seed is not reproducible across calls. One wasted call. Do not reach for this job to rebuild roads.

### t16_drygrass

- **Engine:** PixelLab create-tileset (16 px, seed 51)
- **lower:** short green meadow grass, even medium green, seen from above
- **upper:** dry pale yellow-green meadow grass, sparse and sun-bleached, seen from above
- **where they meet:** soft uneven blend where the lush green grass thins into the dry pale grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=8de72f5c-9d4d-432a-9058-39d365aede04`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=51`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_grass_darkmoss

- **Engine:** PixelLab create-tileset (16 px, seed 9102)
- **lower:** grass
- **upper:** dark moss
- **Settings:** `color_image=<image>`, `detail=low detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=9102`, `shading=flat shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_grass_moss

- **Engine:** PixelLab create-tileset (16 px, seed 9101)
- **lower:** grass
- **upper:** moss
- **Settings:** `color_image=<image>`, `detail=low detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=9101`, `shading=flat shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_moss_flowers

- **Engine:** PixelLab create-tileset (16 px, seed 112)
- **lower:** mossy meadow grass
- **upper:** small white and yellow wildflowers and tall grass tufts in the grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=7be429ba-e876-432a-abb1-5c8bba59a877`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=112`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_moss_pebbles

- **Engine:** PixelLab create-tileset (16 px, seed 111)
- **lower:** mossy meadow grass
- **upper:** small grey and brown pebbles scattered in the grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=7be429ba-e876-432a-abb1-5c8bba59a877`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=111`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_moss_weeds_a

- **Engine:** PixelLab create-tileset (16 px, seed 101)
- **lower:** mossy meadow grass
- **upper:** grass with scattered weeds and clover
- **Settings:** `color_image=<image>`, `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=101`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_moss_weeds_b

- **Engine:** PixelLab create-tileset (16 px, seed 102)
- **lower:** mossy meadow grass
- **upper:** grass with scattered weeds and clover
- **Settings:** `color_image=<image>`, `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=102`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t16_ocean_a

- **Engine:** PixelLab create-tileset (16 px, seed 11)
- **lower:** short green meadow grass, even medium green, seen from above
- **upper:** deep dark navy blue ocean water, calm, seen from above, tiny pale ripples
- **where they meet:** gentle sandy beach: pale turquoise shallow water, then a band of light wet sand, then dry pale sand meeting the grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=8de72f5c-9d4d-432a-9058-39d365aede04`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=11`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.5`, `view=high top-down`

### t16_ocean_b

- **Engine:** PixelLab create-tileset (16 px, seed 12)
- **lower:** short green meadow grass, even medium green, seen from above
- **upper:** deep dark navy blue ocean water, calm, seen from above, tiny pale ripples
- **where they meet:** gentle sandy beach: pale turquoise shallow water, then a band of light wet sand, then dry pale sand meeting the grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=8de72f5c-9d4d-432a-9058-39d365aede04`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=12`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.5`, `view=high top-down`

### t16_plains_dirt

- **Engine:** PixelLab create-tileset (16 px, seed 9104)
- **lower:** plains
- **upper:** dirt
- **Settings:** `enhance=true`, `mode=standard`, `seed=9104`, `shape_style=round`, `tile_size={"width": 16, "height": 16}`, `view=high top-down`
- **Note:** Tried and NOT shipped. 16 px, terse two-word descriptions with enhance on, which leaves the look to the model. t32_dirt_203.json is what shipped.

### t16_plains_grass

- **Engine:** PixelLab create-tileset (16 px, seed 9105)
- **lower:** plains
- **upper:** grass
- **Settings:** `enhance=true`, `mode=standard`, `seed=9105`, `shape_style=round`, `tile_size={"width": 16, "height": 16}`, `view=high top-down`

### t16_plains_grass_hi

- **Engine:** PixelLab create-tileset (16 px, seed 9107)
- **lower:** plains
- **upper:** grass
- **Settings:** `detail=highly detailed`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=9107`, `shading=highly detailed shading`, `shape_style=round`, `tile_size={"width": 16, "height": 16}`, `view=high top-down`

### t16_plains_grass_med

- **Engine:** PixelLab create-tileset (16 px, seed 9106)
- **lower:** plains
- **upper:** grass
- **Settings:** `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=9106`, `shading=medium shading`, `shape_style=round`, `tile_size={"width": 16, "height": 16}`, `view=high top-down`

### t16_plains_grass_pal

- **Engine:** PixelLab create-tileset (16 px, seed 9108)
- **lower:** plains
- **upper:** grass
- **Settings:** `color_image=<image>`, `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=9108`, `shading=medium shading`, `shape_style=round`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.25`, `view=high top-down`

### t16_water_pro

- **Engine:** PixelLab create-tileset (16 px, seed 21)
- **lower:** short green meadow grass, even medium green, seen from above
- **upper:** deep blue sea water with gentle waves
- **where they meet:** shoreline with pale shallow water and wet sand
- **Settings:** `detail=medium detail`, `lower_base_tile_id=8de72f5c-9d4d-432a-9058-39d365aede04`, `lower_reference_image=<image>`, `mode=pro`, `outline=lineless`, `raggedness=0.4`, `seed=21`, `shading=medium shading`, `slope_size=0.0`, `spread_x=0.5`, `text_guidance_scale=8.0`, `tile_size={"width": 16, "height": 16}`, `transition_size=0.0`, `view=high top-down`

### t32_cobble_203

- **Engine:** PixelLab create-tileset (32 px, seed 1209)
- **lower:** mossy meadow grass
- **upper:** cobblestone road paved with small close-set rounded grey stones, dark joints between them, even and flat
- **where they meet:** a single edging course of darker set stones where the paving meets the grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=d1de924b-3a56-495d-bc54-e2253bde8d24`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=1209`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** Cobblestone roads (2026-09-12), superseding the dirt of t32_dirt_203.json. Everything but the two descriptions and the seed is that job unchanged -- same 32 px create-tileset, same lower_base_tile_id d1de924b, so it chains to the grass the dirt roads chained to. Swept into the 24 pieces by tools/roadtile.py --sweep. Note the sweep discards the transition tiles, so the edging course described here never reaches the game: the thin darker border on each road edge is painted by the sweep's own --rim / --rim-shade. Dan authorised up to three calls.

### t32_dark_desert

- **Engine:** PixelLab create-tileset (32 px, seed 142)
- **lower:** dark mossy meadow grass
- **upper:** pale golden desert sand with soft low dunes
- **where they meet:** dry sandy earth with sparse tufts of grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=490576ec-9573-449f-b580-673e4e1c2505`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=142`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_dark_water

- **Engine:** PixelLab create-tileset (32 px, seed 141)
- **lower:** dark mossy meadow grass
- **upper:** deep blue sea water with gentle waves
- **where they meet:** shoreline with pale shallow water and wet sand
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=490576ec-9573-449f-b580-673e4e1c2505`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=141`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_desert_203

- **Engine:** PixelLab create-tileset (32 px, seed 162)
- **lower:** mossy meadow grass
- **upper:** pale golden desert sand with soft low dunes
- **where they meet:** dry sandy earth with sparse tufts of grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=d1de924b-3a56-495d-bc54-e2253bde8d24`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=162`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_desert_a

- **Engine:** PixelLab create-tileset (32 px, seed 81)
- **lower:** lush green meadow grass
- **upper:** pale golden desert sand with soft low dunes
- **where they meet:** dry sandy earth with sparse tufts of grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=87e76087-8d8e-4179-90bd-0b6cb06cbb15`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=81`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_desert_b

- **Engine:** PixelLab create-tileset (32 px, seed 152)
- **lower:** mossy meadow grass
- **upper:** pale golden desert sand with soft low dunes
- **where they meet:** dry sandy earth with sparse tufts of grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=7be429ba-e876-432a-abb1-5c8bba59a877`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=152`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_dirt_203

- **Engine:** PixelLab create-tileset (32 px, seed 171)
- **lower:** mossy meadow grass
- **upper:** packed dirt road, dry brown earth
- **where they meet:** worn ragged edge where bare dirt meets the grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=d1de924b-3a56-495d-bc54-e2253bde8d24`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=171`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** The dirt roads that shipped 2026-09-11 (commit 21e1a9d), superseding the first generation from e944680. 32 px PixelLab create-tileset chained to the mossy grass base d1de924b, then swept into the twenty 96 px pieces by tools/roadtile.py --sweep (defaults: half width 16, rag 3.0, rim 0, seed 1); the sweep takes only the plain upper tile and the pack's own grass.png, so the set's transition tiles never reach the game. Installed dirt reads mean (131,89,64) over 19 colours. Superseded 2026-09-12 by t32_cobble_203.json.

### t32_drygrass_a

- **Engine:** PixelLab create-tileset (32 px, seed 82)
- **lower:** lush green meadow grass
- **upper:** dry pale yellow-green meadow grass, sparse and sun-bleached
- **where they meet:** soft uneven blend where the lush grass thins into the dry grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=87e76087-8d8e-4179-90bd-0b6cb06cbb15`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=82`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_grass_a

- **Engine:** PixelLab create-tileset (32 px, seed 71)
- **lower:** lush green meadow grass
- **upper:** deep blue sea water
- **where they meet:** shoreline
- **Settings:** `color_image=<image>`, `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=71`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_grass_b

- **Engine:** PixelLab create-tileset (32 px, seed 72)
- **lower:** lush green meadow grass, dense short blades, seen from above
- **upper:** deep blue sea water
- **where they meet:** shoreline
- **Settings:** `color_image=<image>`, `detail=medium detail`, `enhance=false`, `mode=standard`, `outline=lineless`, `seed=72`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_moss_a

- **Engine:** PixelLab create-tileset (32 px, seed 203)
- **lower:** mossy meadow grass
- **upper:** grass with a few short tufts
- **Settings:** `color_image=<image>`, `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=203`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_moss_b

- **Engine:** PixelLab create-tileset (32 px, seed 204)
- **lower:** mossy meadow grass
- **upper:** grass with a few short tufts
- **Settings:** `color_image=<image>`, `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=204`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_moss_weeds

- **Engine:** PixelLab create-tileset (32 px, seed 202)
- **lower:** mossy meadow grass
- **upper:** grass with scattered weeds and clover
- **Settings:** `color_image=<image>`, `detail=medium detail`, `enhance=true`, `mode=standard`, `outline=lineless`, `seed=202`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_river_203

- **Engine:** PixelLab create-tileset (32 px, seed 1307)
- **lower:** mossy meadow grass
- **upper:** clear shallow river water, blue, with a gentle current and soft ripples, seen from above
- **where they meet:** a narrow muddy bank where the river meets the meadow grass
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=d1de924b-3a56-495d-bc54-e2253bde8d24`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=1307`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`
- **Note:** River water (2026-09-16) for inland rivers. Everything but the two descriptions and the seed is t32_cobble_203.json unchanged -- same 32 px create-tileset, same lower_base_tile_id d1de924b, so it chains to the grass the roads chained to. Swept into the 24 river_* pieces by tools/roadtile.py --sweep --prefix river; the sweep discards the transition tiles, so the bank described here never reaches the game. Dan chose a new set over the sea tile; one call authorised, more only on request.

### t32_river_mouth

- **Engine:** PixelLab create-tileset (32 px, seed 1319)
- **lower:** clear shallow river water, blue, with a gentle current and soft ripples, seen from above
- **upper:** deep blue sea water with small short horizontal dashes of pale cyan, seen from above
- **where they meet:** open water only: the pale blue river water shading gradually into the deep blue sea, no shore, no sand, no mud, no bank, no border line between them
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=0e608842-1ca9-4d0c-ae6a-70dc08eace09`, `mode=standard`, `outline=lineless`, `seed=1319`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.25`, `upper_reference_image=<image>`, `view=high top-down`
- **Note:** River mouth, second try (2026-09-16). The first (seed 1311, transition 'river water mixing into the sea, the blue deepening gradually') drew a reddish-brown shoreline between the two waters, unusable. Only the transition description and seed changed. Last generation attempt for the mouth.

### t32_water

- **Engine:** PixelLab create-tileset (32 px, seed 31)
- **lower:** short green meadow grass, even medium green, seen from above
- **upper:** deep blue sea water with gentle waves
- **where they meet:** shoreline with pale shallow water and wet sand
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=8de72f5c-9d4d-432a-9058-39d365aede04`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=31`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_water_203

- **Engine:** PixelLab create-tileset (32 px, seed 161)
- **lower:** mossy meadow grass
- **upper:** deep blue sea water with gentle waves
- **where they meet:** shoreline with pale shallow water and wet sand
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=d1de924b-3a56-495d-bc54-e2253bde8d24`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=161`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_water_b

- **Engine:** PixelLab create-tileset (32 px, seed 151)
- **lower:** mossy meadow grass
- **upper:** deep blue sea water with gentle waves
- **where they meet:** shoreline with pale shallow water and wet sand
- **Settings:** `detail=medium detail`, `enhance=false`, `lower_base_tile_id=7be429ba-e876-432a-abb1-5c8bba59a877`, `lower_reference_image=<image>`, `mode=standard`, `outline=lineless`, `seed=151`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_water_moss61

- **Engine:** PixelLab create-tileset (32 px, seed 61)
- **lower:** dense mossy meadow grass, deep rich green, thick and soft, seen from directly above, even, the same everywhere
- **upper:** deep blue sea water with gentle waves
- **where they meet:** shoreline with pale shallow water and wet sand
- **Settings:** `detail=medium detail`, `enhance=false`, `mode=standard`, `outline=lineless`, `seed=61`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### t32_water_moss62

- **Engine:** PixelLab create-tileset (32 px, seed 62)
- **lower:** dense mossy meadow grass, deep rich green, thick and soft, seen from directly above, even, the same everywhere
- **upper:** deep blue sea water with gentle waves
- **where they meet:** shoreline with pale shallow water and wet sand
- **Settings:** `detail=medium detail`, `enhance=false`, `mode=standard`, `outline=lineless`, `seed=62`, `shading=medium shading`, `shape_style=round`, `text_guidance_scale=8.0`, `tile_size={"width": 32, "height": 32}`, `transition_size=0.0`, `view=high top-down`

### temple_ocean  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7501)
- **Pack path:** `art/tiles/temple_ocean.png`
- **prompt:** an isolated cut-out game sprite of a small stone shrine to the sea god, a low rectangular temple of grey weathered stone with four columns across its front and a shallow slab roof, a stone altar before its steps, two standing stones beside it, seen from the front and above with its doorway facing the viewer, no ground, no grass, no rocks and no path under or around it, the flat magenta background coming right up to the temple steps on every side, the whole shrine complete and well inside the picture, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7501`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** The Temple of Ocean map tile (Galliae vista). Object route as every town/castle tile: rd_pro__topdown 96x96, magenta removed; its tile code names grass as its ground. Installed: run 2 (seed 7501), the shrine with its own grass patch, chosen by Dan over the ground-free run 3 (seed 7511), which read flat.

### town_africa  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7133)
- **Pack path:** `art/tiles/town_africa.png`
- **prompt:** an isolated cut-out game sprite of a North African town, lime-white whitewashed houses with flat roofs and sky-blue painted doors and shutters, a red-brown mud-brick watchtower, a few date palms, the buildings seen from the front and above with their doors facing the viewer, a short stretch of paved road in the middle of the cluster that starts and ends among the buildings, the road the only ground drawn, no wall, fence or gate, only the flat magenta background between and below the buildings, the whole town complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7133`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Per-zone town tile. Route settled 2026-09-05 after engine and framing tests: rd_pro__topdown, 96x96, remove_bg with magenta named, NO reference image (a reference made the model fill the frame edge to edge), buildings seen from the front and above with doors facing the viewer (without a facing cue the model draws an isometric diorama on a slab), freestanding with no wall or ground so the tile sits over the terrain, a short road among the buildings named as the only ground (a road 'from the bottom edge' made the model fill and crop the frame).

### town_galliae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7123)
- **Pack path:** `art/tiles/town_galliae.png`
- **prompt:** an isolated cut-out game sprite of a Gallic village, round timber houses with steep thatched roofs, a timber watchtower, a few dark pine trees between the houses, the buildings seen from the front and above with their doors facing the viewer, a short stretch of paved road in the middle of the cluster that starts and ends among the buildings, the road the only ground drawn, no wall, fence or gate, only the flat magenta background between and below the buildings, the whole town complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7123`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Per-zone town tile. Route settled 2026-09-05 after engine and framing tests: rd_pro__topdown, 96x96, remove_bg with magenta named, NO reference image (a reference made the model fill the frame edge to edge), buildings seen from the front and above with doors facing the viewer (without a facing cue the model draws an isometric diorama on a slab), freestanding with no wall or ground so the tile sits over the terrain, a short road among the buildings named as the only ground (a road 'from the bottom edge' made the model fill and crop the frame).

### town_italia  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7119)
- **Pack path:** `art/tiles/town_italia.png`
- **prompt:** an isolated cut-out game sprite of a rustic Italian village, timber-framed farmhouses with rough plank walls and red terracotta roofs, a small villa with a tiled porch on wooden posts, a stone well, a cypress tree, a wooden cart, the buildings seen from the front and above with their doors facing the viewer, a short stretch of paved road in the middle of the cluster that starts and ends among the buildings, the road the only ground drawn, no wall, fence or gate, only the flat magenta background between and below the buildings, the whole town complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7119`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Per-zone town tile. Route settled 2026-09-05 after engine and framing tests: rd_pro__topdown, 96x96, remove_bg with magenta named, NO reference image (a reference made the model fill the frame edge to edge), buildings seen from the front and above with doors facing the viewer (without a facing cue the model draws an isometric diorama on a slab), freestanding with no wall or ground so the tile sits over the terrain, a short road among the buildings named as the only ground (a road 'from the bottom edge' made the model fill and crop the frame).

### town_oriens  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7145)
- **Pack path:** `art/tiles/town_oriens.png`
- **prompt:** an isolated cut-out game sprite of an Eastern provincial city, honey-gold limestone houses, a small colonnade, temple domes glazed in deep turquoise, a tall stepped tower, the buildings seen from the front and above with their doors facing the viewer, a very short stub of paved road between the two middle buildings, the road the only ground drawn, no wall, fence or gate, only the flat magenta background between and below the buildings, the whole town complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7145`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Per-zone town tile. Route settled 2026-09-05 after engine and framing tests: rd_pro__topdown, 96x96, remove_bg with magenta named, NO reference image (a reference made the model fill the frame edge to edge), buildings seen from the front and above with doors facing the viewer (without a facing cue the model draws an isometric diorama on a slab), freestanding with no wall or ground so the tile sits over the terrain, a short road among the buildings named as the only ground (a road 'from the bottom edge' made the model fill and crop the frame).

### town_rome  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7104)
- **Pack path:** `art/tiles/town_rome.png`
- **prompt:** an isolated cut-out game sprite of the city of Rome, the Colosseum with its tiers of arches at the centre, white marble temples with columned porticos and triangular pediments crowded around it, a great domed rotunda, a tall triumphal arch, red terracotta roofs packed tight, a golden eagle standard on the highest roof, the buildings seen from the front and above with their doors facing the viewer, a short stretch of paved road in the middle of the cluster that starts and ends among the buildings, the road the only ground drawn, no wall, fence or gate, only the flat magenta background between and below the buildings, the whole town complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7104`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Per-zone town tile. Route settled 2026-09-05 after engine and framing tests: rd_pro__topdown, 96x96, remove_bg with magenta named, NO reference image (a reference made the model fill the frame edge to edge), buildings seen from the front and above with doors facing the viewer (without a facing cue the model draws an isometric diorama on a slab), freestanding with no wall or ground so the tile sits over the terrain, a short road among the buildings named as the only ground (a road 'from the bottom edge' made the model fill and crop the frame).

### wandering_army_africa  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7273)
- **Pack path:** `art/tiles/wandering_army_africa.png`
- **prompt:** an isolated cut-out game sprite of a Numidian warrior in a white tunic standing and holding up a tall standard topped with ostrich feathers in one hand, a bundle of javelins in the other, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7273`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Per-zone wandering army (2026-09-06), object route: rd_pro__topdown, 96x96, magenta removed, no reference, one warrior beside a standard in the zone's own style. The Galliae one is the shared run02 (horned helmet, round shield, skull standard) renamed. run02: the API's remove_bg left magenta on run01, seed incremented. next run: the planted standard floated with no ground under it; now held in the warrior's hand, seed incremented.

### wandering_army_galliae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7252)
- **Pack path:** `art/tiles/wandering_army_galliae.png`
- **prompt:** an isolated cut-out game sprite of a barbarian warrior standing with a spear and a round shield beside a war standard hung with skulls, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7252`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Map object on the object route (2026-09-06): rd_pro__topdown, 96x96, magenta named and removed, no reference, freestanding with no ground. run02: run01 was a standard with no figure; a warrior added, seed incremented. Renamed 2026-09-06 to the Galliae zone army when armies went per zone.

### wandering_army_italia  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7262)
- **Pack path:** `art/tiles/wandering_army_italia.png`
- **prompt:** an isolated cut-out game sprite of a ragged Italian brigand standing and holding up a crooked standard hung with a skull and rags in one hand, a short sword in the other, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7262`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Per-zone wandering army (2026-09-06), object route: rd_pro__topdown, 96x96, magenta removed, no reference, one warrior beside a standard in the zone's own style. The Galliae one is the shared run02 (horned helmet, round shield, skull standard) renamed. next run: the planted standard floated with no ground under it; now held in the warrior's hand, seed incremented.

### wandering_army_oriens  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7283)
- **Pack path:** `art/tiles/wandering_army_oriens.png`
- **prompt:** an isolated cut-out game sprite of a Parthian archer in scale armour and a peaked cap standing and holding up a dragon windsock standard in one hand, a composite bow in the other, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7283`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Per-zone wandering army (2026-09-06), object route: rd_pro__topdown, 96x96, magenta removed, no reference, one warrior beside a standard in the zone's own style. The Galliae one is the shared run02 (horned helmet, round shield, skull standard) renamed. run02: the API's remove_bg left magenta on run01, seed incremented. next run: the planted standard floated with no ground under it; now held in the warrior's hand, seed incremented.

### water  — INSTALLED

- **Engine:** Retro Diffusion rd_tile__single_tile (48x48, seed 3402)
- **Pack path:** `art/tiles/water.png`
- **prompt:** flat rippled sea surface seen from directly above, medium blue with small short pale cyan dashes scattered evenly, no waves, no bands, no shore, the same everywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=48`, `raw_only=true`, `seed=3402`, `style=rd_tile__single_tile`, `target=[48, 48]`, `width=48`
- **Note:** Base terrain, route settled 2026-09-05: rd_tile__single_tile (the API's purpose-built seamless tile style, cap 64, its craft guide sizes single tiles at 16-32) at 48x48, laid 2x2 by tools/tile2x2.py into the 96x96 pack tile at native pixel density (repeat period 48). Replaces rd_plus__low_res with tile_x/tile_y and the wrapping-paper prompt: that route held the wrap but the model shaded each tile's interior, which repeated as a lattice across a field (grass rim, forest diamond) and no prompt wording, seed or expansion setting removed it; the tile style does not shade the interior at all. The terrain is described plainly, seen from directly above, 'the same everywhere'. No post-processing. run02: run01 drew a horizontal wave band with a lit top edge (a block face), so the surface is described flat with no waves or bands.

## Other

### boat

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7831)
- **Pack path:** `art/sprites/boat_00.png (frame 0)`
- **prompt:** an isolated cut-out game sprite of a Roman war galley in profile facing right, a low wooden hull with a bronze ram at the bow and a curved stern post, one mast carrying a full square sail of cream linen cloth, a row of oars along the side angled down into the water, drawn without any water: the keel is the bottom of the sprite and only the flat magenta background lies below and around it, the whole ship complete and well inside the picture, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7831`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** The hero's boat, reset 2026-09-07: a plain prompt, sail named as cream linen, no oar count (the engine does not hold a count). Object route, transparent; loop on custom action, four frames, expansion on.

### canopy_tile

- **Engine:** Retro Diffusion rd_tile__single_tile (32x32, seed 9001)
- **prompt:** dense dark green broadleaf forest canopy seen from directly above, tightly packed round tree tops, the same everywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=32`, `raw_only=true`, `seed=9001`, `style=rd_tile__single_tile`, `target=[32, 32]`, `width=32`
- **Note:** 2026-09-07: seamless 32 canopy tile, the upper-terrain reference for the PixelLab forest set so every tile shares one tree grid.

### charontes

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6223)
- **prompt:** an Etruscan death demon, a heavy broad-shouldered man-shaped creature with blue-grey rotting flesh, a great hooked nose, pointed ears and two tusks in a snarling mouth, matted black hair, a short dark tunic belted at the waist, huge dark ragged wings spread behind his shoulders, a long rhomphaia held upright at rest in his right hand -- a tall spear shaft with a long straight sword blade for its head -- standing squarely facing right
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6223`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** New troop for the vampires slot (FLY|LEECH|UNDEAD), 2026-09-20, replacing the Striges. The owl the strix demands can only lunge with its feet, so every loop came out either bobbing 20px or twitching a talon; and recasting the strix as a winged woman would have made three near-identical silhouettes among the five fliers (lares, furiae, empusae). Charun's demons are Roman-Etruscan, undead, winged and heavy -- a bulk and a colour nothing else in the army has. Troop route, the settings the good troops use. run02: Dan's call, the bronze flesh-hook is replaced by a rhomphaia, a long spear shaft with a sword blade for a head. A bladed pole also gives the attack loop something to travel: run01 and run02 of the loop both refused to move the hook, the figure only brandishing it.

### forest_native_block

- **Engine:** Retro Diffusion rd_plus__topdown_map (192x136, seed 8971)
- **prompt:** dense dark green forest with a hard tree line on plain grass, seen from directly above
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=136`, `input_image_keep_alpha=false`, `input_image_path=build/art/forest_native_mock/block.png`, `raw_only=true`, `seed=8971`, `strength=0.4`, `style=rd_plus__topdown_map`, `target=[192, 136]`, `width=192`
- **Note:** 2026-09-07: the legacy forest tiles stitched at their native 48x34, no scaling, as the img2img source (Dan's order); one-line prompt.

### forest_ref

- **Engine:** Retro Diffusion rd_pro__topdown (256x256, seed 8981)
- **prompt:** A dense forest of dark green broadleaf tree canopies packed tightly together, ending in a hard uneven tree line with tree trunks showing along its lower edge, on short dull olive-green grass, seen from directly above, drawn with fine detail.
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=256`, `raw_only=true`, `reference_image_paths=["build/art/forest_native_mock/all.png"]`, `seed=8981`, `style=rd_pro__topdown`, `target=[256, 256]`, `width=256`
- **Note:** 2026-09-07: Dan's order: the legacy forest tiles stitched at native size (all thirteen pieces) as a reference image on RD Pro, the engine that takes references. run02: same seed and reference; the prompt carries the look (canopy detail, olive grass) so the reference sets layout rather than pixels.

### forest_ref_edge11

- **Engine:** Retro Diffusion rd_pro__topdown (128x128, seed 8981)
- **prompt:** A dense forest of dark green broadleaf tree canopies packed tightly together, ending in a hard uneven tree line with tree trunks showing along its lower edge, on short dull olive-green grass, seen from directly above, drawn with fine detail.
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/kings-bounty/art/tiles/forest_edge_11.png"]`, `seed=8981`, `style=rd_pro__topdown`, `target=[128, 128]`, `width=128`
- **Note:** 2026-09-07: one tile by the run02 recipe: the legacy edge tile itself as the reference, run02's prompt and seed, 128 to be centre-cropped to 96.

### grass96_pro

- **Engine:** PixelLab tiles (96 px, seed 91)
- **description:** 1). dense mossy meadow grass, deep dark green, thick soft blades, seen from directly overhead, seamless repeating texture, the same everywhere 2). dense mossy meadow grass, rich mid green with a few darker clumps, seen from directly overhead, seamless repeating texture, the same everywhere
- **Settings:** `outline_mode=segmentation`, `seed=91`, `tile_size=96`, `tile_type=square_topdown`, `tile_view=top-down`

### obstacle_01  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 8301)
- **Pack path:** `art/combat/obstacle_01.png`
- **prompt:** an isolated cut-out game sprite of a heap of fallen grey ashlar rubble, broken stone blocks piled on each other, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=8301`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Combat obstacle (2026-09-07): object route, transparent over the field; rubble, bushes, a broken wall (no water).

### obstacle_02  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 8312)
- **Pack path:** `art/combat/obstacle_02.png`
- **prompt:** an isolated cut-out game sprite of a low thicket of wild Mediterranean scrub, thorny bushes with small dark leaves and a few dry branches, ragged and uneven, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=8312`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Combat obstacle (2026-09-07): object route, transparent over the field; rubble, bushes, a broken wall (no water). run02: run01's round cartoon bushes rejected; a ragged thorny scrub thicket instead.

### obstacle_03  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 8321)
- **Pack path:** `art/combat/obstacle_03.png`
- **prompt:** an isolated cut-out game sprite of a short stub of ruined grey stone wall, a few brick courses standing with a jagged broken top and rubble at its foot, seen from the front and above, a freestanding object with no ground under it: its lowest edge is the bottom of the sprite and only the flat magenta background shows below and around it, the whole object complete and well inside the picture with empty background on every side, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=8321`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Combat obstacle (2026-09-07): object route, transparent over the field; rubble, bushes, a broken wall (no water).

### roads32

- **Engine:** PixelLab tiles (32 px, seed 5)
- **description:** packed dirt road through short green meadow grass, even medium green, seen from above
- **Settings:** `seed=5`, `tile_feature=roads`, `tile_size=32`, `tile_type=square_topdown`, `tile_view=top-down`
- **Note:** Tried and NOT shipped: the PixelLab Tiles Pro route (tile_feature 'roads', 18 sub-tiles of 32 px on an edge rule), which tools/roadtile.py consumes with --pro, stamping a 3x3 of sub-tiles per 96 px piece. The --sweep route over t32_dirt_203.json won because it gives true circular curves and 45 degree diagonals rather than a 32 px staircase.

### roads32_seg

- **Engine:** PixelLab tiles (32 px, seed 5)
- **description:** packed dirt road through short green meadow grass, even medium green, seen from above
- **Settings:** `outline_mode=segmentation`, `seed=5`, `tile_feature=roads`, `tile_size=32`, `tile_type=square_topdown`, `tile_view=top-down`
- **Note:** Tried and NOT shipped: roads32.json with outline_mode segmentation. Same Tiles Pro route, same reason it was not used -- see roads32.json.

### siege_scene

- **Engine:** Retro Diffusion rd_plus__environment (512x512, seed 8802)
- **prompt:** an orthographic strategy game battlefield map seen from directly overhead like a floor plan, a square castle courtyard of flat green grass in the centre, surrounded by a thick grey stone battlement wall on the top side, the left side and the right side, and a bottom wall broken open in its middle where the gate has been smashed, and outside the walls a moat of blue water running along the left edge, the right edge and the bottom edge of the picture, the top wall meeting the top edge, everything square and aligned to the picture edges, no perspective, no people, no text
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=512`, `raw_only=true`, `seed=8802`, `style=rd_plus__environment`, `target=[512, 512]`, `width=512`
- **Note:** 2026-09-07: one-picture siege field experiment at Dan's request; to be sliced into 96 tiles if approved.

### siege_scene_dark

- **Engine:** Retro Diffusion rd_plus__topdown_map (384x384, seed 8833)
- **Pack path:** `art/combat/siege/cell_<x>_<y>.png (36 cells at 64, tools/siegeslice.py --grid)`
- **prompt:** an orthographic strategy game battlefield map seen from directly overhead, a castle courtyard of flat plain dark mossy green grass with nothing on it, thick grey stone battlement walls along the top edge, down the left and right edges and along the bottom, the bottom wall broken open in the middle where the gate was, a straight path of grey paving stones lined with stone kerbs running up from the breach into the courtyard, a moat of blue water below the bottom wall, everything square and aligned to the picture edges, no trees, no bushes, no rocks, no people, no text
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=384`, `input_image_keep_alpha=false`, `input_image_path=build/art/siege_src_dark_384.png`, `raw_only=true`, `seed=8833`, `strength=0.55`, `style=rd_plus__topdown_map`, `target=[384, 384]`, `width=384`
- **Note:** 2026-09-10: the run02 scene with grass pixels swapped for the dark 16 px grass, img2img 0.55; k-centroid to 192 afterwards for the 32 px cells.

### siege_scene_from_mock

- **Engine:** Retro Diffusion rd_plus__topdown_map (384x384, seed 8832)
- **Pack path:** `art/combat/siege/cell_<x>_<y>.png (36 cells at 64, tools/siegeslice.py --grid)`
- **prompt:** an orthographic strategy game battlefield map seen from directly overhead, a castle courtyard of flat plain green grass with nothing on it, thick grey stone battlement walls along the top edge, down the left and right edges and along the bottom, the bottom wall broken open in the middle where the gate was, a straight path of grey paving stones lined with stone kerbs running up from the breach into the courtyard, a moat of blue water below the bottom wall, everything square and aligned to the picture edges, no trees, no bushes, no rocks, no people, no text
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=384`, `input_image_keep_alpha=false`, `input_image_path=build/art/siege_src_384x384.png`, `raw_only=true`, `seed=8832`, `strength=0.55`, `style=rd_plus__topdown_map`, `target=[384, 384]`, `width=384`
- **Note:** 2026-09-07: img2img on the 6x6 siege mock (back band included), k-centroid to 384x384 (64 per cell).

### siege_scene_grass_a

- **Engine:** Retro Diffusion rd_plus__topdown_map (384x384, seed 8832)
- **Pack path:** `art/combat/siege/cell_<x>_<y>.png (36 cells at 64, tools/siegeslice.py --grid)`
- **prompt:** an orthographic strategy game battlefield map seen from directly overhead, a castle courtyard of flat plain bright lush green grass with nothing on it, thick grey stone battlement walls along the top edge, down the left and right edges and along the bottom, the bottom wall broken open in the middle where the gate was, a straight path of grey paving stones lined with stone kerbs running up from the breach into the courtyard, a moat of blue water below the bottom wall, everything square and aligned to the picture edges, no trees, no bushes, no rocks, no people, no text
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=384`, `input_image_keep_alpha=false`, `input_image_path=build/art/siege_src_grassA_384.png`, `raw_only=true`, `seed=8832`, `strength=0.55`, `style=rd_plus__topdown_map`, `target=[192, 192]`, `width=384`
- **Note:** 2026-09-10: the run02 scene with its grass pixels replaced by the pack's new grass texture (t32_grass_a), img2img at strength 0.55, k-centroid to 192 (32 per cell) for the pack's 32 px siege cells.

### siege_scene_map

- **Engine:** Retro Diffusion rd_plus__topdown_map (384x384, seed 8812)
- **prompt:** an orthographic strategy game battlefield map seen from directly overhead like a floor plan, a square castle courtyard of flat plain green grass in the centre with nothing on it, no trees, no bushes, no rocks, surrounded by a thick grey stone battlement wall on the top side, the left side and the right side, and a bottom wall broken wide open in its middle where the gate has been smashed, with rubble at the broken ends, and outside the walls a moat of blue water running along the left edge, the right edge and the bottom edge of the picture, the top wall meeting the top edge, everything square and aligned to the picture edges, no perspective, no people, no text
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=384`, `raw_only=true`, `seed=8812`, `style=rd_plus__topdown_map`, `target=[384, 384]`, `width=384`
- **Note:** 2026-09-07: second round on the topdown map style at 384 (four 96 cells); empty courtyard, wide breach.

## Portraits and faces

### augur_portrait  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9931)
- **Pack path:** `art/portraits/augur_portrait.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of an elderly Roman augur with a full white beard, bare-headed, in a heavy toga with a broad scarlet stripe over a white tunic, holding a short pale wooden staff that curls over into a single hook at the top, a black raven perched on his shoulder, an open hilltop sky with distant birds behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/ui/alcove_augur_00.png"]`, `seed=9931`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** The Augur's portrait (2026-09-14) for the modern temple screen's portrait in-lay: the same man as the alcove_augur figure (white beard, scarlet-striped toga, lituus, raven), the figure as reference. Static: 128 still resized whole to 96, like the troop portraits. run01 (2026-09-13) was interrupted and produced no image.

### augur_portrait_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9931)
- **Pack path:** `art/portraits/augur_portrait_00..07.png`
- **prompt:** the raven turns its head, the augur's eyes lift slowly to the sky and back, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/augur_portrait/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9931`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Augur portrait loop (2026-09-13): custom action on run01, eight frames at 128, cropped to 96.

### boatmaster_africa

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9603)
- **Pack path:** `art/portraits/boatmaster_africa_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Punic shipwright with a close black beard and a linen headcloth, an adze tucked in his belt, a round harbour crowded with galleys behind him
- **Settings:** `_animation=raising an eyebrow, lips pursing, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9603`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait (2026-09-13): boat master (Boat detail) or siege engineer (Siege works detail), one per continent. Same route as the informants and priests: still at 128, eight-frame loop at 128, each frame centre-cropped to 96 (Dan-approved for town portraits).

### boatmaster_africa_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9603)
- **Pack path:** `art/portraits/boatmaster_africa_00..07.png`
- **prompt:** raising an eyebrow, lips pursing, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/boatmaster_africa/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9603`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### boatmaster_galliae

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9602)
- **Pack path:** `art/portraits/boatmaster_galliae_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Gallic river boatman with a long braided moustache and a checked cloak, a punting pole over his shoulder, a timber quay on a wide river with flat-bottomed barges behind him
- **Settings:** `_animation=a broad grin, eyes crinkling, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9602`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait (2026-09-13): boat master (Boat detail) or siege engineer (Siege works detail), one per continent. Same route as the informants and priests: still at 128, eight-frame loop at 128, each frame centre-cropped to 96 (Dan-approved for town portraits).

### boatmaster_galliae_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9602)
- **Pack path:** `art/portraits/boatmaster_galliae_00..07.png`
- **prompt:** a broad grin, eyes crinkling, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/boatmaster_galliae/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9602`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### boatmaster_italia

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9601)
- **Pack path:** `art/portraits/boatmaster_italia_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a weathered Roman harbour master in a salt-stained tunic and a leather cap, a coil of rope over his shoulder, the harbour of Ostia with moored ships behind him
- **Settings:** `_animation=squinting against the sea wind, a slow nod, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9601`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait (2026-09-13): boat master (Boat detail) or siege engineer (Siege works detail), one per continent. Same route as the informants and priests: still at 128, eight-frame loop at 128, each frame centre-cropped to 96 (Dan-approved for town portraits).

### boatmaster_italia_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9601)
- **Pack path:** `art/portraits/boatmaster_italia_00..07.png`
- **prompt:** squinting against the sea wind, a slow nod, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/boatmaster_italia/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9601`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### boatmaster_oriens

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9604)
- **Pack path:** `art/portraits/boatmaster_oriens_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Phoenician sea captain with a curled black beard in a purple robe and a felt cap, a harbour of merchant ships under a bright eastern sky behind him
- **Settings:** `_animation=eyes narrowing shrewdly, a small smile, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9604`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait (2026-09-13): boat master (Boat detail) or siege engineer (Siege works detail), one per continent. Same route as the informants and priests: still at 128, eight-frame loop at 128, each frame centre-cropped to 96 (Dan-approved for town portraits).

### boatmaster_oriens_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9604)
- **Pack path:** `art/portraits/boatmaster_oriens_00..07.png`
- **prompt:** eyes narrowing shrewdly, a small smile, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/boatmaster_oriens/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9604`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### emperor_traianus

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9901)
- **Pack path:** `art/portraits/emperor_traianus_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of Emperor Traianus, a regal clean-shaven Roman emperor in his sixties with short grey hair combed forward, a golden laurel wreath, a deep imperial purple toga with gold embroidery over a gilded cuirass, a throne room of marble columns and purple hangings behind him
- **Settings:** `_animation=a slow dignified nod, eyes steady, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9901`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Emperor Traianus (2026-09-13): the modern home castle's portrait slot. Town-portrait route: still at 128, loop at 128, frames cropped to 96.

### emperor_traianus_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9901)
- **Pack path:** `art/portraits/emperor_traianus_00..07.png`
- **prompt:** a slow dignified nod, eyes steady, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/emperor_traianus/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9901`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### informant_market

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9501)
- **Pack path:** `art/portraits/informant_market_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a stout middle-aged Roman market woman in a stained tunic and a knotted headscarf, a basket of fish on her hip, a crowded forum market behind her
- **Settings:** `_animation=<87 chars>`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9501`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait still (2026-09-13): informant (Information row) or continent Pontifex (Buy spell row) for the modern town face slot. The enemy-portrait route: rd_pro__default at 128, no references, expansion on; the loop runs at 128 on this still and each frame is centre-cropped to 96 (Dan-approved for these 8, 2026-09-13). Stills only for now.

### informant_market_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9501)
- **Pack path:** `art/portraits/informant_market_00..07.png`
- **prompt:** leaning in to share gossip, eyes darting left and right, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/informant_market/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9501`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait loop (2026-09-13), the villain route: custom action on the untouched approved still (run01), eight frames at 128, prompt expansion on; each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for these 8).

### informant_taverner

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9502)
- **Pack path:** `art/portraits/informant_taverner_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a heavy bald Roman tavern keeper in a leather apron, polishing a clay cup, racks of amphorae and a lamplit tavern behind him
- **Settings:** `_animation=slow conspiratorial wink, cloth still in hand, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9502`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait still (2026-09-13): informant (Information row) or continent Pontifex (Buy spell row) for the modern town face slot. The enemy-portrait route: rd_pro__default at 128, no references, expansion on; the loop runs at 128 on this still and each frame is centre-cropped to 96 (Dan-approved for these 8, 2026-09-13). Stills only for now.

### informant_taverner_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9502)
- **Pack path:** `art/portraits/informant_taverner_00..07.png`
- **prompt:** slow conspiratorial wink, cloth still in hand, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/informant_taverner/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9502`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait loop (2026-09-13), the villain route: custom action on the untouched approved still (run01), eight frames at 128, prompt expansion on; each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for these 8).

### informant_urchin

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9504)
- **Pack path:** `art/portraits/informant_urchin_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a barefoot Roman street boy in a ragged tunic holding a wax tablet, grinning, a narrow alley hung with laundry behind him
- **Settings:** `_animation=grin widening, a quick glance over his shoulder, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9504`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait still (2026-09-13): informant (Information row) or continent Pontifex (Buy spell row) for the modern town face slot. The enemy-portrait route: rd_pro__default at 128, no references, expansion on; the loop runs at 128 on this still and each frame is centre-cropped to 96 (Dan-approved for these 8, 2026-09-13). Stills only for now.

### informant_urchin_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9504)
- **Pack path:** `art/portraits/informant_urchin_00..07.png`
- **prompt:** grin widening, a quick glance over his shoulder, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/informant_urchin/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9504`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait loop (2026-09-13), the villain route: custom action on the untouched approved still (run01), eight frames at 128, prompt expansion on; each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for these 8).

### informant_veteran

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9503)
- **Pack path:** `art/portraits/informant_veteran_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a grizzled old legionary veteran with a torn ear, a faded red military cloak and a centurion's vine staff over his shoulder, a city gate behind him
- **Settings:** `_animation=narrowing eyes, a slow knowing nod, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9503`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait still (2026-09-13): informant (Information row) or continent Pontifex (Buy spell row) for the modern town face slot. The enemy-portrait route: rd_pro__default at 128, no references, expansion on; the loop runs at 128 on this still and each frame is centre-cropped to 96 (Dan-approved for these 8, 2026-09-13). Stills only for now.

### informant_veteran_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9503)
- **Pack path:** `art/portraits/informant_veteran_00..07.png`
- **prompt:** narrowing eyes, a slow knowing nod, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/informant_veteran/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9503`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait loop (2026-09-13), the villain route: custom action on the untouched approved still (run01), eight frames at 128, prompt expansion on; each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for these 8).

### pontifex_africa

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9507)
- **Pack path:** `art/portraits/pontifex_africa_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Punic priest of Saturn in a tall conical cap and a fringed linen robe, a sun-baked temple courtyard with a stone altar behind him
- **Settings:** `_animation=a slow thin smile, eyelids lowering, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9507`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait still (2026-09-13): informant (Information row) or continent Pontifex (Buy spell row) for the modern town face slot. The enemy-portrait route: rd_pro__default at 128, no references, expansion on; the loop runs at 128 on this still and each frame is centre-cropped to 96 (Dan-approved for these 8, 2026-09-13). Stills only for now.

### pontifex_africa_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9507)
- **Pack path:** `art/portraits/pontifex_africa_00..07.png`
- **prompt:** a slow thin smile, eyelids lowering, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/pontifex_africa/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9507`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait loop (2026-09-13), the villain route: custom action on the untouched approved still (run01), eight frames at 128, prompt expansion on; each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for these 8).

### pontifex_galliae

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9506)
- **Pack path:** `art/portraits/pontifex_galliae_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of an old Gallic druid in white robes with an oak-leaf crown and a golden sickle, a misty sacred oak grove behind him
- **Settings:** `_animation=eyes lifting upward, beard stirring in a breath, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9506`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait still (2026-09-13): informant (Information row) or continent Pontifex (Buy spell row) for the modern town face slot. The enemy-portrait route: rd_pro__default at 128, no references, expansion on; the loop runs at 128 on this still and each frame is centre-cropped to 96 (Dan-approved for these 8, 2026-09-13). Stills only for now.

### pontifex_galliae_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9506)
- **Pack path:** `art/portraits/pontifex_galliae_00..07.png`
- **prompt:** eyes lifting upward, beard stirring in a breath, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/pontifex_galliae/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9506`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait loop (2026-09-13), the villain route: custom action on the untouched approved still (run01), eight frames at 128, prompt expansion on; each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for these 8).

### pontifex_italia

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9505)
- **Pack path:** `art/portraits/pontifex_italia_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of an elderly Roman high priest with his toga drawn over his head, holding a shallow bronze offering dish, the round temple of Vesta behind him
- **Settings:** `_animation=<86 chars>`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9505`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait still (2026-09-13): informant (Information row) or continent Pontifex (Buy spell row) for the modern town face slot. The enemy-portrait route: rd_pro__default at 128, no references, expansion on; the loop runs at 128 on this still and each frame is centre-cropped to 96 (Dan-approved for these 8, 2026-09-13). Stills only for now.

### pontifex_italia_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9505)
- **Pack path:** `art/portraits/pontifex_italia_00..07.png`
- **prompt:** lips moving in prayer, eyes slowly closing and opening, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/pontifex_italia/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9505`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait loop (2026-09-13), the villain route: custom action on the untouched approved still (run01), eight frames at 128, prompt expansion on; each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for these 8).

### pontifex_oriens

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9508)
- **Pack path:** `art/portraits/pontifex_oriens_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Persian magus with a flowing beard in a white felt cap with cheek flaps, holding a bundle of sacred twigs, a fire temple with a burning altar behind him
- **Settings:** `_animation=<88 chars>`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9508`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait still (2026-09-13): informant (Information row) or continent Pontifex (Buy spell row) for the modern town face slot. The enemy-portrait route: rd_pro__default at 128, no references, expansion on; the loop runs at 128 on this still and each frame is centre-cropped to 96 (Dan-approved for these 8, 2026-09-13). Stills only for now.

### pontifex_oriens_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9508)
- **Pack path:** `art/portraits/pontifex_oriens_00..07.png`
- **prompt:** firelight flickering on his face, lips moving in a chant, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/pontifex_oriens/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9508`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town portrait loop (2026-09-13), the villain route: custom action on the untouched approved still (run01), eight frames at 128, prompt expansion on; each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for these 8).

### praefectus_castrorum

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9951)
- **Pack path:** `art/portraits/praefectus_castrorum_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Roman praefectus castrorum, a tough grey-bearded veteran camp prefect in his fifties with a scarred weathered face and close-cropped grey hair, a battered steel lorica segmentata over a red tunic, a vine-staff over his shoulder, the barracks of the legion with racks of shields and spears behind him
- **Settings:** `_animation=a slow stern nod, eyes steady, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9951`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Camp prefect (2026-09-13): runs the barracks; the modern home castle's portrait on the main and Recruit pages (the Emperor only on Audience). Town-portrait route: still at 128, loop at 128, frames cropped to 96.

### praefectus_castrorum_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9951)
- **Pack path:** `art/portraits/praefectus_castrorum_00..07.png`
- **prompt:** a slow stern nod, eyes steady, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/praefectus_castrorum/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9951`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Loop (2026-09-13): custom action on run01, eight frames at 128, each frame cropped to 96 from the top row (a centre crop cut the top of his head, as with Traianus).

### siege_africa

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9607)
- **Pack path:** `art/portraits/siege_africa_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Numidian siege engineer in a white robe over mail holding a coil of sinew rope, stone-throwing catapults in a sun-baked workshop yard behind him
- **Settings:** `_animation=eyes lifting to the catapults, a proud smile, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9607`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait (2026-09-13): boat master (Boat detail) or siege engineer (Siege works detail), one per continent. Same route as the informants and priests: still at 128, eight-frame loop at 128, each frame centre-cropped to 96 (Dan-approved for town portraits).

### siege_africa_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9607)
- **Pack path:** `art/portraits/siege_africa_00..07.png`
- **prompt:** eyes lifting to the catapults, a proud smile, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/siege_africa/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9607`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### siege_galliae

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9606)
- **Pack path:** `art/portraits/siege_galliae_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Gallic carpenter engineer in a leather apron with a heavy wooden mallet on his shoulder, a timber siege tower rising in a forest clearing behind him
- **Settings:** `_animation=wiping his brow, a tired satisfied breath, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9606`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait (2026-09-13): boat master (Boat detail) or siege engineer (Siege works detail), one per continent. Same route as the informants and priests: still at 128, eight-frame loop at 128, each frame centre-cropped to 96 (Dan-approved for town portraits).

### siege_galliae_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9606)
- **Pack path:** `art/portraits/siege_galliae_00..07.png`
- **prompt:** wiping his brow, a tired satisfied breath, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/siege_galliae/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9606`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### siege_italia

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9605)
- **Pack path:** `art/portraits/siege_italia_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Roman military engineer in a leather cuirass holding a measuring rod, a half-built ballista and timber scaffolding behind him
- **Settings:** `_animation=glancing down at his rod and back up, a firm nod, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9605`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait (2026-09-13): boat master (Boat detail) or siege engineer (Siege works detail), one per continent. Same route as the informants and priests: still at 128, eight-frame loop at 128, each frame centre-cropped to 96 (Dan-approved for town portraits).

### siege_italia_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9605)
- **Pack path:** `art/portraits/siege_italia_00..07.png`
- **prompt:** glancing down at his rod and back up, a firm nod, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/siege_italia/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9605`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### siege_oriens

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9608)
- **Pack path:** `art/portraits/siege_oriens_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Syrian siege engineer with a scholar's beard and a wrapped turban holding a bronze gear, great torsion catapults in a stone arsenal behind him
- **Settings:** `_animation=<82 chars>`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9608`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait (2026-09-13): boat master (Boat detail) or siege engineer (Siege works detail), one per continent. Same route as the informants and priests: still at 128, eight-frame loop at 128, each frame centre-cropped to 96 (Dan-approved for town portraits).

### siege_oriens_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9608)
- **Pack path:** `art/portraits/siege_oriens_00..07.png`
- **prompt:** turning the gear slowly in his fingers, eyes on it, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/siege_oriens/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9608`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Town detail portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### townhead_africa

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9803)
- **Pack path:** `art/portraits/townhead_africa_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a dark-bearded Punic town elder in a long striped robe and a tall cylindrical cap, holding a rolled scroll, a whitewashed courtyard with palm trees behind him
- **Settings:** `_animation=a slow courteous nod, eyes smiling, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9803`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Head townsperson portrait (2026-09-13): the Contracts section of the modern town main page, the same person as the standing headman figure on the backdrop. Town-portrait route: still at 128, eight-frame loop at 128, each frame centre-cropped to 96.

### townhead_africa_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9803)
- **Pack path:** `art/portraits/townhead_africa_00..07.png`
- **prompt:** a slow courteous nod, eyes smiling, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/townhead_africa/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9803`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Head townsperson portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### townhead_galliae

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9802)
- **Pack path:** `art/portraits/townhead_galliae_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Gallic town chief with a long fair moustache, a checked cloak and a gold torc at his neck, a timber hall with a thatched roof behind him
- **Settings:** `_animation=a hearty grin, moustache lifting, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9802`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Head townsperson portrait (2026-09-13): the Contracts section of the modern town main page, the same person as the standing headman figure on the backdrop. Town-portrait route: still at 128, eight-frame loop at 128, each frame centre-cropped to 96.

### townhead_galliae_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9802)
- **Pack path:** `art/portraits/townhead_galliae_00..07.png`
- **prompt:** a hearty grin, moustache lifting, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/townhead_galliae/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9802`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Head townsperson portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### townhead_italia

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9801)
- **Pack path:** `art/portraits/townhead_italia_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a grey-haired clean-shaven Roman town magistrate in a white toga with a broad purple border, holding a wax tablet, a sunlit forum colonnade behind him
- **Settings:** `_animation=a warm welcoming smile, a slight nod, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9801`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Head townsperson portrait (2026-09-13): the Contracts section of the modern town main page, the same person as the standing headman figure on the backdrop. Town-portrait route: still at 128, eight-frame loop at 128, each frame centre-cropped to 96.

### townhead_italia_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9801)
- **Pack path:** `art/portraits/townhead_italia_00..07.png`
- **prompt:** a warm welcoming smile, a slight nod, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/townhead_italia/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9801`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Head townsperson portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### townhead_oriens

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9804)
- **Pack path:** `art/portraits/townhead_oriens_00..07.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a white-bearded Greek town magistrate in a draped blue himation with a laurel wreath, a marble agora with columns behind him
- **Settings:** `_animation=a gracious smile, eyebrows lifting in greeting, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9804`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Head townsperson portrait (2026-09-13): the Contracts section of the modern town main page, the same person as the standing headman figure on the backdrop. Town-portrait route: still at 128, eight-frame loop at 128, each frame centre-cropped to 96.

### townhead_oriens_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9804)
- **Pack path:** `art/portraits/townhead_oriens_00..07.png`
- **prompt:** a gracious smile, eyebrows lifting in greeting, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/townhead_oriens/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9804`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Head townsperson portrait loop (2026-09-13): custom action on run01, eight frames at 128, each frame centre-cropped to 96.

### troop_portrait_antaei  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9994)
- **Pack path:** `art/portraits/troop_antaei.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of an Antaeus, a massive grey stone-skinned giant brute with a heavy brow and small pale glowing eyes, cracked rocky skin, huge shoulders, a barren rocky wasteland behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/antaei_00.png"]`, `seed=9994`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_baleares  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9988)
- **Pack path:** `art/portraits/troop_baleares.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Balearic slinger, a weathered grown man with a short black beard and a red cloth headband, a coarse brown tunic over one shoulder, a braided leather sling wound in his hand, a rocky island coast and the sea behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/baleares_00.png"]`, `seed=9988`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation. Re-run 2026-09-20 to match the reworked sprite: the old portrait was a lean bare-chested islander with long hair tied back, drawn from the old naked-boy sprite; the troop is now a bearded man in a red headband and a tunic over one shoulder, and the reference image is the new sprite at the same path.

### troop_portrait_coloni  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9981)
- **Pack path:** `art/portraits/troop_coloni.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a poor Roman farmhand, a thin weary man with shaggy brown hair and stubble in a patched grey-brown tunic, a pitchfork over his shoulder, a dusty farm field and a thatched hut behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/coloni_00.png"]`, `seed=9981`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_cyclopes  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9992)
- **Pack path:** `art/portraits/troop_cyclopes.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a cyclops, a huge green-skinned brute with a single large eye, a heavy brow and tusked jaw, a leather apron, a giant smith's hammer over his shoulder, a volcanic forge cave behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/cyclopes_00.png"]`, `seed=9992`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_dracones  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 10000)
- **Pack path:** `art/portraits/troop_dracones.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a green dragon, its horned head and long neck filling the frame, green scales with a pale yellow belly, sharp teeth, green wings behind it, a mountain peak and sky behind it
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/dracones_00.png"]`, `seed=10000`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_druidae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9995)
- **Pack path:** `art/portraits/troop_druidae.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Celtic druid, an old white-bearded man with a crown of oak leaves in his white hair, a white robe, a gnarled wooden staff wound with ivy, a sacred oak grove behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/druidae_00.png"]`, `seed=9995`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_elephanti  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 8112)
- **Pack path:** `art/portraits/troop_elephanti.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a war elephant of the Seleucid kings, its grey head with long white tusks and a red and gold headplate, its driver in a white tunic on its neck and the corner of the wooden fighting tower behind, dry hills and a marching column behind them
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["build/art/elephanti/run03/01_raw.png"]`, `seed=8112`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-20), the settled portrait route: 128 still with the troop's own sprite as reference, resized whole to 96.

### troop_portrait_empusae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9999)
- **Pack path:** `art/portraits/troop_empusae.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of an Empusa, a pale ghostly winged demon woman with grey-white skin, black bat wings and a gaunt face with glowing eyes, holding a scythe, a dark crossroads at night behind her
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/empusae_00.png"]`, `seed=9999`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation. The engine painted a white frame: cropped to the inner 108x108 (10..118) before the resize, at Dan's request.

### troop_portrait_equites  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9975)
- **Pack path:** `art/portraits/troop_equites.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Roman cavalryman, a bearded rider in a bronze helmet with a red crest and a bronze muscled cuirass over a red cloak, holding a spear, the head of his brown horse with a red saddle cloth beside him, an open plain behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/equites_00.png"]`, `seed=9975`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): shown in the home castle's Recruit portrait slot. 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos) at Dan's request, keeping the background rather than cropping. No animation.

### troop_portrait_fauni  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9986)
- **Pack path:** `art/portraits/troop_fauni.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a faun, a small mischievous goat-horned woodland creature with pointed ears, a brown furry face and a sly grin, holding a wooden club, a green sunlit forest glade behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/fauni_00.png"]`, `seed=9986`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_furiae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9997)
- **Pack path:** `art/portraits/troop_furiae.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Fury, a gaunt woman with ash-grey skin and hollow red eyes, live snakes writhing in her hair, black feathered wings rising behind her shoulders, a ragged black robe, a flaming orange torch raised beside her, a dark stormy underworld behind her
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/furiae_00.png"]`, `seed=9997`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation. Re-run 2026-09-20 to match the reworked sprite (furiae.json seed 6210): ash-grey skin and red eyes rather than a dark unlit face, and the torch flame orange, the accent that separates her from her own wings.

### troop_portrait_gigantes  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9998)
- **Pack path:** `art/portraits/troop_gigantes.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Gigas, a colossal wild-haired bearded giant with a bare muscular chest, green serpent scales on his lower body, lifting a huge boulder, the sky and mountain peaks behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/gigantes_00.png"]`, `seed=9998`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_hastati  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9973)
- **Pack path:** `art/portraits/troop_hastati.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Roman legionary of the hastati, a young soldier in a bronze helmet with a tall red feather crest, a mail and bronze chest plate over a red tunic, a red curved rectangular shield with a gold emblem and a spear, a legion camp with tents behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/hastati_00.png"]`, `seed=9973`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): shown in the home castle's Recruit portrait slot. 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos) at Dan's request, keeping the background rather than cropping. No animation.

### troop_portrait_lares  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9983)
- **Pack path:** `art/portraits/troop_lares.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Lar, a youthful winged Roman household spirit with a green laurel wreath in his hair and pale feathered wings rising behind his shoulders, a bronze chest plate over a white tunic, glowing faintly, the hearth shrine of a Roman house behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/lares_00.png"]`, `seed=9983`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation. Re-run 2026-09-20 to match the reworked sprite: the troop is the FLY slot and now has wings, so the portrait shows them rising behind his shoulders; the shield and sword are dropped from the wording because the frame is head-and-shoulders and they never fitted.

### troop_portrait_larvae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9984)
- **Pack path:** `art/portraits/troop_larvae.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of an undead Roman skeleton warrior, a grinning bare skull with empty eye sockets, a ragged brown cloth hanging from its bony shoulders, a rusty sword, a dark crypt with bones behind it
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/larvae_00.png"]`, `seed=9984`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_lemures  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 10085)
- **Pack path:** `art/portraits/troop_lemures.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a restless dead Roman man, a gaunt hairless human corpse with grey cracked skin, sunken hollow eyes and a thin grim mouth, a torn burial shroud over his bony shoulders, a misty graveyard at night with Roman tombstones behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/lemures_00.png"]`, `seed=10085`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation. run02: run01 drew a lemur (the animal); prompt rewritten as a restless dead man, no 'lemur' word, new seed.

### troop_portrait_ligures  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9990)
- **Pack path:** `art/portraits/troop_ligures.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Ligurian hillman, a huge bearded warrior with long brown hair and a fur cloak over his shoulders, a long-handled axe, grey mountain crags behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/ligures_00.png"]`, `seed=9990`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_lupi  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9983)
- **Pack path:** `art/portraits/troop_lupi.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a grey wolf, its head and shoulders filling the frame, yellow eyes, bared fangs, thick grey fur, a dark forest at dusk behind it
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/lupi_00.png"]`, `seed=9983`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_manes  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9991)
- **Pack path:** `art/portraits/troop_manes.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a manes, a ghostly spirit of the dead in a tattered glowing blue hooded shroud, a hollow shadowed face with faint pale eyes, reaching spectral hands, a dark underworld mist behind it
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/manes_00.png"]`, `seed=9991`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_numidae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9989)
- **Pack path:** `art/portraits/troop_numidae.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Numidian horseman, a lean dark-haired North African rider with a short beard in a blue tunic, holding a curved blade, the head of his brown horse beside him, a desert plain behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/numidae_00.png"]`, `seed=9989`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_praetoriani  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9974)
- **Pack path:** `art/portraits/troop_praetoriani.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Roman praetorian guardsman, a hard veteran in a polished steel helmet with a tall red crest, gleaming segmented steel armour over a red tunic, a red oval shield with a gold scorpion emblem and a drawn sword, the marble halls of the imperial palace behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/praetoriani_00.png"]`, `seed=9974`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): shown in the home castle's Recruit portrait slot. 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos) at Dan's request, keeping the background rather than cropping. No animation.

### troop_portrait_sagittarii  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 7903)
- **Pack path:** `art/portraits/troop_sagittarii.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Roman auxiliary archer of the sagittarii, a weathered soldier in a bronze Roman helmet with cheek guards and a small red crest, a mail shirt over a red tunic, the curved tip of a composite bow and the feathered arrows of a quiver over his shoulder, a legion camp with tents behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["build/art/sagittarii/run01/01_raw.png"]`, `seed=7903`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-16), the Hastati portrait route whole: 128 still with the troop's own sprite as reference, resized whole to 96 (Lanczos) keeping the background, as Dan asked for every troop portrait.

### troop_portrait_sarmatae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9993)
- **Pack path:** `art/portraits/troop_sarmatae.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Sarmatian warrior, a stern bearded horse-lord in a pointed bronze helmet and long scale armour, a brown fur-trimmed cloak, a long sword over his shoulder, the steppe grasslands behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/sarmatae_00.png"]`, `seed=9993`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_silvani  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9988)
- **Pack path:** `art/portraits/troop_silvani.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Silvanus, a tall wild forest guardian with deer antlers growing from his head, pointed ears, a green cloak over brown leather, a longbow over his shoulder, a deep ancient forest behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/silvani_00.png"]`, `seed=9988`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_striges  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9997)
- **Pack path:** `art/portraits/troop_striges.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a strix, a monstrous bat-winged night demon with red-brown skin, pointed ears, sharp teeth and clawed hands, leathery pink wings spread behind it, a moonlit ruined tower behind it
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/striges_00.png"]`, `seed=9997`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos), keeping the background. No animation.

### troop_portrait_tirones  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9971)
- **Pack path:** `art/portraits/troop_tirones.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a young Roman recruit, a beardless youth with short brown hair and a plain leather cap, a simple off-white wool tunic with a leather belt, a small round wooden shield and a short sword, a legion training ground with wooden posts behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/tirones_00.png"]`, `seed=9971`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): shown in the home castle's Recruit portrait slot. 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos) at Dan's request, keeping the background rather than cropping. No animation.

### troop_portrait_velites  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9972)
- **Pack path:** `art/portraits/troop_velites.png`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Roman skirmisher, a lean young man wearing a grey wolf pelt over his head and shoulders, a brown tunic, a small round shield and a light throwing javelin, a rocky hillside behind him
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/velites_00.png"]`, `seed=9972`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Static troop portrait (2026-09-13): shown in the home castle's Recruit portrait slot. 128 still with the troop's map sprite as reference, resized whole to 96 (Lanczos) at Dan's request, keeping the background rather than cropping. No animation.

## Scenes

### backdrop_pass  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 8103)
- **Pack path:** `art/scenes/pass.png`
- **prompt:** a huge iron-bound gate of dark timber and stone closing a narrow mountain pass, twin square towers either side of it and a wall running up into the crags, snow on the high rock above, a stony road leading up to the shut gate, cold blue mountain light, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=8103`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The Armenian pass gate (Oriens' elephant gate). Location-backdrop route: rd_pro__default 240x102, opaque, no reference image.

### scene_causeway  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 7502)
- **Pack path:** `art/scenes/causeway.png`
- **prompt:** a paved stone causeway revealed across a grey northern sea as the water draws back, wet black rocks and kelp on either side, the way running from the foreground shore out to a small island of dark firs and a broken crag, heavy clouds breaking overhead, cold silver light, no people
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=7502`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The Galliae vista scene: the sea drawing back off the causeway to the island. Location-backdrop route, rd_pro__default 240x102, opaque.

### scene_pharos  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 7402)
- **Pack path:** `art/scenes/pharos.png`
- **prompt:** the Pharos lighthouse of Alexandria at dawn seen from the harbour below, a tall three-stage tower of pale stone with a fire burning at its top, the great harbour and the city's roofs spread out beyond it, calm water, gulls, the first light on the sea, no people
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=7402`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The Pharos vista scene (Africa): the location-backdrop route, rd_pro__default 240x102, opaque.

### scene_rubicon  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6501)
- **Pack path:** `art/scenes/rubicon.png`
- **prompt:** a Roman legion far in the distance marching in a long column over a low wooden bridge across a small river, standards raised, green hills and poplars of northern Italy, late afternoon light, seen from a hillside above, the river and the bridge small in a wide landscape
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6501`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The Rubicon vista (game.json zones[italia].events, 2026-09-19). Same engine and size as every location backdrop (rd_pro__default, 240x102, opaque, no reference image): the shell draws it full width in the scene frame.

## Screens and UI

### alcove_augur

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 7412)
- **Pack path:** `art/ui/alcove_augur_00.png (frame 0 of 4)`
- **prompt:** an elderly Roman augur, a state priest of the auspices, standing at rest in a heavy scarlet-striped trabea toga over a white tunic, bare-headed with a full white beard, holding the lituus upright in his right hand -- a short smooth staff of pale wood curving over in a single hook at the top, no knot and no ornament -- and a large black raven perched calmly on his outstretched left forearm
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7412`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The figure who keeps the magic alcove (2026-09-12). Troop route, the pack's trained troop style at 96x96 with magenta removed, because he stands in the same tile-shaped slot on the backdrop that a troop sprite used to fill -- the screen animated Fauni there, a leftover from the original hard-coding Gnomes at the archmage's home. Neutral stance with the lituus at rest, so the gesture is the travel the loop spends. Deliberately NOT the Sibylla class portrait, which is a veiled priestess in white with a laurel sprig: this is a bearded male priest in the striped trabea with the crooked staff and a raven, so the oracle and the player are never the same figure.

### alcove_augur_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8403)
- **Pack path:** `art/ui/alcove_augur_00..13.png`
- **prompt:** the raven on his left forearm slowly spreads both wings wide, beats them once, then folds them back against its body, while the augur turns his head toward the raven to watch it, his body, arms, staff, toga and feet all completely still, smooth loop
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/alcove_augur/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8403`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** The alcove figure's idle loop (2026-09-12), eight frames at 96 from the approved still. run01 (seed 8402) asked him to raise the lituus overhead and quarter the sky: the hand never left waist height, the staff wobbled across his body, and one frame's crook detached from the shaft -- not installed. run02 moves the meaning onto the bird instead: an augur's rite is reading a bird's sign, and a small region moving against a still figure is what this engine does well (the villain face loops). The screen holds each frame for sprites.ui.alcove_figure_frame_ms. Installed as run02: the eight frames played forward then back, written to disk as a plain numbered run 00..13 (08..13 are byte copies of 06 down to 01) so the files are the sequence. The ping-pong is because frame 07 does not return to rest -- the head has not turned back -- and wrapping 07 to 00 snapped it.

### backdrop_alcove  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6402)
- **Pack path:** `art/ui/backdrop_alcove.png`
- **prompt:** a small open-air stone precinct on a high bare hilltop at first light, a low square parapet of pale weathered ashlar around a plain stone altar, a bronze tripod beside it with thin smoke rising, the ground trodden bare earth and cropped grass, a very wide pale dawn sky filling most of the picture with a scatter of dark birds circling high up, distant hills and a far-off city below the summit, still and solemn, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6402`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The magic alcove's location backdrop (2026-09-12), replacing the hill cave's, which it shared with the hills dwelling. Same route as the other six: rd_pro__default at the 240x102 design size, opaque, no background removal, scaled by the shell. Composed as the OPPOSITE of backdrop_hillcave: that one is a dark cave mouth under an overcast sky, this one is open, high and bright, with the sky taking most of the frame -- an augur's templum is a region of sky, so the sky is the subject.

### backdrop_castle

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6102)
- **prompt:** an empty fortress hall interior with hanging legionary standards, a brazier and stone arches, the hall deserted, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6102`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Location backdrop for the castle screens. Same engine as every other screen-shaped piece (rd_pro__default, opaque, no remove_bg). Authored at the 240x102 design size: RD Pro caps generation at 256x256, so design x2 (480x204) is not possible on this engine; the shell scales it by ui_scale, an exact 2x. run05 (seed 6101) put guards in the hall; run06 prompts the people away (empty, deserted, no people anywhere) with the seed incremented. The location screen draws a troop sprite in front, so the hall needs nobody.

### backdrop_dungeon

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6106)
- **prompt:** a crypt interior with columbarium niches and torchlight
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6106`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Location backdrop, same route as art/jobs/backdrop_castle.json: rd_pro__default at the 240x102 design size (RD Pro caps a side at 256), opaque, scaled 2x by the shell.

### backdrop_forest

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6104)
- **prompt:** the interior of a dense sacred grove with shafts of light through the canopy
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6104`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Location backdrop, same route as art/jobs/backdrop_castle.json: rd_pro__default at the 240x102 design size (RD Pro caps a side at 256), opaque, scaled 2x by the shell.

### backdrop_hillcave

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6106)
- **prompt:** a narrow black cave mouth high in steep, jagged, barren rocky hills, reached only over a scramble of broken boulders and loose scree, sheer grey crags on both sides, deep shadow inside the opening, an overcast sky, forbidding and hard to reach, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6106`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Location backdrop, same route as art/jobs/backdrop_castle.json: rd_pro__default at the 240x102 design size, opaque, scaled 2x by the shell. run01 (seed 6105, 'oracle smoke drifting from the opening') read as a soft cave with smoke; run02 drops the smoke and describes a much rockier, darker, less accessible cave mouth, seed incremented.

### backdrop_plains

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6103)
- **prompt:** open Italian countryside with cypress trees and distant blue hills
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6103`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Location backdrop, same route as art/jobs/backdrop_castle.json: rd_pro__default at the 240x102 design size (RD Pro caps a side at 256), opaque, scaled 2x by the shell.

### backdrop_sail  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6601)
- **Pack path:** `art/ui/backdrop_sail.png`
- **prompt:** a Greek penteconter galley under way on the open sea at sunset, seen from behind and to one side, long low black hull, a single bank of oars out, one square sail set, a painted eye at the bow, the low sun on the horizon behind it laying a gold track on calm water, no people visible
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6601`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The sail-to scene (REQ-221c): the province picker and its confirmation are drawn over this, the same 240x102 backdrop shape as every location screen (rd_pro__default, opaque, no reference image).

### backdrop_town

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6124)
- **prompt:** an empty Roman forum street with a colonnade and market awnings, wide view, weathered travertine and brick, terracotta roofs, sun-faded awnings, dusty paving, a muted earthy palette, the stalls unattended and the street deserted, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6124`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Location backdrop, same route as art/jobs/backdrop_castle.json: rd_pro__default at the 240x102 design size, opaque, scaled 2x by the shell. Earlier runs with 'townspeople' (and with the portraits as references) all put figures in the street and were purged; this prompt names the street as empty, deserted, unattended, no people anywhere, with no reference images and a new seed. The location screen draws a troop sprite in front, so the street needs nobody.

### backdrop_town_africa  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6703)
- **Pack path:** `art/ui/backdrop_town_africa.png`
- **prompt:** an empty street in a North African town, whitewashed and sand-coloured walls, flat roofs, a horseshoe arch, date palms above a courtyard wall, hard bright sunlight and short shadows, dust in the air, the street deserted, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6703`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The town screen backdrop for africa (REQ-221d: a zone names its own). Same route as every location backdrop: rd_pro__default at 240x102, opaque, no reference image. The original backdrop_town.png stays as Rome's own, named by the town.

### backdrop_town_galliae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6702)
- **Pack path:** `art/ui/backdrop_town_galliae.png`
- **prompt:** an empty street in a Gallo-Roman town in the north, half-timbered houses over stone footings, steep slate roofs, a muddy lane, smoke from a chimney, a grey overcast sky and dark woods beyond the roofs, the street deserted, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6702`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The town screen backdrop for galliae (REQ-221d: a zone names its own). Same route as every location backdrop: rd_pro__default at 240x102, opaque, no reference image. The original backdrop_town.png stays as Rome's own, named by the town.

### backdrop_town_italia  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6701)
- **Pack path:** `art/ui/backdrop_town_italia.png`
- **prompt:** an empty street in a small Italian country town, stuccoed walls in ochre and rose, terracotta roofs, a stone well and a vine on a trellis, cypresses and low hills beyond the roofs, warm afternoon light, the street deserted, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6701`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The town screen backdrop for italia (REQ-221d: a zone names its own). Same route as every location backdrop: rd_pro__default at 240x102, opaque, no reference image. The original backdrop_town.png stays as Rome's own, named by the town.

### backdrop_town_oriens  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6704)
- **Pack path:** `art/ui/backdrop_town_oriens.png`
- **prompt:** an empty colonnaded avenue in an eastern Roman city, tall limestone columns with striped awnings between them, a tetrapylon at the far end, pale stone paving, dry hills beyond, low golden light, the avenue deserted, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6704`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** The town screen backdrop for oriens (REQ-221d: a zone names its own). Same route as every location backdrop: rd_pro__default at 240x102, opaque, no reference image. The original backdrop_town.png stays as Rome's own, named by the town.

### disgraced_dux  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 7431)
- **Pack path:** `art/ui/disgraced_dux.png`
- **prompt:** a wide view of a forest edge in the rain, a defeated frontier warlord in torn mail and a ragged, bloodied wolf-pelt cloak sits slumped on a tree stump, a bandage over one eye, his iron torc broken beside him, a splintered axe in the mud, a burned palisade in the distance, an overcast sky, alive but beaten, the figure small in a wide landscape view
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png"]`, `seed=7431`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Temporary-death scene for the dux (2026-09-14): backdrop route (rd_pro__default at 240x102, opaque, shown at 3x), the class's hero seed, the class portrait's description made beaten and tattered but alive. Run 2 sends the class portrait as a reference image so the figure matches the character.

### disgraced_legatus  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 7401)
- **Pack path:** `art/ui/disgraced_legatus.png`
- **prompt:** a wide view of a muddy road after a lost battle at dusk, a defeated Roman general in a dented muscled bronze cuirass with lion-head shoulder pieces and a torn, mud-stained red cloak sits slumped on a broken cart wheel, head bowed, a bandage on his arm, his crested helmet lying in the mud at his feet, broken spears and a fallen standard nearby, smoke rising from a burned camp on the horizon, a grey sky, alive but beaten, the figure small in a wide landscape view
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/legatus.png"]`, `seed=7401`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Temporary-death scene for the legatus (2026-09-14): backdrop route (rd_pro__default at 240x102, opaque, shown at 3x), the class's hero seed, the class portrait's description made beaten and tattered but alive. Run 2 sends the class portrait as a reference image so the figure matches the character.

### disgraced_praetorianus  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 7411)
- **Pack path:** `art/ui/disgraced_praetorianus.png`
- **prompt:** a wide view of a ruined roadside shrine in the rain, a defeated Roman warrior-priest with his white veil torn and soiled and his gilded cuirass scratched and dented kneels beside a cold, overturned altar, his sacrificial bowl dropped on the ground, a burned field behind him, dark clouds, alive but beaten, the figure small in a wide landscape view
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/praetorianus.png"]`, `seed=7411`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Temporary-death scene for the praetorianus (2026-09-14): backdrop route (rd_pro__default at 240x102, opaque, shown at 3x), the class's hero seed, the class portrait's description made beaten and tattered but alive. Run 2 sends the class portrait as a reference image so the figure matches the character.

### disgraced_sibylla  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 7421)
- **Pack path:** `art/ui/disgraced_sibylla.png`
- **prompt:** a wide view of a ruined temple at dusk, a defeated veiled Vestal priestess in torn, ash-stained white robes with a crooked gold fillet sits exhausted against a fallen column, a withered laurel sprig in her lap, the sacred flame gone out in a cracked bowl beside her, smoke drifting across a grey sky, alive but beaten, the figure small in a wide landscape view
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=7421`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Temporary-death scene for the sibylla (2026-09-14): backdrop route (rd_pro__default at 240x102, opaque, shown at 3x), the class's hero seed, the class portrait's description made beaten and tattered but alive. Run 2 sends the class portrait as a reference image so the figure matches the character.

### emperor_traianus_figure

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 9921)
- **Pack path:** `art/ui/emperor_traianus_figure_00..07.png`
- **prompt:** Emperor Traianus standing at rest, a regal grey-haired Roman emperor in a deep imperial purple toga with gold embroidery over a gilded cuirass, a golden laurel wreath on his head, one hand resting on a tall gilded sceptre
- **Settings:** `_animation=<125 chars>`, `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=9921`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Traianus standing on the home castle backdrop (2026-09-13): the head townsperson figure route, troop style at 96, magenta removed, no crop.

### emperor_traianus_figure_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 9921)
- **Pack path:** `art/ui/emperor_traianus_figure_00..07.png`
- **prompt:** breathes slowly and lifts his chin a little and lowers it, his body, sceptre, toga and feet all completely still, smooth loop
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/emperor_traianus_figure/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9921`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Figure idle loop (2026-09-13), eight frames at 96, alpha kept.

### end_carpet  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__topdown (96x96, seed 7301)
- **Pack path:** `art/ui/end_carpet.png`
- **prompt:** an isolated cut-out game sprite of a straight red carpet runner with a woven gold border along both long sides, seen from directly above, running straight from the top edge of the picture to the bottom edge and cut off flat by both, narrower than the picture with only the flat magenta background showing to its left and right, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7301`, `style=rd_pro__topdown`, `target=[96, 96]`, `width=96`
- **Note:** Win-cartoon carpet tile (2026-09-06): drawn as a column of stacked tiles over the grass tile, so it is a transparent strip that runs top to bottom on purpose; object route otherwise.

### end_lose_screen  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (144x170, seed 6302)
- **Pack path:** `art/ui/end_lose_screen.png`
- **prompt:** a broken Roman eagle standard fallen in mud with a burning frontier fort behind, the mud strewn with the gear of fallen legionaries: a snapped gladius, a dented crested helmet, a split rectangular scutum shield, a bent pilum and scattered mail, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=170`, `raw_only=true`, `seed=6302`, `style=rd_pro__default`, `target=[144, 170]`, `width=144`
- **Note:** Ending lose screen, screen-shaped route: rd_pro__default at the 144x170 design size (RD Pro caps a side at 256, so x2 = 288x340 does not fit; the shell scales it by an exact 2x), opaque, no references since there is no figure. Worklist prompt plus 'no people anywhere'. run02 adds the fallen legionaries' gear in the mud (snapped gladius, dented helmet, split scutum, bent pilum, mail), seed incremented.

### end_win_screen  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (144x170, seed 6205)
- **Pack path:** `art/ui/end_win_screen.png`
- **prompt:** the recovered golden legionary eagle standard in strict side profile, the same eagle as the reference, mounted on top of a tall standard pole and carried upright at the head of a procession of Roman legionaries in crested helmets and red cloaks marching in a line up broad marble steps toward Imperator Trajan, who stands at the top in a laurel crown and a purple toga over a gilded cuirass with his arms open to receive it, the eagle the brightest thing in the scene; behind and far below, a vast crowd fills the Roman Forum to the horizon, rendered tiny and indistinct, a dense sea of small heads with no faces, in muted tones, temples and columns beyond, a clear sky
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=170`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png", "assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/ui/end_lose_screen.png"]`, `seed=6205`, `style=rd_pro__default`, `target=[144, 170]`, `width=144`
- **Note:** Ending win screen, screen-shaped route: rd_pro__default at the 144x170 design size (RD Pro caps a side at 256, so x2 = 288x340 does not fit), opaque, scaled 2x by the shell. The four approved class portraits are passed as reference_images so the emperor is drawn in their rendering, the picker's route. The brief (GLORY-OF-ROME 1.2) makes the Emperor Trajan and the quest the recovered Aquila; the crowd is described tiny, faceless and muted so it stays a background mass. run02: back turned to the crowd, the eagle mounted on a pole, the installed lose screen added as a fifth reference so the eagle and rendering match it, seed incremented. run03: run02 came out as a view from behind, so the emperor and the eagle are both described as seen from the front facing the viewer with the crowd entirely behind them, seed incremented. run04: the view from behind is the wanted one (run02), with the eagle now also seen from behind, facing the crowd with the emperor, seed incremented. run05: the eagle was never close enough to the lose screen's, so it is now in strict profile on its pole, carried up the steps to the waiting emperor by a procession of legionaries, seed incremented.

### headman_africa

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 9703)
- **Pack path:** `art/ui/headman_africa_00..07.png`
- **prompt:** a Punic town elder standing at rest in a long striped robe and a tall cylindrical cap, dark-bearded, holding a rolled scroll at his side
- **Settings:** `_animation=<116 chars>`, `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=9703`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The town's head townsperson (2026-09-13), one per continent, standing on the town backdrop in the tile-sized slot the troop used to fill. The alcove figure's route: the pack's trained troop style at 96x96, magenta removed, no crop.

### headman_africa_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 9703)
- **Pack path:** `art/ui/headman_africa_00..07.png`
- **prompt:** breathes slowly and taps the scroll against his palm once, his body, robe and feet all completely still, smooth loop
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/headman_africa/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9703`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Head townsperson idle loop (2026-09-13): the alcove figure's loop route, eight frames at 96 on run01, alpha kept.

### headman_galliae

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 9702)
- **Pack path:** `art/ui/headman_galliae_00..07.png`
- **prompt:** a Gallic town chief standing at rest in a checked cloak over a tunic and trousers, a gold torc at his neck and a long fair moustache, holding a wooden staff upright
- **Settings:** `_animation=<113 chars>`, `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=9702`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The town's head townsperson (2026-09-13), one per continent, standing on the town backdrop in the tile-sized slot the troop used to fill. The alcove figure's route: the pack's trained troop style at 96x96, magenta removed, no crop.

### headman_galliae_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 9702)
- **Pack path:** `art/ui/headman_galliae_00..07.png`
- **prompt:** breathes slowly and strokes his moustache once, his body, staff, cloak and feet all completely still, smooth loop
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/headman_galliae/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9702`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Head townsperson idle loop (2026-09-13): the alcove figure's loop route, eight frames at 96 on run01, alpha kept.

### headman_italia

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 9701)
- **Pack path:** `art/ui/headman_italia_00..07.png`
- **prompt:** a Roman town magistrate standing at rest in a white toga with a broad purple border, grey-haired and clean-shaven, holding a wax tablet at his side
- **Settings:** `_animation=<129 chars>`, `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=9701`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The town's head townsperson (2026-09-13), one per continent, standing on the town backdrop in the tile-sized slot the troop used to fill. The alcove figure's route: the pack's trained troop style at 96x96, magenta removed, no crop.

### headman_italia_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 9701)
- **Pack path:** `art/ui/headman_italia_00..07.png`
- **prompt:** breathes slowly and turns his head a little to one side and back, his body, arms, toga and feet all completely still, smooth loop
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/headman_italia/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9701`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Head townsperson idle loop (2026-09-13): the alcove figure's loop route, eight frames at 96 on run01, alpha kept.

### headman_oriens

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 9704)
- **Pack path:** `art/ui/headman_oriens_00..07.png`
- **prompt:** a Greek town magistrate of the eastern provinces standing at rest in a draped blue himation with a laurel wreath, white-bearded, holding a short staff of office
- **Settings:** `_animation=<123 chars>`, `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=9704`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The town's head townsperson (2026-09-13), one per continent, standing on the town backdrop in the tile-sized slot the troop used to fill. The alcove figure's route: the pack's trained troop style at 96x96, magenta removed, no crop.

### headman_oriens_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 9704)
- **Pack path:** `art/ui/headman_oriens_00..07.png`
- **prompt:** breathes slowly and lifts his chin a little and lowers it, his body, staff, robe and feet all completely still, smooth loop
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/headman_oriens/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9704`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Head townsperson idle loop (2026-09-13): the alcove figure's loop route, eight frames at 96 on run01, alpha kept.

### hud_boat_silhouette  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7710)
- **Pack path:** `art/ui/hud_boat_silhouette.png`
- **prompt:** a black silhouette of a small Roman sailing boat with a single square sail against a deep blue background, painted as a small game sidebar panel, the picture filling the whole square edge to edge, no frame, no border, no writing, no lettering
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7710`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** The 'no boat' panel (2026-09-13), shown in the modern town Boat screen's portrait slot where the town has no boat master. The no-contract silhouette's route exactly: rd_pro__default, 96x96, opaque, no references, no lettering, no crop.

### hud_contract_silhouette  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7702)
- **Pack path:** `art/ui/hud_contract_silhouette.png`
- **prompt:** a black silhouette of a hooded bust against a deep blue background, painted as a small game sidebar panel, the picture filling the whole square edge to edge, no frame, no border, no writing, no lettering
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7702`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** HUD sidebar panel (2026-09-06), the inventory icon route: opaque framed card, rd_pro__default, 96x96, no references, no lettering. The siege and magic stills are frame 0 of their four-frame loops. Regenerated without a painted frame (2026-09-06): the shell draws the frame.

### hud_gold_purse  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7862)
- **Pack path:** `art/ui/hud_gold_purse.png`
- **prompt:** a bulging brown leather coin purse tied with a cord with a few gold coins beside it, sitting on a grey flagstone shelf in the upper two thirds of the picture, the whole lower third of the picture a plain flat black band with nothing drawn on it, the picture filling the whole square edge to edge, no frame, no border, no writing, no lettering
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7862`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** HUD sidebar panel (2026-09-06), the inventory icon route: opaque framed card, rd_pro__default, 96x96, no references, no lettering. The siege and magic stills are frame 0 of their four-frame loops. Regenerated without a painted frame (2026-09-06): the shell draws the frame. 2026-09-07: lower third left as a flat black band so the HUD gold figure (bottom 10 of 34 cell rows) sits on plain colour.

### hud_magic_00  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 8742)
- **Pack path:** `art/ui/hud_magic_00.png`
- **prompt:** a bronze tripod brazier with a bright orange flame rising from it against a deep blue background, painted as a small game sidebar panel, the picture filling the whole square edge to edge, no frame, no border, no writing, no lettering
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=128`, `raw_only=true`, `seed=8742`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** HUD sidebar panel (2026-09-06), the inventory icon route: opaque framed card, rd_pro__default, 96x96, no references, no lettering. The siege and magic stills are frame 0 of their four-frame loops. Regenerated without a painted frame (2026-09-06): the shell draws the frame. 2026-09-07: 128 route (as the villains): still at 128, loop at 128, frames centre-cropped to 96 to drop the painted frame.

### hud_magic_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 8743)
- **Pack path:** `art/ui/hud_magic_00..03.png`
- **prompt:** sacred flame changing colour, gold to blue to violet to red, static brazier, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=4`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/hud_magic_00/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8743`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** HUD panel loop (2026-09-06): custom action on the opaque framed still, four frames, expansion on, tag prompt; the sidebar cycles it at two frames a second. run02: from the frameless still. 2026-09-07: loop at 128 on build/art/hud_magic_00/run01; frames centre-cropped to 96 after.

### hud_magic_silhouette  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7732)
- **Pack path:** `art/ui/hud_magic_silhouette.png`
- **prompt:** a grey silhouette of a bronze tripod brazier against a deep blue background, painted as a small game sidebar panel, the picture filling the whole square edge to edge, no frame, no border, no writing, no lettering
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7732`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** HUD sidebar panel (2026-09-06), the inventory icon route: opaque framed card, rd_pro__default, 96x96, no references, no lettering. The siege and magic stills are frame 0 of their four-frame loops. Regenerated without a painted frame (2026-09-06): the shell draws the frame.

### hud_puzzle_grid  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7754)
- **Pack path:** `art/ui/hud_puzzle_grid.png`
- **prompt:** a close crop from the middle of a much larger aged parchment map, the parchment filling the whole square with no edge, corner, rim or border of the parchment visible anywhere, faint coastlines and a few islands drawn in brown ink, no compass rose, no writing, no lettering
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7754`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** HUD sidebar panel (2026-09-06), the inventory icon route: opaque framed card, rd_pro__default, 96x96, no references, no lettering. The siege and magic stills are frame 0 of their four-frame loops. Regenerated without a painted frame (2026-09-06): the shell draws the frame. Re-run: the previous run still painted an inset panel; wording strengthened, seed incremented. Re-run as a crop from the middle of a larger map so no parchment edge appears.

### hud_siege_00  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7823)
- **Pack path:** `art/ui/hud_siege_00.png`
- **prompt:** a tall wooden Roman siege tower on wheels, several storeys high with a hide-covered front and a drawbridge ramp at the top, seen from the side, against a pale sky background, painted as a small game sidebar panel, the picture filling the whole square edge to edge, no frame, no border, no inset panel, no inner rectangle, the background one continuous surface right to every edge, no writing, no lettering
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7823`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** HUD sidebar panel (2026-09-06), the inventory icon route: opaque framed card, rd_pro__default, 96x96, no references, no lettering. The siege and magic stills are frame 0 of their four-frame loops. Regenerated without a painted frame (2026-09-06): the shell draws the frame. Re-run: the previous run still painted an inset panel; wording strengthened, seed incremented. 2026-09-07: ballista replaced by a siege tower at Dan's request.

### hud_siege_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 7824)
- **Pack path:** `art/ui/hud_siege_00..03.png`
- **prompt:** drawbridge ramp lowers and rises, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/hud_siege_00/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=7824`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** HUD panel loop (2026-09-06): custom action on the opaque framed still, four frames, expansion on, tag prompt; the sidebar cycles it at two frames a second. run02: from the frameless still. Re-run from the inset-free ballista still. 2026-09-07: re-run on the siege tower still.

### hud_siege_silhouette  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7812)
- **Pack path:** `art/ui/hud_siege_silhouette.png`
- **prompt:** a black silhouette of a tall wooden Roman siege tower on wheels with a drawbridge ramp at the top against a deep blue background, painted as a small game sidebar panel, the picture filling the whole square edge to edge, no frame, no border, no writing, no lettering
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7812`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** HUD sidebar panel (2026-09-06), the inventory icon route: opaque framed card, rd_pro__default, 96x96, no references, no lettering. The siege and magic stills are frame 0 of their four-frame loops. Regenerated without a painted frame (2026-09-06): the shell draws the frame. 2026-09-07: siege tower.

### inventory_artifact_amulet  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 8602)
- **Pack path:** `art/ui/inventory_artifact_amulet.png`
- **prompt:** a gold locket amulet embossed with a lightning bolt, lying flat on its back with its cord coiled loosely beside it, seen from directly above, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no inset panel, no inner rectangle, no writing, no lettering
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=128`, `raw_only=true`, `seed=8602`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame). 2026-09-07: lying flat (not hanging) at Dan's request; 128 route, centre-cropped to 96 to drop the painted frame.

### inventory_artifact_anchor  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7513)
- **Pack path:** `art/ui/inventory_artifact_anchor.png`
- **prompt:** a bronze anchor with a trident-shaped crossbar, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no inset panel, no inner rectangle, the background one continuous surface right to every edge, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7513`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame). Re-run: the previous run still painted an inset panel; wording strengthened, seed incremented.

### inventory_artifact_articles  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7524)
- **Pack path:** `art/ui/inventory_artifact_articles.png`
- **prompt:** a bronze tablet with a plain hammered surface and a hanging red wax seal on a cord, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no inset panel, no inner rectangle, the background one continuous surface right to every edge, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7524`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. run02: no words on anything (Dan): lettering, banner and the Gallic cockerel removed; seed incremented. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame). Re-run: the previous run still painted an inset panel; wording strengthened, seed incremented.

### inventory_artifact_book  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (104x104, seed 7533)
- **Pack path:** `art/ui/inventory_artifact_book.png`
- **prompt:** a torn scrap of ancient papyrus, blank and faded, curling at the edges, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=104`, `raw_only=true`, `seed=7533`, `style=rd_pro__default`, `target=[104, 104]`, `width=104`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. run02: no words on anything (Dan): lettering, banner and the Gallic cockerel removed; seed incremented. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame). 2026-09-07: generated at 104 and centre-cropped to 96 with tools/cropcentre.py to drop a one-pixel border (Dan's order).

### inventory_artifact_crown  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7542)
- **Pack path:** `art/ui/inventory_artifact_crown.png`
- **prompt:** a golden laurel wreath crown, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7542`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame).

### inventory_artifact_ring  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7552)
- **Pack path:** `art/ui/inventory_artifact_ring.png`
- **prompt:** a heavy gold equestrian signet ring, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7552`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame).

### inventory_artifact_shield  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7563)
- **Pack path:** `art/ui/inventory_artifact_shield.png`
- **prompt:** a curved rectangular Roman shield bearing a Trojan palladium device, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no inset panel, no inner rectangle, the background one continuous surface right to every edge, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7563`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame). Re-run: the previous run still painted an inset panel; wording strengthened, seed incremented.

### inventory_artifact_sword  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7572)
- **Pack path:** `art/ui/inventory_artifact_sword.png`
- **prompt:** an ornate gladius short sword with a ruby pommel and flame etching on the blade, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7572`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame).

### inventory_zone_africa  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7703)
- **Pack path:** `art/ui/inventory_zone_africa.png`
- **prompt:** a heraldic shield, a gold-rimmed escutcheon shape like a coat of arms, bearing a palm and an elephant above a coastal strip and dunes, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7703`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. run02: no words on anything (Dan): lettering, banner and the Gallic cockerel removed; seed incremented. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame). 2026-09-07: re-run as a shield to match Galliae and Italia.

### inventory_zone_galliae  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7594)
- **Pack path:** `art/ui/inventory_zone_galliae.png`
- **prompt:** a heraldic emblem of forested hills with a stone river bridge under them, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no inset panel, no inner rectangle, the background one continuous surface right to every edge, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7594`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. run02: no words on anything (Dan): lettering, banner and the Gallic cockerel removed; seed incremented. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame). Re-run: the previous run still painted an inset panel; wording strengthened, seed incremented.

### inventory_zone_italia  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7682)
- **Pack path:** `art/ui/inventory_zone_italia.png`
- **prompt:** a heraldic emblem of the Italian peninsula with a she-wolf and laurel, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7682`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame).

### inventory_zone_oriens  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 7715)
- **Pack path:** `art/ui/inventory_zone_oriens.png`
- **prompt:** a heraldic shield, a gold-rimmed escutcheon shape like a coat of arms, bearing a domed eastern skyline with a palm and a sun rising over mountains, painted as a small game inventory icon, the object large and centred on a plain dark parchment background that fills the whole picture edge to edge, no frame, no border, no inset panel, no inner rectangle, the background one continuous surface right to every edge, no writing, no lettering, no banner, no ribbon
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `seed=7715`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Inventory icon (2026-09-06): opaque framed card like the original pack's, drawn in the inventory belt and the puzzle grid at the tile size; rd_pro__default, the screen engine, 96x96, no references. run02: no words on anything (Dan): lettering, banner and the Gallic cockerel removed; seed incremented. Regenerated without a painted frame (2026-09-06): the shell draws the frame (sprites.ui.panel_frame). Re-run: the previous run still painted an inset panel; wording strengthened, seed incremented. 2026-09-07: re-run as a shield to match Galliae and Italia.

### palace_barracks  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6611)
- **Pack path:** `art/ui/backdrop_palace_barracks.png`
- **prompt:** the armoury court of an imperial Roman palace, a marble colonnade along a paved parade ground, racks of polished shields, spears and crested helmets set against the wall, legionary standards and gilded eagles raised on poles, red and purple banners, a weapons trophy of captured arms, grand and orderly, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6611`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Roma's own scenes (2026-09-15): the Recruit page. Backdrop route -- rd_pro__default at 240x102, drawn whole at 3x. Imperial, not the shared fortress hall the other castles use.

### palace_throne  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6621)
- **Pack path:** `art/ui/backdrop_palace_throne.png`
- **prompt:** the throne room of an imperial Roman palace, a raised marble dais under a coffered gilded ceiling, an ivory and gold curule throne on the dais, a great purple canopy behind it, tall porphyry columns either side, gilded eagles and laurel wreaths on the walls, lamps burning on bronze stands, solemn and magnificent, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6621`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Roma's own scenes (2026-09-15): the Audience page. Backdrop route -- rd_pro__default at 240x102, drawn whole at 3x. Imperial, not the shared fortress hall the other castles use.

### palace_usher

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 9971)
- **Pack path:** `art/ui/palace_usher_00..07.png`
- **prompt:** a Roman palace usher standing at rest, a clean-shaven middle-aged chamberlain in a long white tunic with a purple border under a light grey cloak, a slim gilded staff of office upright in one hand, a folded wax tablet in the other, calm and formal
- **Settings:** `_animation=<104 chars>`, `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=9971`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The Emperor's chamberlain, who greets you in the palace atrium (2026-09-15): the figure route the camp prefect and the Augur use -- troop style at 96, magenta removed, no crop.

### palace_usher_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 9971)
- **Pack path:** `art/ui/palace_usher_00..07.png`
- **prompt:** breathes slowly and tips the staff of office once, his body, head and feet completely still, smooth loop
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/palace_usher/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9971`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Loop (2026-09-15): custom action on run01, eight frames at 96, alpha kept.

### palace_welcome  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (240x102, seed 6601)
- **Pack path:** `art/ui/backdrop_palace_welcome.png`
- **prompt:** the grand entrance hall of an imperial Roman palace, a wide marble atrium with polished veined marble floor, tall fluted columns in two rows, gilded capitals, a shallow reflecting pool at the centre, purple hangings with gold embroidery between the columns, bronze braziers burning, a broad staircase at the far end leading up into light, richly decorated and imperial, no people anywhere
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6601`, `style=rd_pro__default`, `target=[240, 102]`, `width=240`
- **Note:** Roma's own scenes (2026-09-15): the palace welcome (Recruit / Audience / Leave). Backdrop route -- rd_pro__default at 240x102, drawn whole at 3x. Imperial, not the shared fortress hall the other castles use.

### praefectus_castrorum_figure

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 9961)
- **Pack path:** `art/ui/praefectus_castrorum_figure_00..07.png`
- **prompt:** a Roman praefectus castrorum standing at rest, a tough grey-bearded veteran camp prefect in a battered steel lorica segmentata over a red tunic, a crested centurion helmet under one arm, a vine-staff in his other hand
- **Settings:** `_animation=<122 chars>`, `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=9961`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Camp prefect standing on the home castle backdrop (2026-09-13): head townsperson figure route, troop style at 96, magenta removed, no crop.

### praefectus_castrorum_figure_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 9961)
- **Pack path:** `art/ui/praefectus_castrorum_figure_00..07.png`
- **prompt:** breathes slowly and taps the vine-staff once against his palm, his body, helmet and feet all completely still, smooth loop
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/praefectus_castrorum_figure/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9961`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Loop (2026-09-13): custom action on run01, eight frames at 96, alpha kept.

### promotion_rank_1  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (144x170, seed 6301)
- **Pack path:** `art/ui/promotion_rank_1.png`
- **prompt:** Emperor Traianus, a regal clean-shaven grey-haired Roman emperor in a golden laurel wreath and a deep imperial purple toga with gold embroidery over a gilded cuirass, standing before his marble throne under purple hangings and presenting a round silver phalera medal on a leather harness toward the viewer as an award, a proud stern expression, the award catching the light and the brightest thing in the scene, marble columns and a golden eagle standard behind him, the scene filling the whole picture edge to edge
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=170`, `raw_only=true`, `seed=6301`, `style=rd_pro__default`, `target=[144, 170]`, `width=144`
- **Note:** Promotion image (2026-09-13), first promotion award: silver phalerae: the modern home castle's Promotion page, the same 144x170 size and route as end_win_screen / end_lose_screen (rd_pro__default, opaque, drawn at the pack's UI scale beside the text). No class shown; no reference images.

### promotion_rank_2  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (144x170, seed 6302)
- **Pack path:** `art/ui/promotion_rank_2.png`
- **prompt:** Emperor Traianus, a regal clean-shaven grey-haired Roman emperor in a golden laurel wreath and a deep imperial purple toga with gold embroidery over a gilded cuirass, standing before his marble throne under purple hangings and presenting a heavy golden torc and a pair of gold arm rings, held up high in both hands toward the viewer as an award, a proud stern expression, the award catching the light and the brightest thing in the scene, marble columns and a golden eagle standard behind him, the scene filling the whole picture edge to edge
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=170`, `raw_only=true`, `seed=6302`, `style=rd_pro__default`, `target=[144, 170]`, `width=144`
- **Note:** Promotion image (2026-09-13), second promotion award: gold torc and armillae: the modern home castle's Promotion page, the same 144x170 size and route as end_win_screen / end_lose_screen (rd_pro__default, opaque, drawn at the pack's UI scale beside the text). No class shown; no reference images.

### promotion_rank_3  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (144x170, seed 6311)
- **Pack path:** `art/ui/promotion_rank_3.png`
- **prompt:** Emperor Traianus, a regal clean-shaven grey-haired Roman emperor in a golden laurel wreath and a deep imperial purple toga with gold embroidery over a gilded cuirass, standing before his marble throne under purple hangings and presenting a long ivory sceptre topped with a small golden eagle, held out horizontally in both hands toward the viewer as an award, a proud stern expression, the award catching the light and the brightest thing in the scene, marble columns and a golden eagle standard behind him, the scene filling the whole picture edge to edge
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=170`, `raw_only=true`, `seed=6311`, `style=rd_pro__default`, `target=[144, 170]`, `width=144`
- **Note:** Promotion image (2026-09-13), third promotion award: the ivory eagle sceptre (scipio eburneus) of the triumphal honours -- the golden laurel crown was the Emperor's alone. The modern home castle's Promotion page, 144x170 like end_win_screen / end_lose_screen, rd_pro__default, no class shown, no reference images.

### scene_column

- **Engine:** Retro Diffusion rd_pro__default (28x102, seed 6501)
- **Pack path:** `art/ui/scene_column.png`
- **prompt:** a narrow pale weathered marble Roman column shaft with vertical fluting, dark green ivy climbing and winding around it, seen straight on, the column filling the whole width, seamless vertical tile
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=102`, `raw_only=true`, `seed=6501`, `style=rd_pro__default`, `target=[28, 102]`, `tile_y=true`, `width=28`
- **Note:** The side bars beside a place backdrop (2026-09-15): one 28x102 strip repeated top to bottom at 1x, mirrored for the right bar. rd_pro__default like the backdrops, tile_y for a seamless repeat (support on this style unverified; first use).

### splash_logo_emblem

- **Engine:** Retro Diffusion rd_pro__default (44x44, seed 6402)
- **Pack path:** `art/ui/splash_logo.png (composited)`
- **prompt:** a small game emblem of a bronze globe showing the Mediterranean Sea in blue with the coasts of Italy, Greece, North Africa and Spain around it in raised bronze, wrapped in a golden laurel wreath, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=44`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6402`, `style=rd_pro__default`, `target=[44, 44]`, `width=44`
- **Note:** The emblem for the publisher splash, the homage to the old logo's globe. The lettering ('Dan' / 'Heskett' / 'Presents...') is rendered locally in C059 Bold as 1-bit white with red shading, because generated text garbles; only this 44x44 emblem is generated, on rd_pro__default like every other screen piece, then composited by tools/splashlogo.py into the 320x84 transparent field with drawn coins and sparkles. run02 (2026-09-05): the eagle is dropped (it read too close to a Reich eagle); the globe now shows the Mediterranean, so the emblem is the map of the game.

### splash_title  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (256x164, seed 6501)
- **Pack path:** `art/ui/splash_title.png`
- **prompt:** a golden Roman legionary eagle standard with spread wings on a tall decorated pole, a laurel wreath ring below the eagle, an engraved SPQR plate on the shaft, standing against a deep royal purple field, wide empty space across the top third, no frame, no border
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=164`, `raw_only=true`, `seed=6501`, `style=rd_pro__default`, `target=[256, 164]`, `width=256`
- **Note:** Title screen regenerated without its painted border (2026-09-06); screen route, rd_pro__default at the 256x164 size the shell draws it at.

### title_battle  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (256x177, seed 6611)
- **Pack path:** `art/ui/title_battle.png`
- **prompt:** Roman legionaries with red shields and an eagle standard clash with barbarian warriors with round wooden shields and axes on a grass field under a pale sky.
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=177`, `raw_only=true`, `reference_image_paths=["build/art/title_battle_left/run02/01_raw.png"]`, `seed=6611`, `style=rd_pro__default`, `target=[256, 177]`, `width=256`
- **Note:** Title screen battle (2026-09-13): fades in behind the title words, replacing the purple field, on the modern title screen. Screen route, rd_pro__default at the title's 256x164 so the pixels match; the top third kept calm for the words. run02: closer to the action at Dan's request (run01 was a distant hillside view). run03: 256x177 (3x = 768x531 on the 800x532 screen), short prompt, the run02 left half (title_battle_left) as reference. INSTALLED: run01 (seed 6601, the hillside prompt; its settings are in build/art/title_battle/run01/job.json and in this file at commit 87e8b59). run02 and run03 were not used.

### title_battle_hd

- **Engine:** Retro Diffusion rd_plus__environment (400x266, seed 8123)
- **Pack path:** `art/ui/title_battle_hd.png`
- **prompt:** a battlefield where two opposing armies crash together, on the left Roman legionaries with red rectangular shields, bronze helmets and short swords charging right, on the right wild barbarian warriors with long hair, beards, round wooden shields, axes and spears charging left, the two sides meeting in a violent clash in the middle, golden eagle standard raised above the Romans, open grassy field with dust in the air, a pale sky across the top third of the picture, the scene filling the whole picture edge to edge, no city, no buildings, no text, no frame, no border
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=266`, `raw_only=true`, `seed=8123`, `style=rd_plus__environment`, `target=[400, 266]`, `width=400`
- **Note:** Title battle test at 400x266 (2x fills the 800x532 screen); rd_plus__environment allows up to 512x512. Same prompt and seed as title_battle run02. run02: new seed, prompt reworked to show two opposing sides (run01 showed two Roman lines).

### title_battle_left

- **Engine:** Retro Diffusion rd_pro__default (200x256, seed 6611)
- **Pack path:** `art/ui/title_battle_left.png`
- **prompt:** left half of a battle: Roman legionaries with red shields and an eagle standard charge right. Grass field, pale sky.
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=256`, `raw_only=true`, `reference_image_paths=["build/art/title_battle_left/run01/01_raw.png", "build/art/title_battle_right/run01/01_raw.png"]`, `seed=6611`, `style=rd_pro__default`, `target=[200, 256]`, `width=200`
- **Note:** Title battle left half (2026-09-13): two 200x256 halves on rd_pro__default seed 6611, joined side by side to 400x256 and drawn 2x on the 800x532 title screen; Dan's plan. run02: simpler prompt, both run01 halves as reference images so the sky and grass match.

### title_battle_right

- **Engine:** Retro Diffusion rd_pro__default (200x256, seed 6611)
- **Pack path:** `art/ui/title_battle_right.png`
- **prompt:** right half of a battle: barbarian warriors with round wooden shields and axes charge left. Grass field, pale sky.
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=256`, `raw_only=true`, `reference_image_paths=["build/art/title_battle_left/run01/01_raw.png", "build/art/title_battle_right/run01/01_raw.png"]`, `seed=6611`, `style=rd_pro__default`, `target=[200, 256]`, `width=200`
- **Note:** Title battle right half (2026-09-13): two 200x256 halves on rd_pro__default seed 6611, joined side by side to 400x256 and drawn 2x on the 800x532 title screen; Dan's plan. run02: simpler prompt, both run01 halves as reference images so the sky and grass match.

### title_eagle  — INSTALLED

- **Engine:** Retro Diffusion rd_pro__default (96x164, seed 6602)
- **Pack path:** `art/ui/title_eagle.png`
- **prompt:** a golden Roman legionary eagle standard with spread wings on a tall decorated pole, a laurel wreath ring below the eagle, an engraved SPQR plate on the shaft, the whole standard upright and complete from the eagle's wingtips down to the foot of the pole, isolated, nothing touching or cut off by any edge, solid magenta background
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `height=164`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6602`, `style=rd_pro__default`, `target=[96, 164]`, `width=96`
- **Note:** Title screen eagle standard as a cut-out (2026-09-13), the same standard as splash_title, so the modern title can slide it left while the battle fades in. rd_pro__default at the title's height, magenta removed.

## Sprite batches (trees, rocks)

### africa_o96_rocks

- **Engine:** PixelLab create-1-direction-object (96 px, 8 items)
- **shared:** warm ochre sandstone rock, the whole rock inside the frame, seen from above at a slight angle, lit from the north-west with a shaded front face on the south side, small shadow to the south-east, pixel art, transparent background
- **batch 1:** rounded sandstone boulder, cracked, shaded south face · jagged red-brown crag with a sheer cliff face at the front · small mesa, flat top with a dark cliff face below it · tall sandstone pillar, wind-carved
- **batch 2:** broad rocky massif with a flat top, cliff face at the front · pile of broken sandstone and scree · low weathered boulder with sand drifted against it · split rock with a deep crevice, cliff face at the front
- **Note:** Africa rocks (2026-09-19). Output in art/primitives/africa/rocks.

### africa_o96_trees

- **Engine:** PixelLab create-1-direction-object (96 px, 8 items)
- **shared:** North African tree, round crown of dusty grey-green leaves with a visible trunk below it, the whole tree inside the frame, seen from above at a slight angle, small shadow to the south-east, pixel art, transparent background
- **batch 1:** olive tree, silvery round crown, twisted trunk · carob tree, dark round crown, trunk · argan tree, thorny round crown, short trunk · cork oak, round crown, thick trunk
- **batch 2:** young olive, lighter round crown · fig tree, broad round crown, trunk · tamarisk, feathery round crown, trunk · dark round crown, thick trunk
- **Note:** Africa trees (2026-09-19). Output in art/primitives/africa/trees.

### galliae_o96_rocks

- **Engine:** PixelLab create-1-direction-object (96 px, 8 items)
- **shared:** dark grey granite rock, seen from above at a slight angle, lit from the north-west with a shaded front face on the south side, small shadow to the south-east, pixel art, transparent background
- **batch 1:** rounded granite boulder, cracked, shaded south face · jagged crag with a sheer cliff face at the front · rock ledge, flat top with a dark cliff face below it · tall rock tooth with heather at its foot
- **batch 2:** broad rocky massif with moss and heather on top, cliff face at the front · pile of broken granite rocks and scree · low weathered boulder, moss on the top · split rock with a deep crevice, cliff face at the front
- **Note:** Galliae rocks (2026-09-19): the Italia o96_rocks call with the review-table wording. Output kept in art/primitives/galliae/rocks.

### galliae_o96_trees

- **Engine:** PixelLab create-1-direction-object (96 px, 8 items)
- **shared:** Northern broadleaf tree, round leafy crown with a visible trunk below it, seen from above at a slight angle, small shadow to the south-east, pixel art, transparent background
- **batch 1:** broad beech, round crown, short trunk · English oak, dark round crown, trunk · ash tree, lighter airy crown, trunk · hornbeam, dense round crown, trunk
- **batch 2:** old oak, wide crown, thick trunk · young beech, lighter round crown · lime tree, full round crown, trunk · dark round crown, thick trunk
- **Note:** Galliae trees (2026-09-19): the Italia o96_trees call with the review-table wording. Output kept in art/primitives/galliae/trees.

### galliae_o96_trees_autumn

- **Engine:** PixelLab create-1-direction-object (96 px, 8 items)
- **shared:** Northern broadleaf tree in autumn, round leafy crown of orange, russet and gold leaves with a visible trunk below it, seen from above at a slight angle, small shadow to the south-east, pixel art, transparent background
- **batch 1:** copper beech, round crown, short trunk · English oak, russet round crown, trunk · ash tree, yellow airy crown, trunk · hornbeam, orange dense round crown, trunk
- **batch 2:** old oak, wide russet crown, thick trunk · young beech, golden round crown · lime tree, gold full round crown, trunk · dark red round crown, thick trunk
- **Note:** Galliae autumn trees (2026-09-19), replacing the green run (kept in art/primitives/galliae/trees_green; job galliae_o96_trees.json). Output in art/primitives/galliae/trees.

### oriens_o96_rocks

- **Engine:** PixelLab create-1-direction-object (96 px, 8 items)
- **shared:** pale grey limestone rock, the whole rock inside the frame, seen from above at a slight angle, lit from the north-west with a shaded front face on the south side, small shadow to the south-east, pixel art, transparent background
- **batch 1:** rounded limestone boulder, cracked, shaded south face · jagged crag with a sheer cliff face at the front · rock ledge, flat top with a dark cliff face below it · tall rock tooth with snow on its tip
- **batch 2:** broad rocky massif with a snow cap, cliff face at the front · pile of broken limestone rocks and scree · low weathered boulder, dry scrub on the top · split rock with a deep crevice, cliff face at the front
- **Note:** Oriens rocks (2026-09-19). Output in art/primitives/oriens/rocks.

### oriens_o96_trees

- **Engine:** PixelLab create-1-direction-object (96 px, 8 items)
- **shared:** Levantine tree, round crown of dark green leaves with a visible trunk below it, the whole tree inside the frame, seen from above at a slight angle, small shadow to the south-east, pixel art, transparent background
- **batch 1:** cedar of Lebanon, broad layered round crown, trunk · oriental plane tree, wide round crown, pale trunk · walnut tree, dark round crown, trunk · pistachio tree, small round crown, short trunk
- **batch 2:** old oak, wide round crown, thick trunk · fig tree, broad round crown, trunk · young plane tree, lighter round crown · dark round crown, thick trunk
- **Note:** Oriens trees (2026-09-19). Output in art/primitives/oriens/trees.

## Troops

### antaei

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 5501)
- **prompt:** a hulking earth-giant, hunched, skin caked in soil and moss, enormous hands, regenerating wounds glowing faintly
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=5501`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Prompt as written in ART-WORKLIST. Neutral stance so the attack is the travel the animation spends. The troop style appends the full-length profile, game-sprite and magenta-background clauses. Animate at 96 without padding, the route of the last four troops.

### antaei_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 5502)
- **prompt:** both enormous fists rise together from his sides up above his head, then slam straight down in front of him to the ground with the shoulders following, then lift back to where they started, both feet stay planted, the hunched back stays hunched
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/antaei/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=5502`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Animated at 96 with no padding from the approved run01 still, the route of the last troops. Motion line: path, two endpoints, then what stays still. Installed 2026-09-04 as the grey-granite recolour of run01: every opaque pixel except the pale glowing wounds (s<0.25, v>0.6) set to hue 0.60, saturation x0.25, value x1.2+0.08, done locally, because the generated moss-and-soil greens vanished against the grass.

### baleares  — INSTALLED

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6154)
- **Pack path:** `art/troops/baleares_00.png`
- **prompt:** a Balearic slinger, a wiry grown man with a short black beard, a short coarse brown tunic over one shoulder leaving the right arm bare, a wide leather belt, a bulging hide pouch of sling stones at his left hip, rope sandals, a braided leather sling held out from his right hand with a stone in its cradle and the long cord hanging in a loop below it, a red cloth headband, standing squarely facing right
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6154`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Reworked 2026-09-20 in the adversarial troop review: the 2026-09-05 sprite read as a naked child -- 62px tall and 908 opaque pixels against the velites' 80px and 1959, the smallest human in the game, and installed scaled to 80% on top of that. The old 28-word prompt was the cause: 'a Balearic slinger, bare-chested and wiry, a leather sling hanging loose from his right hand at his side, a pouch of stones at his hip, a cloth headband' -- 'bare-chested and wiry' drew the boy and 'hanging loose at his side' drew a sling of three pixels. Rewritten to the length the good troops use (sagittarii 68 words, numidae 75): clothed, adult, the sling loaded and held out so it has a shape. Install at 100%, not 80%.

### baleares_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6158)
- **Pack path:** `art/troops/baleares_00..03.png`
- **prompt:** he whirls the sling once above his head and releases the stone forward to the right, the way he faces, ending with his right arm straight out in front of him and the empty sling cord hanging slack and clearly drawn from his hand, both feet stay planted, no motion blur and no streaks
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/baleares/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6158`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Re-run 2026-09-20 on the reworked still (baleares.json seed 6154). The 2026-09-05 loop animated a still scaled to 80% for motion room; the new still is 77px tall with its feet on row 88, the same rows velites and sagittarii sit on, so it is animated at full size with no padding like them. Motion line unchanged: path, two endpoints, then what stays still. run02: run01 lost the sling entirely on the follow-through frame, so the release read as a punch; the motion line now says the empty sling stays in his right hand, seed incremented. run03: run02 kept the sling but drew it as a violet motion blur -- 95 blue-violet pixels in frame 3 against 12 in the whole of run01, a colour in no other troop -- and whirled backwards over the left shoulder; the line now names the release direction, asks for the cord drawn slack and clear, and forbids blur and streaks.

### coloni

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 5401)
- **prompt:** a ragged Roman tenant farmer in a torn dirty tunic, barefoot, holding a wooden pitchfork, stooped and unarmoured
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=5401`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The peasants slot. Prompt as written in ART-WORKLIST. Neutral stance, the tool at rest, so the swing is the travel the animation spends. The troop style appends the full-length profile, game-sprite and magenta-background clauses. Animate at 96 without padding, the Hastati, Velites and Tirones route.

### coloni_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 5402)
- **prompt:** the pitchfork jabs forward from level at his side to full reach out in front of him at waist height, then draws back to where it started, both feet stay planted
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/coloni/run01/01_scaled80.png`, `raw_only=true`, `return_spritesheet=true`, `seed=5402`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Animated at 96 with no padding from the run01 still scaled to 80% through the API's k-centroid tool (black flatten, alpha from coverage) and re-centred with the feet on row 88, because the still already holds the fork levelled with the tines 6 px from the right edge. Motion line: path, two endpoints, then what stays still.

### cyclopes

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 5804)
- **prompt:** a huge one-eyed cave giant, single central eye, bare muscled torso, a heavy leather smith apron, a massive hammer resting across his right shoulder with the shaft in his right hand and the single hammer head behind him
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=5804`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** run02 (hammer at rest on the ground) animated into a shoulder-high lift with a doubled hammer head; the shouldered still (rdgen reused the freed run01 number after run01 was deleted) shoulders the hammer with the head behind him so the swing is one arc up, over and down, and takes a new seed. The troop style appends the full-length profile, game-sprite and magenta-background clauses. Animate at 96 without padding.

### cyclopes_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 5805)
- **prompt:** he takes the shaft in both hands, swings the hammer up off his shoulder, over his head in a wide arc, and strikes it down to the ground out in front of him to the right, then lifts it back onto his shoulder, one hammer head only, both feet stay planted, the apron stays on
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/cyclopes/run01/01_scaled90.png`, `raw_only=true`, `return_spritesheet=true`, `seed=5805`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Animated at 96 with no padding from the shouldered still (cyclopes/run01, seed 5804) scaled to 90% through the API's k-centroid tool (black flatten, alpha from coverage) and re-centred with the feet on row 88, because the still filled the cell top to bottom. Two hands named so the swing is the whole body; 'one hammer head only' after the first animation doubled it. cyclopes_attack/run03 was run by mistake on the ground-rest still.

### dracones

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 5281)
- **prompt:** a great scaled dragon standing on four clawed feet, wings spread wide, its long neck held back in a curve and its jaws closed, dark green scales with a pale belly, the figure huge and filling the whole square from edge to edge
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=5281`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The dragons slot. Neutral stance per the pipeline: neck drawn back and jaws closed, so the lunge is the travel the animation spends. The troop style appends the full-length profile, game-sprite and magenta-background clauses. 2026-09-07: re-posed to fill the frame like the Gigantes (Dan).

### dracones_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 5282)
- **Pack path:** `art/troops/dracones_00..03.png`
- **prompt:** the head and neck drive forward from drawn back to full reach out in front of the body, the jaws opening wide as they go, then the neck draws back to where it began and the jaws close, the wings stay spread and the clawed feet stay planted
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/dracones/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=5282`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** 2026-09-07: job copied whole from silvani_attack (96, no padding, four frames, expansion off); the old padded 128 route dropped. Still is dracones run02.

### druidae

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6121)
- **prompt:** a Celtic druid in a long white robe with an oak-leaf wreath, a gnarled oak staff held upright at rest in his right hand, a golden sickle hanging from his belt
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6121`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05, prompt rewritten from the worklist row into a neutral stance with the weapon at rest, so the attack is the animation's travel. Animate at 96 without padding.

### druidae_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6132)
- **prompt:** he swings the staff forward and up in his right hand until it points straight ahead to the right at shoulder height, its tip out in front of him, and a burst of pale golden light flares from the tip, then he swings it back down to upright at his side, both feet stay planted, the robe stays still
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/druidae/run01/01_scaled80.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6132`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05. Still scaled to 80% through the API's k-centroid tool (black flatten, alpha from coverage), feet on row 88, for motion room. Animated at 96 with no padding. Motion line: path, two endpoints, then what stays still. run01 kept the staff vertical; run02 names the tip pointing straight ahead to the right, new seed.

### elephanti

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 8101)
- **prompt:** a war elephant of the Seleucid kings seen from the side facing right, a big grey-brown elephant with long white tusks and a red and gold caparison over its back, a small wooden fighting tower strapped on its back with two spearmen in bronze helmets and red tunics inside it, an Indian driver in a white tunic sitting on its neck with a goad, standing square with all four feet planted
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=8101`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The Elephanti still (2026-09-20). The settled troop route whole: the pack's own troop style at 96x96, background removed, colours named, no reference image -- the same shape as sagittarii.json.

### elephanti_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8111)
- **prompt:** charges forward and swings its tusks to the right, the tower and its spearmen riding with it, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=6`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/elephanti/run03/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8111`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** 2026-09-20: sagittarii_attack copied whole (six frames, expansion on, tag prompt); only the still (elephanti run03), seed and tag differ.

### empusae

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6111)
- **prompt:** a lean winged demon of Hecate with one bronze leg and one pale leg, bat wings folded close against its back, a long scythe held upright at rest in both hands, pale grey skin
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6111`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05, prompt rewritten from the worklist row into a neutral stance with the weapon at rest, so the attack is the animation's travel. Animate at 96 without padding.

### empusae_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6112)
- **prompt:** the scythe sweeps from upright in a wide arc down and across to full reach in front of it on the right at waist height, then swings back up to upright, the wings open once and fold again, both feet stay planted
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/empusae/run01/01_scaled80.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6112`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05. Still scaled to 80% through the API's k-centroid tool (black flatten, alpha from coverage), feet on row 88, for motion room. Animated at 96 with no padding. Motion line: path, two endpoints, then what stays still.

### equites

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6111)
- **prompt:** a Roman heavy cavalryman sitting on a big powerful standing warhorse with a thick neck and heavy hindquarters, all four hooves on the ground and its head up, the horse and rider large and filling the square, the rider broad-shouldered in a muscled cuirass and a crested helmet, an oval shield on his left arm, a lance held upright at rest in his right hand, his cloak hanging still
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6111`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05, prompt rewritten from the worklist row into a neutral stance with the weapon at rest, so the attack is the animation's travel. Animate at 96 without padding. 2026-09-07: re-run heavier and larger at Dan's request; the earlier loop was animated on an 80% scaled still (01_scaled80), which is why it came out small next to the Numidae. The new loop animates the raw still like numidae_attack.

### equites_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6112)
- **prompt:** the horse surges forward one stride to the right and the lance comes down from upright to level and drives forward to full reach in front of him, then the horse settles back and the lance lifts upright again, the shield stays on his arm
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/equites/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6112`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05. Still scaled to 80% through the API's k-centroid tool (black flatten, alpha from coverage), feet on row 88, for motion room. Animated at 96 with no padding. Motion line: path, two endpoints, then what stays still. 2026-09-07: animated on the raw run02 still at full size, no 80% scaling.

### fauni

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6145)
- **prompt:** a small woodland faun with goat legs and curling horns, shaggy brown pelt, a short thick gnarled wooden club held upright at rest in his right hand, a mischievous grin
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6145`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05. The 64 route (run01) animated with a doubled branch twice, so run02 generates and animates at 96 like the men and is then scaled to 66% through the API's k-centroid tool with one shared offset for the four frames, feet on row 88, which gives the small unit its two-thirds height another way. run03: the thin crooked branch was lost or doubled in every animation, so the still now gives him a short thick gnarled club, which survives animation the way the Ligures' axe and the Cyclopes' hammer did.

### fauni_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6146)
- **prompt:** the thick wooden club, the same single club the whole time, swings from upright down and across to the right until it is level at waist height with its head out in front of him, then swings back up to upright, the hooves stay planted
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/fauni/run03/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6146`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05. Animated at 96 with no padding from the 96 still (the 64 route doubled the branch twice); the frames are then scaled to 66% together and set with the feet on row 88. run03's branch shortened mid-swing; run04 names the full-length branch, new seed.

### furiae  — INSTALLED

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6210)
- **Pack path:** `art/troops/furiae_00.png`
- **prompt:** a winged Fury, a gaunt woman with ash-grey skin and hollow red eyes, a ragged black robe torn off at the knee with a pale grey underlayer showing at the hem and sleeves, dark feathered wings spread wide behind her, live snakes writhing in her hair, a burning torch with an orange flame held upright at rest in her right hand, hovering a little above the ground with her bare feet pointed down
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6210`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** First job file this troop has ever had: the shipped furiae sprite predates the job record, so its prompt was never written down. Made 2026-09-20 in the adversarial troop review, where the sprite read as a black blob at map size -- the dark wings, dark robe and unlit face merged into one silhouette. The rewrite gives her ash-grey skin, a pale underlayer at hem and sleeves and an orange torch flame, so the figure separates from its own wings. Troop route, the settings the good troops use (sagittarii, sarmatae): 96 px, figure, background removed, expansion off.

### furiae_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6211)
- **Pack path:** `art/troops/furiae_00..03.png`
- **prompt:** a wide overhead swing, the burning torch carried from upright behind her head in a long arc down and out to full reach in front of her to the right, the way she faces, then back to upright, the wings thrown back and spread as she lunges, hovering with the feet off the ground, smooth loop, no motion blur and no streaks
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/furiae/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6211`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** First job file for this loop: the shipped furiae frames predate the job record. Made 2026-09-20 on the reworked still (furiae.json seed 6210), the four-frame custom action the other troops use, expansion on. The motion line names the swing's direction and forbids blur -- the lesson from the baleares loop, which drew the released sling as a violet smear -- and says the feet stay off the ground, because she flies (abilities FLY|MAGIC).

### gigantes

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6232)
- **prompt:** a towering giant with wild hair and beard, bare massive chest and shoulders, standing on two thick human legs covered in green scaly skin, bare feet planted wide apart, holding a large boulder at rest against his hip in both hands, the figure huge and filling the square with a little space above his head
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6232`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05, prompt rewritten from the worklist row into a neutral stance with the weapon at rest, so the attack is the animation's travel. Animate at 96 without padding. 2026-09-07: re-posed to fill the frame; boulder at the hip for a sideways hurl (Dan). run03: two legs stated explicitly (run02 drew a serpent tail), headroom asked for.

### gigantes_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6232)
- **Pack path:** `art/troops/gigantes_00..03.png`
- **prompt:** he swings the boulder back to his left side in both hands, then hurls it sideways across his body to the right and the boulder flies off straight ahead to the right, his arms following through in front of him, then his arms drop back to his hip, both feet stay planted
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/gigantes/run03/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6232`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** 2026-09-07: job copied whole from silvani_attack (the approved release loop); sideways hurl so the giant keeps the frame (Dan). Still is gigantes run03.

### lares  — INSTALLED

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6178)
- **Pack path:** `art/troops/lares_00.png`
- **prompt:** a Roman household guardian spirit, a small winged genius hovering a little above the ground with his feet together and pointed down, two pale feathered wings spread wide behind him, a short white tunic with a gold border, a small bronze breastplate, a round bronze shield on his left arm and a short sword held upright in his right hand, faintly glowing
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6178`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Lares reworked 2026-09-06 from the Roman household shrine figure: the dancing Lar with rhyton and patera, glowing, full 96 troop route (no longer the 64 small-unit route). Earlier: a translucent ghost (run01, read badly on the map), then a child (run02). run04: the API's remove_bg left the whole magenta field on run03, seed incremented. run05: reworked as an armed guardian with a short sword (Dan): the dancing Lar of run04 read as a party boy, not a troop. run06: halo removed (Dan), seed incremented. Reworked 2026-09-20 in the adversarial troop review: the unit is the FLY slot (sprites, abilities FLY in game.json) and the sprite was a boy standing squarely on the ground -- the old prompt said so in as many words. The history reads ghost -> child -> dancing Lar -> armed guardian, each fix moving further from flight. Now a winged genius hovering, feet together and pointed down, wings spread wide, keeping the bronze armour and short sword that made him read as a troop.

### lares_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6179)
- **Pack path:** `art/troops/lares_00..05.png`
- **prompt:** short sword thrust forward to the right, the way he faces, hovering in place with the feet together and off the ground, the wings beating once and spreading wide again, smooth loop, no motion blur and no streaks
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=6`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/lares/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6179`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Lares loop (2026-09-06), the Sarmatae settings: custom action, six frames, prompt expansion on, tag prompt, the still untouched at 96. Re-run 2026-09-20 on the winged still (lares.json seed 6178). 'Feet planted' is gone -- the unit flies -- and the wings are given the beat; the blur clause is the lesson from the baleares loop, which drew the released sling as a violet smear.

### larvae

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6161)
- **prompt:** a walking human skeleton in a rotted Roman tunic, hollow eye sockets, bone-white, a rusted short sword held upright at rest in its right hand
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6161`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05, prompt rewritten from the worklist row into a neutral stance with the weapon at rest, so the attack is the animation's travel. Animate at 96 without padding.

### larvae_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6172)
- **prompt:** the rusted brown short sword swings from upright down and forward to full reach in front of it at chest height, then back up to upright, the same single rusted brown sword the whole time held in the right hand, both feet stay planted, the skull keeps facing right
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/larvae/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6172`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05. Animated at 96 with no padding. Motion line: path, two endpoints, then what stays still. run01 sent the blade behind the head in frame 2 and turned it purple in frame 3; run02 names the single rusted brown sword and takes a new seed.

### lemures

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6181)
- **prompt:** a shambling rotted corpse in bone-white grave wrappings, arms hanging at its sides, hunched and slow, pale grey flesh, not green
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6181`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05, prompt rewritten from the worklist row into a neutral stance with the weapon at rest, so the attack is the animation's travel. Animate at 96 without padding.

### lemures_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6182)
- **prompt:** it lurches one step forward to the right and both arms swing up and claw forward at chest height, then it settles back with the arms hanging, the hunch stays
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/lemures/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6182`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Batch of 2026-09-05. Animated at 96 with no padding. Motion line: path, two endpoints, then what stays still.

### ligures

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed -)
- **prompt:** a stocky Ligurian mountain tribesman with a thick beard, a fur cloak over a leather cuirass, a heavy long-handled axe held upright in both hands
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Pipeline test on a new unit. Neutral stance, axe upright: the upright axe is the travel the animation spends.

### ligures_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed -)
- **prompt:** the axe head travels from upright above his shoulder over and down through a wide arc until it is level with his knees out in front of him, both feet stay planted, the fur cloak stays on his shoulders
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/ligures/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`

### manes

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 4471)
- **prompt:** a translucent ancestral shade, a hollow hooded robed figure with no legs, its robe trailing into vapour, faintly glowing, hunched forward with both arms reaching out in front at chest height, long bony fingers ending in glowing claws
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=4471`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Ancestral shade, the ghosts slot. Pose copied from the old ghost's first frame: hunched, both arms out in front at chest height, long clawed hands, so the attack animation has arms to sweep up and slash down with. New seed. The troop style appends the full-length profile, game-sprite and magenta-background clauses.

### manes_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 4472)
- **prompt:** both arms sweep up and back over the hood with the clawed hands spread wide as the shade rears up taller, then both arms slash down and forward to full reach in front of it with the claws spread and a burst of pale fire streaming out from the claw tips to the right, the robe and vapour trailing behind, the hollow face keeps facing right
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=128`, `input_image_keep_alpha=true`, `input_image_path=build/art/manes/run02/01_raw.png`, `pad_to=128`, `raw_only=true`, `return_spritesheet=true`, `seed=4472`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** The old ghost's attack, frame for frame: arms draw back, sweep up and back over the hood as the shade rears up, then both claws slash down and forward to full reach with a burst of flame streaming out ahead of them. Padded 96 -> 128 for motion room. Frames come back 128 and are downscaled to 96 with the API's k-centroid tool, not cropped.

### numidae

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 5902)
- **prompt:** a Numidian light horseman sitting bareback on a standing horse with all four hooves on the ground and its head up, no saddle and no bridle, only a rope around the horse's neck, the rider in a short plain sleeveless tunic with bare arms and legs, thick curly hair and a short beard, a small round hide shield on his left arm, a curved single-edged falcata sword held upright at rest in his right hand
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=5902`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** run01 (worklist prompt: leopard skin, galloping, hurling a javelin) was ahistorical, mid-gallop and mid-throw. run02 follows the sources: plain sleeveless tunic, bareback with only a neck rope, small hide shield, standing horse, a falcata held upright at rest so the charge and cut are the animation's travel. The troop style appends the full-length profile, game-sprite and magenta-background clauses. Animate at 96 without padding.

### numidae_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 5903)
- **prompt:** the horse surges forward one stride to the right and the falcata sweeps down and across from upright to full reach in front at shoulder height, then the horse settles back and the sword returns upright, the shield stays on his arm
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/numidae/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=5903`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Animated at 96 with no padding from the accepted run02 still. Motion line as recorded in ART-WORKLIST.

### sagittarii

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 7901)
- **prompt:** a Roman auxiliary archer of the sagittarii in a bronze Roman helmet with cheek guards and a red crest, a gold-bronze scale shirt over a red tunic, red leather boots, a quiver of arrows on his back, a curved composite bow held relaxed and upright at his side in his left hand with the string slack and no arrow on it, his right hand empty at his side
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7901`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** 2026-09-17 run05: run04's prompt with 'standing square with both feet planted' removed, so it ends as Silvani's does and the body turns to the right. Seed 7901, colours named, no reference image.

### sagittarii_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 7908)
- **prompt:** draws the bow and shoots an arrow to the right, feet planted, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=6`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/sagittarii/run05/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=7908`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** 2026-09-17 run05: sarmatae_attack copied whole (six frames, expansion on, tag prompt); only the still (sagittarii run05), seed and tag differ.

### sagittarii_draw

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 7901)
- **prompt:** a Roman auxiliary archer of the sagittarii in a bronze Roman helmet with cheek guards, a mail shirt over a red tunic, a quiver of arrows on his back, seen from the side facing right, his left arm holding the curved composite bow out straight ahead and his right hand pulling the dark bowstring all the way back to his cheek so the bow is bent deeply and the string makes a sharp V at the nocked arrow, the arrow level and pointing right, both feet planted
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/sagittarii_00.png"]`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7901`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** 2026-09-17: the key pose the shooting animation starts from -- bow at full draw, which is what three animation runs from the at-rest still could never invent. Same man and seed as sagittarii.json, with the pack sprite as a reference image.

### sagittarii_idle

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 7907)
- **prompt:** he stands at ease and breathes slowly, his chest and shoulders rising a little and his head turning slightly, the bow stays upright at his side in his left hand with its string, both feet stay planted, the helmet stays on his head and the quiver stays on his back
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/sagittarii/run04/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=7907`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** 2026-09-17: the idle pair's second frame -- strip frame 0 is the still itself, strip frame 1 is one frame of this loop, the figure moved a little. Same still and settings as sagittarii_attack; four frames, only one is used.

### sagittarii_slung

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 7901)
- **prompt:** a Roman auxiliary archer of the sagittarii in a bronze Roman helmet with cheek guards, a mail shirt over a red tunic, a quiver of arrows on his back, his curved composite bow slung over his right shoulder on its dark bowstring with both hands resting empty at his sides, standing square with both feet planted
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/troops/sagittarii_00.png"]`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=7901`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** 2026-09-17: the idle key pose for frames 0-1 of an eight-frame strip. Same man as art/troops/sagittarii_00.png, which goes in as a reference image; the seed is the still's own 7901. The bow rides on his shoulder here so the draw in sagittarii_draw is a real change of pose.

### sarmatae

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6003)
- **prompt:** a Sarmatian steppe warrior in full scale armour of overlapping plates, conical helmet, fur-trimmed cloak, a very long straight two-edged Sarmatian cavalry sword resting across his right shoulder with the hilt in his right hand and the blade behind his head, his left hand empty at his side
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6003`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** run01 (worklist prompt, long lance) animated into a lance that changed hands and swung backward; run02 replaces the lance with the long straight Sarmatian sword shouldered like the Cyclopes' hammer, hilt in the right hand, so the attack is a one-handed overhead cut. The troop style appends the full-length profile, game-sprite and magenta-background clauses. Animate at 96 without padding.

### sarmatae_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6006)
- **prompt:** one-handed overhead long sword swing to the right, feet planted, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=6`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/sarmatae/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6006`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Animated at 96 with no padding from the accepted run02 still (long sword shouldered, hilt in the right hand). Motion line as recorded in ART-WORKLIST: one-handed cut up off the shoulder, over the head and down to the right. run03 (2026-09-05): retried with the villain-loop lessons: prompt expansion left on, a short tag-form prompt, seed incremented. Frame count as the pack declares for troops. run at six frames (the API guide's count for a single action; the pack allows sixteen): the four-frame retry ended with an empty hand.

### silvani

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 5702)
- **prompt:** a forest spirit archer in bark-toned robes with a leaf-patterned cloak and an antlered circlet, a longbow held relaxed and upright at his side in his left hand with the string slack and no arrow on it, his right hand empty at his side
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=5702`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Prompt as written in ART-WORKLIST drew the bow already at full draw (run01), so the still now asks for the bow relaxed, upright at his side, unstrung, nothing drawn: the draw and the loose are the animation's travel. The troop style appends the full-length profile, game-sprite and magenta-background clauses. Animate at 96 without padding.

### silvani_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 5703)
- **prompt:** he raises the longbow, nocks an arrow, draws the string back to his cheek and looses the arrow straight ahead to the right, then the bow arm lowers back to his side, both feet stay planted, the cloak stays on his shoulders
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/silvani/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=5703`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Animated at 96 with no padding from the accepted run02 still (bow relaxed, unstrung). Motion line as recorded in ART-WORKLIST: raise, nock, draw to the cheek, loose to the right, lower.

### striges

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 5604)
- **prompt:** a strix, a vampire woman in the shape of an owl: the hunched body and barred brown feathers of a great owl with a ruff of pale feathers at the neck, but a gaunt pale human woman's face where the owl's face would be, black eyes and a lipless mouth, dark hair among the head feathers, feathered wings half folded at her sides rather than spread, long hooked talons wet with blood, hunched and facing right, nothing under her feet
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=5604`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Prompt as written in ART-WORKLIST. Neutral stance so the attack is the travel the animation spends. The troop style appends the full-length profile, game-sprite and magenta-background clauses. Animate at 96 without padding. Reworked 2026-09-20 in the adversarial troop review: the 2026-09-05 sprite had pink-mauve bat wings on a 56px body and read as an imp, not the screech-owl the name and the lore promise. 'Membranous wings' was the fault -- replaced by broad feathered owl wings and barred brown plumage, and the body told to be the size of a man so it fills the tile like the recent troops. run02: run01 dropped the human face entirely and drew a plain great horned owl, wings spread the full 96 px -- wildlife, not a vampire, and too wide for its neighbours on the grid. The face is now named first and the wings told to stay half folded. run03: run02 got the face and the folded wings right but baked a brown ellipse of bare ground under the talons -- 'standing on bare ground' in the prompt. That clause is gone and the ground is forbidden outright; everything else is run02 unchanged.

### striges_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 5606)
- **prompt:** it lunges forward to the right, the way it faces, with both taloned feet thrust out in front of it, raking downward, the wings beating back behind it, then it draws back to where it started, the head stays at the same height above the ground the whole time and the pale human face keeps facing right, smooth loop, no motion blur and no streaks
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/striges/run03/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=5606`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Re-run 2026-09-20 on the reworked still (striges.json seed 5604, run03). The 2026-09-05 loop animated a still scaled to 66% because that one spanned 94 of 96 columns; the new still is 77x64 with its wings folded, so it is animated at full size with no padding like the recent troops. The blur clause is the lesson from the baleares loop. run02: run01 pounced well but dropped the whole body nearly 20px on the last frame (top edge rows 10, 11, 10, 28), so the creature bobs against its cell; the line now holds the head at one height and says draw back rather than spring.

### tirones

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 5312)
- **prompt:** a young Roman recruit, an ordinary young man of average build with adult proportions, neither slight nor burly, in a plain undyed wool tunic belted at the waist, a simple leather cap, a small round wooden shield with an iron boss on his left arm, a gladius short sword held upright in his right hand, standing stiffly at attention
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=5312`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** The militia slot: a raw recruit, started over 2026-09-04. Runs 01-02 ('raw recruit', 'simple' kit) drew a boyish waif; run03 ('farm hand's build, stocky and broad shouldered') overshot into a bruiser. run04 asks for an ordinary young man of average build with adult proportions. Neutral stance, the gladius upright at rest. Animate at 96 without padding, the Hastati and Velites route, so the frames keep the still's size.

### tirones_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 5314)
- **prompt:** the blade starts low on his left beside the shield and sweeps across in front of him toward the right, the way he faces, ending with his arm straight out to the right at chest height and the point toward the right edge, then comes back to where it started, the shield stays raised in front of his chest, both feet stay planted
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/tirones/run04/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=5314`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** The Hastati and Velites route: rd_advanced_animation__custom_action at 96x96 with no padding, so the frames keep the still's size (the padded 128 route shrank the figure to three quarters in runs 01-02). Path with two named endpoints, then what stays still. Input is the accepted run04 still. run04: run03 swung the blade across to the left, so the direction is named as the way he faces, toward the right edge, seed incremented.

### velites

- **Engine:** Retro Diffusion user__glory_of_rome_troops_bac676cd (96x96, seed 6202)
- **prompt:** a lean Roman velite skirmisher in a wolfskin headdress over a helmet, a small round parma shield on his left arm, a single light javelin with a small iron tip held upright at rest in his right hand, standing square with both feet planted, the figure large and filling the square
- **Settings:** `bypass_prompt_expansion=true`, `figure=true`, `height=96`, `raw_only=true`, `remove_bg=true`, `return_non_bg_removed=true`, `seed=6202`, `style=user__glory_of_rome_troops_bac676cd`, `target=[96, 96]`, `width=96`
- **Note:** Light skirmisher. Neutral stance, javelin upright: the vendor's rule is that a neutral pose gives the best character results, and the upright javelin is the travel the animation will spend. 2026-09-07: re-posed for a throw at Dan's request. 2026-09-07: back to the at-rest pose per ART-PIPELINE (the upright javelin is the travel the loop spends).

### velites_attack

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 6216)
- **Pack path:** `art/troops/velites_00..03.png`
- **prompt:** he raises the javelin from upright at his side up to his shoulder and draws his right arm back behind his head, then thrusts his right arm forward to full reach in front of him at shoulder height with the same single javelin still gripped in his hand and pointing straight ahead to the right, the javelin never leaves his hand, then the arm draws back to his side, both feet stay planted, the round shield stays on his left arm the whole time, the wolfskin stays on his head
- **Settings:** `bypass_prompt_expansion=true`, `figure=false`, `frames_duration=4`, `height=96`, `input_image_keep_alpha=true`, `input_image_path=build/art/velites/run03/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=6216`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** 2026-09-07: job copied whole from silvani_attack (the approved archer release loop); only the still, seed and motion sentence differ. Still is velites run03, at rest, javelin upright. run06: javelin kept in hand on every frame at Dan's order; nothing else changed.

## Villains

### alaric

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8201)
- **Pack path:** `art/villains/alaric_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Visigoth king in looted Roman armour over furs, an iron crown, heavy broadsword hilt at his shoulder, a sacked Roman street burning behind him
- **Settings:** `_animation=snarling face, teeth bared, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8201`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off).

### alaric_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8301)
- **Pack path:** `art/villains/alaric_00..07.png`
- **prompt:** snarling face, teeth bared, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/alaric/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8301`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif.

### arminius

- **Engine:** Retro Diffusion rd_pro__default (104x104, seed 9211)
- **Pack path:** `art/villains/arminius_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Cheruscan chieftain in a bearskin over Roman mail, a wolf-skull helmet, dark forest and mist behind him
- **Settings:** `_animation=sneering face, lip curled, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=104`, `raw_only=true`, `seed=9211`, `style=rd_pro__default`, `target=[104, 104]`, `width=104`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). 2026-09-07: Dan ordered the still generated at 128x128 and centre-cropped to 96x96 to remove the frame the engine draws at 96 (tools/cropcentre.py). Then 104x104 so the loop can be animated at full size and every frame centre-cropped to 96.

### arminius_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9312)
- **Pack path:** `art/villains/arminius_00..07.png`
- **prompt:** sneering face, lip curled, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/arminius/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9312`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. retry after inference_failed (no charge), seed incremented. 2026-09-07: loop run at 128 on the frame-free 128 still (run02); each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for villains).

### attila

- **Engine:** Retro Diffusion rd_pro__default (104x104, seed 9322)
- **Pack path:** `art/villains/attila_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of the Hun warlord, wiry and fierce with a thin braided beard, lamellar armour and fur, a horsehair standard and a burning horizon behind him
- **Settings:** `_animation=cruel grinning face, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=104`, `raw_only=true`, `seed=9322`, `style=rd_pro__default`, `target=[104, 104]`, `width=104`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). Re-run 2026-09-07: run01 drew a full grey frame; references dropped, edge-to-edge tail added, seed incremented. run02: thin 2px frame; seed incremented. 2026-09-07: Dan ordered the still generated at 128x128 and centre-cropped to 96x96 to remove the frame the engine draws at 96 (tools/cropcentre.py). Then 104x104 so the loop can be animated at full size and every frame centre-cropped to 96.

### attila_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9322)
- **Pack path:** `art/villains/attila_00..07.png`
- **prompt:** cruel grinning face, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/attila/run04/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9322`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. retry after inference_failed (no charge), seed incremented. 2026-09-07: loop run at 128 on the frame-free 128 still (run04); each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for villains).

### boudica

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8231)
- **Pack path:** `art/villains/boudica_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a tall Iceni warrior queen with long red hair, a heavy torc at her throat, a checked cloak, a burning Roman town behind her
- **Settings:** `_animation=shouting face, mouth open in fury, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8231`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off).

### boudica_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8331)
- **Pack path:** `art/villains/boudica_00..07.png`
- **prompt:** shouting face, mouth open in fury, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/boudica/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8331`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif.

### brennus

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8242)
- **Pack path:** `art/villains/brennus_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Gallic warchief with a long drooping fair moustache and thick fair hair swept back stiffly from the forehead, bare-chested with a heavy gold torc, the Capitoline hill under a red sky behind him
- **Settings:** `_animation=roaring laughing face, head back, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8242`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). run02: run01 read 'lime-spiked' as green mohawk hair; the hair is now fair, swept back stiffly, seed incremented.

### brennus_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8321)
- **Pack path:** `art/villains/brennus_00..07.png`
- **prompt:** laughing face, head thrown back, mouth wide, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/brennus/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8321`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. run02: redesigned motion at Dan's request ('laughing face, head thrown back, mouth wide, static background, smooth loop'), seed incremented.

### catiline

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8253)
- **Pack path:** `art/villains/catiline_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a disgraced Roman senator in a stained toga, gaunt and hollow-eyed, a dagger half-hidden in the folds, the Senate steps by torchlight behind him, the background scene continuing with detail right up to the top edge, the bottom edge, the left edge and the right edge, no plain strip or band of flat colour along any edge, no bar, no border, no frame
- **Settings:** `_animation=furtive sidelong glance, thin smile, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8253`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). Re-run 2026-09-07: the earlier still carried a flat colour bar on two or three sides; the scene is told to fill the square, seed incremented. Re-run: a flat strip remained along one edge; every edge named. Note 2026-09-07: reference images cause letterbox bars on villain stills; future re-runs drop them.

### catiline_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8352)
- **Pack path:** `art/villains/catiline_00..07.png`
- **prompt:** furtive sidelong glance, thin smile, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/catiline/run03/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8352`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. Re-run from the bar-free still run03.

### civilis

- **Engine:** Retro Diffusion rd_pro__default (128x128, seed 9361)
- **Pack path:** `art/villains/civilis_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Batavian auxiliary commander, one eye missing, Roman mail over Germanic trousers, long blond hair, a Rhine fort in flames behind him, the scene filling the whole square edge to edge, no bar, no border, no frame
- **Settings:** `_animation=scowling face, teeth gritted, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=128`, `raw_only=true`, `seed=9361`, `style=rd_pro__default`, `target=[128, 128]`, `width=128`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). Re-run 2026-09-07: run01 drew a full grey frame; references dropped, edge-to-edge tail added, seed incremented. 2026-09-07: 128 route (ART-PIPELINE villains): still at 128, loop at 128, each frame centre-cropped to 96.

### civilis_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9361)
- **Pack path:** `art/villains/civilis_00..07.png`
- **prompt:** scowling face, teeth gritted, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/civilis/run03/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9361`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. 2026-09-07: loop at 128 on build/art/civilis/run03; frames centre-cropped to 96 after.

### gildo

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8272)
- **Pack path:** `art/villains/gildo_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Moorish count in flowing white desert robes over Roman officer armour, dark-skinned and imposing, grain ships and a harbour behind him, the scene filling the whole square edge to edge, no bar, no border, no frame
- **Settings:** `_animation=contemptuous face, chin raised, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8272`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). Re-run 2026-09-07: the earlier still carried a flat colour bar on two or three sides; the scene is told to fill the square, seed incremented. Note 2026-09-07: reference images cause letterbox bars on villain stills; future re-runs drop them.

### gildo_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8372)
- **Pack path:** `art/villains/gildo_00..07.png`
- **prompt:** contemptuous face, chin raised, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/gildo/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8372`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. Re-run from the bar-free still run02.

### hannibal

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8101)
- **Pack path:** `art/villains/hannibal_00.png (frame 0 of 4)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Carthaginian general in a crested Punic helmet, one eye scarred and blind, purple cloak, snowy Alpine peaks and an elephant behind him
- **Settings:** `_animation=narrows his good eye and sets his jaw, then a slow knowing smile`, `bypass_prompt_expansion=true`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8101`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait, the first on this route (2026-09-05). Villains are opaque head-and-shoulders portraits drawn as faces in the contract view, HUD chip and puzzle grid, not sprites; the earlier worklist rows described combat sprites and were rewritten. Same engine as the class portraits (rd_pro__default) at the 96x96 tile size the views draw them in, opaque, the four class portraits as references so the villains render in the same hand. The four frames come from the custom-action engine on this still with the row's facial loop.

### hannibal_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8381)
- **Pack path:** `art/villains/hannibal_00..07.png`
- **prompt:** snarling face, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/hannibal/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8381`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif.

### jugurtha

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8284)
- **Pack path:** `art/villains/jugurtha_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Numidian king in an ornate gold circlet and rich embroidered robes over a cuirass, arms folded, imperious, a desert palace behind him, the background scene continuing with detail right up to the top edge, the bottom edge, the left edge and the right edge, no plain strip or band of flat colour along any edge, no bar, no border, no frame
- **Settings:** `_animation=sneering face, brow raised, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `seed=8284`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). Re-run 2026-09-07: the earlier still carried a flat colour bar on two or three sides; the scene is told to fill the square, seed incremented. Re-run at Dan's request, seed incremented. Re-run: a flat strip remained along one edge; every edge named. run05: NO reference images. Measured 2026-09-07: the four class portraits (192x204) as references letterbox the 96x96 still with flat bars; the same prompt and seed without them fills the frame.

### jugurtha_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8392)
- **Pack path:** `art/villains/jugurtha_00..07.png`
- **prompt:** sneering face, brow raised, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/jugurtha/run05/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8392`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. Re-run from the bar-free still run05.

### mithridates

- **Engine:** Retro Diffusion rd_pro__default (104x104, seed 9291)
- **Pack path:** `art/villains/mithridates_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of an eastern king in a Persian tiara and richly patterned robes, holding a small phial of poison, a Pontic mountain fortress behind him
- **Settings:** `_animation=thin poisoner's smile, phial lifted, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=104`, `raw_only=true`, `seed=9291`, `style=rd_pro__default`, `target=[104, 104]`, `width=104`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). 2026-09-07: Dan ordered the still generated at 128x128 and centre-cropped to 96x96 to remove the frame the engine draws at 96 (tools/cropcentre.py). Then 104x104 so the loop can be animated at full size and every frame centre-cropped to 96.

### mithridates_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9401)
- **Pack path:** `art/villains/mithridates_00..07.png`
- **prompt:** thin poisoner's smile, phial lifted, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/mithridates/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9401`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. 2026-09-07: loop run at 128 on the frame-free 128 still (run02); each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for villains).

### pyrrhus

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8301)
- **Pack path:** `art/villains/pyrrhus_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Hellenistic king in a plumed Corinthian helmet and gilded muscled cuirass, a phalanx of sarissas behind him
- **Settings:** `_animation=grimacing face, jaw clenched, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8301`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off).

### pyrrhus_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8311)
- **Pack path:** `art/villains/pyrrhus_00..07.png`
- **prompt:** head turning slowly from left to right and back, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/pyrrhus/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8311`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. run02: redesigned motion at Dan's request ('head turning slowly from left to right and back, static background, smooth loop'), seed incremented.

### shapur

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8311)
- **Pack path:** `art/villains/shapur_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Sasanian shah in a towering jewelled korymbos crown and cataphract scale armour, regal, a Persian palace of gold and blue behind him
- **Settings:** `_animation=cold regal stare, lips tightening, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8311`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off).

### shapur_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8421)
- **Pack path:** `art/villains/shapur_00..07.png`
- **prompt:** cold regal stare, lips tightening, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/shapur/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8421`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif.

### spartacus

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8321)
- **Pack path:** `art/villains/spartacus_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Thracian gladiator, bare-chested and scarred, a gladiator helmet with a grille visor pushed up, the arena stands behind him
- **Settings:** `_animation=roaring face, teeth bared, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8321`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off).

### spartacus_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8431)
- **Pack path:** `art/villains/spartacus_00..07.png`
- **prompt:** roaring face, teeth bared, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/spartacus/run01/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8431`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif.

### tacfarinas

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8332)
- **Pack path:** `art/villains/tacfarinas_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Numidian deserter chieftain in a Roman military cloak over desert robes, sun-darkened, javelins across his back, dunes and a Roman outpost behind him, the scene filling the whole square edge to edge, no bar, no border, no frame
- **Settings:** `_animation=squinting face, hard grin, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8332`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). Re-run 2026-09-07: the earlier still carried a flat colour bar on two or three sides; the scene is told to fill the square, seed incremented. Note 2026-09-07: reference images cause letterbox bars on villain stills; future re-runs drop them.

### tacfarinas_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8442)
- **Pack path:** `art/villains/tacfarinas_00..07.png`
- **prompt:** squinting face, hard grin, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/tacfarinas/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8442`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. Re-run from the bar-free still run02.

### vercingetorix

- **Engine:** Retro Diffusion rd_pro__default (104x104, seed 9341)
- **Pack path:** `art/villains/vercingetorix_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Gallic king in a horned helmet and mail shirt, long moustache, a hilltop oppidum with a Roman siege wall behind him
- **Settings:** `_animation=glowering face, moustache bristling, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=104`, `raw_only=true`, `seed=9341`, `style=rd_pro__default`, `target=[104, 104]`, `width=104`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). 2026-09-07: Dan ordered the still generated at 128x128 and centre-cropped to 96x96 to remove the frame the engine draws at 96 (tools/cropcentre.py). Then 104x104 so the loop can be animated at full size and every frame centre-cropped to 96.

### vercingetorix_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (128x128, seed 9451)
- **Pack path:** `art/villains/vercingetorix_00..07.png`
- **prompt:** glowering face, moustache bristling, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=128`, `input_image_keep_alpha=false`, `input_image_path=build/art/vercingetorix/run02/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=9451`, `style=rd_advanced_animation__custom_action`, `target=[128, 128]`, `width=128`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. 2026-09-07: loop run at 128 on the frame-free 128 still (run02); each frame is then centre-cropped to 96 with tools/cropcentre.py (Dan-approved for villains).

### zenobia

- **Engine:** Retro Diffusion rd_pro__default (96x96, seed 8354)
- **Pack path:** `art/villains/zenobia_00.png (frame 0 of 8)`
- **prompt:** a head-and-shoulders portrait, the face filling the frame, of a Palmyrene warrior queen in gilded scale armour and an eastern diadem, dark braided hair, the colonnades of Palmyra behind her, the scene filling the whole square edge to edge, no bar, no border, no frame
- **Settings:** `_animation=disdainful face, brow arched, static background, smooth loop`, `bypass_prompt_expansion=false`, `figure=false`, `height=96`, `raw_only=true`, `reference_image_paths=["assets/glory-of-rome/art/classes/dux.png", "assets/glory-of-rome/art/classes/legatus.png", "assets/glory-of-rome/art/classes/praetorianus.png", "assets/glory-of-rome/art/classes/sibylla.png"]`, `seed=8354`, `style=rd_pro__default`, `target=[96, 96]`, `width=96`
- **Note:** Villain portrait still (2026-09-05), the Hannibal route: rd_pro__default, 96x96 opaque, the four class portraits as references. Prompt expansion left on for the stills from Alaric onward (Hannibal's still was made with it off). Re-run 2026-09-07: the earlier still carried a flat colour bar on two or three sides; the scene is told to fill the square, seed incremented. run03: run02 drew a full grey frame; seed incremented. Re-run at Dan's request, seed incremented. Note 2026-09-07: reference images cause letterbox bars on villain stills; future re-runs drop them.

### zenobia_loop

- **Engine:** Retro Diffusion rd_advanced_animation__custom_action (96x96, seed 8462)
- **Pack path:** `art/villains/zenobia_00..07.png`
- **prompt:** disdainful face, brow arched, static background, smooth loop
- **Settings:** `bypass_prompt_expansion=false`, `figure=false`, `frames_duration=8`, `height=96`, `input_image_keep_alpha=false`, `input_image_path=build/art/zenobia/run04/01_raw.png`, `raw_only=true`, `return_spritesheet=true`, `seed=8462`, `style=rd_advanced_animation__custom_action`, `target=[96, 96]`, `width=96`
- **Note:** Villain face loop (2026-09-05), the route proven on Hannibal: custom action on the untouched approved still, eight frames, prompt expansion on, the worklist row's tag prompt. Judged by pixels changed outside the face and the 3x gif. Re-run from the bar-free still run04.

## The launcher icon

`python3 tools/romeart.py icon build/art/icon` composes it from the title
screen's own pieces -- `art/ui/title_battle.png` and `art/ui/title_eagle.png`
-- squared at 128x128 without the menu or the wordmarks, then doubled with
nearest-neighbour to 512 (Play) and 1024 (App Store). Opaque, because Apple
rejects an icon with an alpha channel.

The installed copies are `ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png`,
`android/res/mipmap-xxxhdpi/ic_launcher.png` and
`android/play-assets/icon-512.png`.
