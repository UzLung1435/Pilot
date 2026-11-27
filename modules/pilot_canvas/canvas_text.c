/* Independent native MultiLineTextSprite slots attached to the Canvas host
 * widget via Widget::initNode, plus UI-thread frame flush hook. */

#include "pilot_canvas_internal.h"

#define PILOT_CANVAS_MAX_TEXTS      8u
#define PILOT_TEXT_FONT_RID         0x0207A018u
#define PILOT_TEXT_DEFAULT_FONT_RID 0x0207A000u

#define APPFW_GOT_UPDATE_FRAME      0x4F20060ull /* WidgetSystem::updateFrame() */
#define APPFW_GOT_DRAW_FB           0x4F20228ull /* GraphicsEngine::drawFramebuffer() */

struct canvas_text_slot {
    int used;
    int active;
    int dirty[PILOT_DISPLAY_COUNT];
    char tag[32];
    char text[160];
    int x, y, w, h;
    u64 deadline_ms;
    void *mls[PILOT_DISPLAY_COUNT];
    void *bound_widget[PILOT_DISPLAY_COUNT];
};

static struct canvas_text_slot g_texts[PILOT_CANVAS_MAX_TEXTS];
static pthread_mutex_t g_text_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_text_any_dirty;

typedef void (*fn_void_self)(void *self);
static fn_void_self g_orig_update_frame;
static fn_void_self g_orig_draw_fb;
static int g_frame_hooked;

static void canvas_flush_dirty_texts_ui(void)
{
    static __thread int s_in_flush;
    if (!g_text_any_dirty || s_in_flush)
        return;
    s_in_flush = 1;
    g_text_any_dirty = 0;

    void *widgets[PILOT_DISPLAY_COUNT] = { NULL, NULL };
    if (canvas_panel_get_attached_widgets(widgets) > 0) {
        for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d) {
            if (widgets[d])
                canvas_sync_texts(widgets[d], d);
        }
    }
    s_in_flush = 0;
}

static void pilot_update_frame_hook(void *ws)
{
    if (g_orig_update_frame)
        g_orig_update_frame(ws);
    canvas_flush_dirty_texts_ui();
}

static void pilot_draw_fb_hook(void *ge)
{
    canvas_flush_dirty_texts_ui();
    if (g_orig_draw_fb)
        g_orig_draw_fb(ge);
}

int canvas_install_frame_hook(void)
{
    if (g_frame_hooked)
        return 1;
    if (!canvas_appfw_base())
        return 0;

    union { void *p; fn_void_self f; } u_upd, u_draw;
    u_upd.p  = dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem11updateFrameEv");
    u_draw.p = dlsym(RTLD_DEFAULT, "_ZN2ux6gfxeng14GraphicsEngine15drawFramebufferEv");
    g_orig_update_frame = u_upd.f;
    g_orig_draw_fb      = u_draw.f;

    int ok_upd  = u_upd.p  ? canvas_patch_appfw_got(APPFW_GOT_UPDATE_FRAME, (void *)pilot_update_frame_hook) : 0;
    int ok_draw = u_draw.p ? canvas_patch_appfw_got(APPFW_GOT_DRAW_FB, (void *)pilot_draw_fb_hook) : 0;
    mlogf("CANVAS_FRAME_GOT upd=%d draw=%d", ok_upd, ok_draw);
    if (ok_upd || ok_draw) {
        g_frame_hooked = 1;
        return 1;
    }
    return 0;
}

void canvas_mark_all_texts_dirty(void)
{
    pthread_mutex_lock(&g_text_lock);
    for (u32 i = 0; i < PILOT_CANVAS_MAX_TEXTS; ++i) {
        if (!g_texts[i].used)
            continue;
        for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d)
            g_texts[i].dirty[d] = 1;
    }
    g_text_any_dirty = 1;
    pthread_mutex_unlock(&g_text_lock);
}

int canvas_has_active_external_texts(void)
{
    int active = 0;
    pthread_mutex_lock(&g_text_lock);
    for (u32 i = 0; i < PILOT_CANVAS_MAX_TEXTS; ++i) {
        if (g_texts[i].used && g_texts[i].active) {
            active = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_text_lock);
    return active;
}

__attribute__((visibility("default")))
PilotStatus pilot_canvas_text_set_v1(const char *tag,
                                     int x, int y, int w, int h,
                                     const char *text, u32 duration_ms)
{
    if (!tag || !tag[0] || !text || !text[0] || w <= 0 || h <= 0)
        return PILOT_STATUS_INVALID_ARGUMENT;

    u64 now = canvas_now_ms();
    pthread_mutex_lock(&g_text_lock);

    struct canvas_text_slot *slot = NULL;
    struct canvas_text_slot *free_slot = NULL;
    for (u32 i = 0; i < PILOT_CANVAS_MAX_TEXTS; ++i) {
        if (g_texts[i].used && strcmp(g_texts[i].tag, tag) == 0) {
            slot = &g_texts[i];
            break;
        }
        if (!g_texts[i].used && !free_slot)
            free_slot = &g_texts[i];
    }
    if (!slot)
        slot = free_slot;
    if (!slot) {
        pthread_mutex_unlock(&g_text_lock);
        return PILOT_STATUS_RANGE;
    }

    slot->used = 1;
    snprintf(slot->tag, sizeof(slot->tag), "%s", tag);
    if (!slot->active || slot->x != x || slot->y != y ||
        slot->w != w || slot->h != h || strcmp(slot->text, text) != 0) {
        slot->x = x;
        slot->y = y;
        slot->w = w;
        slot->h = h;
        snprintf(slot->text, sizeof(slot->text), "%s", text);
        for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d)
            slot->dirty[d] = 1;
        g_text_any_dirty = 1;
    }
    slot->active = 1;
    slot->deadline_ms = (duration_ms > 0u && now > 0u) ? (now + duration_ms) : 0u;
    pthread_mutex_unlock(&g_text_lock);
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default")))
PilotStatus pilot_canvas_text_clear_v1(const char *tag)
{
    if (!tag || !tag[0])
        return PILOT_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_text_lock);
    for (u32 i = 0; i < PILOT_CANVAS_MAX_TEXTS; ++i) {
        if (g_texts[i].used && strcmp(g_texts[i].tag, tag) == 0) {
            if (g_texts[i].active) {
                g_texts[i].active = 0;
                g_texts[i].deadline_ms = 0u;
                for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d)
                    g_texts[i].dirty[d] = 1;
                g_text_any_dirty = 1;
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_text_lock);
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default")))
PilotStatus pilot_canvas_text_clear_all_v1(void)
{
    pthread_mutex_lock(&g_text_lock);
    for (u32 i = 0; i < PILOT_CANVAS_MAX_TEXTS; ++i) {
        if (g_texts[i].used && g_texts[i].active) {
            g_texts[i].active = 0;
            g_texts[i].deadline_ms = 0u;
            for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d)
                g_texts[i].dirty[d] = 1;
            g_text_any_dirty = 1;
        }
    }
    pthread_mutex_unlock(&g_text_lock);
    return PILOT_STATUS_OK;
}

void canvas_sync_texts(void *widget, u32 disp)
{
    if (!widget || disp >= PILOT_DISPLAY_COUNT || !g_sym.text_ctor ||
        !g_sym.widget_init_node || !g_sym.text_set_font ||
        !g_sym.text_set_string || !g_sym.text_draw ||
        !g_sym.elem_set_local_pos || !g_sym.elem_set_visible)
        return;

    u64 now = canvas_now_ms();
    static const u32 fonts[2] = {
        PILOT_TEXT_FONT_RID, PILOT_TEXT_DEFAULT_FONT_RID
    };

    pthread_mutex_lock(&g_text_lock);
    for (u32 i = 0; i < PILOT_CANVAS_MAX_TEXTS; ++i) {
        struct canvas_text_slot *s = &g_texts[i];
        if (!s->used)
            continue;

        if (s->active && s->deadline_ms > 0u && now >= s->deadline_ms) {
            s->active = 0;
            s->deadline_ms = 0u;
            for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d)
                s->dirty[d] = 1;
        }

        if (!s->mls[disp]) {
            if (!s->active)
                continue;
            s->mls[disp] = calloc(1, 0x400);
            if (!s->mls[disp])
                continue;
            g_sym.text_ctor(s->mls[disp]);
            g_sym.widget_init_node(widget, s->mls[disp]);
            s->bound_widget[disp] = widget;
            s->dirty[disp] = 1;
            mlogf("CANVAS_TEXT_INIT disp=%u slot=%u tag=%s mls=%p widget=%p",
                  disp, i, s->tag, s->mls[disp], widget);
        } else if (s->bound_widget[disp] != widget ||
                   !node_attached_to_widget(s->mls[disp], widget)) {
            g_sym.widget_init_node(widget, s->mls[disp]);
            s->bound_widget[disp] = widget;
            s->dirty[disp] = 1;
        }

        if (!s->dirty[disp])
            continue;

        void *mls = s->mls[disp];
        if (!s->active) {
            g_sym.elem_set_visible(mls, false);
            mlogf("CANVAS_TEXT_HIDE disp=%u slot=%u tag=%s", disp, i, s->tag);
        } else {
            float pos[2] = { (float)s->x, (float)s->y };
            g_sym.elem_set_local_pos(mls, pos);
            if (g_sym.node_set_clip_pos)
                g_sym.node_set_clip_pos(mls, 0.0f, 0.0f);
            if (g_sym.text_set_clip_size)
                g_sym.text_set_clip_size(mls, (float)s->w, (float)s->h);
            g_sym.text_set_font(mls, fonts, 2u);
            g_sym.text_set_string(mls, s->text);
            g_sym.text_draw(mls);
            if (g_sym.text_freeze)
                g_sym.text_freeze(mls);
            g_sym.elem_set_visible(mls, true);
            mlogf("CANVAS_TEXT_DRAW disp=%u slot=%u tag=%s xy=%d,%d wh=%d,%d",
                  disp, i, s->tag, s->x, s->y, s->w, s->h);
        }
        s->dirty[disp] = 0;
    }
    pthread_mutex_unlock(&g_text_lock);
}
