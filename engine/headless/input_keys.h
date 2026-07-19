#ifndef OB_ENGINE_INPUT_KEYS_H
#define OB_ENGINE_INPUT_KEYS_H

// Engine input-key codes. These values match raylib's `KEY_*` constants
// exactly so the harness shim can pass them through unchanged. Keep them
// in sync if raylib ever renumbers -- until then, the two namespaces are
// freely interchangeable at the int level.
//
// Engine-side code uses `OB_KEY_*`. Shell-side render code may use the
// raylib names directly.

#define OB_KEY_NULL           0

#define OB_KEY_SPACE          32
#define OB_KEY_APOSTROPHE     39
#define OB_KEY_COMMA          44
#define OB_KEY_MINUS          45
#define OB_KEY_PERIOD         46
#define OB_KEY_SLASH          47
#define OB_KEY_ZERO           48
#define OB_KEY_ONE            49
#define OB_KEY_TWO            50
#define OB_KEY_THREE          51
#define OB_KEY_FOUR           52
#define OB_KEY_FIVE           53
#define OB_KEY_SIX            54
#define OB_KEY_SEVEN          55
#define OB_KEY_EIGHT          56
#define OB_KEY_NINE           57
#define OB_KEY_SEMICOLON      59
#define OB_KEY_EQUAL          61
#define OB_KEY_A              65
#define OB_KEY_B              66
#define OB_KEY_C              67
#define OB_KEY_D              68
#define OB_KEY_E              69
#define OB_KEY_F              70
#define OB_KEY_G              71
#define OB_KEY_H              72
#define OB_KEY_I              73
#define OB_KEY_J              74
#define OB_KEY_K              75
#define OB_KEY_L              76
#define OB_KEY_M              77
#define OB_KEY_N              78
#define OB_KEY_O              79
#define OB_KEY_P              80
#define OB_KEY_Q              81
#define OB_KEY_R              82
#define OB_KEY_S              83
#define OB_KEY_T              84
#define OB_KEY_U              85
#define OB_KEY_V              86
#define OB_KEY_W              87
#define OB_KEY_X              88
#define OB_KEY_Y              89
#define OB_KEY_Z              90
#define OB_KEY_LEFT_BRACKET   91
#define OB_KEY_BACKSLASH      92
#define OB_KEY_RIGHT_BRACKET  93
#define OB_KEY_GRAVE          96

#define OB_KEY_ESCAPE         256
#define OB_KEY_ENTER          257
#define OB_KEY_TAB            258
#define OB_KEY_BACKSPACE      259
#define OB_KEY_INSERT         260
#define OB_KEY_DELETE         261
#define OB_KEY_RIGHT          262
#define OB_KEY_LEFT           263
#define OB_KEY_DOWN           264
#define OB_KEY_UP             265
#define OB_KEY_PAGE_UP        266
#define OB_KEY_PAGE_DOWN      267
#define OB_KEY_HOME           268
#define OB_KEY_END            269
#define OB_KEY_CAPS_LOCK      280
#define OB_KEY_SCROLL_LOCK    281
#define OB_KEY_NUM_LOCK       282
#define OB_KEY_PRINT_SCREEN   283
#define OB_KEY_PAUSE          284
#define OB_KEY_F1             290
#define OB_KEY_F2             291
#define OB_KEY_F3             292
#define OB_KEY_F4             293
#define OB_KEY_F5             294
#define OB_KEY_F6             295
#define OB_KEY_F7             296
#define OB_KEY_F8             297
#define OB_KEY_F9             298
#define OB_KEY_F10            299
#define OB_KEY_F11            300
#define OB_KEY_F12            301
#define OB_KEY_LEFT_SHIFT     340
#define OB_KEY_LEFT_CONTROL   341
#define OB_KEY_LEFT_ALT       342
#define OB_KEY_LEFT_SUPER     343
#define OB_KEY_RIGHT_SHIFT    344
#define OB_KEY_RIGHT_CONTROL  345
#define OB_KEY_RIGHT_ALT      346
#define OB_KEY_RIGHT_SUPER    347

#define OB_KEY_KP_0           320
#define OB_KEY_KP_1           321
#define OB_KEY_KP_2           322
#define OB_KEY_KP_3           323
#define OB_KEY_KP_4           324
#define OB_KEY_KP_5           325
#define OB_KEY_KP_6           326
#define OB_KEY_KP_7           327
#define OB_KEY_KP_8           328
#define OB_KEY_KP_9           329
#define OB_KEY_KP_DECIMAL     330
#define OB_KEY_KP_DIVIDE      331
#define OB_KEY_KP_MULTIPLY    332
#define OB_KEY_KP_SUBTRACT    333
#define OB_KEY_KP_ADD         334
#define OB_KEY_KP_ENTER       335
#define OB_KEY_KP_EQUAL       336

#endif
