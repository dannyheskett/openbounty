#include "input_host.h"
#include "raylib.h"

// Thin pass-through wrappers around raylib input.

bool input_key_pressed(int key) { return IsKeyPressed(key); }
bool input_key_down(int key)    { return IsKeyDown(key); }
int  input_get_key_pressed(void)  { return GetKeyPressed(); }
int  input_get_char_pressed(void) { return GetCharPressed(); }
