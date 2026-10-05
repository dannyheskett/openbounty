#ifndef OB_INTRO_MIX_H
#define OB_INTRO_MIX_H

// The Introduction's sound, mixed offline for --intro-movie: the theme from
// the top and each beat's sounds at their cues, at the levels and with the
// closing fade the player uses (audio_intro_levels, intro_sound_gain). As in
// the player, a sound started again while it plays starts over.

#include "resources.h"
#include <stddef.h>

// A decoded sound, mono at the mix's rate.
typedef struct {
    const float *s;
    size_t       n;
} IntroClip;

// Mix into out[0..n) (zeroed first), sample i at i / rate seconds. `theme`
// may be empty; clips[k] is the sound whose file is paths[k], and a sound
// with no clip is left out. master scales everything, as the volume setting
// does in the game.
void intro_mix_render(const ResIntro *in, float master, int rate,
                      const IntroClip *theme,
                      const char *const *paths, const IntroClip *clips, int nclips,
                      float *out, size_t n);

// The whole intro's sound at `rate`, mono, 16-bit, at full master volume:
// the pack's theme and sound files decoded, then intro_mix_render. Sets
// *frames; returns NULL when the intro has no sound or it cannot be read.
// Free with free().
short *intro_mix_pcm(const Resources *res, int rate, size_t *frames);

#endif
