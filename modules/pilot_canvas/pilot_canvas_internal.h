#ifndef PILOT_CANVAS_INTERNAL_H
#define PILOT_CANVAS_INTERNAL_H

#define _GNU_SOURCE
#include "pilot_canvas.h"

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define PILOT_DISPLAY_COUNT     2u    /* 0 = LCD, 1 = EVF */
#define PILOT_CANVAS_MAX_RECTS  2048u
#define PILOT_CANVAS_MAX_PANELS 8u
#define CANVAS_SCREEN_W         640
#define CANVAS_SCREEN_H         480

/* ---- native signatures (A6700 2.00 appFw.so / libObj.so dynsym) --------- */
typedef void *(*fn_resmgr_instance)(void);
typedef void *(*fn_param_ctor)(void *self, u16 w, u16 h, int format);
typedef void *(*fn_create_image)(const void *param, void *out_img);
typedef void *(*fn_image_raw)(void *img);
typedef u16   (*fn_image_dim)(const void *img);
typedef int   (*fn_image_format)(const void *img);
typedef void  (*fn_image_ctor)(void *img);
typedef void  (*fn_image_dtor)(void *img);
typedef void *(*fn_gfx_clear)(void *obj, const void *color);
typedef void *(*fn_gfx_copy)(void *dst, int16_t, int16_t, int16_t, int16_t,
                             void *src, int16_t, int16_t, int16_t, int16_t,
                             u8, u8, u8);
typedef void *(*fn_sprite_ctor)(void *self);
typedef void  (*fn_sprite_dtor)(void *self);
typedef void  (*fn_sprite_set_count)(void *self, u16 count);
typedef void  (*fn_sprite_rect_color)(void *self, u16 index, const void *color);
typedef void  (*fn_sprite_rect_size)(void *self, u16 index, float w, float h);
typedef void  (*fn_sprite_rect_pos)(void *self, u16 index, const void *vec2);
typedef void  (*fn_widget_init_sprite)(void *widget, void *sprite, u16 cap);
typedef void  (*fn_widget_init_node)(void *widget, void *node);
typedef void  (*fn_widget_ctor)(void *widget);
typedef void  (*fn_widget_set_size)(void *widget, float w, float h);
typedef void  (*fn_widget_set_pos)(void *widget, float x, float y);
typedef void  (*fn_widget_set_parent)(void *widget, void *parent);
typedef void  (*fn_text_ctor)(void *self);
typedef void  (*fn_text_dtor)(void *self);
typedef void  (*fn_text_set_string)(void *self, const char *s);
typedef void  (*fn_text_set_font)(void *self, const u32 *fonts, u8 count);
typedef void  (*fn_text_draw)(void *self);
typedef void  (*fn_text_set_f2)(void *self, float a, float b);
typedef void  (*fn_elem_set_pos)(void *self, const float *vec2);
typedef void  (*fn_elem_set_visible)(void *self, bool visible);
typedef void  (*fn_set_mouse_hook)(void *widget, void *hook);

struct gfx_symbols {
    fn_resmgr_instance    resmgr_instance;
    fn_resmgr_instance    ux_resmgr;
    fn_param_ctor         param_ctor;
    fn_create_image       create_image;
    fn_image_raw          image_raw;
    fn_image_dim          image_width;
    fn_image_dim          image_height;
    fn_image_format       image_format;
    fn_image_ctor         image_ctor;
    fn_image_dtor         image_dtor;
    fn_gfx_clear          gfx_clear;
    fn_gfx_copy           gfx_copy;
    fn_sprite_ctor        sprite_ctor;
    fn_sprite_dtor        sprite_dtor;
    fn_sprite_set_count   sprite_set_count;
    fn_sprite_rect_color  sprite_rect_color;
    fn_sprite_rect_size   sprite_rect_size;
    fn_sprite_rect_pos    sprite_rect_pos;
    fn_widget_init_sprite widget_init_sprite;
    fn_widget_init_node   widget_init_node;
    fn_widget_ctor        widget_ctor;
    fn_widget_set_size    widget_set_size;
    fn_widget_set_pos     widget_set_pos;
    fn_widget_set_parent  widget_set_parent;
    fn_text_ctor          text_ctor;
    fn_text_dtor          text_dtor;
    fn_text_set_string    text_set_string;
    fn_text_set_font      text_set_font;
    fn_text_draw          text_draw;
    fn_text_draw          text_freeze;
    fn_text_set_f2        text_set_clip_size;
    fn_text_set_f2        node_set_clip_pos;
    fn_elem_set_pos       elem_set_local_pos;
    fn_elem_set_visible   elem_set_visible;
    fn_set_mouse_hook     set_mouse_hook;
};

struct PilotCanvas {
    u16 w, h;
    PilotCanvasBackend backend;
    u32 *pixels;
    size_t pixel_bytes;
    void *image_data;
    void *raw;
    void *sprite;
    u16 rect_count;
    u16 rect_capacity;
    int rect_batch;
    int dx, dy, dw, dh;
    int has_dirty;
};

/* ---- shared core state & helpers (canvas_core.c) ------------------------ */
extern struct gfx_symbols g_sym;
extern int g_sym_ready;
extern const PilotApi *g_api;

PilotStatus  pilot_canvas_probe_v1(void);
PilotCanvas *pilot_canvas_create_v1(u16 w, u16 h, PilotCanvasBackend backend);

/* A7 C II: no Live View widget is handed in, so build our own layer and
 * widget.  Never returns; called from pilot_canvas.c's worker on A7C2. */
void         pilot_canvas_a7c2_host_run(void);
void         pilot_canvas_destroy_v1(PilotCanvas *c);
void         pilot_canvas_begin_batch_v1(PilotCanvas *c);
void         pilot_canvas_end_batch_v1(PilotCanvas *c);
void         pilot_canvas_clear_v1(PilotCanvas *c, u32 rgba);
void         pilot_canvas_fill_rect_v1(PilotCanvas *c, int x, int y, int w, int h,
                                       u32 rgba, PilotCanvasBlend mode);
void         pilot_canvas_line_v1(PilotCanvas *c, int x0, int y0, int x1, int y1,
                                  u32 rgba, int thickness, PilotCanvasBlend mode);
void         pilot_canvas_draw_grid_v1(PilotCanvas *c, int pad, int cols, int rows,
                                       u32 rgba);
void         pilot_canvas_draw_osd_text_v1(PilotCanvas *c, int x, int y, int scale,
                                           u32 fg_rgba, u32 bg_rgba, const char *txt);
PilotStatus  pilot_canvas_attach_v2(void *widget, int is_evf);

void mlog(const char *s);
void mlogf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  canvas_resolve_symbols(void);
u64  canvas_now_ms(void);

uintptr_t canvas_appfw_base(void);
int  canvas_patch_appfw_got(uintptr_t got_off, void *hook);
int  canvas_patch_exact_got(uintptr_t got_off, uintptr_t expected_fn_off, void *hook);

/* Verified from libObj.so 0x151250: Element/Node parent pointer at +0x18
 * (low 2 tag bits masked) equals (widget + 0x100) while attached. */
static inline int node_attached_to_widget(const void *node, const void *widget)
{
    if (!node || !widget)
        return 0;
    uintptr_t parent =
        (*(const uintptr_t *)((const u8 *)node + 0x18u)) & ~(uintptr_t)3u;
    return parent == (uintptr_t)((const u8 *)widget + 0x100u);
}

/* Thread-safe async invalidation (safe from background worker thread). */
void canvas_notify_draw_async(void);
/* Synchronous commit (MUST ONLY be called from Sony's UI thread). */
void canvas_notify_draw_sync(void);

/* ---- independent native MultiLineTextSprite slots (canvas_text.c) ------- */
void canvas_sync_texts(void *widget, u32 disp);
void canvas_mark_all_texts_dirty(void);
int  canvas_has_active_external_texts(void);
int  canvas_install_frame_hook(void);

/* ---- reusable panel container + touch + DISP + config (canvas_panel.c) -- */
typedef struct PilotPanel PilotPanel;

typedef struct {
    const char *id;            /* unique tag, e.g. "scope", "guides" */
    int default_x;
    int default_y;
    int w;
    int h;
    int draggable;             /* 1 = touch-drag enabled */
    void *user_data;
    void (*on_render)(PilotPanel *panel, PilotCanvas *c);
    void (*on_tap)(PilotPanel *panel);
    void (*on_cfg_load)(PilotPanel *panel, const char *key, const char *val);
    int  (*on_cfg_save)(PilotPanel *panel, char *buf, size_t cap);
} PilotPanelDesc;

PilotPanel *canvas_panel_register(const PilotPanelDesc *desc);
void        canvas_panel_invalidate(PilotPanel *panel, int sync_ui);
void        canvas_panel_get_pos(PilotPanel *panel, int *x, int *y);
void       *canvas_panel_user_data(PilotPanel *panel);
int         canvas_panel_any_attached(void);
int         canvas_panel_get_attached_widgets(void *out_widgets[PILOT_DISPLAY_COUNT]);
int         canvas_pilot_page_visible(void);
void        canvas_set_lock_pos(int locked);
void        canvas_set_always_on(int always_on);

void canvas_config_mark_dirty(void);
void canvas_config_load(void);
void canvas_config_flush_if_dirty(void);

int  canvas_install_touch_hook(void);
int  canvas_install_disp_hook(void);

/* ---- CP live sensors (canvas_cp_sensors.c) ------------------------------ */
/* Verified live on A6700 2.00:
 *   Photo (3:2) : 44 RGGB cols x 32 row-pairs, stride = 176 B / 352 B (11,264 B)
 *   Movie (16:9): 48 RGGB cols x 26 row-pairs, stride = 192 B / 384 B ( 9,984 B)
 * Auto-detected per frame via trailer signature + vertical row correlation. */
#define OPD_MAX_COLS    48u
#define OPD_MAX_ROWS    32u

enum {
    SCOPE_GAMMA_REC709 = 0,
    SCOPE_GAMMA_SLOG3  = 1,
    SCOPE_GAMMA_COUNT  = 2
};

typedef struct {
    int valid;
    u32 cols;
    u32 rows;
    u8 r[OPD_MAX_ROWS][OPD_MAX_COLS];
    u8 g[OPD_MAX_ROWS][OPD_MAX_COLS];
    u8 b[OPD_MAX_ROWS][OPD_MAX_COLS];
    u8 y[OPD_MAX_ROWS][OPD_MAX_COLS];
} PilotOpdFrame;

int canvas_cp_read_opd(PilotOpdFrame *out, int gamma_mode, int wb_enable);
int canvas_cp_read_focal_mm(char *out, size_t out_sz);
int canvas_cp_read_sync_geom(char *out, size_t out_sz);

/* ---- Live View Scope widget (canvas_scope_widget.c) --------------------- */
void canvas_scope_widget_init(void);
void canvas_scope_widget_tick_label(void);
void canvas_scope_widget_tick_opd(void);

#endif
