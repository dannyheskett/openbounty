#ifndef OB_INTRO_H
#define OB_INTRO_H

// The Introduction (#154): the pack's animated opening, played from the
// modern title menu's Introduction row. The script is data
// (Resources.intro, PACK-FORMAT section 2.4); this draws it as a film: the
// picture at a whole multiple above a black caption band, the speaker's face
// beside the typed line. Every frame is a pure function of the time into the
// intro, so the gallery and --intro-movie can ask for any moment.

#include <stdbool.h>

#include "gfx.h"
#include "resources.h"

// Whether the title menu offers the Introduction: a modern pack with one.
bool intro_available(const Resources *res);

// Play the whole intro. Any key or tap ends it; it ends by itself at the
// last beat. False only when the window closed.
bool run_intro(RenderTexture2D *rt, const Resources *res);

// --gallery / --intro-movie: draw the frame `t` seconds in, into rt. No
// input and no audio. Loads the intro's textures on first use; free them
// with intro_release.
void intro_gallery_draw(RenderTexture2D *rt, const Resources *res, double t);
// The theme's and the sounds' gain `t` seconds in, 0..1: 1 until the last
// scene's fade out, then down to 0 at the end. The player and the
// --intro-movie mix both follow it.
double intro_sound_gain(const ResIntro *in, double t);
void intro_release(void);

// The film's layout on a screen: the picture at the largest whole multiple
// that leaves room for a three-line caption band below it, the two centred
// together; the speaker's face at a whole multiple that fits the band, or
// hidden (face_scale 0).
typedef struct {
    int scale;
    int pic_x, pic_y, pic_w, pic_h;
    int band_y, band_h;
    int face_x, face_y, face_scale;
    int text_x, text_y, text_w;
} IntroLayout;
void intro_layout(int screen_w, int screen_h, int frame_w, int frame_h,
                  int line_h, IntroLayout *out);

#endif
