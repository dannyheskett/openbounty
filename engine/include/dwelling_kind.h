// engine/include/dwelling_kind.h
//
// Dwelling kind enum, shared between engine (step.c classifies tile
// kind and emits screen_dwelling_open) and shell (src/screens/dwelling.c
// renders the appropriate backdrop).
//
// Filename ends in _kind to avoid clashing with src/screens/dwelling.h
// (gcc's "..." include searches the current file's directory first).

#ifndef OB_ENGINE_DWELLING_KIND_H
#define OB_ENGINE_DWELLING_KIND_H

typedef enum {
    DWELLING_KIND_PLAINS = 0,
    DWELLING_KIND_FOREST,
    DWELLING_KIND_HILL,
    DWELLING_KIND_DUNGEON,
} DwellingKind;

#endif
