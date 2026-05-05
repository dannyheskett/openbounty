# COMBAT-PLAN.md — OPENBOUNTY combat engine implementation plan

Faithful re-implementation of the King's Bounty / OpenKB combat engine in OPENBOUNTY-native C+raylib style. Every observable behavior must match OpenKB / DOS KB; visual look must approximate the DOS combat screen at 320×200 (rendered at 640×400). Implementation style is OPENBOUNTY-native (JSON-driven data, modern code, the existing classic-renderer chrome).

---

## 0. Pre-flight: source artifacts grounding this plan

Every formula, constant, and string in this plan is anchored to one of:

- **OPENKB-SPEC.md**: §3.1 (constants, lines 494–599); §3.7 (abilities, 762–770); §3.8 (powers, 779–795); §3.10 (combat-grid 6×5, 809–824); §5 (troop catalog, 1407–1905); §17 (combat engine, 5128–5923); §18.1–18.7 (combat spells, 5944–6004); §18.15–18.17 (target picker, spell-power, workflow, 6076–6125); §19.2–19.4 (combat AI, 6473–6523); §23.1–23.4 (gamestates / hotspots / combat statusbar, 7398–7519); §24.43 (combat log strings, 8233–8254); §26 (palette, 8534–8776); §31.2 (combat keybindings, 9692–9719).
- **OPENBOUNTY-SPEC.md**: §3.10 (lines 528–535); §17 (current stub status, 2034–2050); §18.1–18.7 (currently N/A, 2055–2058); §23.1–23.6 (panel layout, 2310–2376); §23.4–23.5 (modal dialog/prompt API, 2360–2371); §24 (banner/template format, 2425–2476); §26.1–26.2 (palette + scheme conventions, 2495–2531); §31.2 (combat bindings, 2676–2693); Appendix A.32 (RunCombatStub source, 4341).
- **SPEC-COMPARISON.md**: lines 193–219 (combat parity rows); lines 420–426 (spec sections deferred). Confirms scope: full §17 + §18.1–7 + §19.2–4 are missing in OPENBOUNTY.
- **legacy/manuals/Kings-Bounty_Manual_DOS_EN_Web-Manual.md** (player-rule reference for any tie-break the spec leaves open).
- **legacy/screenshots/**: `kb_038.png` — combat options/keybindings list; `kb_074.png` — castle siege (walls visible left/right, troops in formation rows); `kb_075.png` — open-field combat with obstacles + status banner "Options / Vampires F,M1"; `kb_078.png` — open-field with banner "Barbarians vs Giants killing 0"; `kb_076.png` — view-army screen (stat panel reference); `kb_080.png` — Spells menu (Combat | Adventuring 7×2); `kb_066.png` — victory dialog; `kb_096.png` — defeat dialog; `kb_046.png` — typical adventure-screen sidebar (chrome layout reference).
- **kb-legacy/**: KB.EXE / KB.unpacked.EXE — authoritative tiebreaker only; no disassembly required by this plan.
- **/home/danheskett/personal/openbounty/src/combat.{c,h}** — current 60-line stub; `RunCombatStub` returns `COMBAT_RESULT_WIN`. Three call-sites in `src/main.c` (lines 2056, 2092, 2135) bracket FLOW_SIEGE_MONSTER, FLOW_SIEGE_VILLAIN, FLOW_ATTACK_FOE.
- **/home/danheskett/personal/openbounty/src/game.{c,h}** — `Game` struct; deterministic LCG `game_rng_next` at game.c:14–29 (state private; the combat module will need its own seeded LCG, NOT the world-init one — see Phase 14).
- **/home/danheskett/personal/openbounty/src/tables.h** — `TroopDef` already carries every field the engine needs (skill_level, hit_points, move_rate, melee_min/max, ranged_min/max/ammo, abilities, spoils_factor, morale_group). No additions required.
- **/home/danheskett/personal/openbounty/assets/kings-bounty/game.json** — `combat.morale_chart` (5×5 N/L/H), `combat.number_names`, all 7 combat spells already in `spells[]` with index 0..6 — no engine-data additions needed except a new `strings.combat_log` block (Phase 12).
- **/home/danheskett/personal/openbounty/src/classic/views.c** — pattern for `classic_views_draw` dispatch on `ViewKind`; combat will hook in here.
- **/home/danheskett/personal/openbounty/legacy/artifacts/assets/** — **the OpenKB asset extraction. This is the authoritative visual source for the renderer; the screenshots are reference, the assets are the truth.** Subdirectories:
  - `tilesets/combat/vga/frame_00..14.png` — 15 combat tile sprites at 48×34. The renderer indexes into this array directly via the `omap` codes in OPENKB §17.14 / §17.15:
    - `frame_00`: grass field background (tiled across the field for both open and castle modes)
    - `frame_01..03`: random open-field obstacles (boulder, tree-cluster, mound) — chosen by `omap[y][x]` value 1, 2, 3
    - `frame_04`: castle ornament (decorative)
    - `frame_05..10`: castle wall pieces stamped by `castle_omap` values 5..10 from OPENKB §17.15
    - `frame_11..14`: cursor/ring sprites (active-unit ring, target ring, shoot ring, accent)
  - `units/<id>/vga/frame_00..03.png` — per-troop 4-frame idle animation. Already imported into `assets/kings-bounty/art/troops/` as `<name>.png` and `<name>_NN.png`; the engine's existing sprite loader covers these.
  - `tilesets/tilesalt/`, `tileseta/`, `tilesetb/` — adventure-mode tilesets (not combat).
  - `ui/` (cursor, sidebar, select, nwcp, title, endpic) — UI assets; **not** required for the combat field but useful for modal panels (Phase 12 spells, Phase 13 controls, Phase 11 victory dialog).
  - `palette/`, `backdrops/`, `classes/`, `villains/`, `data/` — adventure-mode + meta.

If any answer is not in these artifacts, this plan flags it as an open question and refuses to invent.

---

## 1. Architectural shape

The combat engine is a **standalone subsystem with its own state struct** that lives only for the duration of a battle. The existing `Game` struct is not bloated — combat state is created on entry, mutated in place, and projected back to `Game` (army counts, gold, followers_killed) on exit. No persistent fields of `Combat` go into save files.

```
RunCombatStub(g, mode, target)  ───►  RunCombat(g, mode, target)
                                          │
                                          ▼
                           ┌──────────────────────────────┐
                           │ Combat war (stack-allocated)  │  ◄── seeded LCG
                           │ - units[2][5]                 │
                           │ - omap[6][7], umap[6][7]      │
                           │ - turn/phase/side/spells      │
                           │ - spoils[2], powers[2]        │
                           │ - heroes[2] = { g, NULL }     │
                           │ - cursor / pick state         │
                           │ - log ring + last banner      │
                           └──────────────────────────────┘
                                          │
                  combat_loop ◄─── input dispatch / AI tick / render
                                          │
                                          ▼
                        accept_units / spoils_apply / outcome
```

**Public API (combat.h, replacing current stub):**

```c
typedef enum { COMBAT_RESULT_WIN, COMBAT_RESULT_LOSS, COMBAT_RESULT_FLEE } CombatResult;
typedef enum { COMBAT_MODE_CASTLE, COMBAT_MODE_FOE } CombatMode;

typedef struct CombatTarget {
    const char *name;
    const Unit *garrison;
    int         garrison_slots;
} CombatTarget;

CombatResult RunCombat(Game *g, CombatMode mode, const CombatTarget *target);
```

The signature exactly matches today's `RunCombatStub` so the three call-sites in `main.c` work unchanged. The stub becomes a one-line forward, then is removed once `RunCombat` is feature-complete.

`RunCombat` is **blocking** like the current stub: it loops on input/animation until the battle resolves, then returns. The classic renderer's main loop already pumps input one frame at a time in `main.c`'s adventure loop; combat will run *inside* `RunCombat` with its own `BeginDrawing`/`EndDrawing` block per frame, mirroring how `views_menu_update` / `prompt_*` already work.

---

## 2. Phased implementation plan

Each phase is sized to be testable in isolation and independently mergeable. Phases 1–6 deliver "deal damage between two stacks given pre-placed units" — the pure logic core. Phases 7–10 deliver full battle UX. Phases 11–14 are spells, AI, polish.

Total estimated effort (per phase, rough): 1d = focused half-day to half-week of work.

| Phase | Topic | Effort |
|------:|-------|--------|
| 1 | Data model & module scaffold | 1d |
| 2 | Battlefield setup, castle layout | 1d |
| 3 | RNG, distance helpers, friendliness, turn structure | 1d |
| 4 | Damage formula `combat_deal_damage` | 2d |
| 5 | Movement, flight, melee driver, retaliation, compact | 2d |
| 6 | Ranged shot, MAGIC/IMMUNE, surround check | 1d |
| 7 | Input + grid heuristic + action menu | 2d |
| 8 | Renderer (battlefield, status, banner) | 3d |
| 9 | Target picker (pick_target) | 1d |
| 10 | Combat AI: ai_unit_think / ai_pick_target / unit_closest_offset / unit_fly_offset | 2d |
| 11 | End-of-combat plumbing (compact, victory/defeat, spoils, accept_units) | 1d |
| 12 | Combat spells (Clone, Teleport, Fireball, Lightning, Freeze, Resurrect, Turn Undead) | 2d |
| 13 | Strings & banners in `game.json`; combat_log lifetime | 0.5d |
| 14 | Determinism harness, scripted scenarios | 1d |

Total: ~20d. Each phase delivers a runnable build that improves the stub.

---

### Phase 1 — Data model & module scaffold

**Goal**: replace the 60-line stub with the full data structures and a no-op `RunCombat` that opens, immediately concedes WIN, and shuts down. Establishes file layout and types.

**Spec citations**: OPENKB §17.1 (KBcombat / KBunit fields, lines 5155–5196); OPENKB §3.10 (CLEVEL_W/H, 809–824); OPENBOUNTY §3.10 (528–535).

**Deliverables (new files)**:

- `src/combat.h` (rewritten):
  - Constants: `COMBAT_W = 6`, `COMBAT_H = 5`, `COMBAT_SIDES = 2`, `COMBAT_SLOTS = 5`, `COMBAT_PHASES = 2`. (OPENKB §3.10, line 812.)
  - Side enum: `SIDE_PLAYER = 0`, `SIDE_AI = 1` (OPENKB §17.1, line 5170).
  - `CombatUnit` struct mirroring OPENKB §17.1 `KBunit` (lines 5180–5195). Field-for-field:
    ```c
    typedef struct CombatUnit {
        int  troop_idx;     // catalog index, -1 = empty slot
        int  count;
        int  turn_count;    // snapshot at start of turn
        int  max_count;     // snapshot at combat start
        bool dead;          // post-attack death flag (cleared by compact)
        int  frame;         // 0..3 anim
        int  injury;        // residual sub-HP damage
        bool acted;
        bool retaliated;
        int  moves;
        int  shots;
        int  flights;
        bool frozen;
        bool out_of_control;
        int  x, y;
    } CombatUnit;
    ```
    Note: `troop_idx` is used instead of `troop_id` byte because OPENBOUNTY troops are JSON-indexed; lookup via `troop_by_index`.

  - `Combat` struct (OPENBOUNTY-native rename of `KBcombat`, OPENKB §17.1 lines 5159–5172). +1 padding on omap/umap as in spec line 5174:
    ```c
    typedef struct Combat {
        CombatUnit units[2][5];
        unsigned char omap[COMBAT_H + 1][COMBAT_W + 1];
        unsigned char umap[COMBAT_H + 1][COMBAT_W + 1];   // 1-based UID, 0 = empty
        int  spoils[2];           // accumulated per side
        unsigned char powers[2];  // POWER_* bits, see tables.h ArtifactPower
        Game *heroes[2];          // [0] = player Game*, [1] = NULL for foe/castle AI
        int  turn;
        int  phase;               // 0 or 1
        int  spells_this_round;   // ≤ 1 (OPENKB §17.1, line 5169)
        int  side;                // current acting side
        int  unit_id;             // current unit index in side
        // — RNG —
        unsigned long rng_state;  // seeded LCG, see Phase 3
        // — UI/log —
        char banner[160];         // current top-of-screen banner ("Knights vs Giants killing 5")
        char log_lines[8][80];    // ring buffer
        int  log_count;
        // — pick / cursor —
        int  cursor_x, cursor_y;
        int  target_filter;       // 0..5, see OPENKB §18.15 (line 6082)
        // — outcome scratch —
        int  result;              // 0=running, 1=player win, 2=AI win
        int  villain_id;          // for victory dialog substitution; -1 = none
    } Combat;
    ```

  - Public function: `CombatResult RunCombat(Game *g, CombatMode mode, const CombatTarget *target);`

- `src/combat.c` (rewritten): empty implementation that initializes a `Combat` on the stack, returns WIN. Compiles. The old `RunCombatStub` becomes a thin alias `#define RunCombatStub RunCombat` only if the call-sites haven't been updated — preferred path is to update the three call-sites in main.c to call `RunCombat` directly (mechanical rename).

**Files touched in `src/`**:
- `src/combat.c`, `src/combat.h` — full rewrite.
- `src/main.c` lines 2056, 2092, 2135 — rename `RunCombatStub` → `RunCombat`.

**Verification**:
- Build green; existing stub-driven flows (siege monster, siege villain, attack foe) still complete; outcome still WIN. A compile-and-run smoke test of those 3 flows confirms the rename didn't break anything.
- No behavior change yet.

**Dependencies**: none.

---

### Phase 2 — Battlefield setup (`prepare_units_*`, `reset_match`, castle layout)

**Goal**: when `RunCombat` is entered, populate the `Combat` struct with player + AI armies, place obstacles, and call a stubbed `combat_loop` that immediately concedes.

**Spec citations**: OPENKB §17.2 (lines 5197–5250) for prepare_units_player / foe / castle; §17.14 (random obstacles, 5862–5878); §17.15 (castle_umap / castle_omap, 5882–5901); §17.16 (spoils accumulator, 5907–5912).

**Deliverables (combat.c internal)**:

- `static void combat_prepare_player(Combat *c, Game *g)`:
  Per OPENKB §17.2 lines 5202–5213:
  - For each `i` in 0..4: read `g->army[i]`, resolve troop catalog index, set `units[0][i].troop_idx`, `count`, `max_count`. Empty slot (`g->army[i].id[0] == 0` or count == 0) → `troop_idx = -1`.
  - `units[0][i].out_of_control = !unit_under_control(g, i)` — see Phase 3.
  - Initial position: `y = i; x = 0` (column 0, rows 0..4). OPENKB line 5208.
  - `c->spoils[0] += troop->spoils_factor * 5 * count` (OPENKB line 5209).
  - `c->heroes[0] = g`, `c->heroes[1] = NULL`.
  - `c->powers[0] = 0`; for each artifact found, `|= ARTIFACT_POWERS[i]` from `tables.h`.

- `static void combat_prepare_foe(Combat *c, const CombatTarget *t)`:
  Per OPENKB §17.2 line 5216 — limit to first 3 of 5 slots (`max_troops = 3`). Read `t->garrison[0..2]`; place at `x = COMBAT_W - 1 = 5`, `y = i`. spoils[1] accumulated same formula.

- `static void combat_prepare_castle(Combat *c, const CombatTarget *t)`:
  Per OPENKB §17.2 line 5217 — uses 5 slots for castles. spoils + placement happens in `reset_match`'s castle branch.

- `static void combat_reset_match(Combat *c, bool castle)`:
  Per OPENKB §17.2 lines 5219–5230, §17.15 lines 5882–5901:
  - **Castle layout**: place AI units at fixed positions per the literal grid (OPENKB lines 5235–5249). Decimal table:
    ```
    castle_umap[5][6] = {
        {0, 8, 6, 7, 9, 0},   // row 0
        {0, 0,10, 0, 0, 0},
        {0, 0, 0, 0, 0, 0},
        {0, 5, 3, 4, 0, 0},
        {0, 0, 1, 2, 0, 0},   // row 4
    };
    castle_omap[5][6] = {     // wall obstacle types (preserved verbatim)
        {8, 0, 0, 0, 0, 9},
        {8, 0, 0, 0, 0, 9},
        {8, 0, 0, 0, 0, 9},
        {8, 0, 0, 0, 0, 9},
        {5, 7, 0, 0,10, 6},
    };
    ```
    Implement as `static const unsigned char castle_umap[5][6] = {...};` decoded from UID = `side*5 + id + 1`. So UID 1..5 = player slots 0..4; UID 6..10 = AI slots 0..4.
  - **Open-field**: per OPENKB §17.14 (lines 5862–5871):
    ```
    for j in 0..CLEVEL_H-1:
        for i in 1..CLEVEL_W-3:        // i ∈ {1, 2, 3}
            if !(rng() % 10):
                omap[j][i] = (rng() % 3) + 1
    ```
    The `i` range is `1..CLEVEL_W-3` inclusive of both endpoints — that's columns 1, 2, 3 of the 6-wide grid (per spec note line 5872 "cols 1..3 (4 of 6 columns)" — this contradicts the inclusive math which gives 3 cols; **OPEN QUESTION 14.1**).
  - For each unit with `count > 0`: `shots = troop->ranged_ammo`, `injury = 0`, `frozen = 0`. Write `umap[y][x] = side*5 + slot + 1`.
  - Call `combat_reset_turn(c)`.

- `static void combat_reset_turn(Combat *c)` (skeletal — Phase 3 fills it in fully).

- The `RunCombat` body, replacing Phase 1's no-op:
  ```c
  Combat c = {0};
  combat_seed_rng(&c, g);                        // Phase 3
  combat_prepare_player(&c, g);
  if (mode == COMBAT_MODE_CASTLE) combat_prepare_castle(&c, target);
  else                            combat_prepare_foe(&c, target);
  combat_reset_match(&c, mode == COMBAT_MODE_CASTLE);
  // combat_loop(&c) — Phase 7
  return COMBAT_RESULT_WIN;
  ```

**Files touched**: `src/combat.c` only.

**Verification**:
- Add a debug print (gated on `--combat-trace` CLI flag) that dumps `c.umap` and `c.omap` after `combat_reset_match`.
- Run a castle siege; assert player UIDs land on rows 3-4 and AI UIDs on rows 0-1, with castle walls at columns 0 and 5.
- Run an open-field combat; assert player at column 0 / AI at column 5, ≤ 3 stacks on AI side, 1–2 obstacles in cols 1–3.
- A scripted unit test (Phase 14) seeds a known RNG and validates exact obstacle placement.

**Dependencies**: Phase 1.

---

### Phase 3 — RNG, distance helpers, friendliness, turn structure

**Goal**: deterministic RNG; the per-turn `combat_reset_turn`; friendliness predicates; `next_unit` / `next_turn`; `isqrt32` and `calc_distance`.

**Spec citations**: OPENKB §17.3 (turn structure, lines 5252–5270); §17.4 (control flags, 5272–5282); §17.11.1 (isqrt32, 5733–5752); §17.11.2 (calc_distance, 5766–5772); OPENBOUNTY §13.1 (RNG conventions; existing `game_rng_next` in `src/game.c:14-29`).

**Deliverables**:

- `static void combat_seed_rng(Combat *c, const Game *g)`: derives the combat seed from `g->seed XOR g->stats.followers_killed XOR (mode-specific salt)`. **The combat LCG MUST be separate from the world-init LCG** — rolling combat RNG mid-game must not change post-combat overworld behavior. Implementation copies the constants from `game.c:23-29` (multiplier 25214903917, increment 11, mask `0xFFFFFFFFFFFFFFFFUL`).
- `static int kb_rand(Combat *c, int min, int max)`: inclusive in both bounds; returns `min + (state>>32) % (max-min+1)`. This is the combat-engine equivalent of OPENKB's `KB_rand`.

- Distance helpers (OPENKB §17.11):
  ```c
  static unsigned long isqrt32(unsigned long h);          // exact transcription of spec lines 5734–5751
  static unsigned long calc_distance(int x1, int y1, int x2, int y2);
  ```
  Used by combat AI (Phase 10) and `unit_closest_offset`. Do NOT replace with `sqrtf` — tie-breaking semantics depend on the exact integer fixed-point digit-by-digit walk (OPENKB §17.11.1 line 5754).

- Friendliness predicates (OPENKB §17.4 lines 5272–5282):
  ```c
  static bool unit_under_control(const Game *g, int slot);
  static bool units_are_friendly(const Combat *c, int sA, int iA, int sB, int iB);
  ```
  - `unit_under_control` returns 1 iff the player has leadership cap room for the troop (`army_leadership(g, troop) > 0` in OPENKB; in OPENBOUNTY this is `GameArmyTotalLeadership(g) >= troop->hp * count`). The AI side has no hero (heroes[1] = NULL), so all AI units are "in control" trivially.
  - `units_are_friendly` returns false if either is OOC, else equal sides only.

- Turn helpers (OPENKB §17.3):
  ```c
  static void combat_reset_turn(Combat *c);
  static int  combat_next_unit(Combat *c);   // returns slot or -1
  static void combat_next_turn(Combat *c);
  ```
  Match OPENKB pseudocode line-for-line:
  - **reset_turn** (5254–5263): for each unit with count > 0, `turn_count = count`; `retaliated = 0`; `acted = 0`; `moves = troop->move_rate`; `flights = (abilities & FLY) ? 2 : 0`. If `side == 1` (end of AI turn → about to start player's next turn): unfreeze all (`frozen = 0`); REGEN units zero `injury`. `phase = 0; spells_this_round = 0`.
  - **next_unit** (5264–5267): scan `[unit_id+1..4]` then wrap with `phase++` and scan `[0..4]`; pick first with `count > 0 && !acted`. Recompute `out_of_control` *at this moment* (re-evaluate leadership) — important because mid-battle losses may push units out of control. Return slot or -1.
  - **next_turn** (5268–5270): toggle `side`, call reset_turn, find first living unit on new side.

**Files touched**: `src/combat.c`.

**Verification**:
- Unit test: seed RNG with `0xDEADBEEF`, generate 1000 `kb_rand(1,100)` calls, check histogram is uniform within 5%.
- Unit test: `isqrt32(2 << 16) == 1.41421 << 16` to integer rounding — exercise the same input the spec example uses (line 5760).
- Manual trace of a 2-stack vs 2-stack reset_turn: confirm flights = 2 for FLY, 0 otherwise; confirm REGEN zeroes injury at side==1 only.

**Dependencies**: Phase 1, 2.

---

### Phase 4 — Damage formula `combat_deal_damage`

**Goal**: the heart of the engine. A pure function that, given attacker/target/flags, applies damage and returns kills. The single largest correctness risk in the project — the formula has 12 distinct multiplicative steps and 4 ability-driven branches.

**Spec citations**: OPENKB §17.5 in entirety (lines 5284–5540), with primary focus on §17.5.1 step list (5308–5360). Secondary: §17.5.2 (formula breakdown, 5362–5398), §17.5.3 (`turn_count` rationale, 5400–5421), §17.5.4 (Demon Scythe, 5424–5441), §17.5.5 (MAGIC ranged, 5444–5456), §17.5.6 (Retaliation, 5458–5479), §17.5.7 (Ghosts ABSORB, 5482–5497), §17.5.8 (Vampires LEECH, 5500–5514), §17.5.9 (followers_killed, 5517–5528), §17.5.10 (edge cases, 5530–5540).

**Deliverable**:

```c
// Returns: number of full kills (creatures killed). -1 if attack was cancelled
// (MAGIC vs IMMUNE).
static int combat_deal_damage(Combat *c,
                              int a_side, int a_id,
                              int t_side, int t_id,
                              bool is_ranged,
                              bool is_external,
                              int  external_damage,
                              bool retaliation);
```

Implementation MUST follow OPENKB §17.5.1 exactly:

1. `demon_kills = 0`. (Line 5310.)
2. If `is_external`: `final_damage = external_damage`. Skip step 3. (5311–5312.)
3. Unit-vs-unit:
   - SCYTHE roll: `if ((u.abilities & ABIL_SCYTHE) && kb_rand(1,100) > 89): demon_kills = (t.count + 1) / 2;` (5314–5317). Note: integer ceil = `(n + 1) / 2` for positive n; spec line 5316 spells out `t.count / 2 + (t.count % 2 ? 1 : 0)`.
   - Ranged: set `retaliation = true`; `u.shots--`. Then:
     - MAGIC + IMMUNE → return -1 (5321–5322). DO NOT decrement shots in this branch — but spec line 5320 already decrements before the IMMUNE check; the test harness must verify which order is canonical. **OPEN QUESTION 14.2** — likely shots decrement happens BEFORE the IMMUNE check per literal spec ordering.
     - MAGIC w/o IMMUNE: `dmg = troop->ranged_min` (fixed). (5323–5324.)
     - else: `dmg = kb_rand(troop->ranged_min, troop->ranged_max)`.
   - Melee: `dmg = kb_rand(melee_min, melee_max)`. (5326.)
   - `total = dmg * u.turn_count`. (5327. Use `turn_count`, not `count`; see §17.5.3.)
   - `skill_diff = u.troop.skill_level + 5 - t.troop.skill_level`. (5328.)
   - `final_damage = (total * skill_diff) / 10`. (Integer division, drops fraction. 5329.)
   - **Morale** (only if attacker side has hero AND attacker is in control, line 5330):
     - Look up morale via `troop_morale(hero, a_id)` — see Phase 4.5 below.
     - `MORALE_LOW (1)` → `final_damage /= 2`. (5332.)
     - `MORALE_HIGH (2)` → `final_damage += final_damage / 2` (×1.5). (5333.)
     - `MORALE_NORMAL (0)` → no change.
   - `POWER_INCREASED_DAMAGE` (sword, line 5335): `final_damage += final_damage / 2`.
   - `POWER_QUARTER_PROTECTION` (shield on TARGET side, line 5337): `final_damage = (final_damage / 4) * 3`. **Integer-first floor — spec line 5340 explicitly notes heavy damage shrinks to 75%, small damage can vanish to 0.**
4. `final_damage += t.injury`. (5341.)
5. `final_damage += troop_t->hp * demon_kills`. (5342.)
6. `kills = final_damage / hp_t`. (5344.)
7. `injury_after = final_damage % hp_t`; `t.injury = injury_after`. (5345–5346.)
8. If `kills < t.count`: `t.count -= kills`. Else (stack dies): `t.dead = true; t.count = 0; kills = t.turn_count; final_damage = kills * hp_t`. (5347–5350.) The recompute is critical for ABSORB/LEECH (§17.5.10 line 5532).
9. If `c->heroes[t_side]`: `c->heroes[t_side]->stats.followers_killed += kills` (5351). NB: only player side has a hero; AI side increment is a no-op via NULL guard.
10. If `!is_external`:
    - ABSORB: `u.count += kills` (5353; uses kills not raw damage — spec §17.5.7 line 5491 elaborates).
    - LEECH: `u.count += final_damage / hp_attacker`; clamp to `max_count`; on clamp `injury = 0` (5354–5356; §17.5.8).
    - Retaliation: `if (!retaliation && !t.retaliated) { t.retaliated = true; combat_deal_damage(c, t_side, t_id, a_side, a_id, false, false, 0, true); }` (5357–5359, §17.5.6). Note ranged path already set retaliation=true earlier so it skips here.
11. Return `kills`.

**Phase 4.5 — `troop_morale` lookup**:

- OPENBOUNTY already has `combat.morale_chart` in `game.json` (5×5 N/L/H grid loaded into `Resources.combat.morale_chart`).
- Function: `static int combat_morale(const Game *g, int slot)`:
  - For each occupied stack `i ≠ slot`: read `morale_chart[my_group][their_group]`. Track lowest result: H > N > L (i.e. one L wipes out everything). Return `MORALE_LOW=1`, `MORALE_NORMAL=0`, `MORALE_HIGH=2`. Per OPENKB §3.6 (lines 736–738) the cnv array `{LOW=1, NORMAL=0, HIGH=2}` encodes the strength comparison.

**Files touched**: `src/combat.c`. Possibly add a `static int troop_index_of(const Unit*)` helper if not already present.

**Verification**:
- **Golden table**: a hand-computed 12-row table of canonical attacks (e.g. 100 Knights vs 50 Knights, 100 Peasants vs 5 Dragons, Demon vs Peasants with SCYTHE rolling 90, Druid vs Dragon (IMMUNE), Vampire vs Peasants (LEECH)) and the expected `final_damage`, `kills`, `t.injury`, `u.count` after. This table goes in `tools/combat_golden.c` (Phase 14).
- **Symmetric retaliation**: 50 Knights attacks 50 Knights, both sides should take the same expected damage modulo RNG. Assert.
- **MAGIC vs IMMUNE returns -1**: assert no state change.
- **t.injury accumulation**: two consecutive sub-lethal hits should kill the same total creatures as one hit dealing the sum.
- **morale ×0.5 (LOW) and ×1.5 (HIGH)** vs no morale: assert ratio with seeded RNG.
- **kills clamp to turn_count when stack dies**: 100 dmg overkill on a 5-stack of 1-HP Peasants returns kills=5, not 100.

**Dependencies**: Phases 1–3.

---

### Phase 5 — Movement, flight, melee driver, retaliation, compact

**Goal**: implement the action drivers that translate "player presses arrow at unit at (x,y)" into damage applied to the field.

**Spec citations**: OPENKB §17.6 entire (lines 5542–5599); §17.8 (compact_units, 5615–5620); §17.13 (unit_fly_offset, 5836–5854).

**Deliverables**:

- `static int combat_move_unit(Combat *c, int side, int id, int dx, int dy)` — OPENKB §17.6 lines 5544–5554.
  - Bounds: `nx = u.x + dx`, `ny = u.y + dy`; reject out of `[0,COMBAT_W) × [0,COMBAT_H)` → return 0.
  - Obstacle (`omap[ny][nx] != 0`) → return 0.
  - Other unit (`umap[ny][nx] != 0`):
    - Decode UID into `(side, id)`. If `units_are_friendly` AND both in control, return 0 (can't push allies).
    - Else: call `combat_hit_unit(c, side, id, other_side, other_id)`; return 2.
  - Else: relocate (`umap[u.y][u.x] = 0; umap[ny][nx] = uid; u.x = nx; u.y = ny`). `u.moves--`; if 0, `u.acted = true`; return 1.

- `static int combat_fly_unit(Combat *c, int side, int id, int nx, int ny)` — OPENKB §17.6 lines 5556–5562. Target is pre-validated by `pick_target`. Relocate; `u.flights--`; if 0, `acted = true`; log `"<Troop> fly"`.

- `static void combat_hit_unit(Combat *c, int aS, int aI, int tS, int tI)` — OPENKB §17.6 lines 5563–5573, §17.6.1 lines 5574–5586:
  - Snapshot `turn_count = count` for both attacker and target BEFORE damage (§17.6.1 line 5577 — critical for retaliation symmetry).
  - `kills = combat_deal_damage(c, aS, aI, tS, tI, false, false, 0, false)`.
  - Log `"%s vs %s, %d die"` (banner; OPENKB §24.43 line 8235).
  - Retaliation handled inside `deal_damage` via the recursive call; banner for the retaliation reads `"%s retaliate, killing %d"` (§24.43 line 8236) — emit it after `deal_damage` returns based on whether `t.retaliated` is now true and the recursive call did damage. Note: per §17.5.6 retaliation is *inside* `deal_damage`, but the banner "retaliate" must reflect post-retaliation state — wrap a small helper that emits both banners.
  - `u.acted = true`. **NOT** `u.moves = 0` — moves is only zeroed by exhausting moves; spec line 5571 says set acted=true after the attack drives the swing (5571).
  - `combat_compact_units(c)` — see below.

- `static void combat_compact_units(Combat *c)` — OPENKB §17.8 (lines 5615–5620):
  - Walk `units[s][0..4]`; if `dead`, leave troop but mark for visual removal. The spec is vague on whether stacks are physically reordered or just hidden — OPENKB line 5616 says "shifts trailing stacks up". For OPENBOUNTY we DO NOT physically reorder slot indices (they're tied to umap UIDs); instead, set `dead`-flagged units to `count = 0`, troop_idx unchanged so `accept_units` later writes back in the same slot order.
  - **Pure spec-faithful path** (preferred for parity): replicate OpenKB's reorder so AI / target-selection iterate compactly. **OPEN QUESTION 14.3**: does the slot-reorder change AI target preference? If yes, must match OPENKB semantics.
  - Wipe and rewrite `umap` from surviving units' (x,y).

- `static void combat_unit_fly_offset(...)` — OPENKB §17.13 lines 5840–5852. Used by Phase 10's flying AI.

**Files touched**: `src/combat.c`.

**Verification**:
- Set up two adjacent stacks on a stripped-down field; call `combat_move_unit` toward target, assert `hit_unit` triggers and damage applied both directions.
- Test `compact_units`: a 3-stack player army with middle stack reduced to 0 — confirm umap reflects the surviving stacks at correct positions.
- Test obstacle blocking: unit moves toward obstacle, assert `moves` unchanged and unit stays put.
- Test friendly-block: unit moves into friendly, assert blocked.

**Dependencies**: Phases 1–4.

---

### Phase 6 — Ranged shot, MAGIC, surround check

**Goal**: Implement `unit_try_shoot`, `unit_surrounded`, ranged-attack flow.

**Spec citations**: OPENKB §17.7 entire (lines 5601–5614); §5.2.6 (lines 1513–1531) for ranged ammo behavior; §17.5.5 for MAGIC ranged.

**Deliverables**:

- `static bool combat_unit_surrounded(const Combat *c, int side, int id)`:
  Per OPENKB §17.7 line 5608 — true if any enemy unit in 8-neighborhood. Implementation: scan 8 deltas, check `umap`, decode UID, return true if not friendly.
- `static int combat_try_shoot(Combat *c, int side, int id, int tx, int ty)`:
  Per OPENKB §17.7 lines 5606–5613:
  - If `u.shots == 0 || combat_unit_surrounded(...)` → emit banner `"Can't Shoot"` (§24.43 line 8254) and return 0.
  - Resolve `(tx,ty)` to enemy unit via umap; if not enemy, return 0.
  - If MAGIC + target has IMMUNE: emit `"<Atk> shoot <Tgt>"` then `"The spell seems to have no effect!"` (§24.43 lines 8238–8239). Set `u.acted = true`. Return 0.
  - Else: call `combat_deal_damage(..., is_ranged=true)`. Banner `"%s shoot %s killing %d"` (§24.43 line 8237). `u.acted = true`. `compact_units`.

**Files touched**: `src/combat.c`.

**Verification**:
- Druid (MAGIC, 10 dmg fixed) vs Dragon (IMMUNE) → returns -1, shots NOT decremented (per OPENKB cancel semantics, line 5322 cancels before `u.shots--`? **Open question 14.2 again**). Compare to Druid vs Peasants → 10 dmg per attacker, `u.shots--`.
- Surround check: place enemy adjacent to ranged unit, shooting must be refused.
- Ammo depletion: shoot 12 times with Archers (12 ammo); 13th attempt gets "Can't Shoot".

**Dependencies**: Phases 1–5.

---

### Phase 7 — Input + grid heuristic + action menu

**Goal**: keyboard routes to game actions; the combat loop drives action dispatch. **OpenBounty is keyboard-only by design** — no mouse support is provided or planned, regardless of what OpenKB offers.

**Spec citations**: OPENKB §17.9 (combat_loop, lines 5622–5680); §19.4 (grid_heuristic, 6509–6523); §31.2 (combat key bindings, 9692–9719); OPENBOUNTY §31.2 (combat bindings, 2676–2693). Visual reference: `legacy/screenshots/kb_038.png` (combat options listing).

**Deliverables**:

- `static void combat_loop(Combat *c)`: the main loop. Spec OPENKB §17.9.1 lines 5641–5656:
  1. Render frame (Phase 8).
  2. If AI's turn or active player unit OOC: tick AI (`combat_ai_unit_think`) every 4th anim frame to mimic OPENKB ~600ms cadence (§17.9.2 line 5670). Otherwise poll input.
  3. Translate input to action:
     | Key | Action | OPENKB §31.2 line |
     |-----|--------|---|
     | Arrows / numpad 1,2,3,4,6,7,8,9 / Home/End/PgUp/PgDn | move 1 tile in 8 directions | 9695–9702 |
     | A | view army (push existing VIEW_ARMY) | 9703 |
     | C | Controls (push VIEW_CONTROLS) | 9704 |
     | F | Fly (`combat_try_fly`) | 9705 |
     | G | Give Up (yes/no prompt → COMBAT_RESULT_FLEE) | 9706 |
     | S | Shoot (target picker filter 4) | 9707 |
     | U | Use Magic (combat spell menu) | 9708 |
     | V | View Character (push VIEW_CHARACTER) | 9709 |
     | W | Wait (defer to later in same turn) | 9710 |
     | Space | Pass (skip turn) | 9711 |
     | O | Options menu | 9712 |
     | F10 | Cheat menu (omit in release; **OPEN QUESTION 14.4**) | 9713 |
     | SYN tick | animation frame | 9715 |

     OpenKB §31.2 line 9714 also lists a "mouse click on grid" path that
     dispatches via `grid_heuristic`. OpenBounty is keyboard-only and
     does not implement this. `grid_heuristic` itself is unused.
  4. After action: if active unit's count==0, skip; if !test_defeat → done=2 (AI win); if !test_victory → done=1 (player win); else `combat_next_unit` and on phase-end `combat_next_turn`.
  5. Loop until `done != 0`.

- `combat_grid_heuristic` (OPENKB §19.4): NOT implemented. It exists in
  OpenKB to translate a clicked grid cell into a player intent (move /
  shoot / melee / fly). OpenBounty is keyboard-only, so the player
  drives intent directly via the keys above; no heuristic is needed.

- Wait/Pass: `combat_try_wait(c)` → set acted but allow re-process in phase 2 (OPENKB §3.10 line 819: "wait then act"); `combat_try_pass(c)` → set acted permanently; emit banner `"<Troop> wait"` / `"<Troop> pass"` (§24.43 lines 8242–8243).

- Give Up flow: opens `prompt_yes_no_open` with `strings.combat.give_up_prompt` (§24.33 line 8118). Yes → return COMBAT_RESULT_FLEE (caller treats this like LOSS for temp_death, per §17.10 line 5656).

**Files touched**:
- `src/combat.c` (main).
- `src/views.h` and `src/classic/views.c`: add `VIEW_COMBAT` enum value, dispatch case (Phase 8 actually draws it).
- `src/main.c`: while combat is active, the adventure-loop branch should not run — `RunCombat` is blocking and drives its own `BeginDrawing`/input pump. Verify the existing pattern (no changes needed beyond this assumption).

**Verification**:
- Open-field combat, walk the arrow keys: confirm all 8 directions move 1 tile.
- Step onto adjacent enemy: melee triggers via `combat_move_unit`'s collision branch.
- Press F on a flyer toward a far empty cell: unit flies; on a non-flyer, banner "Can't Fly".
- Press Space at a player turn: turn skipped, next unit selected.

**Dependencies**: Phases 1–6.

---

### Phase 8 — Renderer (battlefield, sprites, status, banner)

**Goal**: render the 6×5 combat field by **loading and blitting the OpenKB combat tileset directly**. The renderer is a thin asset-driven layer: `omap[y][x]` becomes a tileset frame index; `umap[y][x]` becomes a unit-sprite blit; cursors are tileset frames 11–14. No procedural drawing, no invented sprites, no field-truth gaps.

**Asset source — read this carefully, this is the entire premise of the phase**:

The OpenKB combat tileset is already extracted to `legacy/artifacts/assets/tilesets/combat/vga/frame_00.png`..`frame_14.png`. 15 frames at 48×34. The renderer's job is to **copy these into `assets/kings-bounty/art/combat/`** and load them into a `combat_tile[15]` array on the `Sprites` struct. Then every visual element of the field comes from indexing that array. The codes that index it are the same `omap` byte values OPENKB §17.14 / §17.15 already specifies — there's no translation layer.

| Frame | Use | Driven by |
|-------|-----|-----------|
| 0     | Field grass background — tiled across every cell (both modes) | always |
| 1     | Random obstacle: boulder | `omap` value 1 (OPENKB §17.14 line 5868) |
| 2     | Random obstacle: tree cluster | `omap` value 2 |
| 3     | Random obstacle: mound | `omap` value 3 |
| 4     | Castle ornament (decorative) | `omap` value 4 (rare) |
| 5     | Castle wall — bottom-left corner | `castle_omap[4][0] = 5`, OPENKB §17.15 |
| 6     | Castle wall — bottom-right corner | `castle_omap[4][5] = 6` |
| 7     | Castle wall — bottom segment | `castle_omap[4][1] = 7` |
| 8     | Castle wall — left side | `castle_omap[*][0] = 8` |
| 9     | Castle wall — right side | `castle_omap[*][5] = 9` |
| 10    | Castle wall — gate / portcullis | `castle_omap[4][4] = 10` |
| 11    | Cursor: active-unit ring | rendered at `units[side][unit_id]` |
| 12    | Cursor: movable target ring | rendered at `cursor_x, cursor_y` |
| 13    | Cursor: shoot ring | rendered at the picker target during shoot mode |
| 14    | Cursor: small accent (animation pair to 11–13) | optional anim |

The wall codes in `castle_omap` are not arbitrary numbers; OPENKB §17.15 chose them precisely because they are tileset indices. The plan's earlier text treated them as opaque byte tags — that was wrong. They are direct sprite IDs.

**Spec citations**: OPENKB §17.14 (random obstacles, lines 5862–5878); §17.15 (`castle_omap` literal, 5882–5901); §23.4 (status-bar text format, 7510–7518); §29 (DOS asset format / GR_TILESET, identifies the combat tileset). OPENBOUNTY §3.10 (combat grid, 528–535).

**Visual references**: `legacy/screenshots/kb_074.png` (castle siege), `kb_075.png` (open field), `kb_078.png` (post-first-kill title bar). These are confirmation, not derivation — the assets define the look.

**Layout (320×200 design space; all the rest of the engine renders here, blit-scaled to 640×400 in `main.c`)**:

| Region | Pixel rect | Notes |
|--------|------------|-------|
| Top chrome (yellow border + status bar + difficulty strip + bar) | y=0..21 | reuses `classic_chrome_draw_with_status` |
| Combat field | x=16..303, y=22..191 (288×170) | 6 cells × 5 cells × 48×34 px each |
| Bottom chrome (yellow border) | y=192..199 | reuses chrome bitmap |

Cell math: `(col, row)` → screen `(16 + col*48, 22 + row*34)`. 48×34 matches the tileset and the troop sprites exactly. No overflow, no scaling.

**Deliverables**:

- `src/sprites.h` / `src/sprites.c`: add `Texture2D combat_tile[15]` to the `Sprites` struct; load from `assets/kings-bounty/art/combat/frame_NN.png`; unload alongside other textures.
- `assets/kings-bounty/art/combat/frame_00.png`..`frame_14.png`: 15 files copied verbatim from `legacy/artifacts/assets/tilesets/combat/vga/`.
- `src/classic/chrome.h` / `chrome.c`: add `classic_chrome_draw_with_status(g, sprites, status_text)` — same as `classic_chrome_draw` but takes an explicit status string. Combat uses this so the title bar reads `Options / <Actor> M<n>` (pre-first-kill) or `<Player> vs <Foe> killing <N>` (post-first-kill, see §8.4) without polluting the adventure-mode time-stop / days-left paths.
- `src/combat_render.h`:
  - Constants: `CL_COMBAT_X=16`, `CL_COMBAT_Y=22`, `CL_COMBAT_W=288`, `CL_COMBAT_H=170`, `CL_COMBAT_CELL_W=48`, `CL_COMBAT_CELL_H=34`.
  - Public API: `combat_render_frame(const Combat*, const Game*, const Sprites*)`, `combat_log(Combat*, const char *fmt, ...)`, `combat_format_title(const Combat*, const Game*, char *buf, int cap)`.
- `src/combat_render.c`:
  - **Render order**: black backdrop → tile field with `combat_tile[0]` (grass, every cell) → stamp `combat_tile[omap[y][x]]` for every non-zero cell → blit unit sprites (player faces right, AI faces left via mirrored source rect) → count badge under each unit (white digits on black band, anchored bottom-center of cell) → cursor frame 11 at active unit → cursor frame 12 at `cursor_x, cursor_y` → `classic_chrome_draw_with_status` last so the title-bar text overlays.
  - **No procedural rectangles for the field**, no invented colors. Field colour comes from the tileset.
- Animation cadence is already in Phase 7's main loop (`combat_tick_anim`, 150ms SYN tick). Phase 8 just reads `u->frame` and indexes into `s->troop_anim[idx][frame]`.

**What is intentionally NOT in this phase**:

- Modal overlays (spells, controls, victory) — those are Phases 12, 13, 11 respectively.
- Attack-swing / projectile / spell-flash animations — defer to a follow-on. The OpenKB tileset doesn't include attack/projectile sprites; spec §17.9 says "no smooth interpolation; units teleport between tiles." Damage events feed `combat_log` and the banner; a future polish pass can add visual hits.

**Verification**:

- Build green.
- Field cells render at exactly 48×34 with no overflow or seams.
- `omap` codes 1..3 paint the OpenKB obstacle frames in their assigned cells.
- `castle_omap` walls render as the OpenKB wall pieces (not coloured rectangles).
- Active-unit ring (frame 11) and target cursor (frame 12) appear as the OpenKB cursor sprites, not 1-pixel outlines.
- Title bar reads through the existing classic chrome: same yellow border, same difficulty-coloured status fill, same multi-colour bar strip below — combat is visually continuous with adventure mode.

**Files touched**:
- `src/sprites.{c,h}`, `src/classic/chrome.{c,h}`, `src/combat_render.{c,h}` (new), `Makefile` (add `combat_render.c`), `assets/kings-bounty/art/combat/*` (15 PNGs).

**Dependencies**: Phases 1–7.

---

### Phase 9 — Target picker (`pick_target`)

**Goal**: a modal sub-state that lets the player pick a tile under filter constraints. Used by Shoot, all 7 combat spells.

**Spec citations**: OPENKB §18.15 (lines 6076–6094); §31.3 (target_state, lines 9720–9727).

**Deliverables**:

- `static int combat_pick_target(Combat *c, int *out_x, int *out_y, int filter);`
  Per OPENKB §18.15:
  - Filter values (lines 6082–6090):
    - 0: any tile.
    - 1: unoccupied (no unit, no obstacle).
    - 2: any unit.
    - 3: friendly unit.
    - 4: enemy unit (or OOC friendly).
    - 5: enemy undead — implementation SHOULD use this for Turn Undead even though OPENKB code passes 4 there (§18.7 line 6002 notes the bug — OPENBOUNTY fixes it; **but parity says match the bug** — leave as filter 4 with a TODO and mention in §34 of OPENBOUNTY-SPEC).
  - UI: render combat field, plus an animated cursor sprite (frames 11..14 of comtiles per §18.15 line 6094 — use a 4-frame cycle; reusable PNG asset).
  - Input loop: arrow keys move cursor; Enter confirms (only if filter passes); Esc cancels (return 0).
  - Status bar shows the spell's target prompt: `"Select your army to Clone"` etc. (§24.43 line 8253).

**Files touched**: `src/combat.c`. No new files.

**Verification**:
- Cast Freeze (filter 4): confirm cursor cannot land on friendly stack.
- Cast Clone (filter 3): confirm cursor cannot land on enemy.
- Cast Teleport: pick source (filter 2); pick destination (filter 1, must be empty + non-obstacle).
- Esc cancels picker without consuming the spell.

**Dependencies**: Phases 1–8.

---

### Phase 10 — Combat AI (`ai_unit_think`, `ai_pick_target`, `unit_closest_offset`)

**Goal**: AI-side units act each turn; OOC player units also use AI.

**Spec citations**: OPENKB §17.12 (unit_closest_offset, lines 5783–5822); §17.13 (unit_fly_offset, 5836–5852); §19.2 (ai_unit_think, 6473–6489); §19.3 (ai_pick_target, 6491–6507); §19.1.9 differences from foe pathfinder (6454–6471).

**Deliverables**:

- `static void combat_unit_closest_offset(const Combat *c, int side, int id, int px, int py, int ox, int oy, int tx, int ty, int *odx, int *ody);`
  Implement OPENKB §17.12 lines 5791–5822 line-for-line. **The iteration order is `j = +1, 0, -1` and `i = -1, 0, +1` — the REVERSE-j order vs the foe pathfinder** (§19.1.9 line 6460); this affects tie-breaking. Tie-break: combat units prefer SW > S > SE > W > stay > E > NW > N > NE.

- `static int combat_ai_pick_target(const Combat *c, int side, int id, bool nearby);`
  Per OPENKB §19.3:
  - Scan both sides, all 5 slots.
  - Skip count==0; skip own slot; if under_control skip own side; if OOC, attack allies too.
  - If `nearby`: must be `unit_touching` (Chebyshev ≤ 1).
  - Prefer shooters for FAR targets; lower-HP for NEAR.
  - Returns packed UID (`PACK_UID = side*5 + id + 1`) or -1.

- `static void combat_ai_unit_think(Combat *c);`
  Per OPENKB §19.2 lines 6475–6489:
  1. `close = ai_pick_target(c, side, id, nearby=true)`.
  2. If frozen: `acted = true`; banner `"<Troop> are frozen"` (§24.43 line 8244); return.
  3. If shots > 0 AND close == -1: `far = ai_pick_target(c, side, id, nearby=false)`. If found: ranged shot.
  4. If FLY and close == -1: `unit_fly_offset` to far target's adjacency; `fly_unit`.
  5. Else (no shots, no fly, or close != -1): if close == -1, use far. Compute `unit_closest_offset` toward target; `move_unit(dx, dy)`. (Movement may resolve into melee via the `umap`-collision branch of `move_unit`.)
  6. If nothing happened: `combat_try_wait(c)`.

**Files touched**: `src/combat.c`.

**Verification**:
- Place 1 AI Knight + 1 AI Archer; run a turn; confirm Knight moves toward player, Archer shoots if enemy in range.
- Place a flyer (Sprite) far from any target; confirm it flies adjacent.
- Place a frozen unit; confirm it sets `acted = true` without moving.
- Place an OOC player Vampire; confirm it attacks own-side units.

**Dependencies**: Phases 1–9.

---

### Phase 11 — End of combat: victory, defeat, spoils, accept_units

**Goal**: when combat resolves, write back the surviving army to `Game`, award gold, trigger ending dialog using the **same `open_dialog` mechanism the rest of the engine uses** so the panel chrome matches kb_066.

**Modal chrome**: OPENBOUNTY already renders modal dialog panels procedurally — see `src/ui.c::open_dialog` and `src/classic/prompt.c`. The blue panel + yellow border + gold title in kb_066 is built from `PAL_CLR(DBLUE)` for the panel fill, `PAL_CLR(YELLOW)` for the 1-px border, `PAL_CLR(YELLOW)` (or gold) for the title text, `PAL_CLR(WHITE)` for the body text. Reuse `open_dialog`; do NOT roll a custom panel renderer.

**Spec citations**: OPENKB §17.10 (lines 5681–5727); §17.16 (spoils, 5905–5922); §20.7 (foe encounters, 6705–6720); §22 (save load preserves followers_killed); OPENBOUNTY §17 lines 2039–2042 (current call-sites in main.c). Visual: kb_066.png (victory), kb_096.png (defeat).

**Deliverables**:

- `static void combat_test_victory(const Combat *c, int *winner);`
  Returns 0 = running, 1 = player won (AI side has zero count), 2 = AI won (player side has zero count).

- `static void combat_accept_units_player(const Combat *c, Game *g);`
  After combat ends, for each player slot 0..4 in `c->units[0]`: if count > 0, write back to `g->army[i].id` and `count`. Empty slots → clear `g->army[i].id[0] = 0`. **Important**: maintain slot ordering identical to entry — the player should see the same slots in the same positions. (See OPENKB §17.8 line 5619 — `accept_units_player` uses post-compact ordering. **OPEN QUESTION 14.10** — does compact reorder slots that survive? If so, player's slot 2 might shift to slot 1 after losing slot 1; this is the OpenKB observable behavior we want to match.)

- `static void combat_apply_spoils(const Combat *c, Game *g, CombatMode mode);`
  Per OPENKB §17.10 line 5697: on player win, `g->stats.gold += c->spoils[1]` (AI side). Player-side spoils discarded.

- Connect `RunCombat` outcome:
  - Outcome WIN (mode FOE): caller (main.c line 2136) clears foe tile; `villain_id = -1`.
  - Outcome WIN (mode CASTLE villain branch): main.c already calls `GameFulfillContract` and `GameMaybeRankUp` (lines 2112–2114). The combat module just returns; main.c handles the post-combat reward.
  - Outcome LOSS / FLEE: main.c calls `perform_temp_death` (already wired at line 2065/2116/2142).

- Victory dialog (OPENKB §17.10 lines 5716–5727; §24.35 line 8136). The existing main.c flow will call `open_dialog` post-combat with the spoils text. The combat module must populate `c->spoils[1]` accurately for the dialog to render the correct gold amount. Match kb_066:
  ```
              Victory!
  Well done <Name> the <Title>,
  you have successfully vanquished
  yet another foe.
  Spoils of War: <N> gold[ and the
  capture of <VillainName>]
  [For fulfilling your contract you
  receive an additional <N> gold ...]
  ```
  Use existing `strings.win_text.body` template substitution (`%NAME%`, `%TITLE%`, `%GOLD%`, `%VILLAIN%`).

**Files touched**: `src/combat.c`; `src/main.c` (verify spoils flow from `RunCombat` return — possibly add a `spoils_out` parameter to `RunCombat` or expose via `g->stats.gold` delta after the call).

**Verification**:
- Win a foe encounter: gold increases by exactly `Σ(spoils_factor × 5 × count)` for AI side troops at battle start.
- Win a castle siege with the contracted villain inside: contract closed, rank-up triggered.
- Lose a combat: temp_death (existing path), army reset to 20 Peasants on continent 0 at home.

**Dependencies**: Phases 1–10.

---

### Phase 12 — Combat spells (Clone, Teleport, Fireball, Lightning, Freeze, Resurrect, Turn Undead)

**Goal**: Implement all 7 combat spells. **OpenKB itself stubs Fireball/Lightning/Resurrect/Turn Undead** (§18.3, §18.4, §18.6, §18.7 — see lines 5972, 5979, 5995, 6002). For OPENBOUNTY faithfulness we have a choice:
- Option A (strict OpenKB parity): also stub them.
- Option B (DOS KB parity): implement per the manual / docs.

**The user requested DOS KB parity.** So we implement them via the documented intent (base damage values, target filters), and document the deviation from OpenKB in OPENBOUNTY-SPEC §34.

**Modal chrome**: the spell menu in kb_080 is two columns of letter-prefixed spells (Combat A-G, Adventuring A-G) with a "Cast which … spell (A-G)?" prompt at the bottom. The blue panel + yellow border + gold "Spells" title is the **same chrome the existing `open_dialog` mechanism renders** — `PAL_CLR(DBLUE)` panel, `PAL_CLR(YELLOW)` border, `PAL_CLR(YELLOW)` title, `PAL_CLR(WHITE)` body, bfont throughout. Reuse the existing prompt/dialog machinery; the spell menu is a new modal but built from the same primitives. Do NOT roll a custom panel renderer.

**Spec citations**: OPENKB §18.0 (lines 5929–5942); §18.1 Clone (5944–5957); §18.2 Teleport (5962–5968); §18.3 Fireball (5970–5975); §18.4 Lightning (5977–5979); §18.5 Freeze (5981–5990); §18.6 Resurrect (5992–5996); §18.7 Turn Undead (5998–6002); §18.16 spell power scaling table (6098–6111). Visual: kb_080 (spell menu).

**Deliverables**:

- `static int combat_choose_spell(Combat *c, Game *g);`
  Per OPENKB §18.0 lines 5929–5942:
  - Pre-checks: if `!g->stats.knows_magic`: emit `"You don't know any of these spells"` (existing `spell_unavailable` banner) and return -1.
  - If `c->spells_this_round != 0`: emit `"Only 1 spell per round!"` (§24.43 line 8249) and return -1.
  - Render spell menu (already implemented by `views_spells` for adventure spells; reuse with `cast_mode = combat`):
    Per kb_080: 7 rows × 2 cols, header "Cast which combat spell (A-G)?". Combat column shows spells 0..6, Adventure column 7..13. In combat we ONLY accept A..G (combat spells).
  - Player presses A..G. Look up `g->spells.counts[idx]`. If 0: emit `"You don't know that spell!"` (§18.0 line 5940) and return.
  - Decrement `g->spells.counts[idx]--`; `c->spells_this_round++`; return spell idx.

- Per-spell implementations (file: `src/combat_spells.c`, declared in `src/combat_spells.h` for cleaner separation):

  - **0 Clone — `combat_spell_clone`** (§18.1 lines 5946–5957):
    - `pick_target(filter=3)` → friendly stack.
    - `damage = spell_power * 10 + u.injury`
    - `clones = damage / hp; injury = damage % hp`
    - `u.count += clones; u.max_count += clones; u.injury = injury`
    - Log `"<N> <Troops> cloned"` (§24.43 line 8246).

  - **1 Teleport — `combat_spell_teleport`** (§18.2 lines 5963–5968):
    - `pick_target(filter=2)` → source. Log "Select army to Teleport" (§24.43 line 8253).
    - `pick_target(filter=1)` → destination. Log "Select new location".
    - `unit_relocate(c, side, id, nx, ny)` (update umap + x/y).

  - **2 Fireball — `combat_spell_fireball`** (§18.3, OpenKB stub; we implement):
    - `pick_target(filter=4)` → enemy.
    - `base = 25; damage = (base * spell_power) ` — wait, §18.16 line 6106 says Fireball at SP=2 → 50; SP=3 → 75; SP=5 → 125. So formula is `damage = base * spell_power` where base=25. That is: `final_damage = 25 * spell_power`.
    - If target IMMUNE: cancel, banner `"The spell seems to have no effect!"`.
    - Else: `combat_deal_damage(..., is_external=true, external_damage=final_damage)`.

  - **3 Lightning — `combat_spell_lightning`** (§18.4): same as Fireball but base=10 (§18.16 line 6107).

  - **4 Freeze — `combat_spell_freeze`** (§18.5 lines 5983–5990):
    - `pick_target(filter=4)`. If IMMUNE → cancel + "no effect" banner.
    - `u.frozen = true`. Banner `"<Troop> are frozen"` (§24.43 line 8244).

  - **5 Resurrect — `combat_spell_resurrect`** (§18.6, OpenKB stub; we implement):
    - `pick_target(filter=3)` → friendly.
    - `damage = spell_power * 20 + u.injury` — **OPEN QUESTION 14.11**: spec only says "stub", DOS manual says SP×20 hp; verify against the physical King's Bounty manual.
    - `revives = damage / hp; injury = damage % hp`.
    - `u.count = min(u.count + revives, u.max_count); u.injury = injury`.

  - **6 Turn Undead — `combat_spell_turn_undead`** (§18.7, OpenKB stub; we implement):
    - `pick_target(filter=4)` — should be filter=5 (enemy undead) but OpenKB uses 4; we follow OpenKB AND verify target has UNDEAD ability after pick. If not undead: emit "no effect" banner and consume the spell anyway (matches OpenKB behavior of consuming the spell on cancel).
    - `damage = 50 * spell_power` (§18.16 line 6108).
    - If IMMUNE: cancel.
    - Else: `combat_deal_damage(..., is_external=true, external_damage=damage)`.

- All spells set `u.acted = true` on the casting hero only if the spell consumes the unit's turn; per OPENKB §18.17 line 6122 "some spells consume the unit's turn, others don't" — the spec is ambiguous. **OPEN QUESTION 14.12**: which spells consume the turn vs not. Conservative default: NONE consume the unit's turn (matches OpenKB code path where `spells++` only).

**Files touched**:
- New: `src/combat_spells.c`, `src/combat_spells.h`.
- `src/combat.c`: call `combat_choose_spell` on key 'U'.
- `Makefile`: add new objects.

**Verification**:
- Cast Clone with SP=5 on a 1HP Peasant stack: `damage = 50 + 0 = 50; clones = 50; injury = 0`. Stack grows by 50.
- Cast Lightning with SP=5 on Knight (immune to magic? no — IMMUNE is Dragons only): expect `damage = 50` external; `kills = 50 / 35 = 1` Knight, injury = 15.
- Cast Lightning on Dragon: cancel, "no effect" banner.
- Cast Freeze: target's `acted` is set on its next turn entry (`if (u.frozen) { u.acted = true; banner; }`).

**Dependencies**: Phases 1–11.

---

### Phase 13 — Strings, banners, combat_log lifecycle, controls overlay

**Goal**: pull every combat-related string into `game.json`'s `strings.combat_log` block; wire to `Resources` and the banner ring buffer. Add the in-combat **Controls overlay** (kb_038) — same chrome as the spells modal (Phase 12).

**Modal chrome**: kb_038 shows the keybindings list inside the same blue+yellow panel pattern as kb_080 / kb_066, painted over the live battlefield. Reuse `open_dialog` / prompt machinery; the controls overlay is a read-only modal listing the bindings. Do NOT roll a custom panel renderer.

**Controls list (verbatim from kb_038)**:
```
↑ or 8   Move Up           A   View Army
↓ or 2   Move Down         C   Controls
← or 4   Move Left         F   Fly
→ or 6   Move Right        G   Give Up
END or 1 Down Left         S   Shoot
PGDN or 3 Down Right       U   Use Magic
HOME or 7 Up Left          V   View Char
PGUP or 9 Up Right         W   Wait
                           SPC Pass
```
Two-column layout: 8 directional bindings on the left, 9 action keys on the right. Press any key to dismiss.

**Spec citations**: OPENKB §24.43 (combat log strings, lines 8233–8254); §31.2 (combat keybindings, 9692–9719); OPENBOUNTY §24 (template substitution, 2425–2476).

**Deliverables**:

- New JSON block in `assets/kings-bounty/game.json` under `strings`:
  ```json
  "combat_log": {
      "melee_hit":          "%ATK% vs %TGT% killing %COUNT%",
      "retaliate":          "%TGT% retaliate killing %COUNT%",
      "ranged_hit":         "%ATK% shoot %TGT% killing %COUNT%",
      "ranged_no_effect":   "%ATK% shoot %TGT%",
      "no_effect_msg":      "The spell seems to have no effect!",
      "fly":                "%TROOP% fly",
      "move":               "%TROOP% move",
      "wait":               "%TROOP% wait",
      "pass":               "%TROOP% pass",
      "frozen":             "%TROOP% are frozen",
      "ooc":                "%TROOP% are out of control!",
      "cloned":             "%COUNT% %TROOP% cloned",
      "only_one_spell":     "Only 1 spell per round!",
      "select_clone":       "Select your army to Clone",
      "select_freeze":      "Select enemy army to Freeze",
      "select_resurrect":   "Select army to Resurrect",
      "select_damage":      "Select enemy army to %SPELL%",
      "select_teleport":    "Select army to Teleport",
      "select_dest":        "Select new location",
      "cant_shoot":         "Can't Shoot",
      "cant_fly":           "Can't Fly",
      "give_up_prompt":     "Giving up will forfeit your\narmies and send you back to\nthe King. Give up (y/n)?",
      "exit_hint":          "Press 'ESC' to exit"
  }
  ```
  Source for every line: OPENKB §24.43 (8235–8254) and §24.33 (8118).

- `src/resources.h` / `src/resources.c`: add `ResCombatLog combat_log` to `Resources.banners` (or a parallel `Resources.combat_log` block), parsed from JSON.

- `src/combat.c`: replace ad-hoc `snprintf` calls with `resources_format_template(..., res->combat_log.<id>, vars, n_vars)` — the same machinery already used by `RunCombatStub` (see `src/combat.c:21-50`).

**Files touched**: `assets/kings-bounty/game.json`, `src/resources.{c,h}`, `src/combat.c`.

**Verification**:
- Run any combat: every banner emitted matches the format strings letter-for-letter against OPENKB §24.43.
- Edit a string in game.json without recompiling (dev mode): banner updates next combat.

**Dependencies**: Phases 1–12.

---

### Phase 14 — Determinism harness, scripted scenarios, regression suite

**Goal**: provable determinism for the damage formula. User decision (earlier in this session): **self-consistent regression suite** — lock in the current correct behaviour as golden, regression-guard from there. Hand-computed parity tests against the original DOS KB are out of scope (no DOSBox capture available).

**Approach**: a headless `--combat-test` CLI flag in `main.c` runs `combat_test_digest()` — a pure-formula entry that builds a `Combat` with two stacks (no hero, no powers, no morale), runs N rounds of swings, folds (count, injury, kills, rng_state) per round into an FNV-64 digest. Identical inputs → identical digest. The regression script asserts the digest against locked-in goldens.

**Spec citations**: OPENKB §38 (verification checklist); OPENBOUNTY §13.1 (RNG conventions).

**Deliverables**:

- `combat_test_digest()` in `src/combat.c` — pure-formula test entry, no raylib calls.
- `--combat-test SEED:ATK:N:DEF:N:ROUNDS` in `main.c` — runs the digest before InitWindow, prints, exits.
- `scripts/combat_regression.sh` — 5 (or more) locked-in golden scenarios; runs each, compares digest, fails on mismatch. Regenerate goldens with `--write` after intentional formula changes.
- `Makefile` target `combat-test` — wires the script.

**Earlier-style 12-scenario hand-computed harness** (kept as reference; not built):

- `tools/combat_golden.c` would have defined 12 scenarios as static C structs:
    ```c
    typedef struct {
        const char *name;
        int seed;
        int player_troop, player_count;   // single stack each side for the unit tests
        int ai_troop, ai_count;
        int n_actions;
        struct { int actor; const char *kind; int dx, dy; } actions[8];
        // Expected post-state:
        int expect_player_count[5];
        int expect_ai_count[5];
        int expect_player_injury[5];
        int expect_ai_injury[5];
        int expect_spoils_player;
        int expect_spoils_ai;
    } GoldenScenario;
    ```
  - Each scenario seeds the combat RNG with a fixed value (not derived from `g->seed`), runs N actions, asserts post-state against expected values to the unit.
  - 12 scenarios cover:
    1. 100 Peasants vs 50 Peasants — symmetric retaliation.
    2. 100 Peasants vs 5 Dragons — skill-diff floor (skill 1 vs 6 → diff=0, zero damage).
    3. 5 Knights vs 50 Peasants — same-skill formula.
    4. 5 Vampires (LEECH) vs 100 Peasants — leech regrowth, max_count cap.
    5. 5 Ghosts (ABSORB) vs 50 Peasants — absorb growth.
    6. 5 Demons (SCYTHE) vs 100 Peasants, RNG forced to roll 90 — assert demon_kills.
    7. 1 Druid (MAGIC, fixed=10) vs 1 Dragon (IMMUNE) — assert returns -1, no state change.
    8. 50 Knights w/ HIGH morale vs 50 Knights — 1.5× damage.
    9. 50 Knights w/ INCREASED_DAMAGE artifact + HIGH morale — ×2.25.
    10. 50 Knights vs 50 Knights w/ QUARTER_PROTECTION shield — ×0.75 inbound.
    11. 100 Sprites (SL=1) vs 1 Knight (SL=5) — `(1 + 5 - 5)/10 = 0.1` factor.
    12. Cast Lightning SP=5 on 50 Pikemen (HP=10) — assert kills=5, injury=0.

- `tools/combat_replay.c` — replays a scripted action log (one combat scenario per JSON file under `tools/combat_scenarios/`) and prints final state. Useful for regression — when a refactor changes behavior, a scenario file flags it.

- Hook into Makefile: `make test-combat` runs all golden scenarios and asserts.

- Document the harness in `docs/COMBAT-PLAN.md` (this file's eventual home) under "Verification".

**Files touched**:
- New: `tools/combat_golden.c`, `tools/combat_replay.c`, `tools/combat_scenarios/*.json`.
- Makefile: target `test-combat`.

**Verification**: the harness is the verification.

**Dependencies**: Phases 1–13.

---

## 3. Cross-cutting topics

### 3.1 Data integration with existing `Game` and `Resources`

**Game struct unchanged.** Combat reads:
- `g->army[5]` — initial player units. (`src/game.h:222`.)
- `g->spells.counts[14]` — combat spell charges. (`src/game.h:224`.)
- `g->stats.spell_power`, `g->stats.followers_killed`, `g->stats.gold`, `g->stats.knows_magic`. (`src/game.h:152-156`.)
- `g->artifacts.found[8]` — for `powers[0]` derivation via `ARTIFACT_POWERS` table. (`src/game.h:189-191`.)
- `g->seed` — for RNG seeding (XOR-mixed; combat does NOT advance the world LCG).

Combat writes (only on victory or defeat):
- `g->stats.followers_killed` — incremented per kill on player side (per §17.5.9 line 5519).
- `g->stats.gold` — `+= spoils[1]` on player win.
- `g->army[5]` — surviving counts written back.
- `g->spells.counts[]` — decremented per cast (during combat, immediate effect).

**Resources unchanged except**: add `Resources.combat_log` block (Phase 13).

### 3.2 `troops[]` data fields used

All required fields are already in `TroopDef` (`src/tables.h:38-60`):
- `skill_level`, `hit_points`, `move_rate`, `melee_min`/`max`, `ranged_min`/`max`/`ammo`, `spoils_factor`, `abilities`, `morale_group`. Confirmed via §5 troop catalog table (OPENKB lines 1855–1881).

### 3.3 RNG integration

The combat LCG MUST be separate from `game.c`'s `game_rng_state`. Reasons:
- A combat does not advance world state (chest rolls, salting, astrology) — these must remain seeded only by `g->seed` so reload-from-save preserves world determinism.
- Different combats from the same save state should produce different outcomes — combat seed = `g->seed XOR g->stats.followers_killed XOR (turn counter)` gives natural variation.

The LCG constants are copied verbatim from `src/game.c:23-29` (multiplier 25214903917, increment 11, mask `0xFFFFFFFFFFFFFFFFUL`). This matches OPENBOUNTY §13.1 conventions.

### 3.4 Files added vs modified summary

**New files**:
- `src/combat_render.c` / `.h` — battlefield renderer; loads OpenKB combat tileset and unit sprites.
- `src/combat_spells.c` / `.h` (or absorbed into `combat.c`) — 7 spell functions.
- `scripts/combat_regression.sh` — Phase 14 regression harness wired to a `--combat-test` CLI flag in `main.c`.
- `assets/kings-bounty/art/combat/frame_00.png`..`frame_14.png` — **15 PNGs copied verbatim from `legacy/artifacts/assets/tilesets/combat/vga/`**. No re-authoring; no derived art.

**Modified files**:
- `src/combat.c` (full rewrite from the 60-line stub).
- `src/combat.h` (signature change: `RunCombat(Game*, const Sprites*, void *classic_target, CombatMode, const CombatTarget*)`).
- `src/main.c` — three call-site updates (rename `RunCombatStub` → `RunCombat`, pass `&sprites` and `&classic_target`); add `--combat-test SEED:ATK:N:DEF:N:ROUNDS` CLI hook.
- `src/sprites.h` / `src/sprites.c` — add `Texture2D combat_tile[15]` to the `Sprites` struct; load from `assets/kings-bounty/art/combat/frame_NN.png`; unload alongside other textures.
- `src/classic/chrome.h` / `chrome.c` — add `classic_chrome_draw_with_status(g, sprites, status_text)` so combat can render its title in the existing chrome strip without going through adventure-mode time-stop / days-left paths.
- `src/resources.{c,h}` — parse `strings.combat_log` block (Phase 13).
- `assets/kings-bounty/game.json` — add `strings.combat_log` block (Phase 13).
- `Makefile` — add `src/combat_render.c` to `SRC`; add `combat-test` target.

---

## 4. Open questions (require resolution during implementation)

Numbered for citation in code comments / commit messages.

| # | Question | Source of ambiguity | Suggested resolution path |
|---|---------|--------------------|---------|
| 14.1 | Open-field obstacle column range. Spec line 5868 says `for i in 1..CLEVEL_W-3` (math: cols 1, 2 — two cols), but commentary line 5872 says "cols 1..3 (4 of 6 columns)". | OPENKB §17.14 lines 5868 vs 5872 contradict. | Test against DOS KB savegame to determine canonical range. |
| 14.2 | When MAGIC ranged is cancelled by IMMUNE: does `u.shots--` happen before the cancel (literal spec ordering line 5320) or after (cancel-aborts-attack semantics)? | OPENKB §17.5.1 step 3, lines 5319–5322 ordering. | Match literal spec ordering: shots-- BEFORE cancel. Track post-bug. |
| 14.3 | Does `compact_units` physically reorder slot indices, or just clear dead slots? | OPENKB §17.8 line 5616 ambiguous. | Empirical: in the OpenKB binary, observe what `accept_units_player` returns. Default: no reorder (BL slot indices match save indices). |
| 14.4 | Should F10 cheat menu be exposed in combat? | OPENKB §31.2 line 9713 lists it; OPENBOUNTY may not want cheats. | Off in release builds; on in dev (already gated by F10 in adventure). |
| 14.5 | ~~Exact pixel offsets of combat grid, status bar, banner strip.~~ **RESOLVED.** Combat occupies the existing classic chrome inner area: x=16..303, y=22..191, 6×5 cells of 48×34. Cell pitch matches the OpenKB combat tileset (48×34) and unit sprites (48×34). | — | Resolved in Phase 8 rewrite. |
| 14.6 | ~~Castle backdrop interior color.~~ **RESOLVED.** No procedural fill — the field is tiled with `combat_tile[0]` (grass) for both modes, walls are stamped from `castle_omap` codes 5..10 mapping to `combat_tile[5..10]`. | — | Resolved by loading the OpenKB combat tileset. |
| 14.7 | ~~3 obstacle silhouette art (omap values 1, 2, 3).~~ **RESOLVED.** The OpenKB combat tileset frames 1, 2, 3 are the canonical obstacles (boulder, tree-cluster, mound). Already in `legacy/artifacts/assets/tilesets/combat/vga/`. | — | Resolved by loading the OpenKB combat tileset. |
| 14.8 | ~~Active-unit highlight rendering.~~ **RESOLVED.** OpenKB combat tileset frames 11–14 are the cursor sprites (active-unit ring, target ring, shoot ring, accent). Render frame 11 at the active unit and frame 12 at the cursor cell. | — | Resolved by loading the OpenKB combat tileset. |
| 14.9 | Ranged projectile animation. | Spec doesn't specify; OpenKB tileset frame 14 is a small accent sprite that may be the projectile sprite. | Defer; out of scope for the renderer's first pass. The combat log line ("X shoot Y, N die") communicates the event. Revisit when polishing. |
| 14.10 | Slot reorder on compact: see 14.3. | — | — |
| 14.11 | Resurrect spell formula. | OpenKB stub (§18.6 line 5995); not in spec scaling table (§18.16). | Check the printed manual `legacy/manuals/Kings-Bounty_Manual_DOS_EN_Web-Manual.md` for SP→hp scaling. |
| 14.12 | Which spells consume the caster's `acted` flag. | OPENKB §18.17 line 6122 ambiguous. | Default: NONE. The cap is `spells_this_round ≤ 1`, which already gates one cast per turn-pair. |
| 14.13 | Win-by-mutual-zero edge case (both sides reduced to 0 simultaneously). | Not in spec. | Player wins (defender takes the loss). Mirror OpenKB `test_victory` first, `test_defeat` second order in §17.9 line 5635. |
| 14.14 | Banner string pluralization: "1 die" vs "1 dies". | OPENKB §24.43 line 8235 fixed `"%d die"` regardless of N. | Match: always "die" (English-faithful to OpenKB). |
| 14.15 | Animation timing: SYN tick is 150ms in OPENKB (§31.3.1 line 9764). At 60fps raylib that's ~9 frames per anim step. | — | Use `GetTime()` against a 0.15s accumulator. |

---

## 5. Risk register

Ranked by parity-loss likelihood × correctness severity.

| # | Risk | Severity | Mitigation |
|---|-----|---------|---|
| R1 | **Damage formula off-by-integer** in any of the 12 multiplicative steps. Order of operations matters (integer division floors). | CRITICAL — breaks all of combat. | Phase 14 golden-scenario harness with hand-computed expected outcomes per OPENKB §17.5.2 worked examples. |
| R2 | **Retaliation recursion loop**. If `t.retaliated` flag is misset, infinite recursion. | HIGH — crash or hang. | Strict adherence to §17.5.6 lines 5462–5466. Unit test: A attacks B; B retaliates; assert no further recursion. |
| R3 | **`turn_count` snapshot timing**. Forgetting to set turn_count BEFORE the first attack underbids damage. | HIGH — combat feels wrong, hard to detect. | Always set in `combat_hit_unit` per §17.6.1; assert in tests. |
| R4 | **AI tie-breaking diverges**. The reverse-j iteration order in `unit_closest_offset` (§19.1.9) makes combat AI subtly different from foe AI; getting the order wrong shifts AI movement bias. | MEDIUM — AI plays "wrong" but works. | Direct line-for-line port of §17.12 pseudocode; test a fixed AI scenario for movement equality. |
| R5 | **`isqrt32` fixed-point precision**. Replacing with `sqrtf` changes tie-break order. | MEDIUM. | Direct port of §17.11.1 binary algorithm. Unit test: `isqrt32(1<<16) == 1<<16`. |
| R6 | **Castle layout decoded wrong**. The UID encoding `side*5 + id + 1` in `castle_umap` is non-obvious. | MEDIUM. | Use the exact decode `uid - 1 = side*5 + id`; assert player UIDs 1..5, AI UIDs 6..10. |
| R7 | **Scythe rounding**. `ceil(t.count/2)` is `(n+1)/2` for positive n only; for n=0 it's 0. | LOW. | Use `(t.count + 1) / 2`; assert with t.count = 0, 1, 2, 3. |
| R8 | **Powers byte layout**. `POWER_INCREASED_DAMAGE = 0x80`, `POWER_QUARTER_PROTECTION = 0x02`. Confusing them flips offense/defense. | LOW. | Use the named enum from `tables.h`; never hardcode the bits. |
| R9 | **Animation flicker** if every-frame combat re-renders w/o frame-rate gating. | LOW. | 150ms SYN tick gating per §31.3.1. |
| R10 | **Combat blocks main thread**. RunCombat is a blocking call; if it doesn't pump raylib's event loop, the window appears frozen. | MEDIUM. | Inside the loop, every frame call `BeginDrawing/EndDrawing`; never call non-input-pumping helpers. |
| R11 | **Stub call-site contract drift**. main.c's three call-sites pass a `CombatTarget` whose `garrison` is the source-of-truth for the AI side. If the CombatTarget pointer is stale (e.g. `garrison` points to mutable Game state that changes mid-combat), AI-side state changes leak into Game. | LOW (current code copies eagerly). | Combat copies into `c->units[1][]` at prepare time; never reads `target->garrison` again. |

---

## 6. Recommended phasing order & rationale

**Land in this order; each phase is independently mergeable.**

1. **Phase 1** — Scaffold, gets the rename and types in. **0.5d.** No behavior change.
2. **Phase 2** — Battlefield setup. **1d.** Engine prepares units; combat still concedes. Verifiable via debug print.
3. **Phase 3** — RNG + helpers. **1d.** Pure-function unit-testable.
4. **Phase 4** — Damage formula. **2d.** ★ Critical-path. Land golden-scenario test harness skeleton in same PR for confidence.
5. **Phase 5** — Movement + retaliation + compact. **2d.** Now player can manually drive an attack and see numbers shift.
6. **Phase 6** — Ranged. **1d.**
7. **Phase 8** — Renderer (early!). Out-of-order. Without rendering, phases 7 & 10 can't be exercised. **3d.** **Use the OpenKB combat tileset directly from `legacy/artifacts/assets/tilesets/combat/vga/` — do not author placeholder sprites, do not draw colored rectangles, do not invent visuals.** Frames map 1:1 to omap codes per OPENKB §17.14/§17.15.
8. **Phase 7** — Input + grid heuristic + action menu. **2d.** Player can now play a manual battle (no AI yet — AI side passes).
9. **Phase 9** — Target picker. **1d.** Unblocks shoot + spells.
10. **Phase 10** — Combat AI. **2d.** Battle is now playable end-to-end against AI.
11. **Phase 11** — End-of-combat plumbing. **1d.** Hooks into existing main.c flows.
12. **Phase 12** — Combat spells. **2d.** Player and AI can now cast.
13. **Phase 13** — Strings/banners polish. **0.5d.**
14. **Phase 14** — Determinism harness + scripted regression. **1d.**

**Total: ~20d.** A single contributor working full-time hits this in ~4 calendar weeks.

**Suggested PR layout**: 14 PRs (one per phase) keeps review tractable. Phases 4 + 14 can ship as a pair (formula + golden tests) since they're tightly coupled.

---

## 7. Summary of where the spec is fully sufficient vs where field-truth is needed

**Fully spec-grounded (no judgement calls)**:
- Damage formula (§17.5).
- Turn structure (§17.3).
- Spoils computation (§17.16).
- Castle layout (§17.15).
- Friendliness predicates (§17.4).
- AI movement and target picking (§17.12, §19.2, §19.3).
- Combat spell formulas (§18.1–§18.7) for the 3 OpenKB-implemented spells (Clone, Teleport, Freeze).

**Underspecified — flagged as open questions above**:
- Pixel-level UI placement (visual layout requires screenshot overlay).
- 4 spells stubbed in OpenKB (Fireball, Lightning, Resurrect, Turn Undead) — formulas inferred from §18.16 scaling table + DOS manual.
- Animation timing/cadence beyond the SYN-tick (transitions, projectile travel, death fade).
- Backdrop colors for open-field vs castle.
- Mutual-zero edge case.

For each open question, the implementation should:
1. Pick the most spec-conservative default.
2. Add a `// QUESTION 14.N: ...` comment in the code.
3. Document the choice in `docs/OPENBOUNTY-SPEC.md` §34 ("Known bugs / deviations").
4. Defer to KB.EXE binary observation if a regression arises.

---

### Critical Files for Implementation
- /home/danheskett/personal/openbounty/src/combat.c
- /home/danheskett/personal/openbounty/src/combat.h
- /home/danheskett/personal/openbounty/docs/OPENKB-SPEC.md (sections §17, §18.1–7, §19.2–4, §24.43, §31.2)
- /home/danheskett/personal/openbounty/assets/kings-bounty/game.json (add `strings.combat_log`)
- /home/danheskett/personal/openbounty/src/main.c (combat call-sites at lines 2056, 2092, 2135)

---

## 8. Phase 0 field-truth report (pre-flight findings)

Sources reviewed: every screenshot in `legacy/screenshots/` (32 files); the DOS manual combat sections (`legacy/manuals/Kings-Bounty_Manual_DOS_EN_Web-Manual.md` lines 247–356); existing `src/combat.{c,h}` and the three call-sites in `src/main.c` (lines 2048, 2083, 2128); OPENKB-SPEC §17.5 damage formula (lines 5260–5380), §18.1–18.7 spells (5944–6004), §19.2–19.4 AI (6473–6525). Findings below either **resolve** an open question from §4 or **clarify** a phase deliverable; question numbers reference §4.

### 8.1 Combat-relevant screenshots (canonical visual ground truth)

| File | What it shows | Used for which phase |
|---|---|---|
| `kb_038.png` | **Combat options/help screen** — full keybindings list overlaid on a live combat (Orcs M2, S5 in the title bar, defenders top-right with counts 16, 8). Every command is printed: `↑`/`8` Move Up, `↓`/`2` Move Down, `←`/`4` Left, `→`/`6` Right, `END`/`1` Down-Left, `PGDN`/`3` Down-Right, `HOME`/`7` Up-Left, `PGUP`/`9` Up-Right, `A` View Army, `C` Controls, `F` Fly, `G` Give Up, `S` Shoot, `U` Use Magic, `V` View Char, `W` Wait, `SPC` Pass. | Phase 7 (input), Phase 8 (renderer overlay) |
| `kb_055.png` | "Your scouts have sighted: Some Trolls / Some Vampires / A few Dragons. Attack (y/n)?" — combat entry prompt over adventure view. | Phase 1 (entry plumbing), Phase 13 (strings) |
| `kb_066.png` | **Victory!** banner, title bar `Knights vs Giants killing 5`, body lists `Spoils of War: 77200 gold` and `capture of Mahk Bellowspeak`, then bounty `25000 gold` + `piece of the map`. `(space)` prompt at bottom-right of body. | Phase 11 (victory dialog) |
| `kb_074.png` | **Castle siege** — 6×5 grid with crenellated stone walls top + sides; defender row at top with 4 stacks (Demons 28, Vampires 103, Demons 28, Demons 106), middle empty, player row at bottom with 5 stacks; **no field obstacles**; title bar `Options / Trolls M1`. Active troop has subtle highlight. | Phase 2 (castle layout), Phase 8 (renderer), §4.6 |
| `kb_075.png` | **Open-field combat** with several scattered tree-mound obstacles. Title bar `Options / Vampires F.M1`. Player units bottom-row spread (Sprites 4, Vampires 30, Knights 115, Cavalry 16, Pikemen 25), one defender top-right (20). | Phase 2 (open-field obstacles), Phase 8 (renderer), §4.7 |
| `kb_078.png` | **Mid-combat**, open field with mound obstacles; title `Barbarians vs Giants killing 0`; mid-game state showing units mid-grid. Confirms the **end-of-match-style title bar appears once a kill is logged**, not just at end. (Title bar mode-switch happens immediately after first kill — see 8.4.) | Phase 8 (title bar), Phase 13 (strings) |
| `kb_080.png` | **In-combat Spells modal** — title `Spells`, two columns: `Combat` (left) lists `0 Clone A`, `0 Teleport B`, `0 Fireball C`, `0 Lightning D`, `0 Freeze E`, `0 Resurrect F`, `4 Turn Undead G`; `Adventuring` (right) lists `0 Bridge A`, `0 Time Stop B`, `0 Find Villain C`, `2 Castle Gate D`, `0 Town Gate E`, `5 Instant Army F`, `0 Raise Control G`. Bottom prompt: `Cast which Adventure spell (A-G)?` (the prompt switches between Combat/Adventure based on context). | Phase 12 (spell UI), Phase 9 (target picker entry) |
| `kb_084.png` | **Defeat** cinematic — "Oh, Gideon the Lord, you have failed to recover the Sceptre… The Four Continents lay in ruin…" with castle ruin art. | Phase 11 (defeat dialog — adventure level, not combat-level) |
| `kb_098.png` | **Combat-loss** flavor (different from final defeat): "After being disgraced on the field of battle, King Maximus summons you… After a lesson in tactics, he reluctantly re-issues your commission and sends you on your way." Castle backdrop. | Phase 11 (combat-loss return-to-overworld) |

The remaining 23 screenshots are adventure-mode chrome (signposts, dwellings, town interiors, char/army views, intro/outro, stat sheets, ref charts) — they ground UI conventions (yellow border, blue dialog panel, status sidebar) but not combat mechanics.

### 8.2 Color & chrome conventions confirmed by screenshots and assets

**Update (post-Phase 0)**: the field is not a procedural fill. Both modes tile the OpenKB combat-tileset frame_00 (grass) across every cell; castle-mode siege walls come from frames 5–10 stamped via `castle_omap`. The color you see in kb_074 / kb_075 / kb_078 IS frame_00 (and the wall frames) — there's no separate green/gray fill choice. The chrome conventions below describe the surrounding frame, not the field interior.


- **Outer border**: bright yellow (matches OPENKB §26.7 `CS_FRAME` yellow). Used uniformly across adventure + combat.
- **Title bar**: top strip, dark-gray background, white text, format `<Heading>`. Combat heading patterns:
  - Active turn: `Options / <ActorTroopName> <Morale>.M<n>` (e.g. `Vampires F.M1`, `Trolls M1`). The `M<n>` is the **move-rate remaining**; the morale letter is `L`/`N`/`H` (or absent for normal — see kb_074 vs kb_075).
  - After first kill: `<PlayerArmyKind> vs <FoeArmyKind> killing <N>` (kb_066 victory `Knights vs Giants killing 5`; kb_078 in-progress `Barbarians vs Giants killing 0`).
  - Sub-screen modal: `Press 'ESC' to exit` (army view, char view, spells, etc.).
- **Battlefield background**: open field uses solid green `#00AA00` (`COLOR_DGREEN`). Castle siege uses gray cobble (`COLOR_GREY` `#AAAAAA`) inside the wall border. **Resolves §4.6.**
- **Count badge under each unit**: white digits on black band, no separator. Renders below the sprite, centered. (kb_074, kb_075.)
- **Active-unit highlight**: yellow ring/border around the active troop tile, *not* a flashing arrow. The "Active Troop Target" cursor (yellow ring per manual §247) is a *separate* movable cursor the player drives with the d-pad. Two visible elements: (a) ring on the active troop, (b) movable yellow target cursor. Arrow-target (red) appears only after pressing `S` to enter shoot mode. **Refines §4.8** — preferred render is `[two-element static rings, no flash]`, not the previously-suggested flashing arrow. Match the manual: blue ring on the unit + yellow ring on the cursor + red ring for arrow mode.

### 8.3 Manual-confirmed mechanics that pin down spec ambiguity

From manual lines 247–356:
1. **Facing**: "Your army faces right. The opposing army faces left." (line 275) — sprite-mirroring rule for the renderer, Phase 8.
2. **Adjacency rules for movement** (line 291): "You cannot move to a spot occupied by water, a dirt mound, trees, or another troop." Confirms passability set: open, mound, trees, water blocked; obstacle code stored in `omap`.
3. **Sprites special-case** (line 287): "Sprites fly on their first move" — first-turn-only fly. **Note**: this is in the manual but spec is silent on this rule. **Open question 4.16** (new): does this still apply, and how is "first move" tracked? Likely a Sprites-only ability flag we already have via `ABIL_FLY` + `turn_count == 1` check. Defer to Phase 5/6 implementation; flag-driven, no special-cased data.
4. **`A` or `C` to attack** (lines 285, 297): both keys equivalent. Spec (§31.2) only lists `A`. Add `C` as alias. **Refines §4 input table.**
5. **Pressing A/C while target-on-self = WAIT** (manual line 341): the WAIT keybinding has an alternate trigger via the targeting cursor itself. Spec is silent on this dual-binding. Add to Phase 7.
6. **Combat Delay 0–9** (manual line 351): a per-game user setting controlling AI animation pacing. Spec doesn't mention this. **New deliverable: add `combat_delay` to the existing options block in OPENBOUNTY-SPEC §32.2 / `Options` struct, default 5, range 0–9, applied as a multiplier to the SYN tick (default 150 ms × delay/5).** Phase 13 wires the option; Phase 8 renderer respects it.
7. **`B` opens combat options menu** (manual line 345). Spec lists `C` for Controls (kb_038 confirms `C`). Both keys must work — manual-`B` is the mega-drive port, DOS uses `C`. Standardize on `C` for OPENBOUNTY; `B` not bound (OPENBOUNTY uses ESC for back).

### 8.4 Title-bar mode transition (new finding)

Comparing kb_074 (`Options / Trolls M1` — pre-first-kill) vs kb_078 (`Barbarians vs Giants killing 0` — mid-game) vs kb_066 (`Knights vs Giants killing 5` — victory): the title bar **switches mode permanently at the moment of the first death** in the battle. Before any kill: `Options / <ActorName> <Morale>.M<n>`. After any kill (either side): `<PlayerArmyKind> vs <FoeArmyKind> killing <N>` where `N` is the running kill count *of full troop stacks* (not creature kills). This is a new spec-undocumented detail. Phase 8 renderer needs a `combat.first_kill_seen` flag and a `combat.stacks_destroyed` counter. **Likely sourced from OpenKB internals; add to Phase 1 data model.**

### 8.5 Spell stub semantics in OpenKB — IMPORTANT scope decision

OPENKB-SPEC §18.3, §18.4, §18.6, §18.7 explicitly call out that **Fireball, Lightning, Resurrect, and Turn Undead are stubs in OpenKB itself** — the `damage_army` / `resurrect_army` calls return without applying damage or healing. The user instruction was "functionally identical to OpenKB". Strict interpretation: OPENBOUNTY' Fireball/Lightning/Resurrect/Turn Undead should also do nothing. Loose interpretation: "OpenKB" here means "the King's Bounty combat engine that OpenKB targets" and these spells should work per the original DOS game.

**Recommendation**: treat as a deliberate parity gap. **Implement these four spells fully per the formulas the OPENKB spec provides for Fireball (`base_damage=25`, filter 4 enemy unit) and Lightning (`base_damage=10`)** since the spec gives the intended formula even when the OpenKB code is a stub. Resurrect and Turn Undead need a formula that the spec does not give for the stubbed path — the manual will be primary source.

**Open question 4.17** (new): confirm with user before Phase 12 — match OpenKB exactly (broken spells), or fill in the OpenKB stubs with the manual-correct behavior. Default: fill them in.

### 8.6 Existing stub call-sites & required signature change

`src/main.c` calls `RunCombatStub(&game, COMBAT_MODE_CASTLE|FOE, &tgt)` at lines 2056, 2092, 2135. The struct `CombatTarget` in `src/combat.h` already carries `name`, `garrison`, `garrison_slots`. Phase 1 keeps this signature, renames the function to `RunCombat`, and adds:
- A return value beyond `WIN/LOSS/FLEE` is **not** needed for now — the three current call-sites only branch on `outcome == WIN`. Phase 11 may extend later.
- The dialog-only stub in `combat.c` (lines 17–60) becomes the **opening "scouts have sighted" prompt** that already exists and must remain — but the WIN-immediate return is replaced by the real combat loop. Phase 1 must preserve the scouts banner for kb_055 parity.

### 8.7 Phase deltas resulting from Phase 0

Updates to apply during the implementation phases (no document edits to the existing §1–§7 needed; deltas tracked here):

- **Phase 1**: add `first_kill_seen` (bool) and `stacks_destroyed` (int) to the Combat struct. Add `combat_delay` (int 0–9) to existing Options. Rename `RunCombatStub` → `RunCombat`; keep the scouts opening banner.
- **Phase 7**: bind `C` (kb_038 verified) for Controls overlay; bind `A` and `C` (manual line 297) as attack/select; treat A/C-on-self as WAIT (manual line 341); SPC = Pass; `S` Shoot, `U` Use Magic, `V` View Char, `A`-with-no-cursor View Army (context-sensitive). Confirm against kb_038.
- **Phase 8** *(rewritten — see §2 Phase 8 above)*: load OpenKB combat tileset (15 frames at 48×34) into `Sprites.combat_tile[15]`; render order — black backdrop → tile field with `combat_tile[0]` (every cell) → stamp `combat_tile[omap[y][x]]` for non-zero codes → unit sprites (player faces right, AI faces left, mirrored) → count badges → cursor `combat_tile[11]` at active unit → cursor `combat_tile[12]` at target cell → `classic_chrome_draw_with_status` last so the title bar overlays. No procedural fills, no invented sprites. 150 ms × (combat_delay/5) for AI animation pacing.
- **Phase 11**: model the kb_098 "disgraced" flow as the **combat-loss** path (return to king's castle, re-commission); kb_084 is the **game-over** path (days exhausted or full surrender). Use `open_dialog` for the victory and disgrace banners — same chrome the rest of the engine uses.
- **Phase 12**: do not match OpenKB's stub-spells; implement Fireball/Lightning per spec, Resurrect/Turn Undead per manual. **Resolved:** §4.17 user decision = fill in working spells. Spell menu modal uses the same `open_dialog`/prompt chrome as the rest of the engine; column layout per kb_080.
- **Phase 13**: add `Cast which Adventure spell (A-G)?` / `Cast which Combat spell (A-G)?` prompt strings; add `<PlayerKind> vs <FoeKind> killing <N>` title-bar template. Add the Controls overlay (kb_038) as a read-only modal using `open_dialog` chrome; two-column layout (8 directional bindings on the left, 9 action keys on the right).

### 8.8 Open questions resolved (post-Phase 0, asset-load update)

| § | Status |
|---|---|
| 4.5 | **RESOLVED.** Combat field at x=16..303, y=22..191; cells 48×34. Same chrome wraparound as adventure mode. |
| 4.6 | **RESOLVED.** No procedural fill. Field tiled with `combat_tile[0]` (grass) for both modes; castle walls are `combat_tile[5..10]` stamped via `castle_omap`. |
| 4.7 | **RESOLVED.** OpenKB combat tileset frames 1, 2, 3 are the canonical obstacles. Already in `legacy/artifacts/assets/tilesets/combat/vga/`. |
| 4.8 | **RESOLVED.** OpenKB combat tileset frames 11–14 are the cursor sprites. Frame 11 = active unit, frame 12 = target cursor, frame 13 = shoot ring. |
| 4.9 | Open. Frame 14 may be a projectile accent; spec doesn't pin animation timing. Defer to a polish pass; combat log communicates the event in the meantime. |
| 4.17 | **RESOLVED.** User decision = fill in working Fireball/Lightning/Resurrect/Turn Undead per the spec/manual; do NOT match OpenKB stubs. |

### 8.9 New open questions raised by Phase 0

| # | Question | Where to resolve |
|---|---|---|
| 4.16 | Sprites' "first-move-only fly" rule (manual line 287). Implement as `ABIL_FLY` flag check gated on `turn_count == 1`? | Phase 5/6 |
| 4.17 | OpenKB stub spells (Fireball/Lightning/Resurrect/Turn Undead): match OpenKB exactly (broken) or fill in correct behavior? **Awaiting user decision.** | Phase 12 |
| 4.18 | Title-bar mode-switch trigger: confirm "first death" is the trigger, not "first turn" or "first action". | Phase 8 verification (any DOSBox capture would resolve, but we don't have that — keep as best-guess). |
| 4.19 | `combat_delay` setting persistence — which save-file field stores it? Spec doesn't say. | Phase 13 — add to Options block in save schema. |

### 8.10 Decisions Phase 0 made (no further user input needed)

1. Battlefield background colors per 8.2.
2. Three-element cursor system per 8.2 / 4.8.
3. `A` and `C` as attack/select aliases.
4. `C` (not `B`) for Controls overlay.
5. `combat_delay` option 0–9, default 5.
6. Title-bar template strings per 8.4.
7. Match the scouts opening banner from current stub.
8. New Combat struct fields: `first_kill_seen`, `stacks_destroyed`.

---

**Phase 0 complete.** Ready for Phase 1 once §4.17 (stub-spell scope) is decided.