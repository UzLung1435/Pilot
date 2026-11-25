/* Native 0x260 (scr7187_0004) Processing Dialog integration.
 * Included by pilot_menu_ui.c after pilot_ui_option_2a7.inc. */

#define PILOT_BUSY_SCREEN_ID         0x260u
#define PILOT_BUSY_MSG_RID           0x0200606Cu
#define APPFW_CTRL_FACTORY_260_OFF   0x250E390ull
#define APPFW_260_HANDLE_OFF         0x11B20ull /* 0x11000 + 0xB20 (0x250dffc + 0x250e338) */
#define APPFW_260_MGR_OFF            0x11B28ull /* 0x11000 + 0xB28 (0x250dffc + 0x250e340) */

static pthread_mutex_t g_busy_lock = PTHREAD_MUTEX_INITIALIZER;
static ctrl_factory_fn g_orig_ctrl_factory_260;
static void *g_active_260_ctrl;
static void *g_active_260_mgr;
static int g_pilot_busy_pending;
static int g_pilot_busy_active;
static int g_pilot_busy_open;
static char g_pilot_busy_text[160] = "Processing...";

static void pilot_busy_reset_state(void)
{
    pthread_mutex_lock(&g_busy_lock);
    g_pilot_busy_pending = 0;
    g_pilot_busy_active = 0;
    g_pilot_busy_open = 0;
    g_active_260_ctrl = NULL;
    g_active_260_mgr = NULL;
    pthread_mutex_unlock(&g_busy_lock);
}

static int pilot_busy_is_active(void)
{
    pthread_mutex_lock(&g_busy_lock);
    int active = g_pilot_busy_active;
    pthread_mutex_unlock(&g_busy_lock);
    return active;
}

static void pilot_busy_copy_text(char *dst, size_t dst_size)
{
    if (!dst || !dst_size)
        return;
    pthread_mutex_lock(&g_busy_lock);
    snprintf(dst, dst_size, "%s", g_pilot_busy_text);
    pthread_mutex_unlock(&g_busy_lock);
}

static void pilot_260_close_ctrl(void *ctrl, void *expected_mgr)
{
    if (!ctrl)
        return;
    void *mgr = *(void **)((uint8_t *)ctrl + APPFW_260_MGR_OFF);
    void *handle = (uint8_t *)ctrl + APPFW_260_HANDLE_OFF;
    if (!mgr || (expected_mgr && mgr != expected_mgr)) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "PILOT_UI_BUSY_260_MGR_MISMATCH ctrl=%p mgr=%p exp=%p",
                 ctrl, mgr, expected_mgr);
        ui_log(msg);
        return;
    }
    uintptr_t *vt = *(uintptr_t **)mgr;
    if (!vt || !vt[4])
        return;
    typedef void (*mgr_close_fn)(void *mgr_obj, void *handle_ptr);
    ((mgr_close_fn)vt[4])(mgr, handle);
}

static void *pilot_260_ctrl_factory_hook(void *buf, void *arg1, void *arg2,
                                         void *arg3, uint32_t arg4)
{
    void *ctrl = g_orig_ctrl_factory_260(buf, arg1, arg2, arg3, arg4);
    if (!ctrl)
        return NULL;

    int record = 0;
    pthread_mutex_lock(&g_busy_lock);
    if (g_pilot_busy_active) {
        g_active_260_ctrl = ctrl;
        g_active_260_mgr = arg1;
        g_pilot_busy_open = 1;
        record = 1;
    }
    pthread_mutex_unlock(&g_busy_lock);

    if (record) {
        char msg[128];
        snprintf(msg, sizeof(msg), "PILOT_UI_BUSY_260_OPEN ctrl=%p mgr=%p",
                 ctrl, arg1);
        ui_log(msg);
    }
    return ctrl;
}

static void pilot_busy_on_293_enter(void *ctrl_293, uint32_t source_id)
{
    if (source_id == PILOT_BUSY_SCREEN_ID) {
        pthread_mutex_lock(&g_busy_lock);
        g_pilot_busy_open = 0;
        g_active_260_ctrl = NULL;
        g_active_260_mgr = NULL;
        if (!g_pilot_busy_pending)
            g_pilot_busy_active = 0;
        pthread_mutex_unlock(&g_busy_lock);
    }

    if (!ctrl_293 || !g_appfw_base)
        return;

    int should_open = 0;
    pthread_mutex_lock(&g_busy_lock);
    if (g_pilot_busy_pending && !g_pilot_busy_open) {
        g_pilot_busy_active = 1;
        should_open = 1;
    }
    pthread_mutex_unlock(&g_busy_lock);

    if (!should_open)
        return;

    generic_open_fn go =
        (generic_open_fn)(g_appfw_base + APPFW_293_GENERIC_OPEN_OFF);
    if (!go)
        return;

    /* Preserve the current 0x293 page (e.g. module options menu) and focused
     * row so closing 0x260 returns here instead of resetting to Pilot root. */
    const uint32_t *cur_item = pilot_menu_id_ptr(APPFW_GET_CUR_ITEM_OFF);
    g_return_list_page = g_active_page;
    g_return_list_item = (cur_item && *cur_item >= PILOT_ITEM_ID_BASE)
                             ? *cur_item
                             : PILOT_ITEM_ID_BASE;
    g_return_list_pending = 1;

    uint32_t a[4] = {PILOT_BUSY_SCREEN_ID, 0u, 0u, 0u};
    uint32_t b[4] = {SCREEN_NONE, 0u, 0u, 0u};
    uint32_t c[4] = {SCREEN_NONE, 0u, 0u, 0u};
    ui_log("PILOT_UI_BUSY_260_REQUEST");
    go(ctrl_293, a, b, c);

    /* If pilot_ui_hide_busy_v1() raced and completed while generic_open was
     * constructing 0x260, close it immediately now that it is on the stack. */
    void *close_now = NULL;
    void *close_mgr = NULL;
    pthread_mutex_lock(&g_busy_lock);
    if (!g_pilot_busy_pending && g_pilot_busy_open && g_active_260_ctrl) {
        close_now = g_active_260_ctrl;
        close_mgr = g_active_260_mgr;
        g_active_260_ctrl = NULL;
        g_active_260_mgr = NULL;
        g_pilot_busy_open = 0;
        g_pilot_busy_active = 0;
    }
    pthread_mutex_unlock(&g_busy_lock);

    if (close_now) {
        ui_log("PILOT_UI_BUSY_260_CLOSE_DEFERRED");
        pilot_260_close_ctrl(close_now, close_mgr);
    }
}

__attribute__((visibility("default")))
PilotStatus pilot_ui_show_busy_v1(const char *text)
{
    if (!text || !text[0])
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (!g_installed || !g_appfw_base)
        return PILOT_STATUS_DISABLED;

    void *open_from = NULL;
    pthread_mutex_lock(&g_busy_lock);
    snprintf(g_pilot_busy_text, sizeof(g_pilot_busy_text), "%s", text);
    g_pilot_busy_pending = 1;
    g_pilot_busy_active = 1;
    if (g_in_pilot_293 && g_active_293_ctrl && !g_pilot_2a7_active &&
        !g_pilot_detail_active && !g_pilot_busy_open) {
        open_from = g_active_293_ctrl;
    }
    pthread_mutex_unlock(&g_busy_lock);

    if (open_from)
        pilot_busy_on_293_enter(open_from, SCREEN_NONE);

    return PILOT_STATUS_OK;
}

__attribute__((visibility("default")))
PilotStatus pilot_ui_hide_busy_v1(void)
{
    if (!g_installed || !g_appfw_base)
        return PILOT_STATUS_DISABLED;

    void *ctrl = NULL;
    void *mgr = NULL;
    pthread_mutex_lock(&g_busy_lock);
    g_pilot_busy_pending = 0;
    if (g_pilot_busy_open && g_active_260_ctrl) {
        ctrl = g_active_260_ctrl;
        mgr = g_active_260_mgr;
        g_active_260_ctrl = NULL;
        g_active_260_mgr = NULL;
        g_pilot_busy_open = 0;
        g_pilot_busy_active = 0;
    } else if (!g_pilot_busy_open) {
        g_pilot_busy_active = 0;
    }
    pthread_mutex_unlock(&g_busy_lock);

    if (ctrl) {
        ui_log("PILOT_UI_BUSY_260_CLOSE");
        pilot_260_close_ctrl(ctrl, mgr);
    }
    return PILOT_STATUS_OK;
}
