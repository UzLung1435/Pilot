/* Reusable Live View Panel Container:
 *   - Multi-panel registry (up to PILOT_CANVAS_MAX_PANELS independent overlays)
 *   - Per-display (LCD / EVF) Sprite lifecycle + destroy/recreate auto-reattach
 *   - Generic 3-GOT touch router (hit-test, touch-focus cancel, drag, tap)
 *   - DISP virtual page insertion (LCD widget-pointer + EVF 4-step counter)
 *   - Debounced persistent layout/state config (SD:/pilot/modules/pilot_canvas/layout.cfg) */

#include "pilot_canvas_internal.h"

#define APPFW_GOT_POST_MOUSE_MOVE    0x4F200A8ull
#define APPFW_GOT_POST_MOUSE_PRESS   0x4F208D0ull
#define APPFW_GOT_POST_MOUSE_RELEASE 0x4F1FF70ull

#define APPFW_GOT_LV_LCD_DISP_KEY    0x4E8C730ull
#define APPFW_FN_LV_LCD_DISP_KEY     0x2932B5Cull
#define APPFW_GOT_LV_EVF_DISP_KEY    0x4EE6348ull
#define APPFW_FN_LV_EVF_DISP_KEY     0x2942B5Cull

/* Exact live DISP mode .bss offsets proven via model::is_on_histogram_detection_mode_repeat
 * caller (0x3226068 / 0x3226074) in A6700 2.00 appFw.so:
 *   0x2F81B68 -> 0x8D3D968: PRM_disp_mode_shoot_monitor (0=ALL, 1=NO_INFO, 2=HIST, 3=LEVEL, 4=VF, 5=OFF)
 *   0x2F81B90 -> 0x8D3D97C: PRM_disp_mode_shoot_finder  (0=ALL, 1=NO_INFO, 2=HIST, 3=LEVEL) */
#define APPFW_BSS_DISP_MODE_MONITOR  0x8D3D968ull
#define APPFW_BSS_DISP_MODE_FINDER   0x8D3D97Cull
#define PILOT_DISP_NO_INFO           1u

#define CANVAS_MAX_FINGERS           16u
#define CANVAS_TAP_SLOP              12
#define CANVAS_CFG_FILENAME          "layout.cfg"
#define CANVAS_CFG_DEBOUNCE_MS       800u

struct PilotPanel {
    int used;
    PilotPanelDesc desc;
    int x, y;
    PilotCanvas *canvas[PILOT_DISPLAY_COUNT];
    void *widget[PILOT_DISPLAY_COUNT];
};

static struct PilotPanel g_panels[PILOT_CANVAS_MAX_PANELS];
static u32 g_panel_count;
static pthread_mutex_t g_panel_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile int g_pilot_page[PILOT_DISPLAY_COUNT] = { 0, 0 };
static volatile int g_lock_pos  = 0;
static volatile int g_always_on = 0;

static int canvas_read_disp_mode(int is_evf)
{
    uintptr_t base = canvas_appfw_base();
    if (!base || !g_api || !g_api->memory_read)
        return -1;
    uintptr_t off = is_evf ? APPFW_BSS_DISP_MODE_FINDER : APPFW_BSS_DISP_MODE_MONITOR;
    u32 val = 0xFFFFFFFFu;
    if (g_api->memory_read(base + off, &val, sizeof(val)) != PILOT_STATUS_OK)
        return -1;
    u32 maxv = is_evf ? 3u : 5u;
    return (val <= maxv) ? (int)val : -1;
}

/* ---- touch state -------------------------------------------------------- */
typedef void (*fn_post_mouse_move)(void *self, u8 finger, int16_t x, int16_t y,
                                   const void *msec);
typedef void (*fn_post_mouse_press)(void *self, u8 finger, u8 btn,
                                    const void *msec);
typedef void (*fn_post_mouse_release)(void *self, u8 finger, u8 btn,
                                      const void *msec);
typedef u32  (*fn_disp_key_handler)(void *ctrl, const void *ev);

static fn_post_mouse_move    g_orig_post_mouse_move;
static fn_post_mouse_press   g_orig_post_mouse_press;
static fn_post_mouse_release g_orig_post_mouse_release;
static fn_disp_key_handler   g_orig_lcd_disp_key;
static fn_disp_key_handler   g_orig_evf_disp_key;

static int g_touch_hooked;
static int g_disp_hooked;
static int g_click_count;

struct canvas_finger_state {
    int16_t last_x, last_y;
    int16_t start_x, start_y;
    int drag_off_x, drag_off_y;
    int has_pos;
    int grabbed_idx; /* -1 if none, else index into g_panels[] */
    int moved;
};

static pthread_mutex_t g_touch_lock = PTHREAD_MUTEX_INITIALIZER;
static struct canvas_finger_state g_fingers[CANVAS_MAX_FINGERS];

static int g_cfg_dirty;
static u64 g_cfg_save_after_ms;

/* ---- panel registry API ------------------------------------------------- */
static void clamp_panel_pos(const PilotPanel *p, int *x, int *y)
{
    int max_x = CANVAS_SCREEN_W - p->desc.w;
    int max_y = CANVAS_SCREEN_H - p->desc.h;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (*x < 0) *x = 0;
    if (*x > max_x) *x = max_x;
    if (*y < 0) *y = 0;
    if (*y > max_y) *y = max_y;
}

PilotPanel *canvas_panel_register(const PilotPanelDesc *desc)
{
    if (!desc || !desc->id || !desc->id[0] || desc->w <= 0 || desc->h <= 0)
        return NULL;

    pthread_mutex_lock(&g_panel_lock);
    if (g_panel_count >= PILOT_CANVAS_MAX_PANELS) {
        pthread_mutex_unlock(&g_panel_lock);
        return NULL;
    }
    PilotPanel *p = &g_panels[g_panel_count++];
    memset(p, 0, sizeof(*p));
    p->used = 1;
    p->desc = *desc;
    p->x = desc->default_x;
    p->y = desc->default_y;
    clamp_panel_pos(p, &p->x, &p->y);
    pthread_mutex_unlock(&g_panel_lock);
    return p;
}

void canvas_panel_get_pos(PilotPanel *panel, int *x, int *y)
{
    if (!panel)
        return;
    pthread_mutex_lock(&g_panel_lock);
    if (x) *x = panel->x;
    if (y) *y = panel->y;
    pthread_mutex_unlock(&g_panel_lock);
}

void *canvas_panel_user_data(PilotPanel *panel)
{
    return panel ? panel->desc.user_data : NULL;
}

int canvas_pilot_page_visible(void)
{
    return g_always_on != 0 || g_pilot_page[0] != 0 || g_pilot_page[1] != 0;
}

void canvas_set_lock_pos(int locked)
{
    g_lock_pos = locked ? 1 : 0;
}

int canvas_panel_any_attached(void)
{
    int attached = 0;
    pthread_mutex_lock(&g_panel_lock);
    for (u32 i = 0; i < g_panel_count && !attached; ++i) {
        for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d) {
            if (g_panels[i].canvas[d] && g_panels[i].widget[d] &&
                node_attached_to_widget(g_panels[i].canvas[d]->sprite,
                                        g_panels[i].widget[d])) {
                attached = 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_panel_lock);
    return attached;
}

int canvas_panel_get_attached_widgets(void *out_widgets[PILOT_DISPLAY_COUNT])
{
    int count = 0;
    for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d)
        out_widgets[d] = NULL;

    pthread_mutex_lock(&g_panel_lock);
    for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d) {
        for (u32 i = 0; i < g_panel_count; ++i) {
            if (g_panels[i].canvas[d] && g_panels[i].widget[d] &&
                node_attached_to_widget(g_panels[i].canvas[d]->sprite,
                                        g_panels[i].widget[d])) {
                out_widgets[d] = g_panels[i].widget[d];
                ++count;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_panel_lock);
    return count;
}

static int panel_lcd_is_attached(const PilotPanel *p)
{
    return p && p->canvas[0] && p->widget[0] &&
           node_attached_to_widget(p->canvas[0]->sprite, p->widget[0]);
}

static void panel_apply_pos_locked(PilotPanel *p)
{
    if (!p || !g_sym.elem_set_local_pos)
        return;
    float pos[2] = { (float)p->x, (float)p->y };
    for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d) {
        if (p->canvas[d] && p->widget[d] &&
            node_attached_to_widget(p->canvas[d]->sprite, p->widget[d]))
            g_sym.elem_set_local_pos(p->canvas[d]->sprite, pos);
    }
}

void canvas_panel_invalidate(PilotPanel *panel, int sync_ui)
{
    if (!panel || !panel->desc.on_render)
        return;
    int rendered = 0;
    pthread_mutex_lock(&g_panel_lock);
    for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d) {
        if (panel->canvas[d] && panel->widget[d] &&
            node_attached_to_widget(panel->canvas[d]->sprite, panel->widget[d])) {
            panel->desc.on_render(panel, panel->canvas[d]);
            if (!g_always_on && !g_pilot_page[d] && g_sym.sprite_set_count)
                g_sym.sprite_set_count(panel->canvas[d]->sprite, 0u);
            ++rendered;
        }
    }
    pthread_mutex_unlock(&g_panel_lock);
    if (rendered > 0) {
        if (sync_ui)
            canvas_notify_draw_sync();
        else
            canvas_notify_draw_async();
    }
}

__attribute__((visibility("default")))
PilotStatus pilot_canvas_get_geometry_v1(int *x, int *y, int *w, int *h)
{
    if (!x || !y || !w || !h)
        return PILOT_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_panel_lock);
    if (g_panel_count > 0) {
        *x = g_panels[0].x;
        *y = g_panels[0].y;
        *w = g_panels[0].desc.w;
        *h = g_panels[0].desc.h;
    } else {
        *x = 0; *y = 0; *w = 0; *h = 0;
    }
    pthread_mutex_unlock(&g_panel_lock);
    return PILOT_STATUS_OK;
}

/* ---- persistent layout/state config (layout.cfg) ------------------------ */
void canvas_config_mark_dirty(void)
{
    u64 now = canvas_now_ms();
    pthread_mutex_lock(&g_touch_lock);
    g_cfg_dirty = 1;
    g_cfg_save_after_ms = now + CANVAS_CFG_DEBOUNCE_MS;
    pthread_mutex_unlock(&g_touch_lock);
}

void canvas_config_load(void)
{
    if (!g_api || !g_api->file || !g_api->file->read)
        return;

    char buf[512];
    memset(buf, 0, sizeof(buf));
    PilotStatus rc = g_api->file->read(g_api->file, CANVAS_CFG_FILENAME,
                                       buf, sizeof(buf));
    if (rc != PILOT_STATUS_OK || !buf[0]) {
        mlogf("CANVAS_CFG_LOAD default rc=%d", (int)rc);
        return;
    }

    pthread_mutex_lock(&g_panel_lock);
    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r')
            ++p;
        if (*p) {
            *p = '\0';
            ++p;
        }
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        /* Backward-compatible bare x/y keys map to first draggable panel ("scope"). */
        if (strcmp(key, "x") == 0 || strcmp(key, "y") == 0) {
            for (u32 i = 0; i < g_panel_count; ++i) {
                if (!g_panels[i].desc.draggable)
                    continue;
                if (key[0] == 'x')
                    g_panels[i].x = (int)strtol(val, NULL, 10);
                else
                    g_panels[i].y = (int)strtol(val, NULL, 10);
                clamp_panel_pos(&g_panels[i], &g_panels[i].x, &g_panels[i].y);
                break;
            }
            continue;
        }

        /* Namespaced <id>.x / <id>.y or panel-specific keys. */
        int handled = 0;
        for (u32 i = 0; i < g_panel_count; ++i) {
            size_t idlen = strlen(g_panels[i].desc.id);
            if (strncmp(key, g_panels[i].desc.id, idlen) == 0 && key[idlen] == '.') {
                const char *sub = key + idlen + 1;
                if (strcmp(sub, "x") == 0) {
                    g_panels[i].x = (int)strtol(val, NULL, 10);
                    clamp_panel_pos(&g_panels[i], &g_panels[i].x, &g_panels[i].y);
                    handled = 1;
                } else if (strcmp(sub, "y") == 0) {
                    g_panels[i].y = (int)strtol(val, NULL, 10);
                    clamp_panel_pos(&g_panels[i], &g_panels[i].x, &g_panels[i].y);
                    handled = 1;
                } else if (g_panels[i].desc.on_cfg_load) {
                    g_panels[i].desc.on_cfg_load(&g_panels[i], sub, val);
                    handled = 1;
                }
                break;
            }
        }
        if (!handled) {
            for (u32 i = 0; i < g_panel_count; ++i) {
                if (g_panels[i].desc.on_cfg_load)
                    g_panels[i].desc.on_cfg_load(&g_panels[i], key, val);
            }
        }
    }

    int any_attached = 0;
    for (u32 i = 0; i < g_panel_count; ++i) {
        panel_apply_pos_locked(&g_panels[i]);
        for (u32 d = 0; d < PILOT_DISPLAY_COUNT; ++d) {
            if (g_panels[i].canvas[d] && g_panels[i].widget[d] &&
                node_attached_to_widget(g_panels[i].canvas[d]->sprite,
                                        g_panels[i].widget[d])) {
                if (g_panels[i].desc.on_render)
                    g_panels[i].desc.on_render(&g_panels[i], g_panels[i].canvas[d]);
                any_attached = 1;
            }
        }
    }
    if (g_panel_count > 0)
        mlogf("CANVAS_CFG_LOAD ok x=%d y=%d panels=%u",
              g_panels[0].x, g_panels[0].y, (unsigned)g_panel_count);
    pthread_mutex_unlock(&g_panel_lock);

    if (any_attached)
        canvas_notify_draw_async();
}

void canvas_config_flush_if_dirty(void)
{
    if (!g_api || !g_api->file || !g_api->file->write)
        return;

    u64 now = canvas_now_ms();
    int should_save = 0;
    pthread_mutex_lock(&g_touch_lock);
    if (g_cfg_dirty && now >= g_cfg_save_after_ms) {
        should_save = 1;
        g_cfg_dirty = 0;
    }
    pthread_mutex_unlock(&g_touch_lock);
    if (!should_save)
        return;

    char buf[512];
    size_t pos = 0;
    pthread_mutex_lock(&g_panel_lock);
    for (u32 i = 0; i < g_panel_count && pos + 64u < sizeof(buf); ++i) {
        if (i == 0) {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                                    "x=%d\ny=%d\n", g_panels[i].x, g_panels[i].y);
        } else {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                                    "%s.x=%d\n%s.y=%d\n",
                                    g_panels[i].desc.id, g_panels[i].x,
                                    g_panels[i].desc.id, g_panels[i].y);
        }
        if (g_panels[i].desc.on_cfg_save && pos + 32u < sizeof(buf)) {
            int n = g_panels[i].desc.on_cfg_save(&g_panels[i], buf + pos,
                                                 sizeof(buf) - pos);
            if (n > 0 && (size_t)n < sizeof(buf) - pos)
                pos += (size_t)n;
        }
    }
    pthread_mutex_unlock(&g_panel_lock);

    PilotStatus rc = g_api->file->write(g_api->file, CANVAS_CFG_FILENAME, buf);
    mlogf("CANVAS_CFG_SAVE rc=%d bytes=%u", (int)rc, (unsigned)pos);
}

/* ---- 3-GOT touch router (drag + tap + touch-focus cancel) --------------- */
static void pilot_post_mouse_move_hook(void *self, u8 finger, int16_t x,
                                       int16_t y, const void *msec)
{
    int suppress = 0;
    int moved_idx = -1;
    int nx = 0, ny = 0;

    if (finger < CANVAS_MAX_FINGERS) {
        pthread_mutex_lock(&g_touch_lock);
        struct canvas_finger_state *f = &g_fingers[finger];
        f->last_x = x;
        f->last_y = y;
        f->has_pos = 1;
        if (f->grabbed_idx >= 0) {
            suppress = 1;
            int dx = (int)x - (int)f->start_x;
            int dy = (int)y - (int)f->start_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy >= CANVAS_TAP_SLOP)
                f->moved = 1;
            moved_idx = f->grabbed_idx;
            nx = (int)x - f->drag_off_x;
            ny = (int)y - f->drag_off_y;
        }
        pthread_mutex_unlock(&g_touch_lock);
    }

    if (moved_idx >= 0) {
        int changed = 0;
        pthread_mutex_lock(&g_panel_lock);
        if (!g_lock_pos && (u32)moved_idx < g_panel_count &&
            g_panels[moved_idx].desc.draggable) {
            PilotPanel *p = &g_panels[moved_idx];
            clamp_panel_pos(p, &nx, &ny);
            if (nx != p->x || ny != p->y) {
                p->x = nx;
                p->y = ny;
                panel_apply_pos_locked(p);
                changed = 1;
            }
        }
        pthread_mutex_unlock(&g_panel_lock);
        if (changed)
            canvas_notify_draw_sync();
    }

    if (suppress)
        return;
    if (g_orig_post_mouse_move)
        g_orig_post_mouse_move(self, finger, x, y, msec);
}

static void pilot_post_mouse_press_hook(void *self, u8 finger, u8 btn,
                                        const void *msec)
{
    int suppress = 0;
    int hit_idx = -1;
    int lx = 0, ly = 0;
    ++g_click_count;

    if (finger < CANVAS_MAX_FINGERS) {
        int has_pos = 0;
        pthread_mutex_lock(&g_touch_lock);
        struct canvas_finger_state *f = &g_fingers[finger];
        has_pos = f->has_pos;
        lx = f->last_x;
        ly = f->last_y;
        f->grabbed_idx = -1;
        f->moved = 0;
        pthread_mutex_unlock(&g_touch_lock);

        if ((g_always_on || g_pilot_page[0]) && has_pos) {
            int off_x = 0, off_y = 0;
            pthread_mutex_lock(&g_panel_lock);
            for (int i = (int)g_panel_count - 1; i >= 0; --i) {
                PilotPanel *p = &g_panels[i];
                if (!panel_lcd_is_attached(p))
                    continue;
                if (!p->desc.draggable && !p->desc.on_tap)
                    continue;
                if (lx >= p->x && lx < p->x + p->desc.w &&
                    ly >= p->y && ly < p->y + p->desc.h) {
                    hit_idx = i;
                    off_x = lx - p->x;
                    off_y = ly - p->y;
                    break;
                }
            }
            pthread_mutex_unlock(&g_panel_lock);

            if (hit_idx >= 0) {
                pthread_mutex_lock(&g_touch_lock);
                f->grabbed_idx = hit_idx;
                f->moved = 0;
                f->start_x = (int16_t)lx;
                f->start_y = (int16_t)ly;
                f->drag_off_x = off_x;
                f->drag_off_y = off_y;
                pthread_mutex_unlock(&g_touch_lock);
                suppress = 1;
            }
        }
    }

    if (suppress) {
        mlogf("CANVAS_TOUCH_GRAB #%d f=%u panel=%d xy=%d,%d",
              g_click_count, finger, hit_idx, lx, ly);
        return;
    }
    if (g_orig_post_mouse_press)
        g_orig_post_mouse_press(self, finger, btn, msec);
}

static void pilot_post_mouse_release_hook(void *self, u8 finger, u8 btn,
                                          const void *msec)
{
    int suppress = 0;
    int was_tap = 0;
    int idx = -1;

    if (finger < CANVAS_MAX_FINGERS) {
        pthread_mutex_lock(&g_touch_lock);
        struct canvas_finger_state *f = &g_fingers[finger];
        if (f->grabbed_idx >= 0) {
            suppress = 1;
            was_tap = !f->moved;
            idx = f->grabbed_idx;
            f->grabbed_idx = -1;
            f->moved = 0;
        }
        pthread_mutex_unlock(&g_touch_lock);
    }

    if (suppress && idx >= 0 && (u32)idx < g_panel_count) {
        PilotPanel *p = &g_panels[idx];
        if (was_tap) {
            if (p->desc.on_tap)
                p->desc.on_tap(p);
            canvas_config_mark_dirty();
        } else {
            canvas_config_mark_dirty();
            int px = 0, py = 0;
            canvas_panel_get_pos(p, &px, &py);
            mlogf("CANVAS_TOUCH_DROP f=%u id=%s xy=%d,%d",
                  finger, p->desc.id, px, py);
        }
        return;
    }
    if (g_orig_post_mouse_release)
        g_orig_post_mouse_release(self, finger, btn, msec);
}

int canvas_install_touch_hook(void)
{
    if (g_touch_hooked)
        return 1;
    if (!canvas_appfw_base())
        return 0;

    for (u32 i = 0; i < CANVAS_MAX_FINGERS; ++i)
        g_fingers[i].grabbed_idx = -1;

    union { void *p; fn_post_mouse_move f; } u_move;
    union { void *p; fn_post_mouse_press f; } u_press;
    union { void *p; fn_post_mouse_release f; } u_rel;
    u_move.p  = dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem13postMouseMoveEhssNS_4core8MsecTimeE");
    u_press.p = dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem14postMousePressEhhNS_4core8MsecTimeE");
    u_rel.p   = dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem16postMouseReleaseEhhNS_4core8MsecTimeE");
    if (!u_move.p || !u_press.p || !u_rel.p)
        return 0;

    g_orig_post_mouse_move    = u_move.f;
    g_orig_post_mouse_press   = u_press.f;
    g_orig_post_mouse_release = u_rel.f;

    int ok_m = canvas_patch_appfw_got(APPFW_GOT_POST_MOUSE_MOVE, (void *)pilot_post_mouse_move_hook);
    int ok_p = canvas_patch_appfw_got(APPFW_GOT_POST_MOUSE_PRESS, (void *)pilot_post_mouse_press_hook);
    int ok_r = canvas_patch_appfw_got(APPFW_GOT_POST_MOUSE_RELEASE, (void *)pilot_post_mouse_release_hook);
    mlogf("CANVAS_TOUCH_GOT move=%d press=%d rel=%d", ok_m, ok_p, ok_r);
    if (ok_m && ok_p && ok_r) {
        g_touch_hooked = 1;
        return 1;
    }
    return 0;
}

/* ---- DISP virtual page controller (LCD + EVF) --------------------------- */
static void *g_anchor_widget[PILOT_DISPLAY_COUNT];
static void *g_cur_widget[PILOT_DISPLAY_COUNT];
static u32   g_disp_misses[PILOT_DISPLAY_COUNT];
static u32   g_evf_disp_step;

static void canvas_set_pilot_page(u32 disp, int visible)
{
    if (disp >= PILOT_DISPLAY_COUNT)
        return;
    g_pilot_page[disp] = visible ? 1 : 0;
    pthread_mutex_lock(&g_panel_lock);
    for (u32 i = 0; i < g_panel_count; ++i) {
        PilotPanel *p = &g_panels[i];
        if (!p->canvas[disp] || !p->widget[disp] ||
            !node_attached_to_widget(p->canvas[disp]->sprite, p->widget[disp]))
            continue;
        if (g_sym.sprite_set_count) {
            u16 cnt = visible ? p->canvas[disp]->rect_count : 0u;
            g_sym.sprite_set_count(p->canvas[disp]->sprite, cnt);
        }
        if (g_sym.elem_set_visible)
            g_sym.elem_set_visible(p->canvas[disp]->sprite, visible != 0);
    }
    pthread_mutex_unlock(&g_panel_lock);
    canvas_mark_all_texts_dirty();
    canvas_notify_draw_sync();
}

static void canvas_force_redraw(int is_evf)
{
    union { void *p; void (*fn)(int); } refresh;
    refresh.p = dlsym(RTLD_DEFAULT, "pilot_ui_lv_force_refresh_v1");
    if (refresh.p)
        refresh.fn(is_evf);
    canvas_notify_draw_sync();
}

void canvas_set_always_on(int always_on)
{
    int next = always_on ? 1 : 0;
    g_always_on = next;
    if (!g_sym_ready)
        return;
    int dm_lcd = canvas_read_disp_mode(0);
    int dm_evf = canvas_read_disp_mode(1);
    canvas_set_pilot_page(0, next ? 1 : (dm_lcd == (int)PILOT_DISP_NO_INFO));
    canvas_set_pilot_page(1, next ? 1 : (dm_evf == (int)PILOT_DISP_NO_INFO));
    canvas_force_redraw(0);
    canvas_force_redraw(1);
}

__attribute__((visibility("default")))
void pilot_canvas_track_widget_v1(void *widget, int is_evf)
{
    if (!widget)
        return;
    u32 d = is_evf ? 1u : 0u;
    g_cur_widget[d] = widget;
    if (!g_anchor_widget[d])
        g_anchor_widget[d] = widget;
}

static u32 canvas_disp_key_common(void *ctrl, const void *ev, int is_evf,
                                  fn_disp_key_handler orig)
{
    if (g_always_on)
        return orig ? orig(ctrl, ev) : 1u;

    u32 d = is_evf ? 1u : 0u;
    int dm = canvas_read_disp_mode(is_evf);

    if (g_pilot_page[d]) {
        canvas_set_pilot_page(d, 0);
        g_disp_misses[d] = 0u;
        if (is_evf)
            g_evf_disp_step = 1u;
        mlogf("CANVAS_DISP kind=%s dm=%d page=1->0 fwd=1",
              is_evf ? "EVF" : "LCD", dm);
        return orig ? orig(ctrl, ev) : 1u;
    }

    /* Primary exact path: insert virtual Pilot page right after native NO_INFO (1)
     * regardless of which DISP page the camera booted on. */
    if (dm >= 0) {
        if (dm == (int)PILOT_DISP_NO_INFO) {
            canvas_set_pilot_page(d, 1);
            canvas_force_redraw(is_evf);
            mlogf("CANVAS_DISP kind=%s dm=%d page=0->1 fwd=0",
                  is_evf ? "EVF" : "LCD", dm);
            return 1u;
        }
        mlogf("CANVAS_DISP kind=%s dm=%d page=0 fwd=1",
              is_evf ? "EVF" : "LCD", dm);
        return orig ? orig(ctrl, ev) : 1u;
    }

    /* Fallback if BSS mode read ever fails. */
    void *cur = g_cur_widget[d];
    void *anchor = g_anchor_widget[d];
    if (is_evf) {
        if (g_evf_disp_step == 0u) {
            canvas_set_pilot_page(1, 1);
            canvas_force_redraw(1);
            return 1u;
        }
        g_evf_disp_step = (g_evf_disp_step + 1u) % 4u;
        return orig ? orig(ctrl, ev) : 1u;
    }
    if (cur && anchor && cur == anchor) {
        g_disp_misses[d] = 0u;
        canvas_set_pilot_page(0, 1);
        canvas_force_redraw(0);
        return 1u;
    }
    if (++g_disp_misses[d] >= 6u && cur) {
        g_anchor_widget[d] = cur;
        g_disp_misses[d] = 0u;
        canvas_set_pilot_page(0, 1);
        canvas_force_redraw(0);
        return 1u;
    }
    return orig ? orig(ctrl, ev) : 1u;
}

static u32 pilot_lcd_disp_key_hook(void *ctrl, const void *ev)
{
    return canvas_disp_key_common(ctrl, ev, 0, g_orig_lcd_disp_key);
}

static u32 pilot_evf_disp_key_hook(void *ctrl, const void *ev)
{
    return canvas_disp_key_common(ctrl, ev, 1, g_orig_evf_disp_key);
}

int canvas_install_disp_hook(void)
{
    if (g_disp_hooked)
        return 1;
    uintptr_t base = canvas_appfw_base();
    if (!base)
        return 0;

    /* Initialize per-display Pilot page state from live DISP mode so booting
     * on ALL_INFO / HISTOGRAM / LEVEL never overlaps native info overlays. */
    int dm_lcd = canvas_read_disp_mode(0);
    int dm_evf = canvas_read_disp_mode(1);
    g_pilot_page[0] = (dm_lcd == (int)PILOT_DISP_NO_INFO) ? 1 : 0;
    g_pilot_page[1] = (dm_evf == (int)PILOT_DISP_NO_INFO) ? 1 : 0;

    g_orig_lcd_disp_key = (fn_disp_key_handler)(base + APPFW_FN_LV_LCD_DISP_KEY);
    g_orig_evf_disp_key = (fn_disp_key_handler)(base + APPFW_FN_LV_EVF_DISP_KEY);

    int ok_lcd = canvas_patch_exact_got(APPFW_GOT_LV_LCD_DISP_KEY,
                                        APPFW_FN_LV_LCD_DISP_KEY,
                                        (void *)pilot_lcd_disp_key_hook);
    int ok_evf = canvas_patch_exact_got(APPFW_GOT_LV_EVF_DISP_KEY,
                                        APPFW_FN_LV_EVF_DISP_KEY,
                                        (void *)pilot_evf_disp_key_hook);
    mlogf("CANVAS_DISP_GOT lcd=%d evf=%d dm=%d/%d page=%d/%d",
          ok_lcd, ok_evf, dm_lcd, dm_evf, g_pilot_page[0], g_pilot_page[1]);
    if (ok_lcd || ok_evf) {
        g_disp_hooked = 1;
        return 1;
    }
    return 0;
}

/* ---- host widget attachment (called by menu_ui on LV UI thread) --------- */
__attribute__((visibility("default")))
int pilot_canvas_is_active_v1(void)
{
    if (!g_sym_ready)
        return 0;
    if ((g_always_on || g_pilot_page[0] || g_pilot_page[1]) && g_panel_count > 0 &&
        g_sym.widget_init_sprite && g_sym.sprite_ctor)
        return 1;
    return canvas_has_active_external_texts();
}

__attribute__((visibility("default")))
PilotStatus pilot_canvas_attach_v2(void *widget, int is_evf)
{
    if (!widget)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (!g_sym.widget_init_sprite || !g_sym.sprite_ctor)
        return PILOT_STATUS_NOT_FOUND;

    u32 disp = is_evf ? 1u : 0u;
    int dm = canvas_read_disp_mode(is_evf);
    if (!g_always_on && dm >= 0 && dm != (int)PILOT_DISP_NO_INFO && g_pilot_page[disp])
        g_pilot_page[disp] = 0;
    int show = g_always_on ? 1 : g_pilot_page[disp];

    pthread_mutex_lock(&g_panel_lock);
    for (u32 i = 0; i < g_panel_count; ++i) {
        PilotPanel *p = &g_panels[i];
        if (!p->canvas[disp])
            p->canvas[disp] = pilot_canvas_create_v1((u16)p->desc.w, (u16)p->desc.h,
                                                     PILOT_CANVAS_BACKEND_RECT);
        if (!p->canvas[disp])
            continue;

        if (p->widget[disp] != widget ||
            !node_attached_to_widget(p->canvas[disp]->sprite, widget)) {
            g_sym.widget_init_sprite(widget, p->canvas[disp]->sprite,
                                     p->canvas[disp]->rect_capacity);
            p->widget[disp] = widget;
            mlogf("CANVAS_PANEL attach id=%s disp=%u widget=%p sprite=%p cap=%u",
                  p->desc.id, disp, widget, p->canvas[disp]->sprite,
                  (unsigned)p->canvas[disp]->rect_capacity);
            if (p->desc.on_render)
                p->desc.on_render(p, p->canvas[disp]);
        }

        if (g_sym.elem_set_local_pos) {
            float pos[2] = { (float)p->x, (float)p->y };
            g_sym.elem_set_local_pos(p->canvas[disp]->sprite, pos);
        }
        if (g_sym.sprite_set_count) {
            u16 cnt = show ? p->canvas[disp]->rect_count : 0u;
            g_sym.sprite_set_count(p->canvas[disp]->sprite, cnt);
        }
        if (g_sym.elem_set_visible)
            g_sym.elem_set_visible(p->canvas[disp]->sprite, show != 0);
    }
    pthread_mutex_unlock(&g_panel_lock);

    canvas_sync_texts(widget, disp);
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default")))
PilotStatus pilot_canvas_demo_attach_v1(void *widget)
{
    return pilot_canvas_attach_v2(widget, 0);
}
