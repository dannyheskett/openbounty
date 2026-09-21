// Storage for the display insets, plus the JNI entry point the Android Activity
// calls to populate them. Compiles on every platform (the accessor just returns
// zeros where nothing sets the values); the JNI export only exists on Android.
#include "safe_area.h"

static SafeArea s_area;

SafeArea safe_area_get(void) { return s_area; }

#if defined(PLATFORM_ANDROID)
#include <jni.h>

// Called from GloryOfRomeActivity.pushSafeInsets(). Coordinates are already in
// the surface's pixel space (the app renders at native resolution), so the
// layout can compare them against GetScreenWidth() directly.
JNIEXPORT void JNICALL
Java_com_danheskett_gloryofrome_GloryOfRomeActivity_nativeSetSafeInsets(
    JNIEnv* env, jobject thiz, jint top, jint bottom, jint left, jint right,
    jint cutout_left, jint cutout_right) {
    (void)env;
    (void)thiz;
    s_area.top          = top;
    s_area.bottom       = bottom;
    s_area.left         = left;
    s_area.right        = right;
    s_area.cutout_left  = cutout_left;
    s_area.cutout_right = cutout_right;
}
#endif
