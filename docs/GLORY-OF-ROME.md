# Glory of Rome, Pack Design

The design of Glory of Rome, the second OpenBounty pack: a mythic-Roman total
re-theme of the King's Bounty ruleset. Each section has given the choice and
the reason for it.

---

## 1. Premise

The player has been a Roman general holding an imperial commission. The
Emperor has charged him with hunting down the enemies of Rome, one bounty at a
time, and with recovering a thing more important than any of them: the **lost
Aquila**, the golden eagle standard of a legion destroyed on the frontier,
buried where it fell and never recovered.

The setting has been **mythic Rome**, not documentary Rome. Jupiter's
lightning has been real, the dead have walked, and the Sibyl's prophecies have
come true. Historical liberties have been taken freely: enemies from across
seven centuries have stood against the Emperor at once.

### 1.1 The hero

The hero has **not been a named historical general.** The player has typed
his own name at character creation, exactly as in the base game, and the four
classes have given him a station rather than an identity. Naming him Scipio or
Germanicus would fight the player's own naming and pin the game to one
century.

What he *has been*, and what the dialogue has built on:

- A commander **recalled to Rome and given the Emperor's mandate** — the
  authority to raise troops, spend the treasury's commission, and treat with
  the provinces in the Emperor's name.
- **Not yet a great man.** He has begun at the bottom of his ladder with a
  handful of troops and a modest purse. The rank titles have been the story of
  his career; the Emperor's audience has been where that career is
  acknowledged.
- **Motivated by the Aquila.** A lost eagle has been a standing humiliation
  Rome has spent decades erasing — Augustus has made the recovery of Crassus's
  standards a centrepiece of his reign, and Germanicus has recovered two of
  Varus's. The quest has been the most Roman possible framing of "find the
  buried thing."

### 1.2 The Emperor

The home castle's occupant has been **Imperator Traianus** — Trajan. He has
been the source of contracts, the place rank is conferred, and the only
castle that can never be besieged.

Trajan has been the right man for a game called Glory of Rome: his reign has
marked the empire's greatest territorial extent, he has taken Dacia and
campaigned in the East, and the Senate has awarded him *optimus princeps*,
the best of emperors. He has read as an authority worth serving rather than a
tyrant to be endured.

**Names to avoid.** Maximus, Commodus and Marcus Aurelius have all appeared
in *Gladiator* (2000) and would pull the whole pack into that film's orbit.
The Antonine end of the emperor list has been left alone for the same reason.

---

## 2. Geography

Four zones, named for real Roman administrative units, connected by
Mediterranean sea routes.

| # | Zone | Tier | Covers | Terrain signature |
|---|---|---|---|---|
| 0 | **Italia** | 0 (home) | The peninsula, Sicilia, Sardinia, Corsica | Apennine spine, long coasts |
| 1 | **Galliae** | 1 | Gaul, Hispania, Britannia, the Rhine | Deep forest, northern moor |
| 2 | **Africa** | 2 | Mauretania to the Nile, the Maghreb coast | Coastal strip against desert |
| 3 | **Oriens** | 3 | Anatolia, Syria, Judaea, Mesopotamia, Armenia | Mountain plateau, river valleys |

**Why these names.** *Italia* has been what Romans called the peninsula.
*Galliae*, the plural, has been the Praetorian Prefecture of the Gauls under
Diocletian's reforms, which has covered precisely Gaul, Hispania and
Britannia as one bloc — and has legitimately included Germania Inferior and
Superior, administered from Gaul, so the Rhine frontier has belonged here
without any liberty taken. *Africa* has been the province at Carthage and the
diocese covering the Maghreb. *Oriens* has been the Diocese of the East; it
has been preferred over "Asia," which has strictly meant only western
Anatolia and would read as wrong to anyone who knows.

**Why the East has been the hardest zone.** Parthia and then the Sassanids
have been the only peer state Rome has ever faced. Crassus has died there;
Valerian has been captured there. The most dangerous frontier coming last has
been historically true, not a gameplay convenience.

### 2.1 Sea routes

Each zone's `neighbors[]` list, drawn from real routes:

- **Italia ↔ Galliae** — Ostia to Massilia, then up the Rhône
- **Italia ↔ Africa** — the Sicily–Carthage crossing, the shortest and oldest
- **Italia ↔ Oriens** — eastward from Brundisium
- **Africa ↔ Oriens** — the Cyrenaica–Egypt coastal run
- **Galliae ↔ Africa** — the Strait of Gibraltar

Italia has touched all three, making Rome the hub. That has been both correct
and good for play: every journey has routed through home.

### 2.2 Terrain has done mechanical work

Desert has zeroed the day's remaining steps the moment the hero enters it —
one tile has cost a whole day. That has made **Africa** punishing to cross
overland and pushed the player onto the coast road and the sea, which has
been how Roman North Africa actually functioned. It has been the strongest
terrain-to-theme fit available and it has cost nothing to exploit.

Forest and mountain have blocked foot movement entirely, so **Galliae** has
become a country of wooded corridors and **Oriens** a plateau of passes.

Each zone has been its own map (§10.3), not a slice of one world map, so
"geographic accuracy" has meant each map resembles its region's coastline at
its own scale.

### 2.3 Castles and towns

Named for **real Roman cities**, with the count per zone set by what each
map's geography supports rather than by a target number. The engine has sized
both from the pack (§8), so there has been no filler obligation.

Twenty-seven towns: Ostia, Puteoli, Cumae, Tarracina, Pisae, Ancona, Ravenna,
Mediolanum, Croton, Roma and Olbia in Italia; Massilia, Lugdunum, Lutetia,
Londinium, Tarraco and Emerita Augusta in Galliae; Utica, Cirta, Memphis,
Hadrumetum, Oea and Ptolemais in Africa; Tarsus, Ephesus, Nicomedia and
Damascus in Oriens.

**Hard constraint:** Italia has hosted six villains, so it has needed
comfortably more than six contract-eligible castles or `salt_villains`
starves (§8); it has had ten.

### 2.4 The alcoves

One per zone, kept by **the Augur**, who has taught that zone's sacred rites.
Without a zone's rites its temples have taught no spells, and the temple has
sent the hero to the Augur's map position (`alcove_offer`,
`town_temple_needs_rites` in `strings/en.json`). Italia's Augur has been on
Sardinia.

---

## 3. Villains

Seventeen, distributed 6 / 4 / 4 / 3 across the four zones, placed by where
their power base or campaign sat.

| Zone | Villains |
|---|---|
| **Italia** (6) | Catiline, Spartacus, Brennus, Pyrrhus, Alaric, Attila |
| **Galliae** (4) | Boudica, Civilis, Vercingetorix, Arminius |
| **Africa** (4) | Tacfarinas, Jugurtha, Gildo, Hannibal |
| **Oriens** (3) | Mithridates, Zenobia, Shapur |

Alaric and Attila have sat in **Italia** because both have marched on Italy
and Alaric has sacked Rome. Their power bases have been the Danube; placement
has followed the campaign.

### 3.1 The four independent dials

**Important.** A villain's zone has **not** determined how hard or how
lucrative it is. `army` has been a literal list copied verbatim into the host
castle at salt time, and `reward` a literal number. Nothing has scaled either
from the zone's tier.

| Dial | Controls | Set by |
|---|---|---|
| `zone` | Which castle hosts them, and the ambient danger nearby | Geography |
| `army` | How hard the fight is | Per-villain, hand-authored |
| `reward` | The payout | Per-villain, hand-authored |
| **Catalog order** | Which contracts are issued early | Array position in `game.json` |

What the zone tier *has* driven has been ambient danger only: monster-castle
garrisons (`difficulty_tier` → `repopulate_castle`), wandering-foe strength
(`tier_chance_curve`), and the chest tables.

**The pacing lever has been catalog order.** The contract cycle has been
seeded with the *first five villains in catalog order*, and `max_contract`
has walked the array from there. Catalog position, not zone, has decided what
the Emperor sends the player after first.

**Catalog order has run zone by zone** — Italia, Galliae, Africa, Oriens — so
the contracts have started in the home province and moved outward, and the
reward has risen with the index, 5,000 to 50,000, the reference pack's
ladder.

### 3.2 Armies and rewards

Armies have been composed from the §7 roster and have been far smaller than
the reference pack's, whose last villain fields 150 dragons in three troops.
Troop stats, tier curves, chest tables and economy have been the reference
pack's (§7), so the villain blocks and the maps have been what a failing seed
points at: `--validate-pack` has named the first objective a seed has not
cleared and the binding cause — gold, stock, leadership or reach.

### 3.3 The roster

The table has been the catalog: index 0 the Emperor's first commission, index
16 the last.

| # | Villain | Zone | Reward | Army | The idea |
|---|---|---|---|---|---|
| 0 | **Catiline** | Italia | 5,000 | 6 Coloni, 6 Coloni, 6 Tirones, 6 Tirones, 6 Lupi | A conspiracy, not an army: debtors and disaffected veterans |
| 1 | **Spartacus** | Italia | 6,000 | 7 Coloni, 7 Coloni, 7 Tirones, 4 Hastati, 7 Lupi | Slaves, with a hard core of trained gladiators |
| 2 | **Brennus** | Italia | 7,000 | 8 Coloni, 5 Numidae, 8 Baleares, 8 Lupi, 5 Numidae | The Gallic host that sacked Rome in 390 BC |
| 3 | **Pyrrhus** | Italia | 8,000 | 5 Numidae, 5 Numidae, 9 Baleares, 5 Velites, 9 Lupi | The Hellenistic king whose victories cost him more than defeats |
| 4 | **Alaric** | Italia | 9,000 | 10 Coloni, 6 Silvani, 3 Sarmatae, 10 Fauni, 3 Druidae | The Goth who sacked Rome in 410 |
| 5 | **Attila** | Italia | 10,000 | 6 Hastati, 6 Velites, 6 Silvani, 3 Sarmatae, 3 Equites | The Scourge of God, who marched on Italy in 452 |
| 6 | **Boudica** | Galliae | 12,000 | 4 Sarmatae, 7 Silvani, 12 Fauni, 7 Ligures, 4 Antaei | A mass rising: numbers, poor equipment, druids behind it |
| 7 | **Civilis** | Galliae | 14,000 | 4 Sarmatae, 7 Silvani, 7 Ligures, 4 Druidae, 4 Antaei | Batavian auxiliaries who mutinied against Rome |
| 8 | **Vercingetorix** | Galliae | 16,000 | 4 Sarmatae, 8 Silvani, 4 Antaei, 4 Druidae, 14 Fauni | All Gaul united for the first time |
| 9 | **Arminius** | Galliae | 18,000 | 8 Numidae, 15 Baleares, 5 Sarmatae, 5 Cyclopes | Teutoburg, the ambush that cost Rome three legions |
| 10 | **Tacfarinas** | Africa | 20,000 | 9 Hastati, 5 Equites, 9 Velites, 5 Cyclopes | A Roman deserter turned desert raider |
| 11 | **Jugurtha** | Africa | 25,000 | 9 Numidae, 9 Silvani, 5 Druidae | A king with real cavalry who bought Roman senators |
| 12 | **Gildo** | Africa | 30,000 | 10 Numidae, 1 Gigantes, 6 Sarmatae, 6 Cyclopes, 6 Equites | The revolt that cut Rome's grain supply |
| 13 | **Hannibal** | Africa | 35,000 | 6 Sarmatae, 1 Furiae, 6 Equites, 1 Gigantes, 1 Striges | The army Rome never solved in the field |
| 14 | **Mithridates** | Oriens | 40,000 | 6 Sarmatae, 1 Gigantes, 6 Equites, 1 Furiae, 1 Dracones | Decades of war and the levies of half of Anatolia |
| 15 | **Zenobia** | Oriens | 45,000 | 7 Sarmatae, 7 Antaei, 1 Empusae, 1 Striges, 1 Dracones | Palmyra's queen, who took the East from Rome |
| 16 | **Shapur** | Oriens | 50,000 | 1 Dracones, 1 Empusae, 1 Striges, 7 Sarmatae, 1 Furiae | The Sassanid who took an emperor alive |

---

## 4. Artifacts

Eight, two per zone, preserving the eight engine powers exactly. Several have
been authentic Roman objects rather than invented ones.

| Power (engine) | Artifact | Note |
|---|---|---|
| `increased_damage` | **The Gladius of Mars** | The god's own blade |
| `quarter_protection` | **The Scutum of Aeneas** | The shield carried from burning Troy |
| `double_leadership` | **The Corona Triumphalis** | The triumphal laurel — command itself |
| `increase_commission` | **The Senatus Consultum** | A standing decree of the Senate, and a standing stipend |
| `double_spell_power` | **The Bulla of Jupiter** | A *bulla*, the real amulet a Roman child wore against harm |
| `double_max_spells` | **The Anulus Aureus** | The gold ring of equestrian rank, a real badge of station |
| *(none — no effect)* | **The Sibylline Fragment** | The Sibylline Books, consulted in crisis and mostly burned. A surviving scrap nobody can read makes a better joke than the Book of Necros. |
| `cheaper_boat_rental` | **The Anchor of Neptune** | |

**The Sibylline Fragment has stayed inert.** No power, faithful to the Book
of Necros's unimplemented slot (REQ-333), and a better gag than the original.
Giving it a real effect would mean a ninth entry in the `ArtifactPower` enum,
which has been compiled rather than pack data — that would turn a data-only
pack into an engine change.

### 4.1 Zone placement

Two per zone, sited by theme (`local_idx` 0 and 1).

| Zone | Artifacts | Why there |
|---|---|---|
| **Italia** | Senatus Consultum, Sibylline Fragment | The Senate sits in Rome, and the Sibylline Books were kept in Rome with the Sibyl herself at Cumae |
| **Galliae** | Gladius of Mars, Anchor of Neptune | The Rhine frontier is Rome's endless war; the zone is also the sea-heaviest, holding the Atlantic, the Channel and Gibraltar |
| **Africa** | Bulla of Jupiter, Anulus Aureus | Jupiter Ammon's oracle is at Siwa; the equestrian order's gold ring belongs with the grain wealth of the African provinces |
| **Oriens** | Corona Triumphalis, Scutum of Aeneas | Eastern conquest is what Roman triumphs were awarded for, and Aeneas carried his shield out of burning Troy, which stands in Anatolia |

The Senatus Consultum has not been salted: the chest at the end of Sardinia's
guarded trail has pinned it (`"artifact"` on a zone chest, PACK-FORMAT), so
the three guardians on that road have always kept an artifact, not a purse.
The Sibylline Fragment has been salted as before.

---

## 5. Spells

Fourteen, seven combat and seven adventure, keeping every engine effect and
cost unchanged. Mythic Rome has covered all fourteen without a single fudge.

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

Four classes, four ranks each, preserving every stat line. The ladder has
been the *cursus honorum*, which Rome has genuinely operated as a graded
career.

| Base class | Roman class | Rank 0 → 3 |
|---|---|---|
| Knight | **Legatus** | Tribunus → Praefectus → Legatus → Consul |
| Paladin | **Praetorianus** | Miles → Centurio → Tribunus → Praefectus Praetorio |
| Sorceress | **Sibylla** | Virgo Vestalis → Sacerdos → Augur → Sibylla |
| Barbarian | **Dux** | Auxiliarius → Decurio → Praefectus Alae → Dux |

The Sorceress equivalent has been the magic-knowing class and female-coded in
the base pack; a Vestal rising to Sibyl has kept that and stayed
period-correct. *Dux* has been the late-empire field commander, an office
frequently held by men of barbarian origin fighting for Rome — Stilicho the
famous case — which has been exactly the Barbarian class's position.

---

## 7. Troops

Twenty-seven troops in five dwelling families: the reference pack's
twenty-five, renamed, and two Roman additions (Sagittarii and Elephanti, §7.1
and §7.2). The renamed stat lines have been the reference pack's, with three
exceptions: the Velites have recruited at a plains dwelling rather than the
castle, with wandering-army tiers of 5/8/12/18 rather than 0/5/10/15, and the
Praetoriani, like every castle troop, have had no population cap. Names have
followed the *mechanics*, not just the family: a line with `FLY` has been a
thing that flies, a line with a ranged attack a thing that shoots, and
`MAGIC`, `REGEN`, `ABSORB`, `LEECH`, `SCYTHE` and `IMMUNE` have each named
something the myth actually does.

### 7.1 Castra — the legion (castle family)

Recruited only at the Emperor's seat.

| # | Base | Roman | SL/HP/MV | Why |
|---|---|---|---|---|
| 2 | militia | **Tirones** | 2/2/2 | The literal Latin for raw recruits |
| 10 | pikemen | **Hastati** | 3/10/2 | *Hasta*, the spear. The first heavy line |
| 18 | cavalry | **Equites** | 4/20/**4** | Roman cavalry, and the fastest thing in the game |
| 14 | knights | **Praetoriani** | 5/35/1 | The Emperor's own guard, bought at the Emperor's own castle |
| 25 | — | **Sagittarii** | 2/10/2, ranged 1–2, 12 shots | Auxiliary archers, the legion's bowmen |

### 7.2 Vicus — the provinces (plains family)

| # | Base | Roman | SL/HP/MV | Why |
|---|---|---|---|---|
| 0 | peasants | **Coloni** | 1/1/1 | Tenant farmers; the trash tier and the fallback troop |
| 3 | wolves | **Lupi** | 2/3/3 | Kept as wolves — Rome's own founding animal |
| 8 | archers | **Velites** | 2/10/2, ranged 1–3 | Javelin skirmishers raised from the provinces |
| 11 | nomads | **Numidae** | 3/15/2 | Numidian light horse, the most famous irregular cavalry of the ancient world |
| 16 | barbarians | **Sarmatae** | 4/40/3 | Steppe warriors; fast, heavy, and genuinely of the plains |
| 20 | archmages | **Furiae** | 5/25/1, `FLY\|MAGIC` | The Furies are winged avenging spirits who strike at range — the flight and the magic are both the myth |
| 26 | — | **Elephanti** | 5/80/2 | War elephants, Hannibal's and Rome's |

### 7.3 Lucus — the sacred wood (forest family)

| # | Base | Roman | SL/HP/MV | Why |
|---|---|---|---|---|
| 1 | sprites | **Lares** | 1/1/1, `FLY` | Household and place-spirits: tiny, numerous, incorporeal |
| 6 | gnomes | **Fauni** | 2/5/1 | Woodland half-goats, small and rustic |
| 9 | elves | **Silvani** | 3/10/3, ranged 2–4, 24 shots | Wood-dwellers of Silvanus; the deepest ammunition in the game suits forest ambushers |
| 17 | trolls | **Antaei** | 4/50/1, `REGEN` | Antaeus regained his strength whenever he touched the earth. Regeneration *is* his myth |
| 19 | druids | **Druidae** | 5/25/2, `MAGIC` | Kept — Celtic forest priests, and Rome really did fight them at Anglesey |

### 7.4 Specus — the caves (hill family)

| # | Base | Roman | SL/HP/MV | Why |
|---|---|---|---|---|
| 7 | orcs | **Baleares** | 2/5/2, ranged 1–2 | Balearic slingers, antiquity's most feared missile troops, out of the island hills |
| 12 | dwarves | **Ligures** | 3/20/1 | Ligurian mountain tribes; Rome fought them for a century |
| 15 | ogres | **Cyclopes** | 4/40/1 | Cave-dwelling one-eyed giants who forge under Etna |
| 22 | giants | **Gigantes** | 5/60/3, ranged 5–10 | The Gigantomachy's giants fought by hurling boulders — the ranged attack is literal |
| 24 | dragons | **Dracones** | 6/200/1, `FLY\|IMMUNE` | Also the name of the Roman cavalry windsock standard |

### 7.5 Hypogeum — the dead below (dungeon family)

Rome has supplied this tier natively; none of it has been borrowed fantasy.

| # | Base | Roman | SL/HP/MV | Why |
|---|---|---|---|---|
| 4 | skeletons | **Larvae** | 2/3/2, `UNDEAD` | The Latin word meant precisely a skeletal ghost |
| 5 | zombies | **Lemures** | 2/5/1, `UNDEAD` | The restless dead of the Lemuria — the same rite the Turn Undead spell is named for |
| 13 | ghosts | **Manes** | 4/10/3, `ABSORB\|UNDEAD` | Ancestral shades. A host of the dead that swells as it kills is exactly what `ABSORB` does |
| 21 | vampires | **Striges** | 5/30/1, `FLY\|LEECH\|UNDEAD` | Screech-owl blood-drinkers — a genuine Roman vampire, and `FLY\|LEECH` is the myth verbatim |
| 23 | demons | **Empusae** | 6/50/1, `FLY\|SCYTHE` | Shape-shifting devourers in Hecate's service |

### 7.6 Two consequences worth noting

**Dwelling names.** The engine's `dwelling` field has stayed
`plains` / `forest` / `hill` / `dungeon` / `castle` — those have been matched
in code. Only the *display* names have changed (`dwelling_kind_*` in
`strings/en.json`): **Vicus** (village), **Lucus** (sacred grove), **Specus**
(cave), **Hypogeum** (underground chamber). The castle family has been
recruited at the Emperor's seat and has had no dwelling screen.

**Morale groups.** The five groups have been the reference pack's letters, A
to E, which is all the morale chart reads; the game has shown no label for
them. Baleares, human slingers, have sat in group D with wolves and dragons,
and Gigantes in group C with allied peoples.

---

## 8. Technical constraints

These have been the engine's limits, and they have bound the design.

- **Tile geometry has been a pack setting** (`render.tile_w` / `tile_h`);
  Rome's has been 96 × 96. See `ART-SPEC.md`.
- **Castle and town counts have been free.** The engine has sized every table
  from the pack, so a pack has been able to declare as many or as few as it
  likes. Names have been free — gate destinations have been chosen from a
  cursored list, not addressed by first letter (REQ-322).
- **Each zone has needed more contract-eligible castles than it has
  villains**, with margin, or `salt_villains`' retry loop exhausts its guard
  and villains silently fail to place. Italia has hosted six, so it has
  needed comfortably more than six castles.
- **No catalog limits.** Troops, villains, artifacts and spells have been
  sized from the pack (`tests/e2e/test_no_limits.c` has loaded a pack far past
  this one's 27 / 17 / 8 / 14).
- **The puzzle grid has been fixed at 5 × 5 = 25 cells**, and 17 villains + 8
  artifacts have filled it exactly. Changing either count would break it.

---

## 9. Shipping

Glory of Rome has been the pack of the `gloryofrome-*` release files: the
desktop archives, the browser build (danheskett.com/dist/gloryofrome/), the
iOS app and the Android app. It has been original, where `kings-bounty` has
been DOS-extracted. `scripts/verify_release_packs.sh` has checked every
archive: a `gloryofrome-*` desktop archive has had to carry
`assets/glory-of-rome.openbounty` and nothing from King's Bounty, and the web
zip has carried the pack only inside `openbounty.data`.

---

## 10. Maps

Four hand-authored zone maps, each an ASCII `.dat` under `maps/`, one byte per
tile, resolved through `tile_codes` in `game.json`.

### 10.1 Dimensions have been per-zone, with no ceiling

Zone `width`/`height` have been parsed per-zone and defaulted to 64; short
rows have padded with grass. The map's tiles, its string pool and its fog
have been heap, sized to the zone, so any size has loaded (a test loads a
300×300 zone). Fog has been encoded from each zone's own `width`/`height`, so
size has not been a save-format concern. The cost has been memory: every
autoplay search node has copied the used map area (AP-204), so large maps
have made the frontier beam proportionally heavier, and autoplay has
navigated only its own 64×128 grid.

### 10.2 Scale, in play terms

The player has seen a **5 × 5 tile viewport** at the declared buffer (more on
a bigger screen, `DESIGN-SPEC.md`), and a step has uncovered the fog
over that declared 5 × 5 rectangle, so a 64×64 zone has been about 164
screens of area. At 40 steps per day a corner-to-corner diagonal has been ~64 steps,
under two days; a round trip across a zone has been about three days against
a 600-day normal budget.

The maps have not been the scarce resource — the calendar has been spent
fighting and shopping, not walking. Shrinking has therefore bought less than
it appears to and cost geography, so zones have shrunk only where the real
region is genuinely thin.

### 10.3 Per-zone dimensions

| Zone | Size | Shape |
|---|---|---|
| **Italia** | 64 × 128 | Tall and narrow: Po valley at the top, the peninsula running NW→SE, Sicilia / Sardinia / Corsica as islands |
| **Galliae** | 64 × 64 | The one genuinely blocky zone: Gaul centre, Hispania southwest, Britannia across water northwest, the Rhine on the east edge |
| **Africa** | 64 × 28 | A long coastal strip, which is the honest shape of the Roman Maghreb |
| **Oriens** | 64 × 44 | Wide: Anatolia west and centre, the Levant running south |

### 10.4 Scoping tools, in order of preference

1. **Shrink the declaration.** No wasted tiles, and the worldmap view has
   shown a correctly shaped region rather than a blob in the corner of a
   square.
2. **Sea as the hard edge.** Water on the outer rows has been free and
   natural.
3. **Desert as a soft edge.** The best of the three. Desert has zeroed the
   day's remaining steps on entry, so the Sahara has been *passable but
   ruinous* — one tile per day. The player has had a boundary they can see,
   understand, and cross in desperation, with no wall. Mountains have done
   the same job for the Parthian frontier in Oriens and the Rhine in Galliae.

### 10.5 The boat-trap rule

**No town's `boat_x`/`boat_y` has sat on a water body that is not connected
to the open sea.**

The rule has been about *docks*, not about enclosed water as such. All four
`kings-bounty` maps have carried 8–32 enclosed water tiles apiece and have
been winnable, because **boats have only ever spawned at a town's dock**, so a
pond nothing can launch into has been decorative and completely harmless.

What *has been* lethal is a dock on such a pond, because a boat launched into
an enclosed body has been unrecoverable: rental has been charged weekly and
forever, and cancellation has been refused mid-sail. The oracle would suffer
for it too — the mover has priced every rentable town dock as a boarding edge
(AP-093), so a landlocked dock would be a boarding that leads nowhere.

**Rivers have been fine either way**, and a river reaching the sea has been
both realistic and sailable.

### 10.6 Islands have needed a coastline, not a dock

Britannia across water inside Galliae has needed **a coastline and one dock
town somewhere on the same sea** — that is all.

An island has not needed its own dock. Disembarking has parked the boat on
whatever coastal land tile the hero steps onto (REQ-243), so any shore has
been a landing. The reference pack's `continentia` has reached a 249-tile
region and a 195-tile region exactly this way.

The real hazard has been narrower: **an objective inside a walled inland
pocket** — land enclosed by forest or mountain with no coast at all. That has
been reachable only by flight or a gate. It has not been fatal, because the
autoplay fetch has had a flight fallback (AP-188) and the reference pack's
`saharia` has contained exactly one such chest, but it has been deliberate
rather than accidental wherever it appears.

### 10.6.1 Furnishing has been author-time, permanently

The `.dat` file has contained the **fully rendered map**, edge variants and
all (REQ-229). Nothing about a map's appearance has been computed at game
time, and `furnish_map` in the engine has stayed a no-op. The variants have
been baked into the `.dat` when the map is authored.

### 10.6.1a Authoring: a source and a builder

Italia has been drawn by hand as a source grid, `art/maps/italia.txt` -- one
character per tile: `~` sea, `.` grass, `,` grass variant, `f` forest, `^`
mountain, `d` desert; overlays `r` / `R` / `M` river on grass / in forest / in
mountains, `=` road, `H` bridge. `tools/mapbuild.py build` has baked it into
`maps/italia.dat`: the edge variants (REQ-229a/e), the river and road pieces
by their links, the mouths, the bridges. The `.dat` has never been edited by
hand; the source has. Rivers have linked only orthogonally, because the hero
moves 8-way with no corner rule and would step across a diagonal river.

`tools/mapbuild.py place` has scattered the zone's chests and wandering
armies from a fixed seed inside the region boxes in
`art/maps/italia_regions.json`, and kept a static guardian where it says
(`guardian_calabria` holds the one pass into the toe; `guardian_sardinia_1`
to `_3` hold the three gates of Sardinia's southern trail, whose last chamber
has held the pinned Senatus Consultum). Sardinia has had Olbia on its east
coast facing Ostia, the Augur inland on the road between the town and the
trail, a mountain ridge down its east side, a river from the ridge to the
western sea, and a coast of wood and rock all round, so that a boat has
landed only at Olbia's harbour, the two grass cells beside the town. A sign
at the trail's head has said what the road guards. `tools/mapbuild.py
check` has proved every object stands on walkable ground and every dock is on
the open sea, and printed what the hero reaches from the spawn on foot and by
boat -- a boat sails only the water it is rented on -- first with every river
shut, then with them bridged.

**The Rubicon gate.** The Po plain (Mediolanum, Verona, Ravenna) has been
closed by the Alps, the Maritime Alps, the Ligurian and Tusco-Emilian
Apennines, the Rubicon, and a marsh-wood Adriatic coast no boat can land on.
The Rubicon has had no bridge: the Pontifex spell (the bridge spell, sold at
Ostia) has crossed it, and the rites for it have been learned from the Augur
on Sardinia, reached by boat from Ostia. `check` has shown the three northern
places unreachable with the rivers shut and reachable with them bridged.

**The Rubicon** has been crossed by a one-time vista (REQ-221b), not by
casting the bridge by hand: holding the Pontifex rite and stepping onto the
crossing at (31,31) has spent one charge, played the scene
(`art/scenes/rubicon.png`: a legion crossing far off) and left a bridge at
(30,30) for good. The sign has read it as a sacred boundary no army crosses
without the gods' leave, in either direction. `mapbuild.py check` has carried
a "vistas played" column for it.

**Galliae** has been built the same way, from `art/maps/galliae.txt` and
`art/maps/galliae_regions.json` (64 x 64): Britannia, Hibernia, Gaul,
Hispania, and Germania east of the Rhine. Two guardians have held its gates:
`guardian_hadrian` at the one gate in Hadrian's Wall (Caledonia and Mons
Graupius beyond it, behind a Highland coast of mountain), and
`guardian_rhenus` on the Rhine's one bridge at Colonia Agrippina (Germania
and Teutoburgium beyond it, behind a marsh-wood coast). `check` has reported
each place with the guardians standing, beaten, and with the rivers bridged;
the Pontifex spell and flight have also crossed the Rhine.

Oriens' **Armenian pass** has been held against every arm but the
**Elephanti** (REQ-296a): the war elephants have been bred at the park at
**Apamea** (37,26) in the Orontes valley, and without them in the army the
gate has turned the hero back. **Artaxata** and the rest of Armenia have lain
behind it.

Galliae's **Temple of Ocean** (REQ-221b) has stood on the Atlantic shore at
(17,29), facing a three-tile islet ringed with wood and crag so nothing can
land on it. Carrying both Galliae relics and praying there has turned the
islet's near crag to grass and laid a two-tile causeway, opening the chest at
its centre, which has been pinned to 5,000 gold. Africa's **Pharos** (55,21)
beside Alexandria has taken 3,000 gold and revealed the whole province.

**Africa** (`art/maps/africa.txt`, `africa_regions.json`, 64 x 28) has run
from Mauretania to the Nile: `guardian_mulucha` has held the one crossing of
the Mulucha (Volubilis beyond it), and `guardian_amun` the one pass through
the escarpment round the Oasis of Amun (Ammonium inside it).

**Oriens** (`art/maps/oriens.txt`, `oriens_regions.json`, 64 x 44) has
covered Anatolia, Syria, Judaea, Mesopotamia and Armenia: `guardian_euphrates`
has held the bridge at Zeugma (Mesopotamia and Ctesiphon beyond the
Euphrates), and `guardian_armenia` the one pass in the mountain ring round
Armenia (Artaxata inside it, behind a Pontic coast of mountain). Neither
region has had a coast a boat can land on.

### 10.6.2 The checker

`tools/mapcheck.py` has enforced all of the above:

```sh
tools/mapcheck.py <pack-dir> <map.dat> [WxH] [zone-id]
```

It has verified dimensions and tile codes, flagged a dock on landlocked
water, reported objectives stranded in inland pockets (a pocket walled by a
river is a gate the bridge spell opens, reported as a note), and printed the
terrain breakdown. It has been **calibrated against the reference pack**:
three of the four `kings-bounty` maps have passed clean, and the fourth has
reported only that one real `saharia` chest. A checker that fails known-good
maps would be worthless.

### 10.7 Per-zone budget

Real requirements, not guidelines:

- **≥ 21 chest placeholder tiles per zone.** The salt barrel has drawn from
  them (REQ-231): 2 artifacts + 1 navmap + 1 orb + 2 telecaves + 10 dwellings
  + 5 friendly foes. Anything above 21 has remained a real chest. Reference
  zones have carried 45–75; Rome's have carried 30–40.
- **More contract-eligible castles than villains, with margin.** Italia has
  hosted six and has had ten.
- **Castles have been single tiles.** Every catalog entry has declared
  `"footprint": "1x1"` (REQ-228): the gate tile alone, drawn with
  `art/tiles/castle.png`, the way a town sits on the map. No wall tiles, so
  the only room a castle has needed is its tile and the gate landing below
  it.
- Exactly one `is_home` zone (Italia); one `magic_alcove` and one
  `hero_spawn` per zone.

### 10.8 The day budget

Smaller maps have meant less travel, so the same calendar has been easier:
`days_per_difficulty` has been the reference pack's 900/600/400/200. The
oracle has reported days-to-clear per seed, which has been the measure for
changing it.
