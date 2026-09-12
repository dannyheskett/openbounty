// engine/include/game_fwd.h
//
// One declaration of the engine's Game type name, for the headers that take a
// Game * without needing its definition (player_io.h, savegame.h, src/ui.h)
// and for game.h itself, which defines the struct.
//
// It lives on its own and is include-guarded because repeating
// `typedef struct Game Game;` in several headers is a C11 feature: under
// -std=c99 clang reports every repeat ("redefinition of typedef is a C11
// feature"), which in the WebAssembly build meant over a hundred warnings.
// One guarded typedef, included wherever the name is needed, says the same
// thing once.

#ifndef OB_GAME_FWD_H
#define OB_GAME_FWD_H

typedef struct Game Game;

#endif
