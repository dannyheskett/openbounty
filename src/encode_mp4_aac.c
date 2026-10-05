// Dedicated TU for vo-aacenc (the AAC-LC encoder behind --intro-movie's
// sound track), compiled as one unit so the build lists one file. Its
// include directories come from AAC_INC in the Makefile.
//
// Vendored upstream code triggers warnings under our -Wall -Wextra.
// Suppressed here so the project's own code stays warning-clean.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#ifdef __clang__
#pragma clang diagnostic ignored "-Wmacro-redefined"   // its typedefs.h redefines __inline
#endif

// Upstream's build defines __unused away; Apple's <sys/cdefs.h> defines it
// already, so the system headers come first and decide.
#include <stdlib.h>
#ifndef __unused
#define __unused
#endif

#include "../third_party/vo-aacenc/common/cmnMemory.c"
#include "../third_party/vo-aacenc/aacenc/basic_op/basicop2.c"
#include "../third_party/vo-aacenc/aacenc/basic_op/oper_32b.c"
#include "../third_party/vo-aacenc/aacenc/src/aac_rom.c"
#include "../third_party/vo-aacenc/aacenc/src/aacenc.c"
#include "../third_party/vo-aacenc/aacenc/src/aacenc_core.c"
#include "../third_party/vo-aacenc/aacenc/src/adj_thr.c"
#include "../third_party/vo-aacenc/aacenc/src/band_nrg.c"
#include "../third_party/vo-aacenc/aacenc/src/bit_cnt.c"
#include "../third_party/vo-aacenc/aacenc/src/bitbuffer.c"
#include "../third_party/vo-aacenc/aacenc/src/bitenc.c"
#include "../third_party/vo-aacenc/aacenc/src/block_switch.c"
#include "../third_party/vo-aacenc/aacenc/src/channel_map.c"
#include "../third_party/vo-aacenc/aacenc/src/dyn_bits.c"
#include "../third_party/vo-aacenc/aacenc/src/grp_data.c"
#include "../third_party/vo-aacenc/aacenc/src/interface.c"
#include "../third_party/vo-aacenc/aacenc/src/line_pe.c"
#include "../third_party/vo-aacenc/aacenc/src/memalign.c"
#include "../third_party/vo-aacenc/aacenc/src/ms_stereo.c"
#include "../third_party/vo-aacenc/aacenc/src/pre_echo_control.c"
#include "../third_party/vo-aacenc/aacenc/src/psy_configuration.c"
#include "../third_party/vo-aacenc/aacenc/src/psy_main.c"
#include "../third_party/vo-aacenc/aacenc/src/qc_main.c"
#include "../third_party/vo-aacenc/aacenc/src/quantize.c"
#include "../third_party/vo-aacenc/aacenc/src/sf_estim.c"
#include "../third_party/vo-aacenc/aacenc/src/spreading.c"
#include "../third_party/vo-aacenc/aacenc/src/stat_bits.c"
#include "../third_party/vo-aacenc/aacenc/src/tns.c"
#include "../third_party/vo-aacenc/aacenc/src/transform.c"

#pragma GCC diagnostic pop
