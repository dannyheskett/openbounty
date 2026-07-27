# OpenBounty GameBuilder, Requirements

**Status:** built, first pass. The requirement set for **GameBuilder**,
OpenBounty's game-pack editor. Phases 0-6 are implemented
(`tools/gamebuilder/`, `make gamebuilder`); this document is now both the
requirement set and the record of what exists.

**As built.** Start screen with recent packs; in-app file browser; eight
modes (Maps, Objects, Catalog, Strings, Art, Palette, Validate, Package);
undo/redo across all of them; autosave with crash recovery; `.openbounty`
archives opened by extraction; a cJSON-DOM workspace that preserves unknown
keys and saves byte-identically when unchanged; multi-zone terrain painting
with live furnishing; object placement with castle footprints; catalog forms
with typed commit and raw-JSON escape hatch; string editing with token
highlighting; a palette-constrained pixel editor with onion-skin, playback and
in-context tiling preview; three-tier validation; out-of-process winnability
checking; packaging to `.openbounty`; a generated new-pack skeleton; and a
completeness checklist. Cross-compiles for Windows.

**Measured against real packs.** Running every operation headlessly over the
reference pack reports one finding (Saharia has 3 villains and 3 castles, which
REQ-300 says is too tight) and 15/16 checklist items complete. Over Glory of
Rome it reports 8 findings, all true: six town docks carry the reference pack's
coordinates and now sit on land, the zone has no chest placeholders against a
salt budget of 21, and villains + artifacts total 14 rather than the 25 the
puzzle grid needs.

**Not yet built:** art import with per-file validation (GB-241/242), palette
editing as opposed to viewing (GB-260/262), pack content versioning
(GB-330..332), and release packaging (GB-506).

**Conventions.** Each requirement carries a stable identifier `GB-NNN`. This
document owns the `GB-` namespace; `OPENBOUNTY-SPEC.md` owns `REQ-`,
`AUTOPLAY-SPECS.md` owns `AP-`, `DEMO-SPEC.md` owns `DM-`. Requirements are
written as obligations ("the editor shall"), unlike the as-built specs, because
this describes work not yet done.

---

# Part I, Product

## 1. The two goals

- **GB-001.** The editor shall support **building a complete pack from
  nothing**: a user with no pack, only art and ideas, can produce a valid
  `.openbounty` file that the game loads and plays.
- **GB-002.** The editor shall support **editing an existing pack**: open it,
  change anything, validate, and produce a new version. Round-tripping a pack
  through the editor without edits shall not change its behaviour.
- **GB-003.** These two paths shall share one model. "New" is the same editor
  over an empty workspace, not a separate wizard with its own code.

## 2. Audience and platforms

- **GB-010.** The editor **ships to end users** in the release archives. It is
  how community packs get made, which is what `PACK-FORMAT.md` already
  promises. It is not a developer-only tool.
- **GB-011.** It shall build for the four desktop targets the game builds for:
  Linux x86_64, Windows x86_64 and i686, macOS universal. Web is out of scope
  (no filesystem).
- **GB-012.** Because it ships to strangers, every failure shall be reported in
  terms the user can act on — which file, which field, what to do — never a
  bare assert, and never silent. Losing work shall be treated as the most
  serious class of defect.
- **GB-013.** The editor is a separate binary (`openbounty-gamebuilder`). It shall
  not be linked into the game, and the game shall not depend on it.
- **GB-015.** **It is a GUI application, not a command-line tool.** The binary
  shall take **no arguments**. Everything — which pack to open, where to save,
  which base pack to layer, which zone to edit — is chosen by pointing and
  clicking inside the application. There shall be no flag that is the only way
  to reach a feature, and no workflow that requires a terminal.
  (The current `openbounty-mapedit`, which takes `--pack` / `--zone` /
  `--base`, is a development stepping stone and is superseded by this.)
- **GB-016.** The editor shall open on a **start screen**: New Pack, Open Pack,
  and a list of recently opened packs. It shall never open into an empty or
  undefined state.
- **GB-017.** The recent-packs list shall persist across sessions in the user
  data directory (`REQ-412`), never inside a pack.
- **GB-018.** The editor shall provide its **own in-application file browser**
  for choosing directories and `.openbounty` files. It shall not depend on a
  native file dialog: raylib provides none, and the usual Linux answer
  (shelling out to `zenity` or `kdialog`) adds a runtime dependency that fails
  on a bare system. An in-app browser also looks the same on all four targets.
- **GB-014.** **Glory of Rome ships with the editor**, as the release's
  playable pack. It is the first pack that legally can ship: it is entirely
  original content, where the `kings-bounty` pack is DOS-extracted and
  copyright-restricted.
- **GB-014a.** That changes the release workflow's pack-leak assertion, which
  today fails **any** archive containing a `.openbounty` file, in four places
  (`.github/workflows/release.yml`). It shall become an **allowlist by pack
  id**: `glory-of-rome` may ship; anything derived from the DOS assets never
  may. The rule being enforced is "no extracted content in a release", and the
  assertion shall say so, because a blanket deny that someone later widens by
  hand is how extracted art escapes.
- **GB-014b.** Shipping Rome puts the **art commission on the critical path**.
  Rome currently borrows `kings-bounty` art through a base pack for
  development; none of that may survive into the shipped artifact (GB-520). The
  release is gated on original art existing for every category in
  `ART-SPEC.md`.
- **GB-014c.** Building Glory of Rome **in the editor** is the editor's
  acceptance test. A phase is not done because its requirements are ticked off;
  it is done when the corresponding part of Rome was built with it. This is the
  cheapest available guard against building an editor that is pleasant to
  specify and unusable in practice.

## 3. Non-goals

- **GB-020.** Not a general image editor. The pixel editor (§9) is scoped to
  pack sprites at pack dimensions with the pack palette; it will not compete
  with Aseprite and shall not try.
- **GB-021.** Not a scripting or logic editor. Pack authors change data, art,
  audio and maps. Behaviour lives in the engine.
- **GB-022.** Not a save-file editor.
- **GB-023.** No multi-user editing, no version control integration beyond
  writing plain files that a VCS can diff.

---

# Part II, Foundations

## 4. Application shell

- **GB-100.** The editor shall present one window with a **mode bar**: Maps,
  Objects, Catalog, Strings, Art, Palette, Validate, Package. Modes share one
  open workspace; switching modes shall never discard unsaved edits.
- **GB-101.** The UI shall be built on **raygui** (`third_party/raygui`,
  zlib-licensed, already present in the raylib tree). Hand-rolling buttons,
  text fields, scroll lists, tables, dropdowns, colour pickers and modals is
  otherwise the single largest cost in this document.
- **GB-102.** The editor shall support **undo and redo across every mode**,
  with a shared history. An edit that cannot be undone shall be refused or
  confirmed explicitly.
- **GB-103.** The editor shall **autosave the workspace** to a scratch location
  at a fixed interval and on focus loss, and offer recovery after a crash.
  Autosave shall never write to the pack itself.
- **GB-104.** Every destructive action (delete a troop, replace an image,
  overwrite a pack) shall require confirmation and be undoable.

## 5. Workspace and pack IO

- **GB-110.** A **workspace** is one pack under edit plus editor state
  (selection, view, undo history). The editor shall open a loose directory, a
  `.openbounty` archive, or nothing (new pack).
- **GB-111.** Opening an archive shall extract to a working directory; the
  archive itself is never edited in place.
- **GB-112.** The editor shall support a **base pack** layered underneath the
  workspace, so a pack under construction can borrow art it does not carry yet.
  This is **never automatic**. The author explicitly chooses a base pack, in
  the UI, and can change or remove it at any time; nothing is inherited by
  default and no pack is layered without being picked. The alternative to
  layering is equally explicit: copy the assets into the workspace.
- **GB-112a.** Inherited assets shall be **visibly marked as inherited**
  everywhere they appear, so the author always knows which parts of the pack
  are theirs. Packaging shall not silently materialise them (GB-520).
- **GB-113.** Saving shall write **only files that changed**, preserving byte
  content of untouched files, so a pack under version control produces minimal
  diffs.
- **GB-114.** `game.json` shall be written with **stable key order and
  formatting**, so two saves of an unchanged pack are byte-identical.
- **GB-115.** The editor shall detect that the pack changed on disk underneath
  it and offer to reload rather than silently overwrite.

## 6. The raw-JSON escape hatch

- **GB-120.** Every catalog screen shall have a **raw JSON view** of the object
  it edits, editable in place, validated on commit. The editor shall never be
  a dead end for a field its forms do not cover.
- **GB-121.** Unknown keys in a loaded `game.json` shall be **preserved
  verbatim** through a round-trip, so a pack using engine features newer than
  the editor is not silently damaged.

---

# Part III, Editors

## 7. Maps

- **GB-200.** The map editor shall edit **any zone** in the pack, with zone
  create, delete, rename, and resize (bounded by `MAP_MAX_W` / `MAP_MAX_H`,
  `REQ-133`).
- **GB-201.** The author paints **base terrain only**. Edge variants are
  derived by the furnish pass and baked on save; they shall never be placed by
  hand (`REQ-229`, and §10.6.1 of `GLORY-OF-ROME.md`).
- **GB-202.** The canvas shall render through the game's own `tile_cache`, so
  the editor cannot drift from what the game draws.
- **GB-203.** Tools: paint, brush sizes, flood fill, rectangle, line, terrain
  picker (eyedropper), and rectangular select with cut/copy/paste.
- **GB-204.** The despeckle pass shall run continuously and its edits shall be
  **visible and undoable** — the author must be able to see that the editor
  reshaped their terrain and why (three-cardinal, opposite-pair, or
  multi-diagonal, per `REQ-229a`).
- **GB-205.** A minimap of the whole zone, click to navigate.
- **GB-206.** Export the current zone to PNG at flat-colour or full tile
  resolution, replacing `tools/maprender.py`.

## 8. Objects

- **GB-210.** The editor shall place, move, delete and edit every interactive
  the pack declares per zone: towns, castles, chests, signs, dwellings, foe
  armies, and the zone's hero spawn, home spawn and magic alcove.
- **GB-211.** Objects shall be edited **on the map**, in place, not through a
  coordinate form — placement is a spatial decision.
- **GB-212.** A castle shall be placed as its true **3×2 footprint** (walkable
  gate plus five blocking wall tiles, `REQ-228`) with the footprint shown, and
  placement refused where it will not fit.
- **GB-213.** Each object type shall have a properties panel: a town's boat
  dock, intel castle and pinned spell; a castle's difficulty tier and villain;
  a sign's title and body; a dwelling's troop; a foe army's stacks.
- **GB-214.** Salt-placed content (artifacts, navmaps, orbs, telecaves,
  dwellings, friendly foes) is **not** placed directly — it is drawn at game
  start from chest placeholders (`REQ-231`). The editor shall show the per-zone
  salt budget against the number of chest tiles present and warn when the
  budget cannot be met.
- **GB-215.** Objects shall be visible while editing terrain and vice versa,
  with independent layer visibility toggles.

## 9. Catalogs

- **GB-220.** The editor shall provide form-based editing for **every catalog
  in `game.json`**: troops, spells, artifacts, villains, classes (with their
  rank ladders), castles, towns, zones.
- **GB-221.** And for every numeric block: economy, chest tables, tuning,
  spawn tier curves and troop pools, the morale chart, scoring, time and
  difficulty, contract cycle, colours, audio metadata, controls rows.
- **GB-222.** Catalog entries are referenced **by string id** (`REQ-102`).
  Renaming an id shall offer to update every reference to it, and deleting an
  entry that is still referenced shall list the referrers and refuse or cascade
  explicitly.
- **GB-223.** Each catalog shall show its compile-time cap (`CAT_TROOPS_MAX`
  and siblings, `REQ-113`) and refuse to exceed it, naming the constant.
- **GB-224.** Cross-catalog edits shall keep derived views correct: the puzzle
  grid is 5×5 and requires villains + artifacts to total exactly 25
  (`REQ-431`), so changing either count shall warn immediately, not at load.
- **GB-225.** Troop, villain and class forms shall show a **live sprite
  preview** of the entry being edited, animating at its four frames.

## 10. Strings

- **GB-230.** The editor shall edit every user-visible string: the ~111 banner
  entries, the 17 villain descriptions, UI labels, contract-view text,
  win/lose text, credits, count-bucket labels.
- **GB-231.** Each string shall show its **substitution tokens** (`%NAME%`,
  `%GOLD%`, `%COUNT%`) and warn when an edit drops or invents one, since a
  missing token silently renders wrong in game.
- **GB-232.** Strings shall be presented as a searchable, filterable table with
  the engine's built-in English fallback shown for any key the pack omits
  (`REQ-421`), so an author can see what they are overriding.
- **GB-233.** Long-form strings (villain descriptions, sign bodies, win/lose
  text) shall have a multi-line editor with the in-game line-width and
  page-break behaviour previewed, including form-feed pagination (`REQ-432`).

## 11. Art

- **GB-240.** The editor shall present the pack's art as a **browsable
  catalogue** by category — troops, villains, tiles, UI, combat, sprites,
  classes, font — showing which are present, which are inherited from a base
  pack, and which are missing.
- **GB-241.** Import shall accept PNG and validate against `ART-SPEC.md`:
  exact dimensions per category, RGBA, binary alpha, and palette conformance.
  Failures shall name the specific problem, not "invalid image".
- **GB-242.** Import shall never silently alter the source. Any fix-up
  (resize, quantise, alpha threshold) shall be an explicit, previewed,
  undoable action.
- **GB-243.** Preview shall show sprites **in context**: troops animating at
  4 frames, tiles laid against their neighbours with edge variants resolved,
  UI chrome composited at 320×200.
- **GB-244.** The editor shall report art the pack contains but nothing
  references, and references to art the pack lacks.

## 12. Pixel editor

- **GB-250.** The editor shall include a **pixel editor** for pack sprites and
  tiles, opened from the art catalogue on any image.
- **GB-251.** It shall be **constrained to the pack**: the image's own
  dimensions (48×34 for most, per `ART-SPEC.md`), the pack's 256-colour palette
  as the only selectable colours, and binary alpha.
- **GB-252.** Tools: pencil, eraser, fill, line, rectangle, ellipse,
  rectangular select with move/cut/copy/paste, colour picker, mirror and flip.
- **GB-253.** Frame support: an animated sprite's four frames shall be editable
  together, with onion-skinning of adjacent frames and live playback at the
  in-game frame interval.
- **GB-254.** Tile editing shall preview **in context** — the tile drawn amid
  its neighbours on a real map region — because a tile that looks right alone
  routinely looks wrong tiled.
- **GB-255.** The pixel editor is **not** a general image editor (GB-020). No
  layers, no filters, no arbitrary canvas sizes, no non-palette colour.

## 13. Palette

- **GB-260.** The editor shall edit the pack's 256-colour palette
  (`palettes/*.bin`, 768 bytes, `REQ-470`), with the first 16 entries marked as
  the reserved named indices.
- **GB-261.** Changing a palette entry shall preview its effect across all
  loaded art, since a palette edit repaints the whole pack.
- **GB-262.** The editor shall report art using colours outside the palette,
  and offer to build a palette from the pack's existing art.

---

# Part IV, Quality and release

## 14. Validation

- **GB-300.** Validation shall run in three tiers, all **advisory** — nothing
  blocks packaging (GB-320). Each finding shall name the file, the field or
  coordinate, and what to do about it.
- **GB-301.** **Structural**: `game.json` parses; required fields present;
  every referenced art file exists at correct dimensions; every tile code in a
  `.dat` is declared; map dimensions within `MAP_MAX_*`; catalog caps
  respected; palette exactly 768 bytes.
- **GB-302.** **Referential**: every id referenced by another catalog exists;
  no orphaned entries; puzzle grid totals 25; each zone's `neighbors[]` name
  real zones; exactly one zone `is_home`; every villain's zone has more
  contract-eligible castles than villains (`REQ-300`).
- **GB-303.** **Spatial**, per zone: no town dock on landlocked water (the boat
  trap, §10.5 of `GLORY-OF-ROME.md`); no objective in a walled inland pocket;
  castle footprints fit and do not overlap; chest placeholder count meets the
  salt budget; hero spawn and alcove on legal tiles; no tile left without a
  legal edge variant.
- **GB-304.** Validation shall run **continuously in the background** and
  surface a live count per mode, so an author sees a problem when they create
  it rather than at package time.
- **GB-305.** This subsumes `tools/mapcheck.py`, which shall be retired once
  GB-303 is complete.

## 15. Winnability

- **GB-310.** The editor shall run the autoplay oracle (`--validate-pack`,
  `AP-014`) against the workspace, over a user-chosen seed range, reporting per
  seed: verdict, objectives cleared, days, score, and on a miss the first
  blocking objective with its typed cause.
- **GB-311.** It shall run **out of process** against the game binary, not
  linked in, so a hang or crash in the oracle cannot take the editor's unsaved
  work with it.
- **GB-312.** It shall be interruptible, and report progress per seed while
  running, since a full 256-seed sweep can take hours.
- **GB-313.** Results shall be **advisory** (GB-320) and shall be retained with
  the pack version they were measured against, so an author can tell whether a
  result is stale.

## 16. Packaging

- **GB-320.** **Nothing blocks packaging.** All checks report; the author
  decides. A package produced with outstanding findings shall record them in
  the build report rather than refusing.
- **GB-321.** Packaging shall produce a `.openbounty` archive via the engine's
  existing zip path (`pack_zip_dir`, `REQ-510`), and optionally a loose tree.
- **GB-322.** The editor shall show a **pre-package summary**: file count and
  size by category, outstanding findings by tier, last winnability result and
  its age.
- **GB-323.** Packaging shall exclude working files — autosaves, editor
  metadata, `.xcf`/`.psd` sources — by the same rules the existing zip walker
  applies.
- **GB-324.** A packaged pack shall be **immediately loadable**: the editor
  shall offer to launch the game against the output as the final step.

## 17. Pack versioning

- **GB-330.** A pack shall carry an author-facing **content version**, distinct
  from `version`, which is the pack *schema* version the engine checks
  (`PACK-FORMAT.md` §10). Releasing a new version of an existing pack is a
  first-class goal (GB-002) and needs somewhere to record it.
- **GB-331.** The editor shall maintain a changelog alongside the content
  version, and surface both in the pack picker if the engine grows a place to
  show them.
- **GB-332.** The editor shall warn when the pack's schema `version` does not
  match the engine it was built against.

---

# Part V, Starting from nothing

## 18. New pack

- **GB-400.** "New pack" shall produce a **minimal valid pack** that loads and
  runs: one zone, one castle, one town, a playable troop catalog, and the
  engine's built-in string fallbacks (`REQ-421`). It shall be immediately
  launchable, so the author starts from something that works rather than
  something that errors.
- **GB-401.** The skeleton shall carry **no copyright-restricted content**. It
  is generated, not copied from `kings-bounty`.
- **GB-402.** The editor shall offer to start from an existing pack as a
  template, which is GB-002 with a rename.
- **GB-403.** A guided checklist shall track what a new pack still needs to be
  complete — art categories unfilled, catalogs empty, zones without objects —
  since the gap between "loads" and "finished" is where a first-time author
  gets lost.

---

# Part VI, Phasing

Ordered so that **editing an existing pack (GB-002) works before building one
from scratch (GB-001)**, because the former is testable against the reference
pack at every step.

- **GB-500. Phase 0 — Foundations.** raygui vendored; start screen and
  in-app file browser (GB-016, GB-018); mode bar; workspace open and save for
  loose and archive packs; stable JSON writer; undo/redo; autosave; raw-JSON
  view. *Exit:* launch the binary with no arguments, open the reference pack by
  clicking, change one field in raw JSON, save, and the game still loads it; an
  unchanged round-trip is byte-identical.
- **GB-501. Phase 1 — Maps and objects.** Extends the existing terrain editor
  (§7 already partly built) with multi-zone, selection tools, minimap, then all
  of §8. *Exit:* place every object type on a new Glory of Rome zone and play
  it.
- **GB-502. Phase 2 — Catalogs and strings.** §9 and §10. *Exit:* re-theme the
  reference pack end to end — ids, names, stats, all strings — without a text
  editor.
- **GB-503. Phase 3 — Art and palette.** §11 and §13. *Exit:* import a full
  replacement art set and see it in game.
- **GB-504. Phase 4 — Validation and packaging.** §14, §15, §16, §17. *Exit:*
  produce a `.openbounty` from the editor, launch it, and retire
  `mapcheck.py`.
- **GB-505. Phase 5 — Pixel editor.** §12. Last because it is the largest piece
  with the least leverage on the two goals: art can be made elsewhere and
  imported throughout phases 0–4.
- **GB-506. Phase 6 — Ship it.** Cross-platform builds, packaging into the
  release archives, first-run experience, docs. *Exit:* a stranger on Windows
  downloads the release and builds a pack.

## 19. Sizing, honestly

- **GB-510.** This is an application. A realistic estimate is
  **12,000–16,000 lines of C** on top of the current ~600, with the pixel
  editor and the catalog forms the two largest pieces. raygui removes perhaps a
  third of that; without it the estimate roughly doubles. The number is here to
  size the phases, not to argue against the scope.
- **GB-511.** The riskiest requirement is **GB-012** (never lose work). It is
  cheap to state and expensive to honour, and it is the difference between a
  tool people use and one they abandon.

## 20. Open questions

- **GB-520.** **Resolved.** Base packs are chosen explicitly (GB-112), never
  inherited automatically, so the author always knows they are borrowing.
  Packaging shall still **not** silently fold borrowed assets into the output:
  it shall list exactly what is inherited and require an explicit choice per
  package — copy them in, or ship without them. That keeps a derived pack from
  quietly redistributing the reference pack's copyright-restricted DOS art
  while a deliberate author can still do it with their own material.
- **GB-521.** Does the editor need audio import and preview, or is audio
  hand-placed? Six files in the reference pack, so low cost either way.
- **GB-522.** Should the map editor support more than one pack open at once,
  for copying content between packs?
