#ifndef OB_RESOURCES_H
#define OB_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>
#include "tables.h"

// Runtime copy of every value loaded from assets/game.json and the table
// files it references. Mechanics code reads from here instead of holding
// KB-specific constants or catalog data in C. Call resources_load() once
// at startup, pass the returned struct around const, and resources_free()
// at shutdown.
//
// All character buffers are short because the source JSON limits strings.

#define RES_MAX_TOWNS         32
#define RES_MAX_CASTLES       32
#define RES_MAX_ZONES          8
#define RES_MAX_NEIGHBORS      8
#define RES_MAX_ZONE_OBJECTS 256     // each kind, per zone
#define RES_ID_LEN            32
#define RES_NAME_LEN          48
#define RES_SIGN_TITLE_LEN    64
#define RES_SIGN_BODY_LEN    128
#define RES_PATH_LEN         128
#define RES_TILE_CODE_COUNT  128     // indexed by raw byte
#define RES_TILE_ART_LEN      24
// Animation cycle lengths: OB_ANIM_FRAMES_MAX / _DEFAULT, in tables.h.
#define RES_COMBAT_TILES      15     // combat tileset, fixed role order
#define RES_EXTRA_ICONS        8     // view_icons_extra cap 
#define RES_END_BODY_LEN     512     // win/lose body text
#define RES_VDESC_TEXT_LEN   320     // per-villain features / crimes block

// ---- Sub-structures --------------------------------------------------------

// One animation, authored either as a single strip or as four facings.
//
// `directional` false means the pack declared a flat array: the frames live in
// slot OB_FACE_SOUTH and the renderer mirrors them east/west, exactly as every
// pack worked before facings existed. True means the pack declared all four,
// and the renderer selects a facing and never mirrors -- so an asymmetric
// figure keeps its shield on the correct arm walking west.
//
// count[] is per facing because nothing requires the four to be the same
// length; a pack may ship a six-frame walk east and a four-frame walk north.
typedef struct {
    bool directional;
    int  count[OB_FACE_COUNT];
    char frames[OB_FACE_COUNT][OB_ANIM_FRAMES_MAX][RES_PATH_LEN];
} ResAnimSet;

typedef struct {
    int day_steps;
    int week_days;
    int days_per_difficulty[4];   // [easy, normal, hard, impossible]
} ResTime;

// Tunable coefficients (game.json "tuning" block). Mod-friendly knobs
// for spell math that doesn't fit -derived data tables.
typedef struct {
    // Instant Army count = (spell_power + 1) * multiplier[rank].
    //  instant_army_multiplier[MAX_RANKS] = {3,2,1,1}.
    int instant_army_multiplier[4];
    // Search Area cost in days . Used both
    // for the day deduction and as %DAYS% in body_search.
    int search_cost_days;
    // The consolation army a temp death respawns with (game.json
    // "tuning.temp_death": {"troop": id, "count": n}). Defaults: the
    // catalog's cheapest-recruit_cost troop, count 20 (predecessor parity).
    char temp_death_troop[CAT_ID_LEN];
    int  temp_death_count;
} ResTuning;

// Score formula coefficients (). Each villain caught,
// artifact found, and player-owned castle scales by its coefficient; each
// follower killed during play subtracts at the kill_penalty rate. The
// running total is then scaled by difficulty:
//   - if easy_halves and difficulty == 0: score /= 2
//   - else if 1 <= difficulty < 5: score *= difficulty_multiplier[difficulty]
//   - clamp to >= 0
// The difficulty_multiplier[0] slot is unused when easy_halves is true and
// kept for parity with  layout.
typedef struct {
    int per_villain;
    int per_artifact;
    int per_castle;
    int kill_penalty;
    int difficulty_multiplier[5];
    bool easy_halves;
} ResScoring;

// Treasure-chest probability and value tables ().
// Indexed by zone tier 0..3. The chance_* arrays are cumulative thresholds
// against a 1..100 roll; the engine walks them top-to-bottom and the first
// branch whose threshold strictly exceeds the roll fires. gold_min/max,
// commission_min/max define ranges and base_max_spells the +N delta on a
// max-spells outcome.
typedef struct {
    int chance_gold[4];
    int chance_commission[4];
    int chance_spell_power[4];
    int chance_max_spells[4];
    int chance_new_spell[4];
    int gold_min[4];
    int gold_max[4];
    int commission_min[4];
    int commission_max[4];
    int max_spells_base[4];
} ResChest;

typedef struct {
    int alcove_cost;
    int boat_cost_normal;
    int boat_cost_cheap;
    int siege_cost;
    ResChest chest;
    ResScoring scoring;
} ResEconomy;

typedef struct {
    int cycle_length;
    int initial_last_contract;
} ResContract;

// Monster-spawn tables (+). Used by roll_creature-style
// castle/foe repopulation. Four tiers map 1:1 to "Plains / Forest / Hill /
// Dungeon" . Each tier has:
//  - chance_curve[4]: cumulative probability thresholds (1-100). Roll a
//    chance value, walk the curve; the first index where chance <= curve
//    picks the slot in the troop pool. Fifth slot is the fall-through.
//  - troop_pool[5]: troop ids for (strongest .. weakest) order of rarity.
// Count per roll is read from TroopDef.tier_counts[tier].
#define RES_SPAWN_TIERS 4
#define RES_SPAWN_POOL_N 5
typedef struct {
    int  chance_curve[RES_SPAWN_TIERS][RES_SPAWN_POOL_N - 1];
    char troop_pool  [RES_SPAWN_TIERS][RES_SPAWN_POOL_N][RES_ID_LEN];
} ResSpawn;

// Render geometry, declared by the pack. There is no default: a pack must say
// which mode it is authored for, because the two are not interchangeable --
// legacy art is drawn for a 48x34 tile and modern art for a square one. A pack
// that declares neither is rejected at load rather than guessed at.
typedef enum {
    RENDER_MODE_NONE = 0,     // nothing declared -> fatal
    RENDER_MODE_LEGACY,       // 320x200, 48x34 tiles, 5x5 viewport
    RENDER_MODE_MODERN,       // pack-declared tile size and viewport
} RenderMode;

typedef struct {
    RenderMode mode;
    int tile_w, tile_h;       // modern only; legacy forces 48x34
    int tiles_w, tiles_h;     // viewport in tiles; must be odd
    int ui_scale;             // multiplies the font and the chrome bands. A pack
                              // that doubles its tile must say so, or its
                              // furniture stays at 320x200 size around giant
                              // tiles. Legacy is 1.
} ResRender;

typedef struct {
    char starting_zone[RES_ID_LEN];
    char zone_noun[RES_ID_LEN];
    char zone_noun_plural[RES_ID_LEN];
    char language[RES_ID_LEN];    // base locale code; strings load from strings/<language>.json
    int  max_army_slots;
    int  fog_sight;
    // Initial player state defaults, used by GameInit when no override exists.
    char default_name[RES_NAME_LEN];   // fallback when player enters no name
    int  default_options[7];           // delay, sounds, walk_beep, anim, cga, music, volume
} ResWorld;

typedef struct {
    int  index;
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char zone[RES_ID_LEN];        // was "continent" in KB; engine calls it zone
    int  x, y;
    int  boat_x, boat_y;          // -1 if no water adjacency
    int  gate_x, gate_y;          // -1 if not set
    char intel_castle[RES_ID_LEN];// castle id the "gather info" command in
                                  // this town reports on. Empty = none.
    char pinned_spell[RES_ID_LEN];// spell id pre-placed here by salt_spells.
                                  // Empty = no pin (any town may pin in mods).
    char art[RES_TILE_ART_LEN];   // tile art stem under art/tiles/ ("" = "town")
} ResTown;

// Special-castle behavior (King Maximus and other quest castles).
// Empty flow ("") means a normal castle (siege/audience by owner_kind).
typedef struct {
    char flow[RES_ID_LEN];           // "" / "audience" / ...
    bool excluded_from_siege;
    bool excluded_from_intel;
    bool excluded_from_contract;     // villain assignment skips this castle
    char dialog_header[RES_NAME_LEN];
    char dialog_body[320];
    char win_condition[RES_ID_LEN];  // "scepter" triggers end-game on visit
    // Audience-flow text variants (templates with %NAME% / %RANK% /
    // %NEEDED% / %S% substitutions). Only populated when flow == "audience".
    char audience_intro[320];
    char audience_rank_up[320];
    char audience_more_needed[320];
    char audience_final_rank[320];
} ResCastleSpecial;

// Map footprint of a castle (REQ-228). 3x2 is the classic stamp: the gate
// tile plus five wall tiles. 1x1 is the gate tile alone, drawn with the
// single `castle` art, so the castle sits on the map the way a town does.
typedef enum {
    RES_CASTLE_FOOTPRINT_3X2 = 0,
    RES_CASTLE_FOOTPRINT_1X1
} ResCastleFootprint;

typedef struct {
    int  index;
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char zone[RES_ID_LEN];
    int  x, y;                    // -1 for entries without coords (e.g. King Maximus)
    int  gate_x, gate_y;          // tile player lands on from Castle Gate
                                  // spell. Defaults to (x, y+1) if absent.
    int  difficulty_tier;         // 0-3 (plains/forest/hill/dungeon), used by
                                  // roll_creature-style monster generation.
                                  //  castle_difficulty[].
    ResCastleFootprint footprint; // "footprint": "3x2" (default) or "1x1"
    ResCastleSpecial special;
} ResCastle;

// ---- Tile code (single byte in the .dat -> art + flags) -------------------

typedef struct {
    bool present;                 // false = unused code
    char art[RES_TILE_ART_LEN];
    int  terrain;                 // Terrain enum value (see tile.h)
    bool blocks_foot;
    bool is_bridge;
} ResTileCode;

// ---- Per-zone object placements -------------------------------------------

typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    char title[RES_SIGN_TITLE_LEN];
    char body[RES_SIGN_BODY_LEN];
} ResSign;

// (ResZoneTown removed: zone towns were a positional DUPLICATE of the
// authoritative top-level res->towns[] catalog and lacked the town name, which
// caused the demo's town screen to render a blank name. A zone now holds an
// INDEX LIST into res->towns[] -- one source of truth, identical iteration order.)

// One decorative tile relative to a castle's gate position. Painted by
// stamp_objects after the standard 3x2 castle footprint; lets a castle
// declare extra wall pieces (e.g. the home castle's surrounding mini-
// tower complex). Not interactive; blocks_foot.
typedef struct {
    int  dx, dy;
    char art[RES_ID_LEN];
} ResCastleDecor;

#define RES_MAX_CASTLE_DECOR 32

typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    int  decor_count;
    ResCastleDecor decorations[RES_MAX_CASTLE_DECOR];
} ResZoneCastle;

typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
} ResZoneChest;

typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
} ResZoneArtifact;

typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    char kind[RES_ID_LEN];        // "plains" / "forest" / "hills" / "dungeon"
} ResZoneDwelling;

typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    bool is_static;                    // never moves; forced fight on contact
    // Optional explicit garrison. If army_stacks == 0 the foe's garrison is
    // rolled by zone tier (default); otherwise these stacks are used verbatim
    // (a hand-tuned guardian).
    char army_id[5][RES_ID_LEN];       // 5 == GAME_ARMY_SLOTS
    int  army_count[5];
    int  army_stacks;
} ResZoneArmy;

// ---- Strings  -----

typedef struct {
    char id[RES_ID_LEN];
    char alias[RES_NAME_LEN];
    char features[RES_VDESC_TEXT_LEN];
    char crimes[RES_VDESC_TEXT_LEN];
} ResVillainDesc;

typedef struct {
    char header[RES_NAME_LEN];
    char body[RES_END_BODY_LEN];
    char footer[RES_NAME_LEN];
} ResEndText;

// Banner / dialog-body templates for player-facing prompts. Each entry is a
// printf-template-free string; substitution uses %TOKEN% pairs supplied by
// the caller via resources_format_template (resources.c). Loaded from
// game.json strings.banners.*. Built-in defaults are used when absent.
#define RES_BANNER_LEN 320
typedef struct {
    // Treasure-chest outcomes .
    // Substitutions: %GOLD%, %LEADERSHIP%, %POINTS%, %COUNT%, %SPELL%.
    char chest_gold[RES_BANNER_LEN];
    char chest_commission[RES_BANNER_LEN];
    char chest_spell_power[RES_BANNER_LEN];
    char chest_max_spells[RES_BANNER_LEN];
    char chest_new_spell[RES_BANNER_LEN];
    char chest_empty[RES_BANNER_LEN];

    // Town overlay header + sticky widgets .
    // Substitutions: %NAME%, %GOLD%.
    char town_header[RES_BANNER_LEN];
    char town_gold_label[RES_BANNER_LEN];

    // Town menu rows. The boat/spell/siege rows toggle between two
    // strings depending on game state. Substitutions: %COST%, %SPELL%,
    // %SPELL_COST%, %SIEGE_COST%.
    char town_row_contract[RES_BANNER_LEN];
    char town_row_boat_rent[RES_BANNER_LEN];     // %COST%
    char town_row_boat_cancel[RES_BANNER_LEN];
    char town_row_info[RES_BANNER_LEN];
    char town_row_spell[RES_BANNER_LEN];         // %SPELL% %SPELL_COST%
    char town_row_spell_none[RES_BANNER_LEN];
    char town_row_siege_buy[RES_BANNER_LEN];     // %SIEGE_COST%
    char town_row_siege_owned[RES_BANNER_LEN];

    // Town action toasts. Substitutions: %COST%, %LEFT%, %S%, %VILLAIN%,
    // %REWARD%, %ZONE%.
    char town_contract_new[RES_BANNER_LEN];
    char town_contract_none[RES_BANNER_LEN];
    char town_boat_vacate_first[RES_BANNER_LEN];
    char town_no_gold[RES_BANNER_LEN];
    char town_intel_unavailable[RES_BANNER_LEN];
    char town_intel_castle_under[RES_BANNER_LEN];// %NAME%
    char town_intel_owner_rule[RES_BANNER_LEN];  // %OWNER%
    char town_intel_owner_none[RES_BANNER_LEN];
    char town_intel_owner_player[RES_BANNER_LEN];
    char town_intel_owner_king[RES_BANNER_LEN];
    char town_intel_count_named[RES_BANNER_LEN]; // %LABEL% %TROOP%
    char town_intel_count_numeric[RES_BANNER_LEN];// %COUNT% %TROOP%
    char town_intel_monsters_generic[RES_BANNER_LEN];
    char town_intel_no_garrison[RES_BANNER_LEN];
    char town_spell_unavailable[RES_BANNER_LEN];
    char town_spell_at_cap[RES_BANNER_LEN];
    char town_spell_can_learn[RES_BANNER_LEN];   // %LEFT% %S% (s/empty)
    char town_siege_already[RES_BANNER_LEN];
    char town_siege_purchased[RES_BANNER_LEN];

    // Spell-effect dialog bodies .
    // Substitutions: %STEPS%, %CASTLE%, %COUNT%, %QTY%, %TROOP%, %AMOUNT%.
    char spell_time_stop[RES_BANNER_LEN];                 // %STEPS%
    char spell_find_villain_no_contract[RES_BANNER_LEN];
    char spell_find_villain_success[RES_BANNER_LEN];      // %CASTLE%
    char spell_find_villain_none[RES_BANNER_LEN];
    char spell_bridge_prompt[RES_BANNER_LEN];
    char spell_bridge_built[RES_BANNER_LEN];              // %COUNT%
    char spell_bridge_invalid[RES_BANNER_LEN];
    char spell_castle_gate_none[RES_BANNER_LEN];
    char spell_castle_gate_choose[RES_BANNER_LEN];
    char spell_town_gate_none[RES_BANNER_LEN];
    char spell_town_gate_choose[RES_BANNER_LEN];
    char spell_instant_army_fizzle[RES_BANNER_LEN];
    char spell_instant_army_no_room[RES_BANNER_LEN];
    char spell_instant_army_success[RES_BANNER_LEN];      // %QTY% %TROOP%
    char spell_raise_control_success[RES_BANNER_LEN];     // %AMOUNT%
    char spell_gate_teleported[RES_BANNER_LEN];
    char spell_gate_invalid[RES_BANNER_LEN];

    // Foe encounters .
    // Substitutions: %LABEL%, %COUNT%, %TROOP%, %COST%.
    char encounter_join_named[RES_BANNER_LEN];     // friendly w/ word count
    char encounter_join_numeric[RES_BANNER_LEN];   // friendly w/ numeric
    char encounter_wanderers[RES_BANNER_LEN];      // friendly refused
    char encounter_hostile_header[RES_BANNER_LEN]; // composite prefix
    char encounter_hostile_unknown[RES_BANNER_LEN];// fallback line
    char encounter_hostile_count_named[RES_BANNER_LEN];   // %LABEL% %TROOP%
    char encounter_hostile_count_numeric[RES_BANNER_LEN]; // %COUNT% %TROOP%

    // Archmage Aurange alcove .
    // Substitutions: %COST%.
    char alcove_offer[RES_BANNER_LEN];
    char alcove_already[RES_BANNER_LEN];
    char alcove_taught[RES_BANNER_LEN];
    char alcove_no_gold[RES_BANNER_LEN];
    char no_spell_banner[RES_BANNER_LEN];

    // New-game intro banner. Substitutions: %NAME%, %CLASS%.
    char new_game_intro[RES_BANNER_LEN];

    // Dwelling recruitment .
    // Substitutions: %COUNT%, %TROOP%, %COST%, %GOLD%, %CAP%.
    char dwelling_recruit_prompt[RES_BANNER_LEN];
    char dwelling_none_this_week[RES_BANNER_LEN];
    char dwelling_empty[RES_BANNER_LEN];

    // Adventure-tile interactions (telecave / navmap-chest / orb-chest).
    // Substitutions: %ZONE%.
    char telecave_teleport[RES_BANNER_LEN];
    char telecave_inert[RES_BANNER_LEN];
    char navmap_pickup[RES_BANNER_LEN];
    char crystal_ball_pickup[RES_BANNER_LEN];

    // End-of-week / temp_death banners. Substitutions: %WEEK%, %TROOP%.
    char astrology_header[RES_BANNER_LEN];
    char astrology_body[RES_BANNER_LEN];
    char temp_death[RES_BANNER_LEN];

    // Mid-combat give-up confirm. No substitutions. The prompt itself
    // adds " (y/n)?" automatically.
    char combat_give_up_header[RES_BANNER_LEN];
    char combat_give_up_body[RES_BANNER_LEN];

    // Pre-combat scout report. Substitutions: %COUNT% %TROOP%.
    char combat_scouts_header[RES_BANNER_LEN];
    char combat_scouts_count[RES_BANNER_LEN];
    char combat_scouts_small_band[RES_BANNER_LEN];
    char combat_header_siege[RES_BANNER_LEN];      // dialog title
    char combat_header_default[RES_BANNER_LEN];    // dialog title

    // Signposts .
    // Substitutions: %TITLE%, %BODY%.
    char signpost_with_body[RES_BANNER_LEN];
    char signpost_title_only[RES_BANNER_LEN];

    // End-of-week budget panel (src/shell_weekend.c, WK_PHASE_BUDGET).
    // Substitutions: %WEEK%.
    char budget_header[RES_BANNER_LEN];
    char budget_on_hand[RES_BANNER_LEN];
    char budget_payment[RES_BANNER_LEN];
    char budget_boat[RES_BANNER_LEN];
    char budget_army[RES_BANNER_LEN];
    char budget_balance[RES_BANNER_LEN];

    // Status bar (chrome.c). Substitutions: %DAYS%, %STEPS%.
    char status_days_left[RES_BANNER_LEN];
    char status_time_stop[RES_BANNER_LEN];

    // Composite prompt bodies (system flows that have %TOKEN%-style values).
    // Substitutions: %NAME%, %COST%, %GOLD%, %CAP%, %INDEX%, %TROOP%,
    // %COUNT%, %ZONE%, %REASON%.
    char body_save_confirm[RES_BANNER_LEN];
    char body_search[RES_BANNER_LEN];
    char body_dismiss_pick[RES_BANNER_LEN];
    char body_dismiss_last[RES_BANNER_LEN];
    char body_home_castle[RES_BANNER_LEN];
    char body_own_castle[RES_BANNER_LEN];           // %NAME%
    char body_garrison_row_named[RES_BANNER_LEN];   // %INDEX% %TROOP% %COUNT%
    char body_garrison_row_empty[RES_BANNER_LEN];   // %INDEX%
    char body_navigate_row[RES_BANNER_LEN];         // %INDEX% %ZONE%
    char body_no_continents[RES_BANNER_LEN];
    char body_must_be_sailing[RES_BANNER_LEN];

    // Short error / outcome banners reused by recruit / castle paths.
    char cannot_garrison_last[RES_BANNER_LEN];
    char no_troop_slots[RES_BANNER_LEN];
    char army_cannot_handle[RES_BANNER_LEN];
    char no_troops_to_garrison[RES_BANNER_LEN];
    char castle_garrison_empty[RES_BANNER_LEN];
    char spell_unavailable[RES_BANNER_LEN];
    char spell_not_known[RES_BANNER_LEN];
    char spell_unknown[RES_BANNER_LEN];
    char combat_victory_named[RES_BANNER_LEN];   // %NAME% %TARGET% %GOLD%
    char combat_victory_unnamed[RES_BANNER_LEN]; // %NAME% %GOLD%
} ResBanners;

// In-combat log strings (game.json strings.combat_log). One entry per
// distinct line emitted by the combat engine. Templates use the same
// %TOKEN% substitution as banners; tokens listed inline below.
//
// Field set + names follow docs/OPENBOUNTY-SPEC.md section 25 verbatim where the spec
// specifies them; additional names cover lines the spec didn't enumerate.
typedef struct {
    char melee_hit[RES_BANNER_LEN];          // %ATK% %TGT% %COUNT%
    char retaliate[RES_BANNER_LEN];          // %TGT% %COUNT%
    char ranged_hit[RES_BANNER_LEN];         // %ATK% %TGT% %COUNT%
    char ranged_no_effect[RES_BANNER_LEN];   // %ATK% %TGT%
    char no_effect_msg[RES_BANNER_LEN];      // (no tokens)
    char fly[RES_BANNER_LEN];                // %TROOP%
    char move[RES_BANNER_LEN];               // %TROOP%
    char wait[RES_BANNER_LEN];               // %TROOP%
    char pass[RES_BANNER_LEN];               // %TROOP%
    char frozen[RES_BANNER_LEN];             // %TROOP%
    char ooc[RES_BANNER_LEN];                // %TROOP%
    char immune[RES_BANNER_LEN];             // %TROOP%
    char cloned[RES_BANNER_LEN];             // %COUNT% %TROOP%
    char resurrected[RES_BANNER_LEN];        // %COUNT% %TROOP%
    char teleported[RES_BANNER_LEN];         // (no tokens)
    char only_one_spell[RES_BANNER_LEN];     // (no tokens)
    char no_spell_type[RES_BANNER_LEN];      // (no tokens)
    char cannot_cast[RES_BANNER_LEN];        // (no tokens)
    char cast_fireball[RES_BANNER_LEN];      // (no tokens)
    char cast_lightning[RES_BANNER_LEN];     // (no tokens)
    char cast_turn_undead[RES_BANNER_LEN];   // (no tokens)
    char select_clone[RES_BANNER_LEN];
    char select_freeze[RES_BANNER_LEN];
    char select_resurrect[RES_BANNER_LEN];
    char select_damage[RES_BANNER_LEN];      // %SPELL%
    char select_teleport[RES_BANNER_LEN];
    char select_dest[RES_BANNER_LEN];
    char cant_shoot[RES_BANNER_LEN];
    char no_ammo[RES_BANNER_LEN];
    char cant_fly[RES_BANNER_LEN];
    char give_up_prompt[RES_BANNER_LEN];
    char exit_hint[RES_BANNER_LEN];
} ResCombatLog;

// UI label strings (game.json strings.ui / strings.menu / strings.stats /
// strings.army_view / strings.morale / strings.difficulty / strings.startup).
// These are short labels rendered in fixed-width slots, so we pull only the
// label text -- the column math stays in C.
#define RES_UI_LABEL_LEN 32
#define RES_KEYBIND_LABEL_LEN 24
#define RES_KEYBIND_KEY_LEN    8
#define RES_MAX_KEYBINDS      24
#define RES_MAX_COUNT_BUCKETS  8

typedef struct {
    int  threshold;                       // count <= threshold => use label
    char label[RES_UI_LABEL_LEN];
} ResCountBucket;

typedef struct {
    char label[RES_UI_LABEL_LEN];        // Easy/Normal/Hard/Impossible?
    char score_mult[RES_UI_LABEL_LEN];   // " x1" / "x.5" / etc.
} ResDifficultyLabel;

typedef struct {
    char key[RES_KEYBIND_KEY_LEN];        // "Dn" / "PgUp" / "F"
    char label[RES_KEYBIND_LABEL_LEN];    // "Move Down" / "Fly"
} ResKeybind;

typedef struct {
    // Generic UI strings.
    char press_esc_to_exit[RES_UI_LABEL_LEN];
    // Status-bar fast-quit prompt (). Rendered into the
    // top status bar via KB_TopBox, not a bottom dialog.
    char quit_to_dos_prompt[RES_UI_LABEL_LEN * 2];
    char out_of_control[RES_UI_LABEL_LEN];
    char worldmap_hint_your_map[RES_UI_LABEL_LEN];
    char worldmap_hint_whole_map[RES_UI_LABEL_LEN];

    // Menu labels.
    char menu_root_title[RES_UI_LABEL_LEN];
    char menu_views_title[RES_UI_LABEL_LEN];
    char menu_options_title[RES_UI_LABEL_LEN];
    char menu_back[RES_UI_LABEL_LEN];
    char menu_exit[RES_UI_LABEL_LEN];
    char menu_save[RES_UI_LABEL_LEN];
    char menu_load[RES_UI_LABEL_LEN];
    char menu_new_game[RES_UI_LABEL_LEN];
    char menu_views[RES_UI_LABEL_LEN];
    char menu_options[RES_UI_LABEL_LEN];
    char menu_army[RES_UI_LABEL_LEN];
    char menu_spells[RES_UI_LABEL_LEN];
    char menu_character[RES_UI_LABEL_LEN];
    char menu_contract[RES_UI_LABEL_LEN];
    char menu_puzzle[RES_UI_LABEL_LEN];
    char menu_view_map[RES_UI_LABEL_LEN];

    // Character-view stat labels.
    char stat_leadership[RES_UI_LABEL_LEN];
    char stat_commission[RES_UI_LABEL_LEN];
    char stat_gold[RES_UI_LABEL_LEN];
    char stat_spell_power[RES_UI_LABEL_LEN];
    char stat_max_spells[RES_UI_LABEL_LEN];
    char stat_villains_caught[RES_UI_LABEL_LEN];
    char stat_artifacts_found[RES_UI_LABEL_LEN];
    char stat_castles_garrisoned[RES_UI_LABEL_LEN];
    char stat_followers_killed[RES_UI_LABEL_LEN];
    char stat_current_score[RES_UI_LABEL_LEN];

    // Army-view inline label prefixes.
    char army_skill[RES_UI_LABEL_LEN];        // "SL:"
    char army_move[RES_UI_LABEL_LEN];         // "MV:"
    char army_morale[RES_UI_LABEL_LEN];       // "Morale:"
    char army_hit_points[RES_UI_LABEL_LEN];   // "HitPts:"
    char army_damage[RES_UI_LABEL_LEN];       // "Damage:"
    char army_g_cost[RES_UI_LABEL_LEN];       // "G-Cost:"

    // Morale enum labels.
    char morale_normal[RES_UI_LABEL_LEN];
    char morale_low[RES_UI_LABEL_LEN];
    char morale_high[RES_UI_LABEL_LEN];

    // Count buckets (sorted ascending by threshold).
    int            count_buckets_army_view_n;
    ResCountBucket count_buckets_army_view[RES_MAX_COUNT_BUCKETS];
    int            count_buckets_instant_army_n;
    ResCountBucket count_buckets_instant_army[RES_MAX_COUNT_BUCKETS];

    // Difficulty labels (indexed by Difficulty enum 0..3).
    ResDifficultyLabel difficulty[4];

    // Keybind labels.
    int        keybind_count;
    ResKeybind keybinds[RES_MAX_KEYBINDS];

    // Startup strings.
    char startup_controls_hint[RES_UI_LABEL_LEN * 2];
    char startup_class_select_hint[RES_UI_LABEL_LEN * 2];
    char startup_class_picker_missing[RES_UI_LABEL_LEN * 2];
    char startup_save_picker_title[RES_UI_LABEL_LEN];
    char startup_save_picker_empty[RES_UI_LABEL_LEN];
    char startup_save_picker_new_game[RES_UI_LABEL_LEN];
    char startup_new_game_table_header[RES_UI_LABEL_LEN * 2];
    char startup_new_game_select_hint[RES_UI_LABEL_LEN * 2];

    // Controls panel labels.
    char controls_title[RES_UI_LABEL_LEN];
    char controls_on[RES_UI_LABEL_LEN];
    char controls_off[RES_UI_LABEL_LEN];

    // Prompt hint lines (prompt.c). Substitutions: %COUNT%.
    char prompt_text_hint[RES_UI_LABEL_LEN * 2];
    char prompt_numeric_range_hint[RES_UI_LABEL_LEN];
    char prompt_yes_no_hint[RES_UI_LABEL_LEN];
    char prompt_numeric_5_hint[RES_UI_LABEL_LEN];

    // Dialog/prompt header titles (game.json strings.dialog_titles).
    // Spell-flow headers come from spell_by_id()->name and are not stored
    // here; everything else (artifact pickups, system flows, fallbacks)
    // gets a slot.
    char dt_treasure[RES_UI_LABEL_LEN];
    char dt_teleport_cave[RES_UI_LABEL_LEN];
    char dt_crystal_ball[RES_UI_LABEL_LEN];
    char dt_foes[RES_UI_LABEL_LEN];
    char dt_alcove_offer[RES_UI_LABEL_LEN];   // Archmage Aurange
    char dt_alcove_result[RES_UI_LABEL_LEN];  // Aurange
    char dt_castle_default[RES_UI_LABEL_LEN]; // home-castle picker fallback
    char dt_own_castle[RES_UI_LABEL_LEN];
    char dt_search[RES_UI_LABEL_LEN];
    char dt_dismiss_army[RES_UI_LABEL_LEN];
    char dt_dismiss_last[RES_UI_LABEL_LEN];
    char dt_navigate[RES_UI_LABEL_LEN];
    char dt_garrison_pick[RES_UI_LABEL_LEN];
    char dt_remove_pick[RES_UI_LABEL_LEN];
    char dt_save_confirm[RES_UI_LABEL_LEN];
    char dt_lose_fallback[RES_UI_LABEL_LEN];   // header when win_text.header empty
    char dt_win_fallback[RES_UI_LABEL_LEN];
    char dt_combat_victory[RES_UI_LABEL_LEN];  // title of the post-battle spoils dialog

    // Misc fallbacks.
    char empty_slot[RES_UI_LABEL_LEN];   // garrison-row "Empty" placeholder

    // Combat spell-pick screen labels.
    char combat_spells_title[RES_UI_LABEL_LEN];
    char combat_spells_col_combat[RES_UI_LABEL_LEN];
    char combat_spells_prompt[RES_UI_LABEL_LEN];

    // Adventure dwelling backdrop labels.
    char dwelling_kind_plains[RES_UI_LABEL_LEN];
    char dwelling_kind_forest[RES_UI_LABEL_LEN];
    char dwelling_kind_hill[RES_UI_LABEL_LEN];
    char dwelling_kind_dungeon[RES_UI_LABEL_LEN];
    char dwelling_recruit_how_many[RES_UI_LABEL_LEN];
    char dwelling_info_available[RES_UI_LABEL_LEN];    // %COUNT% %TROOP%
    char dwelling_info_cost[RES_UI_LABEL_LEN];         // %COST%
    char dwelling_info_gold[RES_UI_LABEL_LEN];         // %GOLD% (thousands)
    char dwelling_info_recruit_cap[RES_UI_LABEL_LEN];  // %CAP%

    // Home-castle recruit screen.
    char recruit_soldiers_title[RES_UI_LABEL_LEN];
    char recruit_soldiers_how_many[RES_UI_LABEL_LEN];

    // Own-castle garrison/remove mode labels.
    char own_castle_mode_garrison[RES_UI_LABEL_LEN];
    char own_castle_mode_remove[RES_UI_LABEL_LEN];

    // Gate-landing view titles (town vs. castle gate).
    char gate_title_town[RES_UI_LABEL_LEN];
    char gate_title_castle[RES_UI_LABEL_LEN];
    char gate_footer_hint[RES_BANNER_LEN];     // gate chooser footer controls
    char recruit_col_hint[RES_UI_LABEL_LEN];   // "(A-C) " recruit column hint

    // Save/load + game-state toasts (game.json strings.toasts).
    // Substitutions: %REASON%.
    char toast_save_cancelled[RES_UI_LABEL_LEN];
    char toast_save_ok[RES_UI_LABEL_LEN];
    char toast_save_failed[RES_UI_LABEL_LEN];
    char toast_load_cancelled[RES_UI_LABEL_LEN];
    char toast_load_ok[RES_UI_LABEL_LEN];
    char toast_load_failed[RES_UI_LABEL_LEN];
    char toast_new_game[RES_UI_LABEL_LEN];

    // Contract / villain-detail view labels (game.json strings.contract_view).
    // Substitutions: %VALUE%.
    char cv_title_no_contract[RES_UI_LABEL_LEN];
    char cv_label_name[RES_UI_LABEL_LEN];
    char cv_label_alias[RES_UI_LABEL_LEN];
    char cv_label_reward[RES_UI_LABEL_LEN];
    char cv_label_last_seen[RES_UI_LABEL_LEN];
    char cv_label_castle[RES_UI_LABEL_LEN];
    char cv_alias_none[RES_UI_LABEL_LEN];
    char cv_castle_unknown[RES_UI_LABEL_LEN];
    char cv_features_header[RES_UI_LABEL_LEN];
    char cv_crimes_header[RES_UI_LABEL_LEN];

    // Spells view labels (game.json strings.spells_view).
    char sv_title[RES_UI_LABEL_LEN];
    char sv_combat_col[RES_UI_LABEL_LEN];
    char sv_adventure_col[RES_UI_LABEL_LEN];
} ResUI;

// Color tables exposed via game.json so palettes can be tweaked without
// recompiling. Each entry is stored as 0xAARRGGBB packed; raylib `Color`
// is reconstructed at the call site.
typedef struct {
    // Minimap tile colors by terrain (views_render.c terrain_minimap_color).
    unsigned int minimap_grass;
    unsigned int minimap_forest;
    unsigned int minimap_mountain;
    unsigned int minimap_water;
    unsigned int minimap_desert;
    unsigned int minimap_fog;
    // Status bar background color by difficulty (chrome.c).
    unsigned int difficulty_easy;
    unsigned int difficulty_normal;
    unsigned int difficulty_hard;
    unsigned int difficulty_impossible;
} ResColors;

// ---- Zone ------------------------------------------------------------------

// Salt budget for a zone -- how many randomized objects to place at GameInit.
// All fields zero = do not salt this zone (parity-safe for non-continent zones).
typedef struct {
    int artifacts;
    int navmaps;
    int orbs;
    int telecaves;
    int dwellings;
    int friendly_foes;
    // No hostile_foes salt budget -- hostiles come from static armies[]
    // placements in the zone definition.

    // Ordered list of preferred troop ids for the first N salt dwelling
    // slots on this zone. After the list is exhausted, salt_continent
    // falls back to a uniform random pick from
    // dwelling_range_min..dwelling_range_max (catalog indices).
    int  preferred_troop_count;
    char preferred_troops[16][RES_ID_LEN];
    int  dwelling_range_min;   // troop catalog index (inclusive)
    int  dwelling_range_max;   // troop catalog index (inclusive)
} ResZoneSalt;

typedef struct {
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char map_path[RES_PATH_LEN];
    // Optional terrain art folder: when set, every tile_codes art name for
    // this zone resolves under art/tiles/<tile_set>/ instead of art/tiles/.
    // Empty means the shared set. Object tiles (towns, castles, chests...)
    // are never affected.
    char tile_set[RES_ID_LEN];
    int  width, height;
    int  hero_spawn_x, hero_spawn_y;
    int  neighbor_count;
    char neighbors[RES_MAX_NEIGHBORS][RES_ID_LEN];

    // Per-zone object placements. Interactive overlays come from here --
    // the .dat only carries terrain + edge art.
    int  sign_count;     ResSign          signs[RES_MAX_ZONE_OBJECTS];
    // Towns in this zone, as INDICES into the authoritative res->towns[] catalog,
    // in the zone JSON's town order (resolved post-parse by id). Use the
    // resources_zone_town* accessors below. RES_MAX_TOWNS caps the catalog, so a
    // single zone can reference at most that many.
    int  town_count;     int              town_idx[RES_MAX_TOWNS];
    int  castle_count;   ResZoneCastle    castles[RES_MAX_ZONE_OBJECTS];
    int  chest_count;    ResZoneChest     chests[RES_MAX_ZONE_OBJECTS];
    int  artifact_count; ResZoneArtifact  artifacts[RES_MAX_ZONE_OBJECTS];
    int  dwelling_count; ResZoneDwelling  dwellings[RES_MAX_ZONE_OBJECTS];
    int  army_count;     ResZoneArmy      armies[RES_MAX_ZONE_OBJECTS];

    // Salt budget -- randomized objects placed by salt_continent at GameInit.
    ResZoneSalt salt;

    // Zone is the starting continent. Exactly one zone should set this.
    bool is_home;
    // Starting tile on a home zone. Only meaningful when is_home == true.
    int  home_spawn_x, home_spawn_y;

    // Magic alcove tile (optional). If set and the player class already
    // knows magic, GameInit removes this overlay so it cannot be claimed.
    // -1,-1 = no alcove in this zone.
    int  magic_alcove_x, magic_alcove_y;
} ResZone;

// ---- Top-level -------------------------------------------------------------

typedef struct {
    char title[RES_NAME_LEN];
    int  version;

    // Pack identity (game.json top-level "pack_id" / "pack_name"). pack_id
    // is the stable string used by saves to bind to a specific pack;
    // pack_name is for display.
    char pack_id[64];
    char pack_name[64];

    ResTime     time;
    ResEconomy  economy;
    ResContract contract;
    ResSpawn    spawn;
    ResWorld    world;
    ResRender   render;
    ResTuning   tuning;
    ResColors   colors;
    ResUI       ui;

    int         town_count;
    ResTown     towns[RES_MAX_TOWNS];

    int         castle_count;
    ResCastle   castles[RES_MAX_CASTLES];

    int         zone_count;
    ResZone     zones[RES_MAX_ZONES];

    // Indexed by raw byte from the .dat. `present == false` means unused.
    ResTileCode tile_codes[RES_TILE_CODE_COUNT];

    // Catalogs (loaded from game.json top-level arrays).
    int         troops_count;
    TroopDef    troops[CAT_TROOPS_MAX];

    int         spells_count;
    SpellDef    spells[CAT_SPELLS_MAX];

    int         classes_count;
    ClassDef    classes[CAT_CLASSES_MAX];

    int         villains_count;
    VillainDef  villains[CAT_VILLAINS_MAX];

    int         artifacts_count;
    ArtifactDef artifacts[CAT_ARTIFACTS_MAX];

    // Combat rules -- cross-group morale chart .
    // Indexed by (my_group - 'A', their_group - 'A'); values are 'N'/'L'/'H'.
    char morale_chart[5][5];

    // Fuzzy-number labels for intelligence / enemy-sight text
    // .
    // Entries are ordered high-to-low by threshold: the first entry
    // whose threshold is <= count wins. Up to 6 buckets.
    int  number_name_count;
    int  number_name_thresholds[8];
    char number_name_labels[8][24];

    // Controls settings panel .
    // Each entry describes one row: a label, a type ("bool" or "numeric"),
    // a default value, and (for numeric) the range cap.
    struct {
        int count;
        struct {
            char id[RES_ID_LEN];
            char label[24];
            char type[16];       // "bool" or "numeric"
            int  range;          // numeric upper bound (exclusive)
            int  def;            // default value
            bool hidden;         // not shown unless CGA mode active
        } items[8];
    } controls;

    // End-game text blocks (strings.win, strings.lose in game.json).
    ResEndText win_text;
    ResEndText lose_text;

    // Player-facing banner templates (strings.banners in game.json).
    ResBanners banners;

    // In-combat log strings (strings.combat_log in game.json). See
    // docs/OPENBOUNTY-SPEC.md section 25.11 for the canonical key set and provenance.
    ResCombatLog combat_log;

    // Credits screen. Shown once between the title splash and the class
    // picker. Layout: a series of "Label / names..." groups, then a
    // centered copyright block, with an inset image on the right.
    // Modpacks override per game pack.
    struct {
        char image[RES_PATH_LEN];     // path to the inset sprite, or ""
        struct {
            char label[64];           // e.g. "Programmed By:"
            char names[4][64];        // up to 4 indented names
            int  name_count;
        } groups[6];
        int group_count;
        char copyright[4][64];        // up to 4 centered footer lines
        int copyright_count;
    } credits;

    // Victory cartoon tiles + parameters. Tile art is sized to
    // CL_TILE_W x CL_TILE_H so each cell lines up with the map viewport.
    struct {
        char grass_tile[RES_PATH_LEN];   // GR_ENDTILE sub 0
        char carpet_tile[RES_PATH_LEN];  // GR_ENDTILE sub 1
        char hero_tile[RES_PATH_LEN];    // GR_ENDTILE sub 2
        char throne_backdrop[RES_PATH_LEN]; // optional post-cartoon image
        int  grid_width;                 // default 6
        int  grid_height;                // default 5
        int  carpet_column;              // x-column the carpet/hero use
        int  carpet_length;              // max carpet tiles (5 )
        int  frame_count;                // animation frame cap (10)
        int  ticks_per_step;             // advance frame every N ticks (2)
        bool troop_border;               // animate troop walk frames around edges
    } ending;

    // Per-villain description blocks (strings.villain_descriptions).
    int             villain_desc_count;
    ResVillainDesc  villain_descs[CAT_VILLAINS_MAX];

    // Role-fixed sprite manifest (assets that aren't per-catalog-entry).
    struct {
        // Hero. Each set is either a single strip (mirrored east/west by the
        // renderer, which is how kings-bounty is authored) or four authored
        // facings. `idle` is optional; without it the hero holds frame 0 while
        // standing still, which is the behaviour packs had before it existed.
        ResAnimSet hero_walk;
        ResAnimSet hero_idle;
        ResAnimSet hero_boat;
        // Combat tileset, in the fixed role order the renderer indexes by:
        // 0 field, 1-3 obstacles, 4 castle item, 5-10 walls, 11-14 cursor.
        // Declared by the pack; the shell carried this list hardcoded before.
        int  combat_count;
        char combat[RES_COMBAT_TILES][RES_PATH_LEN];
        // Bitmap font strip and the VGA palette binary. Both had their paths
        // compiled into the shell, which meant a pack could not name its own.
        char font[RES_PATH_LEN];
        char palette[RES_PATH_LEN];
        // UI.
        char puzzle_cover[RES_PATH_LEN];
        // Location backdrops .
        char town_backdrop[RES_PATH_LEN];
        char castle_backdrop[RES_PATH_LEN];
        char plains_backdrop[RES_PATH_LEN];
        char forest_backdrop[RES_PATH_LEN];
        char hillcave_backdrop[RES_PATH_LEN];
        char dungeon_backdrop[RES_PATH_LEN];
        // End-game images .
        char ending_win[RES_PATH_LEN];
        char ending_lose[RES_PATH_LEN];
        int  view_icons_extra_count;
        char view_icons_extra[RES_EXTRA_ICONS][RES_PATH_LEN];
        // HUD panels.
        char hud_contract_silhouette[RES_PATH_LEN];
        char hud_siege_silhouette[RES_PATH_LEN];
        int  hud_siege_animation_count;
        char hud_siege_animation[OB_ANIM_FRAMES_MAX][RES_PATH_LEN];
        char hud_magic_silhouette[RES_PATH_LEN];
        int  hud_magic_animation_count;
        char hud_magic_animation[OB_ANIM_FRAMES_MAX][RES_PATH_LEN];
        char hud_puzzle_grid[RES_PATH_LEN];
        char hud_gold_purse[RES_PATH_LEN];
        char hud_bar_strip[RES_PATH_LEN];   // 320x5 middle bar (GR_SELECT, 1)
        char chrome_overworld[RES_PATH_LEN]; // 320x200 frame bitmap,
                                             // transparent interior; pixel-
                                             // exact copy of reference chrome.
        char splash_logo[RES_PATH_LEN];      // publisher logo (first splash)
        char splash_title[RES_PATH_LEN];     // game title (second splash)
        char class_picker[RES_PATH_LEN];     // 288x184 A-D class portrait image
        char class_highlight[RES_PATH_LEN];  // 42x44 cursor glow for class picker
        char orb[RES_PATH_LEN];              // orb of power tile overlay
    } sprites;

    // Background music tracks (OGG) and tune sound effects (WAV).
    // Empty string disables that entry; the audio module silently skips
    // a missing file at load.
    struct {
        char openworld_path[RES_PATH_LEN];
        char combat_path[RES_PATH_LEN];
        char tune_walk[RES_PATH_LEN];
        char tune_bump[RES_PATH_LEN];
        char tune_chest[RES_PATH_LEN];
        char tune_defeat[RES_PATH_LEN];
    } audio;

    // Strict-strings bookkeeping: the engine ships NO hardcoded user-facing
    // text. Every string comes from the pack; a key the pack omits is counted
    // here (and printed) and makes resources_load() hard-fail. Never a silent
    // fallback.
    int  strings_missing;

} Resources;

// Build an absolute (or repo-relative) path by joining the pack root and
// `rel`. Writes "" to `out` when `rel` is NULL or empty. The engine uses
// this to open every file declared by the manifest -- there is no
// hardcoded prefix in C.
void resources_resolve_path(const Resources *res, const char *rel,
                            char *out, size_t cap);

// Load game.json at `manifest_path` and every referenced table file. Paths
// inside game.json are resolved relative to the manifest file's directory.
// Returns true on success; writes a human-readable error to stderr on
// failure and leaves `*res` in an indeterminate state.
bool resources_load(Resources *res, const char *manifest_path);

// Every pack-relative art path this manifest resolves to, de-duplicated.
// Returns the count written to `out` (capped at `cap`).
//
// This is the single source of truth for "what art does this pack use". Art
// used to be reachable five different ways -- explicit paths here, bare
// tile_codes names expanded under art/tiles/, a list hardcoded in the shell,
// villain frames derived from a portrait filename, and the placed-object
// names map.c stamps by interact kind -- so no caller could answer that
// question without replicating all five. Tile names, the villain stem
// fallback and the object names are expanded here so callers see real paths.
#define RES_ART_MANIFEST_MAX 512
int resources_art_manifest(const Resources *res, char out[][RES_PATH_LEN],
                           int cap);
// Override the locale used for the next resources_load. Strings load from
// strings/<lang>.json in the pack; a locale file that is absent falls back to
// the pack's base locale (world.language). Pass NULL or "" to clear the
// override and use the pack's base locale. Wired to the --lang CLI flag.
void resources_set_locale(const char *lang);

// Currently a no-op (all storage is inline). Kept as the proper teardown
// hook in case any catalog-table allocation grows beyond inline storage.
void resources_free(Resources *res);

// Return the most-recently loaded Resources pointer (set by resources_load)
// so lookup helpers that don't take a Resources* can still read combat
// rules, catalogs, etc. NULL before the first load.
const Resources *resources_current(void);

// Re-publish `res` as the singleton behind the catalog lookups (troop_by_id
// etc.). resources_load publishes automatically and resources_free retracts;
// a consumer that runs a PRIVATE Resources lifecycle while a host one stays
// live (autoplay's oracle world inside the visible shell) restores the host
// catalog with this on its way out. NULL is legal (explicit unpublish).
void resources_republish(const Resources *res);

// ---- Lookups ---------------------------------------------------------------

// Global town lookup by (zone id, x, y). Returns NULL if no town sits at
// those coords. () key.
const ResTown   *resources_town_at(const Resources *r,
                                   const char *zone, int x, int y);
const ResTown   *resources_town_by_id(const Resources *r, const char *id);
const ResTown   *resources_town_by_index(const Resources *r, int index);

// Zone town enumeration over the authoritative catalog: `z->town_count` towns,
// the n-th being `&r->towns[z->town_idx[n]]` (in the zone's JSON town order). The
// accessor returns NULL on an out-of-range n or a stale index.
const ResTown   *resources_zone_town(const Resources *r, const ResZone *z, int n);

const ResCastle *resources_castle_at(const Resources *r,
                                     const char *zone, int x, int y);
const ResCastle *resources_castle_by_id(const Resources *r, const char *id);
// Parse a castles[].footprint string. Absent/empty and "3x2" are the default;
// "1x1" is the single-tile castle. Returns false (and writes the default) for
// anything else, so the caller can report the unknown value.
bool resources_parse_castle_footprint(const char *s, ResCastleFootprint *out);

// The home/audience castle -- the King's castle (contract audiences, the recruit
// home pool); never a gate destination. The "which castle is home" rule lives
// HERE (the special.flow marker is pack data); no other layer tests the flow id.
bool resources_castle_is_home(const ResCastle *rc);            // rc may be NULL
const ResCastle *resources_home_castle(const Resources *r);    // NULL if none

const ResZone   *resources_zone_by_id(const Resources *r, const char *id);
// Index of zone `id` in r->zones[] (parallel to Game.world.zones_discovered[]),
// or -1 if unknown. The ONE zone-id -> index lookup (callers add their own
// GAME_CONTINENTS bound before indexing zones_discovered[]).
int              resources_zone_index(const Resources *r, const char *id);

const ResVillainDesc *resources_villain_desc(const Resources *r,
                                             const char *villain_id);

// Look up the first ResCountBucket whose `threshold >= count`. Returns
// `fallback` (typically empty string) if the bucket list is empty.
const char *resources_count_bucket_label(const ResCountBucket *buckets,
                                         int n, int count,
                                         const char *fallback);

// One key/value substitution pair for resources_format_template().
typedef struct {
    const char *key;     // bare token name without surrounding %, e.g. "GOLD"
    const char *value;   // already-stringified value
} ResTemplateVar;

// Substitute %TOKEN% occurrences in `src` using the supplied (key, value)
// pairs and write the result to `out` (always NUL-terminated). Unknown
// tokens are left in place. Safe with NULL/empty `src`.
void resources_format_template(char *out, int out_sz, const char *src,
                               const ResTemplateVar *vars, int nvars);

#endif
