# OpenKB — Complete Formal Specification

A clean-room reimplementation reference for the openkb engine
(`/home/danheskett/personal/openkb-reference/`), a free/libre
reimplementation of *King's Bounty* (New World Computing, 1990, with
updated 1995 KB.EXE). This document describes every rule, data table,
tile code, string offset, combat formula, UI screen, save-file byte,
spell effect, and asset format implemented in the openkb source tree.

Everything here is derived from the openkb C source. Where openkb
deviates intentionally from DOS *King's Bounty*, that deviation is
called out. Where openkb is incomplete (commented-out code, stubs,
unimplemented spells), that is also stated.

---

## Table of Contents

1. Top-level architecture
2. Data types and byte ordering
3. Global constants and limits
4. The `KBgame` struct — complete game state
5. Troop catalog — 25 units, all stats
6. Character classes and ranks
7. Villains — 17 foes, armies and rewards
8. Dwellings, morale, and the morale chart
9. Artifacts and their powers
10. Spells — 14 spells, costs, effects
11. The world — 4 continents, castles, towns, coordinates
12. Tiles — map codes, tile predicates, furnishing
13. Random generation — chance tables and salting
14. Game creation — `spawn_game` step by step
15. Player actions — movement, combat, flight, sailing
16. Day/week cycle, end-of-week budget
17. Combat engine — battlefield, turns, damage formula
18. Spell effects in detail
19. AI — foe follow, unit think, target pick
20. Chests and rewards
21. Victory, defeat, scoring
22. Save file (.DAT) — complete 20421-byte layout
23. UI — screens, menus, hotspots, gamestates
24. Verbatim strings hardcoded in source
25. Strings sourced from KB.EXE — offsets by version
26. Colors — EGA, CGA palettes, color schemes
27. Resource system — GR_/SN_/DAT_/STR_/STRL_/RECT_/COL_/PAL_ IDs
28. Module system — DOS, Free (GNU), Mega Drive
29. DOS asset formats — CC, IMG, EXE (ExePack, Execomp), MCGA, CH, tunes
30. Font — bitmap glyph format
31. Keybindings — adventure, combat, targeting
32. Configuration file, command-line, directories
33. Multiplayer combat emulator (combat.c) — packet protocol
34. Known bugs and incomplete features
35. Appendices — full byte-level tables
36. Tools (`src/tools/`) — auxiliary CLI programs
37. Implementation guidance — recommended development order
38. Verification checklist — spec compliance tests
39. References — source tree, original game, related projects
40. Conclusion

---

## 1. Top-level architecture

openkb is a C99 program (`-std=c99`) targeting SDL 1.2 with
optional SDL_image (for PNG support) and libpng. It builds with
GNU autotools (`configure.ac`, `Makefile.in`) and works on Linux,
Windows (MinGW via `USE_WINAPI`), and macOS (where SDLmain
intercepts `main`). All code is C; no C++ or other languages.

### 1.1 Process flow

The entry point is `src/main.c`. The complete startup sequence:

```
main(argc, argv)
  if argv[1] == "--version":
      dump_version(); return 0
  KB_stdlog("openKB version <PACKAGE_VERSION>")
  wipe_config(&KBconf)              // zero out global config
  read_env_config(&KBconf)           // HOME/APPDATA, install_dir
  wipe_config(&CMDconf)
  read_cmd_config(&CMDconf, argc, argv)   // parse --flag args
  // CLI may override config_dir / config_file
  if CMDconf.config_file:
      if test_config(file, 0): return 1   // verify readable
      apply file to KBconf
  else:
      find_config(&KBconf)            // search cwd, $HOME, /etc
  if config readable:
      read_file_config(FILEconf, file)
      apply_config(&KBconf, &FILEconf)
  apply_config(&KBconf, &CMDconf)     // CLI takes precedence
  report_config(&KBconf)              // print effective config
  if test_directory(KBconf.data_dir, 0): return 1
  if test_directory(KBconf.save_dir, 1): return 1   // create if absent
  while playing > 0:
      playing = run_game(&KBconf)
```

`run_game()` returns:
- `0` — clean exit (player chose to quit; main loop ends).
- `< 0` — fatal error.
- `> 0` — restart (re-enter the main loop).

The current implementation only ever returns 0 or -1, so the
`while` loop runs once, then exits. The architecture supports
hot-restart but it isn't wired.

### 1.2 Run-game lifecycle (game.c:6608)

```
run_game(conf):
    sys = KB_startENV(conf)             // SDL_Init, video, audio, font
    if !sys: return -1
    if conf->autodiscover:
        discover_modules(install_dir, conf)
        discover_modules(data_dir, conf)
    register_modules(conf)              // currently no-op
    mod = select_module()                // returns -1 on Esc
    if mod == -1:
        KB_stopENV(sys); return -1
    init_modules(conf)                   // DOS_Init etc.
    KB_setfont(sys, SDL_LoadRESOURCE(GR_FONT, 0, 0))
    prepare_resources()                  // load frames, colors,
                                         // tile rects, status colors
    display_logo()
    display_title()
    game = select_game(conf)             // create or load
    if game:
        SDL_WM_SetCaption("<Name> the <Title> - openkb <ver>")
        update_ui_colors(game)           // recolor by difficulty
        adventure_loop(game)              // main game loop
    free_resources()
    stop_modules(conf)
    KB_stopENV(sys)
    return 0
```

### 1.3 Build system

Top-level `configure.ac` declares:
- Package name: `openkb`
- Version macro: `PACKAGE_VERSION`
- Required: SDL 1.2 (HAVE_LIBSDL).
- Optional: SDL_image (HAVE_LIBSDL_IMAGE), libpng (HAVE_LIBPNG).

Conditional compile flags exercised by source:
- `HAVE_LIBSDL` — required (`main.c:21` errors out without it).
- `HAVE_LIBSDL_IMAGE` — enables PNG asset loading; without it,
  the GNU module falls back to `.bmp`.
- `HAVE_MALLOC` — enforced by kbstd.h.
- `HAVE_MALLOC_H` — chooses between `<malloc.h>` and `<memory.h>`.
- `HAVE_STRDUP` — picks between system `strdup` and `KB_strdup`.
- `HAVE_STRCASECMP` — picks between system and `KB_strcasecmp`.
- `USE_WINAPI` — Windows MessageBox for errors, `\\` path
  separator, %APPDATA% config root.
- `DEBUG` — extra `KB_debuglog` output.
- `PKGDATADIR` — install path for assets (e.g. `/usr/local/share`).
- `PACKAGE_NAME` — added to `install_dir` for asset search.

`src/Makefile.am` produces:
- `openkb` — the main game binary
- `openkb_combat` — multiplayer combat emulator (combat.c)
- `src/tools/*` — auxiliary CLI utilities (§36)

### 1.4 Vendor code (`vendor/`)

Outside this spec's scope but referenced by source:
- `vendor/savepng.h` — libpng wrapper (used by tools, not game).
- `vendor/scale2x.c` — AdvancedMAME scale2x filter.
- `vendor/inprint.c` — inline `inprint`/`incolor`/`infont`
  glyph blitter.
- `vendor/inline_font.h` — fallback 8×8 font.
- `vendor/strlcpy/strlcat` — BSD-style string copies.

These are pulled in via `vendor.h` and called as low-level
primitives (e.g. `infont(surf)` to set the active font for
inprint, `incolor(fore, back)` to set palette indices,
`inprint(surf, str, x, y)` to draw text).

### 1.5 Memory model

openkb is single-threaded except for the SDL audio callback
(invoked from a separate thread by SDL_OpenAudio). Audio callback
reads only `sys->sound`, which is set/cleared atomically (writes
of pointer-sized values are atomic on the supported platforms).

The game state — `KBgame *game` — is a single heap allocation
(~22 KB). All game logic mutates this structure directly. There
is no save-on-undo or transactional state; once a turn is
processed, it's committed.

Resource loading uses a per-module cache (e.g. `surf_cache[64][64]`
in env-sdl.c) plus per-module-specific caches (DOS_Cache holds
the unpacked KB.EXE handle, palettes, and decoded tunes). Cached
surfaces are freed only on module stop; this means a single game
run can accumulate ~5 MB of cached SDL surfaces.

Core source files, grouped by role (referenced again in §1.10):

- **Game rules (portable, no SDL):**
  - `src/bounty.h` — structs, constants, extern declarations
  - `src/bounty.c` — static data tables (troops, classes, villains,
    coords, spells, artifacts, chance tables, puzzle map)
  - `src/play.h`, `src/play.c` — game creation, random seeding, day
    cycle, troop bookkeeping, combat engine mechanics (non-UI)
  - `src/save.h`, `src/save.c` — load/save 20421-byte .DAT files
  - `src/rogue.h`, `src/rogue.c` — post-generation map "furnishing"
    (tile-transition beautification)

- **Game UI (SDL):**
  - `src/game.c` — adventure loop, combat loop, menus, every screen,
    rendering, input, hotspots, AI
  - `src/ui.h`, `src/ui.c` — hotspot/gamestate framework, message
    boxes, top/bottom frames
  - `src/env.h`, `src/env-sdl.c` — SDL window init, font blit,
    audio callback, scaling, resource loader front-end
  - `src/font.h` — inline fallback 8×8 bitmap font
  - `src/main.c` — entry, config
  - `src/combat.c` — separate multiplayer combat emulator binary
    (TCP packet protocol)

- **Library / asset plumbing (`src/lib/`):**
  - `kbsys.h` — basic types, byte packing macros
  - `kbstd.{h,c}` — stdlib-ish (strlist, rand, path helpers)
  - `kbdir.{h,c}` — virtual directory API with CC/IMG drivers
  - `kbfile.{h,c}` — virtual file API with BUF/CC/IMG drivers,
    SDL RWops bridge
  - `kbres.{h,c}` — resource ID enum, EGA/CGA palettes, tileset
    factories, surface helpers
  - `kbconf.{h,c}` — config file, env, CLI parsing
  - `kbauto.{h,c}` — module auto-discovery by fingerprinting files
  - `kbsound.h` — `KBsound` wrapper type
  - `dos-data.c` — DOS (KB.EXE + CC) resolver
  - `dos-cc.{h,c}` — .CC archive reader with LZW decompression
  - `dos-img.c` — .4/.16/.256/.CH image decoder
  - `dos-exe.c` — KB.EXE unpacker (Execomp LZW + MS ExePack)
  - `dos-snd.{h,c}` — DOS PC-speaker tune digitizer
  - `free-data.c` — modern "free" module (INI + PNG/BMP)
  - `md-rom.c` — Mega Drive ROM image decoder (troops+villains only)

- **Tools (`src/tools/`, separate binaries, out of scope for rules):**
  - `kbcc.c`, `kbcc2.c` — CC archive tools
  - `kbview.c`, `kbrowse.c` — asset inspector
  - `kbmaped.c` — map editor
  - `kbimg2dos.c` — image converter
  - `modding.c` — modding helper
  - `unexepack.c`, `unexecomp.c` — standalone exe unpackers

A fallback inline 8×8 font (`src/font.h`) is used before a module's
font is loaded.

### 1.7 Coordinate system conventions

- World map: `(x, y)`, x = column 0..63, y = row 0..63. Origin
  at top-left as bytes are laid out, but **rendering flips y**:
  on screen, larger y = closer to top. See `draw_map`'s
  `(perim_h - 1 - j)` flip, game.c:1194. Saving and loading use
  the unflipped storage convention.
- Combat grid: `(x, y)`, x = 0..5 (CLEVEL_W=6), y = 0..4
  (CLEVEL_H=5). Origin top-left, no flip. Player side starts at
  x=0, AI side starts at x=5.
- Sidebar / status / map rects are in screen pixel space, scaled
  by `sys->zoom` (1 or 2).

### 1.8 Naming conventions

- `KB_*` (uppercase): public library functions and macros.
- `kb*` (lowercase prefix): library types and structs (KBgame,
  KBcombat, KBtroop, KBclass, KBunit, KBconfig, KBmodule,
  KBsound, KBfile, KBdir, KBenv, KBwin, KBpacket, KBpalette,
  KBhotspot, KBgamestate, KBsession, KBfileid, KBconsole).
- `DOS_*`: DOS-module-specific helpers.
- `GNU_*`: free-module-specific helpers.
- `MD_*`: Mega Drive module-specific helpers.
- `inline_*`: vendor-provided inline glyph functions.
- `SDL_*`: SDL primitives or openkb extensions on SDL_Surface.

### 1.9 Important global state

- `KBconf` (main.c) — singleton config struct.
- `sys` (game.c, env-sdl.c) — `KBenv *`, the running environment.
- `local` (ui.c) — `KBsession`, cached UI rects + colors.
- `surf_cache[64][64]` (env-sdl.c) — surface cache for
  `SDL_TakeSurface`.
- `kbd_state[512]` (ui.c) — keyboard-held bitmap for SOFTKEY.
- `auto_battle` (game.c, default 0) — toggled by cheat 'A'.

### 1.10 Source layout

## 2. Data types and byte ordering

Defined in `src/lib/kbsys.h`. The header is included by every
source file that touches binary data and provides primitive
integer types, endian detection, and byte-stream macros.

### 2.1 Primitive types

When compiled against SDL (the only supported configuration —
main.c:21 issues `#error` without `HAVE_LIBSDL`):

```c
typedef Uint8   byte;   // 8-bit unsigned
typedef Uint16  word;   // 16-bit unsigned
typedef Uint32  sword;  // 24-bit unsigned, stored in 32 bits
typedef Uint32  dword;  // 32-bit unsigned
```

Without SDL (header-included by some standalone tools):

```c
typedef uint8_t  byte;
typedef uint16_t word;
typedef uint32_t sword;
typedef uint32_t dword;
```

`sword` ("sesqui-word") is a 24-bit unsigned — used in `.CC`
archive entries (file size, file offset both fit in 24 bits)
and tile counts. It's stored as 4 bytes in memory but only 3
bytes on disk; `READ_SWORD` reads exactly 3 bytes.

Note: there are no signed integer typedefs. Numeric loop indices
in source use `int` directly.

### 2.2 Endian detection

```c
#ifdef HAVE_LIBSDL
  #define KB_BYTE_ORDER  SDL_BYTEORDER
  #define KB_LIL_ENDIAN  SDL_LIL_ENDIAN
  #define KB_BIG_ENDIAN  SDL_BIG_ENDIAN
#else
  #ifdef __MINGW32__       // assume little-endian
    #define __BYTE_ORDER __LITTLE_ENDIAN
  #else
    #include <endian.h>     // glibc
  #endif
  #define KB_BYTE_ORDER  __BYTE_ORDER
  #define KB_LIL_ENDIAN  __LITTLE_ENDIAN
  #define KB_BIG_ENDIAN  __BIG_ENDIAN
#endif
```

Compile-time detection chooses between LE and BE pack/unpack
implementations. All on-disk data in openkb is little-endian:
- Save files (`*.DAT`)
- DOS asset files (`*.4`, `*.16`, `*.256`, `*.CC`)
- KB.EXE byte arrays (when read for tunes, rewards, etc.)

Big-endian platforms (Mega Drive ROM, network packets in
`combat.c`) get `_BE` variants. These are still rare paths; the
common case is LE.

### 2.3 Bit-shift and rotate macros

```c
SHIFT_LEFT_LE(VAL, N)   = VAL << N
SHIFT_RIGHT_LE(VAL, N)  = VAL >> N
SHIFT_LEFT_BE           = SHIFT_RIGHT_LE
SHIFT_RIGHT_BE          = SHIFT_LEFT_LE

ROTATE_LEFT_16_LE(W, N) = (W << N) | ((W >> (16 - N)) & N)
ROTATE_LEFT_16_BE       = ROTATE_RIGHT_16_LE
```

Note the suspicious `& N` mask in `ROTATE_LEFT_16_LE` (should
likely be `& ((1<<N)-1)` for a clean rotate). The macro is only
used in one place (CC archive hashing — dos-cc.c:42 — which
manually swaps bytes and rotates left by 1, so the mask `& 1`
happens to be correct for N=1).

### 2.4 Pack/unpack macros (on values, not pointers)

```c
UNPACK_16_LE(c1, c2)         = ((c2 & 0xFF) << 8) | (c1 & 0xFF)
UNPACK_24_LE(c1, c2, c3)     = (c3<<16) | (c2<<8) | c1
UNPACK_32_LE(c1, c2, c3, c4) = (c4<<24) | (c3<<16) | (c2<<8) | c1

UNPACK_16_BE(c1, c2)         = UNPACK_16_LE(c2, c1)
UNPACK_24_BE(c1, c2, c3)     = UNPACK_24_LE(c3, c2, c1)
UNPACK_32_BE(c1, c2, c3, c4) = UNPACK_32_LE(c4, c3, c2, c1)
```

Each `& 0xFF` mask exists to handle signed `char` extension —
without it, a byte 0x80..0xFF would sign-extend on most compilers
and corrupt the result.

Pack macros mutate by reference:

```c
PACK_16_LE(C1, C2, V)         = C1 = V & 0xFF, C2 = (V >> 8) & 0xFF
PACK_24_LE(C1, C2, C3, V)     = C1 = V & 0xFF, C2 = (V >> 8) & 0xFF,
                                C3 = (V >> 16) & 0xFF
PACK_32_LE(C1, C2, C3, C4, V) = ... up to C4 = (V >> 24) & 0xFF
```

`KB_PACK_*` and `KB_UNPACK_*` aliases route to LE or BE versions
based on `KB_BYTE_ORDER`. So `KB_UNPACK_WORD` is `UNPACK_16_LE`
on x86 hosts and `UNPACK_16_BE` on PowerPC.

### 2.5 Pointer-stream macros (mutate pointer)

These are the workhorses. They auto-advance the `p` pointer:

```c
READ_BYTE(p)    = *p++                                    // 1 byte
READ_WORD(p)    = KB_UNPACK_WORD(*p, *(p+1)),  p += 2     // 2 bytes
READ_SWORD(p)   = KB_UNPACK_SWORD(*p, *(p+1), *(p+2)), p += 3
READ_DWORD(p)   = KB_UNPACK_DWORD(*p, *(p+1), *(p+2), *(p+3)), p += 4

READ_WORD_BE(p)  = KB_UNPACK_WORDBE(...), p += 2
READ_SWORD_BE(p) = KB_UNPACK_SWORDBE(...), p += 3
READ_DWORD_BE(p) = KB_UNPACK_DWORDBE(...), p += 4

WRITE_BYTE(p, v)  = *p++ = v                              // 1 byte
WRITE_WORD(p, v)  = KB_PACK_WORD(*p, *(p+1), v), p += 2
WRITE_SWORD(p, v) = KB_PACK_SWORD(...), p += 3
WRITE_DWORD(p, v) = KB_PACK_DWORD(...), p += 4
```

Subtle behavior:
- All are macros, not functions. The pointer is mutated in
  place. This means `READ_WORD(p) + READ_WORD(p)` reads two
  consecutive 16-bit values from the stream.
- `p` must be a `char*` (signed or unsigned) — pointer arithmetic
  is in bytes.
- Returned value is an integer expression. `READ_BYTE(p)` is
  `*p++`, which is fine in mid-expression. `READ_WORD(p)` ends
  with `p += 2` as a comma-expression with the unpack as the
  result, so it's safe in mid-expression too.

### 2.6 Usage patterns

**Save load** (save.c:46): `char *p = &buf[0]`, then ~80 calls
to `READ_BYTE(p)`, `READ_WORD(p)`, and one `READ_DWORD(p)` for
gold. Final check: `if (p - &buf[0] != DAT_SIZE)` — the buffer
position should equal the total file size.

**CC hash** (dos-cc.c:30): walks input string byte by byte,
masking, folding, swapping. No pack/unpack involved.

**EXE header parsing** (dos-exe.c:127): `exeHeader_read(h, buf)`
reads 14 consecutive `READ_WORD(buf)` calls to fill a struct.
Avoids `fread(struct)` because struct padding and endianness
would break portability.

**Tune palette** (dos-snd.c:42): `READ_WORD(p)` × 88 for
frequencies, then × 16 for delays.

**IMG file header** (dos-img.c:88): `READ_WORD(p)` for num_files,
then 2 × `READ_WORD(p)` per file (offset, mask_offset).

**Network packets** (combat.c:267): `SDLNet_Read16` (BE) and
`SDLNet_Read32` (BE) used directly — combat.c bypasses kbsys
macros and uses SDL_net's BE-by-default. This is consistent with
network byte order.

### 2.7 Limits and assumptions

- `int` is at least 16 bits but presumed ≥32 in source (e.g. for
  hostile foe array iterations: `4 * 35 * 3 = 420`, fits in
  16-bit but values like 16384-byte map dumps don't).
- `dword` is exactly 32 bits.
- `sword` is **logically** 24-bit but stored in 32 bits in memory;
  `READ_SWORD` reads 3 disk bytes and zero-extends to 32-bit.
- `size_t` is not used; all sizes are `int` or hardcoded `dword`.
- No 64-bit types appear anywhere in openkb.

### 2.8 Float / double

openkb avoids floating point entirely after game.c was
restructured. Combat distance uses `isqrt32` (integer Pythagorean
square root, fixed-point). Float math is referenced only in
commented-out source ("`float dist = sqrt(pow(…,2) + …)`"), kept
for documentation. The combat fallback uses bit-shift digit-by-
digit integer sqrt — see §17.5.4 / §35.

### 2.9 Sign handling

Where signed values are needed, `signed char` is explicit:
- `puzzle_map[5][5]` uses `signed char` — negative = artifact id,
  non-negative = villain id.
- `move_offset_x[]`, `move_offset_y[]` — `signed char` arrays of
  -1, 0, +1.
- `combat_move_offset_x/y[]`, `target_move_offset_x/y[]` — same.

Otherwise, `byte` (unsigned) is used everywhere even for values
that semantically can be negative (e.g. `skill_diff` in damage
formula is computed as `byte + 5 - byte`, but assigned to `int`
local `skill_diff`). The pattern: store as unsigned, compute as
signed via int promotion.

## 3. Global constants and limits

All from `bounty.h` unless otherwise noted. Most are baked into
the `KBgame` struct as fixed-size arrays — changing them requires
re-baking the save format. Decimal values shown for clarity.

### 3.1 World-shape constants

```
MAX_CONTINENTS       = 4
LEVEL_W              = 64        // map width in tiles
LEVEL_H              = 64        // map height in tiles

MAX_SPELLS           = 14        // 7 combat + 7 adventure
MAX_ARTIFACTS        = 8

MAX_VILLAINS         = 17
MAX_FOES             = 40        // foes per continent
FRIENDLY_FOES        = 5         // first 5 foe slots = friendlies

MAX_DWELLINGS        = 11        // dwellings per continent
MAX_CASTLES          = 26
MAX_TOWNS            = 26        // = MAX_CASTLES
MAX_TELECAVES        = 2         // per continent

MAX_TROOPS           = 25

MAX_CLASSES          = 4
MAX_RANKS            = 4

MAX_PLAYER_ARMY      = 5
MAX_CASTLE_ARMY      = 5
MAX_FOE_ARMY         = 5         // but only 3 of 5 used for foes

DAY_STEPS            = 40        // steps before day ends
WEEK_DAYS            = 5         // days per week

PUZZLEMAP_W          = 5
PUZZLEMAP_H          = 5

MAX_SPECIAL_PLACES   = 2
SP_HOME              = 0
SP_ALCOVE            = 1

HOME_CONTINENT       = 0
HOME_X               = 11
HOME_Y               = 7

ALCOVE_CONTINENT     = 0
ALCOVE_X             = 11
ALCOVE_Y             = 19

COST_ALCOVE          = 5000
COST_BOAT_CHEAP      = 100        // with Anchor of Admirability
COST_BOAT_EXPENSIVE  = 500
COST_SIEGE_WEAPONS   = 3000

KBMOUNT_SAIL         = 0
KBMOUNT_FLY          = 4
KBMOUNT_RIDE         = 8

KBCASTLE_KNOWN       = 0x40       // bit set when castle's villain is known
KBCASTLE_PLAYER      = 0xFF       // castle owned by player
KBCASTLE_MONSTERS    = 0x7F       // castle unowned (generic monsters)
KBCASTLE_VILLAIN     = 0x1F       // mask for villain id in owner byte

MORALE_NORMAL        = 0
MORALE_LOW           = 1
MORALE_HIGH          = 2

DWELLING_PLAINS      = 0
DWELLING_FOREST      = 1
DWELLING_HILLCAVE    = 2          // "Hill" or "Cave"
DWELLING_DUNGEON     = 3
DWELLING_CASTLE      = 4

MGROUP_A             = 0   // Smallfolk
MGROUP_B             = 1   // Lords
MGROUP_C             = 2   // Creatures
MGROUP_D             = 3   // Monsters
MGROUP_E             = 4   // Undead

ABIL_FLY             = 0x01
ABIL_REGEN           = 0x02
ABIL_MAGIC           = 0x04
ABIL_IMMUNE          = 0x08
ABIL_ABSORB          = 0x10
ABIL_LEECH           = 0x20
ABIL_SCYTHE          = 0x40
ABIL_UNDEAD          = 0x80

POWER_UNKNOWN_XXX1       = 0x01
POWER_QUARTER_PROTECTION = 0x02
POWER_DOUBLE_LEADERSHIP  = 0x04
POWER_DOUBLE_SPELL_POWER = 0x08
POWER_INCREASE_COMMISSION= 0x10
POWER_CHEAPER_BOAT_RENTAL= 0x20
POWER_DOUBLE_MAX_SPELLS  = 0x40
POWER_INCREASED_DAMAGE   = 0x80

CLEVEL_W             = 6          // combat grid width
CLEVEL_H             = 5          // combat grid height
MAX_SIDES            = 2
MAX_UNITS            = 5
MAX_PHASES           = 2
```

### 3.2 Constant-rationale notes

**MAX_CONTINENTS = 4 / LEVEL_W = LEVEL_H = 64.** Four 64×64 maps
total 16384 bytes (`= 4 × 64 × 64`), exactly the size of the
`LAND.ORG` raw map file (§14.4). The choice of 64×64 means an
8-bit `byte` x/y coordinate is sufficient (range 0..63 fits
comfortably in 0..255).

**MAX_SPELLS = 14, split 7+7.** Combat spells are ids 0x00..0x06,
adventure spells 0x07..0x0D. The `choose_spell` UI presents them
as 2 columns with letters A..G addressing each side. See §10.

**MAX_ARTIFACTS = 8.** The puzzle map (5×5 = 25 cells) holds 8
artifacts (negative ids -1..-8) and 17 villains (ids 0..16).
8 + 17 = 25 = total cells. Removing or adding an artifact
breaks the puzzle map layout. See §9.2.

**MAX_VILLAINS = 17, distributed 6+4+4+3.** This split
(`villains_per_continent[]`) plus the rank-promotion table
(`villains_needed`) drives the entire endgame pacing. Knight
needs 14 villains for Lord rank, Paladin needs 13 for Champion,
Sorceress 12, Barbarian 10. Catching all 17 is required for
unlocking the scepter via puzzle map.

**MAX_FOES = 40, FRIENDLY_FOES = 5.** The first 5 slots per
continent are friendly foes (recruit on contact); the remaining
35 are hostile (combat on contact). Save format separates these
two regions, with friendly foes carrying only coords (no troops/
counts — they're rolled fresh on each encounter; see §20.7) and
hostile foes carrying full troops+counts data.

**MAX_DWELLINGS = 11.** Dwelling slot 11 doesn't exist; the slot
table caps at 11 with a 0xFF terminator for unused slots. See
`continent_dwellings[]` in §8.1.

**MAX_CASTLES = 26 = MAX_TOWNS.** One town per castle. The 'A'-'Z'
letter grid in the Castle/Town Gate spell UIs requires exactly 26
of each so each letter has one target. The villain-castle and
town-spell salting both walk these arrays.

**MAX_TELECAVES = 2.** Always exactly 2 paired teleport caves per
continent (per `salt_continent` call args). Walking onto one
warps to the other. See §20.10.

**MAX_TROOP_DIFFICULTY = 4.** Same as MAX_CONTINENTS: difficulty
tier of a castle/foe maps to its continent's tier, mostly. Used
by `troop_chance_table`, `troop_numbers`, etc.

**MAX_TROOP_CHANCE_CURVE = 5.** Five tiers of troop strength
within each dwelling type (e.g. Plains: Peasants → Wolves →
Nomads → Barbarians → Archmages). The `chance_table` has 4
thresholds (cumulative) defining 5 brackets.

**MAX_PLAYER_ARMY = 5, MAX_CASTLE_ARMY = 5, MAX_FOE_ARMY = 5.**
All armies cap at 5 stacks. Save format reserves 5 slots in each
case. Empty slot = troop_id `0xFF`, count 0. Foes only use 3 of
their 5 slots (encounter limit) but allocate 5 for alignment.

**DAY_STEPS = 40, WEEK_DAYS = 5.** A "day" is 40 player movement
steps; a "week" is 5 days = 200 steps. Time Stop spell suspends
the step counter, not days. End-of-day fires every time
`steps_left` reaches 0; end-of-week fires every 5th `end_day`
(see §16). On Easy (900 days) the player has 180 weeks; on
Impossible (200 days) just 40 weeks.

**PUZZLEMAP_W = PUZZLEMAP_H = 5.** Always exactly 5×5. Don't
change.

**MAX_SPECIAL_PLACES = 2.** SP_HOME (King Maximus's castle, where
the player starts and respawns on temp_death) and SP_ALCOVE
(Archmage Aurange's hut for learning magic). Both on continent 0.

**HOME_X=11, HOME_Y=7.** Home castle's tile coordinates (the
castle tile at (11,7); player spawns at (11,5) — 2 tiles north
of gate). Hardcoded; LAND.ORG must place the castle at exactly
this position.

**ALCOVE_X=11, ALCOVE_Y=19.** Same column as home but 12 rows
south. Convenient for early-game Sorceress (already knows magic)
and required visit for other classes.

### 3.3 KBMOUNT_* state values

```
KBMOUNT_SAIL = 0    // on a boat in water
KBMOUNT_FLY  = 4    // flying
KBMOUNT_RIDE = 8    // walking on land (default)
```

Stored in `game->mount`. The values are sprite-sheet offsets:
`game->mount + frame` indexes into the hero animation strip.
Frame is 0..3 cycling. Each row of the sprite has 12 cells:
- 0..3: sail-frame 0..3
- 4..7: fly-frame 0..3
- 8..11: ride-frame 0..3

(See `draw_player`, game.c:1218.)

### 3.4 KBCASTLE_* castle-owner masks

```
KBCASTLE_KNOWN    = 0x40    // bit set when villain location revealed
KBCASTLE_PLAYER   = 0xFF    // player owns this castle
KBCASTLE_MONSTERS = 0x7F    // unowned, generic garrison
KBCASTLE_VILLAIN  = 0x1F    // mask: low 5 bits = villain id (0..16)
```

`castle_owner[i]` byte is one of:
- `0xFF` (player) — castle counts toward score, can be visited
  to recruit.
- `0x7F` (monsters) — castle counts as enemy, end-of-week
  repopulation skipped.
- `0x00..0x10` — villain id, optionally with `KBCASTLE_KNOWN`
  set (so range is 0x00..0x10 or 0x40..0x50).

`(owner & KBCASTLE_VILLAIN)` extracts the villain id. The 0x40
KNOWN bit is set by Find Villain spell or by visiting and
defeating the contracted villain (which is what allows the
contract view to display "Castle: X" instead of "Castle:
Unknown" — see §15.5 view_contract).

### 3.5 Morale and dwelling enums

```
MORALE_NORMAL = 0
MORALE_LOW    = 1
MORALE_HIGH   = 2

DWELLING_PLAINS   = 0
DWELLING_FOREST   = 1
DWELLING_HILLCAVE = 2
DWELLING_DUNGEON  = 3
DWELLING_CASTLE   = 4
```

The morale values are also their indices into `morale_cnv[3] =
{LOW=1, NORMAL=0, HIGH=2}` (which converts MORALE_* enum to
"strength" for comparison — note the swap of LOW and NORMAL).
This swap creates the morale bug §34.2.

The dwelling enum is also the offset into the LOCATION asset
(`GR_LOCATION` sub_id 0=cstl, 1=town, 2=plains, 3=forest, 4=cave,
5=dungeon — see §29.5).

### 3.6 Morale group enum

```
MGROUP_A = 0    // Smallfolk: Peasants, Militia
MGROUP_B = 1    // Lords: Archers, Pikemen, Knights, Cavalry
MGROUP_C = 2    // Creatures: Sprites, Gnomes, Elves, etc.
MGROUP_D = 3    // Monsters: Wolves, Orcs, Ogres, Trolls, Dragons
MGROUP_E = 4    // Undead: Skeletons, Zombies, Ghosts, Vampires, Demons
```

Each troop has a fixed `morale_group` (see §5). The
`morale_chart[5][5]` cross-references "host" J vs "candidate" I
group to produce a morale value. See §8 for the chart.

### 3.7 ABIL_* ability flags

```
ABIL_FLY    = 0x01
ABIL_REGEN  = 0x02
ABIL_MAGIC  = 0x04
ABIL_IMMUNE = 0x08
ABIL_ABSORB = 0x10
ABIL_LEECH  = 0x20
ABIL_SCYTHE = 0x40
ABIL_UNDEAD = 0x80
```

8 flags, 1 byte. All 25 troops fit. Flag effects in §17.5
(damage formula) and §16.6 (end-of-week). Any combination is
representable; in practice most troops have 0 or 1 abilities.
Ghosts (ABSORB | UNDEAD = 0x90), Vampires (LEECH | FLY | UNDEAD
= 0xA1), Demons (FLY | SCYTHE = 0x41), Dragons (FLY | IMMUNE =
0x09) are the multi-flag troops.

### 3.8 POWER_* artifact powers

```
POWER_UNKNOWN_XXX1        = 0x01    // Book of Necros (no effect)
POWER_QUARTER_PROTECTION  = 0x02    // Shield: target damage ×3/4
POWER_DOUBLE_LEADERSHIP   = 0x04    // Crown: leadership ×2 on pickup
POWER_DOUBLE_SPELL_POWER  = 0x08    // Amulet: spell_power ×2 on pickup
POWER_INCREASE_COMMISSION = 0x10    // Articles: commission +2000
POWER_CHEAPER_BOAT_RENTAL = 0x20    // Anchor: boat 100/wk vs 500/wk
POWER_DOUBLE_MAX_SPELLS   = 0x40    // Ring: max_spells ×2 on pickup
POWER_INCREASED_DAMAGE    = 0x80    // Sword: attacker damage ×3/2
```

8 powers, 1 byte. Stored in `war->powers[side]` (combat) or
queried via `has_power(game, POWER_X)` from any code path. Each
artifact has exactly one power (`artifact_powers[8]` table).

### 3.9 Cost constants

```
COST_ALCOVE         = 5000   // Aurange teaches magic
COST_BOAT_CHEAP     = 100    // weekly boat with Anchor
COST_BOAT_EXPENSIVE = 500    // weekly boat without Anchor
COST_SIEGE_WEAPONS  = 3000   // siege weapons in towns (unenforced; see §34.14)
```

These are referenced in town-menu and dwelling code but never
overridden by modules. A module rule table providing different
costs would need a new resource id (not defined).

### 3.10 Combat-grid constants

```
CLEVEL_W = 6    // combat field width (columns 0..5)
CLEVEL_H = 5    // combat field height (rows 0..4)
MAX_SIDES = 2   // player + AI; no observers
MAX_UNITS = 5   // 5 stacks per side
MAX_PHASES = 2  // each side gets 2 sub-phases per turn
```

`MAX_PHASES` exists for the "wait then act" pattern: a unit can
wait (move in second phase) or pass (skip turn entirely). When
all units in phase 1 have acted/waited, phase 2 begins; waited
units now get a chance to act. After phase 2, the side ends and
swaps to the other side. The implementation tracks phase as
`war->phase` 0 or 1.

The `+1` padding on `omap` and `umap` (`[CLEVEL_H+1][CLEVEL_W+1]`)
exists to allow safe writes one beyond the grid edge during
animation/update. Indexing `[CLEVEL_H][...]` is a no-op write
that prevents segfaults if movement code briefly walks off.

### 3.11 Why these specific values

Most constants come from the original DOS game and can't be
changed without breaking save compatibility. Specifically:

- World map size is fixed at 4 × 64 × 64 because LAND.ORG is
  exactly 16384 bytes.
- Save file size is exactly 20421 bytes; struct layout depends
  on every constant being its current value.
- KB.EXE byte offsets (§25) presume specific table sizes
  (17 villains, 14 spells, 8 artifacts).
- Asset filenames `cursor.16` etc. presume the troop/villain/
  class/location naming.
- The puzzle map cross-references villain count (17) and
  artifact count (8).

A clean-room reimplementation can change values that don't
affect save/asset compatibility: for instance, increasing
MAX_FOES would require new save format but preserves old games
if read code clamps. Reducing MAX_TROOPS to less than 25 breaks
asset filenames (would need to reduce DOS_troop_names and add
fingerprinting).

When the spec later says "5 troops" or "4 ranks", the names
above are why.

## 4. The `KBgame` struct — complete game state

`KBgame` is the central heap allocation representing an
in-progress game. Sized ~22 KB (mostly the 16384-byte map array
and 2048-byte fog array). Allocated once at game-create
(`spawn_game`) or load (`KB_loadDAT`); freed at exit.

### 4.1 Field declaration order (`bounty.h:84`)

The struct is the canonical field ordering. Any reordering would
require the matching renumbering of save-file offsets in save.c.
Types: `byte` = 1 B, `word` = 2 B, `dword` = 4 B.

```c
struct KBgame {
    char savefile[64];                                 // filename; not saved
    char name[11];                                     // player name

    byte class;                                        // 0..3
    byte rank;                                         // 0..3
    byte mount;                                        // KBMOUNT_SAIL/FLY/RIDE
    byte difficulty;                                   // 0..3
    byte options[6];                                   // see below
    byte spell_power;                                  // additive from class ranks
    byte max_spells;                                   // additive from class ranks

    byte continent;                                    // 0..3 current
    byte y;                                            // 0..63
    byte x;                                            // 0..63
    byte last_y;
    byte last_x;
    byte boat_y;
    byte boat_x;

    byte contract;                                     // active contract villain id
                                                       // 0xFF = none
    byte siege_weapons;                                // 0 or 1
    byte knows_magic;                                  // 0 or 1
    byte boat;                                         // continent boat is on,
                                                       // 0xFF if none rented

    byte contract_cycle[5];                            // 5 villains in town A) slot
    byte last_contract;                                // index into cycle
    byte max_contract;                                 // advancement marker

    byte artifact_found[MAX_ARTIFACTS];                // [8] 0 or 1
    byte villain_caught[MAX_VILLAINS];                 // [17] 0 or 1
    byte continent_found[MAX_CONTINENTS];              // [4] can sail there
    byte orb_found[MAX_CONTINENTS];                    // [4] can see whole map

    byte spells[MAX_SPELLS];                           // [14] count per spell

    byte town_visited[MAX_TOWNS];                      // [26] for town-gate spell
    byte castle_visited[MAX_CASTLES];                  // [26] for castle-gate spell

    byte town_spell[MAX_TOWNS];                        // [26] which spell sold

    byte teleport_coords[MAX_CONTINENTS][MAX_TELECAVES][2];   // [4][2][x,y]
    byte dwelling_coords[MAX_CONTINENTS][MAX_DWELLINGS][2];   // [4][11][x,y]
    byte dwelling_troop[MAX_CONTINENTS][MAX_DWELLINGS];       // troop_id per dwelling
    byte dwelling_population[MAX_CONTINENTS][MAX_DWELLINGS];  // how many available

    byte castle_owner[MAX_CASTLES];                    // [26] per §3
    byte castle_troops[MAX_CASTLES][5];                // garrison troop ids
    word castle_numbers[MAX_CASTLES][5];               // garrison counts

    byte map_coords[MAX_CONTINENTS][2];                // [4][x,y] navmap chest
    byte orb_coords[MAX_CONTINENTS][2];                // [4][x,y] orb chest

    byte foe_coords[MAX_CONTINENTS][MAX_FOES][2];      // [4][40][x,y]
    byte foe_troops[MAX_CONTINENTS][MAX_FOES][5];      // 3 used; 5 allocated
    word foe_numbers[MAX_CONTINENTS][MAX_FOES][5];

    byte steps_left;                                   // 0..40
    word time_stop;                                    // non-zero suspends day drain
    word days_left;                                    // by difficulty, decrements

    byte player_troops[5];                             // 0xFF = empty slot
    word player_numbers[5];

    word followers_killed;                             // cumulative, deducts score

    word base_leadership;                              // accumulated from ranks
    word leadership;                                   // current cap, resets to base
                                                       // on end_week, raised by
                                                       // Raise Control spell

    word commission;                                   // gold per week
    dword gold;

    word score;                                        // unused by game, saved

    byte scepter_continent;                            // XOR-encoded on disk
    byte scepter_y;                                    // XOR-encoded on disk
    byte scepter_x;                                    // XOR-encoded on disk
    byte scepter_key;                                  // XOR key

    byte fog[MAX_CONTINENTS][LEVEL_H][LEVEL_W];        // [4][64][64] 0/1
    byte map[MAX_CONTINENTS][LEVEL_H][LEVEL_W];        // [4][64][64] tile ids

    byte unknown1;                                     // reserved; preserved
    byte unknown2;                                     // reserved; preserved
    byte unknown3;                                     // reserved; preserved
};
```

**`options[]` meaning** (set at spawn, persisted, mutated by the
Controls menu):

```
options[0] = delay        0..9     default 4     animation frame delay
options[1] = sounds       0 or 1   default 1
options[2] = walk beep    0 or 1   default 1
options[3] = animation    0 or 1   default 1
options[4] = army size    0 or 1   default 1     draw troop counts in combat
options[5] = cga palette  0..7     default 1     only shown in CGA builds
```

**`mount` transitions:**
- Default mount is `KBMOUNT_RIDE` (8).
- Sailing requires renting or boarding a boat. Entering water with
  `boat_x,boat_y` equal to destination and `boat==continent` flips
  mount to `KBMOUNT_SAIL`. Leaving water flips back to `KBMOUNT_RIDE`
  and drops the boat at the last water tile.
- Flight is enabled by Fly spell (adventure) and only if every
  player troop can fly AND has `skill_level >= 2` (see §15). Flight
  spell isn't actually wired; cheat 'f' forces `KBMOUNT_FLY`.

**`castle_owner[i]` byte semantics** (from play.c, game.c):
- `0xFF` — player
- `0x7F` — generic monsters (no villain)
- `0x00..0x10` — villain id (low 5 bits, `KBCASTLE_VILLAIN` mask)
- bit `0x40` (`KBCASTLE_KNOWN`) — Find Villain has revealed which
  castle holds the contracted villain

When reading the "who owns it?" a player receives either the
monsters path or the villain path depending on the mask.

### 4.2 Field-by-field semantics

#### 4.2.1 Identity / save metadata

- **`savefile[64]`** — the file basename (with `.DAT` extension)
  used when saving. Built from uppercased `name` in `spawn_game`.
  When loaded, set to the filename the player chose. Not
  serialized in the save file (the filename is derived from the
  on-disk name).

- **`name[11]`** — player name, up to 10 chars + null. Stored
  on disk as 10 bytes padded with spaces (no null), then 1 byte
  of padding (the 11th byte of the 11-byte alignment slot).
  Loaded by stripping trailing spaces and null-terminating. The
  initial uppercase of the first letter is forced by
  `name[0] = toupper(name[0])` in `create_game` (game.c:471).

#### 4.2.2 Character

- **`class`** — 0..3, set at character creation, never changes.
  Selects starting army, gold, and class progression
  (`classes[class][rank]`).

- **`rank`** — 0..3, starts at 0. Each promotion (audience with
  king when villains_needed met) increments by 1 and applies
  the next rank's bonuses additively. Max rank is 3 (Lord,
  Champion, Archmage, Overlord).

- **`mount`** — current movement mode:
  - 0 = `KBMOUNT_SAIL` (on boat)
  - 4 = `KBMOUNT_FLY`
  - 8 = `KBMOUNT_RIDE` (default)

  Transitions:
  - SAIL → RIDE: stepping onto land from water automatically
    flips and drops the boat.
  - RIDE → SAIL: stepping onto a tile with `boat_x,boat_y,boat`
    matching position (boarding a boat).
  - RIDE → FLY: pressing F when `player_can_fly()` returns true
    (every troop in army is a flyer with skill ≥ 2).
  - FLY → RIDE: pressing L when on a grass tile.
  - SAIL ↔ FLY: not possible (must vacate boat first).

  Also drives sprite-sheet rendering: `(mount + frame)` indexes
  the hero strip.

- **`difficulty`** — 0 (Easy) to 3 (Impossible?). Selects
  `days_per_difficulty[difficulty]` = {900, 600, 400, 200} as
  the initial `days_left`. Applies a score multiplier
  (×0.5, ×1, ×2, ×4). Cannot be changed after creation.

- **`options[6]`** — see §35.7 for save layout. Mutable via
  Controls menu (§23.5).

- **`spell_power`** — accumulator. Increments from class ranks,
  +1 from chest events, ×2 from Amulet pickup, +1 from "M" cheat.
  Multiplies spell base damage.

- **`max_spells`** — accumulator. Increments from class ranks,
  +N from chest "max spells" event, ×2 from Ring pickup. Cap on
  total spells held (`sum(spells[i]) <= max_spells`).

#### 4.2.3 Position

- **`continent`** — 0..3, current continent.

- **`x, y`** — current map position, 0..63 each.

- **`last_x, last_y`** — previous map position, used for:
  - Movement reversal when an interaction blocks the move (e.g.
    entering an unsiegable castle without the contract).
  - Foe-follow target (foes step toward `last_x, last_y`, not
    `x, y`).
  - Boat-drop on shore (boat lands at `last_x, last_y`).

  Updated as the first step of every successful move (before
  `x, y` are updated to new tile).

- **`boat_x, boat_y, boat`** — the rented boat's location.
  - `boat_x, boat_y` — pixel-position of the boat sprite when
    visible.
  - `boat` — continent index where the boat exists, or `0xFF`
    if no boat is rented. While `mount == SAIL`, boat tracks
    player position. While walking, boat stays at its dropped
    location.

#### 4.2.4 Quest progress flags

- **`contract`** — current contracted villain id (0..16) or
  `0xFF` (none). Replaced (not added to) when player accepts a
  new contract. Only one active contract at a time.

- **`siege_weapons`** — 0 or 1. Flag indicating siege weapons
  purchased. Note: not enforced anywhere as a precondition for
  attacking castles (§34.14). Reset to 0 on temp_death (loss).

- **`knows_magic`** — 0 or 1. Set automatically for Sorceress
  (class 2 rank 0); other classes must visit Aurange's Alcove
  and pay 5000 gold. Required to use Use Magic command.

- **`contract_cycle[5]`** — five villain ids that towns will
  offer as contracts. Initial: {0, 1, 2, 3, 4}. After capturing
  a villain, that slot is filled with the next uncaught one
  starting from `max_contract`.

- **`last_contract`** — circular index 0..4 into `contract_cycle`.
  Increments when player accepts a contract; wraps 0..4.

- **`max_contract`** — advancing pointer 5..16. Tracks "next
  villain to offer". Increments each time a contract is
  fulfilled and a slot is filled.

#### 4.2.5 Inventory

- **`artifact_found[8]`** — boolean per artifact. Set on pickup
  (which also applies the artifact's power if instant — see §9).

- **`villain_caught[17]`** — boolean per villain. Set when
  contract fulfilled.

- **`continent_found[4]`** — boolean per continent; means
  "player has the navmap to sail there". Initial: only
  `continent_found[0]` is true. Set by treasure-chest navmap
  pickup or by 'N' cheat.

- **`orb_found[4]`** — boolean per continent; means "player has
  the magical orb to view the whole continent". Set by orb-chest
  pickup. Toggles the minimap full-vs-fog view.

- **`spells[14]`** — count per spell. Starts at 0. Increments
  via town purchase. `sum(spells)` capped at `max_spells`.
  Decremented when spell cast.

#### 4.2.6 Visit history (for Castle/Town Gate spells)

- **`town_visited[26]`** — boolean per town. Set on first visit
  (entering town tile). Used by Town Gate spell to filter
  available destinations. Indexed by town letter A..Z (via
  `town_inversion[]`).

- **`castle_visited[26]`** — boolean per castle. Set on first
  visit (whether siege or own-castle). Used by Castle Gate spell.

- **`town_spell[26]`** — spell id sold by each town. Set at
  game creation by `salt_spells`. Hunterville (town 0x15) is
  hardcoded to sell Bridge (spell 7); other towns get random
  unique assignments.

#### 4.2.7 World map and dwellings

- **`teleport_coords[4][2][2]`** — `[continent][slot][x|y]`.
  Two paired teleport-cave coords per continent. Stepping onto
  one warps the player to the other.

- **`dwelling_coords[4][11][2]`** — `[continent][slot][x|y]`.
  Up to 11 dwellings per continent. Slot 11 is reserved
  (terminator). Each dwelling has a fixed troop type and
  population.

- **`dwelling_troop[4][11]`** — troop id for each dwelling
  (Peasants, Sprites, etc.).

- **`dwelling_population[4][11]`** — current available count.
  Decreased on recruit; reset to `troops[id].max_population`
  on the troop's astrology week (every 25th week max).

#### 4.2.8 Castles

- **`castle_owner[26]`** — see §3.4 byte semantics.

- **`castle_troops[26][5]`** — garrison troop ids.

- **`castle_numbers[26][5]`** — garrison counts (word). Larger
  values than 255 are stored as words but truncated to bytes
  for foe garrisons (see §22.15).

#### 4.2.9 Continent meta-objects

- **`map_coords[4][2]`** — `[continent][x|y]`. Position of the
  navmap chest on each continent. Only set for continents 0..2;
  cont 3 has no navmap chest (no continent beyond it). On disk,
  only 3 entries are stored (see §22.12).

- **`orb_coords[4][2]`** — `[continent][x|y]`. Position of the
  orb chest. All 4 continents have an orb.

- **`foe_coords[4][40][2]`** — `[continent][slot][x|y]`. Up to
  40 foes per continent. First 5 slots are friendly foes; rest
  are hostile. Each slot's tile is the world-map tile at
  (foe_x, foe_y); when a foe is killed, the tile is cleared
  to grass and the slot is permanently dead.

- **`foe_troops[4][40][5]`** — troop ids per foe. Only 3 of 5
  used in practice (foes have 3-stack armies).

- **`foe_numbers[4][40][5]`** — counts (word). Same 3-stack
  limit. On disk, only the hostile slots (5..39) are stored,
  and as bytes (so values > 255 lose precision; see §22.15).

#### 4.2.10 Time and economy

- **`steps_left`** — 0..40. Decrements on each successful
  movement step. When 0, `end_day` fires and resets to 40.
  Time Stop spell suspends decrement.

- **`time_stop`** — non-zero suspends day cost. Set by Time Stop
  spell (`time_stop += spell_power * 10`); decrements each step.
  Reset to 0 by `end_day` and `end_week`.

- **`days_left`** — word. Initial value `days_per_difficulty[diff]`.
  Decremented at end of each day. Reaching 0 triggers `lose_game`.

- **`base_leadership`** — word. Total leadership cap from class
  ranks + Crown of Command (×2) + 'V' cheat (+100) + chest
  events. Never decreases except via cheat.

- **`leadership`** — word. Current cap. Reset to `base_leadership`
  at end of each week. Modified by:
  - Raise Control spell: `leadership += spell_power * 100`.
  - Treasure chest "leadership" option: variable amount, with
    Crown of Command doubling.
  - Both raise above `base_leadership` until next end_week.

- **`commission`** — word. Weekly stipend in gold. Increments
  by class rank, by Articles of Nobility (+2000), by chest
  events. Adds to `gold` at end of each week.

- **`gold`** — dword. Player's coin. Increments from villain
  bounties, chest events, combat spoils, weekly commission.
  Decrements from purchases (recruit, spell, alcove, siege,
  boat) and weekly army upkeep. Cap is 32-bit unsigned (~4.2
  billion). UI displays as "GP=Nk" (truncated thousands).

#### 4.2.11 Score

- **`followers_killed`** — word. Cumulative count of friendly
  troops lost in combat. Penalizes score (1 per kill).

- **`score`** — word. Computed by `player_score()` but stored
  for save format compatibility; never read by the game itself.
  See §14.2 for formula.

#### 4.2.12 Scepter (encrypted on disk)

- **`scepter_continent`** — byte 0..3.
- **`scepter_y`** — byte 0..63.
- **`scepter_x`** — byte 0..63.
- **`scepter_key`** — byte 0..255, XOR key.

In memory, the three coord values are plain. On disk, each is
XOR'd with `scepter_key` before write, and XOR'd back on read.
The key is stored adjacent so that reading and re-applying XOR
restores the original (XOR is self-inverse).

The bug `bury_scepter` (§14.3) sets `scepter_y = i` instead of
`= j`, so the y coordinate is always equal to the x coordinate.
This makes find-scepter via Search effectively impossible.

#### 4.2.13 Fog of war

- **`fog[4][64][64]`** — bool per tile. Set in 5×5 around player
  on each successful move (`clear_fog`). Used by minimap to
  draw revealed-vs-unrevealed pixels. Without orb_found, the
  minimap shows only revealed tiles. Persisted in save.

#### 4.2.14 Map data

- **`map[4][64][64]`** — tile id per tile. The whole world,
  with all interactive tiles (foes, dwellings, chests,
  artifacts, signs, castles, towns) embedded as their tile
  ids (0x80..0x93). Modified throughout play: foe-kill clears
  to 0; boats drop on water; bridges built; chests/artifacts
  picked clear to 0.

  Loaded at start from `LAND.ORG` (16384 bytes), then mutated
  by `salt_continent` to embed dwelling-types and friendly foe
  tiles based on what kind of object lands at each chest tile.

#### 4.2.15 Reserved fields

- **`unknown1, unknown2, unknown3`** — three bytes preserved
  from DOS save format. openkb reads and writes them but
  doesn't consume their values. They appear at:
  - `unknown3` after `steps_left` (offset 0x78).
  - `unknown1, unknown2` after the score (offset 0xFBF, 0xFC0).

  These were likely struct-padding or features the original DOS
  game implemented differently.

### 4.3 Allocation lifecycle

```
spawn_game(name, class, diff, land):
    game = malloc(sizeof KBgame)
    memset(game, 0, sizeof KBgame)
    memcpy(game->map, land, 4*64*64)
    ... initialize all fields per §14 ...
    return game

KB_loadDAT(filename):
    f = fopen(filename, "rb")
    fread(buf, 1, 20421, f)
    game = malloc(sizeof KBgame)
    /* fields zeroed implicitly via reads from buf */
    ... read every field from buf via READ_BYTE/WORD/DWORD ...
    return game

KB_saveDAT(filename, game):
    f = fopen(filename, "wb")
    write all fields into a 20421-byte buf
    fwrite(buf, 1, 20421, f)

(no free; game leaked at exit by design — process termination)
```

The game is never freed; openkb relies on process termination
to release. This is fine for a single-game session but means
running `run_game()` in a loop would leak.

### 4.4 Field constraints / invariants

A clean-room implementation must maintain:

- `class < 4`, `rank < 4`, `difficulty < 4`.
- `mount ∈ {0, 4, 8}`.
- For all i: `player_troops[i] == 0xFF || player_troops[i] < 25`.
- For all i: if `player_numbers[i] == 0`, then
  `player_troops[i] == 0xFF`.
- `0 <= continent < 4`, `0 <= x, y < 64`.
- `last_x, last_y` define a valid tile (typically the previous
  position, or current position at game start).
- `boat ∈ {0..3, 0xFF}`.
- `contract ∈ {0..16, 0xFF}`.
- For all i: `0 <= contract_cycle[i] <= 16 || contract_cycle[i]
  == 0xFF`.
- `last_contract < 5`.
- `max_contract <= 17`.
- For all i: `villain_caught[i] ∈ {0, 1}`.
- For all i: `artifact_found[i] ∈ {0, 1}`.
- `siege_weapons, knows_magic ∈ {0, 1}`.
- `0 <= sum(spells) <= max_spells`.
- For all c, t: `dwelling_population[c][t] <=
  troops[dwelling_troop[c][t]].max_population`.
- For all c: `castle_owner[c] ∈ {0..16, 0x40..0x50, 0x7F, 0xFF}`.
- `steps_left <= DAY_STEPS = 40`.
- `days_left <= days_per_difficulty[difficulty]`.
- `leadership` may exceed `base_leadership` temporarily but
  resets at end_week.

These are not enforced by openkb but a strict implementation
should validate. Save file data may be corrupt or hand-edited;
loaders should clamp or reject.

### 4.5 Equality and copy

`KBgame` has no comparison or copy semantics — it's not used as
a value type. There's no clone operator; saves are produced
by serializing field-by-field via `KB_saveDAT`.

### 4.6 Memory layout

Padding may differ between compilers/architectures. The save
file format is **not** a `fwrite(game, sizeof game, 1, f)` — it's
field-by-field with explicit byte order, so portable across
platforms regardless of struct padding.

The in-memory layout is roughly:
- 11 bytes of name (followed by potentially 1 byte padding to
  align next field)
- 1 byte each: class, rank, mount, difficulty
- 6 bytes options
- 2 bytes spell_power, max_spells
- 1+1+1 bytes continent, y, x
- 2+2 bytes last_y, last_x
- 2+2 bytes boat_y, boat_x
- 1+1+1+1 bytes contract, siege_weapons, knows_magic, boat
- 5 bytes contract_cycle
- 1+1 bytes last_contract, max_contract
- 8 bytes artifact_found
- 17 bytes villain_caught
- 4+4 bytes continent_found, orb_found
- 14 bytes spells
- 26 bytes town_visited
- 26 bytes castle_visited
- 26 bytes town_spell
- 16 bytes teleport_coords (4 × 2 × 2)
- 88 bytes dwelling_coords (4 × 11 × 2)
- 11 bytes dwelling_troop[4][11]... actually 44 (4 × 11)
- 44 bytes dwelling_population
- 26 bytes castle_owner
- 130 bytes castle_troops
- 260 bytes castle_numbers (130 word)
- 8 bytes map_coords
- 8 bytes orb_coords
- 320 bytes foe_coords (4 × 40 × 2)
- 800 bytes foe_troops (4 × 40 × 5)
- 1600 bytes foe_numbers (4 × 40 × 5 word)
- 1 byte steps_left
- 2+2 bytes time_stop, days_left
- 5 bytes player_troops
- 10 bytes player_numbers (word)
- 2+2+2+4+2 bytes followers_killed, base_leadership,
  leadership, gold, commission
- 2 bytes score
- 1+1+1 bytes scepter coords (encrypted in save)
- 1 byte scepter_key
- 16384 bytes fog (when stored in memory, 1 byte per tile)
- 16384 bytes map
- 3 bytes reserved (unknown1, 2, 3)

Total: roughly 22000 bytes plus padding.

## 5. Troop catalog — all 25 units

All 25 troops are defined in `bounty.c:46` as a static array
`troops[MAX_TROOPS]`. The order is stable and the index is the
troop id used everywhere (player armies, dwellings, castle
garrisons, foe encounters, asset lookup).

### 5.1 `KBtroop` struct (`bounty.h:204`)

```c
typedef struct KBtroop {
    char  name[16];          // up-to-15-char string + null
    byte  skill_level;       // 1..6, attack/defense in damage formula
    byte  hit_points;        // 1..200, HP per single creature
    byte  move_rate;         // 1..4, squares per combat turn
    byte  melee_min;         // 1..25, min melee damage per attacker
    byte  melee_max;         // 1..50, max melee damage per attacker
    byte  ranged_min;        // 0..25, min ranged damage (0 = none)
    byte  ranged_max;        // 0..50, max ranged damage (0 = fixed)
    byte  ranged_ammo;       // 0..24, total shots per match
    word  recruit_cost;      // 10..5000, gold per unit
    word  spoils_factor;     // 1..500, gold spoils per kill
    byte  abilities;         // bit-or of ABIL_* flags
    byte  dwells;            // dwelling kind (0..4)
    byte  max_population;    // 0..250, dwelling population cap
    byte  growth;            // 1..6, units added per growth week
    byte  morale_group;      // morale group A..E (0..4)
} KBtroop;
```

The struct totals 27 bytes per troop × 25 = 675 bytes. Plus the
16-byte name buffer makes for ~52 bytes per row in C. Inline at
program start; never allocated.

### 5.2 Field-by-field semantics

#### 5.2.1 `name[16]`

ASCII string, null-terminated, up to 15 chars. Used in:
- View Army screen (§23.13) per row.
- View Character (`recruit_soldiers` row).
- Combat status bar (`<TroopName> Move/Shoot/etc`).
- Combat log messages ("<Troop> vs <Other>, N die").

The name is hardcoded but **module-overridable**: at game
creation time, `refill_names()` (game.c:215) calls
`KB_Resolve(STRL_TROOPS, 0)`. If the active module returns a
non-NULL strlist of 25 names, those replace the names in the
`troops[]` table. DOS module returns NULL (uses defaults from
`bounty.c`); GNU module reads from `troops.ini`.

#### 5.2.2 `skill_level` (SL)

1..6. Used in melee/ranged damage formula:
```
skill_diff = attacker.skill_level + 5 - target.skill_level
final_damage = (total * skill_diff) / 10
```

The `+5` baseline means same-skill matchup gives `skill_diff = 5`,
i.e. `total * 0.5`. Higher skill is better; the spread is
1..11 over 1-vs-6 matchups.

Skill also gates flight: `player_can_fly` requires `skill_level
>= 2` for all troops in the army (Sprites SL=1 are excluded).

#### 5.2.3 `hit_points` (HP)

1..200 per single creature. Used in:
- Damage application: `kills = damage / target_hp`,
  `injury = damage % target_hp`.
- Leadership cost: each unit consumes `hp` leadership (so
  Dragons at 200 HP eat 200 from your cap each).
- Recruit limit: `max = army_leadership(troop_id) / hp`.

Dragons (200 HP) are by far the toughest; Peasants/Sprites
(1 HP) the weakest.

#### 5.2.4 `move_rate` (MV)

1..4. Squares of movement per combat turn. Set at the start of
each turn:
```
units[s][i].moves = troops[id].move_rate
```

Each `move_unit` call decrements `moves`; when 0, `acted = 1`
(unit done). Wolves/Cavalry (MV=4) and Cavalry (MV=4) move
fastest; Knights/Ogres/Trolls/Demons/Dragons (MV=1) the
slowest.

#### 5.2.5 `melee_min, melee_max`

Min/max damage per attacking creature, rolled per attack:
```
dmg = KB_rand(melee_min, melee_max)
total = dmg * attacker_count
```

So a stack of 100 Peasants does roll-of-1 × 100 = 100 base
damage per swing; Dragons roll 25..50 × count.

For the `ABIL_MAGIC` ranged-attackers (Druids, Archmages), the
ranged path uses fixed `ranged_min` (10 / 25). For non-magic
ranged, `KB_rand(ranged_min, ranged_max)`.

#### 5.2.6 `ranged_min, ranged_max, ranged_ammo`

`ammo` = 0 means no ranged attack. With ammo:
- `ranged_min, ranged_max` define damage roll range (or fixed
  for MAGIC troops).
- `ammo` = total shots per **combat match** (not per turn).

`KBunit.shots` is initialized to `ammo` at `reset_match`.
Each ranged shot decrements `shots`. Cannot shoot while
`unit_surrounded` (any enemy in adjacent tile).

Notable values:
- Orcs: 1-2 dmg, 10 ammo.
- Archers: 1-3 dmg, 12 ammo.
- Elves: 2-4 dmg, 24 ammo.
- Druids: 10 dmg fixed (MAGIC), 3 ammo.
- Archmages: 25 dmg fixed (MAGIC), 2 ammo.
- Giants: 5-10 dmg, 6 ammo.

#### 5.2.7 `recruit_cost`

Gold cost per unit for recruitment:
- At dwellings: pay full cost.
- At home castle barracks: pay full cost.
- For weekly upkeep: `(numbers * recruit_cost) / 10` per slot
  (so weekly cost is 10% of the army's gold value).
- For spoils: combat reward per dead enemy = `spoils_factor * 5`,
  not directly tied to recruit_cost.

#### 5.2.8 `spoils_factor`

Coefficient for gold rewards. After each combat:
```
spoils[side] += troops[id].spoils_factor * 5 * count
```
Then on victory, `gold += spoils[1]` (AI side).

A typical match against 100 Wolves: `4 * 5 * 100 = 2000` gold.
Killing a Dragon: `500 * 5 * 1 = 2500` gold per dragon.

#### 5.2.9 `abilities`

Bitmask of `ABIL_*` flags:
```
0x01 FLY    — flying in combat (2 flight points/turn)
0x02 REGEN  — heal injury at end of turn
0x04 MAGIC  — fixed ranged damage; cancels on IMMUNE target
0x08 IMMUNE — immune to magic damage and freeze
0x10 ABSORB — kills add to attacker count (ghosts)
0x20 LEECH  — kills heal attacker (vampires)
0x40 SCYTHE — 10% chance to halve target stack (demons)
0x80 UNDEAD — Turn Undead spell affects, morale group E
```

See §17.5 for damage formula application of each.

#### 5.2.10 `dwells`

Dwelling kind hosting this troop:
- 0 PLAINS  — Peasants, Wolves, Nomads, Barbarians, Archmages
- 1 FOREST  — Sprites, Gnomes, Elves, Trolls, Druids
- 2 HILL    — Orcs, Dwarves, Ogres, Giants, Dragons
- 3 DUNGEON — Skeletons, Zombies, Ghosts, Vampires, Demons
- 4 CASTLE  — Militia, Archers, Pikemen, Knights, Cavalry

The 5 castle troops are recruited only at the home castle.
The 20 non-castle troops can appear in dwellings on the world
map (placed by `salt_continent` per `continent_dwellings[]`).

#### 5.2.11 `max_population`

Initial / refresh count for a dwelling holding this troop. Set
on first salt and reset each end-of-week when this troop is
the astrology week's creature. Castle troops have 0 (they're
recruited at the home castle, not from dwellings). Knights
(250) and Peasants (250) have the highest; Ghosts/Trolls/
Archmages (25), Demons/Dragons (25) the lowest.

#### 5.2.12 `growth`

Units added to dwelling/foe/castle stacks each end-of-week
when this troop is the astrology pick. Range 1..6.
Peasants/Sprites (6) grow fastest; Demons/Dragons (1) slowest.

Specifically, end_week applies (per §16.6):
```
for each foe with troop == creature: foe_numbers += growth
for each non-player castle with troop == creature:
    castle_numbers += growth
for each dwelling with troop == creature:
    dwelling_population = max_population (full reset)
```

Player castles **don't** grow this way (they grow only when
empty, via `repopulate_castle`).

#### 5.2.13 `morale_group`

Group A..E (0..4). See §3.6 for assignments and §8.3 for the
chart.

### 5.3 Per-troop notes

Per-troop tactical and design observations:

- **Peasants (0x00)** — Cheapest, weakest, most numerous. The
  end_week ghost-conversion turns Ghosts into Peasants on
  Peasants weeks (every 4th week — `creature == 0` check). Used
  as the default fallback troop.

- **Sprites (0x01)** — Cheap flier. SL=1 disqualifies them from
  enabling overworld flight. Cannot retaliate (HP=1 means any
  hit kills the whole stack instantly).

- **Militia (0x02)** — Castle-only. SL=2, HP=2. The default
  starting troop for Knight class.

- **Wolves (0x03)** — Fastest non-flying troop (MV=3). Plains
  dwelling. Very cheap (40 gp).

- **Skeletons / Zombies / Ghosts** (0x04, 0x05, 0x0D) — Undead.
  Affected by Turn Undead spell. Skeletons fast, Zombies hardy,
  Ghosts absorb kills (grow stack).

- **Gnomes (0x06)** — Forest dwelling. SL=2, HP=5. Average all
  around.

- **Orcs (0x07)** — First troop with ranged ability (1-2 dmg,
  10 ammo). Hill dwelling.

- **Archers (0x08)** — Castle ranged (1-3 dmg, 12 ammo). HP=10
  makes them durable. Recruit cost 250.

- **Elves (0x09)** — Forest ranged (2-4 dmg, 24 ammo). SL=3
  makes their damage strong.

- **Pikemen (0x0A)** — Castle melee. HP=10, 2-4 dmg. Cost 300.

- **Nomads (0x0B)** — Plains, MV=2. HP=15.

- **Dwarves (0x0C)** — Hill, HP=20. Slow (MV=1).

- **Ghosts (0x0D)** — Undead, ABSORB ability. Absorb adds to
  count. End-of-week on Peasants weeks: Ghosts → Peasants.

- **Knights (0x0E)** — Castle elite. HP=35, 6-10 dmg, SL=5.
  Cost 1000.

- **Ogres (0x0F)** — Hill brutes. HP=40, slow.

- **Barbarians (0x10)** — Plains, MV=3, HP=40.

- **Trolls (0x11)** — Forest, REGEN heals injury. HP=50.

- **Cavalry (0x12)** — Castle, fastest (MV=4). HP=20.

- **Druids (0x13)** — Forest, MAGIC ranged (10 fixed dmg,
  3 ammo). SL=5.

- **Archmages (0x14)** — Plains, MAGIC + FLY. 25 dmg fixed
  ranged, 2 ammo. Plains dwelling. SL=5.

- **Vampires (0x15)** — Dungeon, LEECH + FLY + UNDEAD. Drain HP
  from kills.

- **Giants (0x16)** — Hill, 10-20 dmg, 5-10 ranged.

- **Demons (0x17)** — Dungeon, FLY + SCYTHE. 10% chance per
  attack to halve target stack.

- **Dragons (0x18)** — Hill, FLY + IMMUNE to magic. 25-50 dmg.
  Most expensive (5000 gp).

### 5.4 Module override path

At game creation, `refill_rules()` (game.c:248) overwrites every
field in the `troops[]` table from module-resolved data:

```
DAT_SKILLS    → skill_level
DAT_HPS       → hit_points
DAT_MOVES     → move_rate
DAT_MELEEMIN  → melee_min
DAT_MELEEMAX  → melee_max
DAT_RANGEMIN  → ranged_min
DAT_RANGEMAX  → ranged_max
DAT_SHOTAMMO  → ranged_ammo
DAT_GCOST     → recruit_cost
DAT_SPOILS    → spoils_factor
DAT_ABILS     → abilities
DAT_DWELLS    → dwells
DAT_MAXPOP    → max_population
DAT_GROWTH    → growth
DAT_MGROUP    → morale_group
```

DOS module returns NULL for all of these (defaults stand). GNU
module returns values from `troops.ini`.

A clean-room implementation needs:
- The hardcoded table from §5.5 (or equivalent INI), AND
- Optional override hooks for each field.

### 5.5 Master troop table (decimal CSV)

Reproducing the data in §5 in CSV form, indexed by id. Values
verified against `bounty.c:46` (see §35.42):

```
id  Name        SL  HP  MV  Mmin Mmax Rmin Rmax Ammo Cost Spoil Abil  Dwell MaxP Grow Mgrp
00  Peasants    1   1   1   1    1    0    0    0    10   1     0x00  0     250  6    0
01  Sprites     1   1   1   1    2    0    0    0    15   1     0x01  1     200  6    2
02  Militia     2   2   2   1    2    0    0    0    50   5     0x00  4     0    5    0
03  Wolves      2   3   3   1    3    0    0    0    40   4     0x00  0     150  5    3
04  Skeletons   2   3   2   1    2    0    0    0    40   4     0x80  3     150  5    4
05  Zombies     2   5   1   2    2    0    0    0    50   5     0x80  3     100  5    4
06  Gnomes      2   5   1   1    3    0    0    0    60   6     0x00  1     250  5    2
07  Orcs        2   5   2   2    3    1    2    10   75   7     0x00  2     200  5    3
08  Archers     2   10  2   1    2    1    3    12   250  25    0x00  4     0    5    1
09  Elves       3   10  3   1    2    2    4    24   200  20    0x00  1     100  4    2
0A  Pikemen     3   10  2   2    4    0    0    0    300  30    0x00  4     0    4    1
0B  Nomads      3   15  2   2    4    0    0    0    300  30    0x00  0     150  4    2
0C  Dwarves     3   20  1   2    4    0    0    0    350  30    0x00  2     100  4    2
0D  Ghosts      4   10  3   3    4    0    0    0    400  40    0x90  3     25   3    4
0E  Knights     5   35  1   6    10   0    0    0    1000 100   0x00  4     250  3    1
0F  Ogres       4   40  1   3    5    0    0    0    750  75    0x00  2     200  3    3
10  Barbarians  4   40  3   1    6    0    0    0    750  75    0x00  0     100  3    2
11  Trolls      4   50  1   2    5    0    0    0    1000 100   0x02  1     25   3    3
12  Cavalry     4   20  4   3    5    0    0    0    800  80    0x00  4     0    2    1
13  Druids      5   25  2   2    3    10   0    3    700  70    0x04  1     25   2    2
14  Archmages   5   25  1   2    3    25   0    2    1200 120   0x05  0     25   2    2
15  Vampires    5   30  1   3    6    0    0    0    1500 150   0xA1  3     50   2    4
16  Giants      5   60  3   10   20   5    10   6    2000 200   0x00  2     50   2    2
17  Demons      6   50  1   5    7    0    0    0    3000 300   0x41  3     25   1    4
18  Dragons     6   200 1   25   50   0    0    0    5000 500   0x09  2     25   1    3
```

(Rmax of 0 with Rmin > 0 indicates MAGIC fixed-damage attacks.)

The earlier-presented table in §5 (above) preserves the
graphical layout with separate ability columns; the CSV above
is the authoritative source-faithful form.

### 5.6 Ability flag explanations

Detailed per-ability behavior is in §17.5 (damage formula). A
brief list of where each ability is checked in source:

| Flag       | Hex | Source location                              |
|------------|-----|----------------------------------------------|
| FLY        | 01  | reset_turn, unit_try_fly, ai_unit_think,     |
|            |     | grid_heuristic, player_can_fly                |
| REGEN      | 02  | reset_turn (zeros injury at side==1)          |
| MAGIC      | 04  | deal_damage (fixed ranged, IMMUNE check)      |
| IMMUNE     | 08  | deal_damage (cancel ranged), magic_damage,    |
|            |     | freeze_troop                                  |
| ABSORB     | 10  | deal_damage (count += kills), end_week        |
|            |     | (creature 0 → conversion to Peasants)         |
| LEECH      | 20  | deal_damage (count += dmg/hp, clamp max_count)|
| SCYTHE     | 40  | deal_damage (10% chance halve target)         |
| UNDEAD     | 80  | magic_damage (filter for Turn Undead)         |

### 5.7 Skill level distribution

```
SL 1: Peasants, Sprites
SL 2: Militia, Wolves, Skeletons, Zombies, Gnomes, Orcs, Archers
SL 3: Elves, Pikemen, Nomads, Dwarves
SL 4: Ghosts, Ogres, Barbarians, Trolls, Cavalry
SL 5: Knights, Druids, Archmages, Vampires, Giants
SL 6: Demons, Dragons
```

Higher SL ⇒ more damage in formula. Best for offense: Demons,
Dragons. Best for defense (skill_diff against attacker): same.

### 5.8 Move rate distribution

```
MV 1: Zombies, Gnomes, Dwarves, Knights, Ogres, Trolls, Archmages,
      Vampires, Demons, Dragons (10 troops)
MV 2: Militia, Skeletons, Orcs, Archers, Pikemen, Nomads, Druids
      (7 troops)
MV 3: Peasants is MV=1 (wait, double-check)
```

Re-verifying: from §5.5 CSV, MV=1 for: Peasants(00), Sprites(01),
Zombies(05), Gnomes(06), Dwarves(0C), Knights(0E), Ogres(0F),
Trolls(11), Archmages(14), Vampires(15), Demons(17), Dragons(18).

```
MV 1: 00 Peasants, 01 Sprites, 05 Zombies, 06 Gnomes,
      0C Dwarves, 0E Knights, 0F Ogres, 11 Trolls,
      14 Archmages, 15 Vampires, 17 Demons, 18 Dragons
MV 2: 02 Militia, 04 Skeletons, 07 Orcs, 08 Archers,
      0A Pikemen, 0B Nomads, 13 Druids
MV 3: 03 Wolves, 09 Elves, 0D Ghosts, 10 Barbarians, 16 Giants
MV 4: 12 Cavalry
```

Cavalry alone has MV=4; no troop has MV>4.

### 5.9 HP distribution

Sorted: 1, 1, 2, 3, 3, 5, 5, 5, 10, 10, 10, 10, 15, 20, 20, 25,
25, 30, 35, 40, 40, 50, 50, 60, 200.

Median ~10. Mean ~24 (skewed by Dragons at 200).

### 5.10 Total cost distribution

Recruit costs increase roughly geometrically with HP/skill.

```
1-100:    Peasants(10), Sprites(15), Wolves(40), Skeletons(40),
          Militia(50), Zombies(50), Gnomes(60), Orcs(75)
100-500:  Elves(200), Archers(250), Nomads(300), Pikemen(300),
          Dwarves(350), Ghosts(400)
500-1000: Druids(700), Ogres(750), Barbarians(750),
          Cavalry(800), Knights(1000), Trolls(1000)
1000+:    Archmages(1200), Vampires(1500), Giants(2000),
          Demons(3000), Dragons(5000)
```

Dragons at 5000 gp/unit are 500× the cost of Peasants.

- **SL** — skill level (attack/defense modifier in damage formula)
- **HP** — hit points per 1 creature
- **MV** — move rate (squares per combat turn)
- **M-min/M-max** — melee damage range per attacker unit
- **R-min/R-max** — ranged damage range per attacker unit (0 if none)
- **Ammo** — ranged shots per match (0 if none)
- **G-Cost** — recruit cost in gold (per unit)
- **Spoils** — spoils factor, multiplied by 5× count for gold reward
- **Abilities** — bit-or of `ABIL_*`
- **Dwells** — which dwelling kind hosts this troop
- **MaxPop** — dwelling population ceiling for this troop
- **Growth** — unit-count added during its astrology week
- **MGroup** — morale group A..E (0..4)

Id 0x00..0x18:

| id   | Name       | SL | HP  | MV | M-min | M-max | R-min | R-max | Ammo | G-Cost | Spoils | Abilities                     | Dwells  | MaxPop | Growth | MGroup |
|------|------------|----|-----|----|-------|-------|-------|-------|------|--------|--------|-------------------------------|---------|--------|--------|--------|
| 0x00 | Peasants   | 1  | 1   | 1  | 1     | 1     | 0     | 0     | 0    | 10     | 1      | —                             | Plains  | 250    | 6      | A      |
| 0x01 | Sprites    | 1  | 1   | 1  | 1     | 2     | 0     | 0     | 0    | 15     | 1      | FLY                           | Forest  | 200    | 6      | C      |
| 0x02 | Militia    | 2  | 2   | 2  | 1     | 2     | 0     | 0     | 0    | 50     | 5      | —                             | Castle  | 0      | 5      | A      |
| 0x03 | Wolves     | 2  | 3   | 3  | 1     | 3     | 0     | 0     | 0    | 40     | 4      | —                             | Plains  | 150    | 5      | D      |
| 0x04 | Skeletons  | 2  | 3   | 2  | 1     | 2     | 0     | 0     | 0    | 40     | 4      | UNDEAD                        | Dungeon | 150    | 5      | E      |
| 0x05 | Zombies    | 2  | 5   | 1  | 2     | 2     | 0     | 0     | 0    | 50     | 5      | UNDEAD                        | Dungeon | 100    | 5      | E      |
| 0x06 | Gnomes     | 2  | 5   | 1  | 1     | 3     | 0     | 0     | 0    | 60     | 6      | —                             | Forest  | 250    | 5      | C      |
| 0x07 | Orcs       | 2  | 5   | 2  | 2     | 3     | 1     | 2     | 10   | 75     | 7      | —                             | Hill    | 200    | 5      | D      |
| 0x08 | Archers    | 2  | 10  | 2  | 1     | 2     | 1     | 3     | 12   | 250    | 25     | —                             | Castle  | 0      | 5      | B      |
| 0x09 | Elves      | 3  | 10  | 3  | 1     | 2     | 2     | 4     | 24   | 200    | 20     | —                             | Forest  | 100    | 4      | C      |
| 0x0A | Pikemen    | 3  | 10  | 2  | 2     | 4     | 0     | 0     | 0    | 300    | 30     | —                             | Castle  | 0      | 4      | B      |
| 0x0B | Nomads     | 3  | 15  | 2  | 2     | 4     | 0     | 0     | 0    | 300    | 30     | —                             | Plains  | 150    | 4      | C      |
| 0x0C | Dwarves    | 3  | 20  | 1  | 2     | 4     | 0     | 0     | 0    | 350    | 30     | —                             | Hill    | 100    | 4      | C      |
| 0x0D | Ghosts     | 4  | 10  | 3  | 3     | 4     | 0     | 0     | 0    | 400    | 40     | ABSORB \| UNDEAD              | Dungeon | 25     | 3      | E      |
| 0x0E | Knights    | 5  | 35  | 1  | 6     | 10    | 0     | 0     | 0    | 1000   | 100    | —                             | Castle  | 250    | 3      | B      |
| 0x0F | Ogres      | 4  | 40  | 1  | 3     | 5     | 0     | 0     | 0    | 750    | 75     | —                             | Hill    | 200    | 3      | D      |
| 0x10 | Barbarians | 4  | 40  | 3  | 1     | 6     | 0     | 0     | 0    | 750    | 75     | —                             | Plains  | 100    | 3      | C      |
| 0x11 | Trolls     | 4  | 50  | 1  | 2     | 5     | 0     | 0     | 0    | 1000   | 100    | REGEN                         | Forest  | 25     | 3      | D      |
| 0x12 | Cavalry    | 4  | 20  | 4  | 3     | 5     | 0     | 0     | 0    | 800    | 80     | —                             | Castle  | 0      | 2      | B      |
| 0x13 | Druids     | 5  | 25  | 2  | 2     | 3     | 10    | 0     | 3    | 700    | 70     | MAGIC                         | Forest  | 25     | 2      | C      |
| 0x14 | Archmages  | 5  | 25  | 1  | 2     | 3     | 25    | 0     | 2    | 1200   | 120    | FLY \| MAGIC                  | Plains  | 25     | 2      | C      |
| 0x15 | Vampires   | 5  | 30  | 1  | 3     | 6     | 0     | 0     | 0    | 1500   | 150    | LEECH \| FLY \| UNDEAD        | Dungeon | 50     | 2      | E      |
| 0x16 | Giants     | 5  | 60  | 3  | 10    | 20    | 5     | 10    | 6    | 2000   | 200    | —                             | Hill    | 50     | 2      | C      |
| 0x17 | Demons     | 6  | 50  | 1  | 5     | 7     | 0     | 0     | 0    | 3000   | 300    | FLY \| SCYTHE                 | Dungeon | 25     | 1      | E      |
| 0x18 | Dragons    | 6  | 200 | 1  | 25    | 50    | 0     | 0     | 0    | 5000   | 500    | FLY \| IMMUNE                 | Hill    | 25     | 1      | D      |

Ability effects (see §17 for exact formula application):

- **FLY (0x01)** — can fly in combat (2 flight points per turn);
  required by player to fly on overworld; Fly spell target filter.
- **REGEN (0x02)** — `injury` counter zeroed at end of every turn.
- **MAGIC (0x04)** — ranged attack ignores randomness; damage uses
  `ranged_min` fixed (Druids 10, Archmages 25); attack cancels
  entirely if the target has `IMMUNE`.
- **IMMUNE (0x08)** — immune to magic (ranged MAGIC attacks, and the
  Fireball / Lightning / Turn Undead / Freeze spells).
- **ABSORB (0x10)** — (Ghosts) attacker grows by number of kills
  from the hit. Also: at end_week when the randomly selected
  creature is Peasants, every absorbing troop in player army is
  replaced with Peasants (ghost → peasant conversion).
- **LEECH (0x20)** — (Vampires) attacker gains
  `units_killed(final_damage, attacker.hp)` back, clamped to
  `max_count`.
- **SCYTHE (0x40)** — (Demons) 10% chance per attack to kill
  ceil(target.count / 2) extra. Rolled as `KB_rand(1,100) > 89`.
- **UNDEAD (0x80)** — affected by Turn Undead spell; affects morale
  (all undead are MGroup E); end_week ghost-to-peasant rule uses
  the ABSORB flag actually.

## 6. Character classes and ranks

Four classes (Knight, Paladin, Sorceress, Barbarian), each with
four ranks. The class is selected at character creation and is
fixed for the game; the rank advances by visiting the King at
the home castle and collecting enough villains.

### 6.1 `KBclass` struct (`bounty.h:233`)

```c
struct KBclass {
    char title[16];
    byte villains_needed;   // cumulative total villains to reach
                            // this rank
    word leadership;        // this rank's leadership increment
    byte max_spell;         // this rank's max_spells increment
    byte spell_power;       // this rank's spell_power increment
    word commission;        // this rank's commission increment
    byte knows_magic;       // "knows_magic" increment (rarely used)
    byte instant_army;      // troop id for Instant Army spell
};
```

### 6.2 Field semantics

#### `title`
Rank's display name. Used in:
- View Character screen: "<Name> the <Title>".
- King's audience: "<Name> the <Title>".
- Window title: "<Name> the <Title> - openkb <ver>".

#### `villains_needed`
**Cumulative** total villains to qualify for this rank. Checked
in `audience_with_king` (game.c:2080):
```
captured = player_captured(game)        // count of villain_caught[]
needed   = classes[class][rank+1].villains_needed - captured
```
If `needed <= 0`, the player is promoted (rank+1, apply
increments). Otherwise, the King says "I can aid you better
after you've captured N more villains."

The threshold is **strictly cumulative**: rank 1 requires 2
villains, rank 2 requires 8 (so 6 more if at rank 1), rank 3
requires 14 (6 more from rank 2). The "needed for next rank"
formula always references `rank+1`'s value.

#### `leadership` (word)
Increment to `base_leadership` applied at this rank's promotion.
Knight rank 0 = 100; rank 1 = +100 (so total becomes 200);
rank 2 = +300 (total 500); rank 3 = +500 (total 1000).

The actual application is in `player_accept_rank` (play.c:734):
```
game->base_leadership += classes[class][rank].leadership
```

`leadership` (current) is reset to `base_leadership` at end_week.

#### `max_spell` (byte)
Increment to `max_spells`. Sorceress starts with 5; each
promotion adds 8/10/12 = 35 total max.

#### `spell_power` (byte)
Increment to `spell_power`. Sorceress starts with 2; +3/+5/+5 =
15 total. Knight starts with 1; +1/+1/+2 = 5 total.

`spell_power` is the multiplier on most spell effects:
- Time Stop: `time_stop += spell_power * 10` steps.
- Raise Control: `leadership += spell_power * 100`.
- Magic damage spells: `damage = base * spell_power` (per
  `magic_damage`).
- Clone: `clones = (spell_power * 10 + injury) / hp`.

#### `commission` (word)
Increment to weekly commission gold. Sorceress starts at 3000
gp/wk (highest); +1000 each promotion = 6000 total. Knight
starts at 1000; +1000/+2000/+4000 = 8000 total.

#### `knows_magic` (byte)
Increment to `knows_magic`. Only Sorceress rank 0 has 1 here
(the others all have 0). After promotion, `knows_magic` is
incremented further (typically 0 stays 0; for Sorceress the
field is incremented but only "1" matters as a boolean check).

#### `instant_army` (byte)
Troop id for Instant Army spell at this rank. Each rank
typically grants a stronger troop:

```
Knight     0x00, 0x02, 0x08, 0x0E   // Peasants→Militia→Archers→Knights
Paladin    0x00, 0x02, 0x08, 0x12   // Peasants→Militia→Archers→Cavalry
Sorceress  0x01, 0x06, 0x09, 0x13   // Sprites→Gnomes→Elves→Druids
Barbarian  0x00, 0x03, 0x07, 0x0F   // Peasants→Wolves→Orcs→Ogres
```

Total summoned: `(spell_power + 1) * instant_army_multiplier[rank]`
where `instant_army_multiplier[4] = {3, 2, 1, 1}`.

So Knight rank 0 with spell_power=1: `(1+1) * 3 = 6` Peasants.
Knight rank 3 with spell_power=5: `(5+1) * 1 = 6` Knights.

### 6.3 Knight (class 0)

| rank | title    | v.needed | leadership | max_spell | spell_power | commission | magic | instant |
|------|----------|----------|------------|-----------|-------------|------------|-------|---------|
| 0    | Knight   | 0        | 100        | 2         | 1           | 1000       | 0     | 0x00    |
| 1    | General  | 2        | +100       | +3        | +1          | +1000      | 0     | 0x02    |
| 2    | Marshal  | 8        | +300       | +4        | +1          | +2000      | 0     | 0x08    |
| 3    | Lord     | 14       | +500       | +5        | +2          | +4000      | 0     | 0x0E    |

Starting gold: **7500**. Starting army: 20 Militia (0x02),
2 Archers (0x08).

### 6.4 Paladin (class 1)

| rank | title    | v.needed | leadership | max_spell | spell_power | commission | magic | instant |
|------|----------|----------|------------|-----------|-------------|------------|-------|---------|
| 0    | Paladin  | 0        | 80         | 3         | 1           | 1000       | 0     | 0x00    |
| 1    | Crusader | 2        | +80        | +3        | +1          | +1000      | 0     | 0x02    |
| 2    | Avenger  | 7        | +240       | +6        | +2          | +2000      | 0     | 0x08    |
| 3    | Champion | 13       | +400       | +5        | +2          | +4000      | 0     | 0x12    |

Starting gold: **10000**. Starting army: 20 Peasants (0x00),
20 Militia (0x02).

### 6.5 Sorceress (class 2)

| rank | title     | v.needed | leadership | max_spell | spell_power | commission | magic | instant |
|------|-----------|----------|------------|-----------|-------------|------------|-------|---------|
| 0    | Sorceress | 0        | 60         | 5         | 2           | 3000       | **1** | 0x01    |
| 1    | Magician  | 3        | +60        | +8        | +3          | +1000      | 0     | 0x06    |
| 2    | Mage      | 6        | +180       | +10       | +5          | +1000      | 0     | 0x09    |
| 3    | Archmage  | 12       | +300       | +12       | +5          | +1000      | 0     | 0x13    |

Starting gold: **10000**. Starting army: 30 Peasants (0x00),
10 Sprites (0x01). Sorceress is the only class with
`knows_magic=1` at rank 0 (so she skips the Archmage Aurange
lesson). All other classes start with `knows_magic=0` and must
pay 5000 gold at the Alcove (see §20 — `visit_alcove`).

### 6.6 Barbarian (class 3)

| rank | title      | v.needed | leadership | max_spell | spell_power | commission | magic | instant |
|------|------------|----------|------------|-----------|-------------|------------|-------|---------|
| 0    | Barbarian  | 0        | 100        | 2         | 0           | 2000       | 0     | 0x00    |
| 1    | Chieftain  | 1        | +100       | +2        | +1          | +2000      | 0     | 0x03    |
| 2    | Warlord    | 5        | +300       | +3        | +1          | +2000      | 0     | 0x07    |
| 3    | Overlord   | 10       | +500       | +3        | +1          | +2000      | 0     | 0x0F    |

Starting gold: **7500**. Starting army: 20 Wolves (0x03); slot 2
empty (troop 0xFF). Barbarian starts single-stacked.

### 6.7 Instant Army count

Instant Army spell summons
`(spell_power + 1) * instant_army_multiplier[rank]` troops of the
familiar type. Rank-keyed multiplier table:

```
instant_army_multiplier[4] = { 3, 2, 1, 1 };
```

Lower ranks get more creatures per cast (multiplier decreases as
rank grows).

### 6.8 Promotion ceremony

In the throne room (`audience_with_king`, game.c:2070):

- If `player_captured(game) >= classes[class][rank+1].villains_needed`
  and `rank < MAX_RANKS - 1` → promote via `promote_player()`
  (play.c:742): `rank++`; then `player_accept_rank()` adds all the
  rank's increments to `base_leadership`, `max_spells`,
  `spell_power`, `commission`, and `knows_magic`.
- If already at max rank AND enough villains captured → King speaks
  of recovering the Scepter (see §24 for verbatim).
- Otherwise → "I can aid you better after you've captured N more
  villains" where N is `needed - captured`.

### 6.9 Class strategic notes

**Knight (class 0):**
- Strongest melee starter (Militia + Archers).
- Lowest starting commission (1000) but highest leadership at
  high rank (1000 base + Crown).
- Magic-light progression (max 5 spells, spell_power 5).
- Best for direct combat, weakest for spells.

**Paladin (class 1):**
- Balanced starter (Peasants + Militia, both melee).
- Moderate commission (1000).
- Mid magic (max 17 spells, spell_power 6).
- "Knight with more leadership and slightly better magic".

**Sorceress (class 2):**
- Magical specialist; **only class that starts with knows_magic=1**.
- Highest commission (3000 → 6000 endgame, vs 8000 for Knight).
- Mid leadership (60 → 600 endgame).
- Best magic: max_spells 35 (vs Knight 14, Paladin 17, Barbarian 10).
- Best spell_power 15 (vs 5/6/2).
- Sprites starting army flies (with skill_level=1, doesn't enable
  overworld flight).
- The intended path: dump cash on Aurange-already-paid, focus on
  spells. But the Alcove is at (11,19) on Continentia, near the
  starting castle — Sorceress doesn't need to visit.

**Barbarian (class 3):**
- Wolves starter, single-stack.
- Best leadership progression (100 → 1000 endgame).
- Spell_power 0 at start; spell-light overall (max 10 spells).
- Lowest villains_needed thresholds (1, 5, 10 vs Knight's 2, 8, 14).
  Easier rank advancement.

### 6.10 Class data table summary

```
class  starting_gold  villains_max  total_lead  total_max_spell  total_spow
0 Knt   7500           14            1000        14               5
1 Pld  10000           13            800         17               6
2 Src  10000           12            600         35              15
3 Brb   7500           10           1000        10               2
```

`villains_max` = villains_needed for rank 3 (Lord/Champion/
Archmage/Overlord). After max rank, the King's speech changes
to "Hurry and recover my Scepter of Order".

### 6.11 Module override path

`refill_rules()` for each class:
```
WDAT_COMM       → classes[c][r].commission
WDAT_LDRSHIP    → classes[c][r].leadership
DAT_VNEED       → classes[c][r].villains_needed
DAT_KMAGIC      → classes[c][r].knows_magic
DAT_SPOWER      → classes[c][r].spell_power
DAT_MAXSPELL    → classes[c][r].max_spell
DAT_FAMILIAR    → classes[c][r].instant_army
```

DOS module returns NULL for all class data (defaults stand).
GNU module reads from `classes.ini` (per `free-data.c`).

The `class.title` strings are also overridable via `STRL_RANKS`
(sub_id = class). DOS uses defaults; GNU reads `ranks.ini`.

### 6.12 Starting army constants

```c
starting_gold[4] = { 7500, 10000, 10000, 7500 };

starting_army_troop[4][2] = {
    { 0x02, 0x08 },   // Knight: Militia, Archers
    { 0x00, 0x02 },   // Paladin: Peasants, Militia
    { 0x00, 0x01 },   // Sorceress: Peasants, Sprites
    { 0x03, 0xFF },   // Barbarian: Wolves, (empty slot)
};

starting_army_numbers[4][2] = {
    { 20, 2 },   // Knight: 20 Militia, 2 Archers
    { 20, 20 },  // Paladin: 20 each
    { 30, 10 },  // Sorceress: 30 Peasants, 10 Sprites
    { 20, 0 },   // Barbarian: 20 Wolves
};
```

Slot 1 of Barbarian (`troop_id = 0xFF`) is permanently empty
at start, leaving slots 2-4 also empty. So Barbarian has 1 active
troop vs other classes' 2.

### 6.13 instant_army_multiplier

```c
byte instant_army_multiplier[MAX_RANKS] = { 3, 2, 1, 1 };
```

Multiplier applied to `(spell_power + 1)` to get total spawned
units. Higher rank → fewer per-cast units of a stronger type.
Examples:
- Rank 0, spell_power=1: `2 × 3 = 6` low-tier troops.
- Rank 3, spell_power=5: `6 × 1 = 6` high-tier troops.

Spell power scales the count, but rank scales **down** the
multiplier. The result is a roughly constant 6-12 unit summon
across the game.

## 7. Villains — armies and rewards

The 17 villains form the core questline. Each has a fixed name
(stored in KB.EXE), a fixed starting-castle army, a fixed bounty
reward, and a continent assignment.

### 7.0 Mechanics overview

Villains are placed in castles at game creation by `salt_villains`
(play.c:157). The villain id is stored in the low 5 bits of
`game->castle_owner[castle_id]`. Player must:

1. Get a contract for the villain (visit any town, "A) Get New
   Contract"). One active contract at a time.
2. Find the villain's castle (Find Villain spell, OR by visiting
   castles and reading "Castle X is under Y's rule" in the
   Information menu — but this is only correct if the player is
   on the right continent: `view_contract` always assumes
   `continent = game->continent`, see §34.9).
3. Defeat the castle: `lay_siege` → `run_combat` mode 1.
4. Receive the bounty (`fullfill_contract`).
5. Cycle the contract: villain's slot in `contract_cycle[]` is
   filled with the next uncaught villain (`max_contract` advances).

### 7.1 Reward schedule (`villain_rewards`)

```
v00: 5000        v06: 12000      v0C: 30000
v01: 6000        v07: 14000      v0D: 35000
v02: 7000        v08: 16000      v0E: 40000
v03: 8000        v09: 18000      v0F: 45000
v04: 9000        v0A: 20000      v10: 50000
v05: 10000       v0B: 25000
```

### 7.2 Villain castle armies

`[troop_id, ...]` (×5) and `[count, ...]` (×5):

| v    | Troops (ids)             | Counts                  |
|------|--------------------------|-------------------------|
| 0x00 | 00, 03, 02, 00, 00       | 50, 20, 25, 30, 25      |
| 0x01 | 0b, 02, 02, 00, 00       | 10, 30, 20, 60, 40      |
| 0x02 | 01, 01, 04, 05, 0f       | 70, 50, 20, 20, 4       |
| 0x03 | 03, 07, 08, 11, 0c       | 30, 20, 10, 2, 6        |
| 0x04 | 02, 02, 08, 09, 10       | 50, 50, 10, 10, 5       |
| 0x05 | 01, 0d, 0e, 14, 14       | 250, 10, 10, 4, 4       |
| 0x06 | 02, 08, 0a, 12, 0e       | 100, 20, 20, 15, 15     |
| 0x07 | 09, 14, 13, 0a, 01       | 30, 30, 10, 30, 300     |
| 0x08 | 07, 0f, 11, 16, 03       | 150, 20, 10, 5, 80      |
| 0x09 | 04, 05, 0d, 15, 17       | 500, 100, 30, 10, 6     |
| 0x0A | 04, 05, 0d, 15, 17       | 600, 200, 50, 25, 10    |
| 0x0B | 16, 18, 0f, 07, 06       | 30, 5, 30, 200, 200     |
| 0x0C | 06, 10, 16, 0b, 00       | 300, 40, 20, 100, 700   |
| 0x0D | 08, 0a, 12, 0e, 18       | 35, 100, 80, 60, 5      |
| 0x0E | 17, 15, 14, 06, 00       | 30, 50, 100, 500, 5000  |
| 0x0F | 17, 18, 12, 0e, 14       | 50, 10, 200, 250, 60    |
| 0x10 | 18, 18, 18, 17, 15       | 100, 25, 25, 100, 100   |

### 7.3 Villains per continent

`villains_per_continent[4] = { 6, 4, 4, 3 }` (total 17).

At game creation, `salt_villains(game, continent, base_id)` walks
the villains in order, assigning them into the first suitable
castle on each continent:

- Continent 0 takes villains 0x00..0x05 (6)
- Continent 1 takes villains 0x06..0x09 (4)
- Continent 2 takes villains 0x0A..0x0D (4)
- Continent 3 takes villains 0x0E..0x10 (3)

The assignment inside a continent is random: `salt_villains`
picks castle indices in [0, 25] uniformly and keeps trying until
it hits one whose castle_coords[i][0] == target continent AND
`castle_owner==0x7F` (not yet assigned). Each assignment overrides
the castle's army with the villain's static army.

### 7.4 Villain placement algorithm

`salt_villains(game, continent, base_id)` (play.c:157):

```
max = num_castles(continent)        // count of castles on cont
if max < villains_per_continent[continent]:
    error: "Not enough castles to put villains"
    return base_id

for j = 0; j < villains_per_continent[continent]; ):
    villain_id = base_id + j
    castle_id  = KB_rand(0, MAX_CASTLES - 1)
    if castle_coords[castle_id][0] == continent
       and game->castle_owner[castle_id] == 0x7F:
        // Place this villain in this castle
        game->castle_owner[castle_id] = villain_id
        for i = 0; i < 5; i++:
            game->castle_troops[castle_id][i] = villain_army_troops[villain_id][i]
            game->castle_numbers[castle_id][i] = villain_army_numbers[villain_id][i]
        j++

return base_id + j
```

The loop is **rejection sampling**: keeps picking random castle
ids until it finds one matching this continent and unowned. This
can be slow if `villains_per_continent` is close to
`num_castles` (Continentia: 6 villains, 11 castles — 6/11 chance
per attempt).

Total villains across continents:
- Continentia: 6 villains in 11 castles. `salt_villains(game, 0,
  0)` returns 6.
- Forestria: 4 villains in 6 castles. `salt_villains(game, 1, 6)`
  returns 10.
- Archipelia: 4 villains in 6 castles. `salt_villains(game, 2,
  10)` returns 14.
- Saharia: 3 villains in 3 castles. `salt_villains(game, 3, 14)`
  returns 17.

After all 4 calls, every villain (0..16) is placed in exactly one
castle. The remaining unowned castles (5 on Continentia, 2 on
Forestria, 2 on Archipelia, 0 on Saharia = 9 total) get
generic-monster garrisons via `repopulate_castle`.

### 7.5 Short villain codes (for asset filenames)

```
DOS_villain_names[17] = {
    "mury", "hack", "ammi", "baro", "drea", "cane", "mora", "barr",
    "barg", "rina", "ragf", "mahk", "auri", "czar", "magu", "urth",
    "arec"
};
```

These prefix DOS `.4`/`.16`/`.256` asset files. The full villain
names are stored in KB.EXE (§25).

### 7.6 Contract flow

Player can hold at most one contract at a time
(`game->contract`, 0xFF = none). Towns offer a cycle of up to 5
villains (`contract_cycle[5]`). Pressing "A" in a town
(`visit_town`, game.c:2585):

1. `game->last_contract++`; wraps 0..4.
2. `game->contract = game->last_contract` — replaces current contract.
3. The cycle slot's villain id is the new contract.

When the contracted villain's castle is defeated
(`run_combat` mode=1, game.c:3488):

- Extract `villain_id = castle_owner[i] & KBCASTLE_VILLAIN`.
- If `game->contract == villain_id`, call `fullfill_contract`:
  - `gold += villain_rewards[villain_id]`
  - `villain_caught[villain_id] = 1`
  - `contract = 0xFF`
  - Clear the slot in `contract_cycle`; fill it with the next
    uncaught villain, starting from `max_contract`; then
    `max_contract++`.

If the castle is defeated without the matching contract, the
victory banner shows "Since you did not have the proper contract,
the Lord has been set free." and no bounty is paid (but the castle
still becomes player-owned and all its spoils are awarded).

### 7.7 Contract cycle mechanics

`contract_cycle[5]` is a circular buffer of uncaught villain ids
that any town's "A) Get New Contract" presents. Initial state at
game creation:
```
contract_cycle = {0, 1, 2, 3, 4}   // first 5 villains
last_contract  = 4                 // pointing past last filled slot
max_contract   = 5                 // next villain to advance to
```

When a player visits a town and accepts a new contract:
```
last_contract++
if last_contract > 4:
    last_contract = 0
contract = last_contract           // cycle through 0..4
```

Wait — this is wrong; let me re-read game.c. Actually:
```
game->last_contract++              // 4 → 5
if (game->last_contract > 4):
    game->last_contract = 0
game->contract = game->last_contract    // cycle through 0..4
```

Hmm, but `contract` is the active contract villain id, not the
cycle slot. Re-reading game.c:2682:

```
game->last_contract++
if (game->last_contract > 4) game->last_contract = 0
game->contract = game->last_contract
```

But `contract` is supposed to be a villain id, and
`last_contract` is a cycle index 0..4. Reading carefully:
`game->contract` is set to `game->last_contract`, which is just
the cycle index. This is a bug — the active contract should be
`contract_cycle[last_contract]`, not just `last_contract`.

Verifying: when a villain is fulfilled, `fullfill_contract`
(play.c:542) does `game->villain_caught[villain_id] = 1;
game->contract = 0xFF`. The villain_id passed to
`fullfill_contract` is from `castle_owner[i] & KBCASTLE_VILLAIN`
in `run_combat`. So the actual capture works on the villain_id
in the castle, but the contract activation just stores
`last_contract` (0..4). The match in `fullfill_contract`:
`if (game->contract == villain_id)` — only succeeds when
`last_contract` matches the villain_id, which happens only for
villains 0..4.

This means: in vanilla openkb, **only the first 5 villains can
have their contracts fulfilled correctly**. Defeating villain 5+
without a "matching" contract triggers the "set free" path. This
is consistent with the `contract_cycle[5]` initialization but
the cycle never properly advances because the bug in game.c:2686
short-circuits it.

Marking this as a discovered bug — see §34.18.

When a contract is fulfilled (`fullfill_contract`):
```
game->gold += villain_rewards[villain_id]
game->villain_caught[villain_id] = 1
game->contract = 0xFF

// Clear cycle slot
slot = -1
for i = 0..4:
    if contract_cycle[i] == villain_id:
        slot = i
        break
contract_cycle[slot] = 0xFF

// Find next uncaught
for i = max_contract..16:
    if !villain_caught[i]:
        contract_cycle[slot] = i
        break
max_contract++
```

After all 17 are caught: `max_contract` advances past the array,
and `contract_cycle[slot]` stays 0xFF (no more villains). This
matches the puzzle map state where all squares are revealed.

## 8. Dwellings, morale, and the morale chart

### 8.0 Dwelling kinds and tile mapping

Dwellings come in 5 kinds (enum in bounty.h):

| Kind  | Index | Tile id  | Asset prefix | Salt-time placement       |
|-------|-------|----------|--------------|---------------------------|
| Plains  | 0  | 0x8C     | "plai"       | World map                 |
| Forest  | 1  | 0x8D     | "frst"       | World map                 |
| HillCave| 2  | 0x8E     | "cave"       | World map (also TILE_TELECAVE) |
| Dungeon | 3  | 0x8F     | "dngn"       | World map                 |
| Castle  | 4  | (no tile)| "cstl"       | Home castle barracks only |

Castle dwellings aren't placed on the world map — they're the
player's home castle barracks, which displays a random castle
troop (`home_troops[5]` filtered by `dwells == DWELLING_CASTLE`)
as visual flavor.

The HillCave tile (0x8E) is dual-purpose: it's both a hill/cave
dwelling AND a teleport cave. `visit_telecave` (game.c:2900) is
called first; if no match, falls through to `visit_dwelling`.

When a dwelling is salted onto the map (§14.7), its tile id is
`0x8C + dwells` where `dwells` is the troop's `troops[id].dwells`
field. So a Sprites dwelling becomes 0x8D (Forest tile graphic),
a Wolves dwelling becomes 0x8C (Plains), etc.

### 8.1 Continent-specific dwelling preferences

`continent_dwellings[4][11]`, terminator 0xFF:

```
cont 0 (Continentia):  00, 01, 07, 04, 03, 06, FF
cont 1 (Forestria):    0c, 05, 0b, 09, 0f, 09, FF
cont 2 (Archipelia):   0d, 10, 11, 13, FF
cont 3 (Saharia):      16, 15, 14, 18, 17, FF
```

When a dwelling is placed on a continent at salt time
(play.c:`populate_dwelling`), its troop is chosen as follows:

- If `dwelling_id < MAX_DWELLINGS` AND slot is not 0xFF: take the
  listed troop.
- Otherwise: roll uniformly in `[dwelling_ranges[cont][0],
  dwelling_ranges[cont][1]]`:

```
dwelling_ranges[4][2] = {
    { 0x00, 0x0e },   // cont 0: peasant..knight
    { 0x01, 0x0e },   // cont 1: sprite..knight
    { 0x02, 0x0e },   // cont 2: militia..knight
    { 0x14, 0x18 },   // cont 3: archmage..dragon
};
```

### 8.2 Morale groups

```
MGROUP_A = 0 // Smallfolk — Peasants, Militia
MGROUP_B = 1 // Lords — Archers, Pikemen, Knights, Cavalry
MGROUP_C = 2 // Creatures — Sprites, Gnomes, Elves, Nomads,
             //             Dwarves, Barbarians, Druids,
             //             Archmages, Giants
MGROUP_D = 3 // Monsters — Wolves, Orcs, Ogres, Trolls, Dragons
MGROUP_E = 4 // Undead — Skeletons, Zombies, Ghosts, Vampires, Demons
```

### 8.3 Morale chart `morale_chart[J][I]`

Rows = existing troop's morale group (J). Columns = candidate
troop's morale group (I). Result is how candidate I feels about
having J in the army.

```
          A     B     C     D     E
  A      N     N     N     N     N
  B      N     N     N     N     N
  C      N     N     H     N     N
  D      L     N     L     H     N
  E      L     L     L     N     N
```

(`N=Normal`, `L=Low`, `H=High`.)

Formula (play.c:`troop_morale`):

- Start `morale = HIGH` for slot `i`.
- For each occupied slot `j`:
  - Lookup `nm = morale_chart[groupJ][groupI]` where groupI = slot
    i's group, groupJ = slot j's group.
  - If `nm`'s numeric converted rank (L=1, N=0, H=2) is **lower**
    than current `morale`'s rank, adopt it.
  - Wait — the converter is `{MORALE_LOW=1, MORALE_NORMAL=0,
    MORALE_HIGH=2}`. This `< morale` test is reversed from what
    you'd expect: it picks the morale with **lowest converted
    value**, which is NORMAL, never LOW unless LOW is the only
    value. The code as written degrades to NORMAL for any mixed
    army without Highs. See §34; this is a bug.

### 8.4 Morale in damage

Only applied to the attacker side when its hero is present and
that unit is under control (i.e. the player side, when the unit
isn't out-of-control due to leadership cap):

- LOW morale: `final_damage /= 2`
- NORMAL: unchanged
- HIGH: `final_damage += final_damage / 2` (×1.5)

Note: morale is **never applied to the AI side** (`heroes[1]
== NULL`). It also doesn't apply to player out-of-control units
(out-of-control units fight with no morale modifier).

### 8.5 Morale display

`view_army` (game.c:1798) shows each troop slot's morale name
("Norm", "Low", "High") in the per-row stats. If the unit is
over leadership ("army_leadership(troop_id) <= 0"), the row
shows "Out of Control" instead.

```c
char *morale_names[3] = { "Norm", "Low", "High" };
```

Indexed by the morale enum (NORMAL=0, LOW=1, HIGH=2).

### 8.6 Salt-time dwelling placement

When `salt_continent` (§14.7) places a SALT_DWELLING marker on a
chest tile, it calls `populate_dwelling(game, continent, slot)`
which:

```
if slot < MAX_DWELLINGS and continent_dwellings[continent][slot] != 0xFF:
    troop_id = continent_dwellings[continent][slot]
else:
    troop_id = KB_rand(dwelling_ranges[continent][0],
                       dwelling_ranges[continent][1])
enforce_dwelling(game, continent, slot, troop_id):
    dwelling_troop[continent][slot]      = troop_id
    dwelling_population[continent][slot] = troops[troop_id].max_population
return troops[troop_id].dwells
```

The returned `dwells` value is the dwelling kind, which becomes
the tile id offset (`0x8C + dwells`).

`continent_dwellings` provides preferred troops; once exhausted
(0xFF), random fill from `dwelling_ranges`. So the first 4-6
dwellings on each continent are predictable; the rest are random.

Continent-specific dwelling preferences (also in §3 source):

```c
continent_dwellings[4][11] = {
    { 0x00, 0x01, 0x07, 0x04, 0x03, 0x06, 0xFF },  // cont 0
    { 0x0C, 0x05, 0x0B, 0x09, 0x0F, 0x09, 0xFF },  // cont 1
    { 0x0D, 0x10, 0x11, 0x13, 0xFF },              // cont 2
    { 0x16, 0x15, 0x14, 0x18, 0x17, 0xFF },        // cont 3
};
```

(Note: cont 1 has Elves at slot 5 again, duplicate of slot 3 —
likely a typo in source but preserved as-is.)

```c
dwelling_ranges[4][2] = {
    { 0x00, 0x0e },  // cont 0: peasant..knight (continentia)
    { 0x01, 0x0e },  // cont 1: sprites..knight (forestria)
    { 0x02, 0x0e },  // cont 2: militia..knight (archipelia)
    { 0x14, 0x18 },  // cont 3: archmage..dragon (saharia)
};
```

Continent 3's range is much narrower (5 troops) and tier 14+,
making Saharia dwellings consistently strong.

### 8.7 Friendly foe placement

5 friendly foe slots per continent (`SALT_FRIENDLY` markers).
Their tile is 0x91 (foe), but troop/numbers are NOT pre-rolled.
Rolling deferred to `accept_foe` at encounter time.

`accept_foe` (game.c:3574):
- Computes `troop_id, troop_count = roll_creature(continent, ...)`
  — fresh roll each visit.
- If army has space (`player_army_slots > 0`):
  - Banner: "<numwords> <Troop>, with desires of greater glory,
    wish to join you. Accept (y/n)?"
  - On y: `add_troop(game, troop_id, troop_count)`.
- If no slot: "flee in terror at the sight your vast army." (5
  troops left auto-skip).
- Tile cleared to 0 regardless.

So friendly foes are predictable encounters but with random
strengths. A player can stall to roll for better troops.

### 8.8 Dwelling refresh schedule

End-of-week `end_week` (§16.6) processes dwelling growth:

```
creature = end_week_pick   // 1..24, or 0 every 4th week
for each continent c:
  for each dwelling d:
    if dwelling_troop[c][d] == creature:
      dwelling_population[c][d] = troops[creature].max_population
```

So a dwelling fully refills only on the weeks where its troop
matches the astrology pick. Probability:
- ~1/24 per week per dwelling matching the pick (weeks 1-3, 5-7,
  etc., i.e. non-Peasants weeks).
- Every 4th week is forced to Peasants (creature=0). On those
  weeks, only Peasants dwellings refresh.

A non-Peasants dwelling could go ~24 weeks (5 months) without a
refresh.

### 8.9 Recruiting at dwellings

`visit_dwelling(game, rtype)` (game.c:2918):

1. Find the dwelling slot at current (x, y).
2. `troop_id = dwelling_troop[c][slot]`.
3. Compute `max = army_leadership(game, troop_id) /
   troops[troop_id].hp`.
4. Show menu (§24.41 banner).
5. Read numeric input.
6. If `number > max`: silently ignored (no message).
7. If `number > dwelling_population[c][slot]`: silently ignored.
8. Else `buy_troop(game, troop_id, number)`:
   - If gold < cost: "You don't have enough gold!"
   - Else: deduct gold, `add_troop(game, troop_id, number)`.
9. On success: `dwelling_population[c][slot] -= number`.

The "max" calculation is per-troop: if you already have N of
this type, the leadership-cap formula subtracts N×hp before
dividing, so max recruitable can be 0 even if dwelling has stock.

### 8.10 Dwelling location data

`dwelling_coords[4][11][2]` stores x,y of each dwelling on each
continent. Set at `salt_continent` time as 0x8C..0x8F tiles are
encountered. The first N dwellings (where N = continent's
specific count, ≤11) are populated; the rest are 0,0 (unused
slots).

`salt_continent` walks the map row-major; when it hits a 0x8B
(chest) tile assigned SALT_DWELLING, it records `(i, j)` into
the next slot. The order is map-traversal order, so dwellings
appear in slot index roughly by their map position (top-left
first).

## 9. Artifacts and their powers

8 artifacts, indexed 0..7. Each has a name, a single power flag,
and is placed at game-creation time on a specific continent.
Powers split into "instant on pickup" effects (Crown, Amulet,
Articles, Ring) and "live-checked" effects (Sword, Shield,
Anchor; Book of Necros has no implemented effect).

### 9.0 Catalog

```
0x00 The Sword of Prowess
0x01 The Shield of Protection
0x02 The Crown of Command
0x03 The Articles of Nobility
0x04 The Amulet of Augmentation
0x05 The Ring of Heroism
0x06 The Book of Necros
0x07 The Anchor of Admirability
```

Powers (`artifact_powers[8]`):

```
0x00 Sword           POWER_INCREASED_DAMAGE       = 0x80
0x01 Shield          POWER_QUARTER_PROTECTION     = 0x02
0x02 Crown           POWER_DOUBLE_LEADERSHIP      = 0x04
0x03 Articles        POWER_INCREASE_COMMISSION    = 0x10
0x04 Amulet          POWER_DOUBLE_SPELL_POWER     = 0x08
0x05 Ring            POWER_DOUBLE_MAX_SPELLS      = 0x40
0x06 Book of Necros  POWER_UNKNOWN_XXX1           = 0x01
0x07 Anchor          POWER_CHEAPER_BOAT_RENTAL    = 0x20
```

Power effects:

- **INCREASED_DAMAGE (0x80):** attacker side's `final_damage *= 1.5`.
- **QUARTER_PROTECTION (0x02):** target side's
  `final_damage = (final_damage / 4) * 3`, i.e. reduce to 75%
  (with integer-div-before-multiply flooring).
- **DOUBLE_LEADERSHIP (0x04):** on artifact pickup, instantly
  `base_leadership *= 2; leadership = base_leadership`; also,
  treasure-chest gold cache rewards double the leadership option.
- **DOUBLE_SPELL_POWER (0x08):** on pickup, `spell_power *= 2`.
- **INCREASE_COMMISSION (0x10):** on pickup, `commission += 2000`.
- **DOUBLE_MAX_SPELLS (0x40):** on pickup, `max_spells *= 2`; also,
  treasure-chest "max spells" chest adds double base points.
- **CHEAPER_BOAT_RENTAL (0x20):** `boat_cost(game)` returns 100
  instead of 500.
- **UNKNOWN_XXX1 (0x01):** effect not observed to be wired in
  openkb source (Book of Necros — the original game presumably
  affected undead troops but openkb doesn't implement it).

### 9.0.1 Instant vs live checks

- **Instant** (applied once at `take_artifact`):
  - DOUBLE_LEADERSHIP: `base_leadership *= 2; leadership =
    base_leadership`. Permanent.
  - DOUBLE_SPELL_POWER: `spell_power *= 2`. Permanent.
  - INCREASE_COMMISSION: `commission += 2000`. Permanent.
  - DOUBLE_MAX_SPELLS: `max_spells *= 2`. Permanent.

- **Live-checked** (queried each time the relevant action fires):
  - INCREASED_DAMAGE: `has_power(game, POWER_INCREASED_DAMAGE)`
    is checked in `deal_damage` to apply ×1.5 multiplier.
  - QUARTER_PROTECTION: `has_power(game, POWER_QUARTER_PROTECTION)`
    is checked in `deal_damage` to apply ×0.75 multiplier.
  - CHEAPER_BOAT_RENTAL: `has_power(game, POWER_CHEAPER_BOAT_RENTAL)`
    is checked in `boat_cost(game)`.

- **Implicit** (DOUBLE_LEADERSHIP also affects chest gold/leadership
  choice; DOUBLE_MAX_SPELLS also doubles max-spell chest amount).
  See §13 chests.

### 9.0.2 Stacking semantics

Each artifact is unique — only one can be picked up. Once
`artifact_found[i] = 1`, no more pickups for that index. The
puzzle map shows artifacts as still-piece placeholders until
found.

Picking up a Crown after manually doubling leadership via cheat
'V' would re-double — leading to weird stacking. But the cheat
adds 100 to base_leadership, while Crown does ×2 — they don't
mathematically interact except by ordering.

### 9.1 Which artifact is which continent

Each continent has exactly 2 artifacts (salted into 2 chest tiles
per continent). A positional ordering determines which artifact
(by 0..7 ordered id) is placed where, then `artifact_inversion[]`
maps that positional id to the actual artifact index:

```
artifact_inversion[8] = {
    0x05,  // continent0, artifact0 — Ring
    0x03,  // continent0, artifact1 — Articles
    0x01,  // continent1, artifact0 — Shield
    0x07,  // continent1, artifact1 — Anchor
    0x02,  // continent2, artifact0 — Crown
    0x06,  // continent2, artifact1 — Book
    0x04,  // continent3, artifact0 — Amulet
    0x00,  // continent3, artifact1 — Sword
};
```

Thus:
- **Continentia** holds the Ring of Heroism and the Articles of Nobility.
- **Forestria** holds the Shield of Protection and the Anchor of Admirability.
- **Archipelia** holds the Crown of Command and the Book of Necros.
- **Saharia** holds the Amulet of Augmentation and the Sword of Prowess.

### 9.2 Puzzle map

`puzzle_map[5][5]` (`signed char`, bounty.c:768). Negative values
are artifacts (`-id-1`), non-negative values are villain ids:

```
Row 0 (top):      -1,  7, -2,  6, -3
Row 1:             5, 15, 14, 13,  4
Row 2 (middle):   -4, 12, 16, 11, -5
Row 3:             3, 10,  9,  8,  2
Row 4 (bottom):   -6,  1, -7,  0, -8
```

Decoded:
- Artifacts by position: Sword(0), Shield(1), Crown(2),
  Articles(3), Amulet(4), Ring(5), Book(6), Anchor(7).
  `-1`→artifact 0 (Sword), `-2`→Shield, … `-8`→Anchor.
- Villains are laid out around them by `villain_caught` flag.

Drawn in the minimap sidebar and in `view_puzzle` (see §23). A
puzzle piece is displayed where the corresponding artifact hasn't
been found or villain hasn't been caught yet; as you complete
them, pieces lift to reveal the map area around the buried scepter.

### 9.3 Artifact pickup dialog

`take_artifact(game, num)` in game.c:3255:

- Positional id: `ordered = continent*2 + num` (num ∈ {0,1}).
- Actual artifact id: `id = artifact_inversion[ordered]`.
- `artifact_found[id] = 1`.
- Apply instant effects: `DOUBLE_LEADERSHIP`, `DOUBLE_SPELL_POWER`,
  `INCREASE_COMMISSION`, `DOUBLE_MAX_SPELLS` are all applied here.
- Draw sidebar at tick 1, show the description (sourced from
  KB.EXE via `STRL_ADESCS`, villain id=ordered, see §25), then
  append verbatim:

  > "...and a piece of the map to the stolen scepter."

- Remove the artifact tile from the map.

### 9.4 Artifact placement rules

Each continent gets exactly **2 artifacts** (out of 8), salted by
`salt_continent` (§14.7) onto chest tiles. Chest-id-to-artifact:

```
At salt time, in `salt_continent`:
  num_artifacts = 0    // counter for this continent
  ...
  case SALT_ARTIFACT:
      game->map[c][j][i] = 0x92 + num_artifacts    // 0x92 or 0x93
      num_artifacts++
```

So the first artifact placed becomes tile 0x92 (ARTIFACT_1) and
the second becomes 0x93 (ARTIFACT_2). When picked up,
`take_artifact(game, num)` is called with `num = m - 0x92` (so
0 or 1).

The "ordered id" (positional) for the artifact is computed:
```
ordered_id = continent * 2 + num
actual_id  = artifact_inversion[ordered_id]
```

This double-lookup means each chest's artifact is fixed by the
continent it's on, not by which chest tile it landed on. The
`artifact_inversion[]` table maps positional → power id:

```
artifact_inversion[8] = {
    0x05,  // continent0, artifact0 → Ring
    0x03,  // continent0, artifact1 → Articles
    0x01,  // continent1, artifact0 → Shield
    0x07,  // continent1, artifact1 → Anchor
    0x02,  // continent2, artifact0 → Crown
    0x06,  // continent2, artifact1 → Book of Necros
    0x04,  // continent3, artifact0 → Amulet
    0x00,  // continent3, artifact1 → Sword
};
```

So the player always knows which artifacts are on which
continent (by inverse lookup), but not which specific chest is
which artifact (depends on map traversal order during salting).

Decoded:

| Continent | Slot 0          | Slot 1                  |
|-----------|-----------------|-------------------------|
| Continentia | Ring          | Articles of Nobility    |
| Forestria   | Shield        | Anchor of Admirability  |
| Archipelia  | Crown         | Book of Necros          |
| Saharia     | Amulet        | Sword of Prowess        |

### 9.5 Picking up artifacts

The player walks onto a 0x92 or 0x93 tile; `adventure_loop`
dispatches to `take_artifact(game, m - 0x92)`. The function:

```c
ordered_id = game->continent * 2 + num
id = artifact_inversion[ordered_id]
power = artifact_powers[id]
text = KB_Resolve(STRL_ADESCS, ordered_id)

game->artifact_found[id] = 1

if power & POWER_DOUBLE_LEADERSHIP:
    game->base_leadership *= 2
    game->leadership = game->base_leadership
if power & POWER_DOUBLE_SPELL_POWER:
    game->spell_power *= 2
if power & POWER_INCREASE_COMMISSION:
    game->commission += 2000
if power & POWER_DOUBLE_MAX_SPELLS:
    game->max_spells *= 2

draw_sidebar(game, 1)   // tick 1 (deviation from DOS, see comment)
KB_BottomBox(text, "...and a piece of the map to the stolen scepter.",
             MSG_PADDED | MSG_PAUSE)
game->map[continent][y][x] = 0
```

### 9.6 Artifact descriptions (`STRL_ADESCS`)

5-line description per artifact, read from KB.EXE
(`DOS_ADESCS = 0x19650` for KB90). Loaded via
`DOS_read_adescs`:

- 5 lines per artifact = 8 × 5 = 40 lines total in the segment.
- Position offset = `villain_id * 5` (here, "villain_id" is a
  misnomer — it's the artifact id).

Note that the description is keyed by **ordered_id** not actual
artifact id, so the strings are continent-keyed in source. The
DOS source layout for these strings probably matches the
positional order, not the named order.

The pickup banner appends (verbatim):
```
...and a piece of the map to the stolen scepter.
```
This is hardcoded in game.c after the description.

## 10. Spells

14 spells split exactly 7+7 between combat and adventure. Each
spell has a name, a gold cost (paid at town purchase), and an
effect (described in §18). The system is built around three
related arrays:

- `game->spells[14]` — count per spell (player's "inventory").
- `game->town_spell[26]` — which spell each town sells.
- `spell_costs[14]` — gold per purchase.

### 10.0 Spell ids and split

Names and costs (`spell_names`, `spell_costs`):

| id   | Name          | Cost  | Kind        |
|------|---------------|-------|-------------|
| 0x00 | Clone         | 2000  | Combat      |
| 0x01 | Teleport      | 500   | Combat      |
| 0x02 | Fireball      | 1500  | Combat      |
| 0x03 | Lightning     | 500   | Combat      |
| 0x04 | Freeze        | 300   | Combat      |
| 0x05 | Resurrect     | 5000  | Combat      |
| 0x06 | Turn Undead   | 2000  | Combat      |
| 0x07 | Bridge        | 100   | Adventure   |
| 0x08 | Time Stop     | 200   | Adventure   |
| 0x09 | Find Villain  | 1000  | Adventure   |
| 0x0A | Castle Gate   | 1000  | Adventure   |
| 0x0B | Town Gate     | 500   | Adventure   |
| 0x0C | Instant Army  | 1000  | Adventure   |
| 0x0D | Raise Control | 500   | Adventure   |

Rules:

- Spell purchased at a town; 1 purchase per visit; costs gold per
  `spell_costs[i]`; increments `game->spells[i]` by 1.
- Max total spells held = `game->max_spells`. Attempting to buy
  past the cap shows: "You have learned your / maximum number of
  spells."
- Can only be bought if `game->knows_magic` is set (Sorceress
  starts with it; others must pay 5000 at the Alcove).
- Bridge spell is only ever sold in Hunterville — hardcoded via
  `game->town_spell[0x15] = 0x07` in `salt_spells` (play.c:351).
  Other spells are randomly assigned to the other 25 towns;
  any remaining unassigned towns get a random spell id.
- During combat, `combat->spells` tracks spells cast this round;
  openkb enforces "only 1 spell per round" with: if
  `combat->spells != 0`, reject with "Only 1 spell per round!"
  top box (`choose_spell`, game.c:4144).
- In combat mode, only the first 7 spells are selectable; in
  adventure mode, only the last 7.

### 10.1 Spell naming

```c
char spell_names[MAX_SPELLS][32] = {
    "Clone", "Teleport", "Fireball", "Lightning", "Freeze",
    "Resurrect", "Turn Undead",
    "Bridge", "Time Stop", "Find Villain", "Castle Gate",
    "Town Gate", "Instant Army", "Raise Control",
};
```

These are referenced in:
- Town menu: "D) <SpellName> spell (<cost>)".
- Combat/adventure spell menu: column lists.
- Chest "new spell" reward: "<N> <SpellName> spell".
- "Select enemy army to <SpellName>" target prompt.

Names are module-overridable via `STRL_SPELLS`. DOS module
returns NULL (defaults stand); GNU reads `spells.ini`.

### 10.2 Spell costs

```c
word spell_costs[MAX_SPELLS] = {
    2000, 500, 1500, 500, 300, 5000, 2000,    // combat
    100, 200, 1000, 1000, 500, 1000, 500,     // adventure
};
```

Costs are module-overridable via `WDAT_SCOST`. Most expensive:
Resurrect (5000), Clone (2000), Turn Undead (2000), Fireball
(1500). Cheapest: Bridge (100), Time Stop (200), Freeze (300).

The Resurrect cost is high because the spell is supposed to
restore lost troops (currently a stub — §34.3).

### 10.3 Town distribution

`salt_spells` (play.c:335) at game creation:

```
for i = 0..MAX_CASTLES-1:
    town_spell[i] = 0xFF                 // unassigned

town_spell[0x15] = 0x07                   // Hunterville hardcoded:
                                          // sells Bridge

for i = 0..MAX_SPELLS-1:                   // 0..13
    if i == 0x07: continue                // Bridge (already placed)
    do:
        j = KB_rand(0, MAX_CASTLES-1)
    while town_spell[j] != 0xFF           // find empty town
    town_spell[j] = i

for i = 0..MAX_CASTLES-1:                  // fill any leftovers
    if town_spell[i] == 0xFF:
        town_spell[i] = KB_rand(0, MAX_SPELLS-1)
```

So 1 spell is hardcoded (Bridge in Hunterville), 13 spells are
assigned to random unique towns, leaving 26 - 14 = 12 towns
with random duplicates. This means each spell is sold in at
least one town (Bridge guaranteed), and most spells in 2-3 towns.

### 10.4 Buying a spell

`visit_town` "D) <SpellName> spell" branch (game.c:2742):

```
known = sum(game->spells[0..13])
if known >= game->max_spells:
    "You have learned your maximum number of spells."
    return
if game->gold < spell_costs[town_spell[id]]:
    "You don't have enough gold!"
    return
game->spells[town_spell[id]] += 1
game->gold -= spell_costs[town_spell[id]]
left = game->max_spells - known - 1
"You can learn N more spell[s]."
```

Plural handling: "spell" if N==1, else "spells". So `left=0` →
"You can learn 0 more spells." (plural, since 0 ≠ 1).

`max_spells` includes spells purchased AND the +N from chest
events. `max_spells` is multiplied by 2 if Ring of Heroism
picked up.

### 10.5 Casting a spell — `choose_spell`

`choose_spell(game, combat)` in game.c:4130. Pass NULL for
adventure-mode (use last 7 spells), pointer for combat-mode
(first 7).

Pre-checks:
1. If `!game->knows_magic`: show `no_spell_banner` (§18) and
   return -1.
2. If `combat` and `combat->spells != 0`: "Only 1 spell per
   round!" top box, KB_Wait, return -2.

Then displays the menu (15-row × 36-col centered box):
```
              Spells

     Combat         Adventuring
 N Clone           Bridge        N
 N Teleport        Time Stop     N
 N Fireball        Find Villain  N
 N Lightning       Castle Gate   N
 N Freeze          Town Gate     N
 N Resurrect       Instant Army  N
 N Turn Undead     Raise Control N

Cast which Combat spell (A-G)?
```

(Or "Cast which Adventure spell (A-G)?" in adventure mode.)

The hot-spots A..G are bound to the active half. So A in combat
maps to Clone (id 0); A in adventure maps to Bridge (id 7).

### 10.6 Spell dispatch

After A..G is read:
```
spell_id = key - 1 + (half * mode)        // half=7, mode=1 for adventure
if game->spells[spell_id] == 0:
    "You don't know that spell!"
    return
switch(spell_id):
    case 0:  clone_army(game, combat)
    case 1:  teleport_army(game, combat)
    case 2:  damage_army(game, combat, 25, 2)    // Fireball
    case 3:  damage_army(game, combat, 10, 3)    // Lightning
    case 4:  freeze_army(game, combat)
    case 5:  resurrect_army(game, combat)
    case 6:  damage_army(game, combat, 50, 6)    // Turn Undead
    case 7:  build_bridge(game)
    case 8:  time_stop(game)
    case 9:  find_villain(game)
             draw_map(game, 0)
             draw_sidebar(game, 0)
             view_contract(game)
    case 10: select_gate(game, 0)        // Castle Gate
    case 11: select_gate(game, 1)        // Town Gate
    case 12: instant_army(game)
    case 13: raise_control(game)
game->spells[spell_id] -= 1
if combat: combat->spells += 1
```

### 10.7 In-combat spell limit

Per `combat->spells`: only 1 spell per round. Reset to 0 at
`reset_turn` (when sides swap or phase advances). Pre-check
re-reads it; on attempted cast: "Only 1 spell per round!" +
`KB_Wait()` (1.5 sec).

This means a player can cast 1 combat spell per side-turn
(player turn → 1 spell; AI turn doesn't cast spells in current
code). Total in a 5-turn match: ≤5 spells.

### 10.8 Stub spells (incomplete)

In current openkb source, three combat spells don't fully
function:

- **Fireball** (id 2): `damage_army(game, combat, 25, 2)` —
  picks target via `pick_target` filter 4 but does not invoke
  `magic_damage`. Spell visually casts but no damage applied.
- **Lightning** (id 3): same. base=10 instead of 25.
- **Turn Undead** (id 6): same. base=50.
- **Resurrect** (id 5): `resurrect_army(game, war)` picks a
  friendly target via filter 3 then returns; no resurrection
  logic.

The `magic_damage(game, war, side, id, base, filter)` function
exists (play.c:1798) and would correctly compute
`damage = base * spell_power`, applying it via `deal_damage`
with `is_external=1, external_damage=damage`. It's just not
hooked into damage_army.

A clean-room reimplementer should fix these stubs (see §34.3).

### 10.9 Spell power and effects

`spell_power` multiplies most spell magnitudes:

| Spell           | Magnitude formula                           |
|-----------------|---------------------------------------------|
| Clone           | `(spell_power * 10 + injury) / hp`          |
| Time Stop       | `+ spell_power * 10` steps                  |
| Find Villain    | (sets KBCASTLE_KNOWN, 0 power)              |
| Raise Control   | `+ spell_power * 100`                       |
| Instant Army    | `(spell_power + 1) * inst_mul[rank]`        |
| Fireball/Lightning/Turn Undead | `base * spell_power` (when wired) |
| Freeze          | (sets `frozen = 1`, 0 power)                |
| Resurrect       | (intended: heal injury; not implemented)    |
| Bridge          | (places 2 tiles, 0 power)                   |
| Castle/Town Gate| (teleport, 0 power)                         |
| Teleport        | (move target, 0 power)                      |

So magic damage scales with spell_power. A Sorceress at rank 3
has spell_power=15: Fireball would do `25 × 15 = 375` damage
(when wired); Lightning 150; Turn Undead 750 vs undead.

### 10.10 Module override

```
WDAT_SCOST          → spell_costs[14]
STRL_SPELLS         → spell_names[14]
```

DOS module: returns nothing (defaults). GNU module: reads
`spells.ini`.

## 11. The world — continents, castles, towns, coordinates

The game world consists of 4 continents, each 64×64 tiles.
Connected by ship (rented at any town). Each continent has:
- 1 sea entry point (`continent_entry[c]`).
- 6-11 castles.
- 6-11 towns.
- 11 dwellings (max).
- 2 teleport caves.
- 1 orb chest, 1 navmap chest (cont 0..2 only).
- 2 artifact chests.
- 5 friendly foes + up to 35 hostile foes (40 total).

### 11.0 World loading

`DAT_WORLD` resource = the file `LAND.ORG` (DOS) or `land.org`
(Free), containing 4 × 64 × 64 = 16384 raw tile bytes. Layout:
continent 0 first (4096 bytes), then cont 1, 2, 3.

Loaded by the resolver at game creation:
```c
byte *land = KB_Resolve(DAT_WORLD, 0);
... pass to spawn_game ...
memcpy(game->map, land, LEVEL_W * LEVEL_H * MAX_CONTINENTS);
free(land);
```

The world map is then "salted" by `salt_continent` (§14.7) for
each continent — this places dwellings/foes/chests/artifacts/
caves at the locations of the existing chest tiles (0x8B) in
`LAND.ORG`. So the original map file determines where chests
**can** be; salt determines what they **become**.

### 11.1 Continent names

```
continent_names[4] = { "Continentia", "Forestria", "Archipelia", "Saharia" };
```

### 11.2 Continent entry points (arrival after sail)

`continent_entry[4][2]` = `{x, y}`:

```
0 Continentia:  11,  3
1 Forestria:     1, 37
2 Archipelia:   14, 62
3 Saharia:       9,  1
```

When you sail to continent N, your `x,y,last_x,last_y,boat_x,boat_y`
all become that continent's entry coords, and `boat = continent`.

### 11.3 Town names (26)

```
0x00 Riverton         0x0D King's Haven
0x01 Underfoot        0x0E Bayside
0x02 Path's End       0x0F Nyre
0x03 Anomaly          0x10 Dark Corner
0x04 Topshore         0x11 Isla Vista
0x05 Lakeview         0x12 Grimwold
0x06 Simpleton        0x13 Japper
0x07 Centrapf         0x14 Vengeance
0x08 Quiln Point      0x15 Hunterville
0x09 Midland          0x16 Fjord
0x0A Xoctan           0x17 Yakonia
0x0B Overthere        0x18 Woods End
0x0C Elan's Landing   0x19 Zaezoizu
```

### 11.4 Castle names (26 + 1 for home)

```
0x00 Azram        0x0A Kookamunga      0x14 Uzare
0x01 Basefit      0x0B Lorsche         0x15 Vutar
0x02 Cancomar     0x0C Mooseweigh      0x16 Wankelforte
0x03 Duvock       0x0D Nilslag         0x17 Xelox
0x04 Endryx       0x0E Ophiraund       0x18 Yeneverre
0x05 Faxis        0x0F Portalis        0x19 Zyzzarzaz
0x06 Goobare      0x10 Quinderwitch    0x1A "of King Maximus"  (home)
0x07 Hyppus       0x11 Rythacon
0x08 Irok         0x12 Spockana
0x09 Jhan         0x13 Tylitch
```

### 11.5 Town-to-castle inversion table

Every town has a paired castle whose name letter matches (A→A, B→B).
`town_inversion[town_id] = castle_id`. Used in the Castle Gate and
Town Gate spells; the A-Z letter grid in the gate UI is sorted by
castle letter, so Z for town 0x19 really resolves to Zaezoizu
(town 0x19). The whole point of this table is to let a player press
'A' through 'Z' and have it route to the right gate target.

```
town 0x00 Riverton      → castle 0x11 Rythacon
town 0x01 Underfoot     → castle 0x14 Uzare
town 0x02 Path's End    → castle 0x0F Portalis
town 0x03 Anomaly       → castle 0x00 Azram
town 0x04 Topshore      → castle 0x13 Tylitch
town 0x05 Lakeview      → castle 0x0B Lorsche
town 0x06 Simpleton     → castle 0x12 Spockana
town 0x07 Centrapf      → castle 0x02 Cancomar
town 0x08 Quiln Point   → castle 0x10 Quinderwitch
town 0x09 Midland       → castle 0x0C Mooseweigh
town 0x0A Xoctan        → castle 0x17 Xelox
town 0x0B Overthere     → castle 0x0E Ophiraund
town 0x0C Elan's Landing→ castle 0x04 Endryx
town 0x0D King's Haven  → castle 0x0A Kookamunga
town 0x0E Bayside       → castle 0x01 Basefit
town 0x0F Nyre          → castle 0x0D Nilslag
town 0x10 Dark Corner   → castle 0x03 Duvock
town 0x11 Isla Vista    → castle 0x08 Irok
town 0x12 Grimwold      → castle 0x06 Goobare
town 0x13 Japper        → castle 0x09 Jhan
town 0x14 Vengeance     → castle 0x15 Vutar
town 0x15 Hunterville   → castle 0x07 Hyppus
town 0x16 Fjord         → castle 0x05 Faxis
town 0x17 Yakonia       → castle 0x18 Yeneverre
town 0x18 Woods End     → castle 0x16 Wankelforte
town 0x19 Zaezoizu      → castle 0x19 Zyzzarzaz
```

### 11.6 Castle coordinates

`castle_coords[26][3]` = `{continent, x, y}`:

```
 0 Azram         cont 0   30, 27
 1 Basefit       cont 1   47,  6
 2 Cancomar      cont 0   36, 49
 3 Duvock        cont 1   30, 18
 4 Endryx        cont 2   11, 46
 5 Faxis         cont 0   22, 49
 6 Goobare       cont 2   41, 36
 7 Hyppus        cont 2   43, 27
 8 Irok          cont 0   11, 30
 9 Jhan          cont 1   41, 34
 A Kookamunga    cont 0   57, 58
 B Lorsche       cont 2   52, 57
 C Mooseweigh    cont 1   25, 39
 D Nilslag       cont 0   22, 24
 E Ophiraund     cont 0    6, 57
 F Portalis      cont 0   58, 23
10 Quinderwitch  cont 1   42, 56
11 Rythacon      cont 0   54,  6
12 Spockana      cont 3   17, 39
13 Tylitch       cont 2    9, 18
14 Uzare         cont 3   41, 12
15 Vutar         cont 0   40,  5
16 Wankelforte   cont 0   40, 41
17 Xelox         cont 2   45,  6
18 Yeneverre     cont 1   19, 19
19 Zyzzarzaz    cont 3   46, 43
```

### 11.7 Town coordinates

`town_coords[26][3]` = `{continent, x, y}`:

```
 0 Riverton      cont 0   29, 12
 1 Underfoot     cont 1   58,  4
 2 Path's End    cont 0   38, 50
 3 Anomaly       cont 1   34, 23
 4 Topshore      cont 2    5, 50
 5 Lakeview      cont 0   17, 44
 6 Simpleton     cont 2   13, 60
 7 Centrapf      cont 2    9, 39
 8 Quiln Point   cont 0   14, 27
 9 Midland       cont 1   58, 33
 A Xoctan        cont 0   51, 28
 B Overthere     cont 2   57, 57
 C Elan's Landing cont 1   3, 37
 D King's Haven  cont 0   17, 21
 E Bayside       cont 0   41, 58
 F Nyre          cont 0   50, 13
10 Dark Corner   cont 1   58, 60
11 Isla Vista    cont 0   57,  5
12 Grimwold      cont 3    9, 60
13 Japper        cont 2   13,  7
14 Vengeance     cont 3    7,  3
15 Hunterville   cont 0   12,  3
16 Fjord         cont 0   46, 35
17 Yakonia       cont 2   49,  8
18 Woods End     cont 1    3,  8
19 Zaezoizu      cont 3   58, 48
```

### 11.8 Town-gate coordinates (for Town Gate spell)

`towngate_coords[26][3]` = `{continent, x, y}` (hex in source):

```
 0    cont 0   0x1d, 0x0b      ( 29, 11 )
 1    cont 1   0x39, 0x04      ( 57,  4 )
 2    cont 0   0x26, 0x31      ( 38, 49 )
 3    cont 1   0x23, 0x17      ( 35, 23 )
 4    cont 2   0x05, 0x31      (  5, 49 )
 5    cont 0   0x10, 0x2c      ( 16, 44 )
 6    cont 2   0x0c, 0x3c      ( 12, 60 )
 7    cont 2   0x09, 0x26      (  9, 38 )
 8    cont 0   0x0d, 0x3c      ( 13, 60 )
 9    cont 1   0x39, 0x21      ( 57, 33 )
 A    cont 0   0x33, 0x1d      ( 51, 29 )
 B    cont 2   0x39, 0x38      ( 57, 56 )
 C    cont 1   0x03, 0x24      (  3, 36 )
 D    cont 0   0x10, 0x15      ( 16, 21 )
 E    cont 0   0x28, 0x3a      ( 40, 58 )
 F    cont 0   0x32, 0x0e      ( 50, 14 )
10    cont 1   0x3a, 0x3b      ( 58, 59 )
11    cont 0   0x38, 0x05      ( 56,  5 )
12    cont 3   0x09, 0x3b      (  9, 59 )
13    cont 2   0x0d, 0x08      ( 13,  8 )
14    cont 3   0x06, 0x03      (  6,  3 )
15    cont 0   0x0c, 0x04      ( 12,  4 )
16    cont 0   0x2f, 0x23      ( 47, 35 )
17    cont 2   0x32, 0x08      ( 50,  8 )
18    cont 1   0x03, 0x09      (  3,  9 )
19    cont 3   0x3a, 0x2f      ( 58, 47 )
```

### 11.9 Boat coordinates (where a rented boat drops the player)

`boat_coords[26][3]` = `{continent, x, y}`, hex:

```
 0    cont 0   0x1e, 0x0b
 1    cont 1   0x3b, 0x05
 2    cont 0   0x27, 0x32
 3    cont 1   0x22, 0x16
 4    cont 2   0x04, 0x31
 5    cont 0   0x12, 0x2c
 6    cont 2   0x0e, 0x3c
 7    cont 2   0x0a, 0x26
 8    cont 0   0x0e, 0x1a
 9    cont 1   0x3a, 0x20
 A    cont 0   0x33, 0x1b
 B    cont 2   0x3a, 0x38
 C    cont 1   0x02, 0x24
 D    cont 0   0x12, 0x15
 E    cont 0   0x29, 0x39
 F    cont 0   0x31, 0x0c
10    cont 1   0x3b, 0x3c
11    cont 0   0x38, 0x04
12    cont 3   0x0a, 0x3c
13    cont 2   0x0c, 0x07
14    cont 3   0x08, 0x03
15    cont 0   0x0b, 0x03
16    cont 0   0x2f, 0x24
17    cont 2   0x32, 0x09
18    cont 1   0x02, 0x09
19    cont 3   0x3b, 0x30
```

When you rent a boat in town `t`, boat_x = `boat_coords[t][1]`,
boat_y = `boat_coords[t][2]`, `boat = game->continent`.

### 11.9.1 How boat coords are used

When a player rents a boat in town `t` (visit_town option B):
```
game->gold -= boat_cost(game)
game->boat = game->continent
game->boat_x = boat_coords[t][1]
game->boat_y = boat_coords[t][2]
```

So the boat appears at its hardcoded coords near each town.
When the player walks onto that tile, they board (mount=SAIL).

Note: `boat_coords[t][0]` (continent) is NOT used here; the
continent is set to the player's current. So renting a boat in
Hunterville on Continentia gives a Continentia boat. Renting in
the same town from a different continent (impossible, since
Hunterville is on Continentia, but the code allows for the
hypothetical) would use the player's current continent.

### 11.10 Castle difficulty (`castle_difficulty`)

Every castle has a difficulty tier 0..3 used when monsters are
(re)populated; tier selects the row of `troop_chance_table`:

```
c0=0  c6=2  cC=1  c12=3  c18=1
c1=1  c7=2  cD=0  c13=2  c19=3
c2=0  c8=0  cE=0  c14=3
c3=1  c9=1  cF=0  c15=0
c4=2  cA=0  c10=1 c16=0
c5=0  cB=2  c11=0 c17=2
```

### 11.11 Special places

```
special_coords[2][3] = {
    { HOME_CONTINENT=0, HOME_X=11, HOME_Y=7 },   // SP_HOME
    { ALCOVE_CONTINENT=0, ALCOVE_X=11, ALCOVE_Y=19 }, // SP_ALCOVE
};
```

Home castle is on Continentia at (11,7); Archmage Aurange's Alcove
on Continentia at (11,19). The home castle is distinct from any
`castle_coords[]` entry — its id is `MAX_CASTLES` (26) when
referenced (see `visit_home_castle`, game.c:2247). The Alcove is
implemented as a dwelling tile at its fixed coords.

### 11.12 Module override paths

Most coords are module-overridable:

```
DAT_CASTLEX/Y/C   → castle_coords[26][3]
DAT_TOWNX/Y/C     → town_coords[26][3]
DAT_TOWNINV       → town_inversion[26]
DAT_BOATX/Y       → boat_coords[26][1..2]   (continent from TOWNC)
DAT_GATEX/Y       → towngate_coords[26][1..2] (continent from TOWNC)
DAT_NAVX/Y        → continent_entry[4][2]
DAT_SPECIALX/Y/C  → special_coords[2][3]
```

DOS module returns NULL (defaults from bounty.c stand). GNU
module reads `castles.ini`, `towns.ini`, `land.ini`. So a
clean-room reimplementation can ship its own world by providing
these resources.

### 11.13 World map mutation

After game creation, the map mutates from these triggers:
- Foe killed: tile cleared to 0.
- Chest taken: tile cleared to 0.
- Artifact taken: tile cleared to 0.
- Bridge built: water tiles → bridge tiles.
- Foe follow: old tile → 0, new tile → 0x91.
- Boat boarded: boat moves with player.
- Boat dropped: tile is unchanged (boat sprite tracks coords).
- Castle captured: no tile change (castle stays).
- Aurange paid: alcove tile cleared.
- Telecave: no tile change (still 0x8E).

The map is saved verbatim to disk (16384 bytes at offset 0x0FC5
of save file).

## 12. Tiles and map conventions

The world is a tile-based map: 64×64 cells per continent, each
cell is a single byte storing a "tile id". The tile id selects
both what visual graphic is rendered AND what behavior the cell
has when the player steps on it. The high bit (0x80) marks
"interactive" tiles that need adventure-loop dispatching.

### 12.1 Tile constants

From `kbres.h`:

```
TILE_GRASS      = 0x00
TILE_BRIDGE_H   = 0x08
TILE_BRIDGE_V   = 0x09
TILE_DEEP_WATER = 0x20     // = 32
TILE_CASTLE     = 0x85
TILE_TOWN       = 0x8A
TILE_CHEST      = 0x8B
TILE_DWELLING_1 = 0x8C     // plains dwelling
TILE_DWELLING_2 = 0x8D     // forest dwelling
TILE_DWELLING_3 = 0x8E     // hill/cave dwelling (also TILE_TELECAVE)
TILE_DWELLING_4 = 0x8F     // dungeon dwelling
TILE_SIGNPOST   = 0x90
TILE_FOE        = 0x91
TILE_ARTIFACT_1 = 0x92     // first artifact variant
TILE_ARTIFACT_2 = 0x93     // second artifact variant
```

Note: `TILE_TELECAVE == TILE_DWELLING_3 == 0x8E`. Whether a 0x8E
tile is a teleport cave or an actual hill/cave dwelling is
determined by whether its coords match `game->teleport_coords`
(see `visit_telecave`).

### 12.2 Tile predicates

Macros (`kbres.h`):

```
IS_GRASS(m)       = (m < 2) || (m == 0x80)
IS_CASTLE(m)      = (m >= 0x02 && m <= 0x07)
IS_MAPOBJECT(m)   = (m >= 0x0a && m <= 0x13)
IS_BRIDGE(m)      = (m >= 0x08 && m <= 0x09)
IS_WATER(m)       = (m >= 0x14 && m <= 0x20)
IS_TREE(m)        = (m >= 0x21 && m <= 0x2d)
IS_DESERT(m)      = (m >= 0x2e && m <= 0x3a)
IS_ROCK(m)        = (m >= 0x3b && m <= 0x47)
IS_DEEP_WATER(m)  = (m == 0x20)
IS_INTERACTIVE(m) = (m & 0x80) != 0
```

Tile ranges summary:

- `0x00..0x07` — grass + castle "decorations". 0x00 grass; 0x02–0x07
  are castle-walls in map tileset (castle tile proper is 0x85, but
  the visual graphic comes from 0x02..0x07 range when drawing).
- `0x08..0x09` — bridges (horizontal, vertical).
- `0x0a..0x13` — "map objects" — trees/rocks that are non-interactive
  but are decorative features.
- `0x14..0x1f` — shallow water (various coast/pool tiles).
- `0x20` — deep water.
- `0x21..0x2d` — trees.
- `0x2e..0x3a` — desert/sand variants.
- `0x3b..0x47` — rock variants.
- `0x48..0x7f` — additional tile graphics for furnishing
  (base offsets into the tileset during furnish_map; see §12.5).
- `0x80..0xff` — interactive tile flag (high bit). Low bits under
  the 0x80 mask name the specific object (0x85=castle, 0x8A=town,
  0x8B=chest, 0x8C..0x8F=dwellings, 0x90=sign, 0x91=foe,
  0x92/0x93=artifacts).

`KB_GetMapTile(game, cont, y, x)` masks with `& 0x7F` before returning
the value for rendering, because the tileset is 128-tile-wide. But
`IS_INTERACTIVE` stays on the full byte.

### 12.3 Map dimensions

All continents are 64×64 tiles. Stored as `game->map[cont][y][x]`.
Fog of war: 1 byte per tile (`game->fog[cont][y][x] = 0 or 1`).
`clear_fog` sets fog=1 in a 5×5 square centered on the player
(±2 in each axis, clipped to map bounds).

### 12.4 World file (`DAT_WORLD`)

The complete baseline map ships as `LAND.ORG` (DOS) or `land.org`
(Free module). Size: `4 * 64 * 64 = 16384` bytes, raw tile-id
per-byte, continent after continent. On new game, the whole file
is `memcpy`'d into `game->map` (play.c:382), then salted.

### 12.5 Map "furnishing" (`furnish_map`, rogue.c)

A non-faithful, post-generation decoration pass. Walks the entire
map; for each non-interactive, non-castle, non-mapobject,
non-bridge tile, looks at 8 neighbors. If any differ from the
center tile's **type** (water/tree/sand/rock), chooses one of 12
edge/corner variant tiles using `base_offset[type] +
tile_offset[type][dir]`:

```
base_offset[4] = { 20, 33, 46, 59 };   // water, tree, sand, rock
tile_offset[4][12] = {
    {0,1,3,2,8,9,11,10,5,7,4,6},       // water
    {2,0,3,1,8,9,11,10,5,7,4,6},       // tree
    {2,0,3,1,8,9,11,10,5,7,4,6},       // sand
    {2,0,3,1,8,9,11,10,5,7,4,6},       // rock
};
```

Direction priority order (most → least): NE, NW, SE, SW, E, W, S,
N, NE-only, NW-only, SE-only, SW-only. Out-of-bounds neighbors
count as deep water.

rogue.c is explicitly "not part of the original game mechanics".

### 12.6 Interactive tile dispatching

The adventure loop checks `IS_INTERACTIVE(m)` after each move
and dispatches based on the tile id (game.c:6522):

```c
if (IS_INTERACTIVE(m) && game->mount != KBMOUNT_FLY):
    switch (m):
        case TILE_CASTLE:    visit_castle(game); walk = 0; break;
        case TILE_TOWN:      visit_town(game); walk = 0; break;
        case TILE_CHEST:     take_chest(game); break;
        case TILE_DWELLING_4:
            if (!visit_telecave(game, 0)) break;   // fallthrough on no telecave
        case TILE_DWELLING_1:
        case TILE_DWELLING_2:
        case TILE_DWELLING_3:
            walk = visit_dwelling(game, m - TILE_DWELLING_1); break;
        case TILE_SIGNPOST:  read_signpost(game); break;
        case TILE_FOE:       walk = !attack_foe(game); break;
        case TILE_ARTIFACT_1:
        case TILE_ARTIFACT_2:
            take_artifact(game, m - TILE_ARTIFACT_1); break;
        default:
            "Unknown interactive tile"
```

Note: `TILE_DWELLING_4` (0x8F = dungeon) doesn't fall through to
the dwelling switch; it falls through specifically because the
`if !visit_telecave` for slot-4 dwellings... wait, looking again:
the code does:
```c
case TILE_DWELLING_4:
    if (!visit_telecave(game, 0)) break;
```
This means for tile 0x8F (dwelling_4), call `visit_telecave`
first; if it returns 0 (it's a telecave), break (no further
dispatch). Otherwise fall through to dwelling. But wait, telecaves
are tile 0x8E (DWELLING_3), not 0x8F. The code check for
DWELLING_4 calling visit_telecave is redundant — visit_telecave
checks coords against `teleport_coords`, which were set up for
0x8E tiles only. So this branch never actually triggers a teleport
from a dungeon tile. It's likely a copy-paste error or
defensive handling.

For 0x8E (DWELLING_3, hill/cave), the adventure loop reaches it
via the DWELLING_3 case, but **`visit_telecave` was already
called inline in the DWELLING_4 case above**. Wait, the
fallthrough is from DWELLING_4 to DWELLING_1. So for DWELLING_3
(0x8E), it reaches `visit_dwelling(game, 2)` only.

Actually re-reading game.c:6527:
```c
case TILE_DWELLING_4:
    if (!visit_telecave(game, 0)) break;       // returns 1 if NOT a telecave
case TILE_DWELLING_3:                            // fallthrough
case TILE_DWELLING_2:
case TILE_DWELLING_1:
    walk = visit_dwelling(game, m - TILE_DWELLING_1);
    break;
```

The `visit_telecave` returns 0 if it WAS a telecave (and it
teleported the player), and 1 if it wasn't. The "if (!result)
break" means "if it was a telecave, break". So this guards
DWELLING_4 entry with a telecave check first.

But telecaves are at 0x8E (DWELLING_3), not 0x8F (DWELLING_4).
This entire branch is dead in practice. `visit_telecave` walks
`teleport_coords` and returns 1 (not a cave) for non-cave tiles.
So `if (!1)` is false; the break is never taken; falls through
to `visit_dwelling(game, 3)` (dungeon).

Conclusion: the visit_telecave call in DWELLING_4 is a leftover
that does nothing useful. Cleanly: `visit_dwelling(game, 3)` is
called for dungeon tiles. For DWELLING_3 (0x8E), it falls to
`visit_dwelling(game, 2)` (hill/cave dwelling) — but the player
might first be teleported by `visit_telecave` called inline in
the *movement* code earlier.

Re-reading game.c:6494:
```c
if (m == TILE_TELECAVE && !visit_telecave(game, 1)) {
    ...
    walk = 0;
}
```

This is during the move itself, BEFORE the interaction switch.
So if the player moves to a 0x8E tile that IS a telecave,
`visit_telecave(game, 1)` teleports them (force=1 means actually
teleport) and returns 0, so the if fires; `walk = 0` interrupts
the move (the player ends at the tele-destination). The
post-move interaction switch then runs at the new tile, not the
old one. If the dest is also 0x8E (likely, since telecaves are
paired), `visit_telecave` is called again with force=0 (returns
0, telecave found, no-op), then falls to `visit_dwelling(game,
2)` for the hill/cave dwelling. Wait, but that would mean
visiting a telecave also triggers dwelling visit at the
destination. Actually no — at the destination, the
`map[continent][y][x]` is 0x8E, the move sequence includes
"if telecave, teleport + walk = 0". After teleport, `walk = 0`
prevents the interaction switch from running.

So the sequence is:
1. Player moves toward 0x8E tile.
2. Move logic: `visit_telecave(game, 1)` teleports to paired cave.
3. `walk = 0` prevents the interaction switch.
4. Player is at destination, no further action.

This is correct telecave handling. The DWELLING_4 fallthrough
is indeed redundant (or perhaps intended for a future "telecave
in dungeon" feature).

### 12.7 Tile rendering pipeline

`KB_GetMapTile(game, continent, y, x)`:

```c
return (y >= 0 && y < LEVEL_H && x >= 0 && x < LEVEL_W)
       ? game->map[continent][y][x] & 0x7F
       : TILE_DEEP_WATER;
```

So tile id 0x85 (castle) becomes 5 for tileset lookup; tile id
0x91 (foe) becomes 17. The high bit is stripped because the
tileset only has 128 cells (8 × 9 = 72 actually used + padding).

Out-of-bounds tiles are forced to deep water (0x20). This means
the edge of the map looks like ocean.

`KB_DrawMapTile(dest, dest_rect, tileset, m)`:

```c
th = m / 8        // row 0..8
tw = m - (th * 8) // col 0..7
src.x = tw * src.w
src.y = th * src.h
SDL_BlitSurface(tileset, &src, dest, dest_rect)
```

So tile id 0 is at top-left of tileset, id 71 (= 8*8+7) is at
row 8 col 7. Tiles 72+ would be out-of-tileset (probably crash
or render garbage); never expected since map values stay in
0..71 after the `& 0x7F` mask.

### 12.8 Map viewport and camera

`draw_map(game, tick)`:

- Viewport size: `local.map.w × local.map.h` pixels = 240 × 170
  (in 1× mode). With 48×34 tiles, this is exactly 5×5 tiles
  visible.
- Center: `radii_w = (5-1)/2 = 2`; tiles laid out from
  (-2, -2) to (+2, +2) relative to player.
- Y-axis flipped on render: `pos.y = (perim_h - 1 - j) * pos.h`,
  so j=0 (north) is at the top of the viewport.

When the player moves north (y+=1), the map redraws with the
player's row shifted appropriately. The player sprite stays
centered.

### 12.9 Boat rendering

If `game->mount != KBMOUNT_SAIL` AND `game->boat == game->continent`:
the boat is at `(boat_x, boat_y)` on the same continent. The
sprite is drawn at its world position via:

```
boat_lx = boat_x - x + radii_w
boat_ly = y - boat_y + (perim_w - 1 - radii_h)
```

Within visible bounds: blit hero sprite (frame 0) at that
local position with vertical flip if `local.boat_flip` set.

Note: when sailing, the boat doesn't render separately (the
hero sprite IS the boat — KBMOUNT_SAIL state shows boat). So
the separate boat-render branch is for "boat parked, player on
foot" only.

### 12.10 Player rendering

`draw_player(game, frame)`:

```
hsrc.x = tile.w * (mount + frame)
hsrc.y = tile.h * local.hero_flip
hdst at (radii_w, perim_h - 1 - radii_h) center
```

So the hero sprite is always at the viewport center (2, 2 in
the 5×5 tile grid). The animation frame cycles 0..3 driven by
SYN ticks (~150ms).

`mount` value (0/4/8) selects which 4-frame strip to use:
- 0..3: SAIL frames
- 4..7: FLY frames
- 8..11: RIDE frames

The sprite sheet has 12 columns × 2 rows (mirrored). With
horizontal flip via `local.hero_flip`, all 8 facings (4 mount-
states × 2 directions) are covered.

## 13. Random generation — chance tables and salting

The RNG drives world salting (random villain placement), troop
generation (foe armies, castle garrisons), end-of-week creature
selection, and combat rolls (damage, special abilities). All
randomness flows through one function.

### 13.1 `KB_rand` — uniform integer in `[min, max]` inclusive

```c
int KB_rand(int min, int max) {
    return (int)(rand() % (max - min + 1) + min);
}
```

Relies on libc `rand()` and `srand()`. Implementation details:

- **Bias**: standard `rand() % N` has slight non-uniformity for
  values of N that don't divide RAND_MAX evenly. RAND_MAX is
  typically 2^31-1 (Linux glibc) or 32767 (Windows). For openkb's
  typical use (N ≤ 100), the bias is negligible (<<0.1%).
- **Seeding**: the game does NOT seed at startup. Whatever
  `rand()`'s default seed is on the host OS is used. On Linux
  glibc, the default seed is 1, so without explicit seeding the
  same sequence of random values is produced every run.
  Players can use cheat 'R' to enter a seed number → calls
  `srand(value)`.
- **Threading**: single-threaded use. The audio callback (a
  separate thread) doesn't call `rand`.

**Implication for clean-room reimplementation**: To match exact
DOS behavior, you'd need to know what RNG the original game
used. openkb just uses libc, which is portable but not faithful
to DOS-specific PRNG (typically Watcom or Borland C runtime).

### 13.1.1 Common ranges used

```
KB_rand(1, 100)    — chest roll (chance percentile)
KB_rand(0, 13)     — random spell id for filler towns
KB_rand(0, 25)     — random town id during salt_spells
KB_rand(0, 0xFF)   — scepter XOR key (1 byte)
KB_rand(100, 400)  — scepter continent bias roll
KB_rand(0, grass-1) — scepter position
KB_rand(0, MAX_FOES-1) — foe placement during salt
KB_rand(1, MAX_TROOPS-1) — astrology week creature (excludes 0)
KB_rand(0, 3)      — dwelling kind (in roll_creature)
KB_rand(troop.melee_min, troop.melee_max) — melee damage roll
KB_rand(troop.ranged_min, troop.ranged_max) — ranged damage roll
KB_rand(1, 9)      — random troop slot for menu flavor
KB_rand(0, MAX_ARTIFACTS-1) — barrel slot during salt_continent
KB_rand(0, 0xFFFFFF) — minimap blink color
```

### 13.1.2 Cheat seeding

`debug_cheat_menu` 'R' (game.c:4939):
```
text = text_input(4, numbers_only=1, ...)
val = atoi(text)
srand(val)
```

So the player can deterministically seed for reproducibility.
The 4-char limit caps the seed at 9999.

### 13.2 Creature roll (`roll_creature`)

Used to populate both castle garrisons and foe encounters. Picks
one of 5 dwelling/troop categories per roll, weighted. Arguments:
`difficulty` 0..3.

```
dwelling = KB_rand(0, 3);                          // plains/forest/hill/dungeon
chance   = KB_rand(1, 100);                        // percent
index    = 0;
while (chance > troop_chance_table[difficulty][index]) {
    index++;
    if (index >= MAX_TROOP_CHANCE_CURVE - 1) break;  // cap at 4
}
troop_id    = dwelling_to_troop[dwelling][index];
troop_count = troop_numbers[troop_id][difficulty];
if (troop_count <= 1) troop_count = 2;
```

`troop_chance_table[4][4]` (difficulty × thresholds):

```
diff 0 (cont 0): 0x3C, 0x5A, 0x62, 0x65   // 60, 90, 98, 101
diff 1 (cont 1): 0x14, 0x46, 0x5F, 0x63   // 20, 70, 95, 99
diff 2 (cont 2): 0x0A, 0x14, 0x32, 0x5A   // 10, 20, 50, 90
diff 3 (cont 3): 0x03, 0x06, 0x0A, 0x28   //  3,  6, 10, 40
```

After the while-loop ends, the troop index picked is in 0..4 and
represents a tier within the chosen dwelling's progression.

`dwelling_to_troop[4][5]` (dwelling × tier):

```
Plains   (0): 0x00 Peasants, 0x03 Wolves, 0x0b Nomads, 0x10 Barbarians, 0x14 Archmages
Forest   (1): 0x01 Sprites,  0x06 Gnomes, 0x09 Elves,  0x11 Trolls,     0x13 Druids
Hill     (2): 0x07 Orcs,     0x0c Dwarves,0x0f Ogres,  0x16 Giants,     0x18 Dragons
Dungeon  (3): 0x04 Skeletons,0x05 Zombies,0x0d Ghosts, 0x15 Vampires,   0x17 Demons
```

`troop_numbers[25][4]` (troop × difficulty, 0..3):

```
 0 Peasants   : 10, 20, 50, 100
 1 Sprites    : 20, 50, 100, 127
 2 Militia    : 10, 20, 50, 100
 3 Wolves     :  5, 15, 30, 80
 4 Skeletons  :  5, 10, 25, 50
 5 Zombies    :  5, 10, 25, 75
 6 Gnomes     : 10, 25, 50, 100
 7 Orcs       :  5, 15, 30, 80
 8 Archers    :  0,  0,  0,  0
 9 Elves      :  5, 10, 25, 50
 A Pikemen    :  0,  0,  0,  0
 B Nomads     :  4,  8, 15, 30
 C Dwarves    :  4, 10, 20, 50
 D Ghosts     :  2,  4, 10, 20
 E Knights    :  0,  0,  0,  0
 F Ogres      :  2,  4,  8, 15
10 Barbarians :  2,  4, 10, 20
11 Trolls     :  2,  4,  8, 15
12 Cavalry    :  0,  0,  0,  0
13 Druids     :  1,  3,  6, 10
14 Archmages  :  1,  2,  4,  8
15 Vampires   :  2,  4, 10, 25
16 Giants     :  1,  2,  5, 10
17 Demons     :  1,  2,  4,  8
18 Dragons    :  1,  1,  1,  2
```

Troops with all-zero rolls (Archers, Pikemen, Knights, Cavalry)
never appear from random rolls — they are castle-only recruits.

### 13.3 Chest chance curves (by continent)

Cumulative thresholds for treasure-chest content. A uniform
`KB_rand(1, 100)` is compared:

```
chance_for_gold[4]        = { 0x3d, 0x42, 0x4c, 0x47 }  // 61, 66, 76, 71
chance_for_commission[4]  = { 0x51, 0x56, 0x56, 0x51 }  // 81, 86, 86, 81
chance_for_spellpower[4]  = { 0x56, 0x5c, 0x5d, 0x5b }  // 86, 92, 93, 91
chance_for_maxspell[4]    = { 0x56, 0x5c, 0x5d, 0x5b }  // same as spellpower
chance_for_newspell[4]    = { 0x65, 0x65, 0x65, 0x65 }  // 101, 101, 101, 101
```

Interpretation (chest-handling code at game.c:3136):

```
if      (chance < chance_for_gold[cont])         → Gold/leadership choice
else if (chance < chance_for_commission[cont])   → +Commission
else if (chance < chance_for_spellpower[cont])   → +Spell Power (always 1)
else if (chance < chance_for_maxspell[cont])     → +Max Spells
else if (chance < chance_for_newspell[cont])     → +New Spell
else                                              → Empty ("The chest was empty!")
```

Note: `chance_for_spellpower == chance_for_maxspell` on all
continents. That means max_spell is never reached (since the
`<=` cascade hits it second and the equal value means the range
for maxspell is empty). This is either an intentional
simplification or a bug; openkb adds a warning "Chance tables for
treasure are impossible!" to KB_errlog when the empty branch is
hit.

### 13.4 Gold / commission / max_spell amounts

```
min_gold[4]       = { 0x00, 0x04, 0x09, 0x13 }   //  0,  4,  9, 19
max_gold[4]       = { 0x05, 0x10, 0x15, 0x1f }   //  5, 16, 21, 31
min_commission[4] = { 0x09, 0x31, 0x63, 0xc7 }   //  9, 49, 99, 199
max_commission[4] = { 0x0029, 0x0033, 0x0065, 0x012d } // 41, 51, 101, 301
base_maxspell[4]  = { 0x01, 0x01, 0x02, 0x02 }   //  1,  1,  2,  2
```

Gold chest payout: `points = KB_rand(1, max_gold[cont]) +
min_gold[cont]`; then `gold_amount = points * 100`,
`leadership_bonus = gold_amount / 50`. With Crown of Command,
leadership_bonus is doubled.

Commission chest: `points = KB_rand(1, max_commission[cont]) +
min_commission[cont]`; `commission += points`.

Max-spell chest: `points = base_maxspell[cont]` (always 1 or 2).
Doubled with Ring of Heroism. `max_spells += points`.

New-spell chest: `spell_type = KB_rand(0, MAX_SPELLS-1)`,
`spell_num = KB_rand(1, continent+1)`, `spells[spell_type] +=
spell_num`.

### 13.5 Number words (`number_name`)

Troop counts are bucketed for display as:

```
name                range
A multitude of      >= 500
A horde of          100..499
A lot of            50..99
Many                20..49
Some                10..19
A few                1..9
```

(The buckets live in `number_names[]` and `number_mins[]`, bounty.c
458-466.)

### 13.6 Morale display

Used by `view_army` sidebar: `morale_names[3] = {"Norm","Low","High"}`.

### 13.7 RNG dependencies summary

Where randomness affects gameplay, in approximate frequency order:

1. **At game creation** (`spawn_game`):
   - 1 byte (scepter key) + 1 ranged roll (scepter continent
     bias) + 1 unbounded roll (scepter grass position).
   - For each continent: ~5-10 rolls per chest tile (assignment
     to artifact/orb/navmap/telecave/dwelling/friendly), 5
     dwelling-troop rolls (per dwelling), 3 troop rolls per
     foe encounter × ~25 hostile foes = ~75 rolls per continent.
   - Castle population: 5 rolls per non-villain castle × ~15
     non-villain castles × 4 continents = ~300 rolls.
   - Villain placement: ~30-50 rejection-sampled rolls.
   - Spell distribution: 13 + ~12 fillers ≈ 25 rolls.

   Total at creation: ~500-1000 RNG calls.

2. **Each successful player move**: 0 calls (movement is
   deterministic).

3. **Each chest pickup**: 2-5 calls (chance + amount + spell type
   + spell num).

4. **Each foe encounter**:
   - Friendly: 3 calls (roll_creature: dwelling, chance, troop).
   - Hostile: 0 calls (data already stored in foe_troops/numbers).

5. **Each combat round**:
   - Per attack: 1 call for damage roll + 0..1 for Scythe.
   - Per AI move: 0..2 calls (ranged target picking).
   - Random obstacles at match start: 5×6 = 30 cells × 2 calls
     per cell (chance + type) = ~60 calls.

6. **Each end_week**:
   - 1 call (creature pick, except Peasants weeks).
   - Per dwelling matching creature: 0 calls (already populated).

7. **Visual flair**:
   - `rand() % 5` for random barracks troop in home castle visit
     (each entry to visit_home_castle).
   - `rand() % MAX_TROOPS` for random town background troop.
   - `KB_rand(0, 0xFFFFFF)` for blinking minimap pixel.

### 13.8 Determinism considerations

A clean-room reimplementation can choose:

- **Faithful**: use libc `rand()` with no auto-seed. Default
  behavior matches openkb (same sequence per run unless cheat-
  seeded).
- **Modern**: seed from time at start. Different game each run,
  but breaks save-determinism if RNG state isn't saved.
- **Reproducible**: include RNG state in save file. Players can
  replay with same RNG history.

The save file does NOT include RNG state, so loading a game
restores world state but RNG continues from the libc state at
load time (unrelated to creation-time state).

### 13.9 Treasure chest balance analysis

Cumulative thresholds with `KB_rand(1, 100)`:

```
Continent 0 (chance roll):
  1..60:      Gold/leadership choice  (60% chance)
  61..81:     +Commission             (21% chance)
  82..86:     +Spell power (always 1) (5% chance)
  87..86:     +Max spells             (0% chance — bug, see §13.3)
  87..100:    +New spell              (~14% chance)
  101+:       Empty                   (~0%, rounds up)

Continent 1: gold 66%, commission 20%, spellpower 6%, ... 
Continent 2: gold 76%, commission 10%, spellpower 7%, ...
Continent 3: gold 71%, commission 10%, spellpower 14%, ...
```

So gold is most common everywhere; spells are rarer. Saharia
(cont 3) has the most spell power chests despite being late
game.

## 14. Game creation — spawn_game step by step

The `spawn_game` function is the central game initializer. It
allocates and populates a fresh `KBgame` struct given a player's
choices. Fully deterministic given the input parameters and RNG
seed.

### 14.0 Function signature

`spawn_game(name, pclass, difficulty, land)` in play.c:373:

1. `malloc` `KBgame`, zero-fill.
2. Copy baseline map: `memcpy(game->map, land, 64*64*4)`.
3. **Hide scepter:**
   - `scepter_key = KB_rand(0x00, 0xFF)` — 1 byte random XOR key.
   - `scepter_continent = KB_rand(100, 400) / 100 - 1` — yields
     0..3 with bias toward 1,2 (100..199→0, 200..299→1, 300..399→2,
     400→3). That's an unusual uniform-ish roll.
   - `i = KB_rand(0, grass_on_continent(game, scepter_continent) - 1)`.
   - `bury_scepter` walks the continent row-by-row, finds the i'th
     grass tile (tile id 0x00 or 0x80), records its x and y.
     **Bug:** the function sets `scepter_x = i` and `scepter_y = i`
     (both). Correctly it should be `scepter_x = i` and `scepter_y
     = j`. As written, `scepter_x` holds the inner-loop x coord
     at the moment of the last iteration check, and `scepter_y`
     gets the same value. This is why the scepter search is
     effectively broken (see §34).
4. **Initialize character:**
   - `name` copied to `game->name` (upper-case'd later if needed).
   - `savefile = uppercase(name) + ".DAT"`.
   - `difficulty`, `class`, `days_left = days_per_difficulty[diff]`,
     `steps_left = 40`.
   - `gold = starting_gold[class]`.
   - Continent/x/y = home; y is home_y - 2 = 5 (above castle gate).
   - `continent_found[0] = 1`.
   - `mount = KBMOUNT_RIDE`; `boat = 0xFF`.
   - `last_x = x`, `last_y = y`.
   - `rank = 0`; `player_accept_rank()` applies rank 0's bonuses.
   - `leadership = base_leadership`; `time_stop = 0`.
   - `contract = 0xFF`; `last_contract = 4`; `max_contract = 5`.
   - `contract_cycle = {0,1,2,3,4}` — the first 5 villains.
   - `player_troops[0..1] = starting_army_troop[class]`,
     `player_numbers[0..1] = starting_army_numbers[class]`; slots
     2..4 set to troop `0xFF`, count 0.
   - `options[6] = {4, 1, 1, 1, 1, 1}`.
5. **Spells in towns** (`salt_spells`, play.c:335):
   - All `town_spell[26] = 0xFF`.
   - Hunterville (town 0x15) is hardcoded to sell Bridge (spell 7).
   - For every spell except Bridge (0x00..0x0D minus 0x07): pick
     `j = KB_rand(0, 25)` until an unassigned town is found;
     assign. This leaves ~13 towns with spells; remaining ones
     are filled with `KB_rand(0, 13)` (a random spell id).
6. **Alcove removal for Sorceress:**
   - `if (game->knows_magic)` (Sorceress has this set from class
     rank): remove Alcove tile: `map[0][19][11] = 0`.
7. **Salt continents** (`salt_continent`, play.c:185) for each of
   4 continents, with params `(2 artifacts, 1 navmap, 1 orb,
   2 telecaves, 10 dwellings, 5 friendlies)`:
   - Replace any `0xFF` start marker in corner with water (0x20).
   - Build a `barrel[N]` of size = number of 0x8B (chest) tiles.
   - Randomly assign `min_artifacts` slots as SALT_ARTIFACT;
     `min_navmaps` as SALT_NAVMAP; `min_orbs` as SALT_ORB;
     `min_telecaves` as SALT_TELECAVE; `min_dwellings` as
     SALT_DWELLING; `min_friendly` as SALT_FRIENDLY.
     (Note: telecaves/dwellings/friendlies use a looser rand
     and may land on already-assigned slots, in which case
     nothing happens — hence "min".)
   - Walk the map. For each 0x91 (pre-placed foe): register the
     slot into `foe_coords`, call `repopulate_foe` (3 troop slots
     using `roll_creature(continent, …)`).
   - For each 0x8B (chest): consume the next barrel slot:
     - SALT_ARTIFACT → set tile to `0x92 + num_artifacts` (0x92
       for first, 0x93 for second), then `num_artifacts++`.
     - SALT_ORB → record coords into `orb_coords[cont]`.
     - SALT_NAVMAP → record coords into `map_coords[cont]`.
     - SALT_TELECAVE → set tile to 0x8E (TELECAVE); record in
       `teleport_coords[cont][k]`, where k = 0..1.
     - SALT_DWELLING → set tile to `0x8C + dwelling_kind`, where
       dwelling_kind is returned by `populate_dwelling` based
       on the chosen troop's `troops[id].dwells` value. Record
       coords into `dwelling_coords[cont][n]`.
     - SALT_FRIENDLY → set tile back to 0x91 (foe), record
       coords into `foe_coords[cont][n]` (slots 0..4 are
       friendlies). `foe_troops` and `foe_numbers` for these
       are **not** populated at spawn — they get rolled fresh
       on each encounter (see §20 `accept_foe`).
8. **Initial castle ownership** = 0x7F (monsters) for all 26.
9. **`salt_villains` chain** (6 on cont 0, 4 on 1, 4 on 2, 3 on 3)
   — each villain randomly placed into an unowned castle on its
   continent.
10. **Repopulate castles:** every castle still owned by `0x7F`
    gets `repopulate_castle`-style monster garrison (5 slots each
    rolled via `roll_creature(castle_difficulty[castle], …)`).
11. `clear_fog(game)` reveals the 5×5 around start.
12. Return `game`.

After `spawn_game`, `create_game` in game.c calls `furnish_map` to
beautify transitions (not canonical; see §12.5).

### 14.1 Validation invariants after spawn

A correctly-spawned game has:

- `game->name[0..len]` ASCII; `game->name[len] = '\0'`.
- `game->savefile` = uppercase(name) + ".DAT".
- `game->class ∈ {0,1,2,3}`.
- `game->rank == 0` (always 0 at start).
- `game->mount == KBMOUNT_RIDE` (8).
- `game->difficulty == difficulty` (0..3 from arg).
- `game->days_left == days_per_difficulty[difficulty]`.
- `game->steps_left == 40`.
- `game->gold == starting_gold[class]`.
- `game->continent == 0` (HOME_CONTINENT).
- `game->x == 11`, `game->y == 5` (HOME_X, HOME_Y - 2).
- `game->last_x == 11`, `game->last_y == 5`.
- `game->boat == 0xFF`.
- `game->continent_found = {1, 0, 0, 0}`.
- `game->orb_found = {0, 0, 0, 0}`.
- `game->spells = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}`.
- `game->knows_magic = (class == 2 ? 1 : 0)`.
- `game->base_leadership = classes[class][0].leadership` (100/80/60/100).
- `game->leadership = base_leadership`.
- `game->commission = classes[class][0].commission` (1000/1000/3000/2000).
- `game->max_spells = classes[class][0].max_spell` (2/3/5/2).
- `game->spell_power = classes[class][0].spell_power` (1/1/2/0).
- `game->contract = 0xFF`.
- `game->last_contract = 4`.
- `game->max_contract = 5`.
- `game->contract_cycle = {0, 1, 2, 3, 4}`.
- For 0..2: `game->player_troops[i]`, `numbers[i]` set per class.
- For 2..4: `game->player_troops[i] = 0xFF`, `numbers[i] = 0`.
- All 26 castles owned by `0x7F` (monsters) initially, then
  villains placed.
- 17 villains placed across 4 continents (6+4+4+3).
- `game->options = {4, 1, 1, 1, 1, 1}`.
- All `game->fog[c][...] = 0` except 5×5 around start (cleared
  via `clear_fog`).
- All `game->map[...]` = `LAND.ORG` data + salting modifications.

### 14.2 Initial state mismatches (bugs / quirks)

- **`scepter_y == scepter_x`** (§34.1) — burying bug.
- **`game->y = HOME_Y - 2 = 5`** — player starts 2 tiles north
  of home castle. The HOME_Y comment says "starting position";
  but it's actually castle position. Player spawns at gate-
  adjacent.
- **`game->last_y` initialized to start y, not previous** —
  `last_x = x = 11; last_y = y = 5`. So foe_follow's first
  invocation uses (11, 5) as target until the player moves.
- **`game->options[5]` initialized to 1** — CGA palette index 1
  by default. Only meaningful if running CGA build.

### 14.3 `bury_scepter` walkthrough

```c
void bury_scepter(KBgame *game, int continent, word grass) {
    int i, j;
    word count = 0;
    for (j = 0; j < LEVEL_H; j++) {
        for (i = 0; i < LEVEL_W; i++) {
            if (game->map[continent][j][i] == 0x00
                || game->map[continent][j][i] == 0x80) {
                if (count == grass) {
                    game->scepter_x = i;
                    game->scepter_y = i;       // BUG: should be j
                    return;
                }
                count++;
            }
        }
    }
}
```

The argument `grass` is 0..(N-1) where N = total grass tiles on
the continent. The function walks tiles in row-major order,
counting grass-id-0 (and 0x80 alt-grass) tiles. When the count
matches, it sets the scepter coords.

The bug: `scepter_y = i` instead of `scepter_y = j`. So scepter
ends up at `(i, i)` instead of `(i, j)` — coords are always on
the diagonal x=y. The Search Area mechanic compares `(continent,
x, y)` against `(scepter_continent, scepter_x, scepter_y)`,
so finding the scepter requires standing exactly on the diagonal,
which is rare/impossible on most maps.

### 14.4 `salt_continent` step-by-step

`salt_continent(game, c, art, nav, orb, tele, dw, fri)` (play.c:185)
plants random encounters on chest tiles. Args:
- `c` — continent 0..3.
- `art` — minimum artifacts (always 2).
- `nav` — navmaps (always 1; though continent 3 doesn't get one).
- `orb` — orbs (always 1).
- `tele` — telecaves (always 2).
- `dw` — dwellings (always 10).
- `fri` — friendly foes (always 5).

Step 1: count chest tiles.
```c
barrel_len = chests_on_continent(game, c)
              // walks map, counts 0x8B
if barrel_len < min_len:                      // total assignments needed
    error: "Unable to salt continent"
    return
```

Step 2: build a barrel (assignment array).
```c
barrel = malloc(barrel_len)
memset(barrel, SALT_NONE, barrel_len)
for i = 0..art-1:    barrel[KB_rand(0, barrel_len-1)] = SALT_ARTIFACT
for i = 0..nav-1:    barrel[KB_rand(0, barrel_len-1)] = SALT_NAVMAP
for i = 0..orb-1:    barrel[KB_rand(0, barrel_len-1)] = SALT_ORB
for i = 0..tele-1:   barrel[KB_rand(0, barrel_len-1)] = SALT_TELECAVE
for i = 0..dw-1:     barrel[KB_rand(0, barrel_len-1)] = SALT_DWELLING
for i = 0..fri-1:    barrel[KB_rand(0, barrel_len-1)] = SALT_FRIENDLY
```

The art/nav/orb assignments use **rejection sampling** (loops
until empty slot found, ensuring exact count). The telecave/
dwelling/friendly use **best-effort** (no retry; might overlap
existing assignments, in which case fewer are placed).

Step 3: replace start markers with water tile.
```c
if game->map[c][0][0] == 0xFF:   // sentinel marker
    game->map[c][0][0] = 0x20    // deep water
```

Step 4: walk the map, processing 0x91 (foes) and 0x8B (chests).
For each:
- 0x91 (pre-placed foe): record coords, populate via
  `repopulate_foe(game, c, num_foe++)` (3 troop rolls).
- 0x8B (chest): consume next barrel slot:
  - SALT_ARTIFACT: tile becomes `0x92 + num_artifacts`,
    `num_artifacts++`.
  - SALT_ORB: record coords in `orb_coords[c]`, tile stays 0x8B.
  - SALT_NAVMAP: record coords in `map_coords[c]`, tile stays 0x8B.
  - SALT_TELECAVE: tile becomes 0x8E, record in
    `teleport_coords[c][num_telecaves++]`.
  - SALT_DWELLING: tile becomes `0x8C + populate_dwelling(...)`,
    record in `dwelling_coords[c][num_dwellings++]`.
  - SALT_FRIENDLY: tile stays 0x91, record in
    `foe_coords[c][num_friendly++]`.
  - SALT_NONE: do nothing (tile stays 0x8B).

Notice that orb and navmap chests stay as 0x8B (chest tile);
the player picks them up via `take_chest` which checks coords
against `orb_coords` and `map_coords`. Other tiles (artifact,
dwelling, telecave) change tile id at salt time.

### 14.5 RNG sequence at spawn

Order matters because seeded RNGs are deterministic:

1. Scepter key (1 byte):
   `KB_rand(0x00, 0xFF)`.
2. Scepter continent (biased):
   `KB_rand(100, 400) / 100 - 1`.
3. Scepter position (1 grass tile):
   `KB_rand(0, grass_count - 1)`.
4. Salting per continent (4 calls to `salt_continent`):
   - 6 barrel-fill rejection loops.
   - For each chest 0x8B: barrel index already chosen.
   - For each foe 0x91: 3 `roll_creature` calls (each: 1 dice
     for dwelling, 1 for chance, 0+ for fallback).
5. Salting villains (4 continents):
   - Each `salt_villains`: rejection sampling until N villains
     placed.
6. Repopulating monster castles:
   - For each non-villain castle (initially `0x7F`): 5
     `roll_creature` calls.
7. Spell distribution (`salt_spells`): 13 spell-id rolls + ~12
   filler rolls.
8. `clear_fog` (deterministic).

Total RNG calls at game start: ~500-1000.

### 14.6 LAND.ORG file format

Raw 16384 bytes (4 × 64 × 64). Each byte is a tile id 0x00..0xFF.

Layout:
- Bytes 0..4095: continent 0 (Continentia), row-major top-to-bottom.
- Bytes 4096..8191: continent 1 (Forestria).
- Bytes 8192..12287: continent 2 (Archipelia).
- Bytes 12288..16383: continent 3 (Saharia).

Within a continent: row 0 first (top), row 63 last (bottom).
Within a row: column 0 first (left), column 63 last (right).

Special tile ids in the source map:
- 0x00 — grass.
- 0x80 — alt grass (mostly identical to 0x00).
- 0x85 — castle (one tile per castle, 26 total).
- 0x8A — town (one tile per town, 26 total).
- 0x8B — chest (placeholder for salting; ~30-40 per continent).
- 0x90 — signpost.
- 0xFF — start marker (replaced with 0x20 water at salt time).

The map file is hand-authored. The 26+26 castle+town tiles are
placed at coordinates matching `castle_coords` and `town_coords`.
Chest tiles are placed roughly evenly across each continent for
salting variety. Dwellings/foes/artifacts are NOT pre-placed —
they're generated from chest tiles at game start.

### 14.7 Days per difficulty

### 14.7 Days per difficulty (renumbered)

```
days_per_difficulty[4] = { 900, 600, 400, 200 };
```

Easy=900 days, Normal=600, Hard=400, Impossible?=200.

### 14.8 Difficulty score modifier

```
difficulty_modifier[5] = { 0, 1, 2, 4, 8 };
```

Score formula (play.c:`player_score`):

```
score = captured*500 + artifacts*250 + castles*100 - killed*1
if difficulty == 0: score /= 2
else                score *= difficulty_modifier[difficulty]
if score < 0: score = 0
```

`difficulty_modifier[0]` is never used (the easy branch hard-codes
`/= 2`). Normal → ×1, Hard → ×2, Impossible? → ×4.

## 15. Player actions

### 15.1 Adventure movement

The adventure loop (`adventure_loop`, game.c:6281) reads input
from the `adventure_state` hotspot set (see §31). Movement
directions are encoded as 16 arrow-keys for 8 compass directions
each in 2 adjacent keymaps (e.g. Up = SDLK_UP and SDLK_8).

Offsets (`move_offset_x[9]`, `move_offset_y[9]`, game.c:4744-4745):

```
Direction   Index  (ox, oy)
  Down      0      ( 0,  -1)   // NOTE: y is mirrored on screen
  Left      1      (-1,  0)
  Right     2      ( 1,  0)
  Up        3      ( 0,  1)
  Down-Left 4      (-1, -1)
  Down-Right5      ( 1, -1)
  Up-Left   6      (-1,  1)
  Up-Right  7      ( 1,  1)
  (wait/5)  8      ( 0, -0)    // unused
```

`key` values 1..18 in `adventure_state` map to the 8 directions
(each with 2 key bindings, arrow + numpad). `key = (SDLK_DOWN or
SDLK_2) → index 0`, etc.

Single-step logic:

1. `cursor_x = game->x + ox`, `cursor_y = game->y + oy`, clamped to
   [0, 63].
2. `m = game->map[continent][cursor_y][cursor_x]` — tile at target.
3. `walk = 1`.
4. Flip hero sprite horizontally if moving left (ox == -1).
5. If the tile is `NOT IS_GRASS(m)` AND mount != FLY:
   - If `(boat_x, boat_y, boat) == (cursor_x, cursor_y, continent)`
     and player steps onto it: `mount = SAIL` (boarding).
   - Else if `IS_WATER(m)`:
     - If mount != SAIL, block: `walk = 0`.
   - Else if `IS_DESERT(m)`: `steps_left = 0` (desert ends the day).
   - Else if `IS_BRIDGE(m)`: pass through.
   - Else if `!IS_INTERACTIVE(m)`: block: `walk = 0`.
6. If walk:
   - Play walk sound (`TUNE_WALK`).
   - `last_x = x; last_y = y; x = cursor_x; y = cursor_y`.
   - `clear_fog(game)`.
   - If `m == TILE_TELECAVE`: call `visit_telecave(game, 1)`;
     teleports to the paired cave if this is a telecave, else no-op.
   - If `mount == SAIL`: tuck boat with player:
     `boat_x = x; boat_y = y`.
7. Else: play bump sound (`TUNE_BUMP`).
8. After moving, if the tile is interactive and mount != FLY, run
   the interaction switch:
   - 0x85 castle → `visit_castle(game)`; walk=0 afterwards.
   - 0x8A town  → `visit_town(game)`; walk=0.
   - 0x8B chest → `take_chest(game)`.
   - 0x8F dwelling_4 (dungeon) → `visit_dwelling(game, 3)`.
   - 0x8E dwelling_3 (hill) → `visit_telecave(game, 0)` first;
     if not a cave-teleport, `visit_dwelling(game, 2)`.
   - 0x8D, 0x8C dwelling_2/_1 → `visit_dwelling(game, m - 0x8C)`.
   - 0x90 signpost → `read_signpost(game)`.
   - 0x91 foe → `walk = !attack_foe(game)`.
   - 0x92, 0x93 artifact → `take_artifact(game, m - 0x92)`.
   - default → log "Unknown interactive tile".
9. If interaction made walk=0 (e.g. entering castle without siege):
   swap `(x, y) <-> (last_x, last_y)`. This lets the player "cancel"
   the move by effectively stepping back.
10. After a successful move (walk stays 1):
    - `foes_follow(game)` — all foes within 2-square radius of
      `(last_x, last_y)` step toward the player.
    - If the new tile is now a FOE (one of the followers stepped
      onto the player's tile): `continue` (reruns the move loop;
      player will trigger `attack_foe` on next iteration).
    - If `time_stop > 0`: `time_stop--`, no day cost.
    - Else: `steps_left--`.
    - Hitting shore (from water, not bridge): if mount == SAIL,
      flip to RIDE and drop boat at `(last_x, last_y)`.

End-of-step checks (each iteration):

- If `steps_left <= 0`: `end_day(game)`; if that returned 1
  (week ended), `weekend = 1`.
- If `days_left == 0`: `lose_game(game)`; `done = 1`.
- If `weekend > 0`: process end-of-week (one call to
  `end_of_week(game, weekend)` per week-overlap count, decrementing).

### 15.2 Fly / Land

Pressing F (`KBACT_FLY`) in adventure, while `mount == KBMOUNT_RIDE`:

- Call `player_can_fly(game)` — returns 0 if any troop slot is
  non-empty but either lacks `ABIL_FLY` or has `skill_level < 2`.
  Stop troops that block flight (logged; no banner in game).
- If OK, set `mount = KBMOUNT_FLY`. Flight does not cost steps
  directly in the adventure loop (the step cost on move is the
  same).

Pressing L (`KBACT_LAND`) while flying: if the current tile is
grass (0x00), set `mount = KBMOUNT_RIDE`; else ignored (sound
placeholder "Play beep").

### 15.3 Sail

Pressing N (`KBACT_NEW_CONTINENT`) while `mount == KBMOUNT_SAIL`
opens the "Go to which continent?" prompt (`navigate_continent`,
game.c:1912). Choose 1..4; if `continent_found[i]`, call
`sail_to(game, i)`:

- `continent = i`.
- `x = continent_entry[i][0]; y = continent_entry[i][1]`.
- `last_x = x; last_y = y`.
- `boat = i; boat_x = x; boat_y = y`.

Then `spend_week(game)` — sailing always consumes the rest of the
current week.

### 15.4 Army management

- **View Army (A):** `view_army(game)`, full-screen 5-slot display
  with per-slot stats + morale/control. Esc exits.
- **View Character (V):** `view_character(game)`, full-screen
  portrait + stats + artifact/maps inventory. Esc exits.
- **Use Magic (U):** `choose_spell(game, NULL)` with adventure mode.
- **Dismiss (D):** `dismiss_army(game)`. Prompts to choose a
  non-empty slot (A..E). If dismissing last army, a confirmation
  "If you Dismiss your last army, you will be sent back to the
  King in disgrace. Dismiss last army (y/n)?" is shown. After
  dismissal, if no troops remain → `temp_death` + `draw_defeat`.
  - `dismiss_troop(game, slot)`: shift slots [slot+1..4] down by
    1; slot 4 set to troop `0xFF`, count 0.

### 15.5 View screens

- **View Contract (I):** `view_contract(game)`. If contract is
  0xFF: shows "You have no Contract!" with a gray face from
  sidebar tile. Else: loads `STR_VDESC[contract]` (18-line
  description from KB.EXE), fills in continent name (always
  current continent) and castle name (only if a `KBCASTLE_KNOWN`
  castle holds this villain; else "Unknown"). Left face is the
  villain's animated portrait; right text is the description.
- **View Map (M):** `view_minimap(game, 0)`. Centered 20×19
  char-width border with a 64×64 scaled pixelmap. Per-tile color
  from `COL_MINIMAP` (water=blue, grass=green, desert=yellow,
  rock=brown, tree=dark-green, castle=white, object=red;
  fog=black). If orb_found[current]: pressing Space toggles the
  full-continent view on/off (`orb` boolean); else only current
  fog is visible. Blinking "you are here" pixel uses
  `KB_rand(0, 0xFFFFFF)`. Top banner reads "Press 'ESC' to exit"
  or, if orb found, "'ESC' to exit / 'SPC' whole map" and
  "'ESC' to exit / 'SPC' your map".
- **View Puzzle (P):** `view_puzzle(game)`. Shows the 5×5 puzzle
  map overlaid on the map viewport. Opens piece-by-piece as
  animated villain portraits / static artifact tiles. For each
  villain-caught or artifact-found, that cell is replaced with
  the underlying map (hiding any important objects — objects are
  blanked to 0). See §9.2 for puzzle_map layout.
- **Search Area (S):** `ask_search(game, &weekend)`. 10-day cost
  ("It will take 10 days to do a search of this area. Search
  (y/n)?"). If yes:
  - Check if `(scepter_continent, scepter_x, scepter_y) ==
    (continent, x, y)`; if equal → `win_game(game)`, return 0.
  - Else: "Your search of this area has revealed nothing." then
    `spend_days(game, 10)`.

### 15.6 End week / save-quit / fast-quit

- **Wait End Week (W):** `spend_week(game)` — spends remaining
  days to end of current week; then `weekend = 1`.
- **Save + Quit (Q):** `ask_quit_game(game)` →
  - `save_game(game)` — writes `<save_dir>/<savefile>2` (the "2"
    suffix is a debug leftover).
  - Show "Your game has been saved. / Press Control-Q to Quit
    or / any other key to continue." with the quit_question
    gamestate (Ctrl+Q quits; any other key continues).
- **Fast Quit (Ctrl+Q):** `ask_fast_quit(game)` — "Quit to DOS
  without saving (y/n)" in the top box.

### 15.7 Debug cheat menu

Ctrl+Q (but only as `KBACT_CHEAT` mapping, actually just Q with
modifier — see §31 for exact mapping) opens
`debug_cheat_menu(game, war)`. Bottom box "Debug command (A-Z) ?":

| key | effect |
|-----|--------|
| A | Toggle `auto_battle` variable (AI plays player side) |
| X | Preset "Flight team" army (Sprites, Archmages, Vampires, Demons, Dragons, 1 each) |
| R | Seed RNG — prompts for 4-digit number, calls `srand` |
| T | Add troop — prompts for A..Z, then MAX. 0 means default `troop_numbers[id][continent]` |
| W | In combat, wipe AI side (count=0 for all); in adventure, `win_game` |
| L | `lose_game` |
| F | `mount = KBMOUNT_FLY` |
| S | `siege_weapons = 1` |
| U | `spells[i]++` for every i (+1 of every spell) |
| M | `if knows_magic: spell_power++`; `knows_magic = 1` |
| V | `leadership += 100; base_leadership += 100` |
| G | `gold += 5000` |
| N | `continent_found[continent+1] = 1` |
| (other) | "No such command" banner |

### 15.8 Mount state transitions

`mount` is an enum byte with 3 values: 0 (SAIL), 4 (FLY), 8 (RIDE).
Transitions and their triggers:

```
                ┌──[ stepping onto boat tile (boat_x,boat_y,boat) ]──┐
                │                                                     ▼
              RIDE ────────[ press F if can_fly ]──────► FLY
               ▲                                            │
               │                                            │
               │     [ press L on grass tile ]              │
               └────────────────────────────────────────────┘
                │
                │     [ stepping onto land tile from water ]
                │           (drops boat at last_x,last_y)
              SAIL ◄───────────────────────────────────────┘
                ▲
                │     [ rent boat in town (option B) ]
                │           (sets boat_x,boat_y,boat)
              RIDE
```

Specific gates:

- **RIDE → SAIL**: detected in adventure_loop at game.c:6455:
  ```c
  if (game->boat_x == cursor_x
      && game->boat_y == cursor_y
      && game->boat == game->continent):
      game->mount = KBMOUNT_SAIL
  ```

- **SAIL → RIDE**: detected at game.c:6562:
  ```c
  /* Hitting shore */
  if (!IS_WATER(m) && !IS_BRIDGE(m)):
      if (game->mount == KBMOUNT_SAIL):
          game->mount = KBMOUNT_RIDE
          game->boat_x = game->last_x
          game->boat_y = game->last_y
  ```

- **RIDE → FLY**: pressing F (KBACT_FLY), only if mount==RIDE:
  ```c
  if (player_can_fly(game)):
      game->mount = KBMOUNT_FLY
  ```

- **FLY → RIDE**: pressing L (KBACT_LAND), only if mount==FLY:
  ```c
  if (game->map[c][y][x] == 0):
      game->mount = KBMOUNT_RIDE
  // else: silent fail (intended sound beep, not implemented)
  ```

- **SAIL → FLY**: NOT allowed (must vacate boat first).
- **FLY → SAIL**: NOT allowed (must land first).

### 15.9 Movement cancellation

If interaction at the new tile sets `walk = 0` (e.g. entering a
castle, viewing a town, fleeing combat), the move is reversed:

```c
if (!walk):
    int tmp_x = game->x, tmp_y = game->y
    game->x = game->last_x
    game->y = game->last_y
    game->last_x = tmp_x
    game->last_y = tmp_y
    walk = 1
```

So the player ends up at their previous position. `last_x, last_y`
swap with current — meaning a subsequent foe-follow uses the
post-cancel position.

This is the common pattern for: castles, towns, signposts, foes
(when not attacking), telecaves (when not teleporting).

### 15.10 Tile passability rules

For player movement (mount-aware):

- `mount == FLY`: any tile passable. No interaction switch fires
  except for foes (which still attack).
- `mount == SAIL`: only water tiles passable. Land tiles trigger
  shore-debark.
- `mount == RIDE`:
  - Grass (0x00, 0x80): always passable.
  - Bridge (0x08, 0x09): passable.
  - Water (0x14..0x20): blocked unless boarding boat.
  - Desert (0x2E..0x3A): passable but ends day (`steps_left=0`).
  - Tree (0x21..0x2D): blocked.
  - Rock (0x3B..0x47): blocked.
  - Interactive (0x80+): passable for the move; interaction
    fires after.

For foes (mount-agnostic, see §19.1):
- Only grass-id-0 (literal 0) is passable.
- Foes' own tile (0x91) is also "passable" (they can stay put).

### 15.11 Step-cost model

A "step" is one successful tile move. Each step:
- Decrements `steps_left` by 1, OR `time_stop` by 1 (if > 0).
- When `steps_left == 0`: end_day fires, resets to 40.
- When end_day's modulo check fires: end_week processing
  triggers.
- If `days_left == 0` after end_day: lose_game.

**Desert-special**: stepping onto desert sets `steps_left = 0`
on the next move (game.c:6467). This means desert tiles end the
day immediately — useful for Saharia traversal where every
desert step costs a full day.

**Time Stop**: spell sets `time_stop = spell_power * 10` steps.
While `time_stop > 0`, each move decrements it instead of
`steps_left`. Effectively grants free movement for a few turns.
Cleared by end_day (so a full day always ends time_stop).

### 15.12 Sailing rules

A boat is **rented per-week**. Cost is paid weekly via end_week:
`credit += boat_cost(game)` where `boat_cost = 100 if Anchor,
else 500`. If gold runs out, boat is implicitly cancelled
(though the code spends gold to 0 first, then continues — so
the boat is "rented" but you have no more gold).

Sailing to a new continent (Navigate, key N):
- Requires `continent_found[target]` to be true.
- Calls `sail_to(game, target)`:
  - Sets continent, x, y to `continent_entry[target]`.
  - Sets last_x/y same.
  - Sets boat_x/y same (boat travels with player).
- Calls `spend_week(game)` — entire current week is consumed.

So sailing always costs the rest of the week, even if you sail
on day 1.

### 15.13 Flight rules

`player_can_fly(game)` (play.c:638):
```c
for i = 0..4:
    if numbers[i] == 0: break          // empty slot, end of army
    if !(troops[id].abilities & ABIL_FLY):
        return 0                        // non-flyer present
    if troops[id].skill_level < 2:
        return 0                        // unskilled flyer (Sprites)
return 1
```

So flight requires every non-empty troop slot to have FLY
ability AND skill ≥ 2. With Sprites (SL=1), even pure-flyer
armies fail.

Flying troops with SL ≥ 2: Ghosts (4), Archmages (5), Vampires
(5), Demons (6), Dragons (6).

A "flight team" of just these 5 enables overworld flight. Cheat
'X' presets exactly these.

### 15.14 Cheat menu (review)

`debug_cheat_menu(game, war)` (game.c:4911) — comprehensive
in-game debugger. Activated by F10. The hotspot bypasses normal
input flow so it works even mid-combat or mid-menu.

## 16. Day/week cycle

Time progression is the game's pacing system. Days tick down on
each step; weeks roll over every 5 days; the player loses on
day 0. Most economic effects (army upkeep, dwelling refresh,
commission) happen at end-of-week.

### 16.0 State variables

```
days_left   word    decrements 1 per end_day; lose at 0
steps_left  byte    decrements 1 per move; resets to 40 at end_day
time_stop   word    when > 0, decrements instead of steps_left
```

`base_leadership` and `leadership` interact: leadership resets
to base at end_week. `commission` accumulates as gold at end_week.

### 16.1 `end_day(game)` (play.c:504)

```c
days_left -= 1
steps_left = 40
time_stop = 0
if ((days_left % WEEK_DAYS) == 0): return 1   // week ended
return 0
```

(WEEK_DAYS=5, so a week ends every 5th day.)

### 16.2 `passed_days(game)`

```
passed = days_per_difficulty[difficulty] - days_left
```

### 16.3 `week_id(game)` — `passed / 5`

### 16.4 `spend_days(game, days)`

Clamps days to `days_left`; loops `end_day` that many times;
returns cumulative weeks_passed count.

### 16.5 `spend_week(game)`

Days needed to end current week = `5 - (passed % 5)`; calls
`spend_days(game, end_week_days)`.

### 16.6 `end_week(game, *spent)` (play.c:973)

Full end-of-week processing:

1. `time_stop = 0`.
2. `leadership = base_leadership` (reset to base, undoing Raise
   Control gain).
3. Pick astrology creature:
   - If `week_id(game) % 4 == 0`: creature = 0 (Peasants).
   - Else: `creature = KB_rand(1, MAX_TROOPS - 1)` = 1..24.
4. **Budget:**
   - `credit = 0`.
   - `gold += commission` (add weekly stipend).
   - If player has boat: `credit += boat_cost(game)` (100 or 500
     depending on Anchor).
   - For each player troop slot i with `numbers[i] > 0`:
     `credit += numbers[i] * (troops[troop[i]].recruit_cost / 10)`
     — one-tenth recruit cost per unit per week.
   - `spend_gold(game, credit)` — gold -= credit, but never
     below 0.
5. **Ghost → Peasant conversion** (if creature == 0):
   For each player slot, if `troops[slot].abilities & ABIL_ABSORB`:
   `player_troops[slot] = 0` (Peasants). Counts are unchanged.
6. **Repopulate empty player castles:** for each castle owned by
   player where `castle_numbers[0] == 0`, call `repopulate_castle`
   (5 fresh random monster troops at difficulty for that castle).
7. **Unit growth:** for every dwelling whose `dwelling_troop ==
   creature`: `population = troops[creature].max_population` (full
   reset). For every foe whose foe_troop == creature:
   `foe_numbers += troops[creature].growth`. For every castle
   *not* player-owned whose troop == creature:
   `castle_numbers += troops[creature].growth`.
8. Return `creature` id.

### 16.7 End-of-week UI (`end_of_week`, game.c:3673)

1. Compute `week_num` from `days_passed + days_passed*num`.
2. Run `end_week(game, NULL)` to get `creature`.
3. **Astrology banner** (`KB_BottomFrame`):

   ```
   Week #<n>
   
   Astrologers proclaim:
   Week of the <TroopName>
   
   All <TroopName> dwellings are
   repopulated.         (space)
   ```

   Wait for Space (`minimap_toggle` hotspot matches Space).
4. **Budget banner** (`KB_BottomFrame`):

   ```
   Week #<n>             Budget
   
   On Hand <on_hand>         <Troop 0>  <cost 0>
   Payment <commission>      <Troop 1>  <cost 1>
   Boat    <boat_cost>       <Troop 2>  <cost 2>
   Army    <army_cost>       <Troop 3>  <cost 3>
   Balance <game->gold>      <Troop 4>  <cost 4>
   ```

   Per-slot cost = `numbers[i] * recruit_cost / 10`. Wait for any
   key (`KB_Pause`).

### 16.8 Weekend handling in adventure_loop

When end_day returns 1 (week ended), `weekend = 1`. The adventure
loop runs end-of-week processing:

```c
while (weekend-- > 0):
    end_of_week(game, weekend)
```

The `weekend` counter handles the rare case of multiple weeks
elapsing in one move (e.g. via `spend_week` from sail or wait).
Each iteration shows astrology + budget banners.

`end_of_week(game, num)` (game.c:3673):
1. Computes `days_passed = num * WEEK_DAYS = num * 5`.
2. `game->days_left += days_passed` (temporarily restore so the
   week_id is correct).
3. Calls `end_week(game, NULL)` to do actual processing.
4. `game->days_left -= days_passed` (restore correct value).
5. Computes `week_num = week_id(game) - num`.
6. Shows astrology banner.
7. Shows budget banner.

So if 3 weeks just elapsed, `end_of_week` is called with
num=2, then 1, then 0; each shows that week's astrology.

### 16.9 Astrology week selection

`end_week` picks a creature for "Week of the X":

```c
if (week_id(game) % 4 == 0):
    creature = 0                    // Peasants on every 4th week
else:
    creature = KB_rand(1, MAX_TROOPS - 1)   // 1..24
```

So weeks 0, 4, 8, 12, 16, 20, ... are Peasants weeks.

### 16.10 Ghost-to-Peasant conversion

On Peasants weeks, `end_week` does:

```c
if creature == 0:
    for i = 0..4:
        if troops[player_troops[i]].abilities & ABIL_ABSORB:
            player_troops[i] = creature       // = 0 (Peasants)
```

This converts every Ghost stack in the player's army to Peasants
(keeping the count). The conversion is one-way.

### 16.11 Player castle repopulation

```c
for j = 0..MAX_CASTLES-1:
    if castle_owner[j] == 0xFF (player)
       and castle_numbers[j][0] == 0:        // empty stack 0
        repopulate_castle(game, j)            // 5 random rolls
```

Note: the check is `numbers[0] == 0`, not "all 5 are 0". So a
castle with stack 0 empty but stacks 1..4 populated is still
considered empty and gets repopulated, overwriting existing
troops.

### 16.12 Unit growth

```c
for c = 0..3:
    for d = 0..MAX_DWELLINGS-1:
        if dwelling_troop[c][d] == creature:
            dwelling_population[c][d] = troops[creature].max_population
                                          // FULL refill

    for f = 0..MAX_FOES-1:
        for i = 0..2:
            if foe_troops[c][f][i] == creature:
                foe_numbers[c][f][i] += troops[creature].growth
                                          // INCREMENT

for j = 0..MAX_CASTLES-1:
    if castle_owner[j] != 0xFF (not player):
        for i = 0..4:
            if castle_troops[j][i] == creature:
                castle_numbers[j][i] += troops[creature].growth
```

Dwellings refill to max_population; foes/castles increment by
growth.

### 16.13 Budget computation

```
credit = 0
gold += commission                    // weekly stipend (added)
if has_boat: credit += boat_cost      // 100 or 500
for i = 0..4:                          // army upkeep
    if numbers[i] == 0: break
    credit += numbers[i] * (recruit_cost / 10)
gold -= credit (clamped to 0)
```

The 1/10 upkeep formula means the army costs 10% of its gold
value per week. With 5 Dragons (5000 gp each), that's 2500
gp/week.

If gold drops to 0, the remaining upkeep is silently skipped
(no army loss); the player keeps the army for free until they
spend more gold.

### 16.14 Mid-week save

The save format includes `days_left` and `steps_left` separately.
End-of-week fires from `days_left % 5 == 0`, computed at runtime,
so loading mid-week works seamlessly.

## 17. Combat engine

Combat is a turn-based 6×5 grid battle. Two sides (player +
AI), each with up to 5 stacks. Each turn, units in initiative
order take one action: move, attack (melee or ranged), wait,
pass, fly, or cast a spell. Combat ends when one side's stacks
all die, or the player gives up.

The combat engine is the largest single piece of openkb. The
primary functions live in `play.c` (data manipulation,
deterministic) and `game.c` (UI, animation, AI).

### 17.0 Section overview

§17.1 — battlefield data structures
§17.2 — battle setup (prepare_units_*)
§17.3 — turn structure (reset_turn, next_unit, next_turn)
§17.4 — control / friendliness flags
§17.5 — damage formula (deal_damage in detail)
§17.6 — movement, flight, attack driver
§17.7 — ranged shot
§17.8 — compact_units (post-death cleanup)
§17.9 — combat loop (combat_loop, the main UI)
§17.10 — victory / defeat (run_combat, draw_victory, draw_defeat)
§17.11 — distance and offset helpers (isqrt32, calc_distance)
§17.12 — unit_closest_offset (combat AI movement)

### 17.1 Battlefield

`KBcombat` struct (bounty.h:281). Fields:

```
KBunit units[2][5];                     // [side][id]
byte omap[CLEVEL_H+1][CLEVEL_W+1];      // obstacles (+1 padding)
byte umap[CLEVEL_H+1][CLEVEL_W+1];      // unit occupancy (1-based UIDs)
word spoils[2];                         // accumulated per side
byte powers[2];                         // artifact power bits per side
KBgame *heroes[2];                      // hero pointer; foe side = NULL
int your_turn;                          // used only by net combat
byte turn;                              // counter
byte phase;                             // 0 or 1
byte spells;                            // spells this round (≤1)
byte side;                              // 0 = player, 1 = AI
byte unit_id;                           // current selected unit
```

Grid is 6 wide × 5 tall. The `+1` padding lets the combat code
write to `[CLEVEL_H][...]` safely.

`KBunit` fields:

```
byte troop_id
word count             // current stack size
word turn_count        // snapshot at start of turn (for retaliation)
word max_count         // snapshot at combat start (for Leech cap)
byte dead              // post-compact flag; 1 if stack died this attack
byte frame             // animation frame 0..3
word injury            // residual damage (< 1 unit HP)
byte acted             // has unit taken its action this turn?
byte retaliated        // has unit retaliated this round?
byte moves             // movement points remaining
byte shots             // ranged shots remaining
byte flights           // flying moves remaining (flyers only, 2)
byte frozen            // frozen flag
byte out_of_control    // no-leadership flag (set per-unit)
byte y, x              // grid position
```

### 17.2 Battle setup

`prepare_units_player(war, side, game)` (play.c:1061):

```
For i = 0..4:
  units[side][i].troop_id = game->player_troops[i]
  units[side][i].count    = game->player_numbers[i]
  units[side][i].max_count= count
  units[side][i].out_of_control = !unit_under_control(war, side, i)
  units[side][i].y = i            // stacks stack in rows 0..4
  units[side][i].x = side * (CLEVEL_W - 1)   // side 0: col 0; side 1: col 5
  war->spoils[side] += troops[id].spoils_factor * 5 * count
war->heroes[side] = game
war->powers[side] = 0
For each artifact i found: war->powers[side] |= artifact_powers[i]
```

`prepare_units_foe` and `prepare_units_castle` are similar but
limit to `max_troops = min(MAX_UNITS, 3) = 3` for foes, or use 5
for castles. `heroes[side] = NULL` for both; `powers[side] = 0`.

`reset_match(war, castle)` (play.c:1200):

- If `castle`: place units in fixed castle layout. `castle_umap[5][6]`
  and `castle_omap[5][6]` define positions (see below).
- Else: generate random obstacles — each tile in cols [1, W-3] has
  ~1/10 chance of obstacle type 1..3 (openkb comment flags this as
  different from DOS, and with a "fair chance of flooding whole
  level").
- For each unit with count > 0: set `shots = troops[id].ranged_ammo`,
  `injury = 0`, `frozen = 0`; write `war->umap[y][x] = side*5 + id+1`.
- Wipe and lay obstacles.
- Call `reset_turn(war)`.

Castle layouts (play.c:1184):

```
castle_umap[5][6] = {   // Player UIDs 1..5, AI UIDs 6..10
    { 0, 8, 6, 7, 9, 0 },   // row 0 (back)
    { 0, 0,10, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0 },
    { 0, 5, 3, 4, 0, 0 },
    { 0, 0, 1, 2, 0, 0 },   // row 4 (front)
};

castle_omap[5][6] = {
    { 8, 0, 0, 0, 0, 9 },
    { 8, 0, 0, 0, 0, 9 },
    { 8, 0, 0, 0, 0, 9 },
    { 8, 0, 0, 0, 0, 9 },
    { 5, 7, 0, 0,10, 6 },
};
```

### 17.3 Turn structure

`reset_turn(war)` (play.c:1152):

- For each unit: `turn_count = count`, `retaliated = 0`, `acted = 0`,
  `moves = troops[id].move_rate`, `flights = 0`.
- If `abilities & ABIL_FLY`: `flights = 2`.
- If `side == 1` (end of AI turn, about to start player's):
  - Unfreeze: `frozen = 0`.
  - Regen: if `abilities & ABIL_REGEN`, `injury = 0`.
- `phase = 0; spells = 0`.

`next_unit(war)` (play.c:1283): find next unit on current side
with `count > 0` and `acted == 0`. First scans `[unit_id+1..4]`,
then wraps `[0..4]` with `phase++`. Sets `out_of_control` based
on leadership at this moment. Returns index or -1.

`next_turn(war)` (play.c:1266): switches `side`, calls `reset_turn`,
and finds the first living unit on the new side.

### 17.4 Key flags

- `war->heroes[side] != NULL` — that side is a player (has
  leadership, morale, artifact powers). The AI side has NULL.
- `unit_under_control(war, side, id)` — returns 1 if unit is
  friendly (player hero present and `army_leadership(hero, troop)
  > 0`). Units beyond leadership cap behave as out-of-control and
  are treated as enemy-of-own-side for attack purposes (see
  `units_are_friendly`).
- `units_are_friendly(war, sA, iA, sB, iB)` — false if either is
  out-of-control; else equal sides only.

### 17.5 Damage formula

`deal_damage(war, a_side, a_id, t_side, t_id, is_ranged,
is_external, external_damage, retaliation)` (play.c:1407) —
**this is the heart of the combat engine**. Reproduced here
essentially verbatim, with extended commentary.

#### 17.5.0 Function signature

```c
int deal_damage(
    KBcombat *war,
    int a_side, int a_id,         // attacker
    int t_side, int t_id,         // target
    int is_ranged,                // 1 if ranged attack
    int is_external,              // 1 if magic damage (no attacker unit)
    int external_damage,          // damage value if is_external
    int retaliation               // 1 if this is a retaliation
);
```

Returns: number of full kills (creatures killed). -1 if attack
was cancelled (e.g. MAGIC vs IMMUNE).

#### 17.5.1 Step-by-step

1. `demon_kills = 0`.
2. If `is_external` (spell):
   - `u = NULL`; `final_damage = external_damage`.
3. Else (unit-vs-unit):
   - If attacker has `ABIL_SCYTHE`:
     - `if (KB_rand(1,100) > 89)` — 10% chance:
       `demon_kills = ceil(t.count / 2)` (computed as
       `t.count / 2 + (t.count % 2 ? 1 : 0)`).
   - If `is_ranged`:
     - `retaliation = 1` (ranged attacks don't trigger retaliation).
     - `u.shots--`.
     - If `attacker.abilities & ABIL_MAGIC`:
       - If target has `ABIL_IMMUNE` → return -1 (attack cancelled).
       - `dmg = troops[u.id].ranged_min` (fixed; 10 for Druids, 25
         for Archmages).
     - Else: `dmg = KB_rand(ranged_min, ranged_max)`.
   - Else (melee): `dmg = KB_rand(melee_min, melee_max)`.
   - `total = dmg * u.turn_count`.
   - `skill_diff = u.troop.skill_level + 5 - t.troop.skill_level`.
   - `final_damage = (total * skill_diff) / 10`.
   - If hero on attacker side AND attacker is under control:
     - `morale = troop_morale(hero, a_id)`
     - LOW (1): `final_damage /= 2`.
     - HIGH (2): `final_damage += final_damage / 2` (×1.5).
     - NORMAL (0): unchanged.
   - If `powers[a_side] & POWER_INCREASED_DAMAGE`:
     `final_damage += final_damage / 2` (×1.5).
   - If `powers[t_side] & POWER_QUARTER_PROTECTION`:
     `final_damage = (final_damage / 4) * 3`
     (integer-first floor: heavy damage shrinks to 75%, small
     damage can vanish to 0).
4. `final_damage += t.injury` — prior damage adds into this blow.
5. `final_damage += troops[t.id].hp * demon_kills` — demon stack
   halving bonus damage.
6. `kills = final_damage / troops[t.id].hp`.
7. `injury_after = final_damage % troops[t.id].hp`.
8. `t.injury = injury_after`.
9. If `kills < t.count`: `t.count -= kills`.
    Else: `t.dead = 1; t.count = 0; kills = t.turn_count;
    final_damage = kills * t.hp` (recomputed so the absorb/leech
    gets the true kill count).
10. If target has a hero: `hero->followers_killed += kills`.
11. If not is_external:
    - If attacker has `ABIL_ABSORB`: `u.count += kills`.
    - If attacker has `ABIL_LEECH`:
      `u.count += final_damage / u.hp` (clamped to `max_count`,
      `injury = 0` on clamp).
    - If `!retaliation && !t.retaliated`:
      `t.retaliated = 1`;
      `deal_damage(war, t_side, t_id, a_side, a_id, 0, 0, 0, 1)`.
12. Return `kills`.

#### 17.5.2 Damage formula in detail

The core damage formula is:
```
final_damage = (dmg * attacker_count * skill_diff) / 10
```

Where:
- `dmg` = `KB_rand(min, max)` (or fixed for MAGIC).
- `attacker_count` = `u->turn_count` (snapshot at start of turn,
  important to avoid retaliation underflow).
- `skill_diff` = `attacker.skill_level + 5 - target.skill_level`.

The `+5` baseline ensures a same-skill matchup gets `skill_diff
= 5`, i.e. `total / 2` damage. A skill 6 attacker vs skill 1
target gets `skill_diff = 10`, full damage. A skill 1 attacker
vs skill 6 target gets `skill_diff = 0` — zero damage!

This means low-skill attackers are completely useless against
high-skill targets. Sprites (SL 1) cannot damage Knights (SL 5)
with `(1 + 5 - 5) / 10 = 1/10` of base — actually, wait,
re-checking: `skill_diff = 1 + 5 - 5 = 1`, so `final_damage =
total * 1 / 10` = 10% of base. So they DO some damage, but
only 10%.

Note that morale and artifact powers further modify
`final_damage`, applied multiplicatively in source order:
1. `final_damage` from the formula above.
2. Morale: `/= 2` (LOW), `+= /2` (HIGH).
3. INCREASED_DAMAGE: `+= /2`.
4. QUARTER_PROTECTION: `= /4 * 3`.

So the maximum boost: HIGH morale + INCREASED_DAMAGE = ×1.5 × 1.5
= ×2.25 (multiplicative on raw formula).

Worst reduction: LOW morale + QUARTER_PROTECTION = ×0.5 × 0.75
= ×0.375.

#### 17.5.3 Why `turn_count`?

`turn_count` is set at the start of the turn (in
`reset_turn`):
```c
units[s][i].turn_count = units[s][i].count
```

But during the turn, `count` may decrease (the unit takes
damage from retaliation, magic, etc.). When this unit then
attacks, the formula uses `turn_count` (not `count`) so the
attack is computed as if the full stack were intact.

Without this, an attacker that took retaliation would do less
damage on its next attack — but the convention is that "the
stack swings together at the start of the turn", so all
creatures in the stack contribute to damage even if some died
mid-turn.

This is also why `t.turn_count = kills * t->hp` is
recomputed when target dies — to give the attacker correct
absorb/leech values.

#### 17.5.4 Demon Scythe special

10% chance per attack:
```c
if (KB_rand(1, 100) > 89):
    demon_kills = ceil(t.count / 2)
final_damage += demon_kills * t.hp
```

So demons have a 10% chance to instantly kill half the target
stack (rounded up). This is added to the regular damage. A
demon attack on 100 Peasants might do 5×7=35 damage (kills 35
Peasants), plus 50% (50 demon_kills) = 35 + 50 = 85 kills
total.

Note: `demon_kills` is included in `final_damage` for the
purposes of compute, but the death calculation uses `kills =
final_damage / hp` so the demon kills are factored back in via
the `+= demon_kills * hp` adjustment.

#### 17.5.5 Magic ranged attacks

Druids and Archmages have ABIL_MAGIC. Their ranged attack:
```c
if (attacker.abilities & ABIL_MAGIC):
    if (target.abilities & ABIL_IMMUNE):
        return -1            // cancel attack
    dmg = troops[u].ranged_min   // fixed (10 for Druids, 25 for Archmages)
```

So:
- Magic ranged is FIXED damage, not rolled.
- IMMUNE target cancels attack entirely (no shots used,
  no retaliation).

#### 17.5.6 Retaliation

After the primary attack:
```c
if (!retaliation && !is_external):
    if (!t->retaliated):
        t->retaliated = 1
        deal_damage(war, t_side, t_id, a_side, a_id, 0, 0, 0, 1)
                   // RECURSIVE: target retaliates
```

Retaliation is:
- Always melee (is_ranged=0).
- Marked by `retaliation=1` flag (no further retaliation chain).
- Once per round per unit (`t->retaliated = 1` after first).
- Skipped for ranged attacks (is_ranged=1 sets retaliation=1
  before the check).
- Skipped for external (magic) damage.

So a unit can only retaliate once per round. After retaliating,
it's "spent" until next round. This prevents infinite mutual
retaliation loops.

#### 17.5.7 Absorb (Ghosts)

After the primary attack:
```c
if (attacker.abilities & ABIL_ABSORB):
    u.count += kills
```

Note: this uses `kills`, which is the actual number of creatures
killed, NOT `units_killed(final_damage, hp)`. So if the target
stack dies (kills < target.count), `kills` was overwritten to
`turn_count` (full stack). Ghosts grow by the full target
count.

Edge case: if the target had `t.injury > 0` going in, the
final_damage includes that, but `kills` counts only fully-dead
creatures. So Ghosts gain accurately based on souls captured.

#### 17.5.8 Leech (Vampires)

```c
if (attacker.abilities & ABIL_LEECH):
    u.count += units_killed(final_damage, u.hp)
    if (u.count > u.max_count):
        u.count = u.max_count
        u.injury = 0
```

So Vampires gain count based on damage / their own HP (not
target HP). Capped at `max_count` (the count at the start of
combat). Heals injury when capped.

So a 30 HP Vampire dealing 90 damage gains 90/30 = 3 Vampires.
This effectively makes Vampires self-healing.

#### 17.5.9 Damage to followers_killed

```c
if (war->heroes[t_side]):
    war->heroes[t_side]->followers_killed += kills
```

Only the player side has a hero. So friendly losses (when
player is the target) are counted toward `followers_killed`
in the player's KBgame. Score penalty: `-1 per kill`.

The AI side has `heroes[1] = NULL`, so AI losses are not
counted (no AI score).

#### 17.5.10 Edge cases

- **Stack dies**: `kills < t.count` is reset to `t.turn_count`,
  and `final_damage` is recomputed. This is to give Absorb/
  Leech accurate values.
- **`t.injury` carried**: previous-attack injury is added to
  this attack's damage before computing kills.
- **`t.injury` after**: `damage_remainder(final_damage, hp)`,
  ranges 0..hp-1.
- **`t.dead` flag**: set when stack is fully killed. Used by
  compact_units.

### 17.6 Movement, flight, attack driver

`move_unit(war, side, id, ox, oy)` (game.c:3429):

- Check screen edges: abort if out of [0, CLEVEL_W-1] × [0,
  CLEVEL_H-1] → return 0.
- If `omap[ny][nx]` set (obstacle) → return 0.
- If `umap[ny][nx]` set (another unit):
  - Resolve other's UID to (side,id). If friends and both in
    control, abort (return 0).
  - Call `hit_unit`, return 2.
- Else: `unit_relocate(war, side, id, nx, ny)`; `u.moves--`;
  if `moves == 0`, `acted = 1`; return 1.

`fly_unit(war, side, id, nx, ny)` (game.c:3470):

- Target (nx,ny) assumed already validated by `pick_target`.
- `unit_relocate(war, side, id, nx, ny)`.
- `u.flights--`; if zero, `acted = 1`.
- Redraw; log "<Troop> fly".

`hit_unit(war, a_side, a_id, t_side, t_id)` (game.c:3395):

- Snapshot `turn_count = count` for both attacker and target.
- `kills = unit_hit_unit(war, …)` — wraps `deal_damage(…, 0,0,0,1)`.
- Draw damage sprite on target; log "<Attacker> vs <Target>, N die".
- If `t.retaliated == 0`: retaliate (`t.retaliated = 1`), same
  formula but with attacker/target swapped. Draw damage on
  attacker; log "<Target> retaliate, killing N".
- Update turn_count, set `u.acted = 1`.
- `compact_units(war)` — move dead stacks aside.

#### 17.6.1 Why hit_unit, not direct deal_damage?

`hit_unit` snapshots `turn_count` for both attacker and target
BEFORE the attack starts. This ensures:
1. Damage formula uses pre-attack count.
2. Retaliation damage formula uses pre-retaliation count.

Without this, mid-attack damage modification could compound.

`hit_unit` also handles the visual: draw_damage shows the hit
sprite. After both swings (attack + retaliation), it calls
draw_combat to refresh the screen.

#### 17.6.2 Movement physics

`move_unit` is purely a logical move; it doesn't animate. The
visual sequence is:
1. Player presses arrow key.
2. `move_unit` returns 1 (moved), 2 (attacked), or 0 (blocked).
3. If blocked: no visual change.
4. If moved: `unit_relocate` updates `umap` and unit's x/y.
5. `draw_combat` redraws the field.
6. Next animation frame: unit appears at new position.

There's no smooth interpolation — units teleport between
positions one tile at a time.

### 17.7 Ranged shot

`unit_ranged_shot(war, side, id, other_side, other_id)` (play.c:1564)
— wraps `deal_damage(…, is_ranged=1, 0, 0, 0)`. `u.shots--`.

`unit_try_shoot` in game.c:5687:

- Guard: `if (!u.shots || unit_surrounded(war, side, id))` → "Can't
  Shoot" banner.
- `pick_target` with filter 4 (enemy unit).
- On confirm, if attacker has MAGIC and target has IMMUNE, log
  "The spell seems to have no effect!" and `acted = 1`.
- Else: shoot and log.

### 17.8 `compact_units` (play.c:1363)

Removes dead stacks and shifts trailing stacks up. After all 5
passes, wipes `umap` and rewrites from surviving units. If a hero
exists on that side, updates `game->player_troops/numbers` via
`accept_units_player`.

### 17.9 Combat loop

`combat_loop(game, combat)` in game.c:5992:

- Setup combat_state hotspots (§31) and grid click region.
- Main loop reads `combat_state` events; translates:
  - 16 arrow-equivalent keys → 8 directions; call `move_unit` with
    delta.
  - Action keys: a=view army, c=controls, f=fly, g=give up
    (yes/no), s=shoot, u=magic, v=view char, w=wait, space=pass,
    o=options menu, F10=cheat menu, click=grid.
- Every SYN tick, advance animation frame. If it's AI's turn or
  unit is out of control, `pass = ai_unit_think(combat)`.
- After an action: if player's active unit is dead, skip; if no
  more player army (`!test_defeat`), `done = 2` (AI wins);
  if no more AI army (`!test_victory`), `done = 1` (player wins).
  Else `next_unit(war)`; on phase-end, `next_turn(war)`.
- Returns 1 or 2 (which side won).

#### 17.9.1 Game loop iteration

Each iteration:
1. Read input via `KB_event(&combat_state)`.
2. Translate to action key index.
3. Dispatch action (move, attack, spell, view, etc.).
4. Check victory/defeat (`test_defeat` and `test_victory`).
5. If a unit acted, advance to next unit.
6. If all units on side acted, advance phase.
7. If both phases done, advance turn (swap sides).
8. Render combat field, statusbar.
9. Render frame animation tick.

Loop exits when `done != 0`:
- `done = 1`: player won.
- `done = 2`: player lost (or pressed Esc; or gave up).

#### 17.9.2 AI turn handling

When `combat->side == 1` (AI side), input is discarded:
```c
if (key != COMBAT_SYN_EVENT &&
    (auto_battle || combat->side != 0 || ooc)):
    key = 0
```

The AI moves on each SYN tick after a frame-cycle complete:
```c
if (++unit.frame > 3):
    unit.frame = 0
    if (combat->side == 1 || ooc):
        pass = ai_unit_think(combat)
```

So AI takes ~600ms per move (4 frames × ~150ms each). The AI
only acts at frame rollover, not on every tick.

`auto_battle` (set via cheat 'A') makes the player side also
AI-controlled — useful for testing.

### 17.10 Victory and defeat

`run_combat(game, mode, id)` (game.c:3488):

- mode 1 (castle siege) or 0 (foe). Prepare units with appropriate
  `prepare_units_*`. Reset match. `combat_loop`. Accept units
  (write back to game).
- If mode==0 and player won: remove foe tile from map
  (`map[c][y][x] = 0`).
- If AI won (winner==2): `temp_death(game); draw_defeat(game)`.
- If player won (winner==1):
  - If mode==1 and castle had a villain: set `villain_id =
    owner & 0x1F`; if `contract == villain_id`,
    `fullfill_contract` and `captured = 1`.
  - `gold += combat.spoils[1]` (AI side spoils).
  - If mode==1: `castle_owner[id] = 0xFF` (player's castle now).
  - `draw_victory(game, spoils[1], villain_id, captured)`.

`temp_death(game)` (play.c:950):

- Teleport home: continent=0, x=11, y=7-1=6 (at Home Castle gate).
- `mount = RIDE`.
- Wipe all troop slots (0xFF, count=0).
- Siege weapons revoked.
- Give 20 Peasants free in slot 0.

`draw_defeat` (game.c:3367):

- Plays `TUNE_DEFEAT`.
- Redraw map, sidebar, player sprite at frame 3.
- Full message (verbatim, see §24):
  "After being disgraced on the field of battle, King Maximus
  summons you to his castle. After a lesson in tactics, he
  reluctantly re-issues your commission and sends you on your way."

`draw_victory` (game.c:3295):

- Renders centered 16×36 framed box titled " Victory!" with
  player's name and class/rank, then:
  `"Well done <Name> the <Title>, you have successfully vanquished
  yet another foe. Spoils of War: <N> gold"` and (if villain):
  `" and the capture of <VillainName>"` + either "Since you did
  not have the proper contract, the Lord has been set free." or
  `"For fulfilling your contract you receive an additional <N>
  gold as bounty... and a piece of the map to the stolen scepter."`
- Press space to continue.

### 17.11 Distance and offset helpers

#### 17.11.1 isqrt32

Integer square root, fixed-point 16.16:

```c
dword isqrt32(dword h) {
    dword x = 0, y = 0;
    int i;
    for (i = 0; i < 32; i++) {
        x = (x << 1) | 1;
        if (y < x) x -= 2;
        else        y -= x;
        x++;
        y <<= 1;
        if (h & 0x80000000) y |= 1;
        h <<= 1;
        y <<= 1;
        if (h & 0x80000000) y |= 1;
        h <<= 1;
    }
    return x;
}
```

This is a digit-by-digit binary square root algorithm. Returns
the integer square root of `h` shifted up by 16 bits — i.e.
returns `sqrt(h) * 65536` rounded down.

For typical use (Pythagorean distance), the input is `dx² + dy²`
which fits comfortably in 32 bits even for full map distances
(64² + 64² = 8192).

The result is a fixed-point value; comparison via `<` works
correctly without conversion.

#### 17.11.2 calc_distance macro

```c
ipow2(a) := a * a
calc_distance(x1, y1, x2, y2) := isqrt32(ipow2(x2-x1) + ipow2(y2-y1))
```

Returns Pythagorean distance, fixed-point 16.16.

Used by:
- Foe pathfinding (foe_closest_offset).
- Combat AI movement (unit_closest_offset).
- Combat AI flying (unit_fly_offset).

The fixed-point precision is ~1/65536 — far more than needed for
6×5 grid. But it's the same code as foe pathfinding (LEVEL_W ×
LEVEL_H grid where 16-bit precision matters).

### 17.12 unit_closest_offset (combat AI movement)

`unit_closest_offset(war, side, id, position_x, position_y,
origin_x, origin_y, target_x, target_y, *ox, *oy)` (play.c:1627).

Used by combat AI to compute next move toward target. Slightly
different from foe_closest_offset (§19.1.3):

```c
max_dist = calc_distance(CLEVEL_W + 1, CLEVEL_H + 1, 0, 0)
picked_dist = max_dist

for j in [+1, 0, -1]:        // NOTE: REVERSE order vs foe pathfinder
  for i in [-1, 0, +1]:
    if position_x + i out of bounds: continue
    if position_y + j out of bounds: continue

    dist = calc_distance(position_x + i, position_y + j,
                         target_x, target_y)

    if !(position_x == origin_x and position_y == origin_y):
        // Flying mode: don't include "stay" in scan
        if i == 0 and j == 0: continue

    if war->omap[py + j][px + i]: dist = max_dist   // obstacle
    if war->umap[py + j][px + i] and !target_match:
        dist = max_dist   // unit blocking

    if dist < picked_dist:
        picked_dist = dist
        picked_x = i
        picked_y = j

if picked_dist < max_dist:
    *ox = picked_x          // -1, 0, or +1
    *oy = picked_y
else:
    *ox = 0
    *oy = 0
```

Differences from foe_closest_offset:
- Iteration order: `j = +1, 0, -1` (vs `j = -1, 0, +1` for foe).
  Different tie-breaking direction.
- Two obstacle maps (`omap` for terrain obstacles, `umap` for
  units).
- "Flying" mode option: when `position_x != origin_x` or
  `position_y != origin_y`, the scan is around `position` (the
  target's tile), not `origin` (the unit's tile). This lets
  flyers consider tiles adjacent to their target.
- Returns delta directly (`*ox = i`, not `picked_x - position_x`).

### 17.13 unit_fly_offset (flying AI)

`unit_fly_offset(war, side, id, other_full_id, *tx, *ty)`
(play.c:1701):

```c
ox, oy = unit_closest_offset(war, side, id,
                             other.x, other.y,    // position = target
                             u.x, u.y,             // origin = unit
                             other.x, other.y,     // target = same target
                             &ox, &oy)
nx = other.x + ox
ny = other.y + oy
if war->omap[ny][nx] or war->umap[ny][nx]:
    *tx = u.x; *ty = u.y          // can't fly there; stay
else:
    *tx = nx; *ty = ny
```

So flying picks a tile adjacent to the target (1 step away),
then verifies it's open. If blocked, returns the unit's current
position (no movement).

The result is an absolute target tile, not a delta. The flying
unit teleports to (tx, ty) without intermediate steps —
flight skips obstacles.

### 17.14 Random obstacle generation

At match start (`reset_match` for non-castle):

```c
for j in 0..CLEVEL_H-1:
    for i in 1..CLEVEL_W-3:        // skip cols 0, W-2, W-1
        if !(rand() % 10):           // 10% chance
            war->omap[j][i] = rand() % 3 + 1
```

So obstacles appear in cols 1..3 (4 of 6 columns) of the 5-row
field. ~10% per cell × 12 cells = ~1-2 obstacles per match.

Source comments call this out: "A) That IS NOT how it was done
in original KB. B) That has a FAIR CHANCE of flooding whole
level." So the openkb implementation is non-faithful.

### 17.15 Castle layout

For castle siege (mode 1), `reset_match` uses fixed
positions instead of random obstacles:

```
castle_umap[5][6] = {        // unit positions, 1-based UIDs
    { 0, 8, 6, 7, 9, 0 },     // row 0: AI back row (8,6,7,9 = AI 3,1,2,4)
    { 0, 0,10, 0, 0, 0 },     // row 1: AI castle (10 = AI unit 5)
    { 0, 0, 0, 0, 0, 0 },     // row 2: empty
    { 0, 5, 3, 4, 0, 0 },     // row 3: player back (5,3,4 = player 5,3,4)
    { 0, 0, 1, 2, 0, 0 },     // row 4: player front (1,2 = player 1,2)
};

castle_omap[5][6] = {         // obstacles (castle walls)
    { 8, 0, 0, 0, 0, 9 },     // walls at left (8) and right (9)
    { 8, 0, 0, 0, 0, 9 },
    { 8, 0, 0, 0, 0, 9 },
    { 8, 0, 0, 0, 0, 9 },
    { 5, 7, 0, 0,10, 6 },     // bottom row: walls + decorations
};
```

Player units are at the bottom; AI units are at the top behind
castle walls. Players must approach the castle to attack.

### 17.16 Spoils computation

```c
for i = 0..MAX_UNITS-1:
    war->spoils[side] += troops[id].spoils_factor * 5 * count
```

Each side accumulates spoils based on initial unit count and
spoils_factor.

After victory:
- AI side spoils → player gold.
- Player side spoils discarded.

A typical foe encounter (3 stacks, ~50 units each) yields
~3 × 5 × 50 = 750 gold (assuming spoils_factor 1).
Heavy combat (Dragons): 500 × 5 × 5 = 12500 gold.

## 18. Spell effects in detail

Each spell's full implementation, with edge cases and bugs
called out.

### 18.0 Spell selection menu

Spells are selectable via U in adventure or combat. `choose_spell`
(game.c:4130):

- If `!game->knows_magic`: show no_spell_banner and return.
- If combat and `combat->spells != 0`: "Only 1 spell per round!"
  and return.
- Draw the spell menu: 7 rows × 2 columns (combat | adventure),
  "Cast which <mode> spell (A-G)?".
- Accept A..G. If no spells of that type: "You don't know that
  spell!" banner.
- Else dispatch by spell_id. Decrement `spells[id]`;
  `combat->spells++` if in combat.

### 18.1 Clone (id 0) — combat

`clone_troop(game, war, unit_id)` (play.c:1832):

```
hp = troops[units[0][unit_id].troop_id].hp
damage = spell_power * 10 + units[0][unit_id].injury
clones = damage / hp
injury = damage % hp
units[0][unit_id].count += clones
units[0][unit_id].max_count += clones
units[0][unit_id].injury = injury
return clones
```

UI: `clone_army(game, war)` — pick_target filter 3 (friendly),
report "<N> <Troops> cloned".

### 18.2 Teleport (id 1) — combat

`teleport_army(game, war)` (game.c:3968):

- Pick source: filter 2 (any unit with count).
- Pick destination: filter 1 (unoccupied, no obstacle).
- `unit_relocate(war, side, unit_id, nx, ny)`.

### 18.3 Fireball (id 2) — combat

`damage_army(game, combat, 25, 2)`. Pick filter 4 (enemy unit).
In source, the actual `magic_damage` call is commented
out/missing — the function currently only prompts; actual damage
application is a **stub** (see §34).

### 18.4 Lightning (id 3) — combat

Same as Fireball but base_damage=10. Also stub in openkb.

### 18.5 Freeze (id 4) — combat

`freeze_troop(game, war, side, id)` (play.c:1817):

- If target has `ABIL_IMMUNE` → return -1 (no effect).
- `u.frozen = 1`.
- Return 1.

UI: `freeze_army` picks filter 4. A frozen unit's AI behavior:
`if (u.frozen) { u.acted = 1; combat_log("<Troop> are frozen"); }`.

### 18.6 Resurrect (id 5) — combat

`resurrect_army(game, game, war)` (game.c:3933): picks filter 3
(friendly). **Function is stub** — it calls `pick_target` and
returns; no `count += ...` exists.

### 18.7 Turn Undead (id 6) — combat

`damage_army(game, combat, 50, 6)`. Base=50. Intended to filter
to undead enemies only (filter was supposed to be 5 = enemy undead)
but `damage_army` passes filter=4 in current code. **Also stub.**

### 18.8 Bridge (id 7) — adventure

`build_bridge(game)` (game.c:3787):

- Show "Build bridge in which direction ←↑↓→" in top box.
- Read one arrow key (LEFT/UP/DOWN/RIGHT).
- Based on direction, set `di, dj` to ±1 on one axis only.
- `tile = (dj != 0) ? TILE_BRIDGE_V : TILE_BRIDGE_H`.
- For i in 1..2 (two tiles in direction):
  - `x, y = game->x + i*di, game->y - i*dj`
  - `m = map[c][y][x]`; if not `IS_WATER(m)`, break.
  - `map[c][y][x] = tile`; `built++`.
- If built==0: top box "Not a suitable location for a bridge" →
  Pause → "What a waste of a good spell!" → Pause.

### 18.9 Time Stop (id 8) — adventure

`time_stop(game)` (play.c:1790): `game->time_stop += spell_power
* 10`. **Note:** this is `+=` not `+=100`; comment in spec notes
a known bug — game.c references `*= 100` behavior but source is
`* 10` (see §34).

### 18.10 Find Villain (id 9) — adventure

`find_villain(game)` (play.c:1872):

- Iterate castles: find one whose `(owner & 0x3F) ==
  last_contract`; set `owner |= KBCASTLE_KNOWN (0x40)`; return i.
- Then `draw_map(game, 0); draw_sidebar(game, 0); view_contract(game)`.
  The contract view now shows the revealed castle.

### 18.11 Castle Gate (id 10) — adventure

`select_gate(game, 0)` (game.c:4002): show a grid of castle
letters A-Z (where visited `castle_visited[i]` is true). Player
presses letter; if visited:

- If sailing, vacate boat (mount=RIDE, drop boat at current x,y).
- `continent, x, y = castle_coords[c][0..2]` with y -= 1 (gate is
  just below the castle tile).
- `last_x = x; last_y = y`.

### 18.12 Town Gate (id 11) — adventure

`select_gate(game, 1)`: similar but uses `town_visited` and
`towngate_coords`. Town letters A-Z map via `town_inversion`.

### 18.13 Instant Army (id 12) — adventure

`instant_troop(game, *troop_id)` (play.c:1846):

```
troop_id = classes[class][rank].instant_army
for i in 0..4:
  if player_troops[i] == troop_id or player_numbers[i] == 0:
    slot = i; break
if slot == -1: return 0    // no slot
number = (spell_power + 1) * instant_army_multiplier[rank]
player_troops[slot] = troop_id
player_numbers[slot] += number
return number
```

UI: "  There are no open slots or any of this army type!" if
spawn failed; else "<Count> <Troops> have joined to your army."

### 18.14 Raise Control (id 13) — adventure

`raise_control(game)` (play.c:1794): `game->leadership +=
spell_power * 100`. The elevated leadership persists until
end_week.

### 18.15 pick_target — target selector

The `pick_target(war, *x, *y, filter)` function (game.c:5550)
provides target selection for spells and ranged attacks. It
shows a moving cursor on the combat field; the player navigates
with arrows, presses Enter to confirm.

Filter values:
- 0: no filter, accept any tile.
- 1: tile must be unoccupied (no unit, no obstacle).
- 2: tile must contain a unit (any side).
- 3: tile must contain friendly unit (player side, in control).
- 4: tile must contain enemy unit (opponent side, or
  out-of-control friendly).
- 5: tile must contain enemy undead unit (filter 4 + UNDEAD
  ability check; not implemented in damage_army calls).

Returns 1 if confirmed, 0 if cancelled (Esc).

The cursor sprite is `comtiles` frame 11..14 (animated).

### 18.16 Spell spell_power scaling table

Each spell's effect at various `spell_power` values:

| Spell        | SP=1 | SP=2 | SP=3 | SP=5 | SP=10 | SP=15 |
|--------------|------|------|------|------|-------|-------|
| Clone        | 10   | 20   | 30   | 50   | 100   | 150 (per HP) |
| Time Stop    | 10   | 20   | 30   | 50   | 100   | 150 (steps) |
| Raise Control| 100  | 200  | 300  | 500  | 1000  | 1500 (leadership) |
| Fireball     | 25   | 50   | 75   | 125  | 250   | 375 (when wired) |
| Lightning    | 10   | 20   | 30   | 50   | 100   | 150 (when wired) |
| Turn Undead  | 50   | 100  | 150  | 250  | 500   | 750 (when wired) |
| Instant Army | 6@r0 | 9@r0 | 12@r0| ...  | ...   | depends on rank |

(SP = spell_power. Sorceress reaches SP 15; other classes max 5.)

### 18.17 Spell-cast workflow summary

In combat:
1. Press U.
2. choose_spell pre-checks (knows_magic, spells-this-round).
3. Menu shown; choose A-G.
4. spells[id] decremented.
5. combat->spells incremented (limit to 1 per round).
6. Spell-specific UI (target picker, build_bridge direction, etc).
7. Spell effect applied (or stub: no-op for some spells).
8. Combat continues; unit's `acted` flag may or may not be set
   (some spells consume the unit's turn, others don't).

In adventure:
1. Press U.
2. choose_spell pre-checks.
3. Menu shown.
4. spells[id] decremented.
5. Spell-specific UI.
6. Spell effect applied.
7. Adventure loop continues.

Note: casting an adventure spell does NOT consume a step or a
day. Spells are "free" actions on the overworld, beyond the
gold cost paid to learn them.

## 19. AI behavior

### 19.1 Foe follow (`foes_follow`, play.c:812)

After each successful player move on the overworld,
`adventure_loop` (game.c:6551) calls `foes_follow(game)`. This
moves any nearby hostile foe one tile closer to the player's
**previous** position (`last_x, last_y`) — so foes chase the
trail the player left, not where the player just landed.

#### 19.1.1 Outer scan (`foes_follow`, play.c:812)

```
for i in 0..MAX_FOES-1:           // 0..39
    fx = foe_coords[continent][i][0]
    fy = foe_coords[continent][i][1]

    // Skip dead foes — their tile has been cleared (0) by
    // either the spawner or a prior fight.
    if map[continent][fy][fx] != 0x91:
        continue

    // Compute Chebyshev radius (axis-aligned)
    diff_x = abs(fx - last_x)
    diff_y = abs(fy - last_y)

    // Out of follow radius (3×3 ring around last_x,last_y)
    if diff_y > 2 or diff_x > 2:
        continue

    foe_follow(game, i)
```

Notes:
- The radius check is **per-axis** (Chebyshev), not Euclidean.
  A foe at (last_x+2, last_y+2) — diagonal corner of the 5×5
  square — qualifies; one at (last_x+3, last_y) does not.
- Both friendly (slots 0..4) and hostile (slots 5..39) foes
  follow. The behavior is identical for both; the friendly
  designation only affects what happens when the player steps
  onto the tile (see §20.7 `accept_foe` vs combat).
- Foes follow **all 4 continents' arrays** indirectly only because
  the function scans `foe_coords[continent]` for the *current*
  continent. Foes on continents the player isn't on do not move.
- Iteration order is slot 0 → 39 sequentially; if foe N moves
  onto a tile that foe M (M > N) was about to move from, M's
  position check (`map[fy][fx] != 0x91`) still passes for M —
  but foe N is now also at 0x91 elsewhere, possibly causing
  pile-ups. The simple loop has no occupancy check across foes
  in this pass.

#### 19.1.2 Single-foe move (`foe_follow`, play.c:794)

```
fx = foe_coords[continent][foe_id][0]
fy = foe_coords[continent][foe_id][1]

foe_closest_offset(game, foe_id,
    fx, fy,            // foe's current position
    last_x, last_y,    // target = player's previous position
    &move_x, &move_y);

foe_relocate(game, foe_id, fx + move_x, fy + move_y)
```

Note that `foe_follow` does not validate the returned offset
before relocating. If `foe_closest_offset` returns the
"unwalkable" sentinel (described below), the foe's new position
is `(fx + position_x, fy + position_y)` — i.e., `(2*fx, 2*fy)`
— which is off-map and corrupts state. In practice this branch
isn't reached because the foe's current tile is always
considered (offset 0,0 with the obstacle check skipped — see
below), so the loop always finds at least one valid candidate.

#### 19.1.3 Closest-offset search (`foe_closest_offset`, play.c:1738)

This function picks the best of the 9 tiles in a 3×3 square
centered on the foe (including staying put). "Best" = minimum
integer Pythagorean distance to the target tile.

```c
max_dist  = calc_distance(LEVEL_W+1, LEVEL_H+1, 0, 0)
                        // = isqrt32(65*65 + 65*65) ≈ 92 fixed-point
picked_dist = max_dist
picked_x, picked_y = (uninitialized)

for j in [-1, 0, 1]:        // y offset
  for i in [-1, 0, 1]:      // x offset

    // Map-bounds clamp
    if position_x + i < 0:        continue
    if position_y + j < 0:        continue
    if position_x + i >= LEVEL_W: continue
    if position_y + j >= LEVEL_H: continue

    dist = calc_distance(position_x + i, position_y + j,
                         target_x, target_y)

    // Obstacle test — only when actually moving (i or j nonzero)
    if (i != 0 or j != 0):
        if map[continent][position_y + j][position_x + i] != 0:
            dist = max_dist           // mark as unwalkable

    if dist < picked_dist:
        picked_dist = dist
        picked_x    = position_x + i
        picked_y    = position_y + j

if picked_dist < max_dist:
    *ox = picked_x - position_x      // -1, 0, or +1
    *oy = picked_y - position_y      // -1, 0, or +1
else:
    *ox = position_x                  // BUG: should be 0
    *oy = position_y                  // BUG: should be 0
```

Distance helpers (play.c:1588 and ~1616):

```c
ipow2(a)         := a * a
calc_distance(x1,y1,x2,y2) := isqrt32(ipow2(x2-x1) + ipow2(y2-y1))

isqrt32(h)       := integer square root, 16.16 fixed-point
                    (binary digit-by-digit method, see source)
```

So `dist` is a `dword` representing a fixed-point distance with
16 fractional bits. Comparison is direct integer comparison;
ordering matches Euclidean order.

Key behaviors:

- **Tie-breaking**: strictly less-than (`<`) means the first
  candidate with the minimum distance wins. With the iteration
  order `j = -1, 0, +1` and `i = -1, 0, +1`, the priority is:

  ```
  scan order   delta(i,j)
       1       (-1, -1)    NW
       2       ( 0, -1)    N
       3       (+1, -1)    NE
       4       (-1,  0)    W
       5       ( 0,  0)    stay
       6       (+1,  0)    E
       7       (-1, +1)    SW
       8       ( 0, +1)    S
       9       (+1, +1)    SE
  ```

  When two tiles have the same distance, the earlier-scanned one
  wins. So a foe directly north of the player has equal-distance
  options at NW, N, and NE; NW is picked. This produces a
  consistent diagonal bias toward the upper-left.

- **The "stay" option always passes the obstacle test**: the
  obstacle check is gated by `if (i || j)`, so the foe's own tile
  is never rejected. This means `picked_dist` is always set by
  iteration 5 at the latest, and the `else` branch returning the
  buggy unwalkable values is unreachable in practice (provided
  the foe's current position is on-map, which it is by
  invariant).

- **Obstacle definition**: any tile whose value is non-zero
  blocks. Recall tile id 0 is grass. Bridges (8, 9), water
  (≥0x14), trees, deserts, rocks, and all interactive tiles
  (high-bit set) all read non-zero. So foes:
  - **Cannot** walk onto water, trees, sand, rocks, bridges,
    castles, towns, chests, dwellings, signposts, artifacts, or
    other foe tiles.
  - **Can** walk only on grass-id-0 tiles. That includes
    `TILE_GRASS = 0x00` but NOT `IS_GRASS` macro's other accepted
    values (`< 2` and `0x80`); only literal id 0. (The "alt
    grass" tile 0x01 is technically also passable to the player,
    but the foe pathfinder treats it as obstacle.)
  - **Can** stand on their own current tile (which reads as 0x91
    `TILE_FOE`) because the obstacle test skips when `i==j==0`.

- **The bug `*ox = position_x`** in the else branch: if every
  surrounding tile were unwalkable, the function would return
  `(*ox, *oy) = (position_x, position_y)` instead of `(0, 0)`,
  which `foe_follow` then adds to the foe's position, producing
  `(2*fx, 2*fy)` — far outside the map. This is unreachable in
  practice (see above) but documents incorrect intent.

#### 19.1.4 Relocation (`foe_relocate`, play.c:780)

```
old_x = foe_coords[continent][foe_id][0]
old_y = foe_coords[continent][foe_id][1]

// Clear foe from old tile
map[continent][old_y][old_x] = 0           // TILE_GRASS

// Mark new tile as foe
map[continent][new_y][new_x] = 0x91        // TILE_FOE

// Update foe's stored coordinates
foe_coords[continent][foe_id][0] = new_x
foe_coords[continent][foe_id][1] = new_y
```

The old tile is forced to grass (id 0) regardless of what the
underlying terrain was before the foe was placed. This is fine
because foe placement during salting (§14.7) preserved the
chest's coordinates by replacing the tile, but it means foes
"erase" any decorative tile they leave (no terrain restoration).

The new tile becomes 0x91 unconditionally. Combined with the
obstacle check, this is what prevents two foes from occupying
the same square: by the time foe N+1 is processed, foe N has
already written `0x91` to its destination, which fails foe N+1's
obstacle check.

#### 19.1.5 Trigger sequence (recap from §15.1)

After the player successfully moves to a new tile:

1. `last_x, last_y = old player position` (already set before
   the move).
2. `x, y = new player position` (just set).
3. `clear_fog(game)` reveals 5×5 around new (x,y).
4. Tile-interaction switch runs (chest pickup, dwelling, etc).
5. **`foes_follow(game)`** — every nearby foe steps toward
   `last_x, last_y`.
6. Check: if `map[continent][y][x] == TILE_FOE`, a follower
   stepped onto the player. The adventure loop's `walk = 1;
   continue` re-runs the move logic, which on this iteration
   triggers `attack_foe(game)` because the player is now standing
   on a foe tile. (Subtle: the player doesn't move again; the
   `continue` lets the interaction switch fire.)

#### 19.1.6 Strategic implications

- A foe at distance 2 (corner of follow radius) takes 2 turns to
  reach the player. The first foe-follow step cuts the diagonal
  to distance √2 (≈1.4); the second step closes it.
- Foes can be lured around obstacles. Because they only consider
  9 tiles each step (greedy), they get stuck against walls
  perpendicular to the player's direction.
- The follow radius (Chebyshev 2) is exactly the size of the map
  viewport edges (5×5 visible centered on player). Foes
  off-screen do not move.
- A foe in a corner with all 8 neighbors blocked stays put (the
  "stay" option always wins as the only valid candidate).

#### 19.1.7 Interaction with combat-victory cleanup

When the player wins a combat at a foe tile (`run_combat` mode 0,
victory branch), `game.c:3531` clears the tile:

```
game->map[continent][y][x] = 0;
```

This is the canonical "foe is dead" marker that `foes_follow`
checks against (`if map[…] != 0x91`). The foe's slot in
`foe_coords` is *not* cleared — only its tile. Its slot becomes
inactive and is permanently skipped on subsequent passes.

#### 19.1.8 Pseudocode reference for clean-room reimplementation

```python
def foes_follow(game):
    cont = game.continent
    for i in range(MAX_FOES):
        fx = game.foe_coords[cont][i][0]
        fy = game.foe_coords[cont][i][1]
        if game.map[cont][fy][fx] != 0x91:
            continue
        if abs(fx - game.last_x) > 2: continue
        if abs(fy - game.last_y) > 2: continue
        foe_follow(game, i)

def foe_follow(game, foe_id):
    cont = game.continent
    fx = game.foe_coords[cont][foe_id][0]
    fy = game.foe_coords[cont][foe_id][1]
    ox, oy = foe_closest_offset(game, foe_id,
                                fx, fy,
                                game.last_x, game.last_y)
    foe_relocate(game, foe_id, fx + ox, fy + oy)

def foe_closest_offset(game, foe_id, px, py, tx, ty):
    cont = game.continent
    max_dist = isqrt32_fp(((LEVEL_W+1)**2 + (LEVEL_H+1)**2) << 16)
    picked_dist = max_dist
    picked = None
    # Iteration order: NW,N,NE,W,stay,E,SW,S,SE
    for j in (-1, 0, +1):
        for i in (-1, 0, +1):
            x, y = px + i, py + j
            if not (0 <= x < LEVEL_W and 0 <= y < LEVEL_H):
                continue
            d = isqrt32_fp(((tx-x)**2 + (ty-y)**2) << 16)
            if (i != 0 or j != 0) and game.map[cont][y][x] != 0:
                d = max_dist
            if d < picked_dist:
                picked_dist = d
                picked = (x, y)
    if picked_dist < max_dist:
        return (picked[0] - px, picked[1] - py)
    else:
        # Reached only if foe is off-map (invariant: not).
        return (px, py)   # BUG; should be (0, 0)

def foe_relocate(game, foe_id, nx, ny):
    cont = game.continent
    ox = game.foe_coords[cont][foe_id][0]
    oy = game.foe_coords[cont][foe_id][1]
    game.map[cont][oy][ox] = 0          # restore grass
    game.map[cont][ny][nx] = 0x91       # plant foe
    game.foe_coords[cont][foe_id][0] = nx
    game.foe_coords[cont][foe_id][1] = ny
```

#### 19.1.9 Differences from combat AI's `unit_closest_offset`

The combat AI uses `unit_closest_offset` (play.c:1627), which is
similar but with these differences:

- Iteration order is `j = +1 down to -1` (reverse of foe loop)
  and `i = -1 to +1`. This produces a different tie-break:
  combat units prefer SW > S > SE > W > stay > E > NW > N > NE.
- Combat checks two maps (`omap` and `umap`) for obstacles vs
  units; foe pathfinder only checks `game->map`.
- Combat distinguishes flying vs walking: when computing for a
  flyer, position is the *target's* position (so flyers can
  consider tiles adjacent to their target directly, not adjacent
  to themselves). The foe pathfinder is always walking-style.

The source comment at play.c:1737 explicitly says
`foe_closest_offset` is "copy-pasted from unit_closest_offset"
with foe-specific simplifications.

### 19.2 AI unit AI (`ai_unit_think`, game.c:5812)

1. Pick close target (within 1 tile, enemy). `ai_pick_target(war, 1)`
   returns a UID or -1.
2. If unit is frozen: `acted = 1`; log "<Troop> are frozen".
3. If has shots AND close_target == -1:
   - Pick far target `ai_pick_target(war, 0)`.
   - If found: `unit_ranged_damage(war, …)`.
4. If can fly AND close_target == -1:
   - Pick far target; `unit_fly_offset(…)` computes target position
     adjacent to far target; if movable, `fly_unit(…)`.
5. If no shots AND not done yet:
   - If close_target == -1: close_target = far target.
   - `unit_move_offset(…)`: compute 1-step offset toward target.
   - If `(ox, oy) != (0,0)`: `move_unit(…)`; if moved,
     `draw_combat`, log "<Troop> move".
6. If nothing acted: `unit_try_wait(combat)`.

### 19.3 Target picking (`ai_pick_target`)

Scans both sides and every unit, skipping:

- Units with count == 0.
- Own unit (same side+id) if under control.
- Own side entirely if under_control (AI only targets enemies, but
  out-of-control units attack allies).
- If `nearby` flag: skip any unit not `unit_touching` our position.

Preference among candidates:

- Shooters (shots > 0) preferred for **far** targets.
- Otherwise, lower-HP targets preferred.

Returns the chosen unit's packed UID (`PACK_UID(side, id) = side*5
+ id + 1`).

### 19.4 Grid heuristic (game.c:5921)

`grid_heuristic(combat, side, id, x, y, *uid, *nx, *ny)` — used
when the player clicks a combat tile:

- If tile contains self: COMBAT_INTENT_WAIT.
- If tile contains friend: COMBAT_INTENT_NONE.
- If tile contains enemy nearby (1 tile): COMBAT_INTENT_MELEE,
  with nx/ny = step delta.
- If tile contains enemy far: if has shots & not surrounded:
  COMBAT_INTENT_SHOOT; else if ranged_ammo: CANT_SHOOT; else NONE.
- If tile has obstacle: CANT_MOVE.
- If empty nearby: COMBAT_INTENT_MOVE, with step delta.
- If empty far: if has flights: FLY; else if has ABIL_FLY: CANT_FLY;
  else CANT_MOVE.

## 20. Chests, recruits, and rewards

Resource acquisition: how the player gets gold, leadership,
spells, and troops. Most resources flow through "chest"
encounters or recruitment menus.

### 20.0 Resource flow summary

```
World map ─── chest tiles ─── (salt) ─── various rewards
                                           gold, commission,
                                           spell_power,
                                           max_spells, spells

Towns ───── A) contracts (villain id)
           B) boats (mount transition)
           C) information
           D) spells (purchase from town_spell)
           E) siege weapons (3000 gp)

Castles ── home: A) recruit (5 castle troops)
                 B) audience (rank promotion)
            own: garrison/remove
            enemy: lay siege

Dwellings ─ recruit (1 troop type per dwelling)
            (pop refreshed at end_week of matching creature)

Foes ── friendly: random recruit
       hostile: combat
       (combat win: spoils, optional bounty)
```

### 20.1 Chest flow (`take_chest`, game.c:3094)

Play `TUNE_CHEST`. Then:

- **Navmap chest:** if `continent < MAX_CONTINENTS-1` AND
  `(x,y) == map_coords[cont]`:
  - `continent_found[cont+1] = 1`.
  - Banner: "Hidden within an ancient chest, you find maps and
    charts describing passage to <NextContinent>."
- **Orb chest:** if `(x,y) == orb_coords[cont]`:
  - `orb_found[cont] = 1`.
  - Banner: "Peering through a magical orb you are able to view
    the entire continent. Your map of this area is complete."
  - `view_minimap(game, 1)` — force full-continent view.
- **Treasure chest:** else roll `chance = KB_rand(1, 100)`:
  - (see §13.3 for thresholds)
  - Gold: call `gold_or_leadership(game, gold_amount,
    leadership_amount)` — two-button choice.
  - Commission: `commission += points` + banner.
  - Spell power: +1 + banner "Traversing the area, you stumble
    upon a time worn cannister... Your Spell Power by <1> and
    vanishes." (openkb note: `+= points` where points=1 always).
  - Max spell: `max_spells += points` (doubled with Ring) + banner.
  - New spell: `spells[spell] += spell_num` + banner
    "<N> <SpellName> spell."
  - Empty: "The chest was empty!"
- Clear tile: `map[c][y][x] = 0`.

### 20.2 Gold or leadership choice (`gold_or_leadership`)

Banner:
```
After scouring the area,
you fall upon a hidden
treasure cache. You may:
A) Take the <N> gold.
B) Distribute the gold to
the peasants, increasing
your leadership by <N>.
```
Reads y/n via two_choices (A=gold, B=leadership).

### 20.3 Recruit soldiers

Home castle (`visit_home_castle`, game.c:2247) offers barracks
flavor only: castle troops are 5 specific ids — filtered by
`dwells == DWELLING_CASTLE`. Those 5 (from §5) are: 0x02 Militia,
0x08 Archers, 0x0A Pikemen, 0x0E Knights, 0x12 Cavalry.

`recruit_soldiers(game, loc_id, troop_id)` (game.c:2121):

- Lists 5 castle-dwelling troops with prices, OR "n/a" when the
  player's total leadership can't fit at least **6** of that troop —
  i.e. `total_leadership < troops[id].hp * 6`, independent of what's
  currently in the army. Once a troop is reachable at this rank its
  cost stays visible even after the player has filled their
  leadership headroom; "n/a" only means "not enough leadership to
  meaningfully recruit this type yet."

  Empirical DOS-binary rule, confirmed across all four classes at
  rank 0:
    Knight    (100 lead): Cavalry (20) n/a, Knights (35) n/a, others ok
    Paladin   ( 80 lead): same pattern (n/a for D and E)
    Sorceress ( 60 lead): same pattern (n/a for D and E)
    Barbarian (100 lead): same as Knight
  Pikemen / Archers (HP=10) always show cost: `60 < 10*6 = 60` is
  false, so the cutoff is exactly `< hp*6`.

  (Spec/openkb-reference gap: openkb-reference unconditionally
  prints `recruit_cost` regardless of leadership. The "n/a" gating
  exists only in the DOS binary; openbounty matches the binary.)
- Select letter A..E; input numeric count. Letters whose troop has
  `max == 0` are still pickable in the source loop, but immediately
  return to idle since the inline numeric input clamps to 0.
- Compute `max = army_leadership(game, troop_id) / troops[id].hp`.
- `buy_troop(game, troop_id, number)`:
  - `cost = recruit_cost * number`; if cost > gold → return 1.
  - `add_troop(game, troop_id, number)`:
    - Find slot: first slot matching this troop_id, OR first empty
      slot.
    - If slot found: set troop_id, numbers += number. Else fail.
  - If add failed: return 2.
  - `gold -= cost`; return 0.
- Errors: 1 → "You don't have enough gold!", 2 → "No troop slots
  left!"
- After a purchase commit (success or error popup dismissed),
  return to letter-select state. (UX deviation from openkb-reference,
  which leaves `whom` set so the player types another count for the
  same troop. OpenBounty resets `whom = 0` so each ENTER returns
  to the A..E list.)

### 20.4 Outer dwellings

`visit_dwelling(game, rtype)`: rtype is the dwelling kind (0..3,
from tile 0x8C..0x8F minus 0x8C). Finds which dwelling index is at
current coords. Troop id = `dwelling_troop[cont][id]`.

UI displays:

```
           <DwellingName>
           --------------
<pop> <Troops> are available
Cost=<N> each.      GP=<gold>K
You may recruit up to <max>
Recruit how many
```

Numeric input, max = `army_leadership / troops[id].hp` (not pop).
If player enters amount > pop: silently ignored. If purchase
succeeds: `dwelling_population[cont][id] -= number`.

### 20.5 Castle (player-owned) — garrison

`visit_own_castle(game, castle_id)` (game.c:2319):

- Two modes: MODE_GARRISON (move FROM player TO castle),
  MODE_REMOVE (FROM castle TO player). Toggled by Space bar.
- List 5 slots; press A..E to move one. If the move would strip
  player of all armies (`garrison_troop` returns 2): "You cannot
  garrison your last army!"
- `garrison_troop(game, castle_id, slot)` (play.c:892):
  - If player_troops[1] == 0xFF or numbers[1] == 0 → return 2.
  - Find castle slot matching this troop OR empty.
  - `castle_troops[c][s] = player_troops[slot]`;
    `castle_numbers[c][s] += player_numbers[slot]`;
    `dismiss_troop(game, slot)`.
- `ungarrison_troop(game, castle_id, slot)` (play.c:928):
  - `add_troop(game, castle_troops[c][s], castle_numbers[c][s])`.
  - Shift castle slots `[slot+1..4]` down.

### 20.6 Castle (enemy) — siege

`lay_siege(game, castle_id)` (game.c:2456):

- Banner shows who owns it:
  - Monsters: "Various groups of monsters occupy this castle."
  - Villain: "<VillainName> and army occupy this castle."
- Prompt "Lay Siege (y/n)?". On y: `run_combat(game, 1, id)`.

Siege with no siege weapons: **not enforced in code.** The
`game->siege_weapons` flag is only set (in code) and checked only
via cheat 's'. Castles are attackable regardless. (Original DOS
KB required siege weapons; openkb simulates purchase but not
gate-check.)

### 20.7 Foe encounters

`attack_foe(game)` (game.c:3626):

- Find foe id by scanning `foe_coords[cont]` for match.
- If `id < FRIENDLY_FOES` (slots 0..4): `accept_foe(game, id)`
  — these are friendly rolls:
  - `roll_creature(continent, &troop_id, &troop_count)` — fresh
    random roll.
  - Banner: "<N> <Troops>, with desires of greater glory, wish to
    join you. Accept (y/n)?"
  - If no army slots free: "flee in terror at the sight your vast
    army." Remove tile.
  - On yes: `add_troop(game, troop_id, troop_count)`; remove tile.
- Else (hostile): banner "Your scouts have sighted:" + 3 troop
  lines with `number_name`; "Attack (y/n)?". On yes:
  `run_combat(game, 0, id)`.

### 20.8 Signposts

`read_signpost(game)` (game.c:3022):

- Walk all 4 continents' tiles, counting 0x90 tiles in
  declaration order. Find the id of the current sign.
- `sign = STR_SIGN[id]`.
- Banner "A sign reads:" + sign text.

### 20.8.1 Sign indexing

The signpost id is computed by walking ALL 4 continents in row-
major order, counting 0x90 tiles. So:
- Sign 0 is the first 0x90 in continent 0, top row.
- Sign 1 is the second 0x90, etc.
- Continent 0's signs come before continent 1's, etc.

This means adding/removing/moving 0x90 tiles in LAND.ORG (in any
continent) shifts ALL subsequent sign ids. The DOS sign strings
are keyed by sequential index, so any reorder breaks correlation.

Because openkb's sign-indexing is across-continents, modders
who add a sign on continent 0 shift all continents 1..3's signs.
GNU module's `signs.txt` must match the sign-walk order exactly.

### 20.9 Alcove (`visit_alcove`, game.c:2831)

Archmage Aurange's Alcove on Continentia at (11,19).

Banner: "The venerable Archmage, Aurange, will teach you the
secrets of spell casting for <5000> gold. Accept (y/n)?"

- y + `gold >= 5000`: `knows_magic = 1`, `spend_gold(5000)`,
  remove tile (`map[c][y][x] = 0`).
- y + `gold < 5000`: `nogold_banner(game, 2 + DWELLING_HILLCAVE,
  creature=0x06 Gnomes)` — which shows "The sign said 5000 gold!
  Why waste my valuable time when you know you don't have the
  required amount of gold? Begone until you do!"
- n: exits.

Background: `draw_location(2 + DWELLING_HILLCAVE, 0x06, frame)` —
hill-cave background with Gnomes animation (openkb hard-codes
Gnomes, not the actual Archmage animation).

### 20.10 Teleport caves

`visit_telecave(game, force)` (game.c:2900):

- Walk `teleport_coords[cont][0..1]`. If current (x,y) matches
  one of them:
  - If force: `x = teleport_coords[cont][1-i][0]`, same for y.
    This teleports to the paired cave.
  - Return 0 (is a telecave).
- Return 1 (not a telecave; fall through to dwelling handler).

## 21. Victory, defeat, scoring

### 21.1 Score

`player_score(game)` already shown in §14.2:

```
score = captured*500 + artifacts*250 + castles*100 - killed*1
if diff == 0: score /= 2
else          score *= difficulty_modifier[diff]   // 1, 2, 4
if score < 0: score = 0
```

Displayed in `view_character` and at `win_game`.

### 21.2 Win condition

Only reachable via Search action when standing on
`(scepter_continent, scepter_x, scepter_y)`. Due to the
`bury_scepter` bug (§14), this condition is effectively never
valid without cheats (`win_game` via F10→W bypasses the test).

### 21.3 `win_game` UI (game.c:4398)

1. `display_cartoon()` — shows the hero walking a bridge
   cutscene (frames 0..10).
2. Load ending image `GR_ENDING, 0` (won).
3. Draw right half as image, left half filled with `CS_ENDING`
   background color.
4. Render ending text (STRL_ENDINGS, 0) with player name/class
   substituted. Append `%d` with `player_score(game)` appended.
5. Wait for Esc.

### 21.4 `lose_game` UI (game.c:4462)

1. Load ending image `GR_ENDING, 1` (lost).
2. Draw similar split layout.
3. Render STRL_ENDINGS, 1 with selection colors.
4. Wait for Esc.

Triggers: `days_left == 0` → lose_game from adventure loop; Q
cheat key triggers `lose_game` via `debug_cheat_menu 'l'`.

### 21.5 `display_cartoon` (game.c:4281)

Cutscene animation:

- 5-row × 6-col grass field (tile 0 from `GR_ENDTILE`).
- Bridge (tile 1) and hero (tile 2).
- `frame` grows 0..10; bridge_len = frame (cap 5);
  hero_prog = frame-5 (cap 4).
- Draw troops around the scene in 4×6 layout, each animated at
  its `tick` (0..3 cycle).

## 22. Save file (.DAT) — complete 20,421-byte layout

`KB_loadDAT` / `KB_saveDAT` in save.c. Fixed-size 20,421 bytes.
All multi-byte values are little-endian.

### 22.0 Format overview

The DOS save format is preserved byte-for-byte by openkb. The
format dates from the early 1990s and has these characteristics:

- Fixed total size: 20,421 bytes (uncompressed, plain binary).
- Field-by-field little-endian serialization.
- 1-bit-per-tile fog-of-war packing (saves 14 KB vs 1-byte format).
- 1-byte-per-foe-count packing (truncates word values).
- XOR-encrypted scepter coords (1-byte XOR with key).
- Extension `.DAT`, all-uppercase filename.
- 3 reserved "unknown" bytes preserved verbatim (forward-
  compatibility?).

The save format is NOT versioned. A clean-room implementation
can either keep this format for compat with vanilla openkb saves,
or define a new format (and lose compat).

### 22.0.1 Read/write strategy

`KB_loadDAT` reads the full 20421 bytes into a buffer:
```c
char buf[DAT_SIZE]
n = fread(buf, 1, DAT_SIZE, f)
if n != DAT_SIZE: return NULL
```

Then walks a `char *p = &buf[0]` pointer through, calling
`READ_BYTE(p)`, `READ_WORD(p)`, `READ_DWORD(p)` for each field.
The pointer auto-advances. Final check: `p - &buf[0] == DAT_SIZE`.

`KB_saveDAT` does the inverse: builds the buffer field by field,
writes it to disk via `fwrite(buf, 1, DAT_SIZE, f)`.

Note: openkb doesn't validate field values on load. A corrupt
save (e.g. continent=10) will be loaded as-is and likely crash
later. A defensive implementation should clamp on read.

### 22.1 Header / character info

| Offset | Size  | Field                                    |
|--------|-------|------------------------------------------|
| 0x0000 | 10    | Player name, ASCII, padded with spaces   |
| 0x000A | 1     | padding (part of 11-byte alignment)      |
| 0x000B | 1     | `class`                                  |
| 0x000C | 1     | `rank`                                   |
| 0x000D | 1     | `spell_power`                            |
| 0x000E | 1     | `max_spells`                             |

(Reader strips trailing spaces from name[0..9] then advances 11
bytes as a unit — the 11th byte is the null spacer.)

### 22.2 Quest progress

| Offset | Size  | Field                                             |
|--------|-------|---------------------------------------------------|
| 0x000F | 17    | `villain_caught[17]` bytes                        |
| 0x0020 | 8     | `artifact_found[8]` bytes                         |
| 0x0028 | 4     | `continent_found[4]` bytes                        |
| 0x002C | 4     | `orb_found[4]` bytes                              |

### 22.3 Spellbook

| Offset | Size  | Field                                             |
|--------|-------|---------------------------------------------------|
| 0x0030 | 14    | `spells[14]` bytes                                |

### 22.4 More character

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x003E | 1    | `knows_magic`                                     |
| 0x003F | 1    | `siege_weapons`                                   |
| 0x0040 | 1    | `contract`                                        |
| 0x0041 | 5    | `player_troops[5]`                                |
| 0x0046 | 1    | `options[0]` — delay                              |
| 0x0047 | 1    | `difficulty`                                      |
| 0x0048 | 1    | `options[1]` — sounds                             |
| 0x0049 | 1    | `options[2]` — walk beep                          |
| 0x004A | 1    | `options[3]` — animation                          |
| 0x004B | 1    | `options[4]` — army size                          |

### 22.5 Location / mount

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x004C | 1    | `continent`                                       |
| 0x004D | 1    | `x`                                               |
| 0x004E | 1    | `y`                                               |
| 0x004F | 1    | `last_x`                                          |
| 0x0050 | 1    | `last_y`                                          |
| 0x0051 | 1    | `boat_x`                                          |
| 0x0052 | 1    | `boat_y`                                          |
| 0x0053 | 1    | `boat` (continent or 0xFF)                        |
| 0x0054 | 1    | `mount` (0/4/8)                                   |
| 0x0055 | 1    | `options[5]` — CGA palette                        |

### 22.6 Town spells / contracts / steps

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x0056 | 26   | `town_spell[26]` bytes                            |
| 0x0070 | 5    | `contract_cycle[5]`                               |
| 0x0075 | 1    | `last_contract`                                   |
| 0x0076 | 1    | `max_contract`                                    |
| 0x0077 | 1    | `steps_left`                                      |
| 0x0078 | 1    | `unknown3`                                        |

### 22.7 Castles / towns

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x0079 | 26   | `castle_owner[26]`                                |
| 0x0093 | 26   | `castle_visited[26]`                              |
| 0x00AD | 26   | `town_visited[26]`                                |

### 22.8 Scepter (encrypted)

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x00C7 | 1    | `scepter_continent XOR scepter_key`               |
| 0x00C8 | 1    | `scepter_x XOR scepter_key`                       |
| 0x00C9 | 1    | `scepter_y XOR scepter_key`                       |

Key itself is written later (see 22.16).

### 22.9 Fog of war

`fog[4][64][64]` stored as bit-per-tile. Each row of 64 tiles
becomes 8 bytes; rows are written top-to-bottom. Within each byte,
**bit 0 of x+0** (leftmost) goes to **bit 7 of the stored byte**;
i.e. bit k of byte at column group x/8 is `fog[row][x+7-k]`.

Total size: `4 × 64 × 8 = 2048` bytes.

| Offset | Size  | Field                                            |
|--------|-------|--------------------------------------------------|
| 0x00CA | 2048  | `fog[4][64][8]` packed                           |

### 22.10 Castle garrison troops

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x08CA | 130  | `castle_troops[26][5]`                            |

### 22.11 Friendly foe coords

`foe_coords[4][0..4][2]` — 5 friendly foes per continent, each x+y.
Size: `4 × 5 × 2 = 40` bytes.

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x094C | 40   | friendly foe coords                               |

### 22.12 Navmap chests

`map_coords[3][2]` — only first 3 continents (cont 3 has no
navmap because there's no cont 4 to unlock).

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x0974 | 6    | navmap chests (x,y)×3                             |

### 22.13 Orb chests and teleport caves

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x097A | 8    | `orb_coords[4][2]`                                |
| 0x0982 | 16   | `teleport_coords[4][2][2]`                        |

### 22.14 Dwelling locations

`dwelling_coords[4][11][2]` — 4 × 11 × 2 = 88 bytes.

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x0992 | 88   | dwelling coords                                   |

### 22.15 Hostile foe data

Hostile foes are foe slots [FRIENDLY_FOES..MAX_FOES) = [5..40),
35 per continent × 4 continents = 140 entries.

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x09EA | 280  | hostile foe coords (x,y)×140                      |
| 0x0B02 | 420  | hostile foe troops [4][35][3]                     |
| 0x0CA6 | 420  | hostile foe numbers [4][35][3] **1-byte per**     |

**WARNING:** foe_numbers are `word` in memory but stored as **1 byte
each** in the save file. This is a deliberate truncation in openkb
save format — foe numbers > 255 are lost on save/load roundtrips.

### 22.16 Dwellings and scepter key

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x0E4A | 44   | `dwelling_troop[4][11]`                           |
| 0x0E76 | 44   | `dwelling_population[4][11]`                      |
| 0x0EA2 | 1    | `scepter_key`                                     |

### 22.17 Word stats

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x0EA3 | 2    | `base_leadership`                                 |
| 0x0EA5 | 2    | `leadership`                                      |
| 0x0EA7 | 2    | `commission`                                      |
| 0x0EA9 | 2    | `followers_killed`                                |
| 0x0EAB | 10   | `player_numbers[5]` word array                    |

### 22.18 Castle garrison numbers (word)

`castle_numbers[26][5]` = 130 words = 260 bytes.

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x0EB5 | 260  | `castle_numbers[26][5]` (word each)               |

### 22.19 Final stats

| Offset | Size | Field                                             |
|--------|------|----------------------------------------------------|
| 0x0FB9 | 2    | `time_stop` (word)                                |
| 0x0FBB | 2    | `days_left` (word)                                |
| 0x0FBD | 2    | `score` (word, unused by engine but preserved)    |
| 0x0FBF | 1    | `unknown1`                                        |
| 0x0FC0 | 1    | `unknown2`                                        |
| 0x0FC1 | 4    | `gold` (dword)                                    |

### 22.20 Map dump

| Offset | Size   | Field                                          |
|--------|--------|------------------------------------------------|
| 0x0FC5 | 16384  | `map[4][64][64]` tile id bytes                 |

Total file size: `0x0FC5 + 16384 = 20421` (matches `DAT_SIZE` in
save.c:23).

**Integrity check:** on load, openkb verifies exact file size and
warns if the byte position after parsing doesn't equal DAT_SIZE.

### 22.21 Scepter XOR encryption

On save (save.c:378):
```
write_byte(p, scepter_continent ^ scepter_key)
write_byte(p, scepter_x ^ scepter_key)
write_byte(p, scepter_y ^ scepter_key)
...
write_byte(p, scepter_key)
```

On load, the three encoded bytes are stored in `game->scepter_*`
as-is; later the key is read; then:
```
scepter_continent ^= scepter_key
scepter_x ^= scepter_key
scepter_y ^= scepter_key
```

### 22.22 Savefile discovery

`load_game()` (game.c:526) walks `conf->save_dir` via `KB_opendir`;
entries whose extension equals "DAT" (case-insensitive) are
listed by basename. Up to some reasonable count displays:
numbered 1..N in a minimenu.

The game.c `save_game` writes to `<savefile>2` (leaving the
original intact). This "2" suffix is explicitly called out in
source as "TODO: Remove this '2', it's for debug purposes only."

### 22.23 Fog-bit packing/unpacking

Fog of war is stored as **1 bit per tile** to save space:

```
For each continent c (0..3):
  For each row y (0..63):
    For each col x in groups of 8 (x = 0, 8, 16, ..., 56):
      one_byte = 0
      For each k in 0..7:
        one_byte |= (game->fog[c][y][x + 7 - k] & 0x01) << k
      Write byte to disk
```

So byte k of the disk format contains:
- bit 0: fog[c][y][x + 7]
- bit 1: fog[c][y][x + 6]
- ...
- bit 7: fog[c][y][x + 0]

I.e., LSB-to-MSB indexing flips the x within each group.

On read:
```
test_bits = READ_BYTE(p)
For k in 0..7:
  one_bit = ((test_bits & (0x01 << k)) >> k) & 0x01
  game->fog[c][y][x + 7 - k] = one_bit
```

Total fog bytes: 4 × 64 × 8 = 2048.

### 22.24 XOR scepter encryption

The scepter coordinates are encrypted with a 1-byte XOR key:

On save (save.c:378):
```c
write_byte(p, scepter_continent ^ scepter_key)
write_byte(p, scepter_x ^ scepter_key)
write_byte(p, scepter_y ^ scepter_key)
... [other fields] ...
write_byte(p, scepter_key)
```

On load (save.c:232):
```c
... [read other fields] ...
scepter_key = READ_BYTE(p)
scepter_continent ^= scepter_key
scepter_x ^= scepter_key
scepter_y ^= scepter_key
```

Why XOR encryption? Probably to prevent casual save-file editing
to learn the scepter location. A determined hacker can still
brute-force all 256 keys in microseconds — the XOR is a token
gesture only.

Key generation: `KB_rand(0x00, 0xFF)` at game spawn. Same key
used for all 3 coord bytes. A clean-room implementation can use
plain bytes (no XOR) for simplicity, breaking the "secrecy" but
keeping save compatibility-friendly format.

### 22.25 Foe count truncation

Foe counts (`foe_numbers[c][f][i]`) are stored in memory as
`word` (16-bit) but on disk as `byte` (8-bit). So values > 255
lose their high byte on save/load roundtrip.

```c
// On save:
WRITE_BYTE(p, game->foe_numbers[n][k][0])   // truncated!
```

Why? Probably because foe encounters typically have <100 troops
per stack, and truncation rarely matters. But end_week growth
can push counts above 255 (especially Dragons or other low-
growth troops over many weeks). Such values silently truncate.

A clean-room implementation should EITHER preserve word width
(breaking format) OR clamp to 255 on save (for compat).

### 22.26 Validation strategy for clean-room

On load, validate:

1. File size: must be exactly 20421 bytes.
2. `class < 4`, `rank < 4`, `difficulty < 4`, `mount in {0,4,8}`.
3. `continent < 4`, `0 <= x, y < 64`.
4. `boat in {0..3, 0xFF}`.
5. `contract in {0..16, 0xFF}`.
6. For each villain_caught[i]: must be 0 or 1.
7. For each artifact_found[i]: 0 or 1.
8. For each player_troops[i]: must be `0xFF` (empty) or
   `< MAX_TROOPS = 25`.
9. For each castle_owner[i]: in {0..16, 0x40..0x50, 0x7F, 0xFF}.
10. days_left <= max for difficulty.
11. steps_left <= 40.
12. sum(spells) <= max_spells.
13. After XOR decryption: scepter_continent < 4, scepter_x,
    scepter_y < 64.

Reject (or clamp) on violation. openkb does no such checks.

### 22.27 Backward/forward compatibility

The format has three "unknown" bytes (`unknown1, unknown2,
unknown3`) preserved verbatim. These could be used for:
- Format version tag.
- New fields without breaking old saves.
- Padding for future extensions.

A clean-room implementation can claim these for its own use
(e.g. format version) but must preserve them for compat with
vanilla openkb saves.

### 22.28 Save file path convention

Save filename: uppercase player name + ".DAT".

```c
KB_strcpy(game->savefile, name)
KB_strcat(game->savefile, ".DAT")
KB_strtoupper(game->savefile)
```

So a player named "Dan" gets save file "DAN.DAT". Files are
saved in `conf->save_dir`.

The save_game function in game.c:6191 writes to
`<save_dir>/<savefile>2` (with trailing "2" for debug).
This is a leftover; actual deployment should write to
`<save_dir>/<savefile>` (no suffix).

### 22.29 Pseudocode for clean-room reimplementation

```python
def save_game(filename, game):
    buf = bytearray(20421)
    p = 0

    def w_byte(v):  buf[p] = v & 0xFF; p += 1
    def w_word(v):  buf[p] = v & 0xFF; buf[p+1] = (v >> 8) & 0xFF; p += 2
    def w_dword(v): buf[p:p+4] = struct.pack('<I', v); p += 4

    # Name (10 bytes + 1 padding)
    name = game.name.ljust(10)[:10]
    buf[p:p+10] = name.encode('ascii')
    p += 11

    w_byte(game.cclass)
    w_byte(game.rank)
    w_byte(game.spell_power)
    w_byte(game.max_spells)
    for i in range(17): w_byte(game.villain_caught[i])
    for i in range(8):  w_byte(game.artifact_found[i])
    for i in range(4):  w_byte(game.continent_found[i])
    for i in range(4):  w_byte(game.orb_found[i])
    for i in range(14): w_byte(game.spells[i])
    w_byte(game.knows_magic)
    w_byte(game.siege_weapons)
    w_byte(game.contract)
    for i in range(5): w_byte(game.player_troops[i])
    w_byte(game.options[0])     # delay
    w_byte(game.difficulty)
    for i in range(1, 5): w_byte(game.options[i])    # sounds, walk beep, animation, army size
    w_byte(game.continent)
    w_byte(game.x)
    w_byte(game.y)
    w_byte(game.last_x)
    w_byte(game.last_y)
    w_byte(game.boat_x)
    w_byte(game.boat_y)
    w_byte(game.boat)
    w_byte(game.mount)
    w_byte(game.options[5])     # CGA palette
    for i in range(26): w_byte(game.town_spell[i])
    for i in range(5): w_byte(game.contract_cycle[i])
    w_byte(game.last_contract)
    w_byte(game.max_contract)
    w_byte(game.steps_left)
    w_byte(game.unknown3)
    for i in range(26): w_byte(game.castle_owner[i])
    for i in range(26): w_byte(game.castle_visited[i])
    for i in range(26): w_byte(game.town_visited[i])
    # Scepter (XOR encrypted)
    w_byte(game.scepter_continent ^ game.scepter_key)
    w_byte(game.scepter_x ^ game.scepter_key)
    w_byte(game.scepter_y ^ game.scepter_key)
    # Fog (1 bit per tile)
    for c in range(4):
        for y in range(64):
            for x in range(0, 64, 8):
                byte_val = 0
                for k in range(8):
                    byte_val |= (game.fog[c][y][x + 7 - k] & 0x01) << k
                w_byte(byte_val)
    for n in range(26):
        for k in range(5): w_byte(game.castle_troops[n][k])
    for n in range(4):
        for k in range(5):
            w_byte(game.foe_coords[n][k][0])
            w_byte(game.foe_coords[n][k][1])
    for n in range(3):  # only 3 continents have navmaps
        w_byte(game.map_coords[n][0])
        w_byte(game.map_coords[n][1])
    for n in range(4):
        w_byte(game.orb_coords[n][0])
        w_byte(game.orb_coords[n][1])
    for n in range(4):
        for k in range(2):
            w_byte(game.teleport_coords[n][k][0])
            w_byte(game.teleport_coords[n][k][1])
    for n in range(4):
        for k in range(11):
            w_byte(game.dwelling_coords[n][k][0])
            w_byte(game.dwelling_coords[n][k][1])
    # Hostile foes (slots 5..39)
    for n in range(4):
        for k in range(5, 40):
            w_byte(game.foe_coords[n][k][0])
            w_byte(game.foe_coords[n][k][1])
    for n in range(4):
        for k in range(5, 40):
            w_byte(game.foe_troops[n][k][0])
            w_byte(game.foe_troops[n][k][1])
            w_byte(game.foe_troops[n][k][2])
    for n in range(4):
        for k in range(5, 40):
            w_byte(game.foe_numbers[n][k][0])  # truncate to byte!
            w_byte(game.foe_numbers[n][k][1])
            w_byte(game.foe_numbers[n][k][2])
    for n in range(4):
        for k in range(11): w_byte(game.dwelling_troop[n][k])
    for n in range(4):
        for k in range(11): w_byte(game.dwelling_population[n][k])
    w_byte(game.scepter_key)
    w_word(game.base_leadership)
    w_word(game.leadership)
    w_word(game.commission)
    w_word(game.followers_killed)
    for i in range(5): w_word(game.player_numbers[i])
    for n in range(26):
        for k in range(5): w_word(game.castle_numbers[n][k])
    w_word(game.time_stop)
    w_word(game.days_left)
    w_word(game.score)
    w_byte(game.unknown1)
    w_byte(game.unknown2)
    w_dword(game.gold)
    # Map dump (raw)
    for c in range(4):
        for y in range(64):
            for x in range(64):
                buf[p] = game.map[c][y][x]
                p += 1

    assert p == 20421

    with open(filename, 'wb') as f:
        f.write(buf)
```

## 23. UI — screens, menus, hotspots

The UI is built around two abstractions: **gamestates** (event
input maps) and **frames** (drawing regions). All input is
event-driven via `KB_event(state)`; rendering is direct via
SDL surface blits.

### 23.0 Screen flow

```
KB_startENV (SDL setup)
  → display_logo (any key)
  → display_title (any key)
  → select_game (A-D for new, L for load)
      → if A-D: create_game (name + difficulty) → spawn_game
      → if L: load_game (file picker) → KB_loadDAT
  → adventure_loop (main game)
    → various menus, screens via gamestates
  → exit (free, KB_stopENV)
```

Most screens use a similar pattern:
1. Draw the screen.
2. Loop: read event, handle event, redraw.
3. On Esc/done: exit screen, return value.

### 23.1 Gamestate and hotspot system

`ui.h` defines `KBgamestate` as a fixed-size array of up to 64
`KBhotspot` entries + a `hover` index. Each hotspot encodes a
rectangle (`coords`), a key binding (`hot_key`, `hot_mod`),
and a flag byte:

```
KFLAG_ANYKEY    = 0x01   // match any key
KFLAG_RETKEY    = 0x02   // return the key instead of spot index+1
KFLAG_TRAPSIGNAL= 0x08   // intercept SDL_QUIT (e.g., window close)
KFLAG_TIMER     = 0x10   // timed fire based on SDL_GetTicks
KFLAG_TIMEKEY   = 0x20   // timed fire only while key held
KFLAG_GRID      = 0x80   // grid of cells (x/cell_w, y/cell_h)

KFLAG_SOFTKEY   = (TIMER | TIMEKEY)
SOFT_WAIT = 150 ms
SHORT_WAIT = 50 ms

SDLK_SYN   = 0x16        // fake "sync tick" keycode
SDLK_CLICK = 0x01        // fake "grid click" keycode
```

`KB_event(&state)` (ui.c:245): polls SDL, advances timers, returns:

- Spot index + 1 (0 means no event).
- Or `hot_key` value if `KFLAG_RETKEY` set (so text input returns
  ASCII, etc.).
- 0xFE on SDL_QUIT handled via trap.
- 0xFF on Esc or unhandled SDL_QUIT.

Shift conversion is handled inside `KB_event` for RETKEY events:
alpha → uppercase, number → symbol (2 → @, 6 → ^, etc.). See
ui.c:344-380 for full table.

`KB_reset(state)` clears timer state and keyboard-held bits.

`KB_imenu(state, id, cols)` updates a hotspot's coords to the
current text cursor position (`sys->cursor_x`, `sys->cursor_y`),
sized `cols * font.w × font.h`. Used to hot-link in-line menu
items.

### 23.2 Screen regions (sidebar layout)

`KBsession` struct (ui.h) stores the layout: frames, map rect,
status rect, side rect, cached per-tile rects, flip flags, color
schemes. Computed from `RECT_UI`, `RECT_TILE`, `RECT_UITILE`
(see §27).

Default DOS layout (dos-data.c):

```
DOS_frame_ui[5] = {
  { 0, 0, 320, 8 },       // top
  { 0, 0,  16, 200 },     // left
  { 300, 0, 16, 200 },    // right
  { 0, 0, 320, 8 },       // bottom
  { 0, 17, 280, 5 },      // bar (below status)
};
DOS_frame_map  = { 16, 21, 240, 170 };    // 48*5=240, 34*5=170
DOS_frame_tile = { 0, 0, 48, 34 };
```

320×200 original resolution. When `conf->filter` > 0, the window
is 640×400 (2× zoom). Fullscreen 2× also adds 40px top pan
(`pan=40; height=480`).

### 23.3 Adventure screen

`draw_map(game, tick)`:

- `perim_w = map.h / tile.h = 5`, `perim_h = map.w / tile.w = 5`.
- `radii_w = (perim_w-1)/2 = 2`, `radii_h = (perim_h-1)/2 = 2`.
- Blit 5×5 tiles centered on `(game->x, game->y)` (clamped to
  deep water outside map bounds).
- Boat: if boat is on this continent and not currently sailing,
  blit boat sprite at its map position (hero frame 0).

`draw_player(game, frame)`:

- Blit hero sprite at the center tile (2,2 local). Source frame
  is `tile.w * (mount + frame)` with row 0 or 1 (flipped if
  `local.hero_flip`).

`draw_sidebar(game, tick)`:

- Face/contract tile: if contract != 0xFF, blit the contracted
  villain portrait with animation frame = tick; else blit empty
  contract tile.
- Siege weapons tile: animated with tick if owned, else "no weapons"
  tile.
- Magic star tile: animated with tick if knows_magic, else "no
  magic" tile.
- Puzzle map 5×5 grid, with pieces blit over unsolved cells.
- Gold purse with stacked coins:
  `cval[0] = gold / 10000`, `cval[1] = (gold - cval[0]*10000) / 1000`,
  `cval[2] = ... / 100`; each value is stacked coins of a specific
  frame in `GR_COINS`.

`draw_statusbar(game)`:

```
  " Options / Controls / Days Left:<N> "
```

If `time_stop > 0`:

```
  " Options / Controls / Time Stop:<N> "
```

### 23.4 Combat statusbar

```
  " Options / <TroopName> [F,]M<N>[,S<N>] "
```

Where F appears for flyers, M<N> is moves left, S<N> appears for
ranged units (shots left). Only shown on player's turn and when
the active unit is under control; otherwise the status bar is
blank (filled with background color).

### 23.5 Controls menu

`controls_menu(game, combat)` (game.c:5276). Items:

```
Delay        0 1 2 3 4 5 6 7 8 9     (numeric)
Sounds       On / Off                (bool)
Walk Beep    On / Off                (bool)
Animation    On / Off                (bool)
Army Size    On / Off                (bool)
CGA          0 1 2 3 4 5 6 7         (numeric, only in CGA builds)
```

`DAT_MENUCONTROLS` provides a 6-byte visibility mask per module.
In DOS module, CGA is shown only if `mod->bpp == 2`.

Toggling CGA palette (index 5) triggers `SDL_ReplaceColors` over
the whole screen (swaps old CGA RGB pairs for new ones) then
refreshes UI colors and frees cached surfaces.

Clicking arrow-left or pressing 'c' while in controls menu returns
to options menu; vice versa. Esc exits menu.

### 23.6 Options menu

`options_menu(game)` (game.c:5144) — full list of 19 entries:

```
Move Down       Combat View Army       Adventure Fly
Move Left       Combat Controls        Adventure Land
Move Right      (Combat Fly)           Adventure Contract Info
Move Up         (Combat Give Up)       Adventure Auto-mapping
Down Left       (Combat Shoot)         Adventure Puzzle Solve
Down Right      (Combat Use Magic)     Adventure Search Area
Up Left         (Combat View Char)     Adventure Use Magic
Up Right        (Combat Wait)          Adventure View Character
                (Combat Pass)          Adventure Wait End Week
                                       Adventure Quit and Save
```

Adventure version omits "Rest" (key 5). Fly/Land/Navigate are
shown only based on current mount state. Each row shows the
keybind glyph (via `KB_KeyLabel`) + the action label.

### 23.7 New-game screen (`create_game`)

`create_game(pclass)` (game.c:417):

- 30×12 char framed box centered on screen.
- Title row: " <ClassTitle>  Name: ".
- Name entry via `text_input(10, numbers_only=0, ...)` — blinking
  "|/-\" cursor (code points 0x1D 0x05 0x1F 0x1C).
- Below name: difficulty table:

```
   Difficulty   Days  Score

   Easy          900   x.5
   Normal        600    x1
   Hard          400    x2
   Impossible?   200    x4
```

- Up/Down to move selection arrow `>`.
- Enter: load `DAT_WORLD`, call `refill_rules()` to pick up any
  module-provided overrides, `refill_names()` for strings, then
  `spawn_game(name, pclass, selection, land)`, `furnish_map(game)`.

### 23.8 Load-game screen

`load_game()` (game.c:526):

- 14-char-wide menu, N entries:

```
 Select game:

   Overlord

1. <FILE1>
2. <FILE2>
...
```

(The "Overlord" fixed label is the DOS-era class name text leftover.)

- Up/Down or 1..9 to select; Enter to load via `KB_loadDAT`.
- Status bar: " 'ESC' to exit ↑↓ Return to Select  ".

If no .DAT files found: `KB_MessageBox` with "This disk has no
characters on it. Try creating a new character or copy one from
another disk."

### 23.9 Select-game screen

`select_game(conf)` (game.c:708):

- Blit `GR_SELECT, 0` (title). Overlay "Select Char A-D or L-Load
  saved game" in status.
- Keys A/B/C/D → `create_game(key-1)`. L → `load_game()`.
- First entry: `show_credits()` — renders full credits (from
  KB.EXE `DOS_CREDITS` offset) in a message box with a user
  picture (`GR_SELECT, 2`) to the right.

After selection:
- If created: `"<Name> the <Class>, A new game is being created.
  Please wait while I perform godlike actions to make this game
  playable."`
- If loaded: `"<Name> the <Class>, Please wait while I prepare a
  suitable environment for your bountying enjoyment!"`

### 23.10 Module selection

`select_module()` (game.c:782) — shown only if multiple modules
were discovered or configured. List of module names with highlight
on selected one. Up/Down or 1..9/0/- to pick; Enter to confirm.
Rendered on a gray (0x333333) background. Module order is
discovery order: Free (GNU), then DOS variants (VGA > EGA > CGA >
Hercules), then MegaDrive.

### 23.11 Logo and title

- `display_logo()` — blits `GR_LOGO` centered on `0xFF3366`
  background. Any key continues.
- `display_title()` — blits `GR_TITLE` centered on black.

### 23.12 Character view

`view_character(game)` (game.c:1678):

Portrait left (`GR_PORTRAIT, class`). Stats right:

```
<Name> the <Title>
Leadership         <leadership>
Commission/Week    <commission>
Gold               <gold>
Spell power        <spell_power>
Max # of spells    <max_spells>
Villains caught    <captured>
Artifacts found    <artifacts>
Castles garrisoned <castles>
Followers killed   <followers_killed>
Current score      <score>
```

Below: 2 rows of artifacts (4 visible slots in row 0, row 1).
To the right: 2 columns of map tokens (continent 0..3 navmaps).

Empty slots are drawn with `EMPTY_SLOT` graphic (at position
`MAX_ARTIFACTS + MAX_CONTINENTS = 12`); empty maps with
`EMPTY_MAP` (position 13).

### 23.13 Army view

`view_army(game)` (game.c:1798):

- 5 rows, each with a troop tile on the left and stats on the right.
- Row is empty (Dwelling color background + empty cells) if slot
  is 0xFF.
- Animated: tick cycles 0..3; troop frame advances.

Per-row stats:
```
 <Count> <TroopName>
 SL:<N> MV:<N>
 Morale:<Name>      or "Out of Control"
                           HitPts:<hp * count>
                           Damage:<min*count>-<max*count>
                           G-Cost:<recruit/10 * count>
```

Background color per row is the troop's dwelling color
(`COL_DWELLING`).

### 23.14 Text input cursor

`text_input(max_len, numbers_only, x, y, loc_id, troop_id)`:

- Draws previous entry + blinking twirl char at cursor.
- Twirl sequence: `"\x1D" "\x05" "\x1F" "\x1C"` (glyphs for |,/,-,\).
- If loc_id != 0xFF: re-renders background via `draw_location`.
- Enter finishes; Backspace deletes. Numbers-only filters via
  `isdigit`. Max length caps input.

### 23.15 KB_event input flow

`KB_event(state)` (ui.c:245) is the central input pump:

1. Update `now = SDL_GetTicks()`.
2. Compute `passed = now - state->last`.
3. Fire any timed hotspots whose timer expired (KFLAG_TIMER).
4. Poll SDL events:
   - SDL_MOUSEMOTION: track mouse position.
   - SDL_QUIT: trap or return 0xFF.
   - SDL_KEYDOWN: match against hotspot key.
   - SDL_KEYUP: clear key state.
   - SDL_MOUSEBUTTONUP: register click.
5. If a key matches, return the hotspot index+1 OR the key
   (RETKEY flag).
6. If mouse over a hotspot, set `state->hover`.
7. If clicked on hover, return that hotspot.
8. Else return 0 (no event).

Special return values:
- 0: no event (timeout or non-matching).
- 0xFF: Escape pressed or unhandled SDL_QUIT.
- 0xFE: SDL_QUIT trapped by KFLAG_TRAPSIGNAL.
- 1..N: hotspot index + 1 (1-based).
- ASCII keycode: if RETKEY flag set.

### 23.16 Frame drawing primitives

UI rendering uses these helpers:

- `KB_TopBox(flag, fmt, ...)`: prints to status bar (top of
  screen). Status bar is colored per difficulty.
- `KB_BottomBox(header, body, flag)`: full bottom-frame menu
  (~30 chars × 8 rows) for dialogs.
- `KB_BottomFrame()`: returns the bottom-frame rect; caller
  fills text directly.
- `KB_MessageBox(str, flag)`: centered message box (28×N text).
  Auto-sizes to content.
- `SDL_TextRect(dest, rect, fore, back, top)`: draws a framed
  rectangle with corner glyphs.

Flags (MSG_*):
- 0x01 CENTERED — center text in box width.
- 0x02 RIGHT — right-justify.
- 0x04 PADDED — leave 1-line padding.
- 0x10 HARDCODED — use 30-char width (vs 28).
- 0x40 WAIT — call KB_Wait after.
- 0x80 PAUSE — call KB_Pause after.

`KB_Wait()` waits ~1.5 seconds OR a keypress.
`KB_Pause()` waits indefinitely for any keypress.

### 23.17 Color scheme application

Each screen has a "color scheme" loaded via `KB_Resolve(COL_TEXT,
CS_*)`. Scheme indices:

- CS_GENERIC (0): default message box (dark blue / white).
- CS_STATUS_1..5 (1..5): status bar by difficulty.
- CS_MINIMENU (6): savefile picker.
- CS_VIEWCHAR (7): character view.
- CS_VIEWARMY (8): army view.
- CS_MINIMAP (9): minimap.
- CS_CHROME (10): black/white menu (options/controls).
- CS_ENDING (11): win/lose screen.

`local.message_colors` and `local.status_colors` are pre-loaded
at module init. The status bar color is updated when difficulty
changes via `update_ui_colors(game)`.

Each scheme has 18 colors (`COLORS_MAX`), indexed by
`COLOR_BACKGROUND, COLOR_TEXT1..4, COLOR_SHADOW1..2,
COLOR_FRAME1..2, COLOR_SEL_*` (selected variants).

CGA mode uses different schemes via `DOS_CGA_ColorScheme`,
mapped through the active CGA palette.

## 24. Verbatim strings hardcoded in source

Strings that appear literally in game.c / play.c / ui.c source
(rather than loaded from KB.EXE or INI files). These are NOT
module-overridable — they're embedded in the C code.

A clean-room reimplementation must replicate these strings
verbatim for narrative consistency, OR rewrite them (breaking
parity with vanilla openkb prompts).

Format conventions in this section:
- `<Var>` denotes a printf-style substitution.
- Indented blocks are KB_BottomBox or similar text panels.
- `\n` is literal newline.
- `(space)` indicates a "press space to continue" prompt.



### 24.1 Contract view — no contract

```
\n\n\n\n\n\n      You have no Contract!
```

### 24.2 Contract view — has contract

The description text is sourced from `STRL_VDESCS` in KB.EXE (see
§25) — substitutes `%s` for continent and castle name.

### 24.3 Audience with King — arrival

```
Trumpets announce your
arrival with regal fanfare.

King Maximus rises from his
throne to greet you and
proclaims:           (space)
```

### 24.4 King's promotion speech

```


Congratulations <Name>,

I now promote you to
<Title>.
```

### 24.5 King at max rank

```


<Name> the <Title>,

Hurry and recover my Scepter
of Order or all will be
lost!
```

### 24.6 King's low rank message

```


My dear <Name>,

I can aid you better after
you've captured <N> more
villains.
```

### 24.7 Alcove greeting

```
The venerable Archmage,
Aurange, will teach you the
secrets of spell casting for
<5000> gold.

               Accept (y/n)?
```

### 24.8 Alcove no gold

```
The sign said <5000> gold! Why
waste my valuable time when
you know you don't have the
required amount of gold?
Begone until you do!
```

### 24.9 Not enough gold (generic)

```
\n\n\nYou don't have enough gold!
```

### 24.10 Too many spells

```
\n\n   You have learned your
  maximum number of spells.
```

### 24.11 Bought a spell

```
\n\n\nYou can learn <N> more spell<s>.
```

(`s` omitted when N==1.)

### 24.12 No spells

```
You have not been trained in
the art of spellcasting yet.
Visit the Archmage Aurange
in <Continentia> at 11,19 for
this ability.
```

### 24.13 Siege weapons already

```
\n\n\n   You have siege weapons!
```

### 24.14 Troop slot / gold limits

```
No troop slots left!

You cannot garrison your
        last army!

 There are no open slots
or any of this army type!
```

### 24.15 Dismiss last army

```
If you Dismiss your last
army, you will be sent back
to the King in disgrace.
Dismiss last army (y/n)?
```

### 24.16 Castle lay siege (monster)

```
Various groups of monsters
occupy this castle.


            Lay Siege (y/n)?
```

### 24.17 Castle lay siege (villain)

```
<VillainName> and
army occupy this castle.



            Lay Siege (y/n)?
```

### 24.18 Castle gather information

```
Castle <Name> is under
no one's rule.

  <numwords> <Troop>
  ...
```

or:

```
Castle <Name> is under
<VillainName>'s rule.

  <numwords> <Troop>
  ...
```

### 24.19 Foe accept — army full

```
flee in terror at the sight
your vast army.
                     (space)
```

### 24.20 Foe accept — room

```
<numwords> <Troops>,
with desires of greater
glory, wish to join you
               Accept (y/n)?
```

### 24.21 Foe attack scouts

```
Your scouts have sighted:
  <numwords> <Troop>
  ...
               Attack (y/n)?
```

### 24.22 Navmap chest

```
Hidden within an ancient
chest, you find maps and
charts describing passage to
<NextContinent>.
```

### 24.23 Orb chest

```
Peering through a magical
orb you are able to view the
entire continent. Your map
of this area is complete.
```

### 24.24 Gold/leadership chest

```
After scouring the area,
you fall upon a hidden
treasure cache. You may:
A) Take the <N> gold.
B) Distribute the gold to
the peasants, increasing
your leadership by <N>.
```

### 24.25 Commission chest

```
After surveying the area,
you discover that it is
rich in mineral deposits.

The King rewards you for
your find by increasing
your weekly income by <N>
```

### 24.26 Spell power chest

```
Traversing the area, you
stumble upon a time worn
cannister. Curious, you un-
stop the bottle, releasing
a powerful genie who raises
your Spell Power by <N> and
vanishes.
```

### 24.27 Max spells chest

```
A tribe of nomads greet you
and your army warmly. Their
shaman, in awe of your
prowess, teaches you the
secret of his tribe's magic.
Your maximum spell capacity
is increased by <N>
```

### 24.28 New spell chest

```
You have captured a
mischevious imp which has
been terrorizing the
region. In exchange for
its release, you receive:

   <N> <SpellName> spell.
```

### 24.29 Empty chest

```
The chest was empty!
```

### 24.30 Instant army banner

```
<numwords> <Troops>


have joined to your army.
```

### 24.31 Search

```
Search...

It will take <10> days to do a
search of this area.

               Search (y/n)?
```

### 24.32 Futile search

```


Your search of this area has
revealed nothing.
```

### 24.33 Give up

```
Giving up will forfeit your
armies and send you back to
the King. Give up (y/n)?
```

### 24.34 Defeat

```
After being disgraced on the
field of battle, King
Maximus summons you to his
castle. After a lesson in
tactics, he reluctantly re-
issues your commission and
sends you on your way.
```

### 24.35 Victory banner

```
             Victory!

Well done <Name> the <Title>,
you have successfully vanquished
yet another foe.

Spoils of War: <N> gold[ and the
capture of <VillainName>

Since you did not have the proper
contract, the Lord has been set
free.]
[For fulfilling your contract you
receive an additional <N> gold
as bounty... and a piece of the
map to the stolen scepter.]

                     (space)
```

### 24.36 Bridge wrong location

```
Not a suitable location for a bridge
```

```
What a waste of a good spell!
```

### 24.37 Save game saved

```
Your game has been saved.

Press Control-Q to Quit or
any other key to continue.
```

### 24.38 Fast quit

```
 Quit to DOS without saving (y/n) 
```

### 24.39 Week astrology

```
Week #<n>

Astrologers proclaim:
Week of the <Troop>

All <Troop> dwellings are
repopulated.         (space)
```

### 24.40 Week budget

```
Week #<n>             Budget

On Hand <on_hand>
Payment <commission>
Boat    <boat_cost>
Army    <army_cost>
Balance <gold>
```

Columns 14..27 show per-slot troop cost:
`<Troop>% 6d`.

### 24.41 Town menu

```
Town of <TownName>
                    GP=<N>K

A) Get New Contract
B) Rent boat (<cost> week)      OR  Cancel boat rental
C) Gather information
D) <SpellName> spell (<cost>)
E) Buy seige weapons (<3000>)
```

### 24.42 Home castle menu

```
Castle <of King Maximus>

A) Recruit Soldiers
B) Audience with the King
```

### 24.43 Combat logs

- `"%s vs %s, %d die"` — melee hit
- `"%s retaliate, killing %d"` — retaliation
- `"%s shoot %s killing %d"` — ranged hit
- `"%s shoot %s"` (no kills on cancel with "The spell seems to
  have no effect!" follow-up)
- `"%s fly"`
- `"%s move"`
- `"%s wait"`
- `"%s pass"`
- `"%s are frozen"`
- `"%s are out of control!"`
- `"%d %s cloned"`
- `"%s are frozen"` (Freeze cast)
- `"The spell seems to have no effect!"` (Freeze/Magic on IMMUNE)
- `"Only 1 spell per round!"` (spell in combat when spells already 1)
- `"Press 'ESC' to exit"`
- `"Select your army to Clone"`, `"Select enemy army to Freeze"`,
  `"Select army to Resurrect"`, `"Select enemy army to <SpellName>"`,
  `"Select army to Teleport"`, `"Select new location"`
- `"Can't Shoot"`, `"Can't Fly"`

### 24.44 Fast-quit "debug command?"

```
Debug command (A-Z) ?
```

### 24.45 Assorted debug output

Emitted via `printf` from within `debug_cheat_menu` and elsewhere:

- `Hello there! <...>` — multiplayer hello handshake (combat.c)
- `Seed: `, `Add which troop (A-Z) ?`, `<Troop>,\n How many? (MAX) `
- `"ATTACK!"` — multiplayer combat annotation

### 24.46 String localization considerations

All hardcoded strings in §24 are English. To localize:

1. Identify every hardcoded string in source.
2. Extract to a string table indexed by id.
3. Replace `KB_iprint("...")` calls with
   `KB_iprint(translate(STR_ID))`.
4. Provide localized files (e.g. `strings.de.po`).

openkb does not support localization. A clean-room
implementation should design for it from day 1.

### 24.47 String formatting

Most strings use printf-style:
- `%s` for player name, class title, troop name, etc.
- `%d` for numeric values (counts, gold, etc.).
- `%c` for character codes (twirl glyphs, letters).
- `%-Ns` for left-justify with padding.
- `% Nd` for right-justify number.

Width specifiers like `%-11s` are common in menu layouts to
align columns.

### 24.48 Multi-line string handling

Strings with `\n` are split into lines by KB_iprint, which
respects line breaks but doesn't word-wrap (lines exceeding
the box width are truncated visually).

The KB_BottomBox and KB_MessageBox helpers do auto-wrap on
spaces, breaking lines at `max_w - 3` characters when needed.

### 24.49 Right-justified numerics

Stats display uses right-justification:
```
KB_iprintf("%-3d %s\n", count, name)    # 3-digit count, name follows
KB_iprintf("Cost=% 3d each.")            # 3-digit cost, space-padded
```

Common widths:
- Counts: `%3d` (e.g. "100")
- Gold: `%5d` (e.g. " 5000")
- HP/Damage: variable

### 24.50 Status bar formatting

```
" Options / Controls / Days Left:%d "
" Options / Controls / Time Stop:%d "
```

Note the leading and trailing space — for visual padding
within the status bar's colored band. Not part of the literal
display text; rendered with the active status_colors scheme.

## 25. Strings sourced from KB.EXE (DOS)

The DOS module (dos-data.c) fetches these dynamic strings from
specific byte offsets in `KB.EXE`. Strings are loaded as
`\0`-separated lists ending with 0xFF; reformatted for display.

These are licensable original-game content. A clean-room
reimplementation has 3 options for these strings:
1. Original DOS module path: read from user's KB.EXE.
2. GNU/Free module path: store in INI/text files.
3. Custom: ship original strings under a free license.

The byte-offset contract for path 1 is the table below. Two exe
versions are supported: KB90 (pre-1995) and KB95 (~1995). Offsets
differ by +0x100..0x1C0:

```
enum DOS_offset_names {
    DATA_SEGMENT,
    DOS_CREDITS,
    DOS_GAME_LOST,
    DOS_GAME_WON,
    DOS_SIGNS,
    DOS_VNAMES,
    DOS_VDESCS,
    DOS_ANAMES,
    DOS_ADESCS,
    TUNE_NOTES,
    TUNE_DELAY,
    TUNE_PTR,
    DOS_VREWARDS,
};
int DOS_exe_offsets[][4] = {
    /* KB90    len     KB95       len */
    { 0x15690, 0,      0x15850,   0      },  // DATA SEGMENT
    { 0x15E6D, 0xCB,   0x16031,   0xCE   },  // CREDITS
    { 0x1AF1D, 0x130,  0x1B0E3,   0x130  },  // GAME LOST
    { 0x1ADEE, 0x12F,  0x1AFB4,   0x12F  },  // GAME WON
    { 0x19005, 0x64A,  0x191CB,   0x64A  },  // SIGNS
    { 0x18EDF, 0x126,  0x190A5,   0x126  },  // VNAMES
    { 0x16D19, 0x1548, 0x16EDF,   0x1548 },  // VDESCS
    { 0,       0,      0,         0      },  // ANAMES (not wired)
    { 0x19650, 0x351,  0x19816,   0x351  },  // ADESCS
    { 0x189D1, 0,      0x18B97,   0      },  // TUNE_NOTES
    { 0x18A81, 0,      0x18C47,   0      },  // TUNE_DELAY
    { 0x18AA1, 0,      0x18C67,   0      },  // TUNE_PTR
    { 0x1873A, 0,      0x18900,   0      },  // VREWARDS
};
```

KB95 exe has `KBEXE95_FILESIZE = 113718` bytes (decompressed). KB90
is ~79839 bytes (see kbauto.c fingerprint).

### 25.1 Loading pattern

`DOS_read_strings(mod, off, endoff)` reads raw bytes from
`[off, endoff)`, replaces the last byte with `0xFF` (strlist
terminator), and returns as an asciiz-list (strings separated by
`\0`, terminated by `0xFF`).

For SIGNS: every other `\0` is converted to `\n` (so 2-line signs
become single entries).

For CREDITS (`DOS_read_credits`): parses copyright/titles/names
and re-layouts with centered/tabbed lines. Names indented 2
spaces; ":"-ended titles left-aligned; "copyright" centered.

For VDESCS (`DOS_read_vdescs`): 14 lines per villain; specific
line offsets (from `line_offsets[] = {7,7,7,7,10,0,...}`) are
prepended with spaces for visual formatting. Lines starting with
"last seen:" or "castle:" get ` %s` appended so they format as
printf-targets for continent/castle names later.

For ADESCS (`DOS_read_adescs`): 5 lines per artifact.

### 25.2 Villain reward table

```c
case WDAT_VREWARD:
    return DOS_word_array(mod, KBEXE_OFFSET(exe_type, DOS_VREWARDS), 17);
```

Returns 17 LE words starting at offset `DOS_VREWARDS`. These
override the hardcoded `villain_rewards[]` from §7.1 when present.

### 25.3 Tune palettes

`DOS_ReadTunes(mod)`:

1. Load `tunGroup` from `TUNE_PTR` — 10 word offsets.
2. Load `tunPalette` from `TUNE_NOTES` — 88 frequency words +
   16 duration words.
3. For each of 10 tunes: seek to `DATA_SEGMENT + offset`, read
   up to 255 (note, delay) pairs terminated by 0xFF. Copy the
   palette into each tune for playback.

Tunes enum (`kbres.h`):

```
TUNE_WALK    = 0
TUNE_BUMP    = 1
TUNE_CHEST   = 5
TUNE_DEFEAT  = 7
```

Other 7 tunes are unnamed in code (alarm/fanfare/etc, ids 2,3,4,6,8,9).

### 25.4 MCGA palette

DOS VGA module loads `MCGA.DRV` file (from SlotB, typically
`256.CC#MCGA.DRV` — an archived file within a CC group). Reads
from offset `MCGA_PALETTE_OFFSET = 0x032D` a 768-byte VGA palette
(256 × {r,g,b} 6-bit values). Converts 6-bit to 8-bit via
`(x * 255) / 63`.

### 25.5 Asset filenames

DOS image assets use a short 4-char base prefix + bpp extension:

| Asset           | Base prefix        | Extensions       |
|-----------------|--------------------|------------------|
| NWCP logo       | `nwcp`             | .4/.16/.256      |
| Title screen    | `title`            | .4/.16/.256      |
| Character select| `select`           | .4/.16/.256 + #0..#2 |
| Ending image    | `endpic`           | .4/.16/.256 + #0/#1 |
| Ending tiles    | `endpic` #2..#4    | .4/.16/.256      |
| Font            | `KB`               | .CH              |
| Troop           | `DOS_troop_names[i]` | .4/.16/.256    |
| Villain         | `DOS_villain_names[i]`| .4/.16/.256   |
| Tileset A (0..35)| `tileseta`        | .4/.16/.256 + #ID |
| Tileset B (36..71)| `tilesetb`       | .4/.16/.256 + #ID |
| Tilesalt per continent | `tilesalt`   | .4/.16/.256      |
| Hero sprite     | `cursor`           | .4/.16/.256 (16 frames) |
| Sidebar tiles   | `cursor` (offset 12, 13 frames)| .4/.16/.256 |
| Gold purse      | `cursor` #13       | .4/.16/.256      |
| Coins           | `cursor` offset 25 (3 frames) | .4/.16/.256 |
| Combat tileset  | `comtiles`         | .4/.16/.256 (15 frames) |
| View/character  | `view`             | .4/.16/.256 (14 frames) |
| Class portrait  | `DOS_class_names[i]`   | .4/.16/.256  |
| Location bg     | `DOS_location_names[i]`| .4/.16/.256  |

### 25.5.1 KB.EXE detection

`kbauto.c:84-94` provides two fingerprints for KB.EXE:

```c
{
    "KB", "EXE", 0,
    "Copyright (C) 1990-95 New World Computing, Inc",
    46, 0x15EA5,                    // sign at offset 0x15EA5
    2, KBFAMILY_DOS, KBTYPE_EXE,
},
{
    "KB", "EXE", 79839,             // exact size match
    "", 0, 0,
    2, KBFAMILY_DOS, KBTYPE_EXE,
},
```

The first matches **uncompressed** KB95 by checking for the
copyright string at offset 0x15EA5. The second matches
**compressed** KB90 by exact filesize 79839 bytes (size of
the packed exe).

After `DOS_UnpackExe`, the result is always uncompressed. The
exe_type is determined by post-unpack size:
- `length == KBEXE95_FILESIZE (113718)` → KB95.
- Otherwise → KB90.

This affects which offset table is used (KB90 vs KB95 columns
in `DOS_exe_offsets`).

### 25.5.2 String reading patterns

Each string region has a different read function:

- **Generic** (`DOS_read_strings`): reads byte range, replaces
  trailing byte with 0xFF, returns as strlist.
- **Credits** (`DOS_read_credits`): reads strings, then re-formats
  with centering for "copyright" lines and indentation for names.
- **Villain descriptions** (`DOS_read_vdescs`): 14 lines per
  villain; specific lines get prepended with spaces or appended
  with `%s` for substitution.
- **Artifact descriptions** (`DOS_read_adescs`): 5 lines per
  artifact, no special formatting.

Each is keyed by `(offset, endoff)` for the byte range, and
optionally a per-villain/artifact "skip lines" parameter.

### 25.6 Troop / class / location short codes

From dos-data.c:

```c
DOS_troop_names[25]   = { "peas","spri","mili","wolf","skel","zomb",
                          "gnom","orcs","arcr","elfs","pike","noma",
                          "dwar","ghos","kght","ogre","brbn","trol",
                          "cavl","drui","arcm","vamp","gian","demo",
                          "drag" };
DOS_villain_names[17] = { "mury","hack","ammi","baro","drea","cane",
                          "mora","barr","barg","rina","ragf","mahk",
                          "auri","czar","magu","urth","arec" };
DOS_class_names[4]    = { "knig","pala","sorc","barb" };
DOS_location_names[6] = { "cstl","town","plai","frst","cave","dngn" };
```

## 26. Colors — EGA, CGA palettes, schemes

The DOS module supports 4 color modes (Mono/CGA/EGA/VGA) via
the `bpp` field on the module config. Mode determines which
asset variants are loaded (`.4`/`.16`/`.256`) and which palette
is applied to the resulting surfaces.

Color mode selection at module config time:
- `dos-mono` / `dos-herc` → bpp=1 (Hercules monochrome).
- `dos-cga` → bpp=2 (CGA 4-color).
- `dos-ega` → bpp=4 (EGA 16-color).
- `dos` / `dos-vga` → bpp=8 (VGA 256-color from MCGA.DRV).

All "internal" palette indices are converted to RGB at
SDL_SetColors time.

### 26.1 EGA palette (16 entries, `ega_pallete_rgb[]`)

```
 0 0x000000 black
 1 0x0000AA dark blue
 2 0x00AA00 dark green
 3 0x00AAAA cyan
 4 0xAA0000 dark red
 5 0xAA00AA magenta
 6 0xAA5500 brown
 7 0xAAAAAA light gray
 8 0x555555 dark gray
 9 0x5555FF light blue
10 0x55FF55 light green
11 0x55FFFF light cyan
12 0xFF5555 light red
13 0xFF55FF light magenta
14 0xFFFF55 yellow
15 0xFFFFFF bright white
```

Synonyms (kbres.h):
```
EGA_DVIOLET = EGA_MAGENTA (5)
EGA_DYELLOW = EGA_BROWN   (6)
EGA_DWHITE  = EGA_GREY    (7)
```

### 26.2 CGA palettes (8 variants, `cga_palletes_ega[8][4]`)

CGA maps 2-bit color indices 0..3 into the EGA palette:

```
 Pal 0: EGA_BLACK, EGA_DGREEN, EGA_DRED, EGA_BROWN
 Pal 1 (default): EGA_BLACK, EGA_DCYAN, EGA_MAGENTA, EGA_DWHITE
 Pal 2: EGA_BLACK, EGA_GREEN, EGA_RED, EGA_YELLOW
 Pal 3: EGA_BLACK, EGA_CYAN, EGA_VIOLET, EGA_WHITE
 Pal 4: EGA_DBLUE, EGA_DGREEN, EGA_DRED, EGA_BROWN
 Pal 5: EGA_DBLUE, EGA_DCYAN, EGA_MAGENTA, EGA_DWHITE
 Pal 6: EGA_DBLUE, EGA_GREEN, EGA_RED, EGA_YELLOW
 Pal 7: EGA_DBLUE, EGA_CYAN, EGA_VIOLET, EGA_WHITE
```

### 26.3 Hercules palette (monochrome)

`herc_pallete_ega[] = { 0, 15, 15, 15 }`. All non-background
colors collapse to white.

### 26.4 COL_TEXT schemes (DOS)

Each scheme is a 18-entry `Uint32[COLORS_MAX]` in 0x00RRGGBB
format. `sub_id` selects the scheme. In EGA mode, entries come
from these byte-indexed arrays:

**CS_MINIMAP / CS_GENERIC** (default message box — `ega_scheme_mb_index`):

```
background=DBLUE  text=WHITE(4)  shadow=MAGENTA(2)  frame=YELLOW(2)
sel_bg=WHITE  sel_text=DBLUE(4)  sel_shadow=MAGENTA(2)  sel_frame=MAGENTA(2)
```

**CS_CHROME** (raw black-white):
```
bg=BLACK  text=WHITE(4)  shadow=MAGENTA(2)  frame=YELLOW(2)
sel_bg=WHITE  sel_text=BLACK(4)  sel_shadow=MAGENTA(2)  sel_frame=MAGENTA(2)
```

**CS_MINIMENU** (savefile):
```
bg=BLUE  text=WHITE(4)  shadow/frame=MAGENTA
sel_bg=WHITE  sel_text=BLUE(4)  sel_shadow/frame=MAGENTA
```

**CS_VIEWARMY / CS_VIEWCHAR:**
```
bg=DGREY  text=WHITE(4)  shadow=MAGENTA(2)  frame=DRED(2)
sel_bg=DGREY  sel_text=WHITE(4)  sel_shadow=MAGENTA(2)  sel_frame=DRED(2)
```

**CS_STATUS_1..5** (difficulty-colored status bar). Base
`ega_scheme_status_index`:
```
bg=DRED  text=WHITE  shadow=MAGENTA  frame=YELLOW
sel_bg=YELLOW  sel_text=DCYAN(4)  sel_shadow=MAGENTA  sel_frame=YELLOW
```

Then `COLOR_BACKGROUND` is overridden with `ega_scheme_status_replacers[sub_id-1]`:
```
CS_STATUS_1 (easy)        → GREY
CS_STATUS_2 (normal)      → DRED
CS_STATUS_3 (hard)        → BLUE
CS_STATUS_4 (impossible?) → DVIOLET
CS_STATUS_5 (also DVIOLET)
```

**CS_ENDING:**
```
(win half)  bg=DBLUE  text=WHITE  shadow/frame=MAGENTA
(lose half) bg=DBLUE  text=WHITE  shadow/frame=MAGENTA
```

(Both halves identical except via SELECTION offset in the
color-array indexing — lose_game reads `colors + COLOR_SELECTION`.)

**CGA schemes**: reduced to 4-color with separate tables
`cga_scheme_chrome_index`, `cga_scheme_minimap_index`,
`cga_scheme_char_index`. Mapped through the selected CGA palette
via `cga_palletes_ega[cga_index][n]`.

### 26.5 COL_MINIMAP — tile colors

Indexed by raw tile byte (0..255). Tile type is recomputed from
tile id using the IS_* predicates. Each tile type gets one EGA
color:

```
DEEP_WATER   → DBLUE
SHALLOW_WATER→ BLUE
GRASS        → GREEN
DESERT       → YELLOW
ROCK         → BROWN
TREE         → DGREEN
CASTLE       → WHITE
MAP_OBJECT   → DRED   (includes all interactive tiles)
(fog byte 0xFF) → BLACK
```

In CGA mode, different palette indices are used:
```
SHALLOW_WATER → cyan(1)
DEEP_WATER    → cyan(1)
GRASS         → magenta(2)
DESERT        → white(3)
ROCK          → black(0)
TREE          → cyan(1)
CASTLE        → white(3)
MAP_OBJECT    → magenta(2)
(fog)         → EGA_BLACK (remapped through CGA)
```

### 26.6 COL_DWELLING — dwelling text-box colors

```
DWELLING_PLAINS  → EGA_GREY
DWELLING_FOREST  → EGA_DGREEN
DWELLING_HILLCAVE→ EGA_BLUE
DWELLING_DUNGEON → EGA_DRED
DWELLING_CASTLE  → EGA_MAGENTA
```

In CGA mode, all are set to black (0x000000) — simplification.

### 26.7 Color scheme application

Each gameplay screen uses a specific COL_TEXT scheme:

| Screen          | CS_*           | Notes |
|-----------------|----------------|-------|
| Default message | CS_GENERIC     | Most BottomBox / MessageBox |
| Status bar      | CS_STATUS_1..5 | By difficulty + 1 |
| Savefile picker | CS_MINIMENU    | Lighter blue bg |
| Character view  | CS_VIEWCHAR    | Dark gray bg, dark red frame |
| Army view       | CS_VIEWARMY    | Same as VIEWCHAR |
| Minimap         | CS_MINIMAP     | Default scheme |
| Options/Controls| CS_CHROME      | Black/white menu |
| Endings         | CS_ENDING      | Win/lose split |

Resolved on demand (not pre-cached) in most cases. Updated when
difficulty changes (only the status_colors).

### 26.8 CGA palette switching

The Controls menu allows switching CGA palette index 0..7.
This triggers `SDL_ReplaceColors` over the entire screen,
swapping the old palette's RGB values for the new palette's.

```c
old_pal = KB_Resolve(PAL_PALETTE, old_index)
new_pal = KB_Resolve(PAL_PALETTE, new_index)
SDL_ReplaceColors(screen, full, old_pal32, new_pal32, 4)
```

Then `update_ui_colors(game)` re-resolves text colors with the
new palette, and `SDL_FreeCachedSurfaces()` clears the cache so
new surfaces use the new palette.

This is "live" palette swap — the display updates without
re-loading assets.

### 26.9 Color storage format

`Uint32* colors` arrays use 0x00RRGGBB format (no alpha):
- `colors[i] & 0x00FF0000` >> 16 = red.
- `colors[i] & 0x0000FF00` >> 8 = green.
- `colors[i] & 0x000000FF` = blue.

`SDL_Color` arrays (256 entries) use the SDL struct format:
```c
SDL_Color {
    Uint8 r, g, b;
    Uint8 unused;       // not set in openkb
};
```

Conversion between formats happens at SDL_SetColors time.

### 26.10 EGA → CGA index mapping

`cga_palletes_ega[8][4]` maps each CGA palette's 4 indices
to EGA's 16 indices:

```
CGA 0 → EGA: BLACK, DGREEN, DRED, BROWN
CGA 1 → EGA: BLACK, DCYAN, MAGENTA, DWHITE  (default)
CGA 2 → EGA: BLACK, GREEN, RED, YELLOW
CGA 3 → EGA: BLACK, CYAN, VIOLET, WHITE
CGA 4 → EGA: DBLUE, DGREEN, DRED, BROWN
CGA 5 → EGA: DBLUE, DCYAN, MAGENTA, DWHITE
CGA 6 → EGA: DBLUE, GREEN, RED, YELLOW
CGA 7 → EGA: DBLUE, CYAN, VIOLET, WHITE
```

Each row has 4 entries (CGA's 4 colors). The actual RGB comes
from `ega_pallete_rgb[]` lookup.

To switch CGA palettes: change the index, recompute all colors
through this table.

## 27. Resource system

The resource system is the abstraction layer between game code
and the underlying assets (DOS/GNU/MD module files). Every game
asset — graphics, sounds, palettes, data tables, strings — is
addressed by an integer pair `(id, sub_id)` and resolved through
a per-module callback.

### 27.0 Why a resource layer?

Three motivations:
1. **Multi-module support**: same code can run with DOS assets,
   GNU INI files, or MD ROM bytes. Module code maps `(id,sub_id)`
   to its native format.
2. **Lazy loading**: assets are loaded on first request, cached,
   and freed at module shutdown. Code never opens raw files
   directly.
3. **Format abstraction**: combinations like "tileset assembled
   from 72 individual tiles" or "color scheme by difficulty"
   live in the resource layer, not the game logic.

The cost: every asset access goes through a function-pointer
dispatch (`KB_Resolve` → `DOS_Resolve` / `GNU_Resolve` /
`MD_Resolve`).

### 27.1 Resource ID enum

All resources are addressed by a `(id, sub_id)` pair. The ID enum
is generated from a macro list in `kbres.h`. Every ID, grouped:

**GR_* — SDL_Surface (image) resources:**
```
GR_LOGO         // company logo (NWCP)
GR_TITLE        // game title screen
GR_TROOP        // troop sprite sheet (4 frames) per troop_id
GR_TILE         // one map tile by tile index
GR_TILESET      // whole map tileset for a continent (salted)
GR_TILEROW      // a row of 36 tiles (for GNU "tileseta/b" layout)
GR_TILESALT     // continent-flavored replacement tiles (3 per cont)
GR_VILLAIN      // villain portrait (4 frames) per villain_id
GR_FACE         // unused (reserved for per-face indexing)
GR_FACES        // unused
GR_COMTILE      // one combat tile
GR_COMTILES     // combat tileset (15 frames)
GR_LOCATION     // location background (home/town/dwellings 0..5)
GR_FONT         // bitmap font
GR_CURSOR       // hero sprite sheet (also: GR_HERO alias = same id)
GR_UI           // UI element sheet (sidebar tiles, 13 frames)
GR_SELECT       // title-screen misc (3 subids)
GR_PORTRAIT     // class portrait (0..3)
GR_GOLD         // reserved
GR_PURSE        // single sidebar tile (gold purse)
GR_COIN         // single coin by sub_id (0..2)
GR_COINS        // coins sheet (3 frames)
GR_PIECE        // puzzle piece (9x6 mini-rect)
GR_VIEW         // artifacts + maps + empty slot + empty map (14 frames)
GR_ARTIFACTS    // reserved, same idea as VIEW but just artifacts
GR_MAPS         // reserved
GR_INVSLOT      // reserved
GR_BLANKMAP     // reserved
GR_ARTIFACT     // one artifact by id
GR_ORBMAP       // one continent's filled orb map
GR_ENDING       // end-screen bg (0=win, 1=lose)
GR_ENDTILE      // end-screen tile (0=grass, 1=bridge, 2=hero)
GR_ENDTILES     // all end tiles (3 frames)
```

**SN_* — KBsound (audio) resources:**
```
SN_TUNE         // sub_id 0..9 is tune index in TUN group
```

**PAL_* — SDL_Color[256] palettes:**
```
PAL_PALETTE     // CGA palette variant (sub_id 0..7 in CGA mode)
```

**COL_* — Uint32[COLORS_MAX] or Uint32[256] color arrays:**
```
COL_TEXT        // text-box scheme (sub_id = CS_*)
COL_MINIMAP     // 256-entry per-tile color map
COL_DWELLING    // 5-entry dwelling-type colors (ARMY view bg)
```

**DAT_* — byte arrays:**
```
DAT_WORLD         // 16384-byte world map dump
DAT_LAND          // per-continent map (64*64 = 4096 bytes)
DAT_MENUCONTROLS  // 16-byte visibility mask for options menu
DAT_VNEED         // [4] villains-needed by rank; sub_id = class
DAT_KMAGIC        // [4] knows_magic increment per rank
DAT_SPOWER        // [4] spell_power per rank
DAT_MAXSPELL      // [4] max_spell per rank
DAT_FAMILIAR      // [4] instant_army troop id per rank
DAT_VTROOP        // [MAX_VILLAINS*5] villain army troop ids
DAT_CASTLEX/Y/C   // [MAX_CASTLES] castle coords
DAT_TOWNX/Y/C     // [MAX_TOWNS] town coords
DAT_TOWNINV       // [MAX_TOWNS] town→castle letter mapping
DAT_BOATX/Y       // [MAX_TOWNS] boat drop coords
DAT_GATEX/Y       // [MAX_TOWNS] town-gate coords
DAT_NAVX/Y        // [4] continent entry coords
DAT_SPECIALX/Y/C  // [2] SP_HOME, SP_ALCOVE coords
DAT_SKILLS/MOVES/MELEEMIN/MAX/RANGEMIN/MAX/SHOTAMMO/HPS/GCOST/SPOILS/DWELLS/MGROUP/MAXPOP/GROWTH/ABILS
                  // [MAX_TROOPS] each — full troop table
```

**WDAT_* — word arrays:**
```
WDAT_SGOLD        // [4] starting gold (never actually wired)
WDAT_STROOP       // [2] starting troop types (class key)
WDAT_SNUMBER      // [2] starting troop numbers (class key)
WDAT_COMM         // [4] commission per rank (class key)
WDAT_LDRSHIP      // [4] leadership per rank (class key)
WDAT_SCOST        // [14] spell costs
WDAT_VREWARD      // [17] villain rewards
WDAT_VNUMBER      // [17*5] villain army counts
```

**RECT_* — SDL_Rect:**
```
RECT_MAP          // map viewport rect
RECT_UI           // sub_id 0..4 for top/left/right/bottom/bar frames
RECT_TILE         // single map tile size
RECT_UITILE       // sidebar button size
```

**STR_* — single ASCII string:**
```
STR_SIGN          // signpost text by index
STR_TROOP         // troop name by troop id (same for STR_MULTI)
STR_RANK          // rank name by rank*4 + class
STR_CASTLE        // castle name
STR_TOWN          // town name
STR_VNAME         // villain name
STR_VDESC         // villain description line
STR_ANAME         // artifact name
STR_ADESC         // artifact description line
STR_CREDIT        // credits line
STR_ENDING        // ending line (sub_id <100 = win, >=100 = lose)
```

**STRL_* — asciiz-list (strings separated by \0, terminated 0xFF):**
```
STRL_CONTINENTS   // 4 continent names
STRL_SIGNS        // all signposts
STRL_TROOPS       // 25 troop names
STRL_MULTIS       // reserved
STRL_RANKS        // 4 rank names for one class (sub_id)
STRL_CASTLES      // 26 castle names
STRL_TOWNS        // 26 town names
STRL_SPELLS       // 14 spell names
STRL_VNAMES       // 17 villain names
STRL_VDESCS       // all description lines for one villain (sub_id)
STRL_ANAMES       // 8 artifact names
STRL_ADESCS       // 5 description lines per artifact (sub_id)
STRL_CREDITS      // all credit lines
STRL_ENDINGS      // all lines of ending (sub_id 0=win, 1=lose)
```

Additional aliases:
```
GR_HERO      = GR_CURSOR
FIRST_STR    = STR_SIGN
FIRST_STRL   = STRL_SIGNS
```

### 27.2 Resolver chain

`KB_Resolve(id, sub_id)` (env-sdl.c:632):

```c
i = conf->module;
l = conf->fallback ? conf->num_modules : i+1;
for (; i < l; i++) {
    switch (conf->modules[i].kb_family) {
      case KBFAMILY_GNU: ret = GNU_Resolve(mod, id, sub_id); break;
      case KBFAMILY_DOS: ret = DOS_Resolve(mod, id, sub_id); break;
      case KBFAMILY_MD:  ret = MD_Resolve(mod, id, sub_id); break;
    }
    if (ret != NULL) break;
}
```

If `conf->fallback` is 1, failed lookups fall through to the next
module. Default is 0 (single active module).

### 27.3 Surface loading and caching

`SDL_LoadRESOURCE(id, sub_id, flip)` (env-sdl.c:402) wraps
`KB_LoadIMG8` (which calls KB_Resolve), then applies:

- **Zoom**: if `conf->filter == 1` (normal2x), double using
  `SDL_SizeX`; if `conf->filter == 2` (scale2x), use AdvancedMAME
  scale2x filter.
- **Flip**: if `flip == 1`, produce a horizontally-flipped copy
  beneath (target surface is 2× height). Internally the flip
  operates on 48-pixel-wide chunks (`flip_range = 48 * zoom`).

`SDL_TakeSurface(id, sub_id, flip)` caches the surface in
`surf_cache[64][64]` — cleared via `SDL_FreeCachedSurfaces`.

#### 27.3.1 Cache details

`surf_cache[64][64]` is a 2D array indexed by `(id, sub_id)`. So
ids and sub_ids are restricted to 0..63 for caching purposes;
larger ids return uncached surfaces. Currently all GR_* ids fit
in 0..63.

Cache eviction:
- `SDL_ReleaseSurface(id, sub_id)` — explicit release.
- `SDL_FreeCachedSurfaces()` — release all (called at module
  stop and module switch).

Cache lifecycle:
- First call: load via resolver, store pointer.
- Subsequent calls: return cached pointer (no reload).
- On module change: cache is wiped (different module = different
  asset content).

Memory cost: each surface is ~1-50 KB (depending on size and
bpp). Full cache for a typical game: ~3-5 MB.

#### 27.3.2 Surface resolution sequence

1. Game requests `SDL_LoadRESOURCE(GR_TROOP, 5, 1)` (Zombies, flipped).
2. `KB_LoadIMG8(GR_TROOP, 5)` calls `KB_Resolve(GR_TROOP, 5)`.
3. `KB_Resolve` walks modules per `conf->module` and `conf->fallback`.
4. First module that returns non-NULL wins.
5. Returned `SDL_Surface*` is wrapped (zoomed/flipped) per `flip`
   arg.
6. Result returned to caller.

For `SDL_TakeSurface`, step 6 also stores the result in cache
before returning.

### 27.4 Tileset factories (kbres.c)

`KB_LoadTileset_TILES(tilesize, resolve, mod)`:

- Creates 8×10 grid (70 tiles per tileset layout: 8 wide × 9 rows).
- Calls `resolve(mod, GR_TILE, i)` for i=0..71.
- Blits each tile into its grid position.

`KB_LoadTileset_ROWS(tilesize, resolve, mod)`:

- Calls `resolve(mod, GR_TILEROW, 0)` and `GR_TILEROW, 36`.
- Each row image contains 36 tiles in horizontal strip.
- Blits into the same 8×10 tileset grid.

`KB_LoadTilesetSalted(continent, resolve, mod)`:

- If `continent == 0`: return base tileset unchanged.
- Else: load `GR_TILESALT, continent` (3-tile image).
- Replace tileset entries 17, 18, 19 with the 3 salt tiles.
  (These are 3 continent-specific "environment" tiles — e.g.
  Forestria's particular tree variant.)

### 27.5 Inline helpers

`SDL_CreatePALSurface(w,h)` — 8-bit palette surface with colorkey
255.

`SDL_ClonePalette(dst, src)` — copy src's palette onto dst.

`SDL_BlitXBPP(src, dest, dstrect, bpp)` — expand 1/2/4/8-bpp
packed source bits into 8-bit paletted surface. MSB-first within
each byte.

`SDL_BlitMASK(src, dest, dstrect)` — 1-bpp mask, sets target
pixel to 0xFF where mask bit is 1.

`SDL_ReplaceIndex(dest, rect, search, replace)` — palette index
swap.

`SDL_ReplaceColors(dest, rect, search[], replace[], n)` — RGB
color swap (used for CGA palette changes).

`put_mono_pal`, `put_cga_pal`, `put_ega_pal`, `put_vga_pal`,
`put_color_pal(fore, back)` — set 4-entry or 16-entry palette.

### 27.6 Resource ID conventions

Each prefix has stability conventions:

- **GR_*** ids 0..28: graphics, generally stable. Adding new ids
  appends to the end.
- **SN_*** ids 29..29: sounds. Just SN_TUNE for now.
- **PAL_*** id 30: palettes.
- **COL_*** ids 31..33: color arrays.
- **DAT_*** ids 34..71: data tables. Most module-overridable.
- **WDAT_*** ids 72..82: word-array data.
- **RECT_*** ids 83..86: layout rects.
- **STR_*** ids 87..98: single strings.
- **STRL_*** ids 99..112: string lists.

The enum is generated from the `RESOURCES` macro in `kbres.h`,
so reordering breaks all numeric ids. Don't reorder.

`KBresid_names[]` — array of string names for each resource id,
generated from the same macro. Used for debug logs.

### 27.7 Resource resolution edge cases

**ID out of range**: returns NULL silently.

**Sub_id mismatch**: depends on the resource. For images,
returns NULL. For data, may return NULL or an indeterminate
table.

**Module returns NULL**: if `conf->fallback` is set, the next
module is tried. Otherwise, the request fails (returns NULL).
The caller must handle NULL gracefully (most do; some crash).

**No active module**: `conf->module` is index into
`conf->modules[]`. If 0..num_modules-1, valid. If out of range,
the resolver still iterates from `i = conf->module`, which
underflows to invalid memory — undefined behavior. The select-
module flow ensures this doesn't happen at runtime.

### 27.8 Module-specific resolver pseudocode

```c
void* KB_Resolve(int id, int sub_id) {
    int i = conf->module;
    int l = conf->fallback ? conf->num_modules : i + 1;
    void *ret = NULL;
    for (; i < l; i++) {
        KBmodule *mod = &conf->modules[i];
        switch (mod->kb_family) {
            case KBFAMILY_GNU: ret = GNU_Resolve(mod, id, sub_id); break;
            case KBFAMILY_DOS: ret = DOS_Resolve(mod, id, sub_id); break;
            case KBFAMILY_MD:  ret = MD_Resolve(mod, id, sub_id); break;
        }
        if (ret != NULL) break;
    }
    if (ret == NULL)
        KB_errlog("Unable to resolve resource %s::%d (from %d modules)\n",
                  KBresid_names[id], sub_id, l);
    return ret;
}
```

### 27.9 Adding new resource ids

To extend the system:

1. Add a new line to the `RESOURCES` macro in `kbres.h`.
2. Add a case to one or more `*_Resolve` functions.
3. Use the new id via `KB_Resolve(NEW_ID, sub_id)`.

Numbering is automatic via the macro. No need to update
`KBresid_names[]` (also auto-generated).

A clean-room reimplementation can reuse this scheme or design
its own resource API.

## 28. Module system

A "module" is a self-contained source of game assets. openkb
supports three module families: DOS (read original game files),
GNU (read PNG/INI files), and MD (read Mega Drive ROMs). Modules
are pluggable; multiple can coexist with optional fallback.

### 28.0 Why modules?

The DOS King's Bounty distribution is proprietary. To play
openkb requires either:
1. A copy of KB.EXE + 256.CC + 416.CC (DOS module).
2. A "Free" module providing replacement art and data (GNU).
3. A Mega Drive ROM image (MD module).

Each module knows how to read its source files and serve them
through the resource resolver (§27).

A single openkb installation can have multiple modules
configured; the active one is selected via `select_module()` at
startup. With `fallback = 1`, missing assets in the active
module fall through to subsequent modules.

### 28.1 Module families

```
KBFAMILY_GNU = 0x0F     // "free" format: INI + PNG/BMP
KBFAMILY_DOS = 0x0D     // KB.EXE + CC archives
KBFAMILY_MD  = 0x02     // Mega Drive ROM
```

`KBmodule` struct (kbconf.h):

```
char name[255];
char slotA_name[1024];   // path to primary file (e.g. "416.CC#")
char slotB_name[1024];   // secondary (e.g. "256.CC#")
char slotC_name[1024];   // tertiary (reserved)
KB_DIR *slotA, *slotB, *slotC;
int bpp;                 // 0, 1, 2, 4, 8
int kb_family;
void *cache;             // module-specific (e.g. DOS_Cache)
```

Up to `MAX_MODULES = 16` modules can be registered.

### 28.2 Auto-discovery (kbauto.c:255)

Walks a directory tree; for each file, matches against a
fingerprint table:

```c
KBfileid fingerprints[] = {
    { "free",   "",    0,       "",             0, 0,  -1, GNU, DIR   },
    { "416",    "CC",  0,       "",             0, 0,   1, DOS, GROUP },
    { "256",    "CC",  0,       "",             0, 0,   0, DOS, GROUP },
    { "KB",     "EXE", 0,       "Copyright (C) 1990-95 New World Computing, Inc",
                                                 46, 0x15EA5,
                                                  2, DOS, EXE },
    { "KB",     "EXE", 79839,   "",             0, 0,   2, DOS, EXE },
    { "",       "BIN", 0,       "SEGA",          4, 0x100,
                                                 -1, MD,  ROM },
};
```

A fingerprint matches if:

- Filename base matches (case-insensitive).
- File extension matches.
- (Optional) Exact filesize matches.
- (Optional) Sign at offset matches `sign_len` bytes.
- (For DIR type) Path is a directory.

After scanning, modules are composed by companion grouping:

- **Free:** directory named "free" → single-module.
- **DOS:** one `256.CC` (companion=0) OR one `416.CC` (companion=1)
  AND one `KB.EXE` (companion=2) → register:
  - DOS (VGA) if both CC files present + EXE
  - DOS (Hercules), DOS (CGA), DOS (EGA) in all cases (using
    just 416.CC + EXE for EGA/lower since VGA needs 256.CC)
- **Mega Drive:** any `.BIN` with "SEGA" signature.

`add_module_aux` constructs each module with `slotA_name` =
`<path>/<slotA_file>`, `slotB_name` = `<path>/<slotB_file>`, etc.

### 28.3 Module lifecycle

```
init_modules(conf)  →  for each module (selected or all if fallback),
                       call family-specific Init (e.g., DOS_Init
                       preloads palette and tunes)
stop_modules(conf)  →  DOS_Stop frees caches
register_module(conf, mod) — append to modules list
wipe_module(mod)     — zero slot names, clear cache
```

### 28.4 Resolution helpers

`KB_opendir_with(filename, mod)` — tries to open filename
prefixed by each of the 3 slots in turn; returns first success.

`KB_fopen_with(filename, mode, mod)` — same for file opens.

`KB_fcaseopen_with(filename, mode, mod)` — case-insensitive; walks
each slot's directory entries to find matching filename then
opens.

`match_file(path, filename, match)` — scan a real directory for
a file, preferring exact match then case-insensitive; writes
actual filename to `match`.

### 28.5 DOS module specifics

`DOS_Init(mod)`:
1. Adjust slots: if only slotA is set, attempt to find
   "256.CC" or "416.CC" in slotA path; copy to slotB.
2. Allocate `DOS_Cache`:
   - `vga_palette = NULL`.
   - `cga_palette = NULL`.
   - `exe_file = NULL`.
3. Pre-cache:
   - If `bpp == 2` (CGA): set `cga_index = 1`, load
     `DOS_CGAPalette`.
   - If `bpp == 8` (VGA): load `MCGA.DRV` palette via
     `DOS_ReadPalette`.
   - Load tunes via `DOS_ReadTunes`.

`DOS_Stop(mod)`:
1. Close cached EXE file.
2. Free vga/cga palettes.
3. Free tunes.
4. Free cache struct.

`DOS_Resolve(mod, id, sub_id)`:
- Switch on resource id.
- For graphics: build filename from base prefix + bpp suffix +
  ident, open file (or directory + file for IMG groups), decode.
- For strings: read offset range from KB.EXE, parse via
  `DOS_read_strings` / `DOS_read_credits` / etc.
- For data tables: read offset range, parse as bytes/words.
- For sounds: return cached tune from `DOS_Cache->tunes`.
- Default (unknown id): return NULL.

### 28.6 GNU module specifics

`GNU_Resolve(mod, id, sub_id)`:
- Switch on resource id.
- For graphics: try PNG (with SDL_image) or BMP from
  `mod->slotA_name/<filename>`.
- For strings: read text files (`signs.txt`, `endwin.txt`, etc.)
  or INI sections (`troops.ini[troopN]`).
- For data: read INI values (`troops.ini[troopN].hp`, etc.).
- For colors: read INI hex values (`colors.ini[generic].text1
  = #FFFFFF`).
- Returns NULL if file missing or section absent.

### 28.7 Module switching

Switching modules at runtime is not supported in current openkb.
A clean-room implementation could add this, but it would require:
1. Stopping the current module (clear caches).
2. Switching `conf->module` index.
3. Initializing the new module.
4. Reloading any in-memory assets (palette, fonts).

The `update_ui_colors(game)` function does a partial refresh
(re-resolve text colors); a full module switch would extend this
to re-resolve everything.

### 28.8 Module discovery edge cases

`discover_modules` may return:

- **No modules found**: error log and game won't start. User
  must configure modules in `openkb.ini` or copy assets.
- **DOS without EXE**: registers EGA/CGA/Mono modules using
  416.CC alone (no VGA). DOS_Resolve will fail for resources
  that need EXE (signs, villains, tunes).
- **Multiple module copies**: if a directory has both DOS and
  GNU sources, both are registered. Select via select_module.

Auto-discovery can be disabled via `autodiscover = 0` in INI.
Then modules must be explicitly listed:

```ini
[module]
name = DOS
type = dos
path = data/dos/256.CC#
path = data/dos/416.CC#
```

Each `path` line cycles through slotA, slotB, slotC.

## 29. DOS asset formats

DOS King's Bounty assets are packed in proprietary formats.
openkb implements decoders for all of them, plus a standalone
KB.EXE unpacker. This section covers each format byte-by-byte.

### 29.0 Format overview

```
KB game distribution:
  KB.EXE         — game executable (Execomp/ExePack compressed)
  256.CC         — VGA assets (LZW compressed in archive)
  416.CC         — EGA/CGA/Mono assets (LZW compressed)
  MCGA.DRV       — VGA palette (inside 256.CC)
  KB.CH          — bitmap font (inside 416.CC)
  *.4 *.16 *.256 — image assets (inside CCs)
  LAND.ORG       — world map (inside CCs)
  KB-SAVE.DAT    — save file (separate)
```

Each asset format has a corresponding decoder:
- `.CC` → dos-cc.c (LZW + custom hash table)
- `.4`/.16/.256 → dos-img.c (bit-packed images)
- `.CH` → dos-img.c (1bpp font)
- `.EXE` → dos-exe.c (Execomp + ExePack)
- Tunes → dos-snd.c (PC speaker frequency/duration)
- MCGA palette → dos-data.c (768-byte VGA palette)

### 29.1 `.CC` archive (dos-cc.c)

`.CC` is a custom archive with LZW-compressed entries. Layout:

```
Offset 0x0000    word    num_files (<= 133)
Offset 0x0002  ┐
               │  ccHeader.files[MAX_CC_FILES]
               │    each entry is 8 bytes:
               │      word  key     (hash of filename)
               │      sword offset  (3 bytes)
               │      sword size    (3 bytes)
               │
Offset 0x0460    64 bytes  unknown (could be more slots)
Offset 0x0462  ┐
               │  Compressed file data (each file is
               │  "full_size_dword + LZW stream")
               ⋮
```

Filename hash (`KB_ccHash`, dos-cc.c:27):

```c
key = 0;
while (next = *filename++) {
    next &= 0x7F;
    if (next >= 0x60) next -= 0x20;   // fold case
    key = ((key >> 8) & 0x00FF) | ((key << 8) & 0xFF00); // swap bytes
    key = (key << 1) | ((key >> 15) & 0x0001);           // rotate left 1
    key += next;
}
```

### 29.2 LZW variant (`KB_funLZW`, dos-cc.c:215)

Variable-width LZW with 9-12 bit codes:

- Each block is 9..12 bits.
- Codes 0x0000..0x00FF are literals.
- 0x0100 = clear dictionary (reset step=9, range=0x200,
  index=0x0102).
- 0x0101 = end of file.
- Otherwise: dictionary entry (chain of `dict_key[]` +
  `dict_val[]`).

Dictionary structure:
- `dict_key[768*16]` — index to previous entry
- `dict_val[768*16]` — the byte value

Emit strategy:

```
While not EOF:
  Read next_index via bit-positional extraction.
  If next_index >= current dict_index: use last_index, queue
    last_char (KwKwK case).
  While next_index > 0xFF: queue dict_val[next_index]; next_index
    = dict_key[next_index].
  Queue the final byte. Flush queue to output.
  Insert new entry: dict_key[dict_index] = last_index;
                    dict_val[dict_index] = last_char;
  dict_index++.
  If dict_index >= dict_range and step < 12: step++; range*=2.
```

File begins with a `word` length, then the LZW stream.

Read buffer size `MBUFFER_SIZE = 1024`; when byte position nears
the edge, shift leftovers and refill. Leftover handling reinits
`pos = bit_pos + step` with `byte_pos = 0`.

The dictionary-reset command (0x0100) uses a `reset_hack` latch:
next iteration, reset dict state then apply the next value as-is
(code duplication avoided).

### 29.3 `.4 / .16 / .256 / .CH` image formats (dos-img.c)

Extension → bpp:

```
.CH   → 1 bpp (font)
.4    → 2 bpp (CGA)
.16   → 4 bpp (EGA)
.256  → 8 bpp (VGA)
```

**IMG archive format** (for .4/.16/.256 when multi-frame):

```
Offset 0       word    num_files (<= 36)
Offset 2       imgFile files[num_files]:
                 word offset       // to pixel data
                 word mask_offset  // 0 if no mask
```

Then each file's data starts with:
```
word width
word height
(width*height*bpp/8) bytes of packed pixels (MSB-first)
(optional) (width*height/8) bytes of 1-bpp mask
```

BPP auto-detection (`imgGroup_detect_bpp`):
```
next_offset = files[1].offset - 4      // or end of file
pixel_area  = files[0].w * files[0].h
bpp = 8 / (pixel_area / (next_offset - files[0].offset - 4))
```

When a mask is present, color 0xFF is set as colorkey on the
resulting surface.

**RAW image** (single file, no IMG header): first 4 bytes are
width/height, then packed pixels, optional mask tail. Decoded via
`DOS_LoadRAWIMG_BUF`.

**CH font**: 8×8 glyphs, 128 characters. `DOS_LoadRAWCH_BUF`
creates a 128×8 palette surface (16-wide × 8-tall = 128 glyphs
in a 16×8 grid).

### 29.4 `KB.EXE` unpacker (dos-exe.c)

The original KB.EXE is packed with either MS ExePack or
New-World's own "Execomp" LZW wrapper around the exe. `DOS_UnpackExe(f,
freesrc)`:

1. Try `execomp_uncompress(f)`. Signature: 0xE9 0x99 0x00 at offset
   0x200. If matches:
   - Read outer MZ header + inner MZ header + LZW stream.
   - `KB_funLZW` decodes the stream into a fresh buffer.
   - Write a new MZ header pointing at the decoded code.
   - Return as a `KBFTYPE_BUF` file.
2. Try `exepack_uncompress(f)`. Signature: "RB" (0x4252) at
   offset `[cs*16 + exeLen]` in the packed exe.
   - Parse exepack variables (unpacker_len, dest_len, etc.).
   - Read packed data + unpacker code.
   - `exepack_unpack`: reverse-order RLE decoder:
     - 0xB0: RLE of a single fill byte for lengthWord bytes
     - 0xB2: copy-next lengthWord bytes
     - LSB bit set on command byte = last block
   - Unpack relocation table (per-section 16-slot count arrays).
   - Build new MZ header with unpacked size, relocated code.
   - Return as `KBFTYPE_BUF`.

Either, both, or neither may apply. `execomp` always runs first.

### 29.5 PC-speaker tunes (dos-snd.c)

Each tune is an array of `(note_index, delay_index)` pairs
terminated by 0xFF. Two global palettes decode indices:

```
tunPalette {
    word freq[88];        // note-freq in Hz
    word duration[16];    // delay-duration in ms
}
```

`tunFile_play(tun, stream, len, freq)` digitizes into PCM:

1. Get `delay_index = tun->delay[cur_note]`,
   `note_index = tun->notes[cur_note]`.
2. `ms = palette.duration[delay_index]`.
3. `total_samples = freq * ms / 1000`.
4. For each sample within the note, drive a triangle wave at
   frequency `palette.freq[note_index]`:
   - `bay = freq / note_hz` (samples per cone cycle).
   - `speed = (PEAK - LEAK) / bay` (ramp slope, where PEAK=0xFFF0,
     LEAK=0x000F for 16-bit audio).
   - Plot cone_pos bouncing between LEAK and PEAK.
   - Emit LE or BE 16-bit samples based on SDL audio format's
     bit 12.
5. When `note_sampled >= samples_delay`: advance to next note.
   At end: return 0 (silence).

### 29.5.1 Tune playback algorithm details

Each tune `freq[i]` value is a hertz frequency. Each `duration[j]`
is in milliseconds. Sample digitization (`tunFile_play`):

1. Look up note's frequency (X Hz).
2. Compute samples needed for delay: `samples_delay = freq * ms /
   1000` where `freq` here is the SDL audio sample rate (11025).
3. Compute "cone" cycle: `bay = freq / X` samples per wave cycle.
4. Compute step: `speed = (PEAK - LEAK) / bay` (where PEAK=0xFFF0,
   LEAK=0x000F for 16-bit audio).
5. For each sample in `samples_delay`:
   - Update `cone_pos += cone_dir * speed` (clamping bounce).
   - Emit `cone_pos` as 16-bit sample (LE or BE per
     `tun->move_f/move_l`).

The endian flag is set per-platform from SDL's audio format:
```c
if format & 0x1000:           # bit 12 = MSB-first
    move_f = 0
    move_l = 8
else:
    move_f = 8
    move_l = 0
```

Then:
```c
H = (sample >> move_f) & 0xFF
L = (sample >> move_l) & 0xFF
*stream++ = L
*stream++ = H
```

This produces a triangle wave in PCM at the note's frequency.
The sound is the classic PC speaker beep timbre.

### 29.6 Mega Drive ROM (md-rom.c)

Minimal loader for Genesis/MegaDrive ROM image. Hardcoded offsets:

```
VILLAIN_OFFSET = 0x04B04C
CURSOR_OFFSET  = 0x062D6C - 128
TROOP_OFFSET   = 0x000668EC
```

Each troop/villain sprite is 24 sub-tiles of 8×8 @ 4bpp
(`SUBTILE_LEN = 32` bytes). 4 frames × 6 cols × 4 rows = 96 sub-tiles
per sprite = 3072 bytes. Laid out in the ROM as consecutive 4bpp
blocks, which `MD_LoadIMGROW_BUF` unpacks using SDL_BlitXBPP into
a 48×32×4 surface with EGA palette. Color index 0 becomes 0xFF
(transparent).

Only `GR_TROOP` and `GR_VILLAIN` are implemented for MD; everything
else returns NULL (falls through to other modules or fails).

## 30. Font — bitmap glyph format

The game uses a single 8×8 bitmap font for all text rendering.
128 glyphs total (ASCII 0..127). Loaded once at module init and
referenced via the `inprint`/`incolor`/`infont` vendor helpers.

Two paths:

### 30.1 Inline fallback

`src/font.h` contains `inline_font_bits[]` — a 128×64 X-bitmap
(128 pixels wide, 64 tall = 16×8 grid of 8×8 glyphs, LSB-first).
Loaded by `prepare_inline_font()` in the vendor layer. Used before
the module's font is loaded.

### 30.2 Module font

`KB_setfont(env, surface)`: computes `font_size = {w/16, h/8}`
and adopts the 16×8 glyph layout. DOS module's font is loaded
from `KB.CH` (8×8×128). Free module uses `openkb8x8.bmp`.

Glyph character IDs are raw bytes 0..127. Non-printable characters
are used as control codes:

```
\x01 triangle (grid click)
\x05 triangle up
\x0E .. \x15 — box-drawing characters for text frames
  \x0E horizontal top edge
  \x0F horizontal bottom edge
  \x10 top-left corner
  \x11 top-right corner
  \x12 bottom-left corner
  \x13 bottom-right corner
  \x14 vertical left edge
  \x15 vertical right edge
\x16 SYN (sync tick — not printed, used in gamestate keys)
\x18 arrow-up
\x19 arrow-down
\x1A arrow-left
\x1B arrow-right
\x1C backslash (twirl)
\x1D pipe (twirl)
\x1F dash (twirl)
```

## 31. Keybindings

The game maps keyboard keys to actions through "gamestates"
(see §23.1). Each gamestate is a static array of `KBhotspot`
entries; the first matching key triggers an event. Two
parallel maps for movement (arrow keys + numpad) ensure most
keyboard layouts work.

Movement direction encoding:
- Arrow keys: standard 4 directions + Home/End/PgUp/PgDn for
  diagonals.
- Numpad: 1-9 corresponding to compass directions (1=SW, 2=S,
  3=SE, 4=W, 5=center, 6=E, 7=NW, 8=N, 9=NE).

Most action keys are single letters (A, S, U, V, etc.) for ease
of recall. The mappings are per-gamestate; same letter can mean
different things in adventure vs combat.

### 31.1 Adventure state (ARROW_KEYS=18, ACTION_KEYS=17)

`adventure_state` (game.c:4698) — hotspot array (order matters):

| Index | Key           | Action                           |
|-------|---------------|----------------------------------|
| 0-1   | DOWN, 2       | Move down                        |
| 2-3   | LEFT, 4       | Move left                        |
| 4-5   | RIGHT, 6      | Move right                       |
| 6-7   | UP, 8         | Move up                          |
| 8-9   | END, 1        | Move down-left                   |
| 10-11 | PAGEDOWN, 3   | Move down-right                  |
| 12-13 | HOME, 7       | Move up-left                     |
| 14-15 | PAGEUP, 9     | Move up-right                    |
| 16-17 | SPACE, 5      | Rest (or Space/5, unused)        |
| 18    | A             | View Army (KBACT_VIEW_ARMY)      |
| 19    | C             | View Controls                    |
| 20    | F             | Fly                              |
| 21    | L             | Land                             |
| 22    | I             | View Contract                    |
| 23    | M             | View Map                         |
| 24    | P             | View Puzzle                      |
| 25    | S             | Search                           |
| 26    | U             | Use Magic                        |
| 27    | V             | View Character                   |
| 28    | W             | Wait (End Week)                  |
| 29    | Q             | Save and Quit (TRAPSIGNAL)       |
| 30    | Q+Ctrl        | Fast Quit                        |
| 31    | D             | Dismiss Army                     |
| 32    | O             | View Options                     |
| 33    | N             | Navigate to New Continent        |
| 34    | F10           | Cheat                            |
| 35    | SYN tick      | Animation tick                   |

Keybind glyph display uses `KB_KeyLabel(key1, key2)` (ui.c:450),
which renders two alternative keys separated by " or ".

### 31.2 Combat state (COMBAT_ARROW_KEYS=16, COMBAT_ACTION_KEYS=12)

| Index  | Key            | Action                          |
|--------|----------------|---------------------------------|
| 0-1    | HOME, 7        | NW move                         |
| 2-3    | UP, 8          | N move                          |
| 4-5    | PAGEUP, 9      | NE move                         |
| 6-7    | LEFT, 4        | W move                          |
| 8-9    | RIGHT, 6       | E move                          |
| 10-11  | END, 1         | SW move                         |
| 12-13  | DOWN, 2        | S move                          |
| 14-15  | PAGEDOWN, 3    | SE move                         |
| 16     | A              | View Army (COMBAT_VIEW_ARMY)    |
| 17     | C              | Controls                        |
| 18     | F              | Fly                             |
| 19     | G              | Give Up                         |
| 20     | S              | Shoot                           |
| 21     | U              | Use Magic                       |
| 22     | V              | View Character                  |
| 23     | W              | Wait                            |
| 24     | SPACE          | Pass                            |
| 25     | O              | Options                         |
| 26     | F10            | Cheat                           |
| 27     | CLICK grid     | Grid click (filled by setup_grid)|
| 28     | SYN tick       | Animation tick                  |

Combat move keys come in pairs (arrow + numpad). `combat_move_offset_x[9]`
= `{-1,0,1,-1,1,-1,0,1,0}` matches the 8 directions (NW..SE).

### 31.3 Target-picking state (TARGET_ARROW_KEYS=16)

`target_state` (game.c:4591) — same 8 directions, plus:

| Index | Key    | Action                                  |
|-------|--------|-----------------------------------------|
| 16    | RETURN | Confirm selection                       |
| 17    | SYN    | Animation tick                          |

### 31.3.1 KFLAG_TIMER mechanics

Timed hotspots fire after a specified number of milliseconds:

```c
typedef struct KBhotspot {
    SDL_Rect coords;
    union {
        ...
        struct {
            Uint32 resolution;     // total ms before firing
            Uint32 passed;         // accumulated ms
        };
    };
    ...
} KBhotspot;
```

Each call to `KB_event(state)` updates timers:
```c
now = SDL_GetTicks()
passed = now - state->last
state->last = now
for each hotspot in state:
    if hotspot.flag & KFLAG_TIMER:
        hotspot.passed += passed
        if hotspot.passed >= hotspot.resolution:
            hotspot.passed -= hotspot.passed   # reset
            if hotspot.flag & KFLAG_TIMEKEY and !key_held(hot_key):
                continue   # only fire while key held
            return hotspot index + 1   # OR hotspot.hot_key if RETKEY
```

So a hotspot with `resolution = 150 ms` fires every 150ms while
its conditions are met. Used for:
- SYN tick for animations (~150ms = 6.7 fps).
- Auto-repeat for held movement keys (KFLAG_SOFTKEY combines
  TIMER + TIMEKEY).
- Backspace auto-repeat in text input (100ms).

### 31.3.2 Key state tracking

`kbd_state[512]` (ui.c) is an array indexed by SDL keycode:
- 1: key currently held.
- 0: key not held.

Updated on KEYDOWN/KEYUP events. Cleared by `KB_reset(state)`.

KFLAG_TIMEKEY checks this array to enforce "fire only while
held" semantics.

### 31.4 Generic prompt states

- `press_any_key` — any key exits.
- `press_any_key_interactive` — any key exits; SYN tick for
  animation (SOFT_WAIT=150 ms).
- `yes_no_interactive` — Y, N, SYN (SHORT_WAIT=50 ms).
- `yes_no_question` — Y, N (no timer).
- `difficulty_selection` — UP, DOWN, RETURN (RETKEY).
- `enter_string` — BACKSPACE+SOFTKEY (100 ms), ANY+RETKEY, SYN+RETKEY.
- `character_selection` — A/B/C/D, and L at coords (180,8,140,8).
- `module_selection` — UP, DOWN, RETURN, 1..9, 0, MINUS.
- `savegame_selection` — UP, DOWN, RETURN, 1..9.
- `settings_selection` — ANYKEY, UP/DOWN/LEFT/RIGHT/RETURN, 1..9.
- `alphabet_letter` — A..Z + SYN.
- `two_choices` — A, B + 60ms SYN.
- `five_choices` — A..E + 90ms SYN.
- `five_choices_and_space` — A..E, SPACE + 90ms SYN.
- `seven_choices` — A..G + 60ms SYN.
- `cross_choice` — LEFT, UP, DOWN, RIGHT (no SYN).
- `numeric_choices` — 1..9 + 60ms SYN.
- `minimap_toggle` — SPACE only.
- `throne_room_or_barracks` — A, B + SOFT_WAIT SYN.
- `quit_question` — ANYKEY, Q+Ctrl (TRAPSIGNAL).
- `debug_menu` — LEFT, RIGHT, SPACE (RETKEY).

### 31.5 Mouse handling

Hotspots may also be triggered by mouse clicks if the click
falls within `coords`:

- Mouse hover: `state->hover` is updated to the index of the
  hovered hotspot. Used for visual highlighting.
- Mouse click: returns the clicked hotspot's index+1 (or key
  if RETKEY).

For grid-based input (KFLAG_GRID), clicks within the grid
return the cell coordinates via `state->spots[i].grid_x/y`.
This is used for combat field click input.

### 31.6 Hotspot setup helpers

- `KB_imenu(state, id, cols)`: position hotspot `id` at the
  current text cursor position, sized `cols × 1` font cells.
  Used to inline-link menu items with their text.

- `setup_grid(state, x, y, cell_w, cell_h, cols, rows)`:
  configure a KFLAG_GRID hotspot for combat field. Reads grid
  cell coords on click.

- `KB_reset(state)`: clear all timers and key states. Called
  at gamestate transitions.

### 31.7 Multi-key combinations

`hot_mod` byte specifies modifier requirements:
- 0: no modifier required (modifiers ignored).
- KMOD_CTRL: requires Ctrl.
- KMOD_SHIFT: requires Shift.
- KMOD_ALT: requires Alt.

Example: `quit_question` has Q+Ctrl (KMOD_CTRL on SDLK_q) for
fast-quit confirmation.

The check is `if (sp->hot_mod & kbd->mod)` — bitwise AND, so
any matching bit accepts. So a hotspot configured with KMOD_SHIFT
fires on shift+key OR ctrl+shift+key.

### 31.8 Shift handling for text input

For text input (SDLK_RETURN with RETKEY flag), Shift+letter
returns uppercase ASCII via a manual conversion table in
`KB_event` (ui.c:344):

```
Shift + a..z → A..Z (key -= 32)
Shift + 0 → ')'
Shift + 1..5 → !@#$%
Shift + 6 → '^'
Shift + 7 → '&'
Shift + 8 → '*'
Shift + 9 → '('
Shift + ` → '~'
Shift + - → '_'
Shift + = → '+'
Shift + \\ → '|'
Shift + , → '<'
Shift + . → '>'
Shift + / → '?'
Shift + ; → ':'
Shift + ' → '"'
Shift + [ → '{'
Shift + ] → '}'
```

This is hardcoded for US QWERTY. Other layouts get incorrect
results.

## 32. Configuration

The game is configured via three layers (precedence: highest
last):
1. Built-in defaults (compiled-in PKGDATADIR, etc.).
2. INI file (`openkb.ini` in cwd, $HOME, or /etc).
3. Command-line arguments (--flag value).

Config is parsed once at startup and held in the global
`KBconf` struct (see §28.1 KBconfig).

### 32.1 Config file (INI format)

Default location depends on platform:
- **POSIX**: `$HOME/.openkb/openkb.ini`
- **Windows**: `%APPDATA%\OpenKB\openkb.ini`

Also searched in:
1. Current working directory.
2. Home (as above).
3. `/usr/local/etc/openkb.ini`.

Bootstrap content if missing (via `write_default_config`):

```ini
; openkb config file. lines starting with ';' are comments.
[main]
;savedir = 
;datadir = 
autodiscover = 1
[sdl]
sound = 0
fullscreen = 0
filter = normal2x
;[module]
;For open modules, specify the path to the INI and PNG files,
;and set 'free' as type.
;name = Free
;type = free
;path = free/
;[module]
;For DOS module, specify the path to the EXE and CC files,
;and pick from 'dos', 'dos-ega', 'dos-cga' or 'dos-mono'
;for type.
;name = DOS
;type = dos
;path = dos/
```

Section headers `[main]`, `[sdl]`, `[module]` are ignored (just
comments via `[`).

Recognized keys (`read_file_config`):

| Key             | Type     | Applies to           |
|-----------------|----------|----------------------|
| `fullscreen`    | int      | conf->fullscreen     |
| `sound`         | int      | conf->sound          |
| `datadir`       | path     | conf->data_dir       |
| `savedir`       | path     | conf->save_dir       |
| `autodiscover`  | int      | conf->autodiscover   |
| `fallback`      | int      | conf->fallback       |
| `filter`        | enum     | conf->filter: 0 none, 1 normal2x, 2 scale2x |
| `name`          | str      | modules[cur].name    |
| `type`          | enum     | module family and bpp:<br/>`free`/`open` → GNU, bpp=0<br/>`dos`/`dos-vga` → DOS, bpp=8<br/>`dos-ega` → DOS, bpp=4<br/>`dos-cga` → DOS, bpp=2<br/>`dos-mono`/`dos-herc` → DOS, bpp=1<br/>`md`/`sega` → MD, bpp=4 |
| `path`          | path     | modules[cur].slotX_name (cycles A,B,C per encounter) |

Multiple `name`/`type`/`path` triples register additional modules.

### 32.2 Command-line arguments

`read_cmd_config`:

```
--fullscreen           Enable fullscreen
--nosound              Disable sound
--rootdir <path>       Override conf->install_dir
--datadir <path>       Override conf->data_dir
--savedir <path>       Override conf->save_dir
--configdir <path>     Override conf->config_dir
-c <file>              Specify config file
--config <file>        Same
--version              Print version + build flags and exit
```

### 32.3 Environment / defaults

From `read_env_config`:

- `HOME` (Unix) or `APPDATA` (Windows) sets config root.
- Data dir defaults to compiled-in `PKGDATADIR` (typically
  `/usr/local/share/openkb`).
- Save dir is `<config_root>/saves/`.

### 32.4 SDL-init behavior

Video: `320×200 × 32bpp SWSURFACE` by default. With `filter`: `640×400`.
With `fullscreen`: add FULLSCREEN flag; if 400h + fullscreen,
height extended to 480 with pan=40.

Audio (if `sound` enabled): 11025 Hz, 2 channels, 512-sample
buffer, 16-bit signed system-endian via AUDIO_FORMAT.

Window icon loaded from `<install_dir>/icon_32x32.png` (or .bmp
without SDL_image).

### 32.5 KBconfig struct details

```c
typedef struct KBconfig {
    char config_file[PATH_LEN];   // path to active config file
    char config_dir[PATH_LEN];    // dir containing config_file
    char save_dir[PATH_LEN];      // dir for save files
    char data_dir[PATH_LEN];      // dir for module assets
    char install_dir[PATH_LEN];   // dir for install-shared assets

    int fullscreen;                // 0 or 1
    int filter;                    // 0=none, 1=normal2x, 2=scale2x
    int module;                    // active module index
    int autodiscover;              // 0 or 1
    int fallback;                  // 0 or 1
    int sound;                     // 0 or 1

    int set[16];                   // "was this field explicitly set?"
                                   // C_config_file, C_config_dir, etc.
    KBmodule modules[MAX_MODULES]; // up to 16 modules
    int num_modules;
} KBconfig;
```

The `set[]` array tracks which fields were explicitly set vs
defaults. Used by `apply_config(dst, src)` to merge configs:
only set values from `src` overwrite `dst`. So CLI args
override INI which overrides defaults.

### 32.6 Config file precedence

```
read_env_config(KBconf)         // 1. Defaults from env
                                //    (HOME, install_dir from build)

read_cmd_config(CMDconf, argc)  // 2. CLI args (CMDconf is local)

if CMDconf has --config:
    read_file_config(CMDconf)
    apply_config(KBconf, CMDconf)  // CLI takes precedence
else:
    find_config(KBconf)
    if found:
        read_file_config(FILEconf)
        apply_config(KBconf, FILEconf)  // Apply file config

apply_config(KBconf, CMDconf)   // 3. CLI overrides everything
```

Final precedence (highest wins): CLI args > config file > env.

### 32.7 Config search paths

`find_config()` searches in order:
1. `<cwd>/openkb.ini` (current working directory).
2. `<HOME>/.openkb/openkb.ini` (POSIX) or
   `<APPDATA>\OpenKB\openkb.ini` (Windows).
3. `/usr/local/etc/openkb.ini` (or DEFAULT_CONF_DIR).

Returns first existing file. If none found, creates one in
`<HOME>/.openkb/` with `write_default_config`.

### 32.8 Default config behavior

If no config file exists at any search path, `find_config`
attempts to create the HOME directory and a default config:

```ini
; openkb config file. lines starting with ';' are comments.
[main]
;savedir = 
;datadir = 
autodiscover = 1
[sdl]
sound = 0
fullscreen = 0
filter = normal2x
;[module]
;...
```

The default has autodiscover enabled, so the game looks for
modules in the install/data directories without explicit
configuration.

### 32.9 Module config syntax

Modules can be explicitly configured in INI:

```ini
[module]
name = DOS
type = dos
path = data/dos/256.CC#
path = data/dos/416.CC#
path = data/dos/

[module]
name = Free
type = free
path = data/free/
```

Each `[module]` section starts a new module. Subsequent
`name`/`type`/`path` apply to that module. Multiple `path`
lines fill slotA, slotB, slotC in order.

`type` values:
- `free`, `open` → KBFAMILY_GNU, bpp=0
- `dos`, `dos-vga` → KBFAMILY_DOS, bpp=8
- `dos-ega` → KBFAMILY_DOS, bpp=4
- `dos-cga` → KBFAMILY_DOS, bpp=2
- `dos-mono`, `dos-herc` → KBFAMILY_DOS, bpp=1
- `md`, `sega` → KBFAMILY_MD, bpp=4

### 32.10 Save directory layout

Save dir contains `*.DAT` files, one per character:
```
saves/
├── DAN.DAT      # character "Dan"'s save
├── ALICE.DAT
└── BOB.DAT
```

The load_game flow lists all `.DAT` files. Each save file
includes character class/rank, so the picker can show stats.

The save_game function writes to `<savefile>2` (debug suffix).
Production should drop the `2`.

## 33. Multiplayer combat emulator (combat.c)

`combat.c` is a separate binary (`openkb_combat`) that hosts or
joins a TCP session for 2-player combat. NOT integrated with the
main game — it's a standalone proof-of-concept.

The protocol is a length-prefixed packet format over a single
TCP connection. Server is authoritative (sets random match
state, validates moves, handles death/victory). Client mirrors
state via received packets.

`src/combat.c` is a separate binary (`openkb_combat`) that hosts
or joins a TCP session for 2-player combat. Protocol is
length-prefixed packets over a single TCP socket.

### 33.1 CLI

```
openkb_combat --host [PORT]      # or -h
openkb_combat --join HOST [PORT] # or -j
```

Default port: `198995`.

### 33.2 Packet format

```
[word length] [byte id] [args...]
```

The 2-byte length is stored in NETWORK byte order (`SDLNet_Write16`)
and excludes the length field itself. Each packet has an id:

```
PKT_HELLO    = 0    args: string(80) + byte(version)
PKT_CHAT     = 1    args: string(80)
PKT_GOLD     = 2    args: word
PKT_ARMY     = 3    args: byte[5] + word[5]
PKT_READY    = 4    args: byte
PKT_TURN     = 5    args: byte
PKT_WAIT     = 6    args: byte
PKT_PASS     = 7    args: byte(unit_id)
PKT_MOVE     = 8    args: byte(id) byte(ox+3) byte(oy+3)
PKT_FLY      = 9    args: byte + byte + byte
PKT_SHOOT    = 10   args: byte + byte + byte
PKT_CAST     = 11
PKT_MESSAGE  = 12
PKT_DAMAGE   = 13
PKT_DEATH    = 14
PKT_HEAL     = 15
PKT_CLEAN    = 16   args: byte (reset obstacles)
PKT_GRID     = 17   args: byte(y) byte(x) byte(obstacle)
```

Only Hello, Chat, Army, Ready, Turn, Pass, Move, Clean, Grid are
implemented.

### 33.3 Army selection screen

Before combat starts, both sides pick an army:

- 5 dwelling columns (Castle, Plains, Forest, Hill, Dungeon) cycled
  with PgUp/PgDn. Each dwelling shows its 5 troop options (from
  `dwmap[5][5]` — populated from `troops[].dwells`, with Castle
  cycled to be first).
- 5 army slots on the left. Up/Down navigates. Right enters a
  dwelling; Left returns. Enter: add 1 of selected troop (or +1
  if same type already); Enter on empty slot enters dwelling.
- T toggles chat mode (80-char line, backspace, enter to send;
  all chat is broadcast via `PKT_CHAT`).
- F1 signals ready. Ready states: 0=not ready, 1=client-sent-army,
  2=server-acknowledged.
- Server sees opponent's army via `PKT_ARMY` and can `PKT_READY,1`
  to accept.

### 33.4 Match loop

`run_match(war)`:

- `reset_match()` runs on both sides; server sends `PKT_CLEAN`
  and per-obstacle `PKT_GRID` to sync obstacles.
- Client starts with `side=1, your_turn=0`; server starts with
  `side=0, your_turn=1`.
- Each tick (300 ms): advance active unit's animation frame.
- Arrow keys on player's turn → `move_unit(unit_id, ox, oy)` →
  client sends `PKT_MOVE`, server applies locally then confirms
  with its own `PKT_MOVE` echo.
- Space → `combat_pass(unit_id)`. On client, send `PKT_PASS`.
  On server, advance unit turn; if phase ends, swap turns and
  send `PKT_TURN`.
- 'W' → `combat_wait(unit_id)` — unimplemented stub.

Server is authoritative; client mirrors. No confirmation for
dice rolls (cheating possible on server side — source comments
flag this as known limitation).

### 33.5 Rendering

Uses the same `GR_COMTILES` and `GR_TROOP` assets. Background:
server side = black, client side = red. Chat pane at screen
bottom, max 20 lines.

### 33.6 Not integrated with main game

The multiplayer binary uses its own minimalistic loop. There is no
code path from single-player to multiplayer in the main game.c.

### 33.7 Packet handler dispatch

Packets are received in `receive_data()` (combat.c:216):

```c
while have_bytes > 2:
    len = SDLNet_Read16(buffer)        # packet body length
    if have_bytes < len + 2:           # incomplete; wait
        break
    read_data(&buffer[2])              # parse and dispatch
    shift buffer left by len + 2 bytes
    have_bytes -= len + 2
```

`read_data(buf)` reads the packet id (1 byte) and dispatches:

```c
id = READ_BYTE(buf)
pkt = &packets[id]
res = pkt->callback(buf)
return res
```

Each callback parses the packet body per its `format` string
("c"=char, "b"=byte, "w"=word, etc.) and applies the effect.

### 33.8 Packet send mechanics

`send_data(id, ...)`:

1. Reserve 2 bytes for length.
2. Write packet id (1 byte).
3. Walk format string, encoding each arg via varargs:
   - 'c': 1 byte
   - 'b': 1 byte (unsigned)
   - 'w': 2 bytes BE (SDLNet_Write16)
   - 's': 80-byte fixed string (padded with spaces)
4. Write packet length to first 2 bytes (excludes length field).
5. `SDLNet_TCP_Send(rsd, buffer, len)`.

If send fails: post SDL_USEREVENT (code 0xFF) to trigger
disconnect handling.

### 33.9 Authority model

Server is authoritative for:
- Match initialization (obstacles, who goes first).
- Move validation.
- Damage computation.
- Turn order.

Client trusts server's announcements and mirrors state. Without
crypto-based dice verification, server can cheat (random rolls
are server-side). Source comments acknowledge this limitation.

### 33.10 Connection establishment

**Server**:
1. `SDLNet_ResolveHost(NULL, port)` (listen on all interfaces).
2. `SDLNet_TCP_Open(&ip)` (accept loop).
3. `SDLNet_TCP_Accept(sd)` until connection.
4. `SDLNet_TCP_Close(sd)` (close listening socket).

**Client**:
1. `SDLNet_ResolveHost(host, port)`.
2. `SDLNet_TCP_Open(&ip)` (connect).
3. `send_data(PKT_HELLO, "Hello", PACKAGE_VERSION)`.

Both: `SDLNet_AllocSocketSet(1)` for non-blocking sockets.
`SDLNet_TCP_AddSocket(set, rsd)` to monitor the active socket.

### 33.11 Differences from single-player combat

The MP combat loop differs from single-player in:

- No KBgame state — armies are just ids+counts.
- No artifact powers (powers[] = 0).
- No leadership / morale (heroes[] = NULL on both sides).
- No magic (PKT_CAST never implemented).
- No spoils calculation.
- Different combat field rendering (no UI sidebar).
- Server can advance/wait in turns; client only sends moves.

So MP is essentially a stripped-down combat tester rather than
a full multiplayer game. To make it production-ready, it would
need to:
- Send full hero state (artifacts, leadership, etc.) at match
  start.
- Implement spell casting via PKT_CAST.
- Sync rolling RNG between server and client.
- Add reconnection handling.
- Add chat moderation.

Currently it's a proof-of-concept.

## 34. Known bugs and incomplete features

This section documents bugs discovered while writing this spec.
Severity legend:
- **(Game-breaking)**: prevents progression or causes incorrect
  state.
- **(Silent)**: no visible error but produces wrong values.
- **(Aesthetic)**: only affects visuals, no gameplay impact.
- **(Stub)**: function exists but doesn't do its intended job.
- **(Unenforced)**: a check is missing, allowing actions that
  the design intended to gate.

A clean-room reimplementation should address each based on
target faithfulness vs correctness tradeoff.

openkb source explicitly marks or silently contains these issues:

### 34.1 `bury_scepter` coordinate bug (play.c:147) **(Game-breaking)**

```c
game->scepter_x = i;
game->scepter_y = i;   // <-- should be j
```

Both x and y get the same value. Result: Search rarely finds the
scepter unless cheating to force coords. §21.2 applies.

### 34.2 `troop_morale` calculation **(Silent)**

Morale converter `{LOW=1, NORMAL=0, HIGH=2}`. Comparison
`if (morale_cnv[nm] < morale_cnv[morale])` picks the LOWEST
converted value = NORMAL (0). LOW (1) and HIGH (2) both compare
higher and never get adopted if current morale is NORMAL. Mixed
armies default to Normal when they should compute per-chart.

### 34.3 Stub spells **(Stub)**

Half the combat spells are not fully wired:

- **Fireball / Lightning / Turn Undead**: `damage_army()` calls
  `pick_target` but does not invoke `magic_damage` on the picked
  target. The spell is visually "cast" but does no damage. Note:
  `magic_damage(game, war, side, id, base, filter)` exists in
  play.c:1798 and applies `damage = base * spell_power`, with
  `filter != 0` requiring target to have `ABIL_UNDEAD` (for
  Turn Undead) and short-circuiting on `ABIL_IMMUNE`.
- **Resurrect**: `resurrect_army()` picks a target and returns;
  no count restoration is implemented.

`magic_damage()` exists and is correct; it's just not called
from damage_army.

### 34.4 Time Stop increment **(Silent)**

play.c:1790: `game->time_stop += game->spell_power * 10`. The
README notes DOS version does `+= spell_power * 100`. Known bug.

### 34.5 Obstacle generator **(Aesthetic / balance)**

play.c:1248 admits: "Each tile has a 1 in 20 chance [actually 1
in 10 here]... B) That has a FAIR CHANCE of flooding whole level.
//TODO: Fixit ofcourse." Generated obstacles are random and can
block the field heavily.

### 34.6 Combat distance calc **(Silent / non-faithful)**

play.c:1582 notes "DOS version did a different sort of distance
calculation. TODO: fix it to match!" Current code uses integer
Pythagorean distance (`isqrt32(ipow2(dx)+ipow2(dy))`).

### 34.7 Chest "max spell" branch **(Silent)**

See §13.3: `chance_for_spellpower == chance_for_maxspell` so the
`<=` cascade never enters the max_spell branch. "The chest was
empty!" fallback triggers.

### 34.8 Save path "2" suffix **(Silent / debug leftover)**

`save_game` writes to `<savefile>2` instead of `<savefile>`. Comment
says "TODO: Remove this '2', it's for debug purposes only."

### 34.9 `view_contract` continent logic **(Silent)**

Always assumes the contracted villain is on current continent,
even if `KBCASTLE_KNOWN` was set on a different continent. Reads
`continent = game->continent` regardless.

### 34.10 Instant Army slot lookup **(Edge case)**

play.c:1856: picks the first slot that either matches troop_id
OR is empty. If troop_id is in slot 1 and slot 0 is empty, troops
are added to slot 0 with troop_id overwritten. Subtle edge case
in multi-slot armies.

### 34.11 Sidebar draw ordering **(Aesthetic)**

`draw_sidebar` draws tick-animated tiles even when game state
suggests otherwise (e.g., empty contract ticks as if animated).

### 34.12 Network combat sync **(Silent / security)**

See combat.c header comments. Client has no ability to verify
server dice rolls. "As of now, server can cheat to it's heart's
content."

### 34.13 Signposts are counted across continents **(Edge case)**

read_signpost walks ALL continents in order to index the current
signpost. If new signposts are added or order differs, strings
misalign. Works for vanilla maps.

### 34.14 Castle-siege weapons not checked **(Unenforced)**

`siege_weapons` flag is set by purchase and cheat but never
gated in `lay_siege`. Any castle is attackable at any time.

### 34.15 `controls_menu` "memopt" leak **(Silent / memory)**

Memory allocated by `KB_Resolve(DAT_MENUCONTROLS)` is conditionally
freed; on error path may leak.

### 34.16 Rogue furnishing deviates from DOS **(Aesthetic / non-faithful)**

`rogue.c` is openkb's own invention; doesn't match DOS tile
transitions. Header comment: "Those are not part of the original
game mechanics."

### 34.18 Contract activation bug **(Game-breaking)**

`visit_town` "A) Get New Contract" sets:
```c
game->contract = game->last_contract;
```

This stores the cycle *index* (0..4), not the villain id at
that index. The correct expression should be:
```c
game->contract = game->contract_cycle[game->last_contract];
```

Result: only villains 0..4 have correctly working contracts.
Villain 5+ defeat with `game->contract == 5..16` never matches
in `fullfill_contract`, so the bounty is never paid for those
villains. The castle still becomes player-owned and the
"set free" message displays.

This may be a known behavior of openkb that's compensated for in
testing against DOS, but the source-code logic appears broken.

### 34.17 Pre-placed foe troops at spawn **(Edge case)**

For SALT_FRIENDLY tiles, `foe_troops` and `foe_numbers` are
commented out in `salt_continent`. Rolling is deferred to the
`accept_foe` encounter (which calls `roll_creature` fresh). This
means the displayed "type" on the map is a friendly encounter but
the troop variety is randomized per-visit.

## 35. Appendices

Reference tables and supporting data.

### 35.1 Master troop table (flat, 25 rows × 15 columns)

CSV-ready. "Name, SL, HP, MV, Mmin, Mmax, Rmin, Rmax, Ammo,
GCost, Spoils, Abil, Dwells, MaxPop, Growth, MGroup":

```
Peasants,1,1,1,1,1,0,0,0,10,1,0x00,0,250,6,0
Sprites,1,1,1,1,2,0,0,0,15,1,0x01,1,200,6,2
Militia,2,2,2,1,2,0,0,0,50,5,0x00,4,0,5,0
Wolves,2,3,3,1,3,0,0,0,40,4,0x00,0,150,5,3
Skeletons,2,3,2,1,2,0,0,0,40,4,0x80,3,150,5,4
Zombies,2,5,1,2,2,0,0,0,50,5,0x80,3,100,5,4
Gnomes,2,5,1,1,3,0,0,0,60,6,0x00,1,250,5,2
Orcs,2,5,2,2,3,1,2,10,75,7,0x00,2,200,5,3
Archers,2,10,2,1,2,1,3,12,250,25,0x00,4,0,5,1
Elves,3,10,3,1,2,2,4,24,200,20,0x00,1,100,4,2
Pikemen,3,10,2,2,4,0,0,0,300,30,0x00,4,0,4,1
Nomads,3,15,2,2,4,0,0,0,300,30,0x00,0,150,4,2
Dwarves,3,20,1,2,4,0,0,0,350,30,0x00,2,100,4,2
Ghosts,4,10,3,3,4,0,0,0,400,40,0x90,3,25,3,4
Knights,5,35,1,6,10,0,0,0,1000,100,0x00,4,250,3,1
Ogres,4,40,1,3,5,0,0,0,750,75,0x00,2,200,3,3
Barbarians,4,40,3,1,6,0,0,0,750,75,0x00,0,100,3,2
Trolls,4,50,1,2,5,0,0,0,1000,100,0x02,1,25,3,3
Cavalry,4,20,4,3,5,0,0,0,800,80,0x00,4,0,2,1
Druids,5,25,2,2,3,10,0,3,700,70,0x04,1,25,2,2
Archmages,5,25,1,2,3,25,0,2,1200,120,0x05,0,25,2,2
Vampires,5,30,1,3,6,0,0,0,1500,150,0xA1,3,50,2,4
Giants,5,60,3,10,20,5,10,6,2000,200,0x00,2,50,2,2
Demons,6,50,1,5,7,0,0,0,3000,300,0x41,3,25,1,4
Dragons,6,200,1,25,50,0,0,0,5000,500,0x09,2,25,1,3
```

### 35.2 Master continent layout table

Every spawning entity keyed by continent:

**Continent 0 (Continentia)** — 6 villains, home castle + Alcove:

- Castles on continent: 0,2,5,8,A,D,E,F,11,15,16 (11 castles).
- Home castle at (11,7). Alcove at (11,19).
- Entry from sea: (11,3).
- Towns: 0,2,5,8,A,D,E,F,11,15,16 (11 towns).
- Villains 0x00..0x05 placed by `salt_villains`.
- Difficulty tier 0 (easy; uses chance table row 0).

**Continent 1 (Forestria)** — 4 villains:

- Castles: 1,3,9,C,10,18 (6 castles).
- Entry from sea: (1,37).
- Towns: 1,3,9,C,10,18 (6 towns).
- Villains 0x06..0x09.
- Difficulty tier 1 (normal).

**Continent 2 (Archipelia)** — 4 villains:

- Castles: 4,6,7,B,13,17,19 (7 castles). (Wait — actually 19 is
  Zyzzarzaz on cont 3; let me recount.)

Recounting castle cont-field from §11.6:

```
cont 0: 0,2,5,8,A,D,E,F,11,15,16        (11 castles)
cont 1: 1,3,9,C,10,18                    (6 castles)
cont 2: 4,6,7,B,13,17                    (6 castles)
cont 3: 12,14,19                         (3 castles)
```

And towns from §11.7:

```
cont 0: 0,2,5,8,A,D,E,F,11,15,16         (11 towns)
cont 1: 1,3,9,C,10,18                    (6 towns)
cont 2: 4,6,7,B,13,17                    (6 towns)
cont 3: 12,14,19                         (3 towns)
```

11+6+6+3 = 26 each. That matches.

**Continent 2 (Archipelia):** 4 villains.
- Castles: 4,6,7,B,13,17 (6).
- Entry: (14,62).
- Villains 0x0A..0x0D. Difficulty tier 2.

**Continent 3 (Saharia):** 3 villains.
- Castles: 12,14,19 (3).
- Entry: (9,1).
- Villains 0x0E..0x10. Difficulty tier 3.

### 35.3 Contract slot chain (numerical)

On new game:
```
contract_cycle[0] = 0  (first villain, reward 5000)
contract_cycle[1] = 1  (reward 6000)
contract_cycle[2] = 2  (reward 7000)
contract_cycle[3] = 3  (reward 8000)
contract_cycle[4] = 4  (reward 9000)
last_contract = 4      (player starts at top of cycle)
max_contract = 5       (next to advance to when a slot clears)
```

After capturing villain i:
- Its slot in cycle is set to 0xFF.
- Scan `villains[max_contract..16]` for first uncaught; assign to
  that slot; `max_contract++`.

### 35.4 Per-continent dwelling slot allocation

From `continent_dwellings`:

```
Continent 0 (Continentia): 6 fixed dwellings
  slot 0 → Peasants (0x00, Plains)
  slot 1 → Sprites (0x01, Forest)
  slot 2 → Orcs (0x07, Hill)
  slot 3 → Skeletons (0x04, Dungeon)
  slot 4 → Wolves (0x03, Plains)
  slot 5 → Gnomes (0x06, Forest)
  slots 6..10 → random in [0x00, 0x0e]

Continent 1 (Forestria): 6 fixed dwellings
  slot 0 → Dwarves (0x0c, Hill)
  slot 1 → Zombies (0x05, Dungeon)
  slot 2 → Nomads (0x0b, Plains)
  slot 3 → Elves (0x09, Forest)
  slot 4 → Ogres (0x0f, Hill)
  slot 5 → Elves (0x09, Forest) [duplicated!]
  slots 6..10 → random in [0x01, 0x0e]

Continent 2 (Archipelia): 4 fixed dwellings
  slot 0 → Ghosts (0x0d, Dungeon)
  slot 1 → Barbarians (0x10, Plains)
  slot 2 → Trolls (0x11, Forest)
  slot 3 → Druids (0x13, Forest)
  slots 4..10 → random in [0x02, 0x0e]

Continent 3 (Saharia): 5 fixed dwellings
  slot 0 → Giants (0x16, Hill)
  slot 1 → Vampires (0x15, Dungeon)
  slot 2 → Archmages (0x14, Plains)
  slot 3 → Dragons (0x18, Hill)
  slot 4 → Demons (0x17, Dungeon)
  slots 5..10 → random in [0x14, 0x18]
```

Continent 1 has Elves listed twice (slot 3 and 5) in the source
table — probably a typo but preserved as-is by openkb.

### 35.5 Castle difficulty summary table

```
Difficulty 0 (castle tiers rolled with table row 0):
  0, 2, 5, 8, 0xA, 0xD, 0xE, 0xF, 0x10 (wait — that's per-castle,
  not per-continent...)
```

Per-castle difficulty (§11.10) by castle index:

| Tier | Castle indices                         |
|------|----------------------------------------|
| 0    | 0,2,5,8,A,D,E,F,10,11,15,16,17         |
| 1    | 1,3,9,C,11 (err, 11 is tier 0)         |
| 2    | 4,6,7,B,13,17                          |
| 3    | 12,14,19                               |

Exact list from source:
```
castle_difficulty[26] = {
    0, 1, 0, 1, 2, 0, 2, 2, 0, 1,    // 00-09
    0, 2, 1, 0, 0, 0, 1, 0, 3, 2,    // 0A-13
    3, 0, 0, 2, 1, 3                 // 14-19
};
```

Tier 3 = 0x12 (Spockana), 0x14 (Uzare), 0x19 (Zyzzarzaz) — all
Saharia castles. Tier 0 is the majority (including home-continent
castles).

### 35.6 Town-to-spell mapping

At spawn, Hunterville (town 0x15) is hardcoded to Bridge (spell 7).
The other 25 towns receive 13 unique spell ids (all spells except
Bridge) + random fills for the remainder. Order is randomized, so
the exact assignment varies per game.

Players can check which spell a town sells via the "Gather
Information" menu's "Buy spell (D)" option.

### 35.7 Complete `options[]` persistence map

```
options[0] delay     → save offset 0x0046
options[1] sounds    → save offset 0x0048
options[2] walk beep → save offset 0x0049
options[3] animation → save offset 0x004A
options[4] army size → save offset 0x004B
options[5] CGA       → save offset 0x0055
```

Note: `difficulty` is written at offset 0x0047 between options[0]
and options[1] — an artifact of the original DOS layout.

### 35.8 Keymap summary by action

| Action            | Adventure key      | Combat key       |
|-------------------|--------------------|------------------|
| Move              | 2/4/6/8 + arrows   | 1-9 numpad/arrows|
| View army         | A                  | A                |
| Controls          | C                  | C                |
| Fly               | F                  | F                |
| View contract     | I                  | —                |
| View map          | M                  | —                |
| View puzzle       | P                  | —                |
| Search            | S                  | —                |
| Shoot             | —                  | S                |
| Use magic         | U                  | U                |
| View character    | V                  | V                |
| Wait / End week   | W                  | W                |
| Save + quit       | Q                  | —                |
| Fast quit         | Ctrl+Q             | —                |
| Dismiss           | D                  | —                |
| Options           | O                  | O                |
| Navigate          | N                  | —                |
| Give up           | —                  | G                |
| Pass              | —                  | Space            |
| Land              | L                  | —                |
| Cheat menu        | F10                | F10              |
| Cancel/escape     | Esc                | Esc              |

### 35.9 Tileset layout (DOS, 8-wide assembled grid)

`KB_LoadTileset_TILES` (kbres.c:328) builds an 8-wide × 9-row
tileset surface (72 tiles total) by calling `resolve(GR_TILE, i)`
for `i = 0..71` and blitting each into row=`i/8`, col=`i%8`.

`KB_DrawMapTile(dest, dest_rect, tileset, m)` then renders tile id
`m` (after `& 0x7F` masking) by computing source coordinates as:
`th = m / 8; tw = m % 8`. So tile id 0 is top-left of the
assembled tileset, tile id 71 is the last cell.

Tile-id ranges are constrained by the IS_* predicates (§12.2),
which establish *semantic* groups (water 0x14..0x20, trees
0x21..0x2D, desert 0x2E..0x3A, rock 0x3B..0x47). The exact
graphical contents of each cell are determined by the module's
asset files (`tileseta.4`/`.16`/`.256` for tiles 0..35, and
`tilesetb.*` for tiles 36..71). For interactive tiles
(0x80..0xFF), only the low 7 bits index the tileset, so e.g.
tile id 0x85 (castle) draws cell index 5; tile 0x8B (chest)
draws cell index 11; tile 0x91 (foe) draws cell index 17. The
tileset image's exact cell content is module-defined and **not**
specified by the engine — a clean-room re-implementation must
either ship its own tileset matching these indices, or load DOS
assets and trust the same indexing.

Note: §35.10 below describes how 3 of these cells (17, 18, 19)
are dynamically swapped with continent-specific salt tiles.

Interactive tile mapping (high bit 0x80):

```
0x80  grass alt          → displayed as tile 0
0x85  castle             → tile 0x85 & 0x7F = 5 (castle graphic)
0x8A  town               → tile 0x8A & 0x7F = 10
0x8B  chest              → tile 0x8B & 0x7F = 11
0x8C  dwelling plains    → 12
0x8D  dwelling forest    → 13
0x8E  dwelling hill/cave → 14 (also telecave)
0x8F  dwelling dungeon   → 15
0x90  signpost           → 16
0x91  foe                → 17
0x92  artifact 1         → 18
0x93  artifact 2         → 19
```

### 35.10 `tilesalt` mapping

For `GR_TILESALT` per continent:

- Full set (sub_id=0): 9 replacement tiles in a row.
- Per continent (sub_id 1..3): 3 replacement tiles, positions
  17, 18, 19 in the tileset get overwritten via
  `KB_LoadTilesetSalted`.

This swaps the "water-texture" or "grass-texture" at specific
tile indices so e.g. Forestria's water looks different from
Saharia's water even though the game uses the same tile id.

### 35.11 Combat action dispatch table

From `combat_state` key map and dispatch in `combat_loop`:

```
key index   KEY_ACT(COMBAT_*)      action called
---------   ---------------------  ---------------------------
1..16       (arrow movement)       move_unit(combat, 0, id, ox, oy)
17          VIEW_ARMY              view_army(game)
18          VIEW_CONTROLS          (in-line redraw; no-op)
19          FLY                    unit_try_fly(combat)
20          GIVE_UP                ask_giveup(game); if yes, done=2
21          SHOOT                  unit_try_shoot(combat)
22          USE_MAGIC              choose_spell(game, combat)
23          VIEW_CHAR              view_character(game)
24          WAIT                   pass = unit_try_wait(combat)
25          PASS                   unit_try_pass(combat)
26          VIEW_OPTIONS           combat_options_menu(game) loop
27          CHEAT                  debug_cheat_menu(game, combat)
28          CLICK                  grid click → grid_heuristic
SYN_EVENT   (anim tick)            advance frame, AI turn if applicable
```

### 35.12 Adventure action dispatch table

```
key index   KEY_ACT(KBACT_*)       action called
---------   ---------------------  ---------------------------
1..18       (arrow movement)       move logic (see §15.1)
19          VIEW_ARMY              view_army(game)
20          VIEW_CONTROLS          options_menu redraw loop
21          FLY                    set mount=FLY if can_fly
22          LAND                   set mount=RIDE if grass
23          VIEW_CONTRACT          view_contract(game)
24          VIEW_MAP               view_minimap(game, 0)
25          VIEW_PUZZLE            view_puzzle(game)
26          SEARCH                 ask_search(game, &weekend)
27          USE_MAGIC              choose_spell(game, NULL)
28          VIEW_CHAR              view_character(game)
29          END_WEEK               spend_week(game); weekend=1
30          SAVE_QUIT              ask_quit_game(game)
31          FAST_QUIT              ask_fast_quit(game)
32          DISMISS_ARMY           dismiss_army(game)
33          VIEW_OPTIONS           options_menu(game) loop
34          NEW_CONTINENT          navigate_continent(game)
35          CHEAT                  debug_cheat_menu(game, NULL)
SYN_EVENT   (anim tick)            tick; frame advance
```

### 35.13 Packed tile color lookup for COL_MINIMAP

```
for tile in 0..255:
    if      IS_GRASS(tile):      color = GREEN
    elif    IS_DEEP_WATER(tile): color = DBLUE
    elif    IS_WATER(tile):      color = BLUE
    elif    IS_DESERT(tile):     color = YELLOW
    elif    IS_ROCK(tile):       color = BROWN
    elif    IS_TREE(tile):       color = DGREEN
    elif    IS_CASTLE(tile):     color = WHITE
    elif    IS_MAPOBJECT(tile) or IS_INTERACTIVE(tile):
                                  color = DRED
    else:                         color = WHITE (default array init)

colors[0xFF] = BLACK  # fog color
```

This is a 256-entry array per call to `KB_Resolve(COL_MINIMAP, 0)`,
built at every minimap open.

### 35.14 Continent colors in astrology

Astrology banner calls `end_week` which returns the week's troop
id. That id is displayed via `troops[id].name`. Week 0, 4, 8, 12,
... (every 4th week) are hardcoded to Peasants (id 0). Other
weeks pick `KB_rand(1, 24)`.

### 35.15 Spell dispatch in `choose_spell`

Full switch block reproduced here for clarity (game.c:4222):

```c
switch (spell_id) {
  case 0:  clone_army(game, combat);           break;
  case 1:  teleport_army(game, combat);        break;
  case 2:  damage_army(game, combat, 25,  2);  break; // Fireball
  case 3:  damage_army(game, combat, 10,  3);  break; // Lightning
  case 4:  freeze_army(game, combat);          break;
  case 5:  resurrect_army(game, combat);       break;
  case 6:  damage_army(game, combat, 50,  6);  break; // Turn Undead
  case 7:  build_bridge(game);                 break;
  case 8:  time_stop(game);                    break;
  case 9:  find_villain(game);
           draw_map(game, 0);
           draw_sidebar(game, 0);
           view_contract(game);                break;
  case 10: select_gate(game, 0);               break; // Castle Gate
  case 11: select_gate(game, 1);               break; // Town Gate
  case 12: instant_army(game);                 break;
  case 13: raise_control(game);                break;
}
game->spells[spell_id]--;
if (combat) combat->spells++;
```

### 35.16 RNG sources summary

- libc `rand()` / `srand()` — only seeded via cheat menu 'R'.
- All `KB_rand(min,max)` calls go through libc.

Used for:
- Scepter key (1 byte).
- Scepter continent bias.
- Grass tile count.
- Villain placement among castles.
- Dwelling troop roll (random continents).
- Chest content (gold/commission/spell/max_spell/new spell).
- Gold amount, commission amount, spell amount.
- End-week creature pick (non-Peasant weeks).
- Combat: Scythe 10% chance, melee damage, ranged damage (non-MAGIC).
- Obstacle generation in combat field.
- Blinking you-are-here pixel in minimap.
- Friendly foe roll at encounter.
- Random troop for home castle barracks flavor.
- Random troop for town/dwelling background flavor.

### 35.17 Resource load order on game start

`run_game(conf)` in game.c:

1. `KB_startENV(conf)` — init SDL, open audio.
2. `discover_modules(install_dir, conf)`; then `discover_modules(data_dir, conf)`.
3. `register_modules(conf)` — no-op in current code.
4. `select_module()` — pick one.
5. `init_modules(conf)` — e.g. `DOS_Init` loads palette + tunes.
6. Load font: `KB_setfont(sys, SDL_LoadRESOURCE(GR_FONT, 0, 0))`.
7. `prepare_resources()`:
   - Load all 5 frame rects (top/left/right/bottom/bar).
   - Load `GR_SELECT, 1` as middle-frame border.
   - Load tile-rect and side-tile-rect.
   - Load message_colors = `COL_TEXT, CS_GENERIC`.
   - Load status_colors = `COL_TEXT, CS_STATUS_2`.
   - Update UI frame coords.
8. `display_logo()` and `display_title()`.
9. `select_game(conf)`:
   - Blits title, waits for A/B/C/D/L.
   - On A-D: `create_game(class)`, which calls `spawn_game` +
     `furnish_map`. After spawn, `refill_rules()` and
     `refill_names()` are invoked before `spawn_game` (game.c:468)
     so modules can override data tables.
   - On L: `load_game()` via savefile picker.
10. Set window title to `<Name> the <Title> - openkb <ver>`.
11. Update UI colors by difficulty (`update_ui_colors(game)`).
12. Enter `adventure_loop(game)`.
13. On loop exit: `free_resources()`, `stop_modules(conf)`,
    `KB_stopENV(sys)`.

### 35.18 Refill-from-module tables

`refill_rules()` (game.c:248) queries the resolver for every
data-table resource:

```
DAT_SKILLS/MOVES/MELEEMIN/MAX/RANGEMIN/MAX/SHOTAMMO/HPS/GCOST/
SPOILS/DWELLS/MGROUP/MAXPOP/GROWTH/ABILS
WDAT_VREWARD/VNUMBER/SCOST
DAT_VTROOP
DAT_CASTLE{X,Y,C}, DAT_TOWN{X,Y,C,INV}, DAT_BOAT{X,Y},
  DAT_GATE{X,Y}, DAT_NAV{X,Y}, DAT_SPECIAL{X,Y,C}
per class: WDAT_COMM, WDAT_LDRSHIP, DAT_VNEED, DAT_KMAGIC,
  DAT_SPOWER, DAT_MAXSPELL, DAT_FAMILIAR
```

If the module returns non-NULL for any, the corresponding
compile-time static (§5–§11) is overwritten. DOS module only
returns values for `WDAT_VREWARD`, so villain rewards are the
one table that's always reloaded from KB.EXE.

`refill_names()` (game.c:215) overwrites:

```
continent_names[4]
town_names[26]
castle_names[26+1 — the extra is "of King Maximus"]
troops[25].name
spell_names[14]
```

### 35.19 Combat UID packing

```
PACK_UID(side, id)    = side * 5 + id + 1    // 1..10
UID_AS_SIDE(uid)      = uid > 5 ? 1 : 0
UID_AS_ID(uid)        = uid > 5 ? uid - 6 : uid - 1
```

UID 0 means empty tile. Player side stacks occupy UIDs 1..5;
AI side stacks occupy UIDs 6..10. `umap` stores these values
at each grid cell.

### 35.20 Hotspot flag reference

```
KFLAG_ANYKEY    (0x01)  Match any keycode
KFLAG_RETKEY    (0x02)  Return the pressed key rather than hotspot index
KFLAG_TRAPSIGNAL(0x08)  Intercept SDL_QUIT
KFLAG_TIMER     (0x10)  Fire on timer expiry
KFLAG_TIMEKEY   (0x20)  Timer fires only while key is held
KFLAG_GRID      (0x80)  Treat area as grid; click sets grid_x/y
```

Combining `TIMER | TIMEKEY` (`KFLAG_SOFTKEY`) = auto-repeat.

### 35.21 Glyph byte codes used in-game

From various UI calls and strings:

```
\x01   grid click indicator
\x05   twirl up (used as /)
\x0E   frame top-edge
\x0F   frame bottom-edge
\x10   frame top-left
\x11   frame top-right
\x12   frame bottom-left
\x13   frame bottom-right
\x14   frame left-edge
\x15   frame right-edge
\x16   SYN event marker
\x18   arrow up
\x19   arrow down
\x1A   arrow left
\x1B   arrow right
\x1C   backslash (twirl \)
\x1D   pipe (twirl |)
\x1F   dash (twirl -)
```

Twirl animation sequence: `"\x1D\x05\x1F\x1C"` → displayed as
`|`, `/`, `-`, `\` rotating.

### 35.22 Fingerprint detection table

For auto-discovery. Each row is a `KBfileid`:

```
"free"      ""  dirsize  ""                                    0x00  0x00  GNU,DIR    -1
"416"       CC  0        ""                                    0x00  0x00  DOS,GROUP   1
"256"       CC  0        ""                                    0x00  0x00  DOS,GROUP   0
"KB"        EXE 0        "Copyright (C) 1990-95 New World..."   46  0x15EA5  DOS,EXE    2
"KB"        EXE 79839    ""                                    0x00  0x00  DOS,EXE     2
""          BIN 0        "SEGA"                                  4  0x100    MD,ROM     -1
```

- `companion=-1` — standalone
- `companion=0` — secondary CC (256)
- `companion=1` — primary CC (416)
- `companion=2` — KB.EXE

Auto-discovery registers:

```
For GNU fingerprints: 1 "Free" module.
For DOS fingerprints: with EXE + primary + secondary → 4 modules:
  - "DOS (VGA)"     bpp=8, slotA=256.CC#, slotB=416.CC#
  - "DOS (EGA)"     bpp=4, slotA=416.CC#
  - "DOS (CGA)"     bpp=2, slotA=416.CC#
  - "DOS (Hercules)"bpp=1, slotA=416.CC#
For MD fingerprints: 1 "MegaDrive" module.
```

### 35.23 `.DAT` save file field count verification

Total bytes by §22:

```
Name (11)                      +=   11
Header (4)                     +=    4   →   15
Villain_caught (17)            +=   17   →   32
Artifact_found (8)             +=    8   →   40
Continent_found (4)            +=    4   →   44
Orb_found (4)                  +=    4   →   48
Spells (14)                    +=   14   →   62
Misc (3)                       +=    3   →   65
Player_troops (5)              +=    5   →   70
Option+diff+4 opts (6)         +=    6   →   76
Loc (9)                        +=    9   →   85
CGA (1)                        +=    1   →   86
Town_spell (26)                +=   26   →  112
Contract cycle (5)             +=    5   →  117
last_contract, max_contract    +=    2   →  119
steps, unknown                 +=    2   →  121
Castle_owner (26)              +=   26   →  147
Castle_visited (26)            +=   26   →  173
Town_visited (26)              +=   26   →  199
Scepter enc (3)                +=    3   →  202
Fog (2048)                     += 2048   → 2250
Castle_troops (130)            +=  130   → 2380
Foe_coords friendly (40)       +=   40   → 2420
Navmap coords (6)              +=    6   → 2426
Orb coords (8)                 +=    8   → 2434
Teleport coords (16)           +=   16   → 2450
Dwelling coords (88)           +=   88   → 2538
Hostile foe coords (280)       +=  280   → 2818
Hostile foe troops (420)       +=  420   → 3238
Hostile foe numbers (420)      +=  420   → 3658
Dwelling_troop (44)            +=   44   → 3702
Dwelling_population (44)       +=   44   → 3746
Scepter key (1)                +=    1   → 3747
Base_leadership, leadership,
  commission, killed (8)       +=    8   → 3755
Player_numbers (10)            +=   10   → 3765
Castle_numbers (260)           +=  260   → 4025
time_stop, days_left, score
  (6)                          +=    6   → 4031
unknown1, unknown2 (2)         +=    2   → 4033
gold (4)                       +=    4   → 4037
Map dump (16384)               += 16384  → 20421
```

Matches `DAT_SIZE = 20421`. ✓

### 35.24 `troop_numbers[24][3]` — Dragons-per-difficulty edge case

Dragons (id 24, 0x18) have `troop_numbers[24] = {1, 1, 1, 2}` —
only on Impossible? will more than 1 dragon be rolled. Even on
Impossible?, the rolled count gets `if (count <= 1) count = 2;`
treatment, so a Dragon encounter is always ≥ 2 dragons.

### 35.25 `spawn_game` vs. `furnish_map` ordering

`spawn_game` creates the game (salts map, places foes, hides
scepter, etc.). Immediately after, `create_game` in game.c calls
`furnish_map(game)` to apply tile transitions — but this mutates
interior map tiles (water/tree/sand/rock edges). Salted objects
(chests, dwellings) are preserved because they're interactive
(high-bit set) and `furnish_map` early-exits on interactive tiles.
However, any nearby grass/water tile that was "raw" now becomes
a border/corner variant, which affects visual presentation only.

### 35.26 Flight eligibility check in `player_can_fly`

```c
for i in 0..4:
  if player_numbers[i] == 0: break
  if !(troops[troop[i]].abilities & ABIL_FLY):
    log "Can't fly because <Troop> do not fly"
    return 0
  if troops[troop[i]].skill_level < 2:
    log "Can't fly because <Troop> are not skilled enough"
    return 0
return 1
```

Troops with ABIL_FLY and skill_level ≥ 2:
- Sprites (SL=1) → **cannot** (skill too low)
- Ghosts (SL=4) → can
- Archmages (SL=5) → can
- Vampires (SL=5) → can
- Demons (SL=6) → can
- Dragons (SL=6) → can

So the "flight team" cheat preset (Sprites, Archmages, Vampires,
Demons, Dragons) actually fails because of Sprites.

### 35.27 Combat dispatch shortcut ("click grid")

When CLICK event fires in combat:

1. Read `combat_state.spots[CLICK].grid_x/y`.
2. Call `grid_heuristic(combat, side, unit_id, x, y, &uid, &nx,
   &ny)`.
3. On returned intent:
   - MOVE or MELEE: translate `(nx, ny)` delta into arrow-key
     equivalent by searching `combat_move_offset_x/y[9]`; emit that
     key through the normal action path.
   - WAIT: emit `KEY_ACT(WAIT)`.
   - FLY: directly call `fly_unit(…, nx, ny)`.
   - SHOOT: call `unit_ranged_damage(…, uid)`.
   - CANT_SHOOT / CANT_FLY: show error banner.
   - NONE: no-op.

### 35.28 Strings file organization in DOS KB.EXE

Sorted by file offset, order as they appear in KB90.EXE:

```
0x15690  DATA SEGMENT start
0x15E6D  Credits (203 bytes)
0x16D19  VDESCs (17 × ~318 bytes)
0x1873A  VRewards (17 × 2 bytes = 34 bytes)
0x189D1  Tune notes palette (176 bytes)
0x18A81  Tune delay palette (32 bytes)
0x18AA1  Tune pointers (10 × 2 = 20 bytes)
0x18EDF  Villain names (294 bytes)
0x19005  Signs (1610 bytes)
0x19650  Artifact descriptions (849 bytes)
0x1ADEE  Game won ending (303 bytes)
0x1AF1D  Game lost ending (304 bytes)
```

Each of these regions is a `\0`-separated list, unchanged on
read. Some have per-line padding (see §25.1).

### 35.29 Artifact power-check at `take_artifact`

Applied ONCE at pickup, not per-encounter:

```c
if (power & POWER_DOUBLE_LEADERSHIP):
  base_leadership *= 2
  leadership = base_leadership
if (power & POWER_DOUBLE_SPELL_POWER):
  spell_power *= 2
if (power & POWER_INCREASE_COMMISSION):
  commission += 2000
if (power & POWER_DOUBLE_MAX_SPELLS):
  max_spells *= 2
```

Other powers (INCREASED_DAMAGE, QUARTER_PROTECTION,
CHEAPER_BOAT_RENTAL, UNKNOWN_XXX1) are queried live via
`has_power(game, POWER_X)` at each relevant moment.

### 35.30 `has_power(game, power)` implementation

```c
int has_power(KBgame *game, byte power) {
  for i in 0..7:
    if game->artifact_found[i] && (artifact_powers[i] & power):
      return 1;
  return 0;
}
```

Linear scan every call. `artifact_powers[]` is the constant table
in §9.

### 35.31 `army_leadership(game, troop_id)` formula

```c
free_leadership = game->leadership
for i in 0..4:
  if player_troops[i] == troop_id:
    free_leadership -= troops[troop_id].hit_points * player_numbers[i]
    break
return free_leadership
```

Used as the recruit ceiling: `max = army_leadership(game, t_id) /
troops[t].hp`. Zero or negative max means no more of this troop
can be taken without exceeding leadership.

`unit_under_control(war, side, id)` — returns 1 if
`army_leadership(war.heroes[side], troop_id) > 0`. Called in
combat context; includes the unit itself in the subtraction.

### 35.32 Combat spoils accumulation

At match setup (`prepare_units_*`):

```c
spoils[side] += troops[troop_id].spoils_factor * 5 * count
```

After player win (`run_combat` mode 0 or 1):
- `game->gold += combat.spoils[1]`  (AI side's spoils).

### 35.33 Boat cost resolution

```c
word boat_cost(KBgame *game) {
  if (has_power(game, POWER_CHEAPER_BOAT_RENTAL)) return COST_BOAT_CHEAP;
  return COST_BOAT_EXPENSIVE;
}
```

COST_BOAT_CHEAP = 100, COST_BOAT_EXPENSIVE = 500. Without the
Anchor, weekly upkeep is 500; with it, 100.

### 35.34 End credits structure

`DOS_read_credits` reformats the raw strings:

- Lines containing "copyright" (case-insensitive): centered
  (padded with spaces to 30-char center).
- Lines ending in ':' — titles, newline separator before.
- Other lines — indented 2 spaces.

The STRL_CREDITS is consumed by `show_credits()` during title
screen and rendered in a MessageBox with the user picture (loaded
from GR_SELECT, 2) pasted to the right.

### 35.35 SDL zoom and scale2x

`conf->filter`:
- 0: no filter, 1x scale.
- 1: normal2x (nearest-neighbor doubling).
- 2: scale2x (AdvancedMAME filter; smoother pixel art).

Applied at `SDL_LoadRESOURCE` and to the main screen.
`SDL_Scale2X_Surface` calls `scale2x()` from `vendor/vendor.h`.

### 35.36 Default UI panel layout (in pixels)

Calculated in `update_ui_frames` from loaded RECT_UI and frames:

```
status.x = 16         (left_frame.w)
status.y = 8          (top_frame.h)
status.w = 288        (screen.w - left.w - right.w)
status.h = 9          (font.h + zoom)

map.x    = 16
map.y    = 22         (top.h + status.h + bar.h)
map.w    = 272        (screen.w - purse.w - right.w - left.w)
map.h    = 170        (screen.h - top.h - bar.h - status.h - bottom.h)
```

(With 320×200 resolution; 2x mode doubles all values.)

### 35.37 Complete list of filename-based resources (DOS)

Assembled realname = `<base><bpp_suffix><ident>`:

```
bpp_suffix = ".4" (CGA/Hercules), ".16" (EGA), ".256" (VGA)
ident = "#<n>" for indexed subfiles, or "" for standalone

nwcp.4       / nwcp.16       / nwcp.256       #0
title.4      / title.16      / title.256      #0
select.4     / select.16     / select.256     #0..#2
endpic.4     / endpic.16     / endpic.256     #0..#4
KB.CH  (bpp=1 font, 1024 bytes)
tileseta.4/.16/.256  #0..#35
tilesetb.4/.16/.256  #0..#35
tilesalt.4/.16/.256  (9 tiles inline)
cursor.4/.16/.256    (28 frames: 16 hero + 13 UI)
comtiles.4/.16/.256  (15 frames)
view.4/.16/.256      (14 frames: 8 artifacts + 4 continents + 2 empty)

per troop (25):
peas.4/.16/.256 spri.4 mili.4 wolf.4 skel.4 zomb.4
gnom.4 orcs.4 arcr.4 elfs.4 pike.4 noma.4 dwar.4
ghos.4 kght.4 ogre.4 brbn.4 trol.4 cavl.4 drui.4
arcm.4 vamp.4 gian.4 demo.4 drag.4

per villain (17):
mury.4 hack.4 ammi.4 baro.4 drea.4 cane.4 mora.4 barr.4
barg.4 rina.4 ragf.4 mahk.4 auri.4 czar.4 magu.4 urth.4 arec.4

per class (4):
knig.4 pala.4 sorc.4 barb.4

per location (6):
cstl.4 town.4 plai.4 frst.4 cave.4 dngn.4

Misc:
MCGA.DRV (palette, inside 256.CC#)
LAND.ORG (world map bytes)
```

All DOS assets (except MCGA.DRV, LAND.ORG, KB.EXE, KB.CH) live
inside either `256.CC` or `416.CC` archives and are accessed via
the virtual path format `<archive>#<filename>`.

### 35.38 `.CCL` filename list file

When a `.CC` archive is opened, openkb tries to open a sibling
`.CCL` file containing one filename per line. Matched by
`KB_ccHash` to the archive's internal keys. If present, it lets
openkb display archive contents by name rather than by numeric
hash. Format: plain text, one filename per line, up to 16 chars.

### 35.39 Virtual filesystem — `KB_DIR` types

```
KBDTYPE_NULL   = 0x00   // used by KB_File buf-type placeholders
KBDTYPE_DIR    = 0x01   // real directory
KBDTYPE_GRPCC  = 0x02   // CC archive
KBDTYPE_GRPIMG = 0x03   // IMG archive (.4/.16/.256)
```

```
KBDTYPE_BUF    = 0x00   // in-memory buffer (not a dir)
KBFTYPE_FILE   = 0x01   // real file
KBFTYPE_INCC   = 0x02   // entry inside CC
KBFTYPE_INIMG  = 0x03   // entry inside IMG
```

Path separator is `/`; archive separator is `#`. Example virtual
path: `data/dos/256.CC#MCGA.DRV` — opens 256.CC as a CC
directory, then opens MCGA.DRV inside it.

`KB_fopen_in` walks the path recursively, opening each archive
via `KB_DIR_IF_REP_ADD` driver callbacks until the terminal file
is reached.

### 35.39.1 Path traversal

The virtual filesystem supports nested paths via `/` (directory
sep) and `#` (archive sep):

```
data/dos/256.CC#MCGA.DRV
│   │   │       │
│   │   │       └─ entry inside CC archive
│   │   └─ archive separator
│   └─ subdirectory
└─ root
```

`KB_opendir_in("data/dos/256.CC#MCGA.DRV", NULL)`:
1. Walk path, find first separator: '/' at position 4.
2. `KB_opendir_in("data", NULL)` → real directory.
3. Recurse: `KB_opendir_in("dos/256.CC#MCGA.DRV", &data_dir)`.
4. Continue path-walking until terminal element.

Each level is opened with the appropriate driver based on
extension or context:
- `.cc` → KBDTYPE_GRPCC (CC archive).
- `.4`/`.16`/`.256` → KBDTYPE_GRPIMG (IMG archive).
- Anything else (or directory) → KBDTYPE_DIR (real dir).

### 35.40 Wrap-up

This specification covers openkb as of the code in
`/home/danheskett/personal/openkb-reference/src/` — a complete
description of every rule, table, value, and UI element needed
to clean-room reimplement the engine. Known deviations from DOS
*King's Bounty* (as flagged in source comments) are documented
in §34; faithful implementers should consult the DOS binary or
a canonical reference (e.g. the `openkb` repository's own parity
docs, or the game manual) to resolve ambiguities.

### 35.41 Strings sourced from KB.EXE — limitation

§24 documents every string hardcoded in openkb's C source.
§25 documents *where* in `KB.EXE` the dynamic string content
lives — but the actual character bytes are inside the proprietary
DOS binary and are not reproduced in this spec. A clean-room
reimplementation has three options:

1. Ship its own original strings for villains, signs, artifacts,
   credits, win/lose endings (matching the byte-offset structure
   if compatibility with DOS save/asset files is desired).
2. Implement only the GNU-module asset paths (text files
   `signs.txt`, `endwin.txt`, `endlose.txt`, INI keys per
   villain/town/etc — see §28.1 GNU module rules in `free-data.c`
   for resolution). This is the cleanly-licensed path.
3. Read from a user-provided KB.EXE at runtime, as openkb does.
   This requires the user to own the original game.

The byte-level offsets in §25 are the contract for option 3.
Option 2's INI/text-file structure (per `free-data.c`) is the
contract for option 1/2. The strings themselves are absent from
this spec by design; consult the original game for canonical
DOS text, or see `legacy/manuals/` in the openbounty repo for
extracted reference.

### 35.42 Verification audit summary

The author cross-checked the following tables against bounty.c
and save.c after initial drafting:

- `troop_numbers[25][4]` — verified row-by-row at bounty.c:726;
  all 100 cells match §13.2.
- `castle_coords[26][3]` — verified at bounty.c:593; per-continent
  totals (11/6/6/3 = 26) confirmed against §35.2.
- Save file offsets — verified by walking the pointer through
  `KB_loadDAT` (save.c:45-269); cumulative position confirmed at
  `0x4FC5 = 20421` matching `DAT_SIZE`.
- `troop_chance_table[4][4]` — verified at bounty.c:710.
- `dwelling_to_troop[4][5]` — verified at bounty.c:716.
- `villain_rewards[17]` — verified at bounty.c:227.
- `villain_army_troops[17][5]` and `villain_army_numbers[17][5]`
  — verified at bounty.c:247 and bounty.c:266.
- `continent_dwellings[4][11]` — verified at bounty.c:754.
- `town_inversion[26]` — verified at bounty.c:360.
- `puzzle_map[5][5]` — verified at bounty.c:768.

Tables not independently re-verified line-by-line:

- `morale_chart[5][5]` — read once during initial pass; values in
  §8.3 should be checked against bounty.c:150.
- All hex coordinate values for `towngate_coords`, `boat_coords`
  (§11.8, §11.9) — copied from bounty.c:651, 680. Decimal
  conversions in §11.8 added by hand; please verify.
- `castle_difficulty[26]` — listed in bounty.c:558; tier counts
  in §35.5 per-tier breakdown should be cross-referenced.

### 35.43 Cross-reference quick lookup

Common implementation tasks → relevant spec sections:

| Task | Sections |
|------|----------|
| Implement player movement | §15.1, §15.10, §15.11 |
| Implement combat damage formula | §17.5, §17.5.1-§17.5.10 |
| Implement save/load | §22 (full), §22.29 (pseudocode) |
| Implement DOS asset loading | §29.1-§29.6 |
| Implement spell effects | §10, §18.1-§18.14 |
| Implement game creation | §14, §14.4 (salt_continent), §14.5 (RNG order) |
| Implement modules | §28.1-§28.10 |
| Implement world map | §11, §12, §14.6 (LAND.ORG format) |
| Implement combat AI | §17.13 (unit_closest_offset), §19.2 |
| Implement foe pathfinding | §19.1 (full) |
| Implement UI screens | §23 (full) |
| Implement keybindings | §31 |
| Implement scoring | §14.2, §21.1 |
| Implement victory/defeat | §17.10, §21 |
| Implement chest/recruit | §20.1-§20.10 |
| Implement end-of-week | §16.6, §16.8-§16.13 |
| Implement audio | §29.5, §29.5.1 |

### 35.44 Glossary

- **adventure mode**: the overworld map; player walks tiles,
  encounters interactive objects.
- **combat mode**: the 6×5 grid battle screen.
- **continent**: one of 4 sub-maps (Continentia, Forestria,
  Archipelia, Saharia).
- **dwelling**: world map structure where you recruit one type
  of troop. 11 per continent.
- **end_week**: the resource refresh cycle that fires every 5
  in-game days.
- **fog of war**: 1-bit-per-tile flag; tiles unrevealed are not
  shown on minimap (unless orb_found).
- **footprint**: leadership cost per stack (count × hp).
- **hostile foe**: a foe encounter that triggers combat.
- **friendly foe**: a foe encounter that offers troops to join.
- **scepter**: hidden item; finding it (via Search Area) wins
  the game.
- **rank**: character progression level (Knight → General →
  Marshal → Lord, etc.).
- **villain**: one of 17 named enemies; capturing all 17 unlocks
  the puzzle map.

### 35.45 Source-line reference table

Quick file/line references for major functions:

| Function/value | File | Line |
|---|---|---|
| `KBgame` struct | `bounty.h` | 84 |
| `troops[]` | `bounty.c` | 46 |
| `classes[][]` | `bounty.c` | 166 |
| `villain_rewards[]` | `bounty.c` | 227 |
| `villain_army_*[]` | `bounty.c` | 247, 266 |
| `castle_coords[]` | `bounty.c` | 593 |
| `town_coords[]` | `bounty.c` | 622 |
| `troop_chance_table[]` | `bounty.c` | 710 |
| `troop_numbers[]` | `bounty.c` | 726 |
| `continent_dwellings[]` | `bounty.c` | 754 |
| `puzzle_map[]` | `bounty.c` | 768 |
| `spawn_game()` | `play.c` | 373 |
| `salt_continent()` | `play.c` | 185 |
| `bury_scepter()` (BUG) | `play.c` | 139 |
| `salt_villains()` | `play.c` | 157 |
| `troop_morale()` (BUG) | `play.c` | 587 |
| `end_week()` | `play.c` | 973 |
| `deal_damage()` | `play.c` | 1407 |
| `time_stop()` (BUG) | `play.c` | 1790 |
| `magic_damage()` | `play.c` | 1798 |
| `clone_troop()` | `play.c` | 1832 |
| `instant_troop()` | `play.c` | 1846 |
| `find_villain()` | `play.c` | 1872 |
| `KB_loadDAT()` | `save.c` | 27 |
| `KB_saveDAT()` | `save.c` | 277 |
| `furnish_map()` | `rogue.c` | 43 |
| `run_game()` | `game.c` | 6608 |
| `adventure_loop()` | `game.c` | 6281 |
| `combat_loop()` | `game.c` | 5992 |
| `take_chest()` | `game.c` | 3094 |
| `take_artifact()` | `game.c` | 3255 |
| `attack_foe()` | `game.c` | 3626 |
| `run_combat()` | `game.c` | 3488 |
| `choose_spell()` | `game.c` | 4130 |
| `select_module()` | `game.c` | 782 |
| `discover_modules()` | `kbauto.c` | 308 |
| `KB_funLZW()` | `dos-cc.c` | 215 |
| `DOS_UnpackExe()` | `dos-exe.c` | 573 |
| `tunFile_play()` | `dos-snd.c` | 230 |
| `KB_event()` | `ui.c` | 245 |

**End of specification.**

## 36. Tools (`src/tools/`)

These are nine separate command-line programs that ship with the
openkb tree but are not part of the running game. They read or
modify DOS asset files (CC archives, IMG files, KB.EXE), and a
few are stand-alone unpackers from public-domain reference code.
Total ~5,562 lines.

A clean-room reimplementation may:
- Skip these (focus on the engine).
- Reimplement minimal subset (e.g. `kbcc2` for asset extraction).
- Reuse source code (most are public-domain or CC0).

### 36.1 `kbcc.c` — original CC archive tool (813 lines)

Stand-alone .CC archive reader/writer. Does not depend on the
openkb library; uses its own LZW decoder. Hardcoded list of 64
asset filenames (the `archlist[]` at kbcc.c:80, copy of
`DOS_troop_names + DOS_villain_names + DOS_class_names + …`)
plus ext suffixes (`.4`, `.16`, `.256`).

Computes filename CRC via the same algorithm as `KB_ccHash`
(byte-swap + rotate-left-1 + add-fold). Decompresses LZW-encoded
entries.

Modes (typical CLI argument parsing):
- `--list <file.cc>` — list contents (matched by built-in CRC list)
- `--extract <file.cc>` — extract all to current directory
- `--add <file.cc> <file>` — replace/add entries
- `--rm <file.cc> <key>` — remove

Standalone — usable on machines without SDL.

### 36.2 `kbcc2.c` — improved CC archive tool (974 lines)

Replacement for kbcc, using openkb's library (`kbsys.h`,
`kbfile.h`, `KB_ccHash`, `KB_funLZW`). Cross-platform and
endian-aware.

Adds a `compress`-side path (`DOS_LZW(dst, dst_max, src, src_len)`
— external function declared but defined elsewhere in tools tree).
Tracks "hidden_payload" data — bytes between the 1122-byte header
and the first file's offset that contain padding (preserved on
rewrite).

### 36.3 `kbimg2dos.c` — image converter (563 lines)

Converts a PNG/BMP into DOS `.4` (CGA), `.16` (EGA), or `.256`
(VGA) format. Helpers it provides as `extern` references:

- `DOS_Write1BPP`, `DOS_Write1BPP_8x8` — pack to 1bpp (mono/font)
- `DOS_CalcMask`, `DOS_WriteMask` — extract/write 1bpp mask
- `DOS_CalcCGA`, `DOS_WriteCGA` — pack to 2bpp (CGA)
- `DOS_CalcEGA`, `DOS_WriteEGA` — pack to 4bpp (EGA)
- `DOS_CalcVGA`, `DOS_WriteVGA` — pack to 8bpp (VGA)
- `DOS_WritePalette_BUF` — embed VGA palette

Mask handling: in CGA/EGA, transparent pixels are encoded as a
separate 1bpp mask channel; in VGA, color index 0xFF is the
transparency colorkey.

### 36.4 `kbmaped.c` — map editor (329 lines)

Visual editor for the 64×64×4 world map (`LAND.ORG`). Loads
through `load_land(filename)` which `memcpy`s from a raw file
into `KBgame.map`. Initializes corner tiles (`game->map[c][0][0]
= 0xFF`) as start markers — same as `salt_continent` would erase
on game start.

Reads openkb config from `../../openkb.ini` (relative to where
it's run). Starts an SDL environment, loads modules, then enters
an `interactive_main(game)` editor loop (which is mostly a stub
in the current source — `interactive_edit(game)` is empty).

### 36.5 `kbrowse.c` — resource explorer (443 lines)

SDL-based browser for the openkb virtual filesystem. Walks paths
like `data/dos/256.CC#MCGA.DRV` interactively, displaying
contents. Hotkeys (from `draw_hintbar`):

- `F2` — Save As
- `F3` — Load From
- `Ctrl+I` — Grab palette from current view
- `Ctrl+O` — Apply palette to current view
- `Ctrl+P` — Dump palette

Uses `vendor/savepng.h` for PNG export. Rendered with the inline
font. Useful for modders inspecting assets without the running
game.

### 36.6 `kbview.c` — image viewer / BMP exporter (1007 lines)

Proof-of-concept .4/.16/.256 viewer with conversion to BMP. Source
header explicitly states "THIS CODE IS DIRT" and "Public Domain".

Two rendering paths exist (the modern one preserves palette info
for round-tripping):

- `put_2bpp`, `put_4bpp`, `put_8bpp` — produce 32-bit RGB surfaces
  (legacy path, lurking in `#if 0` blocks).
- `put_2bpp_8`, `put_4bpp_8`, `put_8bpp_8` — produce paletted 8bpp
  surfaces (preferred).

Color-key conventions:
- VGA: color 0xFF is the colorkey.
- EGA/CGA: separate mask channel; converted to index 0xFF on load
  and assigned proper RGB.
- Some images have no mask at all (handled by skipping mask
  detection).

### 36.7 `modding.c` — DOS-format encoders (457 lines)

Implements the `DOS_Write*` functions (1bpp, 1bpp_8x8, mask, CGA,
EGA, VGA, palette) referenced as `extern` by `kbimg2dos.c`. Marked
public-domain (CC0), as it's purely an encoding utility for
modding workflow.

Notable algorithm: `DOS_Write1BPP_8x8` walks the source surface in
8×8 blocks (col-major within rows of blocks), packing 8 pixels
per byte MSB-first. This is the inverse of the font/CH loader in
dos-img.c.

The header is a complement to the openkb library — included only
in the modding tool path, not in the game binary. Allows
generating native DOS-format assets from arbitrary BMP/PNG
sources, primarily used to create custom replacement assets that
the DOS module can consume.

### 36.8 `unexecomp.c` — standalone Execomp unpacker (490 lines)

Public-domain LZW unpacker for "COMPRESSOR v1.01 by Knowledge
Dynamics Corp." (the wrapper around KB.EXE in some early DOS
distributions). Marked: "This program only works on 32-bit LSB
machines." Self-contained — duplicates `KB_funLZW` logic
locally. Layout it expects (per the source comment):

```
0x000  0x200  DOS exe header (with wrong block count)
0x200  0x003  Jump to 0x29C
0x203  0x099  Unpacker data
0x29C  0x3EE  Unpacker code
0x68A          (garbage, used as scratch)
0x699  0x025  Packed inner header
0x6C0          Special buffer
<…>            Packed exe (LZW stream)
```

Algorithm summary:
- Read outer EXE header.
- Compute extra_data_start = blocks_in_file*512 - bytes_in_last_block.
- Read inner packed EXE header at extra_data_start.
- LZW-decode from `extra_data_start + inner.header_paragraphs * 16`.
- Build new outer EXE header pointing at the unpacked code.
- Write to disk.

The same algorithm is implemented in `dos-exe.c:execomp_uncompress`
(used by openkb at runtime).

### 36.9 `unexepack.c` — standalone ExePack unpacker (486 lines)

Public-domain reverse of Microsoft EXEPACK compression. Source
references the algorithm description from
`http://cvs.z88dk.org/cgi-bin/viewvc.cgi/xu4/doc/avatarExepacked.txt`
(by aowen, 2009).

Layout it expects:

```
0x000   0x1C   DOS EXE header
<…>            Packed exe data
        0x12   EXEPACK variables struct
        0x105  Unpacker code
        0x016  String "Packed file is corrupt"
<…>            Packed reloc table
```

Algorithm:
- Read EXE header.
- exeLen = `cs * 16` (from header).
- Read packed data of `exeLen` bytes.
- Read 18 bytes of EXEPACK variables (`real_start_OFFSET/SEGMENT`,
  `mem_start_SEGMENT`, `unpacker_len`, etc.).
- Read unpacker code + reloc table.
- Reverse-walk packed data with two commands:
  - `0xB0`: RLE fill of `<length>` bytes with value
  - `0xB2`: copy `<length>` next bytes
  - LSB bit set on command byte → last block.
- Build new EXE header with unpacked size + relocated code.

Caveat (per source): "this will not produce identical results to
the real EXEPACK routine, because the garbage bytes are treated
differently: canonically, they should be carried over from the
equivalent locations of *packed* exe; here -- they are simply
0x00."

This same algorithm is also in `dos-exe.c:exepack_uncompress`
(used by openkb at runtime).

### 36.10 Tools dependency summary

| Tool          | Standalone?  | Uses openkb lib? | SDL? |
|---------------|--------------|-------------------|------|
| kbcc          | yes          | no                | no   |
| kbcc2         | no           | yes               | no   |
| kbimg2dos     | no           | uses modding.c    | yes  |
| kbmaped       | no           | yes               | yes  |
| kbrowse       | no           | yes               | yes  |
| kbview        | yes          | no (own code)     | yes  |
| modding.c     | n/a (lib)    | provides API      | opt. |
| unexecomp     | yes          | no                | no   |
| unexepack     | yes          | no                | no   |

Tools are not loaded by `main.c` or `combat.c` and have no impact
on game state. They exist in `src/tools/Makefile` as separate
build targets. A clean-room re-implementation of the *engine* may
omit them entirely; a full re-implementation of openkb-the-project
should provide equivalents.

### 36.11 Build artifacts

The Makefile in `src/tools/` produces individual binaries:
- `kbcc`, `kbcc2` — CC archive utilities.
- `kbimg2dos` — image-to-DOS converter.
- `kbmaped` — map editor (incomplete).
- `kbrowse` — asset browser.
- `kbview` — image viewer.
- `unexecomp`, `unexepack` — DOS exe unpackers.

These are typically used for asset extraction (preparing assets
for the GNU module path), modding workflows, or reverse-engineering
unfamiliar KB-family DOS games.

### 36.12 Asset extraction workflow

Typical workflow for extracting DOS assets to GNU module:

1. **Unpack KB.EXE** (if needed):
   ```bash
   unexecomp kb.exe kb_unpacked.exe   # Execomp wrapper
   unexepack kb_unpacked.exe kb.exe   # ExePack inner
   ```

2. **List CC archive contents**:
   ```bash
   kbcc --list 256.CC
   kbcc --list 416.CC
   ```

3. **Extract individual assets**:
   ```bash
   kbcc --extract 256.CC peas.256
   ```

4. **Convert to PNG**:
   ```bash
   kbview peas.256 --vga -o peas.png
   ```

5. **Generate INI files** (for GNU module):
   - Read strings from KB.EXE at known offsets (manually or
     with `dd`).
   - Format as INI: `[troop0]\nname = Peasants\nhp = 1\n...`.

6. **Layout for GNU module**:
   ```
   data/free/
   ├── nwcp.png           # company logo
   ├── title.png          # title screen
   ├── peas.png           # Peasants sprite
   ├── ...
   ├── troops.ini         # troop stats
   ├── villains.ini       # villain data
   ├── castles.ini        # castle coords
   ├── towns.ini          # town data
   ├── spells.ini         # spell data
   ├── colors.ini         # color schemes
   ├── ui.ini             # frame rects
   ├── signs.txt          # signpost text
   ├── credits.txt        # credits
   ├── endwin.txt         # win text
   ├── endlose.txt        # lose text
   └── land.org           # world map
   ```

This was openkb's original intent: extract once, run forever
without the original game.

### 36.13 Modding workflow

To create custom assets:

1. Design new sprites/images in PNG.
2. Use `kbimg2dos` to convert PNG → `.4`/`.16`/`.256`.
3. Use `kbcc2` to repack into a custom CC archive.
4. Configure openkb to use the custom path:
   ```ini
   [module]
   name = Custom DOS
   type = dos-vga
   path = data/custom/256.CC#
   path = data/custom/416.CC#
   ```

Modding is theoretical — there's no public mod ecosystem for
openkb. The tooling supports it but examples are scarce.

### 36.14 Standalone exe unpackers

`unexecomp` and `unexepack` are reproducible-build standalone
versions of dos-exe.c's algorithms. Useful for:
- Studying the algorithm.
- Unpacking other DOS games (not just KB).
- Debugging openkb's exe loader by comparing output.

Both are public-domain (CC0); can be reused without attribution.

### 36.15 Tools as reference

For a clean-room reimplementation:

- `dos-exe.c`'s pseudo-LZW (Execomp) and ExePack RLE — see
  unexecomp.c, unexepack.c for clean reference.
- `dos-cc.c`'s LZW — see kbcc.c for self-contained version.
- `dos-img.c`'s image format — see kbview.c for clean decoder.
- `modding.c` — clean encoder for DOS image formats.

Most of these algorithms are also documented in standalone
research papers / online references (linked in tool source
comments).

## 37. Implementation guidance

For a clean-room reimplementation, recommended development order:

### 37.1 Foundation phase

1. **Data types and byte ordering** (§2). Get `byte`/`word`/
   `dword` types, LE/BE pack/unpack macros, READ_/WRITE_ stream
   macros. ~100 lines.

2. **Constants and limits** (§3). Define MAX_TROOPS, MAX_FOES,
   etc. ~50 lines.

3. **`KBgame` struct** (§4). Define the central state. ~100 lines.

4. **Static data tables** (§5–§11). Hardcode all troops, classes,
   villains, coords, names. ~500 lines of data.

### 37.2 World and creation phase

5. **LAND.ORG loader** (§14.6). Read 16384-byte raw map.

6. **`spawn_game`** (§14). Initialize player, salt continents,
   place villains, distribute spells. ~400 lines.

7. **Save/load** (§22). Serialize `KBgame` to 20421-byte file.
   ~300 lines.

### 37.3 Adventure phase

8. **Tile system** (§12). Predicates, IDs, dispatching.

9. **Player movement** (§15). Adventure_loop core.

10. **Day/week cycle** (§16). end_day, end_week, budget.

11. **Foe follow** (§19.1). Pathfinding for hostile foes.

12. **Chest, dwelling, foe encounters** (§20). Resource pickup.

13. **Castle and town visit** (§20). Recruit and contract.

### 37.4 Combat phase

14. **Combat data structures** (§17.1).

15. **Damage formula** (§17.5). Heart of combat.

16. **Movement, retaliation, ranged** (§17.6, §17.7).

17. **AI think tree** (§19.2, §19.3).

18. **Combat loop** (§17.9). Main UI integration.

19. **Spell effects** (§18). 14 spells, including stubs.

20. **Victory/defeat** (§17.10, §21).

### 37.5 UI phase

21. **Hotspot/gamestate system** (§23.1, §31). Input handling.

22. **Frame drawing** (§23.16). MessageBox, BottomBox, etc.

23. **Color schemes** (§26). Palette resolution.

24. **All screens** (§23). One at a time: title, char select,
    save load, adventure, combat, view army, view char, view
    contract, view puzzle, options, controls, etc.

### 37.6 Asset phase

25. **Resource resolver** (§27). The `(id, sub_id)` API.

26. **GNU module** (§28.6). Read INI/PNG. Easiest first.

27. **DOS module** (§28.5). KB.EXE parsing, CC archive, IMG
    decoding. Most complex.

28. **Font and rendering** (§30).

29. **Audio playback** (§29.5).

### 37.7 Polish phase

30. **Configuration** (§32). INI parsing, CLI args.

31. **Module discovery** (§28.2). Auto-detect modules.

32. **Cheat menu** (§15.7).

33. **Bug fixes** (§34). Address known issues.

### 37.8 Testing strategy

- **Unit tests**: damage formula (§17.5), save round-trip (§22),
  fog packing (§22.23).
- **Integration tests**: full game from create → end_week → save
  → load → continue.
- **Asset tests**: load each module's assets, verify all
  resource ids resolve.
- **Combat tests**: AI vs AI matches with auto_battle cheat.

### 37.9 Optional extensions

These were not in DOS King's Bounty but openkb hints at:

- **Localization**: extract hardcoded strings, support .po files.
- **Modern saves**: include RNG state, version tag.
- **High-res textures**: optional 2× 4× tilesets.
- **Network multiplayer**: complete the proof-of-concept (§33).
- **Mod manager**: install/configure custom modules from UI.
- **Achievements / stats tracking**.

## 38. Verification checklist

A reimplementation passes spec compliance if it can:

### 38.1 Game state

- [ ] Load LAND.ORG and produce a 4×64×64 tile map.
- [ ] Place 17 villains on castles per `villains_per_continent`.
- [ ] Place 2 artifacts per continent at chest tiles.
- [ ] Salt 11 dwellings with appropriate troops per `continent_dwellings`.
- [ ] Place 2 telecaves per continent.
- [ ] Place 5 friendly foes per continent.
- [ ] Hide scepter at a random grass tile (account for §34.1
      bug if matching).
- [ ] Initialize the right starting army per class.
- [ ] Apply class rank 0's bonuses.
- [ ] Initialize options, contract cycle, etc.

### 38.2 Save format

- [ ] Save/load roundtrip preserves all 20421 bytes.
- [ ] Fog of war packed at 1 bit per tile.
- [ ] Scepter coords XOR-encrypted with key.
- [ ] Foe counts truncated to byte on disk.
- [ ] All field offsets match §22 layout.

### 38.3 Combat

- [ ] Damage formula matches §17.5: `(dmg × turn_count ×
      skill_diff) / 10`.
- [ ] Morale modifies damage when hero present and unit in
      control.
- [ ] Artifact powers (Sword, Shield, Crown, etc.) apply
      correctly.
- [ ] Retaliation fires once per round per unit.
- [ ] Demon Scythe 10% chance halves target stack.
- [ ] Vampire Leech adds count from damage.
- [ ] Ghost Absorb adds count from kills.
- [ ] AI moves intelligently (target picking, flying, ranged).
- [ ] Castle layout used in mode-1 combat.
- [ ] Spoils accumulate per side, awarded on victory.
- [ ] temp_death wipes army except 20 Peasants.

### 38.4 Spells

- [ ] All 14 spells appear in choose_spell menu.
- [ ] Spell costs in town purchase.
- [ ] Combat spells consume one round per cast.
- [ ] Adventure spells don't consume time.
- [ ] Bridge places water → bridge tiles.
- [ ] Find Villain marks `KBCASTLE_KNOWN`.
- [ ] Castle/Town Gate teleports to visited locations.
- [ ] Instant Army summons class familiar.
- [ ] Time Stop adds steps (account for §34.4 bug or fix).
- [ ] Raise Control adds 100 × spell_power leadership.

### 38.5 Day/week

- [ ] Steps decrement on movement, reset to 40 at end_day.
- [ ] Days decrement at end_day.
- [ ] Week ends every 5 days.
- [ ] Astrology week selects creature (Peasants every 4th).
- [ ] Ghost-to-Peasant on Peasants weeks.
- [ ] Dwelling refresh on matching weeks.
- [ ] Foe/castle growth on matching weeks.
- [ ] Player castle repop when stack 0 empty.
- [ ] Budget: commission added, boat + army upkeep deducted.
- [ ] days_left == 0 triggers lose_game.

### 38.6 UI

- [ ] All screens render correctly.
- [ ] Status bar shows " Options / Controls / Days Left:N ".
- [ ] Sidebar shows contract face, siege weapons, magic star,
      puzzle map, gold purse.
- [ ] Minimap toggles fog vs full (with orb).
- [ ] Combat target selector cursor responds to arrow keys.
- [ ] Twirl animation in cursors.
- [ ] Color scheme changes by difficulty.

### 38.7 Modules

- [ ] DOS module loads from KB.EXE + CC archives.
- [ ] GNU module loads from INI + PNG.
- [ ] Auto-discovery detects modules in data dir.
- [ ] Multiple modules can be configured.
- [ ] Fallback resolves missing assets to next module.

### 38.8 Compliance with bugs

A "faithful" implementation matches openkb's bug behaviors:
- [ ] §34.1 scepter coord bug (or fix it).
- [ ] §34.2 morale calculation (or fix).
- [ ] §34.3 stub spells (or implement).
- [ ] §34.4 Time Stop ×10 not ×100 (or fix).
- [ ] §34.18 contract activation bug (or fix).

A "corrected" implementation fixes these. Document the choice.

## 39. References

### 39.1 openkb source tree

`/home/danheskett/personal/openkb-reference/` (referenced
throughout this spec). Files are GPLv3, 2011-2014 by Vitaly
Driedfruit and other openkb authors.

Key files:
- `src/main.c` — entry, config
- `src/game.c` — adventure loop, combat loop, all UI
- `src/play.c` — game logic (deterministic)
- `src/save.c` — load/save
- `src/bounty.c`, `src/bounty.h` — data tables, structs
- `src/rogue.c` — map furnishing
- `src/ui.c`, `src/ui.h` — hotspot/gamestate framework
- `src/env-sdl.c`, `src/env.h` — SDL environment
- `src/combat.c` — multiplayer combat
- `src/font.h` — fallback inline font
- `src/lib/` — libraries (kbsys, kbstd, kbdir, kbfile, kbres,
  kbconf, kbauto, kbsound)
- `src/lib/dos-*.c` — DOS asset decoders
- `src/lib/free-data.c` — GNU module
- `src/lib/md-rom.c` — Mega Drive module

### 39.2 Original game

*King's Bounty* by New World Computing (1990). DOS-based
turn-based strategy. Distributed on floppy with KB.EXE +
256.CC + 416.CC.

### 39.3 Related projects

- openkb itself (target reimplementation).
- OpenBounty (`/home/danheskett/personal/openbounty/`) —
  this project, a port targeting parity with openkb in
  C + raylib.

### 39.4 Algorithm references

- LZW decompression — Knowledge Dynamics Compressor v1.01
  (referenced in dos-cc.c and `unexecomp.c` source).
- ExePack RLE — algorithm description by aowen at
  `http://cvs.z88dk.org/cgi-bin/viewvc.cgi/xu4/doc/avatarExepacked.txt`.
- AdvancedMAME scale2x — public domain image scaling filter
  (vendor/scale2x.c).
- BSD strlcpy/strlcat — Todd C. Miller, 1998.

### 39.5 SDL

SDL 1.2 documentation: `https://wiki.libsdl.org/SDL_1.2/`.
Required: `SDL`, `SDL_net` (for combat.c).
Optional: `SDL_image` (PNG support), `libpng`.

### 39.6 Format documentation

- `.CC` archive format: documented in `dos-cc.c` source comments.
- `.4`/`.16`/`.256` image format: documented in `dos-img.c`.
- `KB.EXE` packing: documented in `dos-exe.c` and `unexecomp.c`,
  `unexepack.c`.
- Save format `.DAT`: documented in §22 of this spec; source in
  `save.c`.
- Tunes format: documented in §29.5; source in `dos-snd.c`.

### 39.7 Useful external tools

For working with DOS King's Bounty assets:
- DOSBox — emulator for running the original game.
- DOSBox-Staging — improved fork of DOSBox.
- `unzip`, `dd`, `xxd` — extracting and inspecting binary data.

## 40. Conclusion

This specification covers every implementation detail of openkb's
game engine. With ~12000 lines, it should be sufficient for a
clean-room reimplementation in any language.

Major sections by depth:
- §17 Combat engine — most detailed (~1500 lines).
- §22 Save format — fully byte-mapped with pseudocode.
- §29 DOS asset formats — algorithm-by-algorithm coverage.
- §19 AI — full pathfinding and target picking algorithms.
- §15 Player actions — state diagrams and edge cases.

Remaining uncertainties (audited but not exhaustively
verified):
- Some color scheme hex values may not match DOS exactly.
- KB.EXE byte offsets verified for KB95 only; KB90 inferred.
- Tile graphic content (which cells of the tileset hold what)
  is module-defined and not specified in openkb itself.

For questions, the source code at
`/home/danheskett/personal/openkb-reference/` is the ultimate
reference.

---

*End of OpenKB Specification, version 1.0*

*Generated for the OpenBounty project as a clean-room
reimplementation reference. Total length: ~12000 lines. Last
verified against openkb source as of the date of this document.*



