/* Native controller lifecycle integration. Included after ui_log(). */
#define APPFW_293_ON_ENTER_OFF 0x29EB2ECull
#define APPFW_CURSOR_FOCUS_ITEM_OFF 0x2185CE0ull
#define APPFW_CURSOR_GET_INDEX_OFF 0x2183778ull

typedef void (*pilot_ctrl_enter_fn)(void *ctrl, const uint32_t *source);
typedef void (*pilot_cursor_focus_fn)(void *cursor, uint32_t item);
typedef uint32_t (*pilot_cursor_index_fn)(void *cursor);

static uintptr_t g_ctrl_vtable_293[VIEW_VTABLE_SLOTS];
static pilot_ctrl_enter_fn g_orig_ctrl_enter_293;
static const PilotUiPage *g_return_list_page;
static uint32_t g_return_list_item;
static int g_return_list_pending;

static uint32_t *pilot_menu_id_ptr(uintptr_t offset)
{
    typedef uint32_t *(*get_id_fn)(void);
    return ((get_id_fn)(g_appfw_base + offset))();
}

static int pilot_current_group(void)
{
    const uint32_t *group = pilot_menu_id_ptr(APPFW_GET_CUR_GROUP_OFF);
    return group && *group == PILOT_GROUP_ID;
}

static void pilot_log_list_state(const char *phase, void *ctrl, uint32_t source)
{
    const uint32_t *row = pilot_menu_id_ptr(APPFW_GET_SEL_ROW_OFF);
    const uint32_t *item = pilot_menu_id_ptr(APPFW_GET_CUR_ITEM_OFF);
    void *cursor = (uint8_t *)ctrl + APPFW_293_CURSOR_OBJ_OFF;
    uint32_t index = ((pilot_cursor_index_fn)(g_appfw_base +
                       APPFW_CURSOR_GET_INDEX_OFF))(cursor);
    char message[224];
    snprintf(message, sizeof(message),
             "PILOT_UI_NAV %s ctrl=%p source=0x%x row=0x%x item=0x%x cursor=%u saved=0x%x pending=%d",
             phase, ctrl, source, row ? *row : 0u, item ? *item : 0u,
             index, g_return_list_item, g_return_list_pending);
    ui_log(message);
}

static void pilot_clear_list_return(void);
static void pilot_busy_on_293_enter(void *ctrl_293, uint32_t source_id);

static void pilot_293_ctrl_enter_hook(void *ctrl, const uint32_t *source)
{
    if (!pilot_current_group()) {
        g_orig_ctrl_enter_293(ctrl, source);
        return;
    }

    uint32_t source_id = source ? *source : SCREEN_NONE;
    if (g_pending_list_page) {
        pilot_apply_pending_list_page();
    } else if (source_id == 0x292u && g_active_page != &g_pilot_root_page) {
        /* Fresh entry from main menu (0x292): always reset to Pilot root. */
        pilot_clear_list_return();
        g_pending_list_page = &g_pilot_root_page;
        pilot_apply_pending_list_page();
    }
    int restore = g_return_list_pending && g_return_list_page == g_active_page;
    pilot_log_list_state("enter_before", ctrl, source_id);
    if (restore) {
        /* Native on_enter consumes sel_row before rebuilding its cursor state. */
        uint32_t *row = pilot_menu_id_ptr(APPFW_GET_SEL_ROW_OFF);
        if (row)
            *row = g_return_list_item;
    }

    g_orig_ctrl_enter_293(ctrl, source);
    if (restore) {
        /* This updates the cursor AND emits the firmware's parameter changes.
         * Writing cur_item alone leaves the cursor and observers out of sync. */
        void *cursor = (uint8_t *)ctrl + APPFW_293_CURSOR_OBJ_OFF;
        ((pilot_cursor_focus_fn)(g_appfw_base + APPFW_CURSOR_FOCUS_ITEM_OFF))(
            cursor, g_return_list_item);
        uint32_t *item = pilot_menu_id_ptr(APPFW_GET_CUR_ITEM_OFF);
        if (!item || *item != g_return_list_item)
            ui_log("PILOT_UI_ERR list cursor restore did not select saved item");
        g_return_list_pending = 0;
    }
    g_pilot_detail_active = 0;
    pilot_log_list_state("enter_after", ctrl, source_id);
    pilot_busy_on_293_enter(ctrl, source_id);
}

static int pilot_attach_list_lifecycle(void *ctrl)
{
    if (!ctrl)
        return 0;
    uintptr_t *table = *(uintptr_t **)ctrl;
    if (!table || table[2] != g_appfw_base + APPFW_293_ON_ENTER_OFF) {
        ui_log("PILOT_UI_ERR unexpected list controller on_enter");
        return 0;
    }
    if (!g_orig_ctrl_enter_293) {
        memcpy(g_ctrl_vtable_293, table, sizeof(g_ctrl_vtable_293));
        g_orig_ctrl_enter_293 = (pilot_ctrl_enter_fn)table[2];
        g_ctrl_vtable_293[2] = (uintptr_t)pilot_293_ctrl_enter_hook;
    }
    *(uintptr_t **)ctrl = g_ctrl_vtable_293;
    return 1;
}

static void pilot_save_list_return(void *ctrl, uint32_t item)
{
    g_return_list_page = g_active_page;
    g_return_list_item = item;
    g_return_list_pending = 1;
    pilot_log_list_state("open_detail", ctrl, PILOT_DETAIL_SCREEN_ID);
}

static void pilot_clear_list_return(void)
{
    g_return_list_page = NULL;
    g_return_list_item = 0;
    g_return_list_pending = 0;
}

/* The native focus tree stores widget IDs in node+0x20. Its root is at
 * cursor+0x18, count at cursor+0x30, children at node+0x10/+0x18.
 * Bound the walk by the fixed 13-row detail table. */
static uint32_t pilot_detail_focus_mask(const void *ctrl)
{
    const uint8_t *cursor = (const uint8_t *)ctrl + APPFW_293_CURSOR_OBJ_OFF;
    const uint8_t *stack[APPFW_LAYOUT_1C3_ITEMS];
    size_t depth = 0, visited = 0;
    uint32_t mask = 0;
    const uint8_t *node = *(const uint8_t *const *)(cursor + 0x18u);
    if (node)
        stack[depth++] = node;
    while (depth && visited++ < APPFW_LAYOUT_1C3_ITEMS) {
        node = stack[--depth];
        uint32_t id = *(const uint32_t *)(node + 0x20u);
        if (id < 32u)
            mask |= 1u << id;
        const uint8_t *left = *(const uint8_t *const *)(node + 0x10u);
        const uint8_t *right = *(const uint8_t *const *)(node + 0x18u);
        if (left && depth < APPFW_LAYOUT_1C3_ITEMS)
            stack[depth++] = left;
        if (right && depth < APPFW_LAYOUT_1C3_ITEMS)
            stack[depth++] = right;
    }
    return mask;
}

static void pilot_log_detail_focus(void *ctrl, int expect_toggle)
{
    if (!ctrl)
        return;
    const uint8_t *cursor = (const uint8_t *)ctrl + APPFW_293_CURSOR_OBJ_OFF;
    uint64_t count = *(const uint64_t *)(cursor + 0x30u);
    uint32_t mask = pilot_detail_focus_mask(ctrl);
    uint64_t want_count = expect_toggle ? 2u : 1u;
    uint32_t want_mask = expect_toggle ? ((1u << 7u) | (1u << 8u)) : (1u << 8u);
    char message[128];
    snprintf(message, sizeof(message),
             "PILOT_UI_FOCUS ctrl=%p count=%llu mask=0x%x update=%u ok=%u expect_toggle=%d",
             ctrl, (unsigned long long)count, mask,
             (mask >> 7u) & 1u, (mask >> 8u) & 1u, expect_toggle);
    ui_log(message);
    if (count != want_count || mask != want_mask)
        ui_log(expect_toggle
                   ? "PILOT_UI_ERR detail focus tree must contain Toggle and OK"
                   : "PILOT_UI_ERR detail focus tree must contain only OK");
}
