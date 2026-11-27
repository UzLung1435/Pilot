/* Pilot Live View text service, startup progress and public text API.
 * Native text-widget binding and rendering live in pilot_lv_text_widget.inc.
 */

static pthread_mutex_t g_lv_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_lv_banner_text[160] = "";
static uint32_t g_lv_banner_seq = 0u;
static int g_lv_banner_active = 0;
static uint64_t g_lv_banner_deadline_ms = 0u;
static int g_lv_ui_available = 0;

static int g_lv_cfg_boot_toast = 1;
static int g_lv_cfg_show_progress = 1;
static uint32_t g_lv_cfg_toast_ms = 4000u;

static char g_lv_expected_mods[PILOT_LIST_ITEMS_MAX][64];
static size_t g_lv_expected_count = 0u;

static int lv_parse_json_bool(const char *json, const char *key, int def_val)
{
    if (!json || !key)
        return def_val;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p)
        return def_val;
    p = strchr(p + strlen(pat), ':');
    if (!p)
        return def_val;
    p = skip_ws(p + 1);
    if (!p)
        return def_val;
    if (strncmp(p, "true", 4) == 0 || *p == '1')
        return 1;
    if (strncmp(p, "false", 5) == 0 || *p == '0')
        return 0;
    return def_val;
}

static uint32_t lv_parse_json_u32(const char *json, const char *key,
                                  uint32_t def_val)
{
    if (!json || !key)
        return def_val;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p)
        return def_val;
    p = strchr(p + strlen(pat), ':');
    if (!p)
        return def_val;
    p = skip_ws(p + 1);
    if (!p || *p < '0' || *p > '9')
        return def_val;
    uint32_t v = 0u;
    while (*p >= '0' && *p <= '9') {
        uint32_t d = (uint32_t)(*p - '0');
        if (v > (0xFFFFFFFFu - d) / 10u)
            return def_val;
        v = v * 10u + d;
        ++p;
    }
    return v;
}

static void lv_load_config(void)
{
    g_lv_cfg_boot_toast = 1;
    g_lv_cfg_show_progress = 1;
    g_lv_cfg_toast_ms = 4000u;

    char json[1024];
    json[0] = '\0';
    const char *src = "default";

    if (g_api && g_api->file && g_api->file->read &&
        g_api->file->read(g_api->file, "config.json", json, sizeof(json)) ==
            PILOT_STATUS_OK &&
        json[0]) {
        src = "moduleDir";
    } else {
        char path[128];
        ensure_pilot_drive_letter();
        snprintf(path, sizeof(path), "%c:/pilot/modules/menu_ui/config.json",
                 g_pilot_drive_letter);
        if (read_pilot_file_fsys(path, json, sizeof(json)) && json[0])
            src = "menu_ui_fallback";
    }

    if (json[0]) {
        g_lv_cfg_boot_toast =
            lv_parse_json_bool(json, "lv_boot_toast", g_lv_cfg_boot_toast);
        g_lv_cfg_show_progress = lv_parse_json_bool(
            json, "lv_show_progress", g_lv_cfg_show_progress);
        uint32_t ms =
            lv_parse_json_u32(json, "lv_toast_ms", g_lv_cfg_toast_ms);
        if (ms < 1000u)
            ms = 1000u;
        if (ms > 15000u)
            ms = 15000u;
        g_lv_cfg_toast_ms = ms;
    }

    char logbuf[128];
    snprintf(logbuf, sizeof(logbuf),
             "PILOT_LV_UI_CFG src=%s toast=%d progress=%d ms=%u",
             src, g_lv_cfg_boot_toast, g_lv_cfg_show_progress,
             (unsigned)g_lv_cfg_toast_ms);
    ui_log(logbuf);
}

static uint64_t lv_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0u;
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000l);
}

static void lv_set_banner_internal(const char *msg, uint32_t duration_ms)
{
    uint64_t now = lv_now_ms();
    pthread_mutex_lock(&g_lv_lock);
    if (!msg || !msg[0]) {
        g_lv_banner_active = 0;
        g_lv_banner_deadline_ms = 0u;
    } else {
        snprintf(g_lv_banner_text, sizeof(g_lv_banner_text), "%s", msg);
        ++g_lv_banner_seq;
        g_lv_banner_active = 1;
        g_lv_banner_deadline_ms =
            (duration_ms > 0u && now > 0u) ? (now + duration_ms) : 0u;
    }
    pthread_mutex_unlock(&g_lv_lock);
}

__attribute__((visibility("default")))
PilotStatus pilot_ui_text_show_v1(const char *msg, uint32_t duration_ms)
{
    if (!msg || !msg[0])
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (strnlen(msg, sizeof(g_lv_banner_text)) >= sizeof(g_lv_banner_text))
        return PILOT_STATUS_RANGE;
    if (!g_lv_ui_available)
        return PILOT_STATUS_DISABLED;
    lv_set_banner_internal(msg, duration_ms);
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default")))
PilotStatus pilot_ui_text_clear_v1(void)
{
    if (!g_lv_ui_available)
        return PILOT_STATUS_DISABLED;
    lv_set_banner_internal(NULL, 0u);
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default")))
void pilot_lv_show_toast(const char *msg, uint32_t duration_ms)
{
    if (duration_ms == 0u)
        duration_ms = 2500u;
    (void)pilot_ui_text_show_v1(msg, duration_ms);
}

static int lv_copy_banner_snapshot(char *out, size_t cap, uint32_t *out_seq)
{
    uint64_t now = lv_now_ms();
    pthread_mutex_lock(&g_lv_lock);
    if (g_lv_banner_active && g_lv_banner_deadline_ms > 0u &&
        now >= g_lv_banner_deadline_ms) {
        g_lv_banner_active = 0;
        g_lv_banner_deadline_ms = 0u;
    }
    int active = g_lv_banner_active;
    snprintf(out, cap, "%s", active ? g_lv_banner_text : "");
    *out_seq = g_lv_banner_seq;
    pthread_mutex_unlock(&g_lv_lock);
    return active;
}

#include "pilot_lv_text_widget.inc"

struct lv_mod_scan_ctx {
    uint32_t loaded_mask;
    int has_6k_og;
};

static int lv_scan_phdr_cb(struct dl_phdr_info *info, size_t size, void *opaque)
{
    (void)size;
    struct lv_mod_scan_ctx *ctx = (struct lv_mod_scan_ctx *)opaque;
    if (!info || !info->dlpi_name || !info->dlpi_name[0])
        return 0;

    if (strstr(info->dlpi_name, "pilot_cp_6k_og.so"))
        ctx->has_6k_og = 1;

    for (size_t i = 0; i < g_lv_expected_count && i < 32u; ++i) {
        if (g_lv_expected_mods[i][0] &&
            strstr(info->dlpi_name, g_lv_expected_mods[i])) {
            ctx->loaded_mask |= (1u << i);
        }
    }
    return 0;
}

static uint32_t lv_popcount32(uint32_t v)
{
    uint32_t c = 0u;
    while (v) {
        c += (v & 1u);
        v >>= 1;
    }
    return c;
}

static void *pilot_lv_monitor_thread(void *arg)
{
    (void)arg;
    size_t total = g_lv_expected_count;
    if (total == 0u)
        total = 1u;

    uint32_t last_loaded = 0u;
    int all_ready = 0;
    int has_6k = 0;

    /* Poll up to 12 seconds (120 * 100ms) for remaining modules in modules.json */
    for (int tick = 0; tick < 120; ++tick) {
        struct lv_mod_scan_ctx ctx;
        ctx.loaded_mask = 0u;
        ctx.has_6k_og = 0;
        dl_iterate_phdr(lv_scan_phdr_cb, &ctx);

        uint32_t loaded = lv_popcount32(ctx.loaded_mask);
        if (loaded == 0u)
            loaded = 1u; /* menu_ui itself is already running */
        has_6k = ctx.has_6k_og;

        if (loaded >= (uint32_t)total) {
            all_ready = 1;
            char msg[128];
            if (has_6k) {
                snprintf(msg, sizeof(msg),
                         "Pilot Ready [6K] (%u/%zu)",
                         loaded, total);
            } else {
                snprintf(msg, sizeof(msg),
                         "Pilot Ready (%u/%zu)",
                         loaded, total);
            }
            lv_set_banner_internal(msg, g_lv_cfg_toast_ms);
            break;
        }

        if (g_lv_cfg_show_progress && loaded != last_loaded) {
            last_loaded = loaded;
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Pilot Loading (%u/%zu)...",
                     loaded, total);
            lv_set_banner_internal(msg, 12000u);
        } else if (loaded != last_loaded) {
            last_loaded = loaded;
        }
        usleep(100000);
    }

    if (!all_ready) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Pilot Loaded (%u/%zu)",
                 last_loaded ? last_loaded : 1u, total);
        lv_set_banner_internal(msg, g_lv_cfg_toast_ms);
    }

    /* Canvas keepalive is now driven by pilot_canvas_is_active_v1() directly,
     * so we no longer overwrite the boot toast with a permanent banner. */

    char logbuf[96];
    snprintf(logbuf, sizeof(logbuf),
             "PILOT_LV_UI_MON done ready=%d total=%zu has_6k=%d",
             all_ready, total, has_6k);
    ui_log(logbuf);
    return NULL;
}

static void install_pilot_lv_ui(void)
{
    lv_load_config();

    /* Snapshot enabled modules from modules.json synchronously while on init thread */
    g_lv_expected_count = 0u;
    char json[2048];
    if (read_modules_json_fsys(json, sizeof(json))) {
        parse_modules_json(json);
        for (size_t i = 0; i < g_mod_count && g_lv_expected_count < PILOT_LIST_ITEMS_MAX; ++i) {
            if (g_mod_enabled[i] && g_mod_names[i][0]) {
                snprintf(g_lv_expected_mods[g_lv_expected_count],
                         sizeof(g_lv_expected_mods[0]), "%.63s", g_mod_names[i]);
                ++g_lv_expected_count;
            }
        }
    }
    if (g_lv_expected_count == 0u) {
        snprintf(g_lv_expected_mods[0], sizeof(g_lv_expected_mods[0]),
                 "pilot_menu_ui.so");
        g_lv_expected_count = 1u;
    }

    PilotStatus render_status = lv_install_text_render_probe();
    if (render_status != PILOT_STATUS_OK) {
        ui_log("PILOT_LV_UI_ERR stage=cstr_hook");
        return;
    }

    uintptr_t lcd_slot = g_appfw_base + APPFW_LV_LCD_VTABLE_UPDATE_OFF;
    uintptr_t lcd_expected = g_appfw_base + APPFW_LV_LCD_UPDATE_ORIG_OFF;
    uintptr_t lcd_cur = 0u;
    int lcd_ok = 0;
    if (g_api->memory_read(lcd_slot, &lcd_cur, sizeof(lcd_cur)) == PILOT_STATUS_OK &&
        lcd_cur == lcd_expected) {
        g_orig_lv_lcd_update = (lv_presenter_update_fn)lcd_cur;
        if (patch_got_ptr(lcd_slot, lcd_cur,
                          (uintptr_t)pilot_lv_lcd_update_hook) == PILOT_STATUS_OK) {
            lcd_ok = 1;
        }
    }

    uintptr_t evf_slot = g_appfw_base + APPFW_LV_EVF_VTABLE_UPDATE_OFF;
    uintptr_t evf_expected = g_appfw_base + APPFW_LV_EVF_UPDATE_ORIG_OFF;
    uintptr_t evf_cur = 0u;
    int evf_ok = 0;
    if (g_api->memory_read(evf_slot, &evf_cur, sizeof(evf_cur)) == PILOT_STATUS_OK &&
        evf_cur == evf_expected) {
        g_orig_lv_evf_update = (lv_presenter_update_fn)evf_cur;
        if (patch_got_ptr(evf_slot, evf_cur,
                          (uintptr_t)pilot_lv_evf_update_hook) == PILOT_STATUS_OK) {
            evf_ok = 1;
        }
    }

    char logbuf[128];
    g_lv_ui_available = lcd_ok || evf_ok;
    snprintf(logbuf, sizeof(logbuf),
             "PILOT_LV_UI_HOOK lcd=%d evf=%d mods=%zu",
             lcd_ok, evf_ok, g_lv_expected_count);
    ui_log(logbuf);

    if ((!lcd_ok && !evf_ok) || !g_lv_cfg_boot_toast)
        return;

    if (g_lv_cfg_show_progress) {
        char init_msg[128];
        snprintf(init_msg, sizeof(init_msg),
                 "Pilot Loading (1/%zu)...", g_lv_expected_count);
        lv_set_banner_internal(init_msg, 12000u);
    }

    pthread_t th;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) == 0) {
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &attr, pilot_lv_monitor_thread, NULL) != 0)
            ui_log("PILOT_LV_UI_ERR monitor thread create failed");
        pthread_attr_destroy(&attr);
    }
}
