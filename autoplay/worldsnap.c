// autoplay/worldsnap.c — see worldsnap.h.
//
// The whole primitive is four value copies in each direction. Game and Map are
// flat structs with no owned heap (Game's only pointer is the read-only shared
// `res`; Map's tiles are an inline fixed-size array), so `*dst = *src` is a complete
// deep copy and restore is bit-identical to capture. The world RNG is a single
// uint64_t global behind GameRngSnapshot/GameRngRestore.

#include "worldsnap.h"

#include <stddef.h>   // size_t
#include "player_io.h"   // player_io_reset (normalize the transient I/O queue)
#include "pending.h"     // pending_reset (clear stale flow state on restore)

void worldsnap_capture(WorldSnapshot *out, const Game *g, const Map *m,
                       const Fog *fog) {
    out->game = *g;
    out->map  = *m;
    out->fog  = *fog;
    out->rng  = GameRngSnapshot();
}

void worldsnap_restore(const WorldSnapshot *snap, Game *g, Map *m, Fog *fog) {
    *g = snap->game;
    *m = snap->map;
    *fog = snap->fog;
    GameRngRestore(snap->rng);
    // The pending_* globals are process globals not stored in Game/Map/Fog.
    // Any navigation that consumed a chest mid-route (or stepped onto any
    // interactive tile) and then failed before answering the flow leaves
    // pending_flow dirty. Since every worldsnap_capture is taken when
    // pending_flow == FLOW_NONE (at the start of a planning attempt, after
    // all previous flows were resolved), restoring always means returning to
    // that clean state.
    pending_reset();
}

static uint64_t fnv_bytes(uint64_t h, const void *buf, size_t n) {
    const unsigned char *p = (const unsigned char *)buf;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

static uint64_t fnv_world(const Game *g, const Map *m, uint64_t rng) {
    uint64_t h = 1469598103934665603ULL;
    // Hash a NORMALIZED copy of Game: zero the fields that are NOT game-progress
    // state but legitimately differ between two independent runs at the same
    // logical point —
    //   - `res`: a boot-specific shared read-only heap ADDRESS, never owned;
    //   - `player_io`: the transient I/O request queue (decisions/messages/views
    //     in flight). Headless drains it inside autoplay_step; the visible shell
    //     drains it via its own per-frame pumps, so its momentary contents differ
    //     between modes even though the game progresses identically. It is
    //     plumbing, not progress.
    // Everything else (position, economy, army, world flags, consumed, castles,
    // contracts, foes, ...) is hashed, so the checkpoint still detects any real
    // divergence.
    Game tmp = *g;
    tmp.res = NULL;
    player_io_reset(&tmp);   // zero the transient I/O queue (operates on Game)
    // Also zero the COSMETIC display fields the shell mutates every frame but the
    // headless runner never touches (hero sprite anim, HUD toggle) — they are not
    // game progress and would diverge the boundary check between modes.
    tmp.anim_frame = 0;
    tmp.hud_visible = false;
    h = fnv_bytes(h, &tmp, sizeof tmp);
    h = fnv_bytes(h, m, sizeof *m);
    h = fnv_bytes(h, &rng, sizeof rng);
    return h;
}

uint64_t worldsnap_fingerprint(const Game *g, const Map *m) {
    // FNV-1a over the full LIVE world byte image (Game + Map) + the live RNG — the
    // same bytes worldsnap_capture copies, so this detects ANY divergence and,
    // because the world is provably deterministic across restore, never fires
    // spuriously. Used as the executor's step-boundary checkpoint (WS-4).
    return fnv_world(g, m, GameRngSnapshot());
}
