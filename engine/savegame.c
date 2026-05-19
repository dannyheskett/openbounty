#include "savegame.h"
#include "game.h"
#include "resources.h"
#include "tables.h"
#include "state_serialize.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Save format v7 — JSON, mirrors the Game struct in game.h for human
// readability. IDs are strings (e.g. "knight", "peasants", "murray").
// Fog is stored per-continent, one hex string per row (each hex nibble
// = 4 tiles, '1' bit = seen). 64-tile rows = 16 hex chars.

static char *read_whole_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)n;
    return buf;
}

// Forward direction (Difficulty -> id) lives in state_serialize.c now.
static Difficulty difficulty_from_id(const char *s) {
    if (!s) return DIFFICULTY_EASY;
    if (strcmp(s, "normal") == 0) return DIFFICULTY_NORMAL;
    if (strcmp(s, "hard") == 0) return DIFFICULTY_HARD;
    if (strcmp(s, "impossible") == 0) return DIFFICULTY_IMPOSSIBLE;
    return DIFFICULTY_EASY;
}
// Forward direction (Mount -> id) lives in state_serialize.c now.
static Mount mount_from_id(const char *s) {
    if (!s) return MOUNT_RIDE;
    if (strcmp(s, "sail") == 0) return MOUNT_SAIL;
    if (strcmp(s, "fly")  == 0) return MOUNT_FLY;
    return MOUNT_RIDE;
}
// Forward direction (CastleOwnerKind -> id) lives in state_serialize.c now.
static CastleOwnerKind castle_owner_from_id(const char *s) {
    if (!s) return CASTLE_OWNER_PLAYER;
    if (strcmp(s, "monsters") == 0) return CASTLE_OWNER_MONSTERS;
    if (strcmp(s, "villain")  == 0) return CASTLE_OWNER_VILLAIN;
    if (strcmp(s, "special")  == 0) return CASTLE_OWNER_SPECIAL;
    return CASTLE_OWNER_PLAYER;
}

// ----- Fog encoding ---------------------------------------------------------
// Encode lives in state_serialize.c now. Decode (load path) stays here.
static bool decode_fog_row(Fog *fog, int row, int width, const char *s) {
    int nibbles = (width + 3) / 4;
    if ((int)strlen(s) != nibbles) return false;
    for (int n = 0; n < nibbles; n++) {
        char c = s[n];
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
        else return false;
        for (int b = 0; b < 4; b++) {
            int x = n * 4 + b;
            if (x >= width) break;
            fog->seen[row][x] = (v >> (3 - b)) & 1;
        }
    }
    return true;
}

// ----- Unit array (troops + counts) ----------------------------------------
// Build lives in state_serialize.c now. Parse (load path) stays here.
static void parse_unit_array(const cJSON *arr, Unit *out, int max) {
    memset(out, 0, sizeof(Unit) * max);
    if (!cJSON_IsArray(arr)) return;
    int i = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (i >= max) break;
        cJSON *jtroop = cJSON_GetObjectItem(it, "troop");
        cJSON *jcount = cJSON_GetObjectItem(it, "count");
        if (cJSON_IsString(jtroop) && cJSON_IsNumber(jcount)) {
            size_t k = 0;
            while (k + 1 < sizeof(out[i].id) && jtroop->valuestring[k]) {
                out[i].id[k] = jtroop->valuestring[k]; k++;
            }
            out[i].id[k] = '\0';
            out[i].count = jcount->valueint;
            i++;
        }
    }
}

SaveResult SaveGameWrite(const char *path,
                         const Game *g,
                         const Map *map,
                         const Fog *fog) {
    // Single source of truth for game-state JSON: state_build_snapshot
    // produces a cJSON tree that is a superset of the save format.
    // The save loader (SaveGameRead, below) ignores the harness-only
    // additive fields (tiles/combat/dialog/view).
    cJSON *root = state_build_snapshot(g, NULL, map, fog,
                                       NULL, NULL, 0, 0);
    if (!root) return SAVE_ERR_IO;

    char *out = cJSON_Print(root);
    cJSON_Delete(root);
    if (!out) return SAVE_ERR_IO;

    FILE *f = fopen(path, "wb");
    if (!f) { free(out); return SAVE_ERR_IO; }
    fwrite(out, 1, strlen(out), f);
    fclose(f);
    free(out);
    return SAVE_OK;
}

static void copy_json_string(char *dst, size_t dst_sz, const cJSON *j) {
    if (!cJSON_IsString(j)) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (i + 1 < dst_sz && j->valuestring[i]) { dst[i] = j->valuestring[i]; i++; }
    dst[i] = '\0';
}

SaveResult SaveGameRead(const char *path,
                        Game *g,
                        Map *map,
                        Fog *fog) {
    size_t sz = 0;
    char *text = read_whole_file(path, &sz);
    if (!text) return SAVE_ERR_IO;

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) return SAVE_ERR_PARSE;

    cJSON *jver = cJSON_GetObjectItem(root, "version");
    if (!cJSON_IsNumber(jver) || jver->valueint != (int)SAVE_VERSION) {
        cJSON_Delete(root); return SAVE_ERR_VERSION;
    }

    // Cross-pack guard: refuse loads from a different pack — IDs in the
    // save belong to the other pack's catalog and would resolve wrong.
    cJSON *jpack = cJSON_GetObjectItem(root, "pack_id");
    if (g && g->res && g->res->pack_id[0] &&
        cJSON_IsString(jpack) && jpack->valuestring &&
        strcmp(jpack->valuestring, g->res->pack_id) != 0) {
        cJSON_Delete(root); return SAVE_ERR_PACK;
    }

    const Resources *keep_res = g->res;
    memset(g, 0, sizeof(*g));
    g->res = keep_res;
    g->version = SAVE_VERSION;

    cJSON *jseed = cJSON_GetObjectItem(root, "seed");
    if (cJSON_IsNumber(jseed)) g->seed = (uint64_t)jseed->valuedouble;

    // Character.
    cJSON *jc = cJSON_GetObjectItem(root, "character");
    if (cJSON_IsObject(jc)) {
        copy_json_string(g->character.name, sizeof(g->character.name),
                         cJSON_GetObjectItem(jc, "name"));
        copy_json_string(g->character.cls.id, sizeof(g->character.cls.id),
                         cJSON_GetObjectItem(jc, "class"));
        cJSON *jr = cJSON_GetObjectItem(jc, "rank_index");
        g->character.cls.rank_index = cJSON_IsNumber(jr) ? jr->valueint : 0;
        copy_json_string(g->character.cls.rank_title, sizeof(g->character.cls.rank_title),
                         cJSON_GetObjectItem(jc, "rank_title"));
        const ClassDef *cdef = class_by_id(g->character.cls.id);
        if (cdef) {
            int ri = g->character.cls.rank_index;
            if (ri < 0) ri = 0;
            if (ri > 3) ri = 3;
            size_t k = 0;
            while (k + 1 < sizeof(g->character.cls.rank_id) && cdef->ranks[ri].id[k]) {
                g->character.cls.rank_id[k] = cdef->ranks[ri].id[k]; k++;
            }
            g->character.cls.rank_id[k] = '\0';
        }
        cJSON *jd = cJSON_GetObjectItem(jc, "difficulty");
        g->character.difficulty = difficulty_from_id(cJSON_IsString(jd) ? jd->valuestring : NULL);
        cJSON *jm = cJSON_GetObjectItem(jc, "mount");
        g->character.mount = mount_from_id(cJSON_IsString(jm) ? jm->valuestring : NULL);
    }

    // Stats.
    cJSON *js = cJSON_GetObjectItem(root, "stats");
    if (cJSON_IsObject(js)) {
        #define GS_INT(field, key) do { \
            cJSON *_j = cJSON_GetObjectItem(js, key); \
            if (cJSON_IsNumber(_j)) g->stats.field = _j->valueint; \
        } while (0)
        GS_INT(gold, "gold");
        GS_INT(commission_weekly, "commission_weekly");
        GS_INT(leadership_base, "leadership_base");
        GS_INT(leadership_current, "leadership_current");
        GS_INT(followers_killed, "followers_killed");
        GS_INT(score, "score");
        GS_INT(spell_power, "spell_power");
        GS_INT(max_spells, "max_spells");
        GS_INT(siege_weapons, "siege_weapons");
        GS_INT(time_stop, "time_stop");
        GS_INT(steps_left_today, "steps_left_today");
        GS_INT(days_left, "days_left");
        GS_INT(last_commission, "last_commission");
        #undef GS_INT
        cJSON *jkm = cJSON_GetObjectItem(js, "knows_magic");
        g->stats.knows_magic = cJSON_IsBool(jkm) ? cJSON_IsTrue(jkm) : false;
        cJSON *jgo = cJSON_GetObjectItem(js, "game_over");
        g->stats.game_over = cJSON_IsBool(jgo) ? cJSON_IsTrue(jgo) : false;
        cJSON *jopts = cJSON_GetObjectItem(js, "options");
        if (cJSON_IsArray(jopts)) {
            int i = 0;
            cJSON *it;
            cJSON_ArrayForEach(it, jopts) {
                if (i >= 8) break;
                if (cJSON_IsNumber(it))
                    g->stats.options[i] = it->valueint;
                i++;
            }
        }
    }

    // Position.
    cJSON *jp = cJSON_GetObjectItem(root, "position");
    if (cJSON_IsObject(jp)) {
        cJSON *jpz = cJSON_GetObjectItem(jp, "zone");
        if (!cJSON_IsString(jpz)) jpz = cJSON_GetObjectItem(jp, "continent");
        copy_json_string(g->position.zone, sizeof(g->position.zone), jpz);
        cJSON *jx = cJSON_GetObjectItem(jp, "x");
        cJSON *jy = cJSON_GetObjectItem(jp, "y");
        cJSON *jlx = cJSON_GetObjectItem(jp, "last_x");
        cJSON *jly = cJSON_GetObjectItem(jp, "last_y");
        cJSON *jf = cJSON_GetObjectItem(jp, "facing_left");
        g->position.x = cJSON_IsNumber(jx) ? jx->valueint : 0;
        g->position.y = cJSON_IsNumber(jy) ? jy->valueint : 0;
        g->position.last_x = cJSON_IsNumber(jlx) ? jlx->valueint : g->position.x;
        g->position.last_y = cJSON_IsNumber(jly) ? jly->valueint : g->position.y;
        g->position.facing_left = cJSON_IsBool(jf) ? cJSON_IsTrue(jf) : false;
        cJSON *jtm = cJSON_GetObjectItem(jp, "travel_mode");
        g->travel_mode = (cJSON_IsString(jtm) && strcmp(jtm->valuestring, "boat") == 0)
            ? TRAVEL_BOAT : TRAVEL_WALK;
        cJSON *jhv = cJSON_GetObjectItem(jp, "hud_visible");
        g->hud_visible = cJSON_IsBool(jhv) ? cJSON_IsTrue(jhv) : true;
    }

    // Army.
    cJSON *ja = cJSON_GetObjectItem(root, "army");
    if (cJSON_IsArray(ja)) {
        cJSON *it;
        cJSON_ArrayForEach(it, ja) {
            cJSON *jslot = cJSON_GetObjectItem(it, "slot");
            cJSON *jtroop = cJSON_GetObjectItem(it, "troop");
            cJSON *jcount = cJSON_GetObjectItem(it, "count");
            if (!cJSON_IsNumber(jslot)) continue;
            int slot = jslot->valueint;
            if (slot < 0 || slot >= GAME_ARMY_SLOTS) continue;
            copy_json_string(g->army[slot].id, sizeof(g->army[slot].id), jtroop);
            g->army[slot].count = cJSON_IsNumber(jcount) ? jcount->valueint : 0;
        }
    }

    // Spells.
    cJSON *jsp = cJSON_GetObjectItem(root, "spells");
    if (cJSON_IsObject(jsp)) {
        for (int i = 0; i < spells_count() && i < 14; i++) {
            const SpellDef *sd = spell_by_index(i);
            if (!sd) continue;
            cJSON *c = cJSON_GetObjectItem(jsp, sd->id);
            if (cJSON_IsNumber(c)) g->spells.counts[i] = c->valueint;
        }
    }

    // Contract.
    cJSON *jct = cJSON_GetObjectItem(root, "contract");
    if (cJSON_IsObject(jct)) {
        copy_json_string(g->contract.active_id, sizeof(g->contract.active_id),
                         cJSON_GetObjectItem(jct, "active"));
        cJSON *jcycle = cJSON_GetObjectItem(jct, "cycle");
        if (cJSON_IsArray(jcycle)) {
            int i = 0;
            cJSON *it;
            cJSON_ArrayForEach(it, jcycle) {
                if (i >= 5) break;
                copy_json_string(g->contract.cycle[i], sizeof(g->contract.cycle[i]), it);
                i++;
            }
        }
        cJSON *jlc = cJSON_GetObjectItem(jct, "last_contract");
        if (cJSON_IsNumber(jlc)) g->contract.last_contract = jlc->valueint;
        cJSON *jmc = cJSON_GetObjectItem(jct, "max_contract");
        if (cJSON_IsNumber(jmc)) g->contract.max_contract  = jmc->valueint;
        cJSON *jcaught = cJSON_GetObjectItem(jct, "villains_caught");
        if (cJSON_IsArray(jcaught)) {
            cJSON *it;
            cJSON_ArrayForEach(it, jcaught) {
                if (!cJSON_IsString(it)) continue;
                const VillainDef *v = villain_by_id(it->valuestring);
                if (v) g->contract.villains_caught[v->index] = true;
            }
        }
    }

    // Artifacts.
    cJSON *jart = cJSON_GetObjectItem(root, "artifacts");
    if (cJSON_IsObject(jart)) {
        cJSON *jfound = cJSON_GetObjectItem(jart, "found");
        if (cJSON_IsArray(jfound)) {
            cJSON *it;
            cJSON_ArrayForEach(it, jfound) {
                if (!cJSON_IsString(it)) continue;
                const ArtifactDef *a = artifact_by_id(it->valuestring);
                if (a) g->artifacts.found[a->index] = true;
            }
        }
    }

    // World progress.
    cJSON *jw = cJSON_GetObjectItem(root, "world");
    if (cJSON_IsObject(jw)) {
        cJSON *jdisc = cJSON_GetObjectItem(jw, "zones_discovered");
        if (cJSON_IsArray(jdisc)) {
            cJSON *it;
            cJSON_ArrayForEach(it, jdisc) {
                if (!cJSON_IsString(it)) continue;
                const ResZone *z = resources_zone_by_id(g->res, it->valuestring);
                if (z && g->res) {
                    int i = (int)(z - g->res->zones);
                    if (i >= 0 && i < GAME_CONTINENTS)
                        g->world.zones_discovered[i] = true;
                }
            }
        }
        cJSON *jorbs = cJSON_GetObjectItem(jw, "orbs_found");
        if (cJSON_IsArray(jorbs)) {
            cJSON *it;
            cJSON_ArrayForEach(it, jorbs) {
                if (!cJSON_IsString(it)) continue;
                const ResZone *z = resources_zone_by_id(g->res, it->valuestring);
                if (z && g->res) {
                    int i = (int)(z - g->res->zones);
                    if (i >= 0 && i < GAME_CONTINENTS)
                        g->world.orbs_found[i] = true;
                }
            }
        }
        cJSON *jpuz = cJSON_GetObjectItem(jw, "puzzle_revealed");
        if (cJSON_IsArray(jpuz)) {
            cJSON *it;
            cJSON_ArrayForEach(it, jpuz) {
                if (!cJSON_IsString(it)) continue;
                const VillainDef *v = villain_by_id(it->valuestring);
                if (v) { g->world.puzzle_revealed[v->index] = true; continue; }
                const ArtifactDef *a = artifact_by_id(it->valuestring);
                if (a) g->world.puzzle_revealed[17 + a->index] = true;
            }
        }
    }

    // Boat.
    cJSON *jb = cJSON_GetObjectItem(root, "boat");
    if (cJSON_IsObject(jb)) {
        cJSON *jh = cJSON_GetObjectItem(jb, "has_boat");
        cJSON *jx = cJSON_GetObjectItem(jb, "x");
        cJSON *jy = cJSON_GetObjectItem(jb, "y");
        g->boat.has_boat = cJSON_IsBool(jh) ? cJSON_IsTrue(jh) : false;
        g->boat.x = cJSON_IsNumber(jx) ? jx->valueint : -1;
        g->boat.y = cJSON_IsNumber(jy) ? jy->valueint : -1;
        cJSON *jbz = cJSON_GetObjectItem(jb, "zone");
        if (!cJSON_IsString(jbz)) jbz = cJSON_GetObjectItem(jb, "continent");
        copy_json_string(g->boat.zone, sizeof(g->boat.zone), jbz);
    }

    // Towns.
    cJSON *jtowns = cJSON_GetObjectItem(root, "towns");
    if (cJSON_IsArray(jtowns)) {
        int i = 0;
        cJSON *it;
        cJSON_ArrayForEach(it, jtowns) {
            if (i >= GAME_TOWNS) break;
            copy_json_string(g->towns[i].id, sizeof(g->towns[i].id),
                             cJSON_GetObjectItem(it, "id"));
            cJSON *jv = cJSON_GetObjectItem(it, "visited");
            g->towns[i].visited = cJSON_IsBool(jv) ? cJSON_IsTrue(jv) : false;
            copy_json_string(g->towns[i].spell_for_sale, sizeof(g->towns[i].spell_for_sale),
                             cJSON_GetObjectItem(it, "spell_for_sale"));
            i++;
        }
    }

    // Castles.
    cJSON *jcastles = cJSON_GetObjectItem(root, "castles");
    if (cJSON_IsArray(jcastles)) {
        int i = 0;
        cJSON *it;
        cJSON_ArrayForEach(it, jcastles) {
            if (i >= GAME_CASTLES) break;
            copy_json_string(g->castles[i].id, sizeof(g->castles[i].id),
                             cJSON_GetObjectItem(it, "id"));
            cJSON *jv = cJSON_GetObjectItem(it, "visited");
            cJSON *jk = cJSON_GetObjectItem(it, "known");
            g->castles[i].visited = cJSON_IsBool(jv) ? cJSON_IsTrue(jv) : false;
            g->castles[i].known   = cJSON_IsBool(jk) ? cJSON_IsTrue(jk) : false;
            cJSON *jo = cJSON_GetObjectItem(it, "owner");
            g->castles[i].owner_kind = castle_owner_from_id(cJSON_IsString(jo) ? jo->valuestring : NULL);
            copy_json_string(g->castles[i].villain_id, sizeof(g->castles[i].villain_id),
                             cJSON_GetObjectItem(it, "villain"));
            parse_unit_array(cJSON_GetObjectItem(it, "garrison"),
                             g->castles[i].garrison, GAME_ARMY_SLOTS);
            i++;
        }
    }

    // Scepter.
    cJSON *jsc = cJSON_GetObjectItem(root, "scepter");
    if (cJSON_IsObject(jsc)) {
        // Accept both "zone" (current) and legacy "continent".
        cJSON *jz = cJSON_GetObjectItem(jsc, "zone");
        if (!cJSON_IsString(jz)) jz = cJSON_GetObjectItem(jsc, "continent");
        if (cJSON_IsString(jz)) {
            size_t k = 0;
            while (k + 1 < sizeof(g->scepter.zone) && jz->valuestring[k]) {
                g->scepter.zone[k] = jz->valuestring[k]; k++;
            }
            g->scepter.zone[k] = '\0';
        }
        cJSON *jx = cJSON_GetObjectItem(jsc, "x");
        cJSON *jy = cJSON_GetObjectItem(jsc, "y");
        g->scepter.x = cJSON_IsNumber(jx) ? jx->valueint : 0;
        g->scepter.y = cJSON_IsNumber(jy) ? jy->valueint : 0;
    }

    // Consumed tiles.
    g->consumed_count = 0;
    cJSON *jconsumed = cJSON_GetObjectItem(root, "consumed");
    if (cJSON_IsArray(jconsumed)) {
        cJSON *m;
        cJSON_ArrayForEach(m, jconsumed) {
            if (g->consumed_count >= GAME_MAX_MUTATIONS) break;
            cJSON *jz = cJSON_GetObjectItem(m, "zone");
            if (!cJSON_IsString(jz)) jz = cJSON_GetObjectItem(m, "continent");
            cJSON *jx = cJSON_GetObjectItem(m, "x");
            cJSON *jy = cJSON_GetObjectItem(m, "y");
            if (!cJSON_IsString(jz) ||
                !cJSON_IsNumber(jx) || !cJSON_IsNumber(jy)) continue;
            TileMutation *tm = &g->consumed[g->consumed_count++];
            copy_json_string(tm->zone, sizeof(tm->zone), jz);
            tm->x = jx->valueint;
            tm->y = jy->valueint;
        }
    }
    // Tile mutations are applied later by main.c after MapLoadZone runs
    // (this SaveGameRead runs before the map is populated, so there's
    // nothing to apply to here).

    // Salted placements — randomized objects produced by GameInit. Read
    // them back so MapLoadZoneWithPlacements can re-stamp them on zone
    // switches after the save is loaded.
    g->placement_count = 0;
    cJSON *jplacements = cJSON_GetObjectItem(root, "placements");
    if (cJSON_IsArray(jplacements)) {
        cJSON *m;
        cJSON_ArrayForEach(m, jplacements) {
            if (g->placement_count >= GAME_MAX_PLACEMENTS) break;
            cJSON *jz = cJSON_GetObjectItem(m, "zone");
            cJSON *jx = cJSON_GetObjectItem(m, "x");
            cJSON *jy = cJSON_GetObjectItem(m, "y");
            cJSON *jk = cJSON_GetObjectItem(m, "kind");
            cJSON *jid = cJSON_GetObjectItem(m, "id");
            if (!cJSON_IsString(jz) || !cJSON_IsNumber(jx) ||
                !cJSON_IsNumber(jy) || !cJSON_IsString(jk)) continue;
            int kind = (int)InteractFromString(jk->valuestring);
            SaltedPlacement *p = &g->placements[g->placement_count++];
            copy_json_string(p->zone, sizeof(p->zone), jz);
            p->x = jx->valueint;
            p->y = jy->valueint;
            p->kind = kind;
            if (cJSON_IsString(jid)) copy_json_string(p->id, sizeof(p->id), jid);
            else p->id[0] = '\0';
        }
    }

    // Hostile foe state. Paired with SALT_HOSTILE placements above.
    g->foe_count = 0;
    cJSON *jfoes = cJSON_GetObjectItem(root, "foes");
    if (cJSON_IsArray(jfoes)) {
        cJSON *m;
        cJSON_ArrayForEach(m, jfoes) {
            if (g->foe_count >= GAME_MAX_FOES) break;
            cJSON *jz = cJSON_GetObjectItem(m, "zone");
            cJSON *jx = cJSON_GetObjectItem(m, "x");
            cJSON *jy = cJSON_GetObjectItem(m, "y");
            cJSON *jid = cJSON_GetObjectItem(m, "id");
            cJSON *ja  = cJSON_GetObjectItem(m, "alive");
            cJSON *jfr = cJSON_GetObjectItem(m, "friendly");
            cJSON *jg  = cJSON_GetObjectItem(m, "garrison");
            if (!cJSON_IsString(jz) || !cJSON_IsNumber(jx) ||
                !cJSON_IsNumber(jy) || !cJSON_IsString(jid)) continue;
            FoeState *f = &g->foes[g->foe_count++];
            copy_json_string(f->zone, sizeof(f->zone), jz);
            f->x = jx->valueint;
            f->y = jy->valueint;
            copy_json_string(f->placement_id, sizeof(f->placement_id), jid);
            f->alive = cJSON_IsBool(ja) ? cJSON_IsTrue(ja) : true;
            // Legacy saves predate the `friendly` flag — fall back to
            // inspecting the placement id, which encoded the type.
            if (cJSON_IsBool(jfr)) {
                f->friendly = cJSON_IsTrue(jfr);
            } else {
                f->friendly = (strstr(f->placement_id, "friendly") != NULL);
            }
            for (int s = 0; s < GAME_ARMY_SLOTS; s++) {
                f->garrison[s].id[0] = '\0';
                f->garrison[s].count = 0;
            }
            if (cJSON_IsArray(jg)) {
                int s = 0;
                cJSON *u;
                cJSON_ArrayForEach(u, jg) {
                    if (s >= GAME_ARMY_SLOTS) break;
                    cJSON *uid = cJSON_GetObjectItem(u, "id");
                    cJSON *uc  = cJSON_GetObjectItem(u, "count");
                    if (cJSON_IsString(uid))
                        copy_json_string(f->garrison[s].id,
                                         sizeof(f->garrison[s].id), uid);
                    if (cJSON_IsNumber(uc))
                        f->garrison[s].count = uc->valueint;
                    s++;
                }
            }
        }
    }

    // Fog. The save format stores one entry per discovered continent
    // under map_state[zone_id], each with a "fog" hex-per-row array.
    // We populate world.continent_fog[] for every zone present, then
    // copy the active zone's fog into the live *fog. Backwards-compat:
    // older saves only have the active zone's entry — the rest stay
    // zero-initialized, which is correct for "not yet visited".
    FogInit(fog);
    for (int zi = 0; zi < GAME_CONTINENTS; zi++) {
        FogInit(&g->world.continent_fog[zi]);
    }
    cJSON *jmstate = cJSON_GetObjectItem(root, "map_state");
    if (cJSON_IsObject(jmstate) && g->res) {
        const char *active = (g->position.zone[0]) ? g->position.zone
                           : (map->name[0] ? map->name : "continentia");
        cJSON *child;
        cJSON_ArrayForEach(child, jmstate) {
            if (!cJSON_IsObject(child) || !child->string) continue;
            const ResZone *rz = resources_zone_by_id(g->res, child->string);
            if (!rz) continue;
            int zi = (int)(rz - g->res->zones);
            if (zi < 0 || zi >= GAME_CONTINENTS) continue;
            int fog_w = rz->width;
            int fog_h = rz->height;
            cJSON *rows = cJSON_GetObjectItem(child, "fog");
            if (!cJSON_IsArray(rows)) continue;
            Fog *dst = &g->world.continent_fog[zi];
            int y = 0;
            cJSON *row;
            cJSON_ArrayForEach(row, rows) {
                if (y >= fog_h) break;
                if (!cJSON_IsString(row)) { y++; continue; }
                if (!decode_fog_row(dst, y, fog_w, row->valuestring)) {
                    cJSON_Delete(root); return SAVE_ERR_MISMATCH;
                }
                y++;
            }
            // The active zone's snapshot is also the live fog.
            if (strcmp(child->string, active) == 0) {
                *fog = *dst;
            }
        }
    }

    cJSON_Delete(root);
    return SAVE_OK;
}

SaveResult SaveGameReadHeader(const char *path, SaveHeader *out) {
    if (!out) return SAVE_ERR_IO;
    memset(out, 0, sizeof(*out));
    if (!path) return SAVE_ERR_IO;

    size_t len = 0;
    char *buf = read_whole_file(path, &len);
    if (!buf) return SAVE_ERR_IO;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return SAVE_ERR_PARSE;

    cJSON *jver = cJSON_GetObjectItem(root, "version");
    if (!cJSON_IsNumber(jver) || jver->valueint != (int)SAVE_VERSION) {
        cJSON_Delete(root);
        return SAVE_ERR_VERSION;
    }

    out->exists = true;

    cJSON *jc = cJSON_GetObjectItem(root, "character");
    if (cJSON_IsObject(jc)) {
        cJSON *jn = cJSON_GetObjectItem(jc, "name");
        if (cJSON_IsString(jn))
            strncpy(out->name, jn->valuestring, sizeof(out->name) - 1);
        cJSON *jcls = cJSON_GetObjectItem(jc, "class");
        if (cJSON_IsString(jcls))
            strncpy(out->class_id, jcls->valuestring, sizeof(out->class_id) - 1);
        cJSON *jrt = cJSON_GetObjectItem(jc, "rank_title");
        if (cJSON_IsString(jrt))
            strncpy(out->rank_title, jrt->valuestring,
                    sizeof(out->rank_title) - 1);
    }
    cJSON *js = cJSON_GetObjectItem(root, "stats");
    if (cJSON_IsObject(js)) {
        cJSON *jdl = cJSON_GetObjectItem(js, "days_left");
        if (cJSON_IsNumber(jdl)) out->days_left = jdl->valueint;
        cJSON *jg = cJSON_GetObjectItem(js, "gold");
        if (cJSON_IsNumber(jg)) out->gold = jg->valueint;
    }
    cJSON *jp = cJSON_GetObjectItem(root, "position");
    if (cJSON_IsObject(jp)) {
        cJSON *jz = cJSON_GetObjectItem(jp, "zone");
        if (cJSON_IsString(jz))
            strncpy(out->zone, jz->valuestring, sizeof(out->zone) - 1);
    }
    cJSON *jpack = cJSON_GetObjectItem(root, "pack_id");
    if (cJSON_IsString(jpack) && jpack->valuestring) {
        strncpy(out->pack_id, jpack->valuestring, sizeof(out->pack_id) - 1);
    }
    cJSON *jph = cJSON_GetObjectItem(root, "pack_hash");
    if (cJSON_IsString(jph) && jph->valuestring) {
        strncpy(out->pack_hash, jph->valuestring, sizeof(out->pack_hash) - 1);
    }

    cJSON_Delete(root);
    return SAVE_OK;
}

const char *SaveResultText(SaveResult r) {
    switch (r) {
        case SAVE_OK:           return "OK";
        case SAVE_ERR_IO:       return "I/O error";
        case SAVE_ERR_PARSE:    return "parse error";
        case SAVE_ERR_VERSION:  return "save was made by an older OpenBounty";
        case SAVE_ERR_MISMATCH: return "save doesn't match current world";
        case SAVE_ERR_PACK:     return "save belongs to a different game pack";
        default:                return "unknown error";
    }
}
