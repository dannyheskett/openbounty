// autoplay/autoplay.c
//
// Entry + boot + verdict (AP-010..AP-017). The engine-only consumer pattern:
// pack + Resources + Game/Map/Fog, no src/. autoplay_run calls search_run
// exactly once; that single call drives the whole game to its verdict, and
// this file holds no loop of its own.

#include "autoplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "exec.h"
#include "exec_ledger.h"
#include "pack.h"
#include "pending.h"
#include "recording.h"
#include "search.h"
#include "spells_adventure.h"
#include "worldsnap.h"

#define AUTOPLAY_MANIFEST "game.json"
#define AUTOPLAY_REC_CAP  262144

const char *autoplay_verdict_str(const AutoplayResult *r) {
    return (r && r->solved) ? "SOLVED" : "NOT-SOLVED";
}

bool autoplay_run(const AutoplayConfig *cfg, AutoplayResult *out) {
    if (!cfg || !out || cfg->seed_index < 0 || cfg->seed_index > 255 ||
        !cfg->pack_dir || !cfg->pack_dir[0])
        return false;
    memset(out, 0, sizeof *out);

    Pack *pack = pack_open(cfg->pack_dir);
    if (!pack) return false;
    // The host's published catalog (may be NULL). The run below loads a
    // PRIVATE Resources copy, which republishes the catalog singleton behind
    // troop_by_id etc., and resources_free retracts it to NULL on the way
    // out -- so the host's continuing world (the visible replay, AP-024) must
    // get its own catalog re-published before this returns, or every table
    // lookup after the run reads an empty catalog and context-dependent
    // engine actions silently no-op.
    const Resources *host_res = resources_current();
    pack_stack_push(pack);

    Resources *res = calloc(1, sizeof *res);
    if (!res || !resources_load(res, AUTOPLAY_MANIFEST)) {
        free(res);
        pack_stack_pop();
        resources_republish(host_res);
        return false;
    }
    Game *game = calloc(1, sizeof *game);
    Map *map = calloc(1, sizeof *map);
    Fog *fog = calloc(1, sizeof *fog);
    if (!game || !map || !fog || !recsink_init(AUTOPLAY_REC_CAP)) {
        free(fog);
        free(map);
        free(game);
        resources_free(res);
        free(res);
        pack_stack_pop();
        resources_republish(host_res);
        return false;
    }

    ledger_reset();
    recruit_exclusions_reset();
    // The day budget comes ENTIRELY from the chosen difficulty (the pack's
    // days_per_difficulty knob); there is no bespoke override. Every real
    // difficulty is a whole multiple of week_days, so the weekly economy
    // (commission, astrology, growth, restock) stays phase-locked and play is
    // the pack's own calendar for that level.
    int difficulty = cfg->difficulty;
    if (difficulty < 0 || difficulty >= 4) difficulty = AUTOPLAY_HERO_DIFFICULTY;
    // Resolve the class id to its catalog index (the engine takes an index).
    // A hero id the pack does not define is a hard error (strict CLI): the
    // headless caller turns a false return into a non-zero exit.
    const char *hero_id = (cfg->hero_id && cfg->hero_id[0]) ? cfg->hero_id
                                                            : AUTOPLAY_HERO_CLASS;
    int pclass = -1;
    for (int i = 0; i < res->classes_count; i++)
        if (strcmp(res->classes[i].id, hero_id) == 0) { pclass = i; break; }
    if (pclass < 0) {
        fprintf(stderr, "openbounty: unknown --autoplay-hero '%s'\n", hero_id);
        resources_free(res);
        free(res);
        free(game);
        free(map);
        free(fog);
        pack_stack_pop();
        resources_republish(host_res);
        return false;
    }
    game->res = res;
    GameInitSeeded(game, AUTOPLAY_HERO_NAME, pclass, difficulty, NULL,
                   cfg->seed_index);
    // Oracle session: keep the legacy on-capture rank promotion (issue #13's
    // King-audience gating applies only to real play). The search relies on it.
    game->oracle_mode = true;
    FogInit(fog);
    bool ok = MapLoadZoneWithPlacements(map, res, res->world.starting_zone,
                                        game);
    if (ok) {
        GameApplyTileMutations(game, map, game->position.zone);
        FogReveal(fog, map, game->position.x, game->position.y,
                  res->world.fog_sight);
        // GameInit read days_left from the difficulty's pack knob.
        int start_days =
            res->time.days_per_difficulty[(int)game->character.difficulty];
        // seed= is the 0..255 catalog index, matching --seed; world= is the
        // expanded value the RNG actually ran.
        if (!ob_diag_quiet())
            printf("[AUTOPLAY] begin: seed=%d world=%llu days=%d gold=%d\n",
                   game->seed_index, (unsigned long long)game->seed,
                   game->stats.days_left, game->stats.gold);

        ExecCtx ctx = { game, map, fog, res };
        int total = 0, best_done = 0;

        // THE oracle (AP-010): one snapshot-tree search from boot. Greedy is
        // the tree's first descent (the step core's own candidate order) and
        // the keystone promotions are root-adjacent branches, so there is no
        // separate stage to run. The committed line -- winning or best-effort --
        // is the recording the search itself built; nothing is re-simulated.
        bool solved_hit = search_run(&ctx, &best_done, &total);
        int done = solved_hit ? total : best_done;
        out->cancelled = autoplay_progress_cancelled();
        if (out->cancelled) ok = false;   // host stopped the resolve
        if (total <= 0) ok = false;   // enumeration failed: no verdict
        if (recsink_truncated()) {
            // The recording dropped actions at capacity: the run happened but
            // cannot be replayed or trusted -- fail it (AP-017 setup-failure
            // semantics), never report a verdict over a broken recording.
            printf("[AUTOPLAY] recording truncated at capacity; "
                   "run not trusted\n");
            ok = false;
        }

        bool solved = (total > 0 && done == total);
        // best_done is the most any node reached; on a solve it is total.
        if (done > best_done) best_done = done;
        // The one authoritative verdict for the whole run.
        if (!ob_diag_quiet())
            printf("[VERDICT READY] %d/%d completed; verdict=%s\n",
                   best_done, total, solved ? "SOLVED" : "NOT-SOLVED");

        out->seed_index = cfg->seed_index;
        out->obj_total = total;
        out->best_done = best_done;
        out->solved = solved;
        out->days_used = start_days - game->stats.days_left;
        out->score = GameComputeScore(game);
        for (int i = 0; i < recsink()->count; i++)
            if (recsink()->prims[i].kind == REC_MOVE) out->moves++;
        if (!solved) {
            snprintf(out->unmet_label, sizeof out->unmet_label, "%s",
                     autoplay_unmet_label());
            snprintf(out->unmet_cause, sizeof out->unmet_cause, "%s",
                     autoplay_unmet_cause());
        }
    }

    pending_reset();
    spells_adventure_reset_ui();
    // The recording sink is deliberately left alive: the visible mode replays
    // it (AP-024) and --validate-pack re-inits it per run. Callers that
    // are done with it call recsink_free().
    free(fog);
    free(map);
    free(game);
    resources_free(res);
    free(res);
    pack_stack_pop();
    resources_republish(host_res);
    return ok;
}
