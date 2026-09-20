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

#define RES_ID_LEN            32
#define RES_NAME_LEN          48
#define RES_SIGN_TITLE_LEN    64
#define RES_SIGN_BODY_LEN    128
#define RES_PATH_LEN         128
// Indexed by raw map byte, so the table spans the whole byte range: a map
// file's code can be any of 256 values and always indexes this table. It was
// 128 (printable ASCII and below) until Rome used 91 of them and the road
// end pieces needed four more. A map that uses a code above 127 is no longer
// plain ASCII -- the reader is byte-wise, so such a file is latin-1.
#define RES_TILE_CODE_COUNT  256
#define RES_TILE_ART_LEN      24
// Animation cycle default: OB_ANIM_FRAMES_DEFAULT, in tables.h.
#define RES_COMBAT_TILES      15     // combat tileset, fixed role order
#define RES_END_BODY_LEN     512     // win/lose body text
#define RES_VDESC_TEXT_LEN   320     // per-villain features / crimes block
#define RES_SPELL_LORE_LEN   1024    // per-spell long description (strings.spell_lore)
#define RES_DOCK_TEXT_LEN    256     // strings.town_docks: where a town's boat waits

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
    char (*frames[OB_FACE_COUNT])[RES_PATH_LEN];   // heap per facing, count[f] frames
} ResAnimSet;

// Optional per-class hero art (a class entry's "hero" block). Any part a
// class leaves out falls back to the pack-wide sprites.hero / ending.hero_tile.
typedef struct {
    ResAnimSet walk, idle, boat;
    char tile[RES_PATH_LEN];      // win-cartoon hero tile
    char disgraced[RES_PATH_LEN]; // modern: the temporary-death scene (a 240x102 backdrop)
} ResClassHero;

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
    int alcove_cost;              // the alcove's price; a zone may set its own
    // Modern rites (game.json "magic.rites_per_zone"): each zone's alcove
    // teaches that zone's rites, and its towns sell spells only to a hero who
    // has them. Off by default, so a pack that does not ask keeps one magic.
    bool rites_per_zone;
    // game.json "foes.evade_needs_free_square": a hostile foe can be evaded
    // only while a square around the hero is free (GameFoeCanEvade). Off by
    // default, so a pack that does not ask keeps the free decline.
    bool evade_needs_free_square;
    // game.json "audiences": the modern home castle's Blessing and Tribute
    // (GameSeekBlessing, GamePayTribute). Off unless the object is present.
    bool audiences;
    int  blessing_leadership_pct;   // one time, once every artifact is found
    int  tribute_cost;              // gold per tribute; any number of times
    int  tribute_leadership_pct;
    int  tribute_magic_pct;         // spell power and spell capacity each
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
//  - troop_pool[kind][5..6]: troop ids in order of rarity. A pool may hold a
//    sixth troop; a pool longer than five then needs its own curve,
//    kind_curve[kind][tier] (spawn.kind_chance_curve), so every five-troop
//    pool keeps exactly the shared curve and the roll it always had.
// Count per roll is read from TroopDef.tier_counts[tier].
#define RES_SPAWN_TIERS 4
// Four dwelling kinds and four difficulty tiers are rules. Everything a pack
// lists inside them -- how many troops a pool holds, how many thresholds a
// curve has -- is heap, sized to what it declares.
typedef struct {
    int   *chance_curve[RES_SPAWN_TIERS];          // shared curve per tier
    int    chance_curve_len[RES_SPAWN_TIERS];
    char (*troop_pool[RES_SPAWN_TIERS])[RES_ID_LEN];   // per kind, in order of rarity
    int    pool_count[RES_SPAWN_TIERS];
    bool   kind_curve_set[RES_SPAWN_TIERS];
    int   *kind_curve[RES_SPAWN_TIERS][RES_SPAWN_TIERS];   // [kind][tier]
    int    kind_curve_len[RES_SPAWN_TIERS][RES_SPAWN_TIERS];
} ResSpawn;

// The pool slot a chance roll (1..100) picks in pool `kind` at difficulty
// `tier`: the first slot whose cumulative threshold the roll does not pass.
// A pool of five or fewer walks as it always did (to slot 4, a missing
// threshold counting as 0); a longer pool walks to its own last slot.
int resources_spawn_slot(const ResSpawn *sp, int kind, int tier, int chance);
// The troop id that roll picks ("" when the slot names none).
const char *resources_spawn_troop(const ResSpawn *sp, int kind, int tier, int chance);

// Render geometry, declared by the pack. There is no default: a pack must say
// which mode it is authored for, because the two are not interchangeable --
// legacy art is drawn for a 48x34 tile and modern art for a square one. A pack
// that declares neither is rejected at load rather than guessed at.
typedef enum {
    RENDER_MODE_NONE = 0,     // nothing declared -> fatal
    RENDER_MODE_LEGACY,       // 320x200, 48x34 tiles, 5x5 viewport
    RENDER_MODE_MODERN,       // pack-declared tile size and viewport
} RenderMode;

// A TrueType/OpenType font declared by a modern pack ("font" block). The
// shell rasterises it at load into the layout's fixed glyph cell. Empty
// `file` means the pack uses its bitmap strip (sprites.font) as always.
typedef struct {
    char file[RES_PATH_LEN];      // .ttf/.otf inside the pack
    int  size;                    // requested pixel size; 0 = largest that fits the cell
    int  caps;                    // 1: every string is drawn in capitals
    char license[RES_PATH_LEN];   // licence text shipped beside it (manifest only)
} ResFont;

typedef struct {
    RenderMode mode;
    int tile_w, tile_h;       // modern only; legacy forces 48x34
    int tiles_w, tiles_h;     // viewport in tiles; must be odd
    int ui_scale;             // multiplies the font and the chrome bands. A pack
                              // that doubles its tile must say so, or its
                              // furniture stays at 320x200 size around giant
                              // tiles. Legacy is 1.
    int native_w, native_h;   // modern only, optional: a FIXED buffer size.
                              // The viewport (tiles_w x tiles_h) sits in it and
                              // the space left over becomes chrome bands; the
                              // window shows the buffer at 1x, 2x or 3x. Zero
                              // means the buffer is derived from the window.
    int dim;                  // modern only: how much the scene darkens under a
                              // detail view, prompt or dialog, 0..100 percent
                              // of black (REQ-430g). Default 55. Legacy is 0.
} ResRender;

typedef struct {
    char starting_zone[RES_ID_LEN];
    char zone_noun[RES_ID_LEN];
    char zone_noun_plural[RES_ID_LEN];
    char language[RES_ID_LEN];    // base locale code; strings load from strings/<language>.json
    int  max_army_slots;
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
    char informant[RES_ID_LEN];   // portraits[] id shown for the town's report ("" = none)
    char headman[RES_ID_LEN];     // portraits[] id of the figure on the town backdrop
    char townhead[RES_ID_LEN];    // portraits[] id of that person's portrait (Contracts, main page)
    char invitations[RES_ID_LEN]; // strings.town_invitations block spoken on the main page
    bool intel_artifact;          // informant names an artifact's whereabouts,
                                  // not a castle's garrison (intel_castle unused)
    // This town's own screen backdrop, overriding its zone's and the pack's
    // (REQ-221d). Empty = the zone's, else sprites.ui.town_backdrop.
    char backdrop[RES_PATH_LEN];
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
    // economy.audiences: the Emperor's words for Blessing and Tribute
    // (%NEEDED% = artifacts still missing / gold still short).
    char audience_blessing_granted[320];
    char audience_blessing_needed[320];
    char audience_blessing_already[320];
    char audience_tribute_paid[320];
    char audience_tribute_needed[320];
    // Modern castle screen (all optional): portraits[] ids of the ruler's
    // portrait and standing figure, and of the image shown on promotion to
    // each rank (promotion[r], r = the new rank index; "" = none).
    char portrait[RES_ID_LEN];
    char figure[RES_ID_LEN];
    char promotion[4][RES_ID_LEN];
    // The barracks keeper shown on the main and Recruit pages (the ruler then
    // appears only on Audience); "" = the ruler everywhere.
    char barracks_portrait[RES_ID_LEN];
    char barracks_figure[RES_ID_LEN];
    // The figure who greets you in the hall (the Emperor's palace usher).
    char greeter_figure[RES_ID_LEN];
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
    char art[RES_TILE_ART_LEN];   // 1x1 only: its own tile art stem under art/tiles/ ("" = "castle")
    ResCastleSpecial special;
} ResCastle;

// ---- Tile code (single byte in the .dat -> art + flags) -------------------


typedef struct {
    bool present;                 // false = unused code
    char art[RES_TILE_ART_LEN];
    int  terrain;                 // Terrain enum value (see tile.h)
    bool blocks_foot;
    bool is_bridge;
    // Optional cosmetic variants of `art` (same terrain, same flags). The
    // shell picks one per cell at draw time (src/tilevar.c); the engine,
    // the .dat and saves never see them (OPENBOUNTY-SPEC REQ-229d).
    int  variant_count;          // cosmetic art variants (a name may repeat to weight it)
    char (*variants)[RES_TILE_ART_LEN];   // heap, variant_count
    // A landmark tile (the Pharos) is drawn over its own ground: the art named
    // here is laid down first, so the tile's transparent parts show terrain
    // rather than black. Empty = the tile is its own ground, as terrain is.
    char ground[RES_TILE_ART_LEN];
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


typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    int  decor_count;
    ResCastleDecor *decorations;   // heap, decor_count entries
} ResZoneCastle;

typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    bool fixed;          // "fixed": true -- always a chest, never salted
    int  gold;           // "gold": N -- this chest always holds N, never rolled
} ResZoneChest;

typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
} ResZoneArtifact;

typedef struct {
    int  x, y;
    char id[RES_ID_LEN];
    char kind[RES_ID_LEN];        // "plains" / "forest" / "hills" / "dungeon"
    // "troop": this dwelling always breeds that troop, instead of the roll
    // from the zone's pool (the elephant park at Apamea). Empty = rolled.
    char troop[RES_ID_LEN];
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
    // "requires_troop": this foe cannot be fought unless that troop stands in
    // the hero's army (Oriens' elephant gate). Empty = anyone may attack.
    char requires_troop[RES_ID_LEN];
    // "scene": the picture shown when it turns the hero back. Indexed into
    // Resources.event_scenes with the vistas' art (REQ-221b).
    char scene[RES_PATH_LEN];
    int  scene_index;
} ResZoneArmy;

// ---- Strings  -----

typedef struct {
    char id[RES_ID_LEN];
    char alias[RES_NAME_LEN];
    char features[RES_VDESC_TEXT_LEN];
    char crimes[RES_VDESC_TEXT_LEN];
} ResVillainDesc;

typedef struct {
    char id[RES_ID_LEN];
    char text[RES_SPELL_LORE_LEN];
} ResSpellLore;

// A portrait with an idle loop (game.json portraits[]): the town's informant
// and the zone's priest, drawn in the modern town's portrait slot.
typedef struct {
    char id[RES_ID_LEN];
    int  anim_count;
    char (*anim)[CAT_PATH_LEN];     // heap, anim_count frames
} ResPortrait;

typedef struct {
    char id[RES_ID_LEN];
    char text[RES_DOCK_TEXT_LEN];
} ResTownDock;

// One strings.town_invitations block (one per town, named by the town's
// "invitations"): what each town section's person says on
// the modern town main page. %HERO% and %TOWN% substitute.
#define RES_INVITE_LEN 256
typedef struct {
    char id[RES_ID_LEN];
    char contracts[RES_INVITE_LEN];
    char boat[RES_INVITE_LEN];
    char information[RES_INVITE_LEN];
    char temple[RES_INVITE_LEN];
    char siege[RES_INVITE_LEN];
} ResTownInvite;

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
    char town_intro[RES_BANNER_LEN];
    char town_intro_inland[RES_BANNER_LEN];   // a town with no dock: no harbour
    char town_visit[RES_BANNER_LEN];
    char cv_wanted[RES_BANNER_LEN];
    char cv_no_contract_hint[RES_BANNER_LEN];
    char puzzle_legend[RES_BANNER_LEN];
    char gate_travel[RES_BANNER_LEN];
    char worldmap_all[RES_BANNER_LEN];
    char worldmap_you[RES_BANNER_LEN];
    char worldmap_boat[RES_BANNER_LEN];
    char worldmap_boat_elsewhere[RES_BANNER_LEN];
    char worldmap_no_boat[RES_BANNER_LEN];
    char class_desc_knight[RES_BANNER_LEN];
    char class_desc_paladin[RES_BANNER_LEN];
    char class_desc_sorceress[RES_BANNER_LEN];
    char class_desc_barbarian[RES_BANNER_LEN];
    char spell_bridge_prompt_modern[RES_BANNER_LEN];
    char save_done_title[RES_BANNER_LEN];
    char save_done[RES_BANNER_LEN];
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
    // A town whose informant reports on artifacts rather than a castle
    // (ResTown.intel_artifact): %ZONE%, %X%, %Y%.
    char town_intel_artifact[RES_BANNER_LEN];
    char town_intel_artifact_none[RES_BANNER_LEN];
    // Messages the engine raises through player_io_message. Substitutions:
    // artifact_found %ARTIFACT%, castle_header %NAME%.
    char artifact_found[RES_BANNER_LEN];
    char artifact_map_piece[RES_BANNER_LEN];
    char castle_header[RES_BANNER_LEN];
    char castle_siege_monsters[RES_BANNER_LEN];
    char castle_uncharted[RES_BANNER_LEN];
    char search_nothing[RES_BANNER_LEN];
    char zone_unreachable[RES_BANNER_LEN];
    char town_spell_unavailable[RES_BANNER_LEN];
    char town_spell_at_cap[RES_BANNER_LEN];
    char town_spell_can_learn[RES_BANNER_LEN];   // %LEFT% %S% (s/empty)
    char town_siege_already[RES_BANNER_LEN];
    char town_siege_purchased[RES_BANNER_LEN];
    // Modern town menu: short row labels, and lines for the detail panel.
    char town_menu_contract[RES_BANNER_LEN];
    char town_menu_boat_rent[RES_BANNER_LEN];
    char town_menu_boat_cancel[RES_BANNER_LEN];
    char town_menu_info[RES_BANNER_LEN];
    char town_menu_spell[RES_BANNER_LEN];
    char town_menu_siege[RES_BANNER_LEN];
    char town_detail_boat_dock[RES_BANNER_LEN];  // %X% %Y%
    char town_detail_intel[RES_BANNER_LEN];      // %CASTLE%
    char town_contract_confirm[RES_BANNER_LEN];
    char foe_fight[RES_BANNER_LEN];
    char foe_evade[RES_BANNER_LEN];
    char foe_evade_blocked[RES_BANNER_LEN];
    char foe_requires_troop[RES_BANNER_LEN];   // %TROOP%: the arm a gate demands
    // Modern castle screens (home castle recruit/audience, own castle garrison).
    char castle_menu_recruit[RES_BANNER_LEN];
    char castle_continue[RES_BANNER_LEN];
    char castle_menu_audience[RES_BANNER_LEN];
    char castle_menu_garrison[RES_BANNER_LEN];
    char castle_menu_withdraw[RES_BANNER_LEN];
    char castle_action_audience[RES_BANNER_LEN];
    char castle_invite_recruit[RES_BANNER_LEN];
    char castle_invite_audience[RES_BANNER_LEN];
    char castle_invite_garrison[RES_BANNER_LEN];
    char castle_invite_withdraw[RES_BANNER_LEN];
    char castle_have[RES_BANNER_LEN];
    char castle_in_garrison[RES_BANNER_LEN];
    char castle_can_recruit[RES_BANNER_LEN];
    char castle_needs_leadership[RES_BANNER_LEN];
    char castle_rank[RES_BANNER_LEN];
    char castle_next_rank[RES_BANNER_LEN];
    char castle_needed[RES_BANNER_LEN];
    char castle_gain_leadership[RES_BANNER_LEN];
    char castle_gain_commission[RES_BANNER_LEN];
    char castle_gain_spells[RES_BANNER_LEN];
    char castle_gain_spell_power[RES_BANNER_LEN];
    char castle_action_promotion[RES_BANNER_LEN];
    char castle_action_blessing[RES_BANNER_LEN];
    char castle_action_tribute[RES_BANNER_LEN];
    char castle_tribute_confirm[RES_BANNER_LEN];
    char castle_artifacts[RES_BANNER_LEN];
    char castle_over_leadership[RES_BANNER_LEN];
    char castle_count_of[RES_BANNER_LEN];
    char castle_cost[RES_BANNER_LEN];
    char castle_no_troops[RES_BANNER_LEN];
    char town_temple_needs_rites[RES_BANNER_LEN]; // %HERO% %ZONE% %X% %Y% (the zone's alcove)
    char town_back[RES_BANNER_LEN];              // the Back row of a town section page
    char town_menu_boat[RES_BANNER_LEN];
    char town_action_spell[RES_BANNER_LEN];
    char town_action_siege[RES_BANNER_LEN];
    char town_action_owned[RES_BANNER_LEN];
    char town_boat_no_master[RES_BANNER_LEN];
    char town_boat_rented[RES_BANNER_LEN];
    char town_boat_returned[RES_BANNER_LEN];
    char town_siege_lore[RES_BANNER_LEN];
    char town_confirm_boat_rent[RES_BANNER_LEN];
    char town_confirm_boat_cancel[RES_BANNER_LEN];
    char town_confirm_spell[RES_BANNER_LEN];
    char town_confirm_siege[RES_BANNER_LEN];

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
    // The title over that offer ("%TROOP%"); a pack that wants none leaves it
    // empty, and the offer shows its words alone, as King's Bounty always has.
    char encounter_join_title[RES_BANNER_LEN];
    char encounter_wanderers[RES_BANNER_LEN];      // friendly refused
    char encounter_hostile_header[RES_BANNER_LEN]; // composite prefix
    char encounter_hostile_unknown[RES_BANNER_LEN];// fallback line
    char encounter_hostile_count_named[RES_BANNER_LEN];   // %LABEL% %TROOP%
    char encounter_hostile_count_numeric[RES_BANNER_LEN]; // %COUNT% %TROOP%

    // Archmage Aurange alcove .
    // Substitutions: %COST%.
    char alcove_offer[RES_BANNER_LEN];
    char alcove_already[RES_BANNER_LEN];
    // Modern temple and dwelling screens.
    char temple_title[RES_BANNER_LEN];
    char temple_learn[RES_BANNER_LEN];
    char location_leave[RES_BANNER_LEN];
    char alcove_offer_modern[RES_BANNER_LEN];
    // Modern menus: row descriptions (gmd_), greyed reasons (gmr_), confirms (gmc_).
    char gmd_hero[RES_BANNER_LEN];
    char gmd_world[RES_BANNER_LEN];
    char gmd_game[RES_BANNER_LEN];
    char gmd_back_up[RES_BANNER_LEN];
    char gmd_unit[RES_BANNER_LEN];
    char fv_your_army[RES_BANNER_LEN];
    char loc_joined[RES_BANNER_LEN];
    char loc_gold_change[RES_BANNER_LEN];
    char count_heading[RES_BANNER_LEN];
    char count_of_lead[RES_BANNER_LEN];
    char count_of_army[RES_BANNER_LEN];
    char count_of_garrison[RES_BANNER_LEN];
    char count_cost[RES_BANNER_LEN];
    char count_gold_left[RES_BANNER_LEN];
    char count_recruit[RES_BANNER_LEN];
    char count_garrison[RES_BANNER_LEN];
    char count_withdraw[RES_BANNER_LEN];
    char count_cancel[RES_BANNER_LEN];
    char count_min[RES_BANNER_LEN];
    char count_max[RES_BANNER_LEN];
    char capture_title[RES_BANNER_LEN];
    char capture_contract[RES_BANNER_LEN];
    char capture_free[RES_BANNER_LEN];
    char capture_promoted[RES_BANNER_LEN];
    char temple_intro[RES_BANNER_LEN];
    char dwelling_intro[RES_BANNER_LEN];
    char loc_title_taught[RES_BANNER_LEN];
    char loc_title_refused[RES_BANNER_LEN];
    char loc_title_known[RES_BANNER_LEN];
    char loc_title_joined[RES_BANNER_LEN];
    char loc_title_none[RES_BANNER_LEN];
    char gmd_leave[RES_BANNER_LEN];
    char gmd_army[RES_BANNER_LEN];
    char gmd_character[RES_BANNER_LEN];
    char gmd_contract[RES_BANNER_LEN];
    char gmd_puzzle[RES_BANNER_LEN];
    char gmd_dismiss[RES_BANNER_LEN];
    char gmd_map[RES_BANNER_LEN];
    char gmd_cast[RES_BANNER_LEN];
    char gmd_search[RES_BANNER_LEN];
    char gmd_fly[RES_BANNER_LEN];
    char gmd_land[RES_BANNER_LEN];
    char gmd_end_week[RES_BANNER_LEN];
    char gmd_rest[RES_BANNER_LEN];
    char gmd_sail[RES_BANNER_LEN];
    char gmd_controls[RES_BANNER_LEN];
    char gmd_save[RES_BANNER_LEN];
    char gmd_load[RES_BANNER_LEN];
    char gmd_new_game[RES_BANNER_LEN];
    char gmd_exit[RES_BANNER_LEN];
    char gmd_back[RES_BANNER_LEN];
    char gmd_debug[RES_BANNER_LEN];
    char gmd_wait[RES_BANNER_LEN];
    char gmd_shoot[RES_BANNER_LEN];
    char gmd_unit_fly[RES_BANNER_LEN];
    char gmd_combat_cast[RES_BANNER_LEN];
    char gmd_combat_army[RES_BANNER_LEN];
    char gmd_combat_character[RES_BANNER_LEN];
    char gmd_give_up[RES_BANNER_LEN];
    char gmd_slot[RES_BANNER_LEN];
    char gmr_no_troops[RES_BANNER_LEN];
    char gmr_not_sailing[RES_BANNER_LEN];
    char gmr_no_saves[RES_BANNER_LEN];
    char gmr_no_shots[RES_BANNER_LEN];
    char gmr_adjacent[RES_BANNER_LEN];
    char gmr_cannot_fly[RES_BANNER_LEN];
    char gmr_one_spell[RES_BANNER_LEN];
    char gmr_no_magic[RES_BANNER_LEN];
    char gmr_no_spell_held[RES_BANNER_LEN];
    char gmr_empty_slot[RES_BANNER_LEN];
    char gmc_overwrite[RES_BANNER_LEN];
    char gmc_load[RES_BANNER_LEN];
    char gmc_exit[RES_BANNER_LEN];
    char dwelling_recruit_row[RES_BANNER_LEN];
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
    // Modern: the top bar opens the menu, so it reads "Menu" in place of
    // "Options / Controls".
    char status_days_left_modern[RES_BANNER_LEN];
    char status_time_stop_modern[RES_BANNER_LEN];
    char status_menu_prefix[RES_BANNER_LEN];
    char status_game_menu[RES_BANNER_LEN];
    char status_days_remaining[RES_BANNER_LEN];
    char status_time_stop_remaining[RES_BANNER_LEN];
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
    char body_navigate_confirm[RES_BANNER_LEN];     // %ZONE%: the sail-to confirmation
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
    // Modern foe view: short stat labels.
    char fv_hp[RES_UI_LABEL_LEN];
    char fv_skill[RES_UI_LABEL_LEN];
    char fv_dmg[RES_UI_LABEL_LEN];
    char fv_move[RES_UI_LABEL_LEN];
    char fv_range[RES_UI_LABEL_LEN];
    char fv_flies[RES_UI_LABEL_LEN];
    // Modern character view.
    char cv_army[RES_UI_LABEL_LEN];
    char cv_magic[RES_UI_LABEL_LEN];
    char cv_campaign[RES_UI_LABEL_LEN];
    char cv_leadership[RES_UI_LABEL_LEN];
    char cv_commission[RES_UI_LABEL_LEN];
    char cv_gold[RES_UI_LABEL_LEN];
    char cv_spell_power[RES_UI_LABEL_LEN];
    char cv_spell_capacity[RES_UI_LABEL_LEN];
    char cv_captured[RES_UI_LABEL_LEN];
    char cv_artifacts[RES_UI_LABEL_LEN];
    char cv_castles[RES_UI_LABEL_LEN];
    char cv_followers[RES_UI_LABEL_LEN];
    char cv_score[RES_UI_LABEL_LEN];
    char cv_days[RES_UI_LABEL_LEN];
    char cv_sacred[RES_UI_LABEL_LEN];
    char cv_continents[RES_UI_LABEL_LEN];
    char cv_honours[RES_UI_LABEL_LEN];
    char cv_blessed[RES_UI_LABEL_LEN];
    char cv_tributes[RES_UI_LABEL_LEN];
    char cv_rites[RES_UI_LABEL_LEN];
    char cv_yes[RES_UI_LABEL_LEN];
    char cv_no[RES_UI_LABEL_LEN];
    char cv_next[RES_UI_LABEL_LEN];
    char cv_top_rank[RES_UI_LABEL_LEN];
    // Modern hint buttons: labels, key names by device, key-free texts.
    char hint_back[RES_UI_LABEL_LEN];
    char hint_quit[RES_UI_LABEL_LEN];
    char hint_continue[RES_UI_LABEL_LEN];
    char key_esc[RES_UI_LABEL_LEN];
    char key_ctrl_q[RES_UI_LABEL_LEN];
    char pad_back[RES_UI_LABEL_LEN];
    char pad_confirm[RES_UI_LABEL_LEN];
    char give_up_header_modern[RES_UI_LABEL_LEN];
    char save_confirm_modern[RES_UI_LABEL_LEN];
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
    char menu_screens[RES_UI_LABEL_LEN];   // modern game menu pages
    char menu_actions[RES_UI_LABEL_LEN];
    // Modern game and combat menus.
    char gm_title[RES_UI_LABEL_LEN];
    char gm_hero[RES_UI_LABEL_LEN];
    char gm_world[RES_UI_LABEL_LEN];
    char gm_game[RES_UI_LABEL_LEN];
    char gm_army[RES_UI_LABEL_LEN];
    char gm_character[RES_UI_LABEL_LEN];
    char gm_contract[RES_UI_LABEL_LEN];
    char gm_puzzle[RES_UI_LABEL_LEN];
    char gm_dismiss[RES_UI_LABEL_LEN];
    char gm_map[RES_UI_LABEL_LEN];
    char gm_cast[RES_UI_LABEL_LEN];
    char gm_search[RES_UI_LABEL_LEN];
    char gm_fly[RES_UI_LABEL_LEN];
    char gm_land[RES_UI_LABEL_LEN];
    char gm_end_week[RES_UI_LABEL_LEN];
    char gm_rest[RES_UI_LABEL_LEN];
    char gm_sail[RES_UI_LABEL_LEN];
    char gm_controls[RES_UI_LABEL_LEN];
    char gm_save[RES_UI_LABEL_LEN];
    char gm_load[RES_UI_LABEL_LEN];
    char gm_new_game[RES_UI_LABEL_LEN];
    char gm_exit[RES_UI_LABEL_LEN];
    char gm_back[RES_UI_LABEL_LEN];
    char gm_close[RES_UI_LABEL_LEN];   // the top menu page's last-but-one row
    char gm_debug[RES_UI_LABEL_LEN];
    char gm_actions[RES_UI_LABEL_LEN];
    char gm_unit[RES_UI_LABEL_LEN];
    char gm_wait[RES_UI_LABEL_LEN];
    char gm_shoot[RES_UI_LABEL_LEN];
    char gm_give_up[RES_UI_LABEL_LEN];

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
    int             count_buckets_army_view_n;
    ResCountBucket *count_buckets_army_view;      // heap
    int             count_buckets_instant_army_n;
    ResCountBucket *count_buckets_instant_army;   // heap

    // Difficulty labels (indexed by Difficulty enum 0..3).
    ResDifficultyLabel difficulty[4];

    // Keybind labels.
    int         keybind_count;
    ResKeybind *keybinds;                // heap

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
    char prompt_yes[RES_UI_LABEL_LEN];         // modern yes/no answer rows
    char prompt_no[RES_UI_LABEL_LEN];
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
    // Modern menu rows (location screens, world map, class select).
    char home_castle_recruit[RES_UI_LABEL_LEN];
    char home_castle_audience[RES_UI_LABEL_LEN];
    char own_castle_row_garrison[RES_UI_LABEL_LEN];
    char own_castle_row_remove[RES_UI_LABEL_LEN];
    char worldmap_row_your_map[RES_UI_LABEL_LEN];
    char worldmap_row_whole_map[RES_UI_LABEL_LEN];
    char class_select_load[RES_UI_LABEL_LEN];
    char class_select_arrows[RES_UI_LABEL_LEN * 2];
    // Modern title menu (before class select) and the in-game New Game.
    char title_new_adventure[RES_UI_LABEL_LEN];
    char title_load_adventure[RES_UI_LABEL_LEN];
    char title_credits[RES_UI_LABEL_LEN];
    char new_game_confirm[RES_UI_LABEL_LEN * 2];
    char hero_name_label[RES_UI_LABEL_LEN];
    char combat_act_wait[RES_UI_LABEL_LEN];
    char combat_act_shoot[RES_UI_LABEL_LEN];
    char combat_act_fly[RES_UI_LABEL_LEN];
    char combat_act_cast[RES_UI_LABEL_LEN];
    char combat_act_controls[RES_UI_LABEL_LEN];
    char combat_act_give_up[RES_UI_LABEL_LEN];

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
    char (*preferred_troops)[RES_ID_LEN];    // heap, preferred_troop_count
    int  dwelling_range_min;   // troop catalog index (inclusive)
    int  dwelling_range_max;   // troop catalog index (inclusive)
} ResZoneSalt;
typedef struct {
    char from[RES_ID_LEN];   // the zone sailed from
    int  x, y;
} ResZoneArrival;


// One precondition of a zone event: what the hero must hold for it to fire.
// `count` is how much (charges, troops, gold, 1 for an artifact); `consume`
// spends it when the event fires.
typedef enum {
    RES_EVENT_REQ_SPELL = 0,
    RES_EVENT_REQ_TROOP,
    RES_EVENT_REQ_GOLD,
    RES_EVENT_REQ_ARTIFACT,
} ResEventReqKind;

typedef struct {
    ResEventReqKind kind;
    char id[RES_ID_LEN];     // spell / troop / artifact id; empty for gold
    int  count;
    bool consume;
} ResEventReq;

// What a fired event changes. A tile effect turns (x, y) into the tile that
// `code` names in tile_codes (a bridge over a river). A reveal effect lifts the
// fog over the whole zone (climbing the Pharos).
typedef enum {
    RES_EVENT_FX_TILE = 0,
    RES_EVENT_FX_REVEAL,
} ResEventFxKind;

typedef struct {
    ResEventFxKind kind;
    int  x, y;
    unsigned char code;       // a tile_codes key, resolved at parse
} ResEventEffect;

// A one-time vista: stepping onto (x, y) with every precondition held plays a
// full-width scene (art `scene`, `title` / `body`, Continue), applies the
// effects for good, and never fires again. Heap lists, sized by the pack.
typedef struct {
    char id[RES_ID_LEN];
    int  x, y;
    char scene[RES_PATH_LEN];
    int  scene_index;        // into Resources.event_scenes (-1 when none)
    char title[RES_NAME_LEN];
    char body[RES_BANNER_LEN];
    int  req_count;       ResEventReq    *reqs;
    int  effect_count;    ResEventEffect *effects;
} ResZoneEvent;

typedef struct {
    char id[RES_ID_LEN];
    char name[RES_NAME_LEN];
    char map_path[RES_PATH_LEN];
    // Optional terrain art folder: when set, every tile_codes art name for
    // this zone resolves under art/tiles/<tile_set>/ instead of art/tiles/.
    // Empty means the shared set. Object tiles (towns, castles, chests...)
    // are never affected.
    // This zone's town-screen backdrop; empty falls back to the pack's
    // sprites.ui.town_backdrop (REQ-221d).
    char town_backdrop[RES_PATH_LEN];
    char tile_set[RES_ID_LEN];
    // Optional overrides ("tile_set_arts"): when listed, only these art names
    // come from the zone's folder and every other name from the master
    // art/tiles/ set. Empty means the whole folder. Heap, sized by the pack.
    int  tile_set_art_count;
    char (*tile_set_arts)[RES_TILE_ART_LEN];
    // Optional wandering-army tile art for this zone (a stem under
    // art/tiles/). Empty means the shared "wandering_army".
    char army_art[RES_TILE_ART_LEN];
    // The map tile the zone's magic alcove is drawn with. Empty falls back to
    // the hills-dwelling sprite, which is what the alcove borrowed before it
    // could name its own art.
    char alcove_art[RES_TILE_ART_LEN];
    char pontifex[RES_ID_LEN];    // portraits[] id of the priest who sells spells here
    int  alcove_cost;             // this zone's alcove price; -1 = economy.alcove_cost
    char boatmaster[RES_ID_LEN];  // portraits[] id of the zone's boat master
    char siegemaster[RES_ID_LEN]; // portraits[] id of the zone's siege engineer
    int  width, height;
    int  hero_spawn_x, hero_spawn_y;
    // Optional per-origin arrivals ("arrivals": {"<zone id>": {"x":..,"y":..}}):
    // sailing in from that zone lands here instead of hero_spawn. Heap, sized
    // by the pack; empty means every arrival uses hero_spawn.
    int  arrival_count;
    ResZoneArrival *arrivals;
    // Optional one-time vistas ("events"), in the order the pack declares them.
    int  event_count;
    ResZoneEvent *events;
    int  neighbor_count;
    char (*neighbors)[RES_ID_LEN];           // heap, neighbor_count

    // Per-zone object placements. Interactive overlays come from here --
    // the .dat only carries terrain + edge art.
    // Each list is heap, sized to what the zone declares.
    int  sign_count;     ResSign          *signs;
    // Towns in this zone, as INDICES into the authoritative res->towns[] catalog,
    // in the zone JSON's town order (resolved post-parse by id). Use the
    // resources_zone_town* accessors below.
    int  town_count;     int              *town_idx;
    int  castle_count;   ResZoneCastle    *castles;
    int  chest_count;    ResZoneChest     *chests;
    int  artifact_count; ResZoneArtifact  *artifacts;
    int  dwelling_count; ResZoneDwelling  *dwellings;
    int  army_count;     ResZoneArmy      *armies;

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
    ResFont     font;
    ResTuning   tuning;
    ResColors   colors;
    ResUI       ui;

    int         town_count;
    ResTown    *towns;                // heap, town_count entries

    int         castle_count;
    ResCastle  *castles;              // heap, castle_count entries

    int         zone_count;
    ResZone    *zones;                // heap, zone_count entries
    // Every distinct `events[].scene` the pack declares, in first-seen order.
    // A fired vista names its art by this index, so the shell loads the set
    // once and draws by index. Heap, sized by the pack.
    int         event_scene_count;
    char      (*event_scenes)[RES_PATH_LEN];
    // Parallel to classes[]: per-class hero art, all-zero when undeclared.
    ResClassHero *class_hero;          // heap, classes_count entries

    // Indexed by raw byte from the .dat. `present == false` means unused.
    ResTileCode tile_codes[RES_TILE_CODE_COUNT];

    // Catalogs (loaded from game.json top-level arrays).
    // Heap, troops_count entries: as many as the pack declares.
    int         troops_count;
    TroopDef   *troops;

    int         spells_count;
    SpellDef   *spells;               // heap, spells_count entries

    int         classes_count;
    ClassDef   *classes;              // heap, classes_count entries

    int         villains_count;
    VillainDef *villains;             // heap, villains_count entries

    int         artifacts_count;
    ArtifactDef *artifacts;           // heap, artifacts_count entries

    // Combat rules -- cross-group morale chart .
    // Indexed by (my_group - 'A', their_group - 'A'); values are 'N'/'L'/'H'.
    char morale_chart[5][5];

    // Fuzzy-number labels for intelligence / enemy-sight text
    // .
    // Entries are ordered high-to-low by threshold: the first entry
    // whose threshold is <= count wins. Up to 6 buckets.
    int   number_name_count;
    int  *number_name_thresholds;        // heap, number_name_count
    char (*number_name_labels)[24];      // heap, number_name_count

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
        struct ResCreditGroup {
            char label[64];           // e.g. "Programmed By:"
            char (*names)[64];        // heap, name_count indented names
            int  name_count;
        } *groups;                    // heap, group_count
        int group_count;
        char (*copyright)[64];        // heap, copyright_count centered footer lines
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
    ResVillainDesc *villain_descs;    // heap
    // Per-spell long descriptions (strings.spell_lore), optional.
    int             spell_lore_count;
    ResSpellLore   *spell_lore;       // heap
    // Per-spell one-line effects (strings.spell_brief), optional: what the
    // spell does and nothing else, for a menu's two-line description.
    int             spell_brief_count;
    ResSpellLore   *spell_brief;      // heap
    // portraits[]: as many as the pack declares (heap, released by
    // resources_free) -- town people, castle keepers, promotion images.
    int             portrait_count;
    ResPortrait    *portraits;
    int             town_dock_count;
    ResTownDock    *town_docks;       // heap
    int             town_invite_count;
    ResTownInvite  *town_invites;     // heap

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
        char (*view_icons_extra)[RES_PATH_LEN];        // heap
        // HUD panels.
        char hud_contract_silhouette[RES_PATH_LEN];
        char hud_boat_silhouette[RES_PATH_LEN];     // optional: the town Boat screen with no boat master
        char hud_siege_silhouette[RES_PATH_LEN];
        int  hud_siege_animation_count;
        char (*hud_siege_animation)[RES_PATH_LEN];     // heap
        char hud_magic_silhouette[RES_PATH_LEN];
        int  hud_magic_animation_count;
        char (*hud_magic_animation)[RES_PATH_LEN];     // heap
        // The magic alcove's own backdrop and the figure who keeps it. Both
        // optional: without the backdrop the alcove borrows the hill cave's,
        // and without the figure it animates whatever troop the screen names,
        // which is what it did before a pack could declare either.
        char alcove_backdrop[RES_PATH_LEN];
        char sail_backdrop[RES_PATH_LEN];   // the sail-to scene (REQ-221c)
        // Modern: the column in the bars beside a place backdrop -- its capital,
        // a shaft piece repeated between, and its base ("" = the lattice).
        // The Emperor's own castle: its three scenes (welcome, recruit,
        // audience). Empty: the shared castle backdrop.
        char palace_welcome[RES_PATH_LEN];
        char palace_barracks[RES_PATH_LEN];
        char palace_throne[RES_PATH_LEN];
        char scene_column_capital[RES_PATH_LEN];
        char scene_column_shaft[RES_PATH_LEN];
        char scene_column_base[RES_PATH_LEN];
        char alcove_figure[RES_PATH_LEN];
        int  alcove_figure_animation_count;
        char (*alcove_figure_animation)[RES_PATH_LEN]; // heap
        // Where the figure stands, in the backdrop's own 240x102 design units
        // (the card scales by ui_scale, and so does this). w 0 means none was
        // declared: the figure takes the tile-sized troop slot, one tile in
        // from the left and one up from the card's bottom edge.
        int  alcove_figure_x, alcove_figure_y, alcove_figure_w, alcove_figure_h;
        // Milliseconds each frame is held. 0 means the screen's own tick,
        // which is what a troop strip on that screen always played at.
        int  alcove_figure_frame_ms;
        char hud_puzzle_grid[RES_PATH_LEN];
        char hud_gold_purse[RES_PATH_LEN];
        char hud_bar_strip[RES_PATH_LEN];   // 320x5 middle bar (GR_SELECT, 1)
        char chrome_overworld[RES_PATH_LEN]; // 320x200 frame bitmap,
                                             // transparent interior; pixel-
                                             // exact copy of reference chrome.
        char splash_logo[RES_PATH_LEN];      // publisher logo (first splash)
        char splash_title[RES_PATH_LEN];     // game title (second splash)
        char alcove_portrait[RES_PATH_LEN];  // modern temple: the keeper's portrait (96)
        // Modern title sequence layers, all 256x164 but the eagle (96x164):
        // the battle fades in over the purple, the eagle standard slides left.
        // All three or none; otherwise the title is splash_title, still.
        char title_battle[RES_PATH_LEN];
        char title_eagle[RES_PATH_LEN];
        char title_words[RES_PATH_LEN];
        char class_picker[RES_PATH_LEN];     // 288x184 A-D class portrait image
        char class_highlight[RES_PATH_LEN];  // 42x44 cursor glow for class picker
        // Modern class select: the picker with one figure picked out, per class
        // in catalog order (tools/classpicker.py).
        int  class_picker_selected_count;
        char (*class_picker_selected)[RES_PATH_LEN];   // heap, one per class
        // Palette colour name (e.g. "YELLOW") for the frame the shell draws
        // round every panel slot: HUD panels, inventory cells, contract face.
        // Empty: the shell draws no frame and the art carries its own.
        char panel_frame[16];
        // Optional decorative wall the shell draws across the band above the
        // siege board (a cell-sized tile repeated six times). Empty: nothing.
        char siege_back_wall[RES_PATH_LEN];
        char siege_back_wall_left[RES_PATH_LEN];   // optional end cells of the band
        char siege_back_wall_right[RES_PATH_LEN];
        // Optional full grid of siege tiles, one file per cell: the prefix of
        // "<prefix>_<x>_<y>.png" for x in 0..COMBAT_W-1 and y in 0..COMBAT_H,
        // row 0 being the band above the board. When set, the shell draws each
        // cell's own tile as the siege ground and nothing for the wall codes,
        // and the siege_back_wall keys are ignored. Empty: the per-code walls.
        char siege_grid[RES_PATH_LEN];
        // Combat ground: "field" (default) draws sprites.combat[0] under every
        // cell; "terrain" draws the map tile the hero stands on instead and
        // combat[0] is not part of the pack.
        char combat_ground[16];
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
// The tile code a tile_codes key names: the key's own byte for a
// single-character key, or the byte a two-digit "\xNN" hex escape spells.
// -1 if the key names no code. Pure; unit tested.
int resources_tile_code_from_key(const char *key);

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
// The list grows to hold every path; free it with resources_art_list_free.
typedef struct {
    char (*path)[RES_PATH_LEN];
    int    n, cap;
} ResArtList;
int  resources_art_manifest(const Resources *res, ResArtList *out);
// Whether terrain art `stem` for tile set `set` comes from art/tiles/<set>/
// (the set is whole, or lists `stem` in "tile_set_arts") rather than from the
// master art/tiles/ set.
bool resources_tile_from_set(const Resources *res, const char *set, const char *stem);

// Where a hero sailing in from zone `from` lands on zone `z`: the zone's
// "arrivals" entry for that origin, else its hero_spawn.
void resources_zone_arrival(const ResZone *z, const char *from, int *x, int *y);
void resources_art_list_free(ResArtList *list);
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

// The siege grid tile for board column x and grid row y (row 0 = the band
// above the board, rows 1..COMBAT_H = board rows 0..COMBAT_H-1), built from
// sprites.siege_grid. False, and out empty, when the pack declares no grid.
bool resources_siege_grid_path(const Resources *res, int x, int y,
                               char *out, int cap);

// True when the pack draws the hero's map terrain as the combat ground
// (sprites.ui.combat_ground == "terrain") rather than sprites.combat[0].
bool resources_combat_ground_is_terrain(const Resources *res);

// The home/audience castle -- the King's castle (contract audiences, the recruit
// home pool); never a gate destination. The "which castle is home" rule lives
// HERE (the special.flow marker is pack data); no other layer tests the flow id.
bool resources_castle_is_home(const ResCastle *rc);            // rc may be NULL
const ResCastle *resources_home_castle(const Resources *r);    // NULL if none

const ResZone   *resources_zone_by_id(const Resources *r, const char *id);
// The per-class hero art for a class id, or NULL when the class declared none.
const ResClassHero *resources_class_hero(const Resources *r, const char *class_id);
// Index of zone `id` in r->zones[] (parallel to Game.world.zones_discovered[]),
// or -1 if unknown. The ONE zone-id -> index lookup.
int              resources_zone_index(const Resources *r, const char *id);

const ResVillainDesc *resources_villain_desc(const Resources *r,
                                             const char *villain_id);
// A strings.town_invitations block by id, or NULL.
const ResTownInvite *resources_town_invite(const Resources *r, const char *id);
// Where the town's boat waits (strings.town_docks), or NULL.
const char *resources_town_dock(const Resources *r, const char *town_id);
// Index of portraits[] entry `id`, or -1.
int resources_portrait_index(const Resources *r, const char *id);
// The spell's long description from strings.spell_lore, or NULL.
const char *resources_spell_lore(const Resources *r, const char *spell_id);
// strings.spell_brief for a spell id, or NULL.
const char *resources_spell_brief(const Resources *r, const char *spell_id);

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
