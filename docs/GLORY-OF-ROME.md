# Glory of Rome, Pack Design

**Status:** design in progress. This is the plan for a second OpenBounty pack,
a mythic-Roman total re-theme of the King's Bounty ruleset. It records
decisions made and flags what is still open.

Sections marked **DECIDED** are settled. Sections marked **PROPOSED** are a
first pass that needs a review before anyone commissions art or writes JSON.

---

## 1. Premise

**DECIDED.** The player is a Roman general holding an imperial commission. The
Emperor has charged him with hunting down the enemies of Rome, one bounty at a
time, and with recovering a thing more important than any of them: the **lost
Aquila**, the golden eagle standard of a legion destroyed on the frontier,
buried where it fell and never recovered.

The setting is **mythic Rome**, not documentary Rome. Jupiter's lightning is
real, the dead walk, and the Sibyl's prophecies are true. Historical liberties
are taken freely: enemies from across seven centuries stand against the
Emperor at once.

### 1.1 The hero

**DECIDED.** The hero is **not a named historical general.** The player types
his own name at character creation, exactly as in the base game, and the four
classes give him a station rather than an identity. Naming him Scipio or
Germanicus would fight the player's own naming and pin the game to one
century.

What he *is*, and what the dialogue should build on:

- A commander **recalled to Rome and given the Emperor's mandate** — the
  authority to raise troops, spend the treasury's commission, and treat with
  the provinces in the Emperor's name.
- **Not yet a great man.** He begins at the bottom of his ladder with a
  handful of troops and a modest purse. The rank titles are the story of his
  career; the Emperor's audience is where that career is acknowledged.
- **Motivated by the Aquila.** A lost eagle was a standing humiliation Rome
  would spend decades to erase — Augustus made the recovery of Crassus's
  standards a centrepiece of his reign, and Germanicus recovered two of
  Varus's. The quest is the most Roman possible framing of "find the buried
  thing."

### 1.2 The Emperor

**DECIDED.** The home castle's occupant is **Imperator Traianus** — Trajan.
He is the source of contracts, the place rank is conferred, and the only
castle that can never be besieged.

Trajan is the right man for a game called Glory of Rome: his reign was the
empire's greatest territorial extent, he took Dacia and campaigned in the
East, and the Senate awarded him *optimus princeps*, the best of emperors. He
reads as an authority worth serving rather than a tyrant to be endured.

**Names to avoid.** Maximus, Commodus and Marcus Aurelius all appear in
*Gladiator* (2000) and will pull the whole pack into that film's orbit. The
Antonine end of the emperor list is best left alone for the same reason.

---

## 2. Geography

**DECIDED.** Four zones, named for real Roman administrative units, connected
by Mediterranean sea routes.

| # | Zone | Tier | Covers | Terrain signature |
|---|---|---|---|---|
| 0 | **Italia** | 0 (home) | The peninsula, Sicilia, Sardinia, Corsica | Apennine spine, long coasts |
| 1 | **Galliae** | 1 | Gaul, Hispania, Britannia, the Rhine | Deep forest, northern moor |
| 2 | **Africa** | 2 | Mauretania to Cyrenaica, the Maghreb coast | Coastal strip against desert |
| 3 | **Oriens** | 3 | Anatolia, Syria, Judaea, Aegyptus | Mountain plateau, river valleys |

**Why these names.** *Italia* is what Romans called the peninsula. *Galliae*,
the plural, is the Praetorian Prefecture of the Gauls under Diocletian's
reforms, which covered precisely Gaul, Hispania and Britannia as one bloc —
and legitimately includes Germania Inferior and Superior, administered from
Gaul, so the Rhine frontier belongs here without any liberty taken. *Africa*
is the province at Carthage and the diocese covering the Maghreb. *Oriens* is
the Diocese of the East; it is preferred over "Asia," which strictly meant
only western Anatolia and would read as wrong to anyone who knows.

**Why the East is the hardest zone.** Parthia and then the Sassanids were the
only peer state Rome ever faced. Crassus died there; Valerian was captured
there. The most dangerous frontier being last is historically true, not a
gameplay convenience.

### 2.1 Sea routes

**DECIDED.** Each zone's `neighbors[]` list, drawn from real routes:

- **Italia ↔ Galliae** — Ostia to Massilia, then up the Rhône
- **Italia ↔ Africa** — the Sicily–Carthage crossing, the shortest and oldest
- **Italia ↔ Oriens** — eastward from Brundisium
- **Africa ↔ Oriens** — the Cyrenaica–Egypt coastal run
- **Galliae ↔ Africa** — the Strait of Gibraltar

Italia touches all three, making Rome the hub. That is both correct and good
for play: every journey routes through home.

### 2.2 Terrain does mechanical work

**DECIDED.** Desert zeroes the day's remaining steps the moment the hero
enters it — one tile costs a whole day. That makes **Africa** punishing to
cross overland and pushes the player onto the coast road and the sea, which
is how Roman North Africa actually functioned. It is the strongest
terrain-to-theme fit available and it costs nothing to exploit.

Forest and mountain block foot movement entirely, so **Galliae** becomes a
country of wooded corridors and **Oriens** a plateau of passes.

Each zone is its own 64×64 map, not a slice of one world map, so "geographic
accuracy" means each map resembles its region's coastline at its own scale.

### 2.3 Castles and towns

**DECIDED.** Named for **real Roman cities**, with the count per zone set by
what each map's geography actually supports rather than by a target number.
The engine caps both at 26 but requires neither (§8), so there is no filler
obligation.

Working sources: Roma, Ostia, Neapolis, Ravenna and Mediolanum in Italia;
Massilia, Lugdunum, Londinium, Corduba and Colonia Agrippina in Galliae;
Carthago, Leptis Magna, Cyrene, Alexandria and Lambaesis in Africa;
Antiochia, Ephesus, Palmyra, Byzantium and Caesarea in Oriens.

**Hard constraint:** Italia hosts six villains, so it needs comfortably more
than six contract-eligible castles or `salt_villains` starves (§8). That sets
the floor for the home map's density before any aesthetic consideration.

### 2.4 The alcoves

**DECIDED.** One per zone — where a non-priest class buys the knowledge of
magic. Mythic Rome supplies real oracles:

| Zone | Site |
|---|---|
| Italia | The **Sibyl of Cumae**, in her cave near Neapolis |
| Galliae | The **sacred grove of the Carnutes**, the druids' centre in Caesar's account |
| Africa | The **Oracle of Ammon at Siwa**, which Alexander consulted |
| Oriens | **Didyma**, the oracle of Apollo near Miletus |

---

## 3. Villains

**DECIDED.** Seventeen, distributed 6 / 4 / 4 / 3 across the four zones, placed
by where their power base or campaign sat.

| Zone | Villains |
|---|---|
| **Italia** (6) | Brennus, Spartacus, Catiline, Pyrrhus, Alaric, Attila |
| **Galliae** (4) | Vercingetorix, Boudica, Arminius, Civilis |
| **Africa** (4) | Hannibal, Jugurtha, Tacfarinas, Gildo |
| **Oriens** (3) | Mithridates, Zenobia, Shapur |

Alaric and Attila sit in **Italia** because both marched on Italy and Alaric
sacked Rome. Their power bases were the Danube, but placement follows the
campaign here, and — see below — it costs nothing in difficulty.

### 3.1 The four independent dials

**DECIDED, and important.** A villain's zone does **not** determine how hard
or how lucrative it is. Verified against `game.json`: `army` is a literal
five-stack list copied verbatim into the host castle at salt time, and
`reward` is a literal number. Nothing scales either from the zone's tier.

| Dial | Controls | Set by |
|---|---|---|
| `zone` | Which castle hosts them, and the ambient danger nearby | Geography |
| `army` | How hard the fight is | Per-villain, hand-authored |
| `reward` | The payout | Per-villain, hand-authored |
| **Catalog order** | Which contracts are issued early | Array position in `game.json` |

What the zone tier *does* drive is ambient danger only: monster-castle
garrisons (`difficulty_tier` → `repopulate_castle`), wandering-foe strength
(`tier_chance_curve`), and the chest tables.

So Alaric and Attila can hold fortresses in the gentle home province with apex
armies and top rewards. That is a good shape: the player walks past a threat
he cannot touch for most of the game.

**The pacing lever is catalog order, and it is easy to miss.** The contract
cycle is seeded with the *first five villains in catalog order*, and
`max_contract` walks the array from there. Catalog position, not zone and not
difficulty, decides what the Emperor sends the player after first. Put Alaric
at index 0 and the opening commission is a fight that cannot be won for two
hundred days.

**Rule: catalog order tracks the intended difficulty ramp, decoupled entirely
from geography.** Brennus and Spartacus early; Alaric and Attila last. The
concrete ordering falls out of the authored armies (§3.2) rather than being
chosen separately.

### 3.2 Armies and rewards

**DECIDED: both are re-authored, not inherited.** Each villain's five stacks
are composed from thematically right troops — Boudica fields Celts, Shapur
fields cataphracts, Hannibal fields elephants — and the reward ladder is set
to match. This is the one place the design deliberately gives up the
frozen-numbers safety net, because a villain fielding the wrong troops is the
most visible possible failure of a re-theme.

**How winnability is kept, given that:** author for flavour first, then run
`--validate-pack 0 255` and fix what the oracle reports. A failing seed names
the first objective it could not clear and the binding cause — gold, stock,
leadership or reach — so a too-hard villain is diagnosable rather than
mysterious. Expect to iterate. Everything *else* stays frozen (troop stats,
tier curves, chest tables, economy), so the oracle's report isolates the
villain blocks and the maps as the only things that can be wrong.

---

## 4. Artifacts

**DECIDED.** Eight, two per zone, preserving the eight engine powers exactly.
Several are authentic Roman objects rather than invented ones.

| Power (engine) | Artifact | Note |
|---|---|---|
| `increased_damage` | **The Gladius of Mars** | The god's own blade |
| `quarter_protection` | **The Scutum of Aeneas** | The shield carried from burning Troy |
| `double_leadership` | **The Corona Triumphalis** | The triumphal laurel — command itself |
| `increase_commission` | **The Senatus Consultum** | A standing decree of the Senate, and a standing stipend |
| `double_spell_power` | **The Bulla of Jupiter** | A *bulla* is the real amulet a Roman child wore against harm |
| `double_max_spells` | **The Anulus Aureus** | The gold ring of equestrian rank, a real badge of station |
| *(none — no effect)* | **The Sibylline Fragment** | The Sibylline Books were consulted in crisis and mostly burned. A surviving scrap nobody can read is a better joke than the Book of Necros ever was. |
| `cheaper_boat_rental` | **The Anchor of Neptune** | |

**DECIDED: the Sibylline Fragment stays inert.** No power, faithful to the
Book of Necros's unimplemented slot (REQ-333), and a better gag than the
original. Giving it a real effect would mean a ninth entry in the
`ArtifactPower` enum, which is compiled rather than pack data — that would
turn a data-only pack into an engine change.

### 4.1 Zone placement

**DECIDED.** Two per zone, sited by theme (`local_idx` 0 and 1).

| Zone | Artifacts | Why there |
|---|---|---|
| **Italia** | Senatus Consultum, Sibylline Fragment | The Senate sits in Rome, and the Sibylline Books were kept in Rome with the Sibyl herself at Cumae |
| **Galliae** | Gladius of Mars, Anchor of Neptune | The Rhine frontier is Rome's endless war; the zone is also the sea-heaviest, holding the Atlantic, the Channel and Gibraltar |
| **Africa** | Bulla of Jupiter, Anulus Aureus | Jupiter Ammon's oracle is at Siwa; the equestrian order's gold ring belongs with the grain wealth of the African provinces |
| **Oriens** | Corona Triumphalis, Scutum of Aeneas | Eastern conquest is what Roman triumphs were awarded for, and Aeneas carried his shield out of burning Troy, which stands in Anatolia |

---

## 5. Spells

**PROPOSED.** Fourteen, seven combat and seven adventure, keeping every
engine effect and cost unchanged. Mythic Rome covers all fourteen without a
single fudge.

**Combat (7)**

| Engine effect | Name | Note |
|---|---|---|
| clone | **Simulacrum** | A conjured double |
| teleport | **Mercury's Passage** | |
| fireball | **Vulcan's Fire** | |
| lightning | **Fulmen** | Jupiter's bolt — *the* Roman divine intervention |
| freeze | **Medusa's Gaze** | Petrification reads better than ice |
| resurrect | **Rite of Aesculapius** | |
| turn_undead | **Rite of the Lemuria** | The real May festival at which Romans expelled the restless dead from their houses |

**Adventure (7)**

| Engine effect | Name | Note |
|---|---|---|
| bridge | **Pontifex** | *Pontifex* literally means "bridge-builder." The priestly title and the spell are the same word. |
| time_stop | **Iter Magnum** | The forced march |
| find_villain | **Augury** | |
| castle_gate | **Cursus Publicus** | The imperial relay network |
| town_gate | **Via** | The road |
| instant_army | **Dilectus** | The levy |
| raise_control | **Imperium** | The legal authority to command — exactly what leadership is |

---

## 6. Classes and ranks

**PROPOSED.** Four classes, four ranks each, preserving every stat line. The
ladder is the *cursus honorum*, which Rome genuinely operated as a graded
career.

| Base class | Roman class | Rank 0 → 3 |
|---|---|---|
| Knight | **Legatus** | Tribunus → Praefectus → Legatus → Consul |
| Paladin | **Praetorianus** | Miles → Centurio → Tribunus → Praefectus Praetorio |
| Sorceress | **Sibylla** | Virgo Vestalis → Sacerdos → Augur → Sibylla |
| Barbarian | **Dux** | Auxiliarius → Decurio → Praefectus Alae → Dux |

The Sorceress equivalent is the magic-knowing class and is female-coded in the
base pack; a Vestal rising to Sibyl keeps that and stays period-correct.
*Dux* is the late-empire field commander, an office frequently held by men of
barbarian origin fighting for Rome — Stilicho being the famous case — which
is exactly the Barbarian class's position.

---

## 7. Troops

**PROPOSED — structure decided, names need a pass.** Twenty-five troops in
five dwelling families of five, stats unchanged from the reference pack.

| Family | Concept |
|---|---|
| **Castle** | Rome's own, recruited only at the Emperor's seat. The manipular legion was already five graded lines by age and experience, so this is a one-to-one correspondence: skirmisher, spearman, veteran, cavalry, guard. Velites, Hastati, Principes, Triarii, Equites and Praetoriani are all available; pick five to fit the five stat lines. |
| **Plains** | Auxiliary and allied horse — Numidian light cavalry, Sarmatian and Parthian cataphracts — topped by something winged and magical. |
| **Forest** | Celtic and Germanic warriors, plus the woodland powers: fauns, dryads, the followers of Silvanus. |
| **Hill** | Mountain tribes, war elephants, and the giants of myth — cyclopes and gigantes at the top. |
| **Dungeon** | The chthonic tier, and Rome supplies it natively: *lemures* and *larvae* (malevolent dead), *manes* (ancestral shades), *striges* — screech-owl blood-drinkers, a genuine Roman vampire tradition rather than a borrowed one — *empusae*, and a hellhound at the apex. |

The five stat lines per family are fixed; only the names and art change.

**DECIDED: the full 25-name mapping is drafted in one pass and reviewed as a
table.** Each name has to fit its stat line's mechanics, not just its family —
a flier must be a thing that flies, a ranged line must be a thing that shoots,
and the dungeon family must read as undead for Turn Undead and morale group E
to make sense. That draft is the next deliverable on this document.

---

## 8. Technical constraints

**Verified against the engine.** These bound the design.

- **Tile and sprite geometry is 48 × 34**, compiled into the renderer, not a
  pack setting. See `ART-SPEC.md`.
- **Castle and town counts are ceilings, not requirements.** `GAME_CASTLES`
  and `GAME_TOWNS` are 26; every consumer loops to `min(res->count, cap)`, so
  a pack may declare fewer. Names are free — gate destinations are chosen from
  a cursored list, not addressed by first letter (REQ-322).
- **Each zone needs more contract-eligible castles than it has villains**, with
  margin, or `salt_villains`' retry loop exhausts its guard and villains
  silently fail to place. Italia hosts six, so it needs comfortably more than
  six castles.
- **Catalog headroom exists but is not needed:** 32 troops, 32 villains, 16
  artifacts, 32 spells against this design's 25 / 17 / 8 / 14.
- **The puzzle grid is fixed at 5 × 5 = 25 cells**, and 17 villains + 8
  artifacts fills it exactly. Changing either count breaks it.

---

## 9. Build order

**PROPOSED.**

1. **Freeze everything except the villain blocks.** Copy the reference pack's
   `game.json` wholesale and change ids, display names, art paths, zone names,
   and the villain roster. Troop stat lines, tier curves, chest tables and
   economy values stay numerically identical. Villain armies and rewards are
   authored fresh (§3.2) — deliberately the one exception, and therefore the
   one thing a validation failure can be attributed to alongside the maps.
2. **Author the four maps.** Four hand-drawn 64×64 ASCII `.dat` files, plus
   the zone object lists (towns, castles, signs, chests, dwellings, armies).
   This is the real design work.
3. **Validate.** `--validate-pack 0 255` runs the oracle over all 256 catalog
   worlds and reports whether the pack is winnable per seed. Frozen numbers
   plus a passing sweep means the maps are sound.
4. **Commission art** against `ART-SPEC.md` once the roster is final, since
   the troop and villain lists determine the file list.
5. **Re-tune afterward, one axis at a time**, re-validating each time.

An all-original pack is also the first one that could legally **ship inside
the release archives** — the current packs are excluded because the
DOS-extracted assets are copyright-restricted. That changes the pack-leak
check in the release workflow, deliberately rather than accidentally.

---

## 10. Next deliverables

The six decisions previously open here are now settled and folded into the
sections above. What remains is work, not choices:

1. **The 25-troop name table** (§7) — the immediate next piece.
2. **The villain armies and rewards** (§3.2), which also fixes catalog order.
3. **The four maps** and their settlement placements (§2.3), the largest job.
4. **Validation** — `--validate-pack 0 255`, then iterate on whatever the
   oracle reports (§3.2).
5. **Art commission** against `ART-SPEC.md`, once the troop and villain
   rosters are final and the file list is therefore known.
