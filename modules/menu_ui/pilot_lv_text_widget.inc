/* ILCE-6700 2.00: the update slots belong to scr0132_0001/0002_presenter.
 * Each presenter has a seven-slot vtable. The next RTTI header starts a new
 * class; these offsets are slot 2 of the actual Live View presenters.
 *
 * Use the AP text slots that displayed Pilot's original video-mode banner.
 * DLGREC1025 is not backed by a matching AP widget in the reported video
 * startup. Render only the selected view_string, with a small text profile.
 */
#define APPFW_LV_LCD_VTABLE_UPDATE_OFF  0x4C6DBF8ull
#define APPFW_LV_LCD_UPDATE_ORIG_OFF    0x25D8960ull
#define APPFW_LV_EVF_VTABLE_UPDATE_OFF  0x4C74600ull
#define APPFW_LV_EVF_UPDATE_ORIG_OFF    0x2650B08ull

#define APPFW_LV_LCD_LAYOUT_GOT_OFF    0x4EF70A8ull
#define APPFW_LV_LCD_LAYOUT_OFF        0x50749F0ull
#define APPFW_LV_EVF_LAYOUT_GOT_OFF    0x4E91820ull
#define APPFW_LV_EVF_LAYOUT_OFF        0x4FD96B0ull
#define APPFW_LV_LCD_TEXT_IDX          391u /* No1339_FooterTouchBackgroundDefocusSettingBeltValueName */
#define APPFW_LV_EVF_TEXT_IDX          263u /* No583_IntervalshootingcancelguideMessage */

#define APPFW_VIEW_MODEL_GET_OFF       0x19452F8ull
#define APPFW_VIEW_STRING_PARAM_GET_OFF 0x19A61B0ull
#define APPFW_VIEW_STRING_UPDATE_OFF   0x19A78E8ull
#define APPFW_VIEW_STRING_TYPE_OFF     0x8343B90ull
#define APPFW_VIEW_WIDGET_COUNT_OFF    0x5DD0ull
#define APPFW_VIEW_WIDGETS_OFF         0xA05E00ull
#define APPFW_VIEW_STRING_PARAM_OFF    0x1D0ull

#define LV_TEXT_VISIBLE_OFF            0x2Cu
#define LV_TEXT_RID_OFF                0x30u
#define LV_TEXT_CSTR_OFF               0x38u
#define LV_TEXT_ARG_COUNT_OFF          0x328u
#define LV_TEXT_GEOMETRY_OFF           0x18u
#define LV_TEXT_FONTS_OFF              0x44u
#define PILOT_LV_TEXT_FONT_RID         0x0207A018u /* native movie recording-time font */
#define PILOT_LV_TEXT_DEFAULT_FONT_RID 0x0207A000u

typedef void (*lv_presenter_update_fn)(void *presenter, uint32_t flag);
typedef void (*lv_text_widget_update_fn)(void *widget);
typedef void (*lv_text_set_font_fn)(void *sprite, uint32_t *font_rids,
                                    uint8_t count);

struct lv_text_geometry {
    int32_t x, y, width, height;
};

/* Full-screen fixed host geometry for both LCD and EVF. The host view_string
 * never moves; individual Pilot Canvas sprites and text slots position
 * themselves independently via Element::setLocalPos so dragging a panel never
 * moves system toasts (Pilot Ready / 3K OG). */
static const struct lv_text_geometry g_lv_text_geometry[2] = {
    {0, 0, 640, 480},
    {0, 0, 640, 480}
};

struct lv_text_render_probe {
    const char *text;
    void *sprite;
    uint32_t calls;
};

struct lv_text_render_state {
    void *widget;
    uint32_t seq;
    int active;
    const char *error;
};

static lv_presenter_update_fn g_orig_lv_lcd_update;
static lv_presenter_update_fn g_orig_lv_evf_update;
static lv_text_set_font_fn g_lv_set_text_font;
static pthread_mutex_t g_lv_render_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread struct lv_text_render_probe *g_lv_render_probe;
static struct lv_text_render_state g_lv_render_states[2];
static void *g_lv_last_presenter[2];

static void pilot_lv_set_string_cstr_hook(void *sprite, const char *text)
{
    struct lv_text_render_probe *probe = g_lv_render_probe;
    if (probe && text == probe->text) {
        /* This is the font profile used by the original small Pilot banner.
         * Apply it after the native layout/font setup and before setString.
         */
        uint32_t fonts[2] = {
            PILOT_LV_TEXT_FONT_RID, PILOT_LV_TEXT_DEFAULT_FONT_RID
        };
        g_lv_set_text_font(sprite, fonts, 2u);
        probe->sprite = sprite;
        ++probe->calls;
    }
    g_orig_set_string_cstr(sprite, text);
}

static void lv_text_bind_error(int is_evf, struct lv_text_render_state *state,
                               const char *stage)
{
    if (state->error == stage)
        return;
    state->error = stage;
    char message[160];
    snprintf(message, sizeof(message),
             "PILOT_LV_UI_ERR kind=%s idx=%u stage=%s",
             is_evf ? "EVF" : "LCD",
             is_evf ? APPFW_LV_EVF_TEXT_IDX : APPFW_LV_LCD_TEXT_IDX,
             stage);
    ui_log(message);
}

static void lv_canvas_track_widget(void *widget, int is_evf)
{
    if (!widget)
        return;
    union {
        void *object;
        void (*function)(void *, int);
    } fn;
    fn.object = dlsym(RTLD_DEFAULT, "pilot_canvas_track_widget_v1");
    if (fn.object)
        fn.function(widget, is_evf);
}

static int lv_canvas_is_active(void)
{
    union {
        void *object;
        int (*function)(void);
    } fn;
    fn.object = dlsym(RTLD_DEFAULT, "pilot_canvas_is_active_v1");
    if (fn.object)
        return fn.function() != 0;
    return 0;
}

static void lv_canvas_attach(void *widget, int is_evf)
{
    union {
        void *object;
        PilotStatus (*v2)(void *, int);
        PilotStatus (*v1)(void *);
    } canvas;
    canvas.object = dlsym(RTLD_DEFAULT, "pilot_canvas_attach_v2");
    if (canvas.object) {
        (void)canvas.v2(widget, is_evf);
        return;
    }
    canvas.object = dlsym(RTLD_DEFAULT, "pilot_canvas_demo_attach_v1");
    if (canvas.object)
        (void)canvas.v1(widget);
}

static void lv_apply_banner_common(void *presenter, int is_evf)
{
    if (!presenter || !g_appfw_base)
        return;

    g_lv_last_presenter[!!is_evf] = presenter;

    char text[160];
    uint32_t seq = 0u;
    int active = lv_copy_banner_snapshot(text, sizeof(text), &seq);
    int canvas_active = lv_canvas_is_active();

    pthread_mutex_lock(&g_lv_render_lock);
    struct lv_text_render_state *state = &g_lv_render_states[!!is_evf];

    /* Always report the live view_string widget pointer to pilot_canvas so
     * its DISP virtual-page state machine tracks native page switches even
     * while the Pilot page is hidden. */
    {
        uint32_t tidx = is_evf ? APPFW_LV_EVF_TEXT_IDX : APPFW_LV_LCD_TEXT_IDX;
        uint8_t *tp = (uint8_t *)presenter;
        uint8_t *tview = *(uint8_t **)(tp + 8u);
        if (tview) {
            uint64_t tcnt = *(uint64_t *)(tview + APPFW_VIEW_WIDGET_COUNT_OFF);
            if (tcnt > tidx && tcnt <= 999u) {
                void *tw = *(void **)(tview + APPFW_VIEW_WIDGETS_OFF + tidx * 8u);
                if (tw)
                    lv_canvas_track_widget(tw, is_evf);
            }
        }
    }

    if (!active && !canvas_active) {
        if (state->active) {
            char message[80];
            snprintf(message, sizeof(message),
                     "PILOT_LV_UI_NATIVE kind=%s restored=1",
                     is_evf ? "EVF" : "LCD");
            ui_log(message);
            state->active = 0;
        }
        pthread_mutex_unlock(&g_lv_render_lock);
        return;
    }
    if (!active) {
        /* Banner expired, but pilot_canvas has active graphics/text slots.
         * Keep the host view_string render pass alive with a blank base string. */
        text[0] = ' ';
        text[1] = '\0';
    }

    uint32_t idx = is_evf ? APPFW_LV_EVF_TEXT_IDX : APPFW_LV_LCD_TEXT_IDX;
    uintptr_t got_off = is_evf ? APPFW_LV_EVF_LAYOUT_GOT_OFF
                                : APPFW_LV_LCD_LAYOUT_GOT_OFF;
    uintptr_t layout_off = is_evf ? APPFW_LV_EVF_LAYOUT_OFF
                                   : APPFW_LV_LCD_LAYOUT_OFF;
    uint8_t *layout = *(uint8_t **)(g_appfw_base + got_off);
    uint8_t *p = (uint8_t *)presenter;
    uint8_t *view = *(uint8_t **)(p + 8u);
    uint8_t *model = *(uint8_t **)(p + 16u);
    if (!view || !model || (uintptr_t)layout != g_appfw_base + layout_off) {
        lv_text_bind_error(is_evf, state, "view_or_layout");
        goto done;
    }

    uintptr_t *view_vtable = *(uintptr_t **)view;
    uint64_t count = *(uint64_t *)(view + APPFW_VIEW_WIDGET_COUNT_OFF);
    if (!view_vtable ||
        view_vtable[6] != g_appfw_base + APPFW_VIEW_MODEL_GET_OFF ||
        model != view + 16u || count <= idx || count > 999u) {
        lv_text_bind_error(is_evf, state, "view_model");
        goto done;
    }

    uint8_t *record = layout + (uintptr_t)idx * 0x60u;
    uint8_t *entry = model + (uintptr_t)idx * 0x18u;
    uintptr_t type_addr = g_appfw_base + APPFW_VIEW_STRING_TYPE_OFF;
    if (*(uintptr_t *)record != type_addr ||
        *(uint32_t *)(record + 8u) != idx ||
        *(uintptr_t *)entry != *(uintptr_t *)type_addr) {
        lv_text_bind_error(is_evf, state, "string_type");
        goto done;
    }

    uint8_t *param = *(uint8_t **)(entry + 16u);
    void *widget = *(void **)(view + APPFW_VIEW_WIDGETS_OFF + idx * 8u);
    if (!widget || !param ||
        param != (uint8_t *)widget + APPFW_VIEW_STRING_PARAM_OFF) {
        if (!state->error) {
            char message[224];
            snprintf(message, sizeof(message),
                     "PILOT_LV_UI_BIND kind=%s idx=%u layout_cp=%u "
                     "widget=%p param=%p expected_param=%p count=%llu",
                     is_evf ? "EVF" : "LCD", idx,
                     (unsigned)record[0x58u], widget, param,
                     widget ? (uint8_t *)widget + APPFW_VIEW_STRING_PARAM_OFF
                            : NULL,
                     (unsigned long long)count);
            ui_log(message);
        }
        lv_text_bind_error(is_evf, state, "string_widget");
        goto done;
    }
    uintptr_t *widget_vtable = *(uintptr_t **)widget;
    if (!widget_vtable ||
        widget_vtable[0x140u / 8u] !=
            g_appfw_base + APPFW_VIEW_STRING_PARAM_GET_OFF ||
        widget_vtable[0x150u / 8u] !=
            g_appfw_base + APPFW_VIEW_STRING_UPDATE_OFF) {
        lv_text_bind_error(is_evf, state, "string_methods");
        goto done;
    }

    /* Parameters belong to Sony. Override them only during this AP text
     * render, then restore them before the native controller reads them.
     * A non-null C string selects view_string's native setString(const char*)
     * branch. No custom RID, visibility dispatcher or whole-view update.
     */
    uint8_t visible = param[LV_TEXT_VISIBLE_OFF];
    uint32_t rid = *(uint32_t *)(param + LV_TEXT_RID_OFF);
    const char *native_text = *(const char **)(param + LV_TEXT_CSTR_OFF);
    uint32_t arg_count = *(uint32_t *)(param + LV_TEXT_ARG_COUNT_OFF);
    struct lv_text_geometry native_geometry;
    uint32_t native_fonts[2];
    const struct lv_text_geometry *geometry = &g_lv_text_geometry[!!is_evf];
    memcpy(&native_geometry, param + LV_TEXT_GEOMETRY_OFF,
           sizeof(native_geometry));
    memcpy(native_fonts, param + LV_TEXT_FONTS_OFF, sizeof(native_fonts));
    param[LV_TEXT_VISIBLE_OFF] = 1u;
    *(uint32_t *)(param + LV_TEXT_RID_OFF) = 0xFFFFFFFFu;
    *(const char **)(param + LV_TEXT_CSTR_OFF) = text;
    *(uint32_t *)(param + LV_TEXT_ARG_COUNT_OFF) = 0u;
    memcpy(param + LV_TEXT_GEOMETRY_OFF, geometry, sizeof(*geometry));
    *(uint32_t *)(param + LV_TEXT_FONTS_OFF) = PILOT_LV_TEXT_FONT_RID;
    *(uint32_t *)(param + LV_TEXT_FONTS_OFF + 4u) =
        PILOT_LV_TEXT_DEFAULT_FONT_RID;

    /* Optional Canvas integration. When pilot_canvas.so is loaded, bind/sync
     * its native Sprite and independent text slots for this display (LCD/EVF)
     * BEFORE the widget update draws. */
    lv_canvas_attach(widget, is_evf);

    struct lv_text_render_probe probe = { text, NULL, 0u };
    g_lv_render_probe = &probe;
    ((lv_text_widget_update_fn)widget_vtable[0x150u / 8u])(widget);
    g_lv_render_probe = NULL;

    param[LV_TEXT_VISIBLE_OFF] = visible;
    *(uint32_t *)(param + LV_TEXT_RID_OFF) = rid;
    *(const char **)(param + LV_TEXT_CSTR_OFF) = native_text;
    *(uint32_t *)(param + LV_TEXT_ARG_COUNT_OFF) = arg_count;
    memcpy(param + LV_TEXT_GEOMETRY_OFF, &native_geometry,
           sizeof(native_geometry));
    memcpy(param + LV_TEXT_FONTS_OFF, native_fonts, sizeof(native_fonts));

    if (probe.calls == 0u) {
        lv_text_bind_error(is_evf, state, "cstr_not_called");
    } else if (!state->active || state->widget != widget || state->seq != seq ||
               state->error) {
        char message[320];
        snprintf(message, sizeof(message),
                 "PILOT_LV_UI_RENDER kind=%s idx=%u seq=%u cstr_calls=%u "
                 "bytes=%zu widget=%p sprite=%p xy=%d,%d wh=%d,%d "
                 "font=0x%x profile=small-left native_rid=0x%x "
                 "native_visible=%u",
                 is_evf ? "EVF" : "LCD", idx, seq, probe.calls,
                 strlen(text), widget, probe.sprite,
                 geometry->x, geometry->y, geometry->width, geometry->height,
                 PILOT_LV_TEXT_FONT_RID, rid, (unsigned)visible);
        ui_log(message);
        state->error = NULL;
    }
    state->active = 1;
    state->widget = widget;
    state->seq = seq;
done:
    pthread_mutex_unlock(&g_lv_render_lock);
}

static void pilot_lv_lcd_update_hook(void *presenter, uint32_t flag)
{
    g_orig_lv_lcd_update(presenter, flag);
    lv_apply_banner_common(presenter, 0);
}

static void pilot_lv_evf_update_hook(void *presenter, uint32_t flag)
{
    g_orig_lv_evf_update(presenter, flag);
    lv_apply_banner_common(presenter, 1);
}

__attribute__((visibility("default")))
void pilot_ui_lv_force_refresh_v1(int is_evf)
{
    void *p = g_lv_last_presenter[!!is_evf];
    if (p)
        lv_apply_banner_common(p, !!is_evf);
}

static PilotStatus lv_install_text_render_probe(void)
{
    g_lv_set_text_font = (lv_text_set_font_fn)dlsym(
        RTLD_DEFAULT, "_ZN2ux6gfxeng10TextSprite7setFontEPjh");
    if (!g_lv_set_text_font) {
        ui_log("PILOT_LV_UI_ERR stage=small_font_symbol");
        return PILOT_STATUS_NOT_FOUND;
    }
    uintptr_t current = 0u;
    PilotStatus status = g_api->memory_read(
        g_appfw_base + APPFW_GOT_SET_STRING_CSTR, &current, sizeof(current));
    if (status != PILOT_STATUS_OK)
        return status;
    return patch_got_ptr(g_appfw_base + APPFW_GOT_SET_STRING_CSTR, current,
                         (uintptr_t)pilot_lv_set_string_cstr_hook);
}
