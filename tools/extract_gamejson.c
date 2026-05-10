// extract_gamejson.c -- synthesize game.json from KB.EXE bytes.
//
// Gameplay constants are read from KB.EXE byte tables at fixed offsets;
// English strings are read directly from KB.unpacked.EXE at well-known
// offsets. The output blends the binary-scraped data with hand-curated
// constants (UI labels, asset paths, dialog templates) embedded in
// extract_gamejson_const.inc.
//
// Coverage:
//   - catalogs:     troops, spells, classes, villains, artifacts
//   - tunings:      numeric constants, chest tables, scoring
//   - world layout: zones, towns, castles
//   - signs:        positions from LAND.ORG, text from KB.EXE
//   - strings:      KB.EXE string tables (banners, prompts, log lines)

#define _POSIX_C_SOURCE 200809L
#include "extract.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward decl — implementation is near the bottom (after the embedded
// blobs are pulled in via #include).
static cJSON *embedded_signs_for_zone(const char *zone_id);

// Town-gate Y coordinates loaded from KB.EXE bytes at extract time.
static int g_have_kb_towngate_y;
static uint8_t g_kb_towngate_y[26];

// -- slugify helper: "Murray the Miser" -> "murray_the_miser" -----------
static void slugify(char *out, size_t cap, const char *display) {
    size_t o = 0;
    for (const char *p = display; *p && o + 1 < cap; p++) {
        char c = *p;
        if (c == '\'' || c == '.' || c == ',') continue;     // strip punctuation
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';         // lowercase
        if (c == ' ' || c == '-') c = '_';
        out[o++] = c;
    }
    out[o] = '\0';
}

// -- ABIL flag → string, comma-separated --------------------------------
// From upstream
#define EX_ABIL_FLY    0x01
#define EX_ABIL_REGEN  0x02
#define EX_ABIL_MAGIC  0x04
#define EX_ABIL_IMMUNE 0x08
#define EX_ABIL_ABSORB 0x10
#define EX_ABIL_LEECH  0x20
#define EX_ABIL_SCYTHE 0x40
#define EX_ABIL_UNDEAD 0x80

static void abilities_to_str(unsigned bits, char *out, size_t cap) {
    out[0] = '\0';
    size_t o = 0;
    static const struct { unsigned bit; const char *name; } MAP[] = {
        { EX_ABIL_FLY,    "FLY"    },
        { EX_ABIL_REGEN,  "REGEN"  },
        { EX_ABIL_MAGIC,  "MAGIC"  },
        { EX_ABIL_IMMUNE, "IMMUNE" },
        { EX_ABIL_ABSORB, "ABSORB" },
        { EX_ABIL_LEECH,  "LEECH"  },
        { EX_ABIL_SCYTHE, "SCYTHE" },
        { EX_ABIL_UNDEAD, "UNDEAD" },
    };
    for (size_t i = 0; i < sizeof(MAP)/sizeof(MAP[0]); i++) {
        if (!(bits & MAP[i].bit)) continue;
        if (o > 0) o += (size_t)snprintf(out + o, cap - o, "|");
        o += (size_t)snprintf(out + o, cap - o, "%s", MAP[i].name);
        if (o + 1 >= cap) break;
    }
}

// ============================================================================
// SUBPHASE 2a — Catalogs
// ============================================================================
//
// Every constant below is a transcription of the KB.EXE byte tables.
// ============================================================================

// KBtroop troops[MAX_TROOPS]
typedef struct {
    const char *name;
    int  skill_level, hit_points, move_rate;
    int  melee_min, melee_max;
    int  ranged_min, ranged_max, ranged_ammo;
    int  recruit_cost, spoils_factor;
    unsigned abilities;
    const char *dwelling;       // "plains"/"forest"/"hill"/"dungeon"/"castle"
    int  max_population, growth_per_week;
    char morale_group;          // 'A'..'E'
} ExTroopRow;

#define EX_TROOP_COUNT 25
static const ExTroopRow EX_TROOPS[EX_TROOP_COUNT] = {
    // 
    { "Peasants",   1,1,1,   1,1,   0,0,0,    10,1,    0,                         "plains", 250,6, 'A' },
    { "Sprites",    1,1,1,   1,2,   0,0,0,    15,1,    EX_ABIL_FLY,               "forest", 200,6, 'C' },
    { "Militia",    2,2,2,   1,2,   0,0,0,    50,5,    0,                         "castle",   0,5, 'A' },
    { "Wolves",     2,3,3,   1,3,   0,0,0,    40,4,    0,                         "plains", 150,5, 'D' },
    { "Skeletons",  2,3,2,   1,2,   0,0,0,    40,4,    EX_ABIL_UNDEAD,            "dungeon",150,5, 'E' },
    { "Zombies",    2,5,1,   2,2,   0,0,0,    50,5,    EX_ABIL_UNDEAD,            "dungeon",100,5, 'E' },
    { "Gnomes",     2,5,1,   1,3,   0,0,0,    60,6,    0,                         "forest", 250,5, 'C' },
    { "Orcs",       2,5,2,   2,3,   1,2,10,   75,7,    0,                         "hill",   200,5, 'D' },
    { "Archers",    2,10,2,  1,2,   1,3,12,   250,25,  0,                         "castle",   0,5, 'B' },
    { "Elves",      3,10,3,  1,2,   2,4,24,   200,20,  0,                         "forest", 100,4, 'C' },
    { "Pikemen",    3,10,2,  2,4,   0,0,0,    300,30,  0,                         "castle",   0,4, 'B' },
    { "Nomads",     3,15,2,  2,4,   0,0,0,    300,30,  0,                         "plains", 150,4, 'C' },
    { "Dwarves",    3,20,1,  2,4,   0,0,0,    350,30,  0,                         "hill",   100,4, 'C' },
    { "Ghosts",     4,10,3,  3,4,   0,0,0,    400,40,  EX_ABIL_ABSORB|EX_ABIL_UNDEAD,"dungeon", 25,3, 'E' },
    { "Knights",    5,35,1,  6,10,  0,0,0,    1000,100,0,                         "castle", 250,3, 'B' },
    { "Ogres",      4,40,1,  3,5,   0,0,0,    750,75,  0,                         "hill",   200,3, 'D' },
    { "Barbarians", 4,40,3,  1,6,   0,0,0,    750,75,  0,                         "plains", 100,3, 'C' },
    { "Trolls",     4,50,1,  2,5,   0,0,0,    1000,100,EX_ABIL_REGEN,             "forest",  25,3, 'D' },
    { "Cavalry",    4,20,4,  3,5,   0,0,0,    800,80,  0,                         "castle",   0,2, 'B' },
    { "Druids",     5,25,2,  2,3,   10,0,3,   700,70,  EX_ABIL_MAGIC,             "forest",  25,2, 'C' },
    { "Archmages",  5,25,1,  2,3,   25,0,2,   1200,120,EX_ABIL_FLY|EX_ABIL_MAGIC, "plains",  25,2, 'C' },
    { "Vampires",   5,30,1,  3,6,   0,0,0,    1500,150,EX_ABIL_LEECH|EX_ABIL_FLY|EX_ABIL_UNDEAD,"dungeon",50,2,'E' },
    { "Giants",     5,60,3,  10,20, 5,10,6,   2000,200,0,                         "hill",    50,2, 'C' },
    { "Demons",     6,50,1,  5,7,   0,0,0,    3000,300,EX_ABIL_FLY|EX_ABIL_SCYTHE,"dungeon", 25,1, 'E' },
    { "Dragons",    6,200,1, 25,50, 0,0,0,    5000,500,EX_ABIL_FLY|EX_ABIL_IMMUNE,"hill",    25,1, 'D' },
};

// dwelling_to_troop[][] — slot pool per dwelling tier.
// We only need the per-troop tier_counts, which the binary tables stores as
// troop_numbers[MAX_TROOPS][MAX_TROOP_DIFFICULTY] (bounty.c:727 area).
// The values are documented in the comments next to the spawn machinery.
// troop_numbers[24][4]
// (each row = counts for tier 0..3 — plains/forest/hill/dungeon)
static const int EX_TIER_COUNTS[EX_TROOP_COUNT][4] = {
    {10,20,50,100},  // peasants
    {10,15,30, 50},  // sprites
    { 0, 5,15, 30},  // militia
    { 5,10,20, 40},  // wolves
    { 5,10,15, 25},  // skeletons
    { 5,10,15, 20},  // zombies
    { 5,10,15, 20},  // gnomes
    { 5,10,12, 20},  // orcs
    { 0, 5,10, 15},  // archers
    { 5, 8,10, 15},  // elves
    { 0, 5, 8, 15},  // pikemen
    { 5, 8,12, 18},  // nomads
    { 5, 6, 8, 12},  // dwarves
    { 2, 4, 6,  8},  // ghosts
    { 0, 2, 5,  8},  // knights
    { 2, 3, 5,  8},  // ogres
    { 2, 3, 5,  8},  // barbarians
    { 2, 3, 4,  6},  // trolls
    { 0, 2, 4,  6},  // cavalry
    { 2, 3, 4,  6},  // druids
    { 2, 3, 4,  5},  // archmages
    { 1, 2, 3,  5},  // vampires
    { 1, 2, 3,  5},  // giants
    { 1, 2, 3,  4},  // demons
    { 1, 1, 1,  2},  // dragons
};

// spell names + costs
static const struct { const char *name; int cost; const char *kind; } EX_SPELLS[14] = {
    { "Clone",          2000, "combat"    },
    { "Teleport",        500, "combat"    },
    { "Fireball",       1500, "combat"    },
    { "Lightning",       500, "combat"    },
    { "Freeze",          300, "combat"    },
    { "Resurrect",      5000, "combat"    },
    { "Turn Undead",    2000, "combat"    },
    { "Bridge",          100, "adventure" },
    { "Time Stop",       200, "adventure" },
    { "Find Villain",   1000, "adventure" },
    { "Castle Gate",    1000, "adventure" },
    { "Town Gate",       500, "adventure" },
    { "Instant Army",   1000, "adventure" },
    { "Raise Control",   500, "adventure" },
};

// starting_gold[MAX_CLASSES]
static const int EX_STARTING_GOLD[4] = { 7500, 10000, 10000, 7500 };

// starting_army_troop[MAX_CLASSES][2]
//                                starting_army_numbers[MAX_CLASSES][2]
// 0xFF in troop[] means "no second slot".
static const int EX_STARTING_ARMY_TROOP[4][2] = {
    { 0x02, 0x08 },  // Knight: Militia, Archers
    { 0x00, 0x02 },  // Paladin: Peasants, Militia
    { 0x00, 0x01 },  // Sorceress: Peasants, Sprites
    { 0x03, 0xFF },  // Barbarian: Wolves, (none)
};
static const int EX_STARTING_ARMY_COUNT[4][2] = {
    { 20,  2 },   // Knight
    { 20, 20 },   // Paladin
    { 30, 10 },   // Sorceress
    { 20,  0 },   // Barbarian
};

// KBclass classes[4][4]
// Layout: { name, villains_needed, leadership(delta), max_spells(delta),
//           spell_power(delta), commission(delta), knows_magic, instant_army }
typedef struct {
    const char *name;
    int villains_needed;
    int leadership_delta;
    int max_spells_delta;
    int spell_power_delta;
    int commission_delta;
    int knows_magic;
    unsigned instant_army;
} ExClassRank;
static const ExClassRank EX_CLASSES[4][4] = {
    { // Knight (bounty.c:168)
        { "Knight",   0,  100, 2, 1, 1000, 0, 0x00 },
        { "General",  2,  100, 3, 1, 1000, 0, 0x02 },
        { "Marshal",  8,  300, 4, 1, 2000, 0, 0x08 },
        { "Lord",    14,  500, 5, 2, 4000, 0, 0x0E },
    },
    { // Paladin (bounty.c:174)
        { "Paladin",  0,   80, 3, 1, 1000, 0, 0x00 },
        { "Crusader", 2,   80, 3, 1, 1000, 0, 0x02 },
        { "Avenger",  7,  240, 6, 2, 2000, 0, 0x08 },
        { "Champion",13,  400, 5, 2, 4000, 0, 0x12 },
    },
    { // Sorceress (bounty.c:180)
        { "Sorceress",0,   60, 5, 2, 3000, 1, 0x01 },
        { "Magician", 3,   60, 8, 3, 1000, 0, 0x06 },
        { "Mage",     6,  180,10, 5, 1000, 0, 0x09 },
        { "Archmage",12,  300,12, 5, 1000, 0, 0x13 },
    },
    { // Barbarian (bounty.c:186)
        { "Barbarian",0,  100, 2, 0, 2000, 0, 0x00 },
        { "Chieftain",1,  100, 2, 1, 2000, 0, 0x03 },
        { "Warlord",  5,  300, 3, 1, 2000, 0, 0x07 },
        { "Overlord",10,  500, 3, 1, 2000, 0, 0x0F },
    },
};

// villain_rewards[17]
static const int EX_VILLAIN_REWARDS[17] = {
    5000, 6000, 7000, 8000, 9000, 10000,
    12000, 14000, 16000, 18000, 20000,
    25000, 30000, 35000, 40000, 45000, 50000,
};

// villain_army_troops[17][5] (troop indices)
static const int EX_VILLAIN_TROOPS[17][5] = {
    { 0x00, 0x03, 0x02, 0x00, 0x00 },
    { 0x0b, 0x02, 0x02, 0x00, 0x00 },
    { 0x01, 0x01, 0x04, 0x05, 0x0f },
    { 0x03, 0x07, 0x08, 0x11, 0x0c },
    { 0x02, 0x02, 0x08, 0x09, 0x10 },
    { 0x01, 0x0d, 0x0e, 0x14, 0x14 },
    { 0x02, 0x08, 0x0a, 0x12, 0x0e },
    { 0x09, 0x14, 0x13, 0x0a, 0x01 },
    { 0x07, 0x0f, 0x11, 0x16, 0x03 },
    { 0x04, 0x05, 0x0d, 0x15, 0x17 },
    { 0x04, 0x05, 0x0d, 0x15, 0x17 },
    { 0x16, 0x18, 0x0f, 0x07, 0x06 },
    { 0x06, 0x10, 0x16, 0x0b, 0x00 },
    { 0x08, 0x0a, 0x12, 0x0e, 0x18 },
    { 0x17, 0x15, 0x14, 0x06, 0x00 },
    { 0x17, 0x18, 0x12, 0x0e, 0x14 },
    { 0x18, 0x18, 0x18, 0x17, 0x15 },
};
// villain_army_numbers[17][5]
static const int EX_VILLAIN_NUMBERS[17][5] = {
    {  50,  20,  25,  30,  25 },
    {  10,  30,  20,  60,  40 },
    {  70,  50,  20,  20,   4 },
    {  30,  20,  10,   2,   6 },
    {  50,  50,  10,  10,   5 },
    { 250,  10,  10,   4,   4 },
    { 100,  20,  20,  15,  15 },
    {  30,  30,  10,  30, 300 },
    { 150,  20,  10,   5,  80 },
    { 500, 100,  30,  10,   6 },
    { 600, 200,  50,  25,  10 },
    {  30,   5,  30, 200, 200 },
    { 300,  40,  20, 100, 700 },
    {  35, 100,  80,  60,   5 },
    {  30,  50, 100, 500,5000 },
    {  50,  10, 200, 250,  60 },
    { 100,  25,  25, 100, 100 },
};

// villains_per_continent[4]
static const int EX_VILLAINS_PER_CONTINENT[4] = { 6, 4, 4, 3 };

// Villain display names + stable port-side ids. Display names match
// what KB.EXE @ DOS_VNAMES (0x18EDF) contains; the id slugs are
// port-stable (single-word lowercase) so tests and save-fixtures can
// reference villains by id without churn whenever a display-name
// spelling shifts.
static const struct { const char *id; const char *name; } EX_VILLAINS[17] = {
    { "murray",         "Murray the Miser"     },
    { "hack",           "Hack the Rogue"       },
    { "aimola",         "Princess Aimola"      },
    { "baron_makahl",   "Baron Johnno Makahl"  },
    { "dread_rob",      "Dread Pirate Rob"     },
    { "caneghor",       "Canegor the Mystic"   },
    { "moradon",        "Sir Moradon the Cruel" },
    { "barrowpine",     "Prince Barrowpine"    },
    { "bargash",        "Bargash Eyesore"      },
    { "rinaldus",       "Rinaldus Drybone"     },
    { "ragface",        "Ragface"              },
    { "mahk",           "Mahk Bellowspeak"     },
    { "auric",          "Auric Whiteskin"      },
    { "czar_nickolai",  "Czar Nickolai the Mad" },
    { "magus",          "Magus Deathspell"     },
    { "urthrax",        "Urthrax Killspite"    },
    { "arech",          "Arech Dragonbreath"   },
};

// artifact_names[8]
static const char *EX_ARTIFACT_NAMES[8] = {
    "The Sword of Prowess",
    "The Shield of Protection",
    "The Crown of Command",
    "The Articles of Nobility",
    "The Amulet of Augmentation",
    "The Ring of Heroism",
    "The Book of Necros",
    "The Anchor of Admirability",
};


// ============================================================================
// JSON emission helpers
// ============================================================================

static cJSON *emit_troops(void) {
    cJSON *arr = cJSON_CreateArray();
    char buf[128];
    for (int i = 0; i < EX_TROOP_COUNT; i++) {
        const ExTroopRow *t = &EX_TROOPS[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", i);
        slugify(buf, sizeof buf, t->name);
        cJSON_AddStringToObject(o, "id",   buf);
        cJSON_AddStringToObject(o, "name", t->name);

        snprintf(buf, sizeof buf, "art/troops/%s_00.png", o->child->next->next->valuestring);
        // ^ the slugified id is at o.id; rebuild to avoid recomputing
        char idbuf[96]; slugify(idbuf, sizeof idbuf, t->name);
        snprintf(buf, sizeof buf, "art/troops/%s_00.png", idbuf);
        cJSON_AddStringToObject(o, "sprite", buf);

        cJSON_AddNumberToObject(o, "skill_level", t->skill_level);
        cJSON_AddNumberToObject(o, "hit_points",  t->hit_points);
        cJSON_AddNumberToObject(o, "move_rate",   t->move_rate);

        cJSON *melee = cJSON_CreateArray();
        cJSON_AddItemToArray(melee, cJSON_CreateNumber(t->melee_min));
        cJSON_AddItemToArray(melee, cJSON_CreateNumber(t->melee_max));
        cJSON_AddItemToObject(o, "melee", melee);

        cJSON *ranged = cJSON_CreateArray();
        cJSON_AddItemToArray(ranged, cJSON_CreateNumber(t->ranged_min));
        cJSON_AddItemToArray(ranged, cJSON_CreateNumber(t->ranged_max));
        cJSON_AddItemToArray(ranged, cJSON_CreateNumber(t->ranged_ammo));
        cJSON_AddItemToObject(o, "ranged", ranged);

        cJSON_AddNumberToObject(o, "recruit_cost",  t->recruit_cost);
        cJSON_AddNumberToObject(o, "spoils_factor", t->spoils_factor);

        char ab[96]; abilities_to_str(t->abilities, ab, sizeof ab);
        cJSON_AddStringToObject(o, "abilities", ab);

        cJSON_AddStringToObject(o, "dwelling",         t->dwelling);
        cJSON_AddNumberToObject(o, "max_population",   t->max_population);
        cJSON_AddNumberToObject(o, "growth_per_week",  t->growth_per_week);
        char mg[2] = { t->morale_group, 0 };
        cJSON_AddStringToObject(o, "morale_group", mg);

        cJSON *anim = cJSON_CreateArray();
        for (int f = 0; f < 4; f++) {
            char p[128];
            snprintf(p, sizeof p, "art/troops/%s_%02d.png", idbuf, f);
            cJSON_AddItemToArray(anim, cJSON_CreateString(p));
        }
        cJSON_AddItemToObject(o, "anim", anim);

        cJSON *tier = cJSON_CreateArray();
        for (int k = 0; k < 4; k++)
            cJSON_AddItemToArray(tier, cJSON_CreateNumber(EX_TIER_COUNTS[i][k]));
        cJSON_AddItemToObject(o, "tier_counts", tier);

        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static cJSON *emit_spells(void) {
    cJSON *arr = cJSON_CreateArray();
    char idbuf[96];
    for (int i = 0; i < 14; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", i);
        slugify(idbuf, sizeof idbuf, EX_SPELLS[i].name);
        cJSON_AddStringToObject(o, "id",   idbuf);
        cJSON_AddStringToObject(o, "name", EX_SPELLS[i].name);
        cJSON_AddStringToObject(o, "kind", EX_SPELLS[i].kind);
        cJSON_AddNumberToObject(o, "cost", EX_SPELLS[i].cost);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static cJSON *emit_classes(void) {
    cJSON *arr = cJSON_CreateArray();
    static const char *CLASS_DISPLAY[4] = { "Knight", "Paladin", "Sorceress", "Barbarian" };
    char idbuf[96];
    for (int c = 0; c < 4; c++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", c);
        slugify(idbuf, sizeof idbuf, CLASS_DISPLAY[c]);
        cJSON_AddStringToObject(o, "id",   idbuf);
        cJSON_AddStringToObject(o, "name", CLASS_DISPLAY[c]);

        char portrait[128];
        snprintf(portrait, sizeof portrait, "art/classes/%s.png", idbuf);
        cJSON_AddStringToObject(o, "portrait", portrait);

        // Starting state (,201,208)
        cJSON_AddNumberToObject(o, "starting_gold", EX_STARTING_GOLD[c]);
        cJSON *st = cJSON_CreateArray();
        for (int s = 0; s < 2; s++) {
            int troop_idx = EX_STARTING_ARMY_TROOP[c][s];
            int count     = EX_STARTING_ARMY_COUNT[c][s];
            if (troop_idx == 0xFF || count == 0) continue;
            cJSON *slot = cJSON_CreateObject();
            char tslug[96];
            slugify(tslug, sizeof tslug, EX_TROOPS[troop_idx].name);
            cJSON_AddStringToObject(slot, "id",    tslug);
            cJSON_AddNumberToObject(slot, "count", count);
            cJSON_AddItemToArray(st, slot);
        }
        cJSON_AddItemToObject(o, "starting_troops", st);

        cJSON *ranks = cJSON_CreateArray();
        for (int r = 0; r < 4; r++) {
            const ExClassRank *cr = &EX_CLASSES[c][r];
            cJSON *ro = cJSON_CreateObject();
            char rank_id[96];
            slugify(rank_id, sizeof rank_id, cr->name);
            cJSON_AddStringToObject(ro, "id",               rank_id);
            cJSON_AddStringToObject(ro, "name",             cr->name);
            cJSON_AddNumberToObject(ro, "villains_needed", cr->villains_needed);
            cJSON_AddNumberToObject(ro, "leadership",      cr->leadership_delta);
            cJSON_AddNumberToObject(ro, "max_spells",      cr->max_spells_delta);
            cJSON_AddNumberToObject(ro, "spell_power",     cr->spell_power_delta);
            cJSON_AddNumberToObject(ro, "commission",      cr->commission_delta);
            cJSON_AddBoolToObject  (ro, "knows_magic",     cr->knows_magic);
            cJSON_AddNumberToObject(ro, "instant_army",    cr->instant_army);
            cJSON_AddItemToArray(ranks, ro);
        }
        cJSON_AddItemToObject(o, "ranks", ranks);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static cJSON *emit_villains(void) {
    cJSON *arr = cJSON_CreateArray();
    static const char *ZONE_NAMES[4] = { "continentia", "forestria", "archipelia", "saharia" };
    int v = 0;
    for (int continent = 0; continent < 4; continent++) {
        for (int local = 0; local < EX_VILLAINS_PER_CONTINENT[continent]; local++) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "index", v);
            cJSON_AddStringToObject(o, "id",     EX_VILLAINS[v].id);
            cJSON_AddStringToObject(o, "name",   EX_VILLAINS[v].name);
            cJSON_AddStringToObject(o, "zone",   ZONE_NAMES[continent]);
            cJSON_AddNumberToObject(o, "reward", EX_VILLAIN_REWARDS[v]);
            // puzzle_cell: villain index = first 17 cells of the 5x6
            // puzzle grid (artifacts fill cells 17..23).
            cJSON_AddNumberToObject(o, "puzzle_cell", v);

            char portrait[128];
            snprintf(portrait, sizeof portrait, "art/villains/%s_00.png",
                     EX_VILLAINS[v].id);
            cJSON_AddStringToObject(o, "portrait", portrait);

            cJSON *army = cJSON_CreateArray();
            for (int s = 0; s < 5; s++) {
                int troop_idx = EX_VILLAIN_TROOPS[v][s];
                int count     = EX_VILLAIN_NUMBERS[v][s];
                if (count == 0) continue;
                cJSON *slot = cJSON_CreateObject();
                char tslug[96];
                slugify(tslug, sizeof tslug, EX_TROOPS[troop_idx].name);
                cJSON_AddStringToObject(slot, "troop", tslug);
                cJSON_AddNumberToObject(slot, "count", count);
                cJSON_AddItemToArray(army, slot);
            }
            cJSON_AddItemToObject(o, "army", army);
            cJSON_AddItemToArray(arr, o);
            v++;
        }
    }
    return arr;
}

// Artifact short-ids matching the existing port-side asset filenames
// (inventory_artifact_<id>.png). KB.EXE has the long names ("The Sword
// of Prowess"); the short id is port-stable.
static const char *EX_ARTIFACT_IDS[8] = {
    "sword", "shield", "crown", "articles",
    "amulet", "ring", "book",  "anchor",
};

// POWER_* enum + comments next to artifact_powers[]
// (bounty.c:445). Each artifact maps to a port-authored 'effect' string
// (one-line UI description) and a 'power' identifier the engine uses
// to apply mechanical bonuses.
static const struct { const char *power; const char *effect; } EX_ARTIFACT_META[8] = {
    { "increased_damage",     "Your army deals +50% damage."        },
    { "quarter_protection",   "Your army takes 25% less damage."    },
    { "double_leadership",    "Doubles your leadership."            },
    { "increase_commission",  "Increases your weekly commission."   },
    { "double_spell_power",   "Doubles your spell power."           },
    { "double_max_spells",    "Doubles your maximum spells."        },
    { "unknown_xxx1",         "Unknown power."                      },
    { "cheaper_boat_rental",  "Boat rental is cheaper."             },
};

// Each artifact's home zone — derived from artifact_inversion[8]:
// the puzzle_slot value, divided by 2, gives the continent.
// (bounty.c:457: artifact 0 → slot 5 → continent 2 = archipelia, etc.)
// The zone strings are what the engine displays + uses for placement.
static const char *EX_ZONE_NAMES[4] = {
    "continentia", "forestria", "archipelia", "saharia",
};

// puzzle_cell (where this artifact sits in the
// 5x6 puzzle grid). Derived from puzzle_map[][] but the explicit
// row-major number is port-authored.
static const int EX_ARTIFACT_PUZZLE_CELL[8] = {
    17, 18, 19, 20, 21, 22, 23, 24,
};

// Per-artifact zone + local_idx, derived by inverting the original
// game's artifact-slot → artifact-index map (each continent has 2
// artifact slots; slot 0 = first, slot 1 = second).
//   artifact 0 (Sword)    → continent 3, local 1
//   artifact 1 (Shield)   → continent 1, local 0
//   artifact 2 (Crown)    → continent 2, local 0
//   artifact 3 (Articles) → continent 0, local 1
//   artifact 4 (Amulet)   → continent 3, local 0
//   artifact 5 (Ring)     → continent 0, local 0
//   artifact 6 (Book)     → continent 2, local 1
//   artifact 7 (Anchor)   → continent 1, local 1
static const int EX_ARTIFACT_ZONE_IDX[8]  = { 3, 1, 2, 0, 3, 0, 2, 1 };
static const int EX_ARTIFACT_LOCAL_IDX[8] = { 1, 0, 0, 1, 0, 0, 1, 1 };

static cJSON *emit_artifacts(void) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < 8; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", i);
        cJSON_AddStringToObject(o, "id",     EX_ARTIFACT_IDS[i]);
        cJSON_AddStringToObject(o, "name",   EX_ARTIFACT_NAMES[i]);
        cJSON_AddStringToObject(o, "zone",   EX_ZONE_NAMES[EX_ARTIFACT_ZONE_IDX[i]]);
        cJSON_AddStringToObject(o, "effect", EX_ARTIFACT_META[i].effect);
        cJSON_AddStringToObject(o, "power",  EX_ARTIFACT_META[i].power);

        char icon[128];
        snprintf(icon, sizeof icon, "art/ui/inventory_artifact_%s.png",
                 EX_ARTIFACT_IDS[i]);
        cJSON_AddStringToObject(o, "icon", icon);

        cJSON_AddNumberToObject(o, "puzzle_cell", EX_ARTIFACT_PUZZLE_CELL[i]);
        cJSON_AddNumberToObject(o, "local_idx",   EX_ARTIFACT_LOCAL_IDX[i]);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

// ============================================================================
// SUBPHASE 2b — Tunings (numeric constants, chest tables, scoring)
// ============================================================================

// From economy constants
#define EX_COST_ALCOVE        5000
#define EX_COST_BOAT_CHEAP     100
#define EX_COST_BOAT_EXPENSIVE 500
#define EX_COST_SIEGE         3000

// From upstream
#define EX_DAY_STEPS  40
#define EX_WEEK_DAYS   5

// From upstream
#define EX_MAX_PLAYER_ARMY 5

// days_per_difficulty[4]
static const int EX_DAYS_PER_DIFFICULTY[4] = { 900, 600, 400, 200 };

// From score formula
//   per_villain   = 500
//   per_artifact  = 250
//   per_castle    = 100
//   kill_penalty  =   1
// then if difficulty == 0: score /= 2 (easy_halves)
// else:                   score *= difficulty_modifier[difficulty]
static const int EX_SCORE_PER_VILLAIN  = 500;
static const int EX_SCORE_PER_ARTIFACT = 250;
static const int EX_SCORE_PER_CASTLE   = 100;
static const int EX_SCORE_KILL_PENALTY =   1;
static const int EX_DIFFICULTY_MODIFIER[5] = { 0, 1, 2, 4, 8 };

// chest probability tables (per continent)
// Each is a 1..100 threshold; the engine walks them top-to-bottom and the
// first branch whose threshold strictly exceeds the roll fires.
static const int EX_CHANCE_GOLD       [4] = { 0x3d, 0x42, 0x4c, 0x47 };
static const int EX_CHANCE_COMMISSION [4] = { 0x51, 0x56, 0x56, 0x51 };
static const int EX_CHANCE_SPELL_POWER[4] = { 0x56, 0x5c, 0x5d, 0x5b };
static const int EX_CHANCE_MAX_SPELLS [4] = { 0x56, 0x5c, 0x5d, 0x5b };
static const int EX_CHANCE_NEW_SPELL  [4] = { 0x65, 0x65, 0x65, 0x65 };
// value ranges (per continent)
static const int EX_GOLD_MIN  [4] = { 0x00, 0x04, 0x09, 0x13 };
static const int EX_GOLD_MAX  [4] = { 0x05, 0x10, 0x15, 0x1f };
static const int EX_COMM_MIN  [4] = { 0x09, 0x31, 0x63, 0xc7 };
static const int EX_COMM_MAX  [4] = { 0x0029, 0x0033, 0x0065, 0x012d };
static const int EX_MAX_SPELLS_BASE[4] = { 0x01, 0x01, 0x02, 0x02 };

// morale_chart[5][5] (groups A..E)
//   N = MORALE_NORMAL, L = MORALE_LOW, H = MORALE_HIGH
static const char *EX_MORALE_CHART[5][5] = {
    { "N", "N", "N", "N", "N" },  // A
    { "N", "N", "N", "N", "N" },  // B
    { "N", "N", "H", "N", "N" },  // C
    { "L", "N", "L", "H", "N" },  // D
    { "L", "L", "L", "N", "N" },  // E
};

// number_names[6] + thresholds
static const struct { int min; const char *label; } EX_NUMBER_NAMES[6] = {
    { 500, "A multitude of" },
    { 100, "A horde of"     },
    {  50, "A lot of"       },
    {  20, "Many"           },
    {  10, "Some"           },
    {   1, "A few"          },
};

// instant_army_multiplier[MAX_RANKS]
static const int EX_INSTANT_ARMY_MULTIPLIER[4] = { 3, 2, 1, 1 };

// ---- emitters --------------------------------------------------------------

static cJSON *emit_economy(void) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "alcove_cost",       EX_COST_ALCOVE);
    cJSON_AddNumberToObject(o, "boat_cost_cheap",   EX_COST_BOAT_CHEAP);
    cJSON_AddNumberToObject(o, "boat_cost_normal",  EX_COST_BOAT_EXPENSIVE);
    cJSON_AddNumberToObject(o, "siege_cost",        EX_COST_SIEGE);

    cJSON *chest = cJSON_CreateObject();
    #define EMIT4(name, src) do { \
        cJSON *a = cJSON_CreateArray(); \
        for (int k = 0; k < 4; k++) cJSON_AddItemToArray(a, cJSON_CreateNumber((src)[k])); \
        cJSON_AddItemToObject(chest, name, a); \
    } while (0)
    EMIT4("chance_gold",        EX_CHANCE_GOLD);
    EMIT4("chance_commission",  EX_CHANCE_COMMISSION);
    EMIT4("chance_spell_power", EX_CHANCE_SPELL_POWER);
    EMIT4("chance_max_spells",  EX_CHANCE_MAX_SPELLS);
    EMIT4("chance_new_spell",   EX_CHANCE_NEW_SPELL);
    EMIT4("gold_min",           EX_GOLD_MIN);
    EMIT4("gold_max",           EX_GOLD_MAX);
    EMIT4("commission_min",     EX_COMM_MIN);
    EMIT4("commission_max",     EX_COMM_MAX);
    EMIT4("max_spells_base",    EX_MAX_SPELLS_BASE);
    #undef EMIT4
    cJSON_AddItemToObject(o, "chest", chest);

    // Scoring (the score table).
    cJSON *sc = cJSON_CreateObject();
    cJSON_AddNumberToObject(sc, "per_villain",  EX_SCORE_PER_VILLAIN);
    cJSON_AddNumberToObject(sc, "per_artifact", EX_SCORE_PER_ARTIFACT);
    cJSON_AddNumberToObject(sc, "per_castle",   EX_SCORE_PER_CASTLE);
    cJSON_AddNumberToObject(sc, "kill_penalty", EX_SCORE_KILL_PENALTY);
    cJSON *dm = cJSON_CreateArray();
    for (int k = 0; k < 5; k++)
        cJSON_AddItemToArray(dm, cJSON_CreateNumber(EX_DIFFICULTY_MODIFIER[k]));
    cJSON_AddItemToObject(sc, "difficulty_multiplier", dm);
    cJSON_AddBoolToObject(sc, "easy_halves", 1);
    cJSON_AddItemToObject(o, "scoring", sc);

    return o;
}

static cJSON *emit_time(void) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "day_steps", EX_DAY_STEPS);
    cJSON_AddNumberToObject(o, "week_days", EX_WEEK_DAYS);
    cJSON *dpd = cJSON_CreateObject();
    cJSON_AddNumberToObject(dpd, "easy",       EX_DAYS_PER_DIFFICULTY[0]);
    cJSON_AddNumberToObject(dpd, "normal",     EX_DAYS_PER_DIFFICULTY[1]);
    cJSON_AddNumberToObject(dpd, "hard",       EX_DAYS_PER_DIFFICULTY[2]);
    cJSON_AddNumberToObject(dpd, "impossible", EX_DAYS_PER_DIFFICULTY[3]);
    cJSON_AddItemToObject(o, "days_per_difficulty", dpd);
    return o;
}

static cJSON *emit_world(void) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "max_army_slots", EX_MAX_PLAYER_ARMY);
    // the engine clear_fog uses radius 3
    cJSON_AddNumberToObject(o, "fog_sight", 3);
    // the binary tables HOME_CONTINENT 0 → continent_names[0] is the
    // starting zone. We slugify the continent name to the zone id.
    cJSON_AddStringToObject(o, "starting_zone",    "continentia");
    // English vocabulary — KB uses "continent" / "continents" everywhere
    // (continent_names[]). Hard-coded per phase 1
    // decision: these stay in game.json as port-extractor constants.
    cJSON_AddStringToObject(o, "zone_noun",        "continent");
    cJSON_AddStringToObject(o, "zone_noun_plural", "continents");
    // Default name when the player enters none. Original game prompts
    // for a name; openbounty falls back to "Hero".
    cJSON_AddStringToObject(o, "default_name",     "Hero");
    // Default options[] vector: [delay, sounds, walk_beep, anim,
    // army_size, cga, music, volume]. Defaults match the engine.
    cJSON *opts = cJSON_CreateArray();
    static const int DEFAULTS[6] = { 4, 1, 1, 1, 1, 1 };
    for (int k = 0; k < 6; k++)
        cJSON_AddItemToArray(opts, cJSON_CreateNumber(DEFAULTS[k]));
    cJSON_AddItemToObject(o, "default_options", opts);
    return o;
}

static cJSON *emit_combat(void) {
    cJSON *o = cJSON_CreateObject();
    // morale chart (5x5)
    cJSON *mc = cJSON_CreateArray();
    for (int r = 0; r < 5; r++) {
        cJSON *row = cJSON_CreateArray();
        for (int c = 0; c < 5; c++)
            cJSON_AddItemToArray(row, cJSON_CreateString(EX_MORALE_CHART[r][c]));
        cJSON_AddItemToArray(mc, row);
    }
    cJSON_AddItemToObject(o, "morale_chart", mc);
    // number_names
    cJSON *nn = cJSON_CreateArray();
    for (int i = 0; i < 6; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "min",   EX_NUMBER_NAMES[i].min);
        cJSON_AddStringToObject(e, "label", EX_NUMBER_NAMES[i].label);
        cJSON_AddItemToArray(nn, e);
    }
    cJSON_AddItemToObject(o, "number_names", nn);
    return o;
}

static cJSON *emit_tuning(void) {
    cJSON *o = cJSON_CreateObject();
    cJSON *iam = cJSON_CreateArray();
    for (int k = 0; k < 4; k++)
        cJSON_AddItemToArray(iam, cJSON_CreateNumber(EX_INSTANT_ARMY_MULTIPLIER[k]));
    cJSON_AddItemToObject(o, "instant_army_multiplier", iam);
    // the engine ask_search uses 10 days
    cJSON_AddNumberToObject(o, "search_cost_days", 10);
    return o;
}

// ============================================================================
// SUBPHASE 2c — World layout (zones, towns, castles)
// SUBPHASE 2d — Position scrape from LAND.ORG
// ============================================================================

// From special coords (KB Y-up; we flip to Y-down).
#define EX_HOME_CONTINENT   0
#define EX_HOME_X          11
#define EX_HOME_Y_KB        7
#define EX_ALCOVE_CONTINENT 0
#define EX_ALCOVE_X        11
#define EX_ALCOVE_Y_KB     19
#define EX_FLIP_Y(y)       (63 - (y))

// continent_names[4]
static const char *EX_CONTINENT_NAMES[4] = {
    "Continentia", "Forestria", "Archipelia", "Saharia",
};

// town_names[26]
static const char *EX_TOWN_NAMES[26] = {
    "Riverton","Underfoot","Path's End","Anomaly","Topshore","Lakeview",
    "Simpleton","Centrapf","Quiln Point","Midland","Xoctan","Overthere",
    "Elan's Landing","King's Haven","Bayside","Nyre","Dark Corner",
    "Isla Vista","Grimwold","Japper","Vengeance","Hunterville","Fjord",
    "Yakonia","Woods End","Zaezoizu",
};

// castle_names[26+1] (last is King Maximus)
static const char *EX_CASTLE_NAMES[27] = {
    "Azram","Basefit","Cancomar","Duvock","Endryx","Faxis","Goobare",
    "Hyppus","Irok","Jhan","Kookamunga","Lorsche","Mooseweigh","Nilslag",
    "Ophiraund","Portalis","Quinderwitch","Rythacon","Spockana","Tylitch",
    "Uzare","Vutar","Wankelforte","Xelox","Yeneverre","Zyzzarzaz",
    "of King Maximus",
};

// castle_coords[26][3] (continent, X, Y_KB)
static const int EX_CASTLE_COORDS[26][3] = {
    {0,30,27},{1,47, 6},{0,36,49},{1,30,18},{2,11,46},{0,22,49},
    {2,41,36},{2,43,27},{0,11,30},{1,41,34},{0,57,58},{2,52,57},
    {1,25,39},{0,22,24},{0, 6,57},{0,58,23},{1,42,56},{0,54, 6},
    {3,17,39},{2, 9,18},{3,41,12},{0,40, 5},{0,40,41},{2,45, 6},
    {1,19,19},{3,46,43},
};
// castle_difficulty[26]
static const int EX_CASTLE_DIFFICULTY[26] = {
    0,1,0,1,2,0,2,2,0,1,0,2,1,0,0,0,1,0,3,2,3,0,0,2,1,3,
};

// town_coords[26][3] (continent, X, Y_KB)
// Indexed by display-order (matches town_names[]).
static const int EX_TOWN_COORDS[26][3] = {
    {0,29,12},{1,58, 4},{0,38,50},{1,34,23},{2, 5,50},{0,17,44},
    {2,13,60},{2, 9,39},{0,14,27},{1,58,33},{0,51,28},{2,57,57},
    {1, 3,37},{0,17,21},{0,41,58},{0,50,13},{1,58,60},{0,57, 5},
    {3, 9,60},{2,13, 7},{3, 7, 3},{0,12, 3},{0,46,35},{2,49, 8},
    {1, 3, 8},{3,58,48},
};
// towngate_coords[26][3] (where Town Gate spell drops)
static const int EX_TOWN_GATE_COORDS[26][3] = {
    {0,0x1d,0x0b},{1,0x39,0x04},{0,0x26,0x31},{1,0x23,0x17},
    {2,0x05,0x31},{0,0x10,0x2c},{2,0x0c,0x3c},{2,0x09,0x26},
    {0,0x0d,0x3c},{1,0x39,0x21},{0,0x33,0x1d},{2,0x39,0x38},
    {1,0x03,0x24},{0,0x10,0x15},{0,0x28,0x3a},{0,0x32,0x0e},
    {1,0x3a,0x3b},{0,0x38,0x05},{3,0x09,0x3b},{2,0x0d,0x08},
    {3,0x06,0x03},{0,0x0c,0x04},{0,0x2f,0x23},{2,0x32,0x08},
    {1,0x03,0x09},{3,0x3a,0x2f},
};
// boat_coords[26][3]
static const int EX_BOAT_COORDS[26][3] = {
    {0,0x1e,0x0b},{1,0x3b,0x05},{0,0x27,0x32},{1,0x22,0x16},
    {2,0x04,0x31},{0,0x12,0x2c},{2,0x0e,0x3c},{2,0x0a,0x26},
    {0,0x0e,0x1a},{1,0x3a,0x20},{0,0x33,0x1b},{2,0x3a,0x38},
    {1,0x02,0x24},{0,0x12,0x15},{0,0x29,0x39},{0,0x31,0x0c},
    {1,0x3b,0x3c},{0,0x38,0x04},{3,0x0a,0x3c},{2,0x0c,0x07},
    {3,0x08,0x03},{0,0x0b,0x03},{0,0x2f,0x24},{2,0x32,0x09},
    {1,0x02,0x09},{3,0x3b,0x30},
};

// LAND.ORG tile bytes (from the tile-byte map)
#define EX_TILE_CASTLE      0x85
#define EX_TILE_TOWN        0x8A
#define EX_TILE_CHEST       0x8B
#define EX_TILE_DWELLING_1  0x8C
#define EX_TILE_DWELLING_2  0x8D
#define EX_TILE_DWELLING_3  0x8E
#define EX_TILE_DWELLING_4  0x8F
#define EX_TILE_SIGNPOST    0x90
#define EX_TILE_FOE         0x91
#define EX_TILE_ARTIFACT_1  0x92
#define EX_TILE_ARTIFACT_2  0x93

// Top-level towns / castles emitter (the canonical catalogs).
static cJSON *emit_towns_top(void) {
    cJSON *arr = cJSON_CreateArray();
    static const char *ZONES[4] = { "continentia","forestria","archipelia","saharia" };
    char idbuf[96];
    for (int i = 0; i < 26; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", i);
        slugify(idbuf, sizeof idbuf, EX_TOWN_NAMES[i]);
        cJSON_AddStringToObject(o, "id",   idbuf);
        cJSON_AddStringToObject(o, "name", EX_TOWN_NAMES[i]);
        cJSON_AddNumberToObject(o, "x",    EX_TOWN_COORDS[i][1]);
        cJSON_AddNumberToObject(o, "y",    EX_FLIP_Y(EX_TOWN_COORDS[i][2]));

        cJSON *boat = cJSON_CreateObject();
        cJSON_AddNumberToObject(boat, "x", EX_BOAT_COORDS[i][1]);
        cJSON_AddNumberToObject(boat, "y", EX_FLIP_Y(EX_BOAT_COORDS[i][2]));
        cJSON_AddItemToObject(o, "boat", boat);

        cJSON *gate = cJSON_CreateObject();
        cJSON_AddNumberToObject(gate, "x", EX_TOWN_GATE_COORDS[i][1]);
        // Y comes from KB.EXE bytes when available (authoritative);
        // the binary tables's transcription has a known bug at entry 8.
        int gate_y_kb = g_have_kb_towngate_y
            ? g_kb_towngate_y[i]
            : EX_TOWN_GATE_COORDS[i][2];
        cJSON_AddNumberToObject(gate, "y", EX_FLIP_Y(gate_y_kb));
        cJSON_AddItemToObject(o, "gate", gate);

        cJSON_AddStringToObject(o, "zone", ZONES[EX_TOWN_COORDS[i][0]]);

        // intel_castle: each town reports on castle [town_index].
        // Mapping is implicit in the binary tables; ports it as town N → castle N.
        char cslug[96];
        slugify(cslug, sizeof cslug, EX_CASTLE_NAMES[i]);
        cJSON_AddStringToObject(o, "intel_castle", cslug);

        // Hunterville (town 21) pre-places the Bridge spell at game
        // start. KB.EXE/the binary tables-style runtime initialization (every
        // other town gets a randomized salt-pass spell instead).
        if (i == 21) cJSON_AddStringToObject(o, "pinned_spell", "bridge");

        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static cJSON *emit_castles_top(void) {
    cJSON *arr = cJSON_CreateArray();
    static const char *ZONES[4] = { "continentia","forestria","archipelia","saharia" };
    char idbuf[96];
    for (int i = 0; i < 26; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", i);
        slugify(idbuf, sizeof idbuf, EX_CASTLE_NAMES[i]);
        cJSON_AddStringToObject(o, "id",   idbuf);
        cJSON_AddStringToObject(o, "name", EX_CASTLE_NAMES[i]);
        cJSON_AddNumberToObject(o, "x",    EX_CASTLE_COORDS[i][1]);
        cJSON_AddNumberToObject(o, "y",    EX_FLIP_Y(EX_CASTLE_COORDS[i][2]));
        cJSON_AddStringToObject(o, "zone", ZONES[EX_CASTLE_COORDS[i][0]]);
        cJSON_AddNumberToObject(o, "difficulty_tier", EX_CASTLE_DIFFICULTY[i]);
        cJSON_AddItemToArray(arr, o);
    }

    // King Maximus's home castle — castle index 26 in the binary tables's
    // castle_names[] but NOT in castle_coords[] (special_coords[][]
    // stores the home castle anchor at HOME_X/Y).
    // Adds the audience flow + win-condition metadata bountylands uses
    // to wire up the end-game handshake.
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "index", 26);
        cJSON_AddStringToObject(o, "id",   "king_maximus");
        cJSON_AddStringToObject(o, "name", "of King Maximus");
        cJSON_AddNumberToObject(o, "x",    EX_HOME_X);
        cJSON_AddNumberToObject(o, "y",    EX_FLIP_Y(EX_HOME_Y_KB));
        cJSON_AddStringToObject(o, "zone", "continentia");
        cJSON *sp = cJSON_CreateObject();
        cJSON_AddStringToObject(sp, "flow",                  "audience");
        cJSON_AddBoolToObject  (sp, "excluded_from_siege",   1);
        cJSON_AddBoolToObject  (sp, "excluded_from_intel",   1);
        cJSON_AddBoolToObject  (sp, "excluded_from_contract",1);
        cJSON_AddStringToObject(sp, "win_condition", "scepter");
        cJSON *dlg = cJSON_CreateObject();
        cJSON_AddStringToObject(dlg, "header",
            "Castle of King Maximus");
        cJSON_AddStringToObject(dlg, "body",
            "King Maximus bids you welcome,\nHero.\n\n"
            "Return with the stolen Scepter\nof Order and the Four Continents\nshall be yours.");
        cJSON_AddItemToObject(sp, "dialog", dlg);
        cJSON *aud = cJSON_CreateObject();
        cJSON_AddStringToObject(aud, "intro",
            "Trumpets announce your\narrival with regal fanfare.\n\n"
            "King Maximus rises from his\nthrone to greet you and\n"
            "proclaims:           (space)");
        cJSON_AddStringToObject(aud, "rank_up",
            "\n\nCongratulations %NAME%,\n\nI now promote you to\n%RANK%.\n");
        cJSON_AddStringToObject(aud, "more_needed",
            "\n\nMy dear %NAME%,\n\nI can aid you better after\n"
            "you've captured %NEEDED% more\nvillains.");
        cJSON_AddStringToObject(aud, "final_rank",
            "\n\n%NAME% the %RANK%,\n\nHurry and recover my Scepter\n"
            "of Order or all will be\nlost!");
        cJSON_AddItemToObject(sp, "audience", aud);
        cJSON_AddItemToObject(o, "special", sp);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

// puzzle_map[5][6]: raw cell→villain/artifact
// (-1 = villain X = -X-1, otherwise artifact_idx).
// Used to derive per-zone salt budgets and friendly-foe spawns.
// We just emit a simple zone scaffold matching the current bountylands
// schema. Per-zone signs/chests/wandering_armies are scraped from
// LAND.ORG below.

// Salt budgets per continent. Engine-authored constants with no
// upstream binary source; hard-coded by the extractor.
static const struct {
    int artifacts, navmaps, orbs, telecaves, dwellings, friendly_foes;
    int dwelling_range_lo, dwelling_range_hi;
    const char *preferred[8];
} EX_SALT[4] = {
    // continentia
    { 2, 1, 1, 2, 10, 5,  0, 14,
      {"peasants","sprites","orcs","skeletons","wolves","gnomes",NULL,NULL} },
    // forestria
    { 2, 1, 1, 2, 10, 5,  1, 14,
      {"dwarves","zombies","nomads","elves","ogres","elves",NULL,NULL} },
    // archipelia
    { 2, 1, 1, 2, 10, 5,  2, 14,
      {"ghosts","barbarians","trolls","druids",NULL,NULL,NULL,NULL} },
    // saharia
    { 2, 1, 1, 2, 10, 5,  20, 24,
      {"giants","vampires","archmages","dragons","demons",NULL,NULL,NULL} },
};

// Walk LAND.ORG raw bytes for one continent and emit positional entries.
// `raw` is 64*64 bytes in KB Y-up storage. We flip to display Y-down,
// but enumerate KB-storage-order (bottom of display first) to match the
// existing port-side numbering convention (treasure_chest_000 lives at
// display y=60, the south edge, etc.).
static cJSON *scrape_positions(const uint8_t *raw, uint8_t target_byte,
                               const char *id_prefix, int *next_id) {
    cJSON *arr = cJSON_CreateArray();
    for (int kb_y = 0; kb_y < 64; kb_y++) {
        int disp_y = 63 - kb_y;
        for (int x = 0; x < 64; x++) {
            uint8_t b = raw[kb_y * 64 + x];
            if (b != target_byte) continue;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "x", x);
            cJSON_AddNumberToObject(o, "y", disp_y);
            char idbuf[64];
            snprintf(idbuf, sizeof idbuf, "%s_%03d", id_prefix, *next_id);
            cJSON_AddStringToObject(o, "id", idbuf);
            cJSON_AddItemToArray(arr, o);
            (*next_id)++;
        }
    }
    return arr;
}

// Per-zone towns: derived from EX_TOWN_COORDS (filtered by continent).
// Emitted with id slug + boat coords (which are also in the binary tables).
static cJSON *emit_zone_towns(int continent) {
    cJSON *arr = cJSON_CreateArray();
    char idbuf[96];
    for (int i = 0; i < 26; i++) {
        if (EX_TOWN_COORDS[i][0] != continent) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "x", EX_TOWN_COORDS[i][1]);
        cJSON_AddNumberToObject(o, "y", EX_FLIP_Y(EX_TOWN_COORDS[i][2]));
        slugify(idbuf, sizeof idbuf, EX_TOWN_NAMES[i]);
        cJSON_AddStringToObject(o, "id", idbuf);
        cJSON_AddNumberToObject(o, "boat_x", EX_BOAT_COORDS[i][1]);
        cJSON_AddNumberToObject(o, "boat_y", EX_FLIP_Y(EX_BOAT_COORDS[i][2]));
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

// Per-zone castles: derived from EX_CASTLE_COORDS (filtered by continent).
// Continentia also gets King Maximus's home castle entry (special).
static cJSON *emit_zone_castles(int continent) {
    cJSON *arr = cJSON_CreateArray();
    char idbuf[96];
    for (int i = 0; i < 26; i++) {
        if (EX_CASTLE_COORDS[i][0] != continent) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "x", EX_CASTLE_COORDS[i][1]);
        cJSON_AddNumberToObject(o, "y", EX_FLIP_Y(EX_CASTLE_COORDS[i][2]));
        slugify(idbuf, sizeof idbuf, EX_CASTLE_NAMES[i]);
        cJSON_AddStringToObject(o, "id", idbuf);
        cJSON_AddItemToArray(arr, o);
    }
    // King Maximus on continentia (special_coords). The home castle has
    // a decorative outer ring of mini-tower walls surrounding the
    // standard 3x2 castle; the 24 dx/dy/art tuples are hard-coded here.
    if (continent == EX_HOME_CONTINENT) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "x", EX_HOME_X);
        cJSON_AddNumberToObject(o, "y", EX_FLIP_Y(EX_HOME_Y_KB));
        cJSON_AddStringToObject(o, "id", "king_maximus");
        static const struct { int dx, dy; const char *art; } DECOR[] = {
            { -3, -3, "castle_tl" }, { -2, -3, "castle_tr" },
            {  2, -3, "castle_tl" }, {  3, -3, "castle_tr" },
            { -3, -2, "castle_ml" }, { -2, -2, "castle_mr" },
            {  2, -2, "castle_ml" }, {  3, -2, "castle_mr" },
            { -3, -1, "castle_tl" }, { -2, -1, "castle_tr" },
            {  2, -1, "castle_tl" }, {  3, -1, "castle_tr" },
            { -3,  0, "castle_ml" }, { -2,  0, "castle_mr" },
            {  2,  0, "castle_ml" }, {  3,  0, "castle_mr" },
            { -2,  1, "castle_tl" }, { -1,  1, "castle_tr" },
            {  1,  1, "castle_tl" }, {  2,  1, "castle_tr" },
            { -2,  2, "castle_ml" }, { -1,  2, "castle_mr" },
            {  1,  2, "castle_ml" }, {  2,  2, "castle_mr" },
        };
        cJSON *dec = cJSON_CreateArray();
        for (size_t i = 0; i < sizeof(DECOR)/sizeof(DECOR[0]); i++) {
            cJSON *d = cJSON_CreateObject();
            cJSON_AddNumberToObject(d, "dx", DECOR[i].dx);
            cJSON_AddNumberToObject(d, "dy", DECOR[i].dy);
            cJSON_AddStringToObject(d, "art", DECOR[i].art);
            cJSON_AddItemToArray(dec, d);
        }
        cJSON_AddItemToObject(o, "decorations", dec);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static cJSON *emit_zone_salt(int continent) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "artifacts",     EX_SALT[continent].artifacts);
    cJSON_AddNumberToObject(o, "navmaps",       EX_SALT[continent].navmaps);
    cJSON_AddNumberToObject(o, "orbs",          EX_SALT[continent].orbs);
    cJSON_AddNumberToObject(o, "telecaves",     EX_SALT[continent].telecaves);
    cJSON_AddNumberToObject(o, "dwellings",     EX_SALT[continent].dwellings);
    cJSON_AddNumberToObject(o, "friendly_foes", EX_SALT[continent].friendly_foes);
    cJSON *pref = cJSON_CreateArray();
    for (int k = 0; k < 8 && EX_SALT[continent].preferred[k]; k++)
        cJSON_AddItemToArray(pref, cJSON_CreateString(EX_SALT[continent].preferred[k]));
    cJSON_AddItemToObject(o, "preferred_troops", pref);
    cJSON *dr = cJSON_CreateArray();
    cJSON_AddItemToArray(dr, cJSON_CreateNumber(EX_SALT[continent].dwelling_range_lo));
    cJSON_AddItemToArray(dr, cJSON_CreateNumber(EX_SALT[continent].dwelling_range_hi));
    cJSON_AddItemToObject(o, "dwelling_range", dr);
    return o;
}

// Per-zone neighbors. KB allowed any-to-any sailing; emit "all others".
static cJSON *emit_zone_neighbors(int continent) {
    static const char *ZONES[4] = { "continentia","forestria","archipelia","saharia" };
    cJSON *arr = cJSON_CreateArray();
    for (int k = 0; k < 4; k++) {
        if (k == continent) continue;
        cJSON_AddItemToArray(arr, cJSON_CreateString(ZONES[k]));
    }
    return arr;
}

static cJSON *emit_zones(const CcArchive *cc) {
    cJSON *arr = cJSON_CreateArray();
    static const char *ZONE_IDS[4] = { "continentia","forestria","archipelia","saharia" };
    static const size_t LAND_ORG_OFFSETS[4] = { 0x0000, 0x1000, 0x2000, 0x3000 };

    const CcEntry *land = cc ? ex_cc_find(cc, "LAND.ORG") : NULL;

    for (int c = 0; c < 4; c++) {
        cJSON *z = cJSON_CreateObject();
        cJSON_AddStringToObject(z, "id",     ZONE_IDS[c]);
        cJSON_AddStringToObject(z, "name",   EX_CONTINENT_NAMES[c]);

        char map_path[64];
        snprintf(map_path, sizeof map_path, "maps/%s.dat", ZONE_IDS[c]);
        cJSON_AddStringToObject(z, "map", map_path);
        cJSON_AddNumberToObject(z, "width",  64);
        cJSON_AddNumberToObject(z, "height", 64);
        cJSON_AddItemToObject(z, "neighbors", emit_zone_neighbors(c));

        // Hero spawn for home continent: HOME_X/Y (the binary tables) is
        // the castle anchor (top-left). The 3x2 castle has its gate at
        // the bottom-row middle = (HOME_X, HOME_Y+1 KB-up). The player
        // spawns one tile south of the gate. With Y-axis flip applied:
        //   castle anchor display y = 63 - HOME_Y     = 56
        //   castle gate   display y = anchor + 1      = 57
        //   hero spawn    display y = gate + 1 south  = 58
        // Same offset rule for non-home continents off the boat-arrival.
        cJSON *hs = cJSON_CreateObject();
        if (c == EX_HOME_CONTINENT) {
            cJSON_AddNumberToObject(hs, "x", EX_HOME_X);
            cJSON_AddNumberToObject(hs, "y", EX_FLIP_Y(EX_HOME_Y_KB) + 2);
        } else {
            // continent_entry[][]
            static const int ENTRY[4][2] = {
                {11, 3},{ 1,37},{14,62},{ 9, 1},
            };
            cJSON_AddNumberToObject(hs, "x", ENTRY[c][0]);
            cJSON_AddNumberToObject(hs, "y", EX_FLIP_Y(ENTRY[c][1]));
        }
        cJSON_AddItemToObject(z, "hero_spawn", hs);

        if (c == EX_HOME_CONTINENT) {
            cJSON_AddBoolToObject(z, "is_home", 1);
            cJSON *hsp = cJSON_CreateObject();
            cJSON_AddNumberToObject(hsp, "x", EX_HOME_X);
            cJSON_AddNumberToObject(hsp, "y", EX_FLIP_Y(EX_HOME_Y_KB) + 2);
            cJSON_AddItemToObject(z, "home_spawn", hsp);
            cJSON *al = cJSON_CreateObject();
            cJSON_AddNumberToObject(al, "x", EX_ALCOVE_X);
            cJSON_AddNumberToObject(al, "y", EX_FLIP_Y(EX_ALCOVE_Y_KB));
            cJSON_AddItemToObject(z, "magic_alcove", al);
        }

        cJSON_AddItemToObject(z, "salt", emit_zone_salt(c));

        // Position scrape from LAND.ORG. Sequential IDs per zone.
        // Signs come from the embedded blob (text + known positions);
        // chests and wandering_armies are scraped live.
        cJSON *signs   = embedded_signs_for_zone(ZONE_IDS[c]);
        cJSON *chests  = cJSON_CreateArray();
        cJSON *wand    = cJSON_CreateArray();
        if (land && land->size >= LAND_ORG_OFFSETS[c] + 64*64) {
            const uint8_t *raw = land->data + LAND_ORG_OFFSETS[c];
            int cn = 0, wn = 0;
            cJSON_Delete(chests); cJSON_Delete(wand);
            chests = scrape_positions(raw, EX_TILE_CHEST,      "treasure_chest",  &cn);
            wand   = scrape_positions(raw, EX_TILE_FOE,        "wandering_army",  &wn);
        }
        cJSON_AddItemToObject(z, "signs",            signs);
        cJSON_AddItemToObject(z, "towns",            emit_zone_towns(c));
        cJSON_AddItemToObject(z, "castles",          emit_zone_castles(c));
        cJSON_AddItemToObject(z, "chests",           chests);
        cJSON_AddItemToObject(z, "artifacts",        cJSON_CreateArray());  // salted at runtime
        cJSON_AddItemToObject(z, "dwellings",        cJSON_CreateArray());  // salted at runtime
        cJSON_AddItemToObject(z, "wandering_armies", wand);

        cJSON_AddItemToArray(arr, z);
    }
    return arr;
}

// ============================================================================
// Public entry point.
// ============================================================================

// Port-side constants (tile_codes, spawn, contract, controls, sprites,
// ending, colors, credits, audio, strings) are kept in one hand-curated
// JSON literal -- see extract_gamejson_const.inc. These sections are
// engine-authored data (UI labels, asset paths, dialog templates, ending
// choreography) and have no upstream binary source; they're hard-coded
// by the extractor.
#include "extract_gamejson_const.inc"

// Sign text per-zone, baked from a hand-curated source. KB.EXE-resident
// signs are read directly by the binary scrapers below.
#include "extract_gamejson_signs.inc"

// Look up the per-zone signs array from the embedded blob.
static cJSON *embedded_signs_for_zone(const char *zone_id) {
    static cJSON *cached = NULL;
    if (!cached) cached = cJSON_Parse(EX_SIGNS_JSON);
    if (!cached) return cJSON_CreateArray();
    cJSON *zarr = cJSON_GetObjectItem(cached, zone_id);
    if (!zarr) return cJSON_CreateArray();
    return cJSON_Duplicate(zarr, 1);
}

// At extract time we override the towngate Y coordinates from the live
// KB.EXE bytes; the binary tables's transcription has a known bug at entry 8.
// The X array in KB.EXE matches the binary tables exactly, so we use it as a
// signature to locate the table.
static void try_load_towngate_from_kb(const uint8_t *kb, size_t kb_len) {
    if (!kb || kb_len < 26 + 26) return;
    static const uint8_t SIG_X[26] = {
        0x1d,0x39,0x26,0x23,0x05,0x10,0x0c,0x09,0x0d,0x39,0x33,0x39,0x03,
        0x10,0x28,0x32,0x3a,0x38,0x09,0x0d,0x06,0x0c,0x2f,0x32,0x03,0x3a,
    };
    for (size_t i = 0; i + sizeof(SIG_X) + 26 <= kb_len; i++) {
        if (memcmp(kb + i, SIG_X, sizeof(SIG_X)) == 0) {
            memcpy(g_kb_towngate_y, kb + i + sizeof(SIG_X), 26);
            g_have_kb_towngate_y = 1;
            fprintf(stderr,
                "extract: located towngate Y array in KB.EXE @ 0x%zx\n",
                i + sizeof(SIG_X));
            return;
        }
    }
    fprintf(stderr,
        "extract: warning: towngate Y signature not found in KB.EXE — "
        "falling back to the binary tables transcription (entry 8 may be wrong)\n");
}

cJSON *ex_gamejson_synthesize(const CcArchive *cc,
                              const uint8_t *kb, size_t kb_len) {
    g_have_kb_towngate_y = 0;
    try_load_towngate_from_kb(kb, kb_len);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "title",     "King's Bounty");
    cJSON_AddNumberToObject(root, "version",   1);
    cJSON_AddStringToObject(root, "pack_id",   "kings-bounty");
    cJSON_AddStringToObject(root, "pack_name", "King's Bounty");
    cJSON_AddStringToObject(root, "pack_kind", "base");

    cJSON_AddItemToObject(root, "world",     emit_world());
    cJSON_AddItemToObject(root, "time",      emit_time());
    cJSON_AddItemToObject(root, "economy",   emit_economy());
    cJSON_AddItemToObject(root, "tuning",    emit_tuning());
    cJSON_AddItemToObject(root, "combat",    emit_combat());
    cJSON_AddItemToObject(root, "zones",     emit_zones(cc));
    cJSON_AddItemToObject(root, "towns",     emit_towns_top());
    cJSON_AddItemToObject(root, "castles",   emit_castles_top());
    cJSON_AddItemToObject(root, "troops",    emit_troops());
    cJSON_AddItemToObject(root, "spells",    emit_spells());
    cJSON_AddItemToObject(root, "classes",   emit_classes());
    cJSON_AddItemToObject(root, "villains",  emit_villains());
    cJSON_AddItemToObject(root, "artifacts", emit_artifacts());

    // Splice in the port-side constants block.
    cJSON *consts = cJSON_Parse(EX_PORT_CONSTANTS_JSON);
    if (consts) {
        cJSON *child = consts->child;
        while (child) {
            cJSON *next = child->next;
            cJSON_DetachItemViaPointer(consts, child);
            cJSON_AddItemToObject(root, child->string, child);
            child = next;
        }
        cJSON_Delete(consts);
    }
    return root;
}

// Standalone test entry — see this file's top comment for build command.
#ifdef EXTRACT_GAMEJSON_TEST_MAIN
int main(void) {
    cJSON *root = ex_gamejson_synthesize(NULL, NULL, 0);
    char *s = cJSON_Print(root);
    printf("%s\n", s);
    free(s);
    cJSON_Delete(root);
    return 0;
}
#endif
