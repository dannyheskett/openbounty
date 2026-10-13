# Italia key frame

`italia_key.png` is the key frame for every Italia tile, object, hero and animation. It was chosen by Dan on 2026-10-10 at Gate A.
It is a reference, not a game frame. Every later Italia generation is judged against it for look, colour, detail, light and camera.

- **Job:** `art/jobs/italia96/key_pro4.json`, run01 (request `eb73c70c-324c-42e3-95e8-b48507cdf055`, $0.18).
  The job's `_kept` field records the pick.
- **Settings:** `rd_pro__topdown`, 256 × 256, seed 9651, one image, no reference image, no palette.
- **Prompt:** "A Roman rider on horseback travelling a cypress-lined stone road through lush green Italian hills, past
  silver-green olive groves, vineyards and wheat fields toward a walled town with terracotta roofs, beside a river with a
  stone arch bridge that runs into a blue Mediterranean bay with a sandy beach, rocky mountains beyond, warm afternoon
  sunlight"
- **Palette (locked by Dan 2026-10-10; 8 rock greys added on Dan's word the same day):** `art/palettes/italia.png`, 72 colours,
  one pixel each, sorted by hue then lightness.
  - All 26 of the key's colours are included.
  - 38 more were added from other italia96 runs: `key_pro` run01, all four takes (28); `key_pro3` run01 (6), excluding
    its white border; `re_town_rome` run01 (4).
  - Each addition was the colour farthest from the palette so far, weighted by how many pixels use it. Colours under
    30 px were ignored.
  - 8 rock greys were then added, because the first mountain sprite (plan v2, `rock_00`) took sea blues for grey rock.
    They're a neutral grey ramp from (35, 37, 44) to (196, 203, 210), picked the same way from the shipped Italia rock
    sprites (`art/primitives/italia/rocks/`).

The other files here (`town_rome.png`, `castle_palatium.png`, `town_italia.png`) are the previous game art flattened on
grass. They were used as reference images in `art/jobs/italia96/obj_*.json` and are not the key.
