// iOS app shell: a UIKit application hosting a CAMetalLayer view driven by a
// CADisplayLink. No Storyboard, no scene manifest -- a classic AppDelegate
// window, so the whole app is clang plus a plist (see the Makefile's ios_build).
//
// THE LOOP. openbounty's shell is fifteen blocking loops across five files
// (docs/IOS-BACKEND-SPIKE.md); the web build only works because ASYNCIFY
// unwinds them, and iOS has no equivalent. So the game runs on its OWN THREAD
// with its loops intact, and the display link on the main thread does nothing
// but tick the clock the game waits on. The game thread is the only thread
// that touches Metal, which is legal: a MTLCommandQueue may be used from one
// thread at a time, and the main thread never encodes.
//
// Until the game is wired in (checkpoint 3), the thread runs a self-test frame
// that exercises the real renderer -- clear, rects, an outline, a rounded
// panel, a triangle -- so the Simulator screenshot from CI proves the pipeline
// rather than just the toolchain.

#import <UIKit/UIKit.h>

extern "C" {
#include "gfx.h"
#include "plat_ios.h"
}
#include "gfx_metal.h"

extern "C" void ob_ios_selftest_frame(void);
// The game's entry point (src/main.c). On iOS it takes no arguments worth the
// name: there is no command line, and the pack and save directory are resolved
// by src/plat_ios.c.
extern "C" int shell_run_game(int argc, char **argv);

@interface OBMetalView : UIView
@property (nonatomic) BOOL started;
@end

@implementation OBMetalView

+ (Class)layerClass { return [CAMetalLayer class]; }

- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (!self.window || self.started) return;
    self.started = YES;

    self.multipleTouchEnabled = NO;   // the game reads one contact
    gfx_metal_attach((CAMetalLayer *)self.layer);
    [self updateDrawableSize];
    {
        int w = 0, h = 0;
        plat_ios_screen(&w, &h);
        NSLog(@"openbounty: view ready, metal=%d, game area %dx%d",
              gfx_metal_ready() ? 1 : 0, w, h);
    }

    CADisplayLink *link =
        [CADisplayLink displayLinkWithTarget:self selector:@selector(onFrame:)];
    link.preferredFramesPerSecond = 60;
    [link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

    [self startGameThread];
}

// The game runs on its own thread with its blocking loops intact -- the one
// architectural decision of this port (docs/IOS-BACKEND-SPIKE.md). It is also
// the only thread that encodes Metal work, which is legal because the main
// thread never does.
//
// A generous stack: main.c holds several large locals (Resources is ~3.9 MB,
// Map ~848 KB), the same reason the Windows and web builds raise theirs.
- (void)startGameThread {
    NSThread *t = [[NSThread alloc] initWithBlock:^{
        static char arg0[] = "gloryofrome";
        char *argv[] = { arg0, NULL };
        // Markers, not decoration: an iOS app's stdout goes nowhere a CI job
        // can read, so the only way to see how far the game got is os_log.
        NSLog(@"openbounty: game thread starting (metal ready=%d)",
              gfx_metal_ready() ? 1 : 0);
        int rc = shell_run_game(1, argv);
        NSLog(@"openbounty: game thread returned %d", rc);
    }];
    t.stackSize = 16 * 1024 * 1024;
    t.name = @"openbounty.game";
    [t start];
}

- (void)layoutSubviews { [super layoutSubviews]; [self updateDrawableSize]; }
- (void)safeAreaInsetsDidChange {
    [super safeAreaInsetsDidChange];
    [self updateDrawableSize];
}

- (void)updateDrawableSize {
    CAMetalLayer *layer = (CAMetalLayer *)self.layer;
    CGFloat scale = self.window ? self.window.screen.scale
                                : [UIScreen mainScreen].scale;
    self.contentScaleFactor = scale;
    layer.contentsScale = scale;

    CGSize full = CGSizeMake(self.bounds.size.width * scale,
                             self.bounds.size.height * scale);
    if (full.width  < 1) full.width  = 1;
    if (full.height < 1) full.height = 1;
    layer.drawableSize = full;

    // The game is given the safe rectangle, in DEVICE PIXELS: it presents its
    // buffer at an integer scale of whatever it is told the window is, and on
    // a phone the device count is the one that matters (a 390pt-wide screen is
    // 1170 real pixels, which is what makes an 800x532 buffer fit at all).
    UIEdgeInsets ins = self.safeAreaInsets;
    int ox = (int)(ins.left * scale);
    int oy = (int)(ins.top  * scale);
    int sw = (int)full.width  - ox - (int)(ins.right  * scale);
    int sh = (int)full.height - oy - (int)(ins.bottom * scale);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;

    gfx_metal_set_viewport((int)full.width, (int)full.height, ox, oy);
    plat_ios_set_screen(sw, sh, ox, oy, (float)scale);
}

// The display link's whole job: advance the clock the game thread reads. It
// does NOT draw -- the game thread does, at its own pace, inside the loops it
// already had.
- (void)onFrame:(CADisplayLink *)link {
    plat_ios_tick(link.targetTimestamp - link.timestamp);
}

// --- touches: one contact, in the game's coordinate space -------------------

- (void)publish:(UIEvent *)event {
    UITouch *t = [[event allTouches] anyObject];
    if (!t || t.phase == UITouchPhaseEnded || t.phase == UITouchPhaseCancelled) {
        plat_ios_set_touch(false, 0, 0);
        return;
    }
    CGPoint p = [t locationInView:self];
    CGFloat scale = self.contentScaleFactor;
    plat_ios_set_touch(true, (int)(p.x * scale), (int)(p.y * scale));
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)touches; [self publish:event];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)touches; [self publish:event];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)touches; [self publish:event];
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)touches; [self publish:event];
}

@end

// ---------------------------------------------------------------------------
// The self-test frame (checkpoint 2). Replaced by the game thread in
// checkpoint 3; kept afterwards as the thing to run when the game will not
// start, because it needs nothing but the renderer.
// ---------------------------------------------------------------------------

extern "C" void ob_ios_selftest_frame(void) {
    if (!gfx_metal_ready()) return;
    int w = 0, h = 0;
    plat_ios_screen(&w, &h);

    gfx_frame_begin();
    gfx_clear((Color){ 24, 28, 32, 255 });
    gfx_rect(0, 0, w, 40, (Color){ 170, 0, 0, 255 });                  // title bar
    gfx_rect_lines(8, 48, w - 16, h - 56, (Color){ 200, 200, 120, 255 });
    gfx_rect_rounded(32, 80, 240, 120, 0.15f, 6, (Color){ 20, 40, 90, 255 });
    gfx_rect_rounded_lines(32, 80, 240, 120, 0.15f, 6,
                           (Color){ 250, 220, 60, 255 });
    gfx_triangle((Vector2){ 320, 200 }, (Vector2){ 400, 90 },
                 (Vector2){ 480, 200 }, (Color){ 60, 160, 60, 255 });
    gfx_circle(560, 150, 45.0f, (Color){ 200, 120, 40, 255 });
    gfx_frame_end();
}

// The plist declares landscape-only, but a window with no scene manifest can
// still come up portrait on a Simulator booted that way, so the view
// controller states it too -- belt and braces, and it costs two methods.
@interface OBViewController : UIViewController
@end

@implementation OBViewController
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}
- (BOOL)shouldAutorotate { return YES; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
@end

@interface OBAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation OBAppDelegate

- (UIInterfaceOrientationMask)application:(UIApplication *)application
  supportedInterfaceOrientationsForWindow:(UIWindow *)window {
    (void)application; (void)window;
    return UIInterfaceOrientationMaskLandscape;
}

- (BOOL)application:(UIApplication *)application
        didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application; (void)options;
    CGRect bounds = [UIScreen mainScreen].bounds;
    self.window = [[UIWindow alloc] initWithFrame:bounds];
    OBViewController *vc = [[OBViewController alloc] init];
    vc.view = [[OBMetalView alloc] initWithFrame:bounds];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];

    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationDidBecomeActiveNotification
                    object:nil queue:nil
                usingBlock:^(NSNotification *n) { (void)n; plat_ios_set_active(true); }];
    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationWillResignActiveNotification
                    object:nil queue:nil
                usingBlock:^(NSNotification *n) { (void)n; plat_ios_set_active(false); }];
    return YES;
}

@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil,
                                 NSStringFromClass([OBAppDelegate class]));
    }
}
