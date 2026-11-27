/* Pilot Canvas core: native gfxeng/Sprite backend, atomic batch primitives,
 * reusable grid / chart / 5x7 OSD text drawing, and GOT / draw helpers. */

#include "pilot_canvas_internal.h"

struct gfx_symbols g_sym;
int g_sym_ready;
const PilotApi *g_api;

static uintptr_t g_appfw_base_cached;

void mlog(const char *s)
{
    if (g_api && g_api->mlog && g_api->mlog->print)
        (void)g_api->mlog->print(g_api->mlog, s);
}

void mlogf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    mlog(buf);
}

u64 canvas_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (u64)ts.tv_sec * 1000ull + (u64)(ts.tv_nsec / 1000000l);
}

static int appfw_phdr_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    uintptr_t *out = (uintptr_t *)data;
    if (info->dlpi_name && strstr(info->dlpi_name, "appFw.so")) {
        *out = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

uintptr_t canvas_appfw_base(void)
{
    if (!g_appfw_base_cached)
        dl_iterate_phdr(appfw_phdr_cb, &g_appfw_base_cached);
    return g_appfw_base_cached;
}

int canvas_patch_appfw_got(uintptr_t got_off, void *hook)
{
    uintptr_t base = canvas_appfw_base();
    if (!base || !g_api || !g_api->memory_read || !g_api->memory_patch)
        return 0;
    uintptr_t got = base + got_off;
    uintptr_t cur = 0;
    if (g_api->memory_read(got, &cur, sizeof(cur)) != PILOT_STATUS_OK)
        return 0;
    uintptr_t repl = (uintptr_t)hook;
    return g_api->memory_patch(got, &cur, &repl, sizeof(repl)) == PILOT_STATUS_OK;
}

int canvas_patch_exact_got(uintptr_t got_off, uintptr_t expected_fn_off, void *hook)
{
    uintptr_t base = canvas_appfw_base();
    if (!base || !g_api || !g_api->memory_read || !g_api->memory_patch)
        return 0;
    uintptr_t got = base + got_off;
    uintptr_t expected = base + expected_fn_off;
    uintptr_t cur = 0;
    if (g_api->memory_read(got, &cur, sizeof(cur)) != PILOT_STATUS_OK ||
        cur != expected)
        return 0;
    uintptr_t repl = (uintptr_t)hook;
    return g_api->memory_patch(got, &expected, &repl, sizeof(repl)) == PILOT_STATUS_OK;
}

/* Thread-safe async invalidation: wakes Sony's UI/render thread without
 * invoking GraphicsEngine::drawFramebuffer() from a background thread. */
void canvas_notify_draw_async(void)
{
    union { void *p; void *(*get)(void); } ws_get;
    union { void *p; void (*fn)(void *); } ws_notify;
    ws_get.p = dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem15sys_getInstanceEv");
    ws_notify.p = dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem10notifyDrawEv");
    if (ws_get.p && ws_notify.p) {
        void *ws = ws_get.get();
        if (ws)
            ws_notify.fn(ws);
    }
}

/* Synchronous commit: MUST ONLY be called from Sony's UI thread (touch/DISP hooks). */
void canvas_notify_draw_sync(void)
{
    canvas_notify_draw_async();
    union { void *p; void *(*get)(void); } ge_get;
    union { void *p; void (*fn)(void *); } ge_draw;
    ge_get.p = dlsym(RTLD_DEFAULT, "_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv");
    ge_draw.p = dlsym(RTLD_DEFAULT, "_ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv");
    if (ge_get.p && ge_draw.p) {
        void *ge = ge_get.get();
        if (ge)
            ge_draw.fn(ge);
    }
}

/* ---- native symbol resolution ------------------------------------------- */
static int addr_in_native_module(void *p)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f)
        return 1;
    char line[512];
    uintptr_t a = (uintptr_t)p;
    int ok = 0;
    while (fgets(line, sizeof(line), f)) {
        uintptr_t lo = 0, hi = 0;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && a >= lo && a < hi) {
            ok = (strstr(line, "libObj.so") != NULL ||
                  strstr(line, "appFw.so") != NULL ||
                  strstr(line, "libOb5.so") != NULL);
            break;
        }
    }
    fclose(f);
    return ok;
}

static void *resolve_native(const char *name)
{
    void *p = dlsym(RTLD_DEFAULT, name);
    if (!p)
        return NULL;
    if (!addr_in_native_module(p)) {
        mlogf("CANVAS_SYM_OUTSIDE %s=%p", name, p);
        return NULL;
    }
    return p;
}

int canvas_resolve_symbols(void)
{
    if (g_sym_ready)
        return 1;

    union { void *p; fn_resmgr_instance f; } a, ux;
    union { void *p; fn_param_ctor f; } b;
    union { void *p; fn_create_image f; } c;
    union { void *p; fn_image_raw f; } d;
    union { void *p; fn_image_dim f; } e, f2;
    union { void *p; fn_image_format f; } g;
    union { void *p; fn_image_ctor f; } h;
    union { void *p; fn_image_dtor f; } h2;
    union { void *p; fn_gfx_copy f; } i;
    union { void *p; fn_gfx_clear f; } i2;
    union { void *p; fn_sprite_ctor f; } j;
    union { void *p; fn_sprite_dtor f; } j2;
    union { void *p; fn_sprite_set_count f; } j3;
    union { void *p; fn_sprite_rect_color f; } k;
    union { void *p; fn_sprite_rect_size f; } l;
    union { void *p; fn_sprite_rect_pos f; } m;
    union { void *p; fn_widget_init_sprite f; } n;
    union { void *p; fn_widget_init_node f; } n2;
    union { void *p; fn_widget_ctor f; } o;
    union { void *p; fn_widget_set_size f; } p2;
    union { void *p; fn_widget_set_pos f; } q;
    union { void *p; fn_widget_set_parent f; } r;
    union { void *p; fn_text_ctor f; } s2;
    union { void *p; fn_text_dtor f; } s3;
    union { void *p; fn_text_set_string f; } t2;
    union { void *p; fn_text_set_font f; } u2;
    union { void *p; fn_text_draw f; } t3, t4;
    union { void *p; fn_text_set_f2 f; } t5, t6;
    union { void *p; fn_elem_set_pos f; } t7;
    union { void *p; fn_elem_set_visible f; } t8;
    union { void *p; fn_set_mouse_hook f; } v2;

    a.p  = resolve_native("_ZN2ux6resmgr15ResourceManager15sys_getInstanceEv");
    ux.p = resolve_native("_ZN8UXEngine6resmgrEv");
    b.p  = resolve_native("_ZN5libra16CreateImageParamC1Etti");
    c.p  = resolve_native("_ZN2ux6resmgr15ResourceManager11createImageERKN5libra16CreateImageParamEPNS2_9ImageDataE");
    d.p  = resolve_native("_ZN5libra9ImageData16sys_getImageDataEv");
    e.p  = resolve_native("_ZNK5libra9ImageData8getWidthEv");
    f2.p = resolve_native("_ZNK5libra9ImageData9getHeightEv");
    g.p  = resolve_native("_ZNK5libra9ImageData14getPixelFormatEv");
    h.p  = resolve_native("_ZN5libra9ImageDataC1Ev");
    h2.p = resolve_native("_ZN5libra9ImageDataD1Ev");
    i.p  = resolve_native("_ZN2ux4core13GraphicsImage4copyEPvssssS2_ssssbbb");
    i2.p = resolve_native("_ZN2ux4core13GraphicsImage5clearEPvRKN6stella6CColorE");
    j.p  = resolve_native("_ZN2ux6gfxeng6SpriteC1Ev");
    j2.p = resolve_native("_ZN2ux6gfxeng6SpriteD1Ev");
    j3.p = resolve_native("_ZN2ux6gfxeng6Sprite13setCurRectNumEt");
    k.p  = resolve_native("_ZN2ux6gfxeng6Sprite12setRectColorEtRKN6stella6CColorE");
    l.p  = resolve_native("_ZN2ux6gfxeng6Sprite11setRectSizeEtff");
    m.p  = resolve_native("_ZN2ux6gfxeng6Sprite10setRectPosEtRKNS_4core7Vector2E");
    n.p  = resolve_native("_ZN2ux6wgtsys6Widget10initSpriteERNS_6gfxeng6SpriteEt");
    n2.p = resolve_native("_ZN2ux6wgtsys6Widget8initNodeERNS_6gfxeng4NodeE");
    o.p  = resolve_native("_ZN2ux6wgtsys6WidgetC1Ev");
    p2.p = resolve_native("_ZN2ux6wgtsys6Widget7setSizeEff");
    q.p  = resolve_native("_ZN2ux6wgtsys6Widget11setLocalPosERKNS_4core7Vector2E");
    r.p  = resolve_native("_ZN2ux6wgtsys6Widget9setParentERNS0_10WidgetBaseE");
    s2.p = resolve_native("_ZN2ux6gfxeng19MultiLineTextSpriteC1Ev");
    s3.p = resolve_native("_ZN2ux6gfxeng19MultiLineTextSpriteD1Ev");
    t2.p = resolve_native("_ZN2ux6gfxeng10TextSprite9setStringEPKc");
    u2.p = resolve_native("_ZN2ux6gfxeng10TextSprite7setFontEPjh");
    t3.p = resolve_native("_ZN2ux6gfxeng10TextSprite8drawTextEv");
    t4.p = resolve_native("_ZN2ux6gfxeng10TextSprite14freezeDrawTextEv");
    t5.p = resolve_native("_ZN2ux6gfxeng10TextSprite11setClipSizeEff");
    t6.p = resolve_native("_ZN2ux6gfxeng4Node10setClipPosEff");
    t7.p = resolve_native("_ZN2ux6gfxeng7Element11setLocalPosERKNS_4core7Vector2E");
    t8.p = resolve_native("_ZN2ux6gfxeng7Element10setVisibleEb");
    v2.p = resolve_native("_ZN2ux6wgtsys10WidgetBase17setMouseEventHookEb");

    int missing = 0;
#define CHECK(x) do { if (!(x)) { mlogf("CANVAS_SYM_MISSING %s", #x); ++missing; } } while (0)
    CHECK(a.p); CHECK(ux.p); CHECK(b.p); CHECK(c.p); CHECK(d.p); CHECK(e.p);
    CHECK(f2.p); CHECK(g.p); CHECK(h.p); CHECK(h2.p); CHECK(i.p); CHECK(i2.p);
#undef CHECK
    if (missing)
        return 0;

    g_sym.resmgr_instance    = a.f;
    g_sym.ux_resmgr          = ux.f;
    g_sym.param_ctor         = b.f;
    g_sym.create_image       = c.f;
    g_sym.image_raw          = d.f;
    g_sym.image_width        = e.f;
    g_sym.image_height       = f2.f;
    g_sym.image_format       = g.f;
    g_sym.image_ctor         = h.f;
    g_sym.image_dtor         = h2.f;
    g_sym.gfx_copy           = i.f;
    g_sym.gfx_clear          = i2.f;
    g_sym.sprite_ctor        = j.f;
    g_sym.sprite_dtor        = j2.f;
    g_sym.sprite_set_count   = j3.f;
    g_sym.sprite_rect_color  = k.f;
    g_sym.sprite_rect_size   = l.f;
    g_sym.sprite_rect_pos    = m.f;
    g_sym.widget_init_sprite = n.f;
    g_sym.widget_init_node   = n2.f;
    g_sym.widget_ctor        = o.f;
    g_sym.widget_set_size    = p2.f;
    g_sym.widget_set_pos     = q.f;
    g_sym.widget_set_parent  = r.f;
    g_sym.text_ctor          = s2.f;
    g_sym.text_dtor          = s3.f;
    g_sym.text_set_string    = t2.f;
    g_sym.text_set_font      = u2.f;
    g_sym.text_draw          = t3.f;
    g_sym.text_freeze        = t4.f;
    g_sym.text_set_clip_size = t5.f;
    g_sym.node_set_clip_pos  = t6.f;
    g_sym.elem_set_local_pos = t7.f;
    g_sym.elem_set_visible   = t8.f;
    g_sym.set_mouse_hook     = v2.f;
    g_sym_ready = 1;
    mlogf("CANVAS_SYMS_OK text=%d/%d/%d/%d/%d",
          g_sym.text_ctor != NULL, g_sym.text_draw != NULL,
          g_sym.elem_set_local_pos != NULL, g_sym.elem_set_visible != NULL,
          g_sym.widget_init_node != NULL);
    return 1;
}

/* ---- PIXEL backend blend helpers ---------------------------------------- */
static u32 blend_pixel(u32 dst, u32 src, PilotCanvasBlend mode)
{
    if (mode == PILOT_CANVAS_BLEND_NONE)
        return src;
    int sa = (int)((src >> 24) & 0xFFu);
    if (sa == 0)
        return dst;
    if (mode == PILOT_CANVAS_BLEND_ADD) {
        int r = (int)((dst >> 16) & 0xFFu) + (int)((src >> 16) & 0xFFu);
        int g = (int)((dst >> 8) & 0xFFu) + (int)((src >> 8) & 0xFFu);
        int b = (int)(dst & 0xFFu) + (int)(src & 0xFFu);
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    }
    int sr = (int)((src >> 16) & 0xFFu), sg = (int)((src >> 8) & 0xFFu);
    int sb = (int)(src & 0xFFu);
    int dr = (int)((dst >> 16) & 0xFFu), dg = (int)((dst >> 8) & 0xFFu);
    int db = (int)(dst & 0xFFu);
    int da = (int)((dst >> 24) & 0xFFu);
    int r = sr + (dr * (255 - sa)) / 255;
    int g = sg + (dg * (255 - sa)) / 255;
    int b = sb + (db * (255 - sa)) / 255;
    int a = sa + (da * (255 - sa)) / 255;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (a > 255) a = 255;
    return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

static void mark_dirty(PilotCanvas *c, int x, int y, int w, int h)
{
    if (!c->has_dirty) {
        c->dx = x; c->dy = y; c->dw = w; c->dh = h;
        c->has_dirty = 1;
        return;
    }
    int x1 = c->dx < x ? c->dx : x;
    int y1 = c->dy < y ? c->dy : y;
    int x2 = (c->dx + c->dw) > (x + w) ? (c->dx + c->dw) : (x + w);
    int y2 = (c->dy + c->dh) > (y + h) ? (c->dy + c->dh) : (y + h);
    c->dx = x1; c->dy = y1; c->dw = x2 - x1; c->dh = y2 - y1;
}

/* ---- exported Canvas API ------------------------------------------------ */
__attribute__((visibility("default")))
PilotCanvas *pilot_canvas_create_v1(u16 w, u16 h, PilotCanvasBackend backend)
{
    if (w == 0 || h == 0 || w > 1920 || h > 1080)
        return NULL;
    if (!g_sym_ready && !canvas_resolve_symbols())
        return NULL;

    PilotCanvas *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->w = w;
    c->h = h;
    c->backend = backend;

    if (backend == PILOT_CANVAS_BACKEND_RECT) {
        if (!g_sym.sprite_ctor || !g_sym.sprite_set_count ||
            !g_sym.sprite_rect_pos || !g_sym.sprite_rect_size ||
            !g_sym.sprite_rect_color) {
            free(c);
            return NULL;
        }
        c->sprite = calloc(1, 0x200);
        if (!c->sprite) {
            free(c);
            return NULL;
        }
        g_sym.sprite_ctor(c->sprite);
        c->rect_capacity = PILOT_CANVAS_MAX_RECTS;
        mlogf("CANVAS_CREATE_RECT ok w=%u h=%u cap=%u sprite=%p",
              (unsigned)w, (unsigned)h, (unsigned)c->rect_capacity, c->sprite);
        return c;
    }

    c->pixel_bytes = (size_t)w * (size_t)h * 4u;
    c->pixels = calloc(1, c->pixel_bytes);
    c->image_data = calloc(1, 256);
    void *param = calloc(1, 128);
    if (!c->pixels || !c->image_data || !param) {
        free(param); free(c->image_data); free(c->pixels); free(c);
        return NULL;
    }
    g_sym.image_ctor(c->image_data);
    (void)g_sym.param_ctor(param, w, h, 1);
    (void)g_sym.create_image(param, c->image_data);
    free(param);
    c->raw = g_sym.image_raw(c->image_data);
    return c;
}

__attribute__((visibility("default")))
void pilot_canvas_destroy_v1(PilotCanvas *c)
{
    if (!c)
        return;
    if (c->sprite) {
        if (g_sym.sprite_dtor)
            g_sym.sprite_dtor(c->sprite);
        free(c->sprite);
    }
    if (c->image_data && g_sym.image_dtor)
        g_sym.image_dtor(c->image_data);
    free(c->image_data);
    free(c->pixels);
    free(c);
}

__attribute__((visibility("default")))
void pilot_canvas_begin_batch_v1(PilotCanvas *c)
{
    if (!c || c->backend != PILOT_CANVAS_BACKEND_RECT)
        return;
    c->rect_batch = 1;
    c->rect_count = 0;
}

__attribute__((visibility("default")))
void pilot_canvas_end_batch_v1(PilotCanvas *c)
{
    if (!c || c->backend != PILOT_CANVAS_BACKEND_RECT)
        return;
    c->rect_batch = 0;
    if (g_sym.sprite_set_count && c->sprite)
        g_sym.sprite_set_count(c->sprite, c->rect_count);
}

__attribute__((visibility("default")))
void pilot_canvas_clear_v1(PilotCanvas *c, u32 rgba)
{
    if (!c)
        return;
    if (c->backend == PILOT_CANVAS_BACKEND_RECT) {
        c->rect_count = 0;
        if (!c->rect_batch && g_sym.sprite_set_count && c->sprite)
            g_sym.sprite_set_count(c->sprite, 0);
        return;
    }
    if (!c->pixels)
        return;
    for (size_t i = 0; i < c->pixel_bytes / 4u; ++i)
        c->pixels[i] = rgba;
    mark_dirty(c, 0, 0, c->w, c->h);
}

__attribute__((visibility("default")))
void pilot_canvas_fill_rect_v1(PilotCanvas *c, int x, int y, int w, int h,
                               u32 rgba, PilotCanvasBlend mode)
{
    if (!c || w <= 0 || h <= 0)
        return;

    if (c->backend == PILOT_CANVAS_BACKEND_RECT) {
        if (!c->sprite || c->rect_count >= c->rect_capacity)
            return;
        float vec[2] = { (float)x, (float)y };
        u8 color[4] = {
            (u8)((rgba >> 16) & 0xFFu), (u8)((rgba >> 8) & 0xFFu),
            (u8)(rgba & 0xFFu), (u8)((rgba >> 24) & 0xFFu)
        };
        u16 idx = c->rect_count++;
        g_sym.sprite_rect_pos(c->sprite, idx, vec);
        g_sym.sprite_rect_size(c->sprite, idx, (float)w, (float)h);
        g_sym.sprite_rect_color(c->sprite, idx, color);
        if (!c->rect_batch)
            g_sym.sprite_set_count(c->sprite, c->rect_count);
        return;
    }

    if (!c->pixels)
        return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > (int)c->w ? (int)c->w : x + w;
    int y1 = y + h > (int)c->h ? (int)c->h : y + h;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            c->pixels[(size_t)yy * c->w + (size_t)xx] =
                blend_pixel(c->pixels[(size_t)yy * c->w + (size_t)xx], rgba, mode);
    mark_dirty(c, x0, y0, x1 - x0, y1 - y0);
}

__attribute__((visibility("default")))
void pilot_canvas_line_v1(PilotCanvas *c, int x0, int y0, int x1, int y1,
                          u32 rgba, int thickness, PilotCanvasBlend mode)
{
    if (!c)
        return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int t = thickness < 1 ? 1 : thickness;
    for (;;) {
        pilot_canvas_fill_rect_v1(c, x0 - t / 2, y0 - t / 2, t, t, rgba, mode);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

__attribute__((visibility("default")))
void pilot_canvas_blit_v1(PilotCanvas *c, int x, int y,
                          const void *src, u16 sw, u16 sh, size_t stride,
                          PilotCanvasBlend mode)
{
    if (!c || !c->pixels || !src)
        return;
    const u8 *s = (const u8 *)src;
    for (int yy = 0; yy < (int)sh; ++yy) {
        int py = y + yy;
        if (py < 0 || py >= (int)c->h)
            continue;
        const u32 *row = (const u32 *)(s + (size_t)yy * stride);
        for (int xx = 0; xx < (int)sw; ++xx) {
            int px = x + xx;
            if (px < 0 || px >= (int)c->w)
                continue;
            size_t idx = (size_t)py * c->w + (size_t)px;
            c->pixels[idx] = blend_pixel(c->pixels[idx], row[xx], mode);
        }
    }
    mark_dirty(c, x, y, sw, sh);
}

__attribute__((visibility("default")))
PilotStatus pilot_canvas_commit_v1(PilotCanvas *c)
{
    if (!c)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (c->backend == PILOT_CANVAS_BACKEND_RECT)
        return c->rect_count ? PILOT_STATUS_OK : PILOT_STATUS_DISABLED;
    if (!c->pixels || !c->raw || !g_sym.gfx_copy)
        return PILOT_STATUS_DISABLED;
    if (!c->has_dirty)
        return PILOT_STATUS_OK;
    int w = c->dw < 0 ? 0 : c->dw, h = c->dh < 0 ? 0 : c->dh;
    if (w == 0 || h == 0)
        return PILOT_STATUS_OK;
    (void)g_sym.gfx_copy(c->raw, (int16_t)c->dx, (int16_t)c->dy,
                         (int16_t)w, (int16_t)h,
                         c->pixels, (int16_t)c->dx, (int16_t)c->dy,
                         (int16_t)w, (int16_t)h, 0u, 0u, 0u);
    c->has_dirty = 0;
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default")))
PilotStatus pilot_canvas_bind_widget_v1(PilotCanvas *c, void *widget)
{
    if (!c || !widget)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (c->backend != PILOT_CANVAS_BACKEND_RECT || !c->sprite)
        return PILOT_STATUS_UNSUPPORTED;
    if (!g_sym.widget_init_sprite)
        return PILOT_STATUS_NOT_FOUND;
    g_sym.widget_init_sprite(widget, c->sprite, c->rect_capacity);
    return PILOT_STATUS_OK;
}

/* ---- reusable drawing primitives (grid, 5x7 OSD text, line chart) ------- */
__attribute__((visibility("default")))
void pilot_canvas_draw_grid_v1(PilotCanvas *c, int pad, int cols, int rows,
                               u32 rgba)
{
    if (!c || !rgba)
        return;
    int uw = (int)c->w - 2 * pad;
    int uh = (int)c->h - 2 * pad;
    if (uw <= 0 || uh <= 0)
        return;
    for (int i = 0; i < cols; ++i) {
        int x = pad + i * uw / (cols > 1 ? (cols - 1) : 1);
        pilot_canvas_fill_rect_v1(c, x, pad, 1, uh, rgba, PILOT_CANVAS_BLEND_NONE);
    }
    for (int i = 0; i < rows; ++i) {
        int y = pad + i * uh / (rows > 1 ? (rows - 1) : 1);
        pilot_canvas_fill_rect_v1(c, pad, y, uw, 1, rgba, PILOT_CANVAS_BLEND_NONE);
    }
}

static void osd_glyph_5x7(char ch, u8 rows[7])
{
    static const u8 dig[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
        {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* 2 */
        {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
        {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    };
    memset(rows, 0, 7);
    if (ch >= '0' && ch <= '9') {
        memcpy(rows, dig[ch - '0'], 7);
        return;
    }
    switch (ch) {
    case 'A': { static const u8 g[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; memcpy(rows,g,7); break; }
    case 'B': { static const u8 g[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; memcpy(rows,g,7); break; }
    case 'C': { static const u8 g[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; memcpy(rows,g,7); break; }
    case 'D': { static const u8 g[7]={0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}; memcpy(rows,g,7); break; }
    case 'E': { static const u8 g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; memcpy(rows,g,7); break; }
    case 'F': { static const u8 g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; memcpy(rows,g,7); break; }
    case 'G': { static const u8 g[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}; memcpy(rows,g,7); break; }
    case 'H': { static const u8 g[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; memcpy(rows,g,7); break; }
    case 'I': { static const u8 g[7]={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; memcpy(rows,g,7); break; }
    case 'L': { static const u8 g[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; memcpy(rows,g,7); break; }
    case 'M': { static const u8 g[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; memcpy(rows,g,7); break; }
    case 'N': { static const u8 g[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11}; memcpy(rows,g,7); break; }
    case 'O': { static const u8 g[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; memcpy(rows,g,7); break; }
    case 'P': { static const u8 g[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; memcpy(rows,g,7); break; }
    case 'R': { static const u8 g[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; memcpy(rows,g,7); break; }
    case 'S': { static const u8 g[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; memcpy(rows,g,7); break; }
    case 'T': { static const u8 g[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; memcpy(rows,g,7); break; }
    case 'U': { static const u8 g[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; memcpy(rows,g,7); break; }
    case 'V': { static const u8 g[7]={0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; memcpy(rows,g,7); break; }
    case 'W': { static const u8 g[7]={0x11,0x11,0x11,0x15,0x15,0x15,0x0A}; memcpy(rows,g,7); break; }
    case 'X': { static const u8 g[7]={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; memcpy(rows,g,7); break; }
    case 'Y': { static const u8 g[7]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; memcpy(rows,g,7); break; }
    case 'm': { static const u8 g[7]={0x00,0x00,0x1A,0x15,0x15,0x15,0x15}; memcpy(rows,g,7); break; }
    case 'x': { static const u8 g[7]={0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}; memcpy(rows,g,7); break; }
    case '.': { static const u8 g[7]={0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}; memcpy(rows,g,7); break; }
    case ':': { static const u8 g[7]={0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}; memcpy(rows,g,7); break; }
    case '-': { static const u8 g[7]={0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; memcpy(rows,g,7); break; }
    case '%': { static const u8 g[7]={0x18,0x19,0x02,0x04,0x08,0x13,0x03}; memcpy(rows,g,7); break; }
    default: break;
    }
}

__attribute__((visibility("default")))
void pilot_canvas_draw_osd_text_v1(PilotCanvas *c, int x, int y, int scale,
                                   u32 fg_rgba, u32 bg_rgba, const char *txt)
{
    if (!c || !txt || !txt[0])
        return;
    int s = scale < 1 ? 1 : scale;
    int len = (int)strlen(txt);
    if (len > 32)
        len = 32;
    int adv = 6 * s;
    int pad_x = 4, pad_y = 3;
    if (bg_rgba) {
        int bw = len * adv - s + 2 * pad_x;
        int bh = 7 * s + 2 * pad_y;
        pilot_canvas_fill_rect_v1(c, x, y, bw, bh, bg_rgba, PILOT_CANVAS_BLEND_NONE);
    }

    int cx = x + (bg_rgba ? pad_x : 0);
    int cy = y + (bg_rgba ? pad_y : 0);
    for (int i = 0; i < len; ++i) {
        u8 rows[7];
        osd_glyph_5x7(txt[i], rows);
        for (int r = 0; r < 7; ++r) {
            u8 bits = rows[r];
            int col = 0;
            while (col < 5) {
                if (!(bits & (1u << (4 - col)))) {
                    ++col;
                    continue;
                }
                int run = 1;
                while (col + run < 5 && (bits & (1u << (4 - (col + run)))))
                    ++run;
                pilot_canvas_fill_rect_v1(c, cx + col * s, cy + r * s,
                                          run * s, s, fg_rgba,
                                          PILOT_CANVAS_BLEND_NONE);
                col += run;
            }
        }
        cx += adv;
    }
}

#define CHART_PAD 8

static int chart_map_x(const PilotChartDesc *d, const PilotCanvas *c,
                       int16_t xv, uint16_t i)
{
    int usable = (int)c->w - 2 * CHART_PAD;
    if (usable <= 0)
        return 0;
    if (!d->xs) {
        if (d->count <= 1)
            return CHART_PAD + usable / 2;
        return CHART_PAD + (int)i * usable / (int)(d->count - 1);
    }
    int lo = d->x_min, hi = d->x_max;
    if (hi <= lo)
        return CHART_PAD + usable / 2;
    int v = ((int)xv - lo) * usable / (hi - lo);
    return CHART_PAD + (v < 0 ? 0 : (v > usable ? usable : v));
}

static int chart_map_y(const PilotChartDesc *d, const PilotCanvas *c, int16_t yv)
{
    int usable = (int)c->h - 2 * CHART_PAD;
    if (usable <= 0)
        return 0;
    int lo = d->y_min, hi = d->y_max;
    if (hi <= lo)
        return CHART_PAD + usable / 2;
    int v = ((int)yv - lo) * usable / (hi - lo);
    if (v < 0) v = 0;
    if (v > usable) v = usable;
    return CHART_PAD + usable - v;
}

__attribute__((visibility("default")))
PilotStatus pilot_chart_draw_v1(PilotCanvas *c, const PilotChartDesc *d)
{
    if (!c || !d || !d->ys || d->count < 2 || d->count > 2048)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (c->backend != PILOT_CANVAS_BACKEND_RECT)
        return PILOT_STATUS_UNSUPPORTED;

    if (d->grid_color)
        pilot_canvas_draw_grid_v1(c, CHART_PAD, d->grid_x_lines,
                                  d->grid_y_lines, d->grid_color);

    int t = d->line_thickness < 1 ? 1 : d->line_thickness;
    int px = chart_map_x(d, c, d->xs ? d->xs[0] : 0, 0);
    int py = chart_map_y(d, c, d->ys[0]);
    for (uint16_t i = 1; i < d->count; ++i) {
        int nx = chart_map_x(d, c, d->xs ? d->xs[i] : 0, i);
        int ny = chart_map_y(d, c, d->ys[i]);
        pilot_canvas_line_v1(c, px, py, nx, ny, d->line_color, t,
                             PILOT_CANVAS_BLEND_NONE);
        px = nx;
        py = ny;
    }
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default")))
PilotStatus pilot_canvas_probe_v1(void)
{
    if (!g_sym_ready && !canvas_resolve_symbols())
        return PILOT_STATUS_NOT_FOUND;
    mlogf("CANVAS_PROBE_MGR sys=%p ux=%p",
          g_sym.resmgr_instance(), g_sym.ux_resmgr());
    return PILOT_STATUS_OK;
}
