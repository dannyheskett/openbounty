#ifndef OB_INPUT_HOST_H
#define OB_INPUT_HOST_H

#include <stdbool.h>

// Input host shim. Thin wrappers around the raylib input calls used by
// game logic, so callers don't include raylib directly.

bool input_key_pressed(int key);     // IsKeyPressed
bool input_key_down(int key);        // IsKeyDown
int  input_get_key_pressed(void);    // GetKeyPressed
int  input_get_char_pressed(void);   // GetCharPressed

#endif
