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

### 3.3 The roster

**DRAFTED — needs review.** Catalog order is the difficulty ramp, so the table
below *is* the catalog: index 0 is the Emperor's first commission, index 16
the last. Zone is independent of it, which is why Italia appears at both ends.

Armies are composed from the §7 roster. The reward ladder is the reference
pack's, unchanged: 5,000 rising to 50,000.

| # | Villain | Zone | Reward | Army (5 stacks) | The idea |
|---|---|---|---|---|---|
| 0 | **Catiline** | Italia | 5,000 | 60 Coloni, 40 Coloni, 30 Tirones, 20 Tirones, 15 Lupi | A conspiracy, not an army: debtors and disaffected veterans |
| 1 | **Spartacus** | Italia | 6,000 | 80 Coloni, 50 Coloni, 40 Tirones, 25 Hastati, 20 Lupi | Slaves, with a hard core of trained gladiators |
| 2 | **Tacfarinas** | Africa | 7,000 | 60 Coloni, 40 Numidae, 30 Baleares, 30 Lupi, 20 Numidae | A Roman deserter turned desert raider; all skirmish, no line |
| 3 | **Jugurtha** | Africa | 8,000 | 50 Numidae, 40 Numidae, 40 Baleares, 30 Velites, 25 Lupi | A king with real cavalry and Roman-trained troops he bought |
| 4 | **Boudica** | Galliae | 9,000 | 100 Coloni, 60 Silvani, 40 Sarmatae, 30 Fauni, 20 Druidae | A mass rising: enormous numbers, poor equipment, druids behind it |
| 5 | **Civilis** | Galliae | 10,000 | 60 Hastati, 50 Velites, 50 Silvani, 40 Sarmatae, 25 Equites | Batavian auxiliaries who mutinied — he fights Rome *with Rome's own drill* |
| 6 | **Brennus** | Italia | 12,000 | 70 Sarmatae, 60 Silvani, 50 Fauni, 40 Ligures, 20 Antaei | The Gallic host that sacked Rome in 390 BC |
| 7 | **Vercingetorix** | Galliae | 14,000 | 80 Sarmatae, 60 Silvani, 50 Ligures, 30 Druidae, 25 Antaei | All Gaul united for the first time |
| 8 | **Arminius** | Galliae | 16,000 | 90 Sarmatae, 70 Silvani, 50 Antaei, 40 Druidae, 30 Fauni | Teutoburg — forest-heavy, because the forest was the weapon |
| 9 | **Gildo** | Africa | 18,000 | 140 Numidae, 80 Baleares, 70 Sarmatae, 60 Cyclopes, 40 Furiae | The revolt that cut Rome's grain supply |
| 10 | **Pyrrhus** | Italia | 20,000 | 120 Hastati, 80 Equites, 70 Gigantes, 60 Velites, 40 Cyclopes | A Hellenistic professional army, elephants at its centre |
| 11 | **Mithridates** | Oriens | 25,000 | 150 Numidae, 100 Silvani, 80 Furiae, 70 Gigantes, 50 Druidae | Decades of war and the levies of half of Anatolia |
| 12 | **Hannibal** | Africa | 30,000 | 100 Numidae, 80 Gigantes, 70 Sarmatae, 50 Cyclopes, 40 Equites | Elephants and Numidian horse — the army Rome never solved |
| 13 | **Zenobia** | Oriens | 35,000 | 150 Sarmatae, 90 Furiae, 70 Equites, 60 Gigantes, 40 Striges | Palmyra's cataphracts, and something worse out of the desert |
| 14 | **Shapur** | Oriens | 40,000 | 200 Sarmatae, 100 Gigantes, 80 Equites, 60 Furiae, 30 Dracones | The Sassanid who took an emperor alive |
| 15 | **Alaric** | Italia | 45,000 | 200 Sarmatae, 120 Antaei, 100 Empusae, 80 Striges, 40 Dracones | The sack of Rome, and the dead the sack left behind |
| 16 | **Attila** | Italia | 50,000 | 150 Dracones, 120 Empusae, 100 Striges, 100 Sarmatae, 80 Furiae | The Scourge of God |

**The ramp.** Summed hit-point worth rises monotonically across the seventeen,
from about 245 at Catiline to about 45,000 at Attila — the same shape as the
reference pack, whose final villain fields 150 dragons. Hit-point worth is a
rough proxy and not the same as difficulty (abilities, morale and ranged
attacks all move the real answer), which is exactly why the oracle sweep
decides and this table only proposes.

**Two deliberate shapes.** Civilis fields Roman troops because the Batavian
revolt was mutinying auxiliaries — the player meets his own army's drill from
the wrong side, once, early. And Alaric and Attila stand at the top of the
ladder while sitting in the tier-0 home province, so the player walks past
them from the opening hour.

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

**DRAFTED — needs review.** Twenty-five troops in five dwelling families of
five. Every stat line is unchanged from the reference pack; only the name and
the art move. Names were chosen against the *mechanics*, not just the family:
a line with `FLY` is a thing that flies, a line with a ranged attack is a
thing that shoots, and `MAGIC`, `REGEN`, `ABSORB`, `LEECH`, `SCYTHE` and
`IMMUNE` each name something the myth actually does.

### 7.1 Castra — the legion (castle family)

Recruited only at the Emperor's seat. The manipular legion was already five
graded lines, so this is a direct correspondence.

| # | Base | Roman | SL/HP/MV | Why |
|---|---|---|---|---|
| 2 | militia | **Tirones** | 2/2/2 | The literal Latin for raw recruits |
| 8 | archers | **Velites** | 2/10/2, ranged 1–3 | The legion's own javelin skirmishers — Rome's ranged line, not an auxiliary import |
| 10 | pikemen | **Hastati** | 3/10/2 | *Hasta*, the spear. The first heavy line |
| 18 | cavalry | **Equites** | 4/20/**4** | Roman cavalry, and the fastest thing in the game |
| 14 | knights | **Praetoriani** | 5/35/1 | The Emperor's own guard, bought at the Emperor's own castle |

### 7.2 Vicus — the provinces (plains family)

| # | Base | Roman | SL/HP/MV | Why |
|---|---|---|---|---|
| 0 | peasants | **Coloni** | 1/1/1 | Tenant farmers; the trash tier and the fallback troop |
| 3 | wolves | **Lupi** | 2/3/3 | Kept as wolves — Rome's own founding animal |
| 11 | nomads | **Numidae** | 3/15/2 | Numidian light horse, the most famous irregular cavalry of the ancient world |
| 16 | barbarians | **Sarmatae** | 4/40/3 | Steppe warriors; fast, heavy, and genuinely of the plains |
| 20 | archmages | **Furiae** | 5/25/1, `FLY\|MAGIC` | The Furies are winged avenging spirits who strike at range — the flight and the magic are both the myth |

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

Rome supplies this tier natively; none of it is borrowed fantasy.

| # | Base | Roman | SL/HP/MV | Why |
|---|---|---|---|---|
| 4 | skeletons | **Larvae** | 2/3/2, `UNDEAD` | The Latin word meant precisely a skeletal ghost |
| 5 | zombies | **Lemures** | 2/5/1, `UNDEAD` | The restless dead of the Lemuria — the same rite the Turn Undead spell is named for |
| 13 | ghosts | **Manes** | 4/10/3, `ABSORB\|UNDEAD` | Ancestral shades. A host of the dead that swells as it kills is exactly what `ABSORB` does |
| 21 | vampires | **Striges** | 5/30/1, `FLY\|LEECH\|UNDEAD` | Screech-owl blood-drinkers — a genuine Roman vampire, and `FLY\|LEECH` is the myth verbatim |
| 23 | demons | **Empusae** | 6/50/1, `FLY\|SCYTHE` | Shape-shifting devourers in Hecate's service |

### 7.6 Two consequences worth noting

**Dwelling names.** The engine's `dwelling` field must stay
`plains` / `forest` / `hill` / `dungeon` / `castle` — those are matched in
code. Only the *display* names change, and the section headings above are the
proposal: **Vicus** (village), **Lucus** (sacred grove), **Specus** (cave),
**Hypogeum** (underground chamber), **Castra** (the legionary camp).

**Morale groups.** The five groups are frozen along with everything else, but
their flavour labels can be re-themed: **Plebs** (A), **Legio** (B), **Socii**
(C), **Ferae** (D), **Inferi** (E). One rough edge — Baleares are human
slingers sitting in group D with wolves and dragons, and Gigantes sit in
group C with allied peoples. The letters are what the morale chart reads, so
this costs nothing mechanically, but if the labels are ever surfaced in the
UI those two will look odd.

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

**DECIDED: Glory of Rome ships with the editor**, in the release archives, as
the playable pack. It is the first pack that legally can — it is entirely
original, where `kings-bounty` is DOS-extracted and copyright-restricted.

Three consequences follow, recorded in `GAMEBUILDER-SPEC.md` GB-014:

- The release workflow's pack-leak assertion becomes an **allowlist by pack
  id** rather than a blanket deny, in four places, so `glory-of-rome` ships and
  nothing derived from the DOS assets ever does.
- The **art commission is on the critical path** to shipping, not just to
  looking finished. Rome borrows `kings-bounty` art through a base pack for
  development and none of it may survive into the released artifact.
- Rome is **built in the editor**, and doing so is the editor's acceptance
  test. That settles the sequencing: the editor's first phases come before the
  remaining three zones, because hand-placing objects is precisely the work the
  editor exists to delete.

---

## 10. Maps

**DECIDED.** Four hand-authored zone maps, each an ASCII `.dat` under
`maps/`, one byte per tile, resolved through `tile_codes` in `game.json`.

### 10.1 Dimensions are per-zone, and bounded by a raisable constant

Zone `width`/`height` are parsed per-zone and default to 64; short rows pad
with grass. The ceiling is `MAP_MAX_W` / `MAP_MAX_H` in
`engine/include/map.h`, enforced at load — `MapLoadZone` prints `too large`
and fails the zone. That was verified empirically: a 96×96 zone is rejected
and aborts the run.

The ceiling is a **constant, not a structural limit**, and this pack raises
`MAP_MAX_H` from 64 to **128** so Italia can be a proper boot. Measured
consequences of that change:

- **Behaviour-neutral.** 211/211 tests pass and the reference pack's
  validation sweep is identical either way (seeds 0–2: PASS 3/3, 299 days,
  6072 score at both settings).
- **Not a save-format change.** Fog is encoded from each zone's own
  `width`/`height`, never from these bounds.
- **The cost is memory.** `sizeof(Map)` is 212 bytes per tile: 848 KB at
  64×64, 1,696 KB at 64×128. Every autoplay search node snapshots a whole
  `Map` (AP-204), so the frontier beam pays proportionally. Raising the
  *width* too would have cost 4× rather than 2×, which is why only the height
  moved.

### 10.2 Scale, in play terms

The player sees a **5 × 5 tile viewport** with fog radius 3, so a 64×64 zone
is about 164 screens of area. At 40 steps per day a corner-to-corner diagonal
is ~64 steps, under two days; a round trip across a zone is about three days
against a 600-day normal budget.

The maps are not the scarce resource — the calendar is spent fighting and
shopping, not walking. Shrinking therefore buys less than it appears to and
costs geography, so zones shrink only where the real region is genuinely thin.

### 10.3 Per-zone dimensions

| Zone | Size | Shape |
|---|---|---|
| **Italia** | 64 × 128 | Tall and narrow: Po valley at the top, the peninsula running NW→SE, Sicilia / Sardinia / Corsica as islands |
| **Galliae** | 64 × 64 | The one genuinely blocky zone: Gaul centre, Hispania southwest, Britannia across water northwest, the Rhine on the east edge |
| **Africa** | 64 × 28 | A long coastal strip, which is the honest shape of the Roman Maghreb |
| **Oriens** | 64 × 44 | Wide: Anatolia west and centre, the Levant running south |

### 10.4 Scoping tools, in order of preference

1. **Shrink the declaration.** No wasted tiles, and the worldmap view shows a
   correctly shaped region rather than a blob in the corner of a square.
2. **Sea as the hard edge.** Water on the outer rows is free and natural.
3. **Desert as a soft edge.** The best of the three. Desert zeroes the day's
   remaining steps on entry, so the Sahara is *passable but ruinous* — one
   tile per day. The player gets a boundary they can see, understand, and
   cross in desperation, with no wall. Mountains do the same job for the
   Parthian frontier in Oriens and the Rhine in Galliae.

### 10.5 The boat-trap rule

**No town's `boat_x`/`boat_y` may sit on a water body that is not connected to
the open sea.**

The rule is about *docks*, not about enclosed water as such. An earlier
draft of this section demanded one water body per zone; **that was wrong, and
the shipped pack disproves it** — all four `kings-bounty` maps carry 8–32
enclosed water tiles apiece and the pack still clears 15/15. The reason is
that **boats only ever spawn at a town's dock**, so a pond nothing can launch
into is decorative and completely harmless.

What *is* lethal is a dock on such a pond, because a boat launched into an
enclosed body is unrecoverable: rental is charged weekly and forever, and
cancellation is refused mid-sail. The oracle would suffer for it too — the
mover prices every rentable town dock as a boarding edge (AP-093), so a
landlocked dock is a boarding that leads nowhere.

**Rivers are fine either way**, and a river reaching the sea is both realistic
and sailable.

### 10.6 Islands need a coastline, not a dock

Britannia across water inside Galliae needs **a coastline and one dock town
somewhere on the same sea** — that is all.

A second earlier claim, that an island needs its own dock, was also wrong.
Disembarking parks the boat on whatever coastal land tile the hero steps onto
(REQ-243), so any shore is a landing. The shipped `continentia` reaches a
249-tile region and a 195-tile region exactly this way.

The real hazard is narrower: **an objective inside a walled inland pocket** —
land enclosed by forest or mountain with no coast at all. That is reachable
only by flight or a gate. It is not fatal, because the autoplay fetch has a
flight fallback (AP-188) and the shipped `saharia` contains exactly one such
chest, but it should be deliberate rather than accidental.

### 10.6.1 Furnishing is author-time, permanently

**DECIDED.** The `.dat` file contains the **fully rendered map**, edge variants
and all (REQ-229). Nothing about a map's appearance is computed at game time,
and `furnish_map` in the engine stays a no-op forever. The editor bakes the
variants when it saves.

### 10.6.2 The checker

`tools/mapcheck.py` enforces all of the above:

```sh
tools/mapcheck.py <pack-dir> <map.dat> [WxH] [zone-id]
```

It verifies dimensions and tile codes, flags a dock on landlocked water,
reports objectives stranded in inland pockets, and prints the terrain
breakdown. It is **calibrated against the shipped pack** — three of the four
`kings-bounty` maps pass clean, and the fourth reports only that one real
`saharia` chest. A checker that failed known-good maps would be worthless,
which is how the two wrong rules above were caught.

### 10.7 Per-zone budget

Real requirements, not guidelines:

- **≥ 21 chest placeholder tiles per zone.** The salt barrel draws from them
  (REQ-231): 2 artifacts + 1 navmap + 1 orb + 2 telecaves + 10 dwellings + 5
  friendly foes. Anything above 21 remains a real chest. Reference zones carry
  45–75.
- **More contract-eligible castles than villains, with margin.** Italia hosts
  six, so it wants 10–12.
- **Castles are 3 × 2 footprints** — one walkable gate plus five blocking wall
  tiles. They need flat room.
- Exactly one `is_home` zone (Italia); one `magic_alcove` and one
  `hero_spawn` per zone.

### 10.8 One consequence to watch

Smaller maps mean less travel, so **the pack gets easier** while
`days_per_difficulty` stays at 900/600/400/200. This is measurable rather than
guesswork — the oracle reports days-to-clear per seed — so the day budget is
left alone until validation says otherwise.

---

## 11. Next deliverables

The six decisions previously open here are now settled and folded into the
sections above. What remains is work, not choices:

1. ~~The 25-troop name table~~ — **drafted** (§7), awaiting review.
2. ~~The villain armies and rewards~~ — **drafted** (§3.3), which also fixes
   catalog order. Awaiting review.
3. **The four maps** and their settlement placements (§2.3), the largest job.
4. **Validation** — `--validate-pack 0 255`, then iterate on whatever the
   oracle reports (§3.2).
5. **Art commission** against `ART-SPEC.md`, once the troop and villain
   rosters are final and the file list is therefore known.
