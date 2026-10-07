# Glossary

The words the code and the docs have used, in alphabetical order.

| Term | Meaning |
|---|---|
| **AP-NNN** | A requirement in `AUTOPLAY-SPECS.md`. |
| **Autoplay** | The winnability oracle (`autoplay/`): a search that has played a world to its end and reported SOLVED or NOT-SOLVED. `--autoplay`, `--validate-pack`. |
| **Catalog world** | One of a pack's 256 worlds, picked by index `0`–`255` (`--seed N`); the index has expanded to the full seed (`GameSeedFromIndex`). |
| **Combat target** | What a fight has been against: a foe's or a castle's garrison, its name and the identity that has seeded the fight's dice (`CombatTarget`, built by `CombatTargetForFoe` and `CombatTargetForCastle`). |
| **Contract** | The villain the hero has been commissioned to catch. A villain caught under contract has paid its bounty and uncovered its piece of the puzzle map. |
| **Demo** | The human-like player (`demo/`, `DEMO-SPEC.md`): it has played one committed timeline under a player's limits. `--demo`. |
| **DM-NNN** | A requirement in `DEMO-SPEC.md`. |
| **DSGN-NNNN** | A requirement in `DESIGN-SPEC.md`, the modern presentation. |
| **Engine** | `engine/`, built as `libobengine.a`: the game's state and rules, with no rendering, audio or input. |
| **Flow** | A decision the engine has raised and waited on (attack or evade, recruit how many, …), named by `PendingFlow` (`engine/include/pending.h`) and applied by `engine/flow_resolve.c`. |
| **Foe** | An army on the map (`FoeState`): hostile (a fight) or friendly (a recruit offer). |
| **Gallery** | `--gallery <dir>`: every screen drawn to a PNG, each one checked for its taps. |
| **Garrison** | The troops holding a castle, or a foe's own army. |
| **Guardian** | A zone army with `static`: it has never moved, and with `combat.guardian_full_band` it has fielded all five stacks. |
| **Legacy** | The presentation that has reproduced King's Bounty's DOS screens (`render.mode` `legacy`); finished and pinned by `test_legacy_freeze`. |
| **Leadership** | How many hit points of troops the hero has been able to control; it has capped recruiting and decided which stacks have stayed under control. |
| **Modern** | The Glory of Rome's presentation (`render.mode` `modern`): larger tiles, a rail, touch and pages. `CL_IS_MODERN` in the shell. |
| **Pack** | A game's content: `game.json`, strings, art, audio and maps, as a directory or a `.openbounty` zip (`PACK-FORMAT.md`). |
| **Pack stack** | The open packs, read top-down: a file in a higher pack has won (`pack_stack_read`). |
| **Placement** | An object the salt has put on a map: a chest, an artifact, a dwelling, a friendly foe (`SaltedPlacement`). |
| **Player-IO queue** | The engine's FIFO of everything the player must see or answer (`engine/include/player_io.h`), drained alike by the shell, the demo and autoplay. |
| **REQ-NNN** | A requirement in `OPENBOUNTY-SPEC.md`. |
| **Rites** | In The Glory of Rome, what a zone's Augur has taught; without them that zone's temples have taught no spells (`magic.rites_per_zone`). |
| **Salt** | The seeded scattering of objects over a zone (`salt_continent`), set by the zone's `salt` keys. |
| **Scepter** | The object buried in one zone. The puzzle map has shown where, and searching its tile has won the game. |
| **Shell** | `src/`: the window, rendering, audio, input and screens around the engine. |
| **Siege** | A fight against a castle's garrison (`COMBAT_MODE_CASTLE`). |
| **Week end** | The turn of the week: commission, upkeep, the astrology's creature and, in The Glory of Rome, a renewed spell. |
| **Zone** | One map of the world: a *continent* in King's Bounty, a *province* in The Glory of Rome. |
