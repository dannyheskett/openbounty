// Metal backend for src/gfx.h. The iOS build links no raylib at all, so every
// drawing call the shell makes lands here (docs/IOS-BACKEND-SPIKE.md).
//
// One pipeline, one shader, one vertex buffer. Every primitive the game draws
// is a triangle list of (position, colour, uv) vertices: a filled rectangle is
// two triangles, an outline is four thin rectangles, a rounded corner is a fan,
// a textured blit is two triangles with uv. Vertices accumulate on the CPU for
// the whole frame and go out in as few draw calls as the state changes allow --
// a new call starts only when the bound texture or the scissor rect changes,
// which for a typical frame is a few dozen.
//
// Coordinates: the game draws in WINDOW PIXELS with the origin top-left, the
// same space raylib gave it. The shader converts to Metal's NDC, so no call
// site changes.
//
// Textures: the pack is pixel art. Every texture is sampled nearest-neighbour
// with clamped edges, which is what src/gfx.h's two texture_point* entry points
// ask for; there is no mipmapping and no filtering anywhere.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

extern "C" {
#include "gfx.h"
}
#include "gfx_metal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// The shader. Compiled from source at runtime (newLibraryWithSource) rather
// than pre-built into a .metallib, so the build needs no offline Metal
// compiler -- the whole iOS build is clang plus a plist.
// ---------------------------------------------------------------------------

static const char *kShaderSrc = R"METAL(
#include <metal_stdlib>
using namespace metal;

// Laid out to match the C Vertex exactly: two float2s then a float4, which
// MSL aligns at 0, 8 and 16 for a 32-byte stride. No [[attribute]] qualifiers:
// this is read as a plain buffer, not through a vertex descriptor.
struct VIn {
    float2 pos;
    float2 uv;
    float4 color;
};

struct VOut {
    float4 pos [[position]];
    float2 uv;
    float4 color;
};

struct Uniforms {
    float2 viewport;   // drawable size in pixels
};

vertex VOut ob_vertex(uint vid [[vertex_id]],
                      const device VIn *verts [[buffer(0)]],
                      constant Uniforms &u [[buffer(1)]]) {
    VOut o;
    float2 p = verts[vid].pos;
    // Pixels (top-left origin) -> NDC (centre origin, y up).
    o.pos = float4((p.x / u.viewport.x) * 2.0 - 1.0,
                   1.0 - (p.y / u.viewport.y) * 2.0,
                   0.0, 1.0);
    o.uv = verts[vid].uv;
    o.color = verts[vid].color;
    return o;
}

fragment float4 ob_fragment(VOut in [[stage_in]],
                            texture2d<float> tex [[texture(0)]],
                            sampler smp [[sampler(0)]],
                            constant int &textured [[buffer(0)]]) {
    if (textured == 0) return in.color;
    float4 t = tex.sample(smp, in.uv);
    return t * in.color;   // tint, exactly as DrawTexturePro's tint does
}
)METAL";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} Vertex;

// One batch = a run of vertices sharing a texture and a scissor rect.
typedef struct {
    int      first, count;
    unsigned texture;          // 0 = untextured
    bool     clip;
    int      cx, cy, cw, ch;
} Batch;

#define VERTS_MAX   (64 * 1024)
#define BATCH_MAX   1024
// Every tile, sprite, portrait, backdrop and font atlas is a texture, and the
// pack's map tiles alone are 181 per zone across four zones. 512 was not
// enough: the table filled during sprites_load and the FRAME BUFFER itself
// then failed to allocate, so the game drew a whole frame into nothing and the
// screen stayed black. 4096 is ~32 KB of pointers and several times the most
// any pack has asked for.
#define TEX_MAX     4096

static id<MTLDevice>              s_device;
static id<MTLCommandQueue>        s_queue;
static id<MTLRenderPipelineState> s_pipeline;
static id<MTLSamplerState>        s_sampler;
static CAMetalLayer              *s_layer;

static Vertex  s_verts[VERTS_MAX];
static int     s_vert_count;
static Batch   s_batches[BATCH_MAX];
static int     s_batch_count;

// Texture table. Handle 0 is "none", so slot i is handle i+1 -- the same
// convention raylib's GL names follow, and what src/ob_types.h documents.
static id<MTLTexture> s_textures[TEX_MAX];
static bool           s_tex_used[TEX_MAX];
// Which slots are offscreen render targets. It matters when they are drawn:
// see the vertical-flip rule in gfx_texture_draw.
static bool           s_tex_is_target[TEX_MAX];

static int  s_vp_w = 1, s_vp_h = 1;    // drawable size, device pixels
static int  s_origin_x, s_origin_y;    // safe-area offset
static float s_clear[4] = { 0, 0, 0, 1 };

// The offscreen buffer the game renders its frame into (src/present.c). A
// frame is exactly two passes, never nested: the shell draws everything into
// the target, ends it, then draws the scaled blit plus the touch chrome into
// the drawable. s_target is the pass in progress; nil means the drawable.
static id<MTLTexture> s_target;
static bool  s_clear_pending;          // a gfx_clear since this pass began

static bool  s_clip_on;
static int   s_clip[4];
static unsigned s_cur_tex;

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void gfx_metal_attach(CAMetalLayer *layer) {
    s_layer = layer;
    s_device = MTLCreateSystemDefaultDevice();
    s_layer.device = s_device;
    s_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    s_layer.framebufferOnly = NO;   // the frame buffer is read back by nothing
                                    // today, but a NO here costs nothing and
                                    // keeps a future capture path open
    s_queue = [s_device newCommandQueue];

    NSError *err = nil;
    id<MTLLibrary> lib =
        [s_device newLibraryWithSource:[NSString stringWithUTF8String:kShaderSrc]
                               options:nil
                                 error:&err];
    if (!lib) {
        NSLog(@"openbounty: Metal shader failed: %@", err);
        return;
    }

    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction   = [lib newFunctionWithName:@"ob_vertex"];
    pd.fragmentFunction = [lib newFunctionWithName:@"ob_fragment"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    // Straight alpha blending: the pack's sprites have hard alpha edges and
    // the overlays fade with a tint alpha.
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
    pd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
    pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    s_pipeline = [s_device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!s_pipeline) NSLog(@"openbounty: Metal pipeline failed: %@", err);
    else NSLog(@"openbounty: Metal pipeline ready");

    MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterNearest;
    sd.magFilter = MTLSamplerMinMagFilterNearest;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    s_sampler = [s_device newSamplerStateWithDescriptor:sd];
}

void gfx_metal_set_viewport(int width, int height, int origin_x, int origin_y) {
    s_vp_w = width  > 0 ? width  : 1;
    s_vp_h = height > 0 ? height : 1;
    s_origin_x = origin_x;
    s_origin_y = origin_y;
}

bool gfx_metal_ready(void) { return s_pipeline != nil; }

// ---------------------------------------------------------------------------
// Vertex accumulation
// ---------------------------------------------------------------------------

static void batch_break(void) {
    if (s_batch_count >= BATCH_MAX) return;
    Batch *b = &s_batches[s_batch_count++];
    b->first   = s_vert_count;
    b->count   = 0;
    b->texture = s_cur_tex;
    b->clip    = s_clip_on;
    b->cx = s_clip[0]; b->cy = s_clip[1]; b->cw = s_clip[2]; b->ch = s_clip[3];
}

static Batch *batch_current(void) {
    if (s_batch_count == 0) batch_break();
    Batch *b = &s_batches[s_batch_count - 1];
    // A change of texture or clip starts a new draw call.
    if (b->texture != s_cur_tex || b->clip != s_clip_on ||
        (s_clip_on && (b->cx != s_clip[0] || b->cy != s_clip[1] ||
                       b->cw != s_clip[2] || b->ch != s_clip[3]))) {
        batch_break();
        b = &s_batches[s_batch_count - 1];
    }
    return b;
}

static float s_zoom = 1.0f;

static void push(float x, float y, float u, float v, Color c) {
    // Decide the batch BEFORE appending: batch_current() may start a new one
    // at s_vert_count, and a new batch must begin AT this vertex, not after it.
    Batch *b = batch_current();
    if (s_vert_count >= VERTS_MAX) return;   // a dropped vertex, not a crash
    Vertex *o = &s_verts[s_vert_count++];
    // The safe-area offset belongs to the DRAWABLE only: inside the offscreen
    // buffer, (0,0) is the buffer's own corner and the notch is irrelevant.
    float ox = s_target ? 0.0f : (float)s_origin_x;
    float oy = s_target ? 0.0f : (float)s_origin_y;
    o->x = ox + x * s_zoom;
    o->y = oy + y * s_zoom;
    o->u = u; o->v = v;
    o->r = c.r / 255.0f; o->g = c.g / 255.0f;
    o->b = c.b / 255.0f; o->a = c.a / 255.0f;
    b->count++;
}

// A quad as two triangles, with uv corners for the textured case.
static void quad(float x0, float y0, float x1, float y1,
                 float u0, float v0, float u1, float v1, Color c) {
    // The first few quads of the first few frames, so a black screen can be
    // read: what was drawn, where, with which texture and colour.
    static int s_logged;
    if (s_logged < 24) {
        NSLog(@"openbounty: quad %d %s tex=%u (%.0f,%.0f)-(%.0f,%.0f) rgba=%d,%d,%d,%d",
              s_logged, s_target ? "target" : "drawable", s_cur_tex,
              x0, y0, x1, y1, c.r, c.g, c.b, c.a);
        s_logged++;
    }
    push(x0, y0, u0, v0, c); push(x1, y0, u1, v0, c); push(x1, y1, u1, v1, c);
    push(x0, y0, u0, v0, c); push(x1, y1, u1, v1, c); push(x0, y1, u0, v1, c);
}

// ---------------------------------------------------------------------------
// src/gfx.h -- frame
// ---------------------------------------------------------------------------

static void pass_reset(void) {
    s_vert_count = 0;
    s_batch_count = 0;
    s_cur_tex = 0;
    s_clip_on = false;
    s_clear_pending = false;
}

void gfx_frame_begin(void) { pass_reset(); }

void gfx_clear(Color color) {
    // Recorded, not drawn: it becomes this pass's clear colour, which is both
    // cheaper than a full-screen quad and what makes the area outside the safe
    // inset match the frame.
    s_clear[0] = color.r / 255.0f;
    s_clear[1] = color.g / 255.0f;
    s_clear[2] = color.b / 255.0f;
    s_clear[3] = color.a / 255.0f;
    s_clear_pending = true;
}

// Submit `count` vertices from `first` into `enc`.
static void encode_batches(id<MTLRenderCommandEncoder> enc, int vp_w, int vp_h) {
    struct { float w, h; } uniforms = { (float)vp_w, (float)vp_h };
    [enc setRenderPipelineState:s_pipeline];
    [enc setVertexBytes:&uniforms length:sizeof uniforms atIndex:1];
    [enc setFragmentSamplerState:s_sampler atIndex:0];

    // A FRESH buffer per pass, not one shared buffer refilled.
    //
    // A frame is two passes -- the offscreen buffer, then the drawable -- and
    // the CPU runs far ahead of the GPU: refilling one buffer meant the second
    // pass's vertices overwrote the first's before the GPU had drawn it, so
    // the game's whole frame was rendered with the blit's six vertices and the
    // screen came out black. Metal keeps a buffer alive until the command
    // buffer that references it completes, so handing each pass its own is
    // both correct and self-managing. Two small allocations a frame.
    id<MTLBuffer> vbuf = nil;
    if (s_vert_count > 0) {
        vbuf = [s_device newBufferWithBytes:s_verts
                                     length:sizeof(Vertex) * (NSUInteger)s_vert_count
                                    options:MTLResourceStorageModeShared];
    }
    if (!vbuf) return;

    for (int i = 0; i < s_batch_count; i++) {
        Batch *b = &s_batches[i];
        if (b->count <= 0) continue;
        if (b->clip) {
            MTLScissorRect r;
            int ox = s_target ? 0 : s_origin_x;
            int oy = s_target ? 0 : s_origin_y;
            int x = b->cx + ox, y = b->cy + oy;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            int w = b->cw, h = b->ch;
            if (x + w > vp_w) w = vp_w - x;
            if (y + h > vp_h) h = vp_h - y;
            if (w <= 0 || h <= 0) continue;
            r.x = (NSUInteger)x; r.y = (NSUInteger)y;
            r.width = (NSUInteger)w; r.height = (NSUInteger)h;
            [enc setScissorRect:r];
        } else {
            MTLScissorRect full = { 0, 0, (NSUInteger)vp_w, (NSUInteger)vp_h };
            [enc setScissorRect:full];
        }
        int textured = b->texture ? 1 : 0;
        [enc setFragmentBytes:&textured length:sizeof textured atIndex:0];
        if (b->texture && b->texture <= TEX_MAX && s_tex_used[b->texture - 1])
            [enc setFragmentTexture:s_textures[b->texture - 1] atIndex:0];
        [enc setVertexBuffer:vbuf
                      offset:sizeof(Vertex) * (NSUInteger)b->first
                     atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:(NSUInteger)b->count];
    }
}

// Encode whatever has accumulated into `tex` (nil = this frame's drawable) and
// submit it. The viewport the shader normalises against is the target's own
// size, so a pass into the offscreen buffer uses the buffer's dimensions.
static void pass_flush(id<MTLTexture> tex, id<CAMetalDrawable> drawable) {
    if (!s_pipeline) return;
    id<MTLTexture> dst = tex ? tex : (drawable ? drawable.texture : nil);
    if (!dst) return;
    static int s_passes;
    if (s_passes < 8) {
        NSLog(@"openbounty: pass %d -> %s %lux%lu, %d verts, %d batches, clear=%d",
              s_passes, tex ? "TARGET" : "drawable",
              (unsigned long)dst.width, (unsigned long)dst.height,
              s_vert_count, s_batch_count, s_clear_pending ? 1 : 0);
    }
    s_passes++;

    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction =
        s_clear_pending ? MTLLoadActionClear : MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor =
        MTLClearColorMake(s_clear[0], s_clear[1], s_clear[2], s_clear[3]);

    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    encode_batches(enc, (int)dst.width, (int)dst.height);
    [enc endEncoding];
    if (drawable) [cb presentDrawable:drawable];
    [cb commit];

    // Read one pixel back out of the offscreen buffer for the first few
    // frames. A black screen with correct-looking geometry has exactly two
    // explanations -- the target pass is not drawing, or the drawable pass is
    // not sampling what it drew -- and this tells them apart.
    if (tex && s_passes <= 8) {
        [cb waitUntilCompleted];
        unsigned char px[4] = { 0, 0, 0, 0 };
        MTLRegion r = MTLRegionMake2D((NSUInteger)(dst.width / 2),
                                      (NSUInteger)(dst.height / 2), 1, 1);
        [tex getBytes:px bytesPerRow:4 fromRegion:r mipmapLevel:0];
        NSLog(@"openbounty: target centre pixel = %d,%d,%d,%d",
              px[0], px[1], px[2], px[3]);
    }
    pass_reset();
}

void gfx_frame_end(void) {
    static int s_frames;
    if (s_frames < 3) NSLog(@"openbounty: frame_end #%d, %d verts, %d batches",
                            s_frames, s_vert_count, s_batch_count);
    s_frames++;
    if (!s_pipeline || !s_layer) return;
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [s_layer nextDrawable];
        if (!drawable) { pass_reset(); return; }
        pass_flush(nil, drawable);
    }
}

// ---------------------------------------------------------------------------
// src/gfx.h -- primitives
// ---------------------------------------------------------------------------

void gfx_rect(int x, int y, int w, int h, Color color) {
    if (w <= 0 || h <= 0) return;
    s_cur_tex = 0;
    quad((float)x, (float)y, (float)(x + w), (float)(y + h), 0, 0, 0, 0, color);
}

void gfx_rect_lines(int x, int y, int w, int h, Color color) {
    if (w <= 0 || h <= 0) return;
    // raylib's DrawRectangleLines is a 1px outline drawn INSIDE the rect.
    gfx_rect(x, y, w, 1, color);
    gfx_rect(x, y + h - 1, w, 1, color);
    gfx_rect(x, y + 1, 1, h - 2, color);
    gfx_rect(x + w - 1, y + 1, 1, h - 2, color);
}

// A rounded rectangle as the middle cross plus four corner fans. `roundness`
// is raylib's: a fraction of the shorter side.
static void rounded(int x, int y, int w, int h, float roundness, int segments,
                    Color color, bool outline) {
    if (w <= 0 || h <= 0) return;
    if (roundness <= 0.0f) {
        if (outline) gfx_rect_lines(x, y, w, h, color);
        else         gfx_rect(x, y, w, h, color);
        return;
    }
    if (roundness > 1.0f) roundness = 1.0f;
    if (segments < 2) segments = 2;
    float shorter = (w < h) ? (float)w : (float)h;
    float r = roundness * shorter * 0.5f;
    if (r < 1.0f) r = 1.0f;

    s_cur_tex = 0;
    if (!outline) {
        // Middle bar, then the two side bars: the corners are the fans below.
        gfx_rect(x, (int)(y + r), w, (int)(h - 2 * r), color);
        gfx_rect((int)(x + r), y, (int)(w - 2 * r), (int)r, color);
        gfx_rect((int)(x + r), (int)(y + h - r), (int)(w - 2 * r), (int)r, color);
    }

    const float cxs[4] = { x + r, x + w - r, x + w - r, x + r };
    const float cys[4] = { y + r, y + r, y + h - r, y + h - r };
    const float start[4] = { 180.0f, 270.0f, 0.0f, 90.0f };
    for (int c = 0; c < 4; c++) {
        for (int s = 0; s < segments; s++) {
            float a0 = (start[c] + 90.0f * (float)s / segments) * 3.14159265f / 180.0f;
            float a1 = (start[c] + 90.0f * (float)(s + 1) / segments) * 3.14159265f / 180.0f;
            float x0 = cxs[c] + cosf(a0) * r, y0 = cys[c] + sinf(a0) * r;
            float x1 = cxs[c] + cosf(a1) * r, y1 = cys[c] + sinf(a1) * r;
            if (outline) {
                // A 1px chord: a thin quad along the arc segment.
                float dx = x1 - x0, dy = y1 - y0;
                float len = sqrtf(dx * dx + dy * dy);
                if (len < 0.001f) continue;
                float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;
                push(x0 - nx, y0 - ny, 0, 0, color);
                push(x1 - nx, y1 - ny, 0, 0, color);
                push(x1 + nx, y1 + ny, 0, 0, color);
                push(x0 - nx, y0 - ny, 0, 0, color);
                push(x1 + nx, y1 + ny, 0, 0, color);
                push(x0 + nx, y0 + ny, 0, 0, color);
            } else {
                push(cxs[c], cys[c], 0, 0, color);
                push(x0, y0, 0, 0, color);
                push(x1, y1, 0, 0, color);
            }
        }
    }
    if (outline) {
        // The four straight runs between the corners.
        gfx_rect((int)(x + r), y, (int)(w - 2 * r), 1, color);
        gfx_rect((int)(x + r), y + h - 1, (int)(w - 2 * r), 1, color);
        gfx_rect(x, (int)(y + r), 1, (int)(h - 2 * r), color);
        gfx_rect(x + w - 1, (int)(y + r), 1, (int)(h - 2 * r), color);
    }
}

void gfx_rect_rounded(int x, int y, int w, int h, float roundness,
                      int segments, Color color) {
    rounded(x, y, w, h, roundness, segments, color, false);
}

void gfx_rect_rounded_lines(int x, int y, int w, int h, float roundness,
                            int segments, Color color) {
    rounded(x, y, w, h, roundness, segments, color, true);
}

void gfx_triangle(Vector2 a, Vector2 b, Vector2 c, Color color) {
    s_cur_tex = 0;
    // Winding-independent: the pipeline has no culling, so either order draws.
    push(a.x, a.y, 0, 0, color);
    push(b.x, b.y, 0, 0, color);
    push(c.x, c.y, 0, 0, color);
}

void gfx_circle(int cx, int cy, float radius, Color color) {
    if (radius <= 0.0f) return;
    s_cur_tex = 0;
    const int segs = 24;
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i * 2.0f * 3.14159265f / segs;
        float a1 = (float)(i + 1) * 2.0f * 3.14159265f / segs;
        push((float)cx, (float)cy, 0, 0, color);
        push(cx + cosf(a0) * radius, cy + sinf(a0) * radius, 0, 0, color);
        push(cx + cosf(a1) * radius, cy + sinf(a1) * radius, 0, 0, color);
    }
}

// ---------------------------------------------------------------------------
// src/gfx.h -- chrome labels
// ---------------------------------------------------------------------------
//
// The touch chrome's captions ("Esc", "Y", "N") -- the only text this layer
// draws; everything else is the pack's own font through src/bfont.c.
//
// NOT YET IMPLEMENTED (checkpoint 5): it needs a small baked atlas of the
// system font. Until then a caption draws nothing rather than a placeholder
// box: src/touch.c has already drawn the button's fill and border, so the
// control is visible and tappable, just unlabelled. The width is the metric
// raylib's default font gives for a 1:2 cell, which is what the caller uses to
// centre and to shrink-to-fit.
void gfx_label(const char *text, int x, int y, int size, Color color) {
    (void)text; (void)x; (void)y; (void)size; (void)color;
}

int gfx_label_width(const char *text, int size) {
    int n = 0;
    while (text && text[n]) n++;
    return n * size / 2;
}

// ---------------------------------------------------------------------------
// src/gfx.h -- clipping
// ---------------------------------------------------------------------------

void gfx_clip_begin(int x, int y, int w, int h) {
    s_clip_on = true;
    s_clip[0] = x; s_clip[1] = y; s_clip[2] = w; s_clip[3] = h;
}

void gfx_clip_end(void) { s_clip_on = false; }

// ---------------------------------------------------------------------------
// src/gfx.h -- textures
// ---------------------------------------------------------------------------

static int tex_alloc(void) {
    for (int i = 0; i < TEX_MAX; i++) if (!s_tex_used[i]) return i;
    // Loud, because the failure mode is silent: a texture id of 0 draws
    // nothing at all, and a frame buffer that fails to allocate takes the
    // whole screen with it.
    NSLog(@"openbounty: OUT OF TEXTURE SLOTS (%d) -- raise TEX_MAX", TEX_MAX);
    return -1;
}

Texture2D gfx_texture_from_image(Image img) {
    Texture2D out = { 0, 0, 0, 0, 0 };
    if (!s_device || !img.data || img.width <= 0 || img.height <= 0) return out;
    int slot = tex_alloc();
    if (slot < 0) return out;

    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:(NSUInteger)img.width
                                    height:(NSUInteger)img.height
                                 mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> t = [s_device newTextureWithDescriptor:td];
    if (!t) return out;
    // src/font_backend.h and src/assets.c both hand over 8-bit RGBA, which is
    // what stb_image gives with 4 requested components.
    MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)img.width,
                                       (NSUInteger)img.height);
    [t replaceRegion:region
         mipmapLevel:0
           withBytes:img.data
         bytesPerRow:(NSUInteger)img.width * 4];

    s_textures[slot] = t;
    s_tex_used[slot] = true;
    out.id = (unsigned)(slot + 1);
    out.width = img.width;
    out.height = img.height;
    return out;
}

void gfx_texture_free(Texture2D t) {
    if (t.id == 0 || t.id > TEX_MAX) return;
    s_textures[t.id - 1] = nil;
    s_tex_used[t.id - 1] = false;
    s_tex_is_target[t.id - 1] = false;
}

// Nearest + clamp is the sampler's only mode, so both of these are already
// true of every texture and nothing has to change.
void gfx_texture_point(Texture2D t) { (void)t; }
void gfx_texture_point_clamp(Texture2D t) { (void)t; }

void gfx_texture_draw(Texture2D t, Rectangle src, Rectangle dst, Color tint) {
    if (t.id == 0 || t.width <= 0 || t.height <= 0) return;
    s_cur_tex = t.id;
    // raylib's convention: a NEGATIVE source width or height means "the same
    // rectangle, sampled mirrored" -- it is how a render target, which is
    // stored bottom-up, is blitted the right way up. So take the magnitude
    // first and swap the two edges; using the signed value directly gives a
    // v range of -1..0, which clamps to the texture's top row and drew the
    // whole frame as one flat band of whatever colour that row was. Black.
    float w = src.width  < 0 ? -src.width  : src.width;
    float h = src.height < 0 ? -src.height : src.height;
    float u0 = src.x / (float)t.width;
    float v0 = src.y / (float)t.height;
    float u1 = (src.x + w) / (float)t.width;
    float v1 = (src.y + h) / (float)t.height;
    if (src.width < 0) { float tmp = u0; u0 = u1; u1 = tmp; }

    // The vertical flip, and why a render target is exempt.
    //
    // raylib stores a render texture bottom-up, as OpenGL does, so every
    // caller that blits one passes a negative height to mirror it back the
    // right way up (src/present.c does exactly this). A Metal texture that
    // has been rendered into is stored TOP-DOWN, the same way up as any other
    // texture here, so obeying that negative sign mirrors a frame that was
    // already correct -- which drew the whole game upside down.
    bool flip_v = (src.height < 0);
    if (t.id && t.id <= TEX_MAX && s_tex_is_target[t.id - 1]) flip_v = false;
    if (flip_v) { float tmp = v0; v0 = v1; v1 = tmp; }
    quad(dst.x, dst.y, dst.x + dst.width, dst.y + dst.height,
         u0, v0, u1, v1, tint);
    s_cur_tex = 0;
}

// ---------------------------------------------------------------------------
// src/gfx.h -- CPU images
// ---------------------------------------------------------------------------
//
// Implemented in ios/image_ios.c against stb_image, which is plain C: only the
// GPU side belongs in this file.

// ---------------------------------------------------------------------------
// src/gfx.h -- camera + offscreen target
// ---------------------------------------------------------------------------

// The whole-frame integer zoom a pack with a large fixed buffer uses
// (src/present.c). Applied in push(), so it scales about the origin exactly as
// raylib's Camera2D with a zero offset did.
void gfx_zoom_begin(float zoom) { s_zoom = zoom > 0 ? zoom : 1.0f; }
void gfx_zoom_end(void) { s_zoom = 1.0f; }

RenderTexture2D gfx_target_create(int w, int h) {
    RenderTexture2D rt;
    memset(&rt, 0, sizeof rt);
    if (!s_device || w <= 0 || h <= 0) return rt;
    int slot = tex_alloc();
    if (slot < 0) return rt;

    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:(NSUInteger)w
                                    height:(NSUInteger)h
                                 mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    id<MTLTexture> t = [s_device newTextureWithDescriptor:td];
    if (!t) return rt;

    s_textures[slot] = t;
    s_tex_used[slot] = true;
    s_tex_is_target[slot] = true;
    rt.id = (unsigned)(slot + 1);
    rt.texture.id = rt.id;
    rt.texture.width = w;
    rt.texture.height = h;
    return rt;
}

void gfx_target_free(RenderTexture2D rt) { gfx_texture_free(rt.texture); }

void gfx_target_begin(RenderTexture2D rt) {
    if (rt.id == 0 || rt.id > TEX_MAX) return;
    // A pass never nests: the shell draws the whole frame into the target,
    // ends it, and only then touches the drawable. Anything accumulated before
    // this call would belong to no pass, so start clean.
    pass_reset();
    s_target = s_textures[rt.id - 1];
}

void gfx_target_end(void) {
    if (s_target) {
        @autoreleasepool { pass_flush(s_target, nil); }
        s_target = nil;
    }
}
