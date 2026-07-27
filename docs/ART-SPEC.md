# OpenBounty Art Spec

Technical specification for a complete pack of game graphics. The reference
pack at `assets/kings-bounty/` is the working example of every category below
and is the place to look for exact filenames and counts.

---

## 1. The one hard constraint: 48 × 34

Nearly every sprite and every map tile is **48 × 34 pixels**. It is not
negotiable and not a pack setting — it is compiled into the renderer
(`src/layout.h`, `CL_TILE_W 48`).

The odd aspect comes from the original VGA 320×200 mode, whose pixels were
**not square**: they were displayed on a 4:3 screen, so each pixel is about
20% taller than it is wide. A 48×34 tile therefore reads on screen as roughly
square. **Draw for the display, not the pixel grid** — a circle you want to
look round should be drawn as a slightly wide ellipse. If you work in a
square-pixel program, set a 1.2 : 1 horizontal stretch on your preview.

The whole game renders into a 320 × 200 buffer, integer-scaled to the window.
Nothing is drawn at a fractional scale, so there is no antialiasing to hide
behind: every pixel you place is a pixel the player sees.

## 2. Format and transparency

- **PNG, 32-bit RGBA.** Indexed-color PNGs are not required.
- **Alpha is binary in practice** — fully opaque or fully transparent. Avoid
  soft or feathered edges; they read as dirt at this size.
- **Sprites need real transparent backgrounds.** Troops, villains, the hero,
  the boat, and map objects are drawn *over* the terrain beneath them. An
  opaque background shows as a rectangle of the wrong ground.
- **Terrain tiles are fully opaque** and must tile seamlessly against their own
  kind on all four edges.
- No drop shadows onto transparency, no baked-in ground under a unit.

## 3. Palette and color

The pack ships a 256-color VGA palette (`palettes/palette.bin`, exactly
768 bytes, 256 × RGB triplets). The engine uses it for the UI chrome's named
colors, **not** to quantize your art — the PNGs can carry any RGBA values.

Even so, the game should look like one thing. Work from a **restricted palette
of roughly 32–64 colors** across the whole pack and deliver the final table as
the 768-byte binary. The first 16 entries are the standard EGA/VGA named
colors (black, blue, green, cyan, red, magenta, brown, light grey, dark grey,
the six bright variants, white); everything from index 16 up is yours.

Style target: late-80s / early-90s DOS VGA. Chunky, high-contrast, readable at
100%. Dithering is welcome and period-correct.

## 4. Animation

Animated things are **4 frames**, named `<root>_00.png` … `<root>_03.png`,
played as a straight 0→1→2→3 loop at a player-configurable 0.05–0.30 s per
frame. No ping-pong, no per-frame timing, no directional variants.

These are **idle animations** — a breathing bob, a weapon shift, a flicker of
cloth — not walk cycles. Sprites do not turn to face a direction; the engine
mirrors the hero horizontally when he moves left, so **draw the hero facing
right**.

Everything in §5 is a single static frame unless the table says 4.

## 5. Categories

| Category | Size | Frames | What it is |
|---|---|---|---|
| **Troop creatures** | 48 × 34 | 4 | The recruitable/enemy creature types, spanning a trash-to-apex power curve. Seen on the overworld and in combat, so they must differ in silhouette. Some fly, some are undead, some are ranged attackers — the art should telegraph which. |
| **Villains** | 48 × 34 | 4 | Named individual antagonists, escalating in menace. Each has flavor text in `game.json` (`strings.villain_descriptions`) worth reading first. Frame `_00` doubles as the contract-view portrait. |
| **Base terrain** | 48 × 34 | 1 | Grass, forest, mountain, water, desert, plus a grass variant. Opaque, seamlessly tiling. |
| **Terrain edges** | 48 × 34 | 1 | Transition pieces blending each non-grass terrain into grass: twelve orientations per terrain (four sides, four outer corners, four inner corners). |
| **Structures** | 48 × 34 | 1 | Castles (a 3×2 footprint: one walkable gate tile plus five wall pieces), towns, and the four dwelling types (plains, forest, hills, dungeon). |
| **Map objects** | 48 × 34 | 1 | Chests, artifact pickups, signposts, bridges (horizontal and vertical), and the wandering-army marker. These sit on grass — transparent surrounds. |
| **Hero and boat** | 48 × 34 | 4 | The player avatar on the overworld, and the boat that replaces him while sailing. |
| **Combat arena** | 48 × 34 | 1 | The open-field battle floor (tiled across a 6×5 grid), scattered field obstacles, and siege walls/spikes for castle battles. |
| **Target cursor** | 48 × 34 | 4 | The combat target-picker. Overlays a unit, so a thin animated frame, not a fill. |
| **Class portraits** | 96 × 102 | 1 | One per playable class. The only place the hero is rendered large. |
| **Location backdrops** | 240 × 102 | 1 | The scene behind each visit screen. Six moods: civic, martial, open country, woodland, cave, crypt. |
| **Screen chrome** | 320 × 200 | 1 | The full-screen frame — top status bar, map viewport border, right sidebar. The map and HUD draw into its cutouts. |
| **Title and splash** | 320 × 200, 320 × 84 | 1 | Title screen and publisher logo. |
| **Character creation** | 288 × 184, 42 × 44 | 1 | The class-picker panel and its selection cursor. |
| **Endgame art** | 144 × 170 | 1 | Win and lose illustrations. A few 48 × 34 pieces compose the animated victory scene. |
| **Sidebar icons** | 48 × 34 | 1 and 4 | Gold purse and puzzle grid are static. Contract, siege, and magic each need a static "unavailable" silhouette plus a 4-frame "available" animation. |
| **Artifact icons** | 48 × 34 | 1 | One per collectible artifact, shown in the inventory view. |
| **Continent emblems** | 48 × 34 | 1 | One per zone. |
| **Puzzle cover** | 9 × 6 | 1 | One cell of the 5×5 puzzle grid's concealing cover. |
| **HUD separator** | 320 × 5 | 1 | Thin horizontal rule. |
| **Bitmap font** | 1024 × 8 | 1 | See §6. |

## 6. Font

A single horizontal strip of **128 glyphs, each 8 × 8 pixels**, in ASCII
order — glyph *n* occupies x = n × 8. Supply printable glyphs for codes
32–126; codes 0–31 are unused except for four the engine repurposes as a
spinner (`|`, `/`, `-`, `\`). White on transparent; the engine tints it at
draw time.

## 7. Delivery

Directory layout, filenames, and per-category counts follow the reference pack
at `assets/kings-bounty/art/`. Filenames are referenced from `game.json` and
are **case-sensitive** — match them exactly unless you are also editing that
manifest. The palette binary goes at `palettes/palette.bin`.

An asset is done when it (a) is the exact pixel dimensions listed, (b) is RGBA
PNG with clean binary alpha, (c) holds the shared palette, and (d) reads
correctly at 1× on a 320×200 screen, not just zoomed in. Drop the tree into a
pack directory and run `./openbounty --pack /path/to/pack` to see it live.

Audio, if in scope: OGG for music (overworld and combat themes), WAV for
effects (walk, bump, chest, defeat).
