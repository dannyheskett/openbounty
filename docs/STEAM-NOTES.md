# STEAM-NOTES.md — Future Directions

Forward-looking notes from a brainstorm session about Steam distribution, asset
packs, DLC, and multiplayer. Nothing here is committed; this is a capture of
the thinking so far so we can pick it up later.

---

## 1. Steam as a distribution target

### What Steam is, in this context

Steam is Valve's PC game store + launcher. Steam Direct lets anyone publish:
$100 one-time fee per game, fill out tax/banking forms, upload a build, set a
price. Valve takes 30% of sales (drops to 25% after $10M lifetime, 20% after
$50M — irrelevant at hobby scale). Monthly bank payouts. No quality
gatekeeping. Steam handles refunds (under 2 hours played, under 14 days
owned), regional pricing, sales, and tax collection.

### Steam Deck

Valve's handheld gaming PC. Linux-based (SteamOS, Arch-derived). 7-inch
screen, built-in gamepad + two trackpads, AMD APU, 16 GB RAM. Real audience
for retro / indie / DOS-reimplementation projects.

Distribution paths:
- **Native Steam release**: full store presence via Steamworks.
- **Sideload**: copy binary, add as "non-Steam game." Free, fine for personal
  or shared-with-friends use.

Steam Deck Verified program tiers: Verified / Playable / Unsupported. Verified
means "works with controller out of the box on the small screen."

### Why OpenBounty fits Steam well already

Structural alignment:

- **Single self-contained binary.** Embedded-asset release build = one exe
  with no external runtime deps. Exactly what Steam wants for clean installs.
- **JSON-driven content.** Pack model is the natural shape; no refactor
  needed to support multiple packs, just discovery and a picker.
- **Platform-correct save paths.** `$XDG_DATA_HOME` on Linux, `%APPDATA%` on
  Windows. Steam Cloud syncs whatever directory you point it at.
- **Deterministic seed + JSON saves.** Cloud sync trivial — small text files,
  no embedded paths, no opaque blobs.
- **Linux + Windows builds already cross-compile.** Deck (Linux) + Windows
  desktop are the two platforms that matter. Both shipping today.
- **No DRM, no online requirement, no account system.** Steam prefers this —
  fewer support tickets, plays offline, plays on Deck without fuss.
- **320×200 internal render scaled to window.** Scales cleanly to Deck's
  1280×800 and to 4K alike. No resolution-specific assets.
- **Recorder + MP4 encoder built in.** Trailer/store-page footage is already
  a feature, not a separate tool.
- **Headless harness + deterministic combat.** Useful for automated
  regression testing before each Steam build push.

### Gaps to close before shipping on Steam

- **Gamepad input path.** Today is keyboard-only. raylib has the API
  (`IsGamepadButtonPressed`, axis reads); just not wired up. Day or two of
  work. OpenBounty has very few commands — mapping is straightforward.
- **`--asset-dir` (or equivalent) for pack selection.** Already on the bug
  list as Bug-022.
- **Steamworks SDK integration.** Linking the SDK + DLC-ownership checks +
  Workshop directory scanning. Net-new code, vendored dependency.
- **Pack format spec document.** Schema is implicit in the loader; a written
  spec is what unlocks community packs.
- **Window/fullscreen polish for Deck.** Confirm fullscreen-by-default on
  Deck, suspend/resume behavior.
- **Title screen pack picker.** If shipping base + DLC + Workshop, the picker
  is the user-visible glue.

Nice-to-haves, not blockers:
- Steam achievements (cheap via Steamworks once integrated — "win on
  Impossible," "find all artifacts," etc.).
- Steam Cloud explicit opt-in (one config flag).
- Trading cards (Valve-approved per game; later).

### Controller mapping sketch

The keyboard surface is small and mostly toggles/one-shots. A natural pad
mapping:

- **D-pad / left stick** → 8-direction movement (the only continuous input).
- **A** → confirm / advance dialog / search current tile.
- **B** → cancel / Esc / dismiss view.
- **X / Y** → two most-used actions (Use Magic, End Week likely).
- **L/R bumpers** → cycle view screens (Army, Character, Contract, Puzzle,
  Map, Controls, Options) — or one bumper opens a radial.
- **L/R triggers** → Fly / Land, or Save+Quit modifier.
- **Start / Select** → menu / save.
- **Trackpad / right stick** → optional cursor for letter-grid UIs (gate
  spells, worldmap).

The letter-grid gate spells ("press M for Mooseweigh") become a list cursor
in pad mode. We control that UI; trivial change.

Steam Input also lets users remap on their end without us doing anything, so
even a keyboard-only build is *playable* on Deck via Valve's remapper. Native
pad mode is nicer; the floor is "it already works."

raylib already has gamepad support; the Deck exposes itself as Xbox-layout
by default. No new dependency.

---

## 2. Asset packs, DLC, and the modding model

### The three-tier content model

The engine is already pack-shaped (`Resources` loads `game.json` + assets from
a single directory). Natural extension to three content sources, all loaded
uniformly:

1. **Base game.** Engine + one bundled pack, sold once on Steam.
2. **Community packs.** Format documented publicly. Authors upload to Steam
   Workshop; players subscribe; engine scans Workshop directory. Free; zero
   ongoing cost to us.
3. **Official DLC packs.** Authored (or commissioned) by us, sold as separate
   Steam SKUs. Steamworks API tells the running game which DLC the user owns;
   the engine unlocks those packs in the picker. Steam handles payment,
   refunds, regional pricing, sales events.

### Engine work to enable this

- **Multi-source pack discovery.** Scan: install dir (base + DLC depots),
  Workshop dir, optional `--asset-dir` override. Each pack identified by a
  `pack.json` (or top-level fields in `game.json`): id, name, version, author,
  required `pack_format_version`.
- **Pack picker on title screen.** Lists all discovered packs, distinguishes
  source (base / DLC / Workshop / local), launches the chosen one.
- **Steamworks integration** for DLC entitlement + Workshop subscription
  enumeration.
- **Robust load failure handling.** Community packs will hit edge cases our
  own pack never does. Malformed JSON / missing assets should fail to load
  with a useful error, not crash.
- **Pack format versioning.** A `pack_format_version` field + documented
  compatibility policy avoids "every engine update breaks the Workshop."

### Pack format documentation

Highest-leverage artifact in this whole plan. Write it well once, ecosystem
grows without further engine work. The schema is already implicit in
`src/resources.c` and the existing `assets/kings-bounty/game.json`; the work
is making it explicit, stable, and authoring-friendly.

### Why this is a good model for OpenBounty specifically

- Existing systems carry over wholesale: troops, spells, artifacts, villains,
  zones, towns, castles, dwellings — all data-driven.
- A healthy Workshop scene drives long-tail base-game sales (cf. *Cities:
  Skylines*, *RimWorld*, *Crusader Kings*).
- DLC packs become the natural way to ship paid expansion content without
  fragmenting the codebase.

### DLC pricing context

Indie expansion DLC typically runs 25–50% of base-game price. Steam supports:
- Bundles (base + all DLC at a discount).
- Per-SKU sales (independent discount schedules).
- Free DLC (e.g. a "community spotlight" pack to keep momentum).

---

## 3. Connecting to web services

Steam doesn't restrict outbound network connections. Plenty of Steam games
talk to web services for live ops, leaderboards, daily challenges, UGC
delivery, etc.

### Steam's own infrastructure (free, optional)

- **Steam Workshop** — built-in UGC hosting + distribution. Players
  subscribe, Steam downloads into a known folder, the game reads it.
- **Steam Cloud** — save-file sync across devices.
- **DLC system** — paid expansions as separate SKUs; Steamworks API exposes
  ownership; game gates content accordingly.
- **Steamworks SDK** — C API. Workshop, DLC checks, achievements, cloud,
  friends, lobbies, networking.

### Our own web service (also fine)

HTTPS to a server we run. Hosting cost + uptime responsibility, but useful
for things Steam doesn't cover: custom accounts, cross-platform pack
delivery (itch + Steam users get the same packs), telemetry,
server-authoritative game logic.

### Hybrid pattern most indies use

- **Official paid expansions** → Steam DLC (Steam handles payment/refunds/
  regional/entitlement for free).
- **Free community packs** → Steam Workshop (free hosting + discovery).
- **Custom web service** → only when there's a specific need Steam doesn't
  cover.

For OpenBounty content packs specifically, Workshop is the natural fit. The
`Resources` loader is already pack-shaped — integration is mostly "scan the
Workshop folder and list what you find."

---

## 4. Multiplayer

### Steamworks multiplayer stack

Strong reason to be on Steam if multiplayer ships.

- **Steam Matchmaking / Lobbies.** Built-in lobby system. Public, friends-
  only, private, invite-only. Lobby metadata (settings, map, difficulty),
  built-in chat, host migration. Free. The "rooms" layer.
- **Steam Networking (SteamNetworkingSockets).** P2P or relay. Killer
  feature: **Steam Datagram Relay (SDR)** — traffic through Valve's backbone.
  No NAT/firewall config, no port forwarding, no public IPs, often lower
  latency than naive P2P.
- **Steam Friends + Rich Presence.** "Join game" buttons in friends list,
  Discord-style status ("OpenBounty — In Lobby (2/4)"), invite-via-friend.
- **Steam Voice.** Built-in voice chat. Probably overkill for turn-based.
- **Steam Remote Play Together.** One player owns the game, up to ~4 friends
  join via streamed input — they don't need to own it. Zero netcode work on
  our end. Surprisingly viable for low-input-rate turn-based games.

### OpenBounty-shaped multiplayer design

Working design from the brainstorm:

- **4 players, shared map, last-hero-standing** (and/or castle-count, and/or
  time-limited match).
- **No villains, no quests, no audience-with-king progression.** Strip the
  single-player narrative scaffolding.
- **Time exists and runs in real wall-clock**, not turn-driven.
  - 900 days × 30 ticks/day = 27,000 ticks per match.
  - Example pacing: 30 days = 2 minutes → 1 day every 4 sec, 1 tick every
    ~133ms, ~225 minutes per match. Probably want presets (faster/slower).
  - Each tick is a movement opportunity. Press a direction during the tick →
    move. Don't press → stay put. World advances regardless.
  - All time mechanics still apply: week ticks, astrology, commission,
    upkeep, dwelling refresh, enemy castle growth.
  - When `days_left` hits 0 → match ends. Winner per match's win condition.
- **Combat asymmetry**:
  - **PvP combat freezes the global clock.** Everyone watches the fight
    (forced spectator mode). When it ends, clock resumes. Combat-as-spectacle
    is intentional design — surfaces army composition, tactics, drama.
  - **PvE combat does not freeze.** Time ticks for everyone (including the
    attacker — combat duration eats their wall-clock budget). Other players
    keep playing in real time. Quick decisive PvE becomes a skill; long
    fights cost you the race.
- **Combat queue**: at most one combat resolves globally at a time. New
  triggers (PvP or PvE) queue FIFO behind the current fight.
  - Re-evaluate trigger conditions at pop time. If they no longer hold (tile
    empty, target moved, attacker died), drop the queued event with a
    notification.
  - This bounds PvE-as-safe-haven: a queued PvP attack on a player who's
    chain-fighting monsters fires immediately when their PvE resolves.

### Mechanical questions that fall out

These are open and worth playtesting before committing:

1. **Input buffering within a tick.** Buffered to next tick boundary
   (cleaner) vs immediate (snappier). Buffering is probably right —
   keeps everyone on the same tick clock.
2. **Multiple inputs in one tick.** Last-input-wins, one move per tick.
   Otherwise reflexes beat strategy.
3. **Non-movement actions** (chest, dwelling, town menu, cast spell):
   time keeps running while menus are open. Spend too long shopping → an
   opponent reaches your position. Strategic tradeoff: economic actions
   cost exploration time.
4. **What counts as PvP vs PvE for the freeze rule?**
   - Hero-on-hero: PvP, freezes.
   - Sieging another player's castle when they aren't physically there:
     leaning toward PvE-clock (the garrison, not the player, is fighting),
     but defensible either way. Means a player can lose a castle while
     distracted in their own real-time exploration.
   - Combat against a dead player's abandoned army (if respawn rule leaves
     stacks on the map): PvE-clock.
5. **PvE combat pacing.** Attacker plays combat at their own pace, world
   keeps ticking. Cost is elapsed real time. Naturally rewards quick
   decisive PvE.
6. **Spectator UX during PvP freeze.** Full-screen combat view on everyone's
   screen. Spectators can plan but can't act. Menus are also frozen during
   PvP combat (otherwise freeze becomes free shopping time).
7. **Information asymmetry during PvE.** Other players see "Player A is in
   combat" on HUD/minimap but not the fight itself. Know A is busy, don't
   know how badly A is winning. Good for tension.
8. **Disconnects.** Hero stays on map, frozen, takes no actions. Days tick
   down for them. Castles still theirs (defended by garrisons). Rejoin →
   resume. Match-end without rejoin → final score from disconnect state.
9. **AFK vs disconnect distinction.** Connected-but-idle just doesn't move.
   Maybe a soft prompt after N idle days. Their loss either way.

### What needs to change in single-player systems for multiplayer mode

- **Win condition flips** from "find scepter" to last-hero-standing, most
  castles, first to N kills, or time-limited. Match config knob.
- **No contract system, no rank-via-villains.** Replace progression driver:
  rank/leadership growth from castle ownership? gold thresholds? weeks
  survived? defeating enemy heroes? Or static rank per match-length setting.
- **Audience-with-king flow doesn't exist.** Home castle is just starting
  castle.
- **Castles flip ownership** when captured. Garrison left by loser. Need
  respawn/elimination rule for defeated heroes.
- **Fog of war becomes per-player**, not global. Currently per-tile-bit;
  becomes per-tile-bit-per-player. Mechanically small change.
- **Combat is hero-vs-hero too.** Combat engine already handles two-army
  battles; main change is the loser's hero state (death, retreat with
  reduced army, respawn at home castle after delay, etc.).
- **Adventure spells take on PvP weight.** Find Villain gone. Castle/Town
  Gate, Bridge, Instant Army still work. **Time Stop is potentially
  game-breaking** in real-time multiplayer — freezes world for everyone
  except caster. Probably needs cap/cooldown/counter mechanic. Or trust
  gold cost + spell-power scaling. Worth playtesting before nerfing.

### Engine work for multiplayer mode

- **Tick driver.** Wall-clock timer fires existing per-step / per-day /
  per-week logic on schedule. `GameOnStep` already does most of this; extract
  the "advance time and apply consequences" path from "player just pressed a
  key" and call it from the tick timer.
- **Input buffering.** Capture keypresses between ticks, apply at tick
  boundary. Small ring buffer, last-write-wins.
- **Combat freeze hook.** PvP combat starts → pause global tick. Ends →
  resume. Trivial — `bool global_paused` checked at top of tick driver.
  PvE combat doesn't pause.
- **Combat queue.** Ring buffer of `{trigger_type, attacker_id, defender_id,
  location, timestamp}`, processed FIFO at top of each tick after combat
  resolution. Re-evaluate trigger conditions at pop.
- **Per-player vs global state split.** Today `Game` is singular world +
  hero. Split into shared `World` (map, castles, towns, dwellings,
  per-player fog) and per-player `Hero` (position, army, gold, spells,
  artifacts). Not a rewrite — careful separation of fields that exist.
- **Spectator rendering.** During PvP combat, local view switches to
  rendering combat instead of own map. Combat state broadcast to spectators
  (just `Combat` struct serialized; small).
- **Match state tracking.** Win condition resolution at `days_left=0`.
  Match config (length, win condition, starting class/gold/army).
- **Strip single-player flows in multiplayer mode.** Villain placement,
  contract cycle, scepter burial, audience, ending cartoon — gated off.

### Why the existing engine is a strong foundation

- **Deterministic LCG.** Lockstep netcode is straightforward.
- **JSON-serializable state.** Reconnection = send the save; small payload.
- **Existing combat engine is turn-based.** Slots in unchanged as the
  "synchronous moment" in a real-time exploration shell.
- **Tick-based time system.** The "wall-clock drives ticks" change is a
  remap of the time driver, not a rewrite.
- **Recorder / replay.** Already there. Match replay falls out almost free.

### Netcode shape

Turn-based tick lockstep is the easy mode of multiplayer netcode:

- Each tick has a deterministic moment where all players' inputs are
  collected and applied.
- World state is derivable from the action log (already true in
  single-player via deterministic LCG + JSON saves).
- Periodically hash state, compare across clients, scream on desync.
- Reconnect = send log from cursor onward (or current JSON state — saves
  are small).

Steamworks integration estimate from the brainstorm:
- Lobby + matchmaking: ~1 week.
- Networking layer (SteamNetworkingSockets, message serialization, state
  sync, desync detection): ~2–3 weeks for turn-based deterministic.
- In-game lobby browser UI: ~1 week.

Much less if we go Remote Play Together as a first cut.

---

## 5. Open questions

Captured for future drilling, not resolved here.

- **PvP castle siege when defender isn't present.** PvE-clock or PvP-freeze?
  Leaning PvE but defensible either way.
- **Queued PvP against a player whose castle just changed hands mid-queue.**
  Re-evaluation rule covers most of this; need to pin down the exact
  predicate.
- **Time Stop balance** in multiplayer.
- **Spell rebalance / removal list** for multiplayer mode (Find Villain
  gone; others case-by-case).
- **Match-end resolution** when `days_left` hits zero — exact tiebreak
  rules per win condition.
- **Calendar / week-tick handling** if we ever revisit per-player clocks
  (rejected in this brainstorm in favor of a single global clock, but
  worth re-examining).
- **Multiplayer-specific maps.** Single-player maps tuned for one hero on
  64×64 with specific villain/artifact placements. 4-player maps need
  symmetry (or fairness), separated starts, contested neutral mid-map
  castles, terrain choke points. Likely 2–3 hand-authored shipped with
  base game; community/DLC adds more. Plays into the asset-pack model —
  multiplayer maps are just another pack type.
- **AI playtester / balance harness.** `combat_run_headless` + auto-player
  toggle is the bones of automated balance testing. Could grow into a
  nightly job reporting win-rates per class/difficulty. Useful both
  single-player and for tuning multiplayer.

---

## 6. Rough sequencing if we ever pursue this

Not a commitment, just an order that makes sense:

1. **Gamepad input + `--asset-dir` flag.** Small, unblocks Deck testing
   and pack work.
2. **Pack format spec written down.** Highest-leverage doc artifact.
3. **Pack picker UI + multi-source discovery.** Foundation for DLC and
   Workshop both.
4. **Steamworks SDK integration** (DLC entitlement + Workshop scan).
5. **First original asset pack authored** (the sellable base-game content).
6. **Steam store page + Steam Direct submission.**
7. **Multiplayer mode** (separate, larger track — only after the single-
   player product is shipping).

Steps 1–6 plausibly land a paid base-game release. Multiplayer is a
deliberate later expansion, not part of the v1 ship.
