// Autoplay recording sink: prims carry a PRE-state fingerprint (AP-022), the
// one fingerprint function keys on the replay-relevant state, and mark /
// rollback truncate exactly (AP-030). Zero-asset.

#include "greatest.h"

#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "recording.h"

TEST recording_mark_rollback_truncates(void) {
    ASSERT(recsink_init(64));
    Game *g = calloc(1, sizeof *g);
    snprintf(g->position.zone, sizeof g->position.zone, "z");
    rec_push_move(g, 1, 0);
    rec_push_move(g, 0, 1);
    int mark = recsink_mark();
    ASSERT_EQ_FMT(2, mark, "%d");
    rec_push_action(g, RA_SEARCH, NULL, 0, 0);
    rec_push_move(g, -1, 0);
    ASSERT_EQ_FMT(4, recsink()->count, "%d");
    recsink_rollback(mark);
    ASSERT_EQ_FMT(2, recsink()->count, "%d");
    ASSERT_EQ_FMT((int)REC_MOVE, (int)recsink()->prims[1].kind, "%d");
    recsink_free();
    free(g);
    PASS();
}

TEST recording_fp_tracks_replay_state(void) {
    Game *g = calloc(1, sizeof *g);
    snprintf(g->position.zone, sizeof g->position.zone, "z");
    g->stats.gold = 100;
    uint32_t a = rec_world_fp(g);
    uint32_t b = rec_world_fp(g);
    ASSERT_EQ_FMT(a, b, "%u");        // deterministic
    g->stats.gold = 101;
    ASSERT(rec_world_fp(g) != a);     // replay-relevant state changes it
    g->stats.gold = 100;
    ASSERT_EQ_FMT(a, rec_world_fp(g), "%u");
    // Non-replay state (animation frame) must NOT change it.
    g->anim_frame = 3;
    ASSERT_EQ_FMT(a, rec_world_fp(g), "%u");
    free(g);
    PASS();
}

TEST recording_prims_stamp_pre_state(void) {
    ASSERT(recsink_init(8));
    Game *g = calloc(1, sizeof *g);
    snprintf(g->position.zone, sizeof g->position.zone, "z");
    uint32_t fp = rec_world_fp(g);
    rec_push_action(g, RA_SPEND_WEEK, NULL, 5, 0);
    ASSERT_EQ_FMT(fp, recsink()->prims[0].fp, "%u");
    ASSERT_EQ_FMT((int)RA_SPEND_WEEK, (int)recsink()->prims[0].action, "%d");
    ASSERT_EQ_FMT(5, recsink()->prims[0].a, "%d");
    recsink_free();
    free(g);
    PASS();
}

SUITE(autoplay_recording_suite) {
    RUN_TEST(recording_mark_rollback_truncates);
    RUN_TEST(recording_fp_tracks_replay_state);
    RUN_TEST(recording_prims_stamp_pre_state);
}
