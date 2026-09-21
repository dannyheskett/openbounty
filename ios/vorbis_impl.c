// stb_vorbis's implementation, compiled once on its own: the header-only mode
// used by ios/audio_ios.mm declares the API, and this provides it. Kept out of
// that file because stb_vorbis.c is C and audio_ios.mm is Objective-C++.
#if defined(PLATFORM_IOS)
#define STB_VORBIS_NO_PUSHDATA_API   // the game decodes whole tracks in one go
#define STB_VORBIS_NO_STDIO          // every asset arrives as bytes from the pack
#include "stb_vorbis.c"
#endif
