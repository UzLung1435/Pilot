/* Native 0x2A7 (scr7001_1005) generic option picker integration.
 * Included by pilot_menu_ui.c after ui_log() and helper declarations. */

#define PILOT_OPT_SCREEN_ID        0x2A7u
#define PILOT_OPT_GROUP_ID         0x536u
#define PILOT_OPT_CHOICE_KEY_BASE  0x2000u
#define PILOT_OPT_CHOICES_MAX      16u
#define PILOT_LIST_VAL_KEY_BASE    0x2020u

#define PILOT_RID_OPT_TITLE        0x02FFFF04u
#define PILOT_RID_OPT_CHOICE_BASE  0x02FFFF60u
#define PILOT_RID_LIST_VAL_BASE    0x02FFFF80u

/* ILCE-6700 2.00 appFw.so offsets for 0x2A7 option picker */
#define APPFW_STOCK_OPT_DESC_OFF   0x4BE5F58ull /* group 0x33B On/Off descriptor (0xB0 B) */
#define APPFW_STOCK_OPT_ROW_OFF    0x4BB14F8ull /* group 0x33B row[0] template (0xA0 B) */
#define APPFW_OPT_DESC_SIZE        0xB0ull
#define APPFW_OPT_ROW_SIZE         0xA0ull

#define APPFW_ACCESSOR_VTABLE_OFF  0x4B4DBD0ull /* GOT 0x4E5B430 -> +0x10 has 313 slots */
#define APPFW_ACCESSOR_HEADER_QWS  2u
#define APPFW_ACCESSOR_SLOTS       313u

#define APPFW_GOT_2A7_CONFIRM      0x4F19520ull /* -> 0x29ed258 */
#define APPFW_2A7_GET_FOCUSED_OFF  0x29ECE68ull /* returns const uint32_t * */
#define APPFW_2A7_CLOSE_OFF        0x29ED1D0ull /* closes 0x2A7 back to caller screen */

typedef void *(*accessor_ptr_fn)(void *self, uint32_t key);
typedef uint32_t (*accessor_u32_fn)(void *self, uint32_t key);
typedef void (*ctrl_2a7_close_fn)(void *ctrl);

static uintptr_t g_accessor_vtable_buf[APPFW_ACCESSOR_HEADER_QWS + APPFW_ACCESSOR_SLOTS]
    __attribute__((aligned(16)));
static uintptr_t g_orig_accessor_vtable;
static accessor_ptr_fn g_orig_acc_get_group_desc;   /* slot 2   (+0x010) */
static accessor_ptr_fn g_orig_acc_get_row_desc;     /* slot 4   (+0x020) */
static accessor_u32_fn g_orig_acc_is_row_visible;   /* slot 50  (+0x190) */
static accessor_u32_fn g_orig_acc_get_setting_val;  /* slot 82  (+0x290) */
static accessor_u32_fn g_orig_acc_item_to_group;    /* slot 94  (+0x2f0) */
static accessor_ptr_fn g_orig_acc_get_row_struct;   /* slot 203 (+0x658) */
static accessor_ptr_fn g_orig_acc_get_group_struct; /* slot 206 (+0x670) */
static ctrl_event_fn g_orig_2a7_confirm;

static uint8_t g_stock_opt_desc[APPFW_OPT_DESC_SIZE] __attribute__((aligned(16)));
static uint8_t g_stock_opt_row[APPFW_OPT_ROW_SIZE] __attribute__((aligned(16)));
static uint8_t g_opt_setting_desc[APPFW_OPT_DESC_SIZE] __attribute__((aligned(16)));
static uint8_t g_opt_rows[PILOT_OPT_CHOICES_MAX][APPFW_OPT_ROW_SIZE]
    __attribute__((aligned(16)));
static uint8_t g_list_val_descs[PILOT_LIST_ITEMS_MAX][APPFW_OPT_DESC_SIZE]
    __attribute__((aligned(16)));
static uint8_t g_list_val_rows[PILOT_LIST_ITEMS_MAX][APPFW_OPT_ROW_SIZE]
    __attribute__((aligned(16)));
static const char *g_list_val_strings[PILOT_LIST_ITEMS_MAX];
static void *g_list_val_sprites[PILOT_LIST_ITEMS_MAX];

static int g_pilot_2a7_active;
static size_t g_pilot_2a7_mod_idx;
static size_t g_pilot_2a7_row_idx;
static uint32_t g_pilot_2a7_cur_value;
static uint32_t g_pilot_2a7_choice_count;
static const char *g_opt_title_string = "";
static const char *g_opt_choice_strings[PILOT_OPT_CHOICES_MAX];

static int is_pilot_opt_choice_key(uint32_t key)
{
    return key >= PILOT_OPT_CHOICE_KEY_BASE &&
           key < PILOT_OPT_CHOICE_KEY_BASE + PILOT_OPT_CHOICES_MAX;
}

static int is_pilot_list_val_key(uint32_t key)
{
    return key >= PILOT_LIST_VAL_KEY_BASE &&
           key < PILOT_LIST_VAL_KEY_BASE + PILOT_LIST_ITEMS_MAX;
}

static int is_pilot_list_val_group(uint32_t group_id)
{
    if (!g_in_pilot_293 || !pilot_current_group())
        return 0;
    if (group_id < PILOT_ITEM_ID_BASE ||
        group_id >= PILOT_ITEM_ID_BASE + PILOT_LIST_ITEMS_MAX)
        return 0;
    const char *v = g_list_val_strings[group_id - PILOT_ITEM_ID_BASE];
    return v && v[0] != '\0';
}

static void pilot_bind_list_val_row(size_t row, const char *val_str)
{
    if (row >= PILOT_LIST_ITEMS_MAX)
        return;
    g_list_val_strings[row] = (val_str && val_str[0]) ? val_str : NULL;
    if (!g_list_val_strings[row])
        return;

    memcpy(g_list_val_descs[row], g_stock_opt_desc, APPFW_OPT_DESC_SIZE);
    *(uint32_t *)(g_list_val_descs[row] + 0x00ull) =
        PILOT_RID_LIST_ITEM_BASE + (uint32_t)row;
    *(uint32_t *)(g_list_val_descs[row] + 0x0Cull) = PILOT_RID_EMPTY;
    *(uint32_t *)(g_list_val_descs[row] + 0x20ull) = PILOT_OPT_SCREEN_ID;
    *(uint32_t *)(g_list_val_descs[row] + 0x40ull) = 1u;
    *(uintptr_t *)(g_list_val_descs[row] + 0x48ull) =
        (uintptr_t)g_list_val_rows[row];
    *(uintptr_t *)(g_list_val_descs[row] + 0x60ull) =
        g_appfw_base + APPFW_VISIBILITY_OK_OFF;
    *(uintptr_t *)(g_list_val_descs[row] + 0x68ull) =
        g_appfw_base + APPFW_VISIBILITY_OK_OFF;
    *(uintptr_t *)(g_list_val_descs[row] + 0x70ull) = 0ull;
    *(uintptr_t *)(g_list_val_descs[row] + 0x78ull) = 0ull;
    *(uintptr_t *)(g_list_val_descs[row] + 0x98ull) = 0ull;
    *(uintptr_t *)(g_list_val_descs[row] + 0xA0ull) = 0ull;

    memcpy(g_list_val_rows[row], g_stock_opt_row, APPFW_OPT_ROW_SIZE);
    *(uint32_t *)(g_list_val_rows[row] + 0x00ull) =
        PILOT_LIST_VAL_KEY_BASE + (uint32_t)row;
    *(uint32_t *)(g_list_val_rows[row] + 0x04ull) =
        PILOT_RID_LIST_VAL_BASE + (uint32_t)row;
    *(uint32_t *)(g_list_val_rows[row] + 0x0Cull) = PILOT_RID_EMPTY;
    *(uint32_t *)(g_list_val_rows[row] + 0x48ull) = 0u;
    *(uintptr_t *)(g_list_val_rows[row] + 0x60ull) = 0ull;
    *(uintptr_t *)(g_list_val_rows[row] + 0x80ull) =
        g_appfw_base + APPFW_VISIBILITY_OK_OFF;
    *(uint32_t *)(g_list_val_rows[row] + 0x94ull) = 0u;
    *(uintptr_t *)(g_list_val_rows[row] + 0x98ull) = 0ull;
}

static void *pilot_acc_get_group_desc_hook(void *self, uint32_t group_id)
{
    if (group_id == PILOT_OPT_GROUP_ID)
        return g_opt_setting_desc;
    if (is_pilot_list_val_group(group_id))
        return g_list_val_descs[group_id - PILOT_ITEM_ID_BASE];
    return g_orig_acc_get_group_desc(self, group_id);
}

static void *pilot_acc_get_row_desc_hook(void *self, uint32_t item_key)
{
    if (is_pilot_opt_choice_key(item_key))
        return g_opt_rows[item_key - PILOT_OPT_CHOICE_KEY_BASE];
    if (is_pilot_list_val_key(item_key))
        return g_list_val_rows[item_key - PILOT_LIST_VAL_KEY_BASE];
    return g_orig_acc_get_row_desc(self, item_key);
}

static uint32_t pilot_acc_is_row_visible_hook(void *self, uint32_t item_key)
{
    if (is_pilot_opt_choice_key(item_key))
        return (item_key - PILOT_OPT_CHOICE_KEY_BASE) < g_pilot_2a7_choice_count
                   ? 1u
                   : 0u;
    if (is_pilot_list_val_key(item_key)) {
        const char *v = g_list_val_strings[item_key - PILOT_LIST_VAL_KEY_BASE];
        return (v && v[0]) ? 1u : 0u;
    }
    return g_orig_acc_is_row_visible(self, item_key);
}

static uint32_t pilot_acc_get_setting_val_hook(void *self, uint32_t group_id)
{
    if (group_id == PILOT_OPT_GROUP_ID)
        return g_pilot_2a7_cur_value;
    if (is_pilot_list_val_group(group_id))
        return 0u;
    return g_orig_acc_get_setting_val(self, group_id);
}

static uint32_t pilot_acc_item_to_group_hook(void *self, uint32_t item_key)
{
    if (is_pilot_opt_choice_key(item_key))
        return PILOT_OPT_GROUP_ID;
    if (is_pilot_list_val_key(item_key))
        return PILOT_ITEM_ID_BASE + (item_key - PILOT_LIST_VAL_KEY_BASE);
    return g_orig_acc_item_to_group(self, item_key);
}

static void *pilot_acc_get_row_struct_hook(void *self, uint32_t item_key)
{
    if (is_pilot_opt_choice_key(item_key))
        return g_opt_rows[item_key - PILOT_OPT_CHOICE_KEY_BASE];
    if (is_pilot_list_val_key(item_key))
        return g_list_val_rows[item_key - PILOT_LIST_VAL_KEY_BASE];
    return g_orig_acc_get_row_struct(self, item_key);
}

static void *pilot_acc_get_group_struct_hook(void *self, uint32_t group_id)
{
    /* vtable[+0x4c0] (slot 152, is_nonsetting_group) simply checks
     * (vtable[+0x670](group_id) != NULL). Returning NULL for setting-backed
     * virtual groups (0x2A7 picker group and 0x293 rows with right-aligned
     * values) makes 0x293 take the setting path (0x22cd1b8) to render the
     * right-aligned value string instead of the nonsetting '>' arrow, while
     * rows without values fall through to g_item_nonsetting_descs[row] ('>'). */
    if (group_id == PILOT_OPT_GROUP_ID || is_pilot_list_val_group(group_id))
        return NULL;
    return g_orig_acc_get_group_struct(self, group_id);
}

static void pilot_2a7_prepare_choices(const char *title,
                                      const char *const *choices,
                                      size_t count,
                                      uint32_t current_index)
{
    if (count > PILOT_OPT_CHOICES_MAX)
        count = PILOT_OPT_CHOICES_MAX;
    if (count == 0u)
        return;
    if (current_index >= count)
        current_index = 0u;

    g_opt_title_string = title ? title : "";
    g_pilot_2a7_choice_count = (uint32_t)count;
    g_pilot_2a7_cur_value = current_index;

    memcpy(g_opt_setting_desc, g_stock_opt_desc, APPFW_OPT_DESC_SIZE);
    *(uint32_t *)(g_opt_setting_desc + 0x00ull) = PILOT_RID_OPT_TITLE;
    *(uint32_t *)(g_opt_setting_desc + 0x0Cull) = PILOT_RID_EMPTY;
    *(uint32_t *)(g_opt_setting_desc + 0x20ull) = PILOT_OPT_SCREEN_ID;
    *(uint32_t *)(g_opt_setting_desc + 0x40ull) = (uint32_t)count;
    *(uintptr_t *)(g_opt_setting_desc + 0x48ull) = (uintptr_t)g_opt_rows;
    *(uintptr_t *)(g_opt_setting_desc + 0x60ull) =
        g_appfw_base + APPFW_VISIBILITY_OK_OFF;
    /* 0x22cdc44 checks [desc + 0x68] != 0 before calling vtable[+0x290];
     * keep a non-NULL stub here so get_cur_item() queries our vtable hook
     * instead of returning 0 (which highlights empty rows). */
    *(uintptr_t *)(g_opt_setting_desc + 0x68ull) =
        g_appfw_base + APPFW_VISIBILITY_OK_OFF;
    *(uintptr_t *)(g_opt_setting_desc + 0x70ull) = 0ull;
    *(uintptr_t *)(g_opt_setting_desc + 0x78ull) = 0ull;
    *(uintptr_t *)(g_opt_setting_desc + 0x98ull) = 0ull;
    *(uintptr_t *)(g_opt_setting_desc + 0xA0ull) = 0ull;

    for (size_t i = 0; i < count; ++i) {
        g_opt_choice_strings[i] = choices[i] ? choices[i] : "";
        memcpy(g_opt_rows[i], g_stock_opt_row, APPFW_OPT_ROW_SIZE);
        *(uint32_t *)(g_opt_rows[i] + 0x00ull) =
            PILOT_OPT_CHOICE_KEY_BASE + (uint32_t)i;
        *(uint32_t *)(g_opt_rows[i] + 0x04ull) =
            PILOT_RID_OPT_CHOICE_BASE + (uint32_t)i;
        *(uint32_t *)(g_opt_rows[i] + 0x0Cull) = PILOT_RID_EMPTY;
        *(uint32_t *)(g_opt_rows[i] + 0x48ull) = (uint32_t)i;
        *(uintptr_t *)(g_opt_rows[i] + 0x60ull) = 0ull;
        *(uintptr_t *)(g_opt_rows[i] + 0x80ull) =
            g_appfw_base + APPFW_VISIBILITY_OK_OFF;
        *(uint32_t *)(g_opt_rows[i] + 0x94ull) = 0u;
        *(uintptr_t *)(g_opt_rows[i] + 0x98ull) = 0ull;
    }
}

static void pilot_2a7_apply_selection(uint32_t choice_index);

static uint32_t pilot_2a7_confirm_hook(void *ctrl, void *ev)
{
    if (g_pilot_2a7_active && ctrl && g_appfw_base) {
        const uint32_t *cur = pilot_menu_id_ptr(APPFW_GET_CUR_ITEM_OFF);
        if (cur && *cur == PILOT_OPT_GROUP_ID) {
            typedef const uint32_t *(*get_id_ptr_fn)(void);
            get_id_ptr_fn get_focused =
                (get_id_ptr_fn)(g_appfw_base + APPFW_2A7_GET_FOCUSED_OFF);
            const uint32_t *focused = get_focused ? get_focused() : NULL;
            uint32_t key = focused ? *focused : 0u;
            if (is_pilot_opt_choice_key(key)) {
                uint32_t choice_idx = key - PILOT_OPT_CHOICE_KEY_BASE;
                pilot_2a7_apply_selection(choice_idx);
            }
            g_pilot_2a7_active = 0;
            uint32_t *item_p = pilot_menu_id_ptr(APPFW_GET_CUR_ITEM_OFF);
            if (item_p)
                *item_p = PILOT_ITEM_ID_BASE + (uint32_t)g_pilot_2a7_row_idx;
            ctrl_2a7_close_fn close_fn =
                (ctrl_2a7_close_fn)(g_appfw_base + APPFW_2A7_CLOSE_OFF);
            if (close_fn)
                close_fn(ctrl);
            return 1u;
        }
    }
    return g_orig_2a7_confirm ? g_orig_2a7_confirm(ctrl, ev) : 1u;
}

static int install_pilot_option_2a7(void)
{
    uintptr_t got_acc = 0, got_2a7_confirm = 0;
    if (g_api->memory_read(g_appfw_base + APPFW_GOT_ISNAV_TABLE, &got_acc,
                           sizeof(got_acc)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_2A7_CONFIRM,
                           &got_2a7_confirm,
                           sizeof(got_2a7_confirm)) != PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR 2a7 got read failed");
        return 0;
    }
    if (got_acc != g_appfw_base + APPFW_ACCESSOR_VTABLE_OFF) {
        ui_log("PILOT_UI_ERR unexpected accessor vtable GOT");
        return 0;
    }
    if (read_appfw_bytes(got_acc, g_accessor_vtable_buf,
                         sizeof(g_accessor_vtable_buf)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_STOCK_OPT_DESC_OFF,
                           g_stock_opt_desc,
                           APPFW_OPT_DESC_SIZE) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_STOCK_OPT_ROW_OFF,
                           g_stock_opt_row,
                           APPFW_OPT_ROW_SIZE) != PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR 2a7 template read failed");
        return 0;
    }

    uintptr_t *vt = g_accessor_vtable_buf + APPFW_ACCESSOR_HEADER_QWS;
    g_orig_accessor_vtable = got_acc;
    g_orig_acc_get_group_desc   = (accessor_ptr_fn)vt[2];
    g_orig_acc_get_row_desc     = (accessor_ptr_fn)vt[4];
    g_orig_acc_is_row_visible   = (accessor_u32_fn)vt[50];
    g_orig_acc_get_setting_val  = (accessor_u32_fn)vt[82];
    g_orig_acc_item_to_group    = (accessor_u32_fn)vt[94];
    g_orig_acc_get_row_struct   = (accessor_ptr_fn)vt[203];
    g_orig_acc_get_group_struct = (accessor_ptr_fn)vt[206];

    vt[2]   = (uintptr_t)pilot_acc_get_group_desc_hook;
    vt[4]   = (uintptr_t)pilot_acc_get_row_desc_hook;
    vt[50]  = (uintptr_t)pilot_acc_is_row_visible_hook;
    vt[82]  = (uintptr_t)pilot_acc_get_setting_val_hook;
    vt[94]  = (uintptr_t)pilot_acc_item_to_group_hook;
    vt[203] = (uintptr_t)pilot_acc_get_row_struct_hook;
    vt[206] = (uintptr_t)pilot_acc_get_group_struct_hook;

    g_orig_2a7_confirm = (ctrl_event_fn)got_2a7_confirm;

    if (patch_got_ptr(g_appfw_base + APPFW_GOT_ISNAV_TABLE, got_acc,
                      (uintptr_t)g_accessor_vtable_buf) != PILOT_STATUS_OK ||
        patch_got_ptr(g_appfw_base + APPFW_GOT_2A7_CONFIRM, got_2a7_confirm,
                      (uintptr_t)pilot_2a7_confirm_hook) != PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR 2a7 GOT patch failed");
        return 0;
    }

    ui_log("PILOT_UI_2A7_READY group=0x536 screen=0x2a7");
    return 1;
}
