#ifndef PILOT_CANVAS_H
#define PILOT_CANVAS_H

#include "../../patch/pilot.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pilot Canvas: a generic drawing component for AP Live View.
 *
 * Two backends share one API:
 *   PILOT_CANVAS_BACKEND_RECT  - native ux::gfxeng::Sprite rects; drawn by GPE.
 *                                IMPLEMENTED; needs a host widget (stage 1).
 *   PILOT_CANVAS_BACKEND_PIXEL - module-owned RGBA buffer uploaded through
 *                                libra::ImageData + GraphicsImage::copy.
 *                                Compiled, but pixel input channel not closed;
 *                                native drawing uses GRM fillrect, not a raw
 *                                CPU framebuffer. Prefer RECT.
 *
 * The provider is pilot_canvas.so. Symbols are resolved at call time, so a
 * caller has no link-time dependency on the provider. Add pilot_canvas.so to
 * the modules array of /pilot/modules.txt and load it before callers.
 *
 * Text remains native: keep using pilot_ui_show_text(). The canvas draws
 * graphics only.
 */

typedef enum {
    PILOT_CANVAS_BACKEND_RECT = 0,
    PILOT_CANVAS_BACKEND_PIXEL = 1
} PilotCanvasBackend;

typedef enum {
    PILOT_CANVAS_BLEND_NONE = 0,
    PILOT_CANVAS_BLEND_SRC_OVER = 1,
    PILOT_CANVAS_BLEND_ADD = 2
} PilotCanvasBlend;

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t rgba; /* 0xAARRGGBB */
} PilotCanvasPoint;

typedef struct PilotCanvas PilotCanvas;

/* Declarative line chart drawn on a RECT-backend canvas.
 * Data domain maps linearly into the canvas rectangle with a small margin.
 * xs == NULL means even spacing from 0 to count-1. */
typedef struct {
    const int16_t *xs;        /* optional x values, NULL -> index */
    const int16_t *ys;        /* y values (required) */
    uint16_t count;
    int16_t x_min, x_max;     /* data domain; ignored when xs == NULL */
    int16_t y_min, y_max;
    uint32_t line_color;      /* 0xAARRGGBB */
    uint32_t grid_color;      /* 0 = no grid */
    int line_thickness;       /* >= 1 */
    uint16_t grid_x_lines;    /* vertical grid line count */
    uint16_t grid_y_lines;    /* horizontal grid line count */
} PilotChartDesc;

#define PILOT_CANVAS_PROBE_SYMBOL "pilot_canvas_probe_v1"
#define PILOT_CANVAS_CREATE_SYMBOL "pilot_canvas_create_v1"
#define PILOT_CANVAS_DESTROY_SYMBOL "pilot_canvas_destroy_v1"
#define PILOT_CANVAS_CLEAR_SYMBOL "pilot_canvas_clear_v1"
#define PILOT_CANVAS_FILL_RECT_SYMBOL "pilot_canvas_fill_rect_v1"
#define PILOT_CANVAS_LINE_SYMBOL "pilot_canvas_line_v1"
#define PILOT_CANVAS_BLIT_SYMBOL "pilot_canvas_blit_v1"
#define PILOT_CANVAS_COMMIT_SYMBOL "pilot_canvas_commit_v1"
#define PILOT_CANVAS_BIND_WIDGET_SYMBOL "pilot_canvas_bind_widget_v1"
#define PILOT_CANVAS_CHART_DRAW_SYMBOL "pilot_chart_draw_v1"
#define PILOT_CANVAS_TEXT_SET_SYMBOL "pilot_canvas_text_set_v1"
#define PILOT_CANVAS_TEXT_CLEAR_SYMBOL "pilot_canvas_text_clear_v1"
#define PILOT_CANVAS_TEXT_CLEAR_ALL_SYMBOL "pilot_canvas_text_clear_all_v1"
#define PILOT_CANVAS_IS_ACTIVE_SYMBOL "pilot_canvas_is_active_v1"
#define PILOT_CANVAS_ATTACH_V2_SYMBOL "pilot_canvas_attach_v2"
#define PILOT_CANVAS_GET_GEOMETRY_SYMBOL "pilot_canvas_get_geometry_v1"
#define PILOT_CANVAS_BEGIN_BATCH_SYMBOL "pilot_canvas_begin_batch_v1"
#define PILOT_CANVAS_END_BATCH_SYMBOL "pilot_canvas_end_batch_v1"
#define PILOT_CANVAS_DRAW_GRID_SYMBOL "pilot_canvas_draw_grid_v1"
#define PILOT_CANVAS_DRAW_OSD_TEXT_SYMBOL "pilot_canvas_draw_osd_text_v1"

typedef PilotStatus (*PilotCanvasProbeFn)(void);
typedef PilotCanvas *(*PilotCanvasCreateFn)(uint16_t, uint16_t,
                                            PilotCanvasBackend);
typedef void (*PilotCanvasDestroyFn)(PilotCanvas *);
typedef void (*PilotCanvasClearFn)(PilotCanvas *, uint32_t);
typedef void (*PilotCanvasFillRectFn)(PilotCanvas *, int, int, int, int,
                                      uint32_t, PilotCanvasBlend);
typedef void (*PilotCanvasLineFn)(PilotCanvas *, int, int, int, int,
                                  uint32_t, int, PilotCanvasBlend);
typedef void (*PilotCanvasBlitFn)(PilotCanvas *, int, int, const void *,
                                  uint16_t, uint16_t, size_t, PilotCanvasBlend);
typedef PilotStatus (*PilotCanvasCommitFn)(PilotCanvas *);
typedef PilotStatus (*PilotCanvasBindWidgetFn)(PilotCanvas *, void *);
typedef PilotStatus (*PilotChartDrawFn)(PilotCanvas *, const PilotChartDesc *);
typedef PilotStatus (*PilotCanvasTextSetFn)(const char *, int, int, int, int,
                                            const char *, uint32_t);
typedef PilotStatus (*PilotCanvasTextClearFn)(const char *);
typedef PilotStatus (*PilotCanvasTextClearAllFn)(void);
typedef void (*PilotCanvasBatchFn)(PilotCanvas *);
typedef void (*PilotCanvasDrawGridFn)(PilotCanvas *, int, int, int, uint32_t);
typedef void (*PilotCanvasDrawOsdTextFn)(PilotCanvas *, int, int, int,
                                         uint32_t, uint32_t, const char *);

#define PILOT_CANVAS_RESOLVE(sym, type, var)                       \
    union { void *object; type function; } var;                    \
    var.object = dlsym(RTLD_DEFAULT, (sym));                       \
    if (!var.object)                                              \
        return (PILOT_STATUS_NOT_FOUND)

static inline PilotStatus pilot_canvas_probe(void)
{
    PILOT_CANVAS_RESOLVE(PILOT_CANVAS_PROBE_SYMBOL, PilotCanvasProbeFn, fn);
    return fn.function();
}

static inline PilotCanvas *pilot_canvas_create(uint16_t w, uint16_t h,
                                               PilotCanvasBackend backend)
{
    union { void *object; PilotCanvasCreateFn function; } fn;
    fn.object = dlsym(RTLD_DEFAULT, PILOT_CANVAS_CREATE_SYMBOL);
    if (!fn.object)
        return NULL;
    return fn.function(w, h, backend);
}

static inline void pilot_canvas_destroy(PilotCanvas *c)
{
    union { void *object; PilotCanvasDestroyFn function; } fn;
    fn.object = dlsym(RTLD_DEFAULT, PILOT_CANVAS_DESTROY_SYMBOL);
    if (fn.object)
        fn.function(c);
}

static inline void pilot_canvas_clear(PilotCanvas *c, uint32_t rgba)
{
    union { void *object; PilotCanvasClearFn function; } fn;
    fn.object = dlsym(RTLD_DEFAULT, PILOT_CANVAS_CLEAR_SYMBOL);
    if (fn.object)
        fn.function(c, rgba);
}

static inline void pilot_canvas_fill_rect(PilotCanvas *c, int x, int y,
                                          int w, int h, uint32_t rgba,
                                          PilotCanvasBlend mode)
{
    union { void *object; PilotCanvasFillRectFn function; } fn;
    fn.object = dlsym(RTLD_DEFAULT, PILOT_CANVAS_FILL_RECT_SYMBOL);
    if (fn.object)
        fn.function(c, x, y, w, h, rgba, mode);
}

static inline void pilot_canvas_line(PilotCanvas *c, int x0, int y0,
                                     int x1, int y1, uint32_t rgba,
                                     int thickness, PilotCanvasBlend mode)
{
    union { void *object; PilotCanvasLineFn function; } fn;
    fn.object = dlsym(RTLD_DEFAULT, PILOT_CANVAS_LINE_SYMBOL);
    if (fn.object)
        fn.function(c, x0, y0, x1, y1, rgba, thickness, mode);
}

static inline void pilot_canvas_blit(PilotCanvas *c, int x, int y,
                                     const void *src, uint16_t sw, uint16_t sh,
                                     size_t stride, PilotCanvasBlend mode)
{
    union { void *object; PilotCanvasBlitFn function; } fn;
    fn.object = dlsym(RTLD_DEFAULT, PILOT_CANVAS_BLIT_SYMBOL);
    if (fn.object)
        fn.function(c, x, y, src, sw, sh, stride, mode);
}

static inline PilotStatus pilot_canvas_commit(PilotCanvas *c)
{
    PILOT_CANVAS_RESOLVE(PILOT_CANVAS_COMMIT_SYMBOL, PilotCanvasCommitFn, fn);
    return fn.function(c);
}

/* Attach a RECT-backend canvas to a live native widget (e.g. the LV
 * view_string widget captured by menu_ui). The native widget then composites
 * our Sprite in its normal draw pass; the caller still triggers the renderer
 * commit (renderer->vtable[+0x20]) after drawing. */
static inline PilotStatus pilot_canvas_bind_widget(PilotCanvas *c, void *widget)
{
    PILOT_CANVAS_RESOLVE(PILOT_CANVAS_BIND_WIDGET_SYMBOL,
                        PilotCanvasBindWidgetFn, fn);
    return fn.function(c, widget);
}

static inline PilotStatus pilot_chart_draw(PilotCanvas *c,
                                            const PilotChartDesc *desc)
{
    PILOT_CANVAS_RESOLVE(PILOT_CANVAS_CHART_DRAW_SYMBOL, PilotChartDrawFn, fn);
    return fn.function(c, desc);
}

/* Independent native MultiLineTextSprite slots attached to the Canvas host
 * widget via Widget::initNode. Multiple callers (e.g. 3K OG, CP diag, HUD)
 * each own their own tagged slot with independent (x, y, w, h) and expiry,
 * without overwriting each other or menu_ui's shared banner slot.
 * duration_ms == 0 keeps the text visible until cleared or overwritten. */
static inline PilotStatus pilot_canvas_text_set(const char *tag,
                                                int x, int y, int w, int h,
                                                const char *text,
                                                uint32_t duration_ms)
{
    PILOT_CANVAS_RESOLVE(PILOT_CANVAS_TEXT_SET_SYMBOL,
                         PilotCanvasTextSetFn, fn);
    return fn.function(tag, x, y, w, h, text, duration_ms);
}

static inline PilotStatus pilot_canvas_text_clear(const char *tag)
{
    PILOT_CANVAS_RESOLVE(PILOT_CANVAS_TEXT_CLEAR_SYMBOL,
                         PilotCanvasTextClearFn, fn);
    return fn.function(tag);
}

static inline PilotStatus pilot_canvas_text_clear_all(void)
{
    PILOT_CANVAS_RESOLVE(PILOT_CANVAS_TEXT_CLEAR_ALL_SYMBOL,
                         PilotCanvasTextClearAllFn, fn);
    return fn.function();
}

#undef PILOT_CANVAS_RESOLVE

#ifdef __cplusplus
}
#endif

#endif
