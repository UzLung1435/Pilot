#include "../pilot_menu_ui.c"
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>

static uint32_t test_group = PILOT_GROUP_ID;
static uint32_t test_row, test_item, test_index;
static unsigned test_focus_commits, test_enters, test_detail_factories;
static int test_patch_failure;
static uint8_t test_ctrl[0x1D000] __attribute__((aligned(16)));
static uintptr_t test_vtable[VIEW_VTABLE_SLOTS];
static uint8_t test_ok_node[0x38] __attribute__((aligned(16)));

static uint32_t *get_group(void) { return &test_group; }
static uint32_t *get_row(void) { return &test_row; }
static uint32_t *get_item(void) { return &test_item; }
static uint32_t get_index(void *cursor)
{
    assert(cursor == test_ctrl + APPFW_293_CURSOR_OBJ_OFF);
    return test_index;
}
static void focus_item(void *cursor, uint32_t item)
{
    assert(cursor == test_ctrl + APPFW_293_CURSOR_OBJ_OFF);
    assert(item >= PILOT_ITEM_ID_BASE && item < PILOT_ITEM_ID_BASE + 4u);
    test_index = 12u + item - PILOT_ITEM_ID_BASE;
    test_item = item;
    ++test_focus_commits;
}
static void native_enter(void *ctrl, const uint32_t *source)
{
    assert(ctrl == test_ctrl && source);
    ++test_enters;
    /* Reproduce native reactivation replacing the global item before view
     * creation; callers must restore through the cursor after this call. */
    test_item = PILOT_ITEM_ID_BASE;
    test_index = 12;
}

static void install_function(uintptr_t offset, void (*function)(void))
{
    uint8_t *destination = (uint8_t *)(g_appfw_base + offset);
    uintptr_t address = (uintptr_t)function;
#if defined(__x86_64__)
    destination[0] = 0x48; destination[1] = 0xB8; /* movabs rax, address */
    memcpy(destination + 2, &address, 8);
    destination[10] = 0xFF; destination[11] = 0xE0; /* jmp rax */
#else
#error Run this host harness with an x86_64 compiler.
#endif
}

static PilotStatus test_log(const char *message)
{
    puts(message);
    return PILOT_STATUS_OK;
}
static PilotStatus test_patch(uintptr_t address, const void *expected,
                              const void *replacement, size_t size)
{
    if (test_patch_failure)
        return PILOT_STATUS_CONFLICT;
    assert(memcmp((void *)address, expected, size) == 0);
    memcpy((void *)address, replacement, size);
    return PILOT_STATUS_OK;
}
static unsigned test_orig_1c3_confirms;
static unsigned test_1c3_view_updates;
static char test_last_toggle_str[64];
static char test_last_action_str[64];
static char test_last_status_str[160];
static char test_last_slot1_str[512];
static int test_toggle_sprite_dummy;
static int test_action_sprite_dummy;
static int test_status_sprite_dummy;
static int test_slot1_sprite_dummy;
static uint8_t test_1c3_view[0x980];
static char test_written_path[128];
static char test_written_json[2048];
static size_t test_written_len;
static int test_fsys_fail_write;
static int test_fsys_flushed;
static int test_fsys_handle_dummy;

__attribute__((visibility("default")))
int Fsys_Open(const char *path, void **handle_out, int flags)
{
    if (test_fsys_fail_write || flags != 0xE)
        return -1;
    snprintf(test_written_path, sizeof(test_written_path), "%s", path ? path : "");
    test_written_len = 0;
    test_written_json[0] = '\0';
    test_fsys_flushed = 0;
    *handle_out = &test_fsys_handle_dummy;
    return 0;
}

__attribute__((visibility("default")))
int Fsys_Write(void *handle, const void *buf, size_t len)
{
    if (handle != &test_fsys_handle_dummy || test_written_len + len >= sizeof(test_written_json))
        return -1;
    memcpy(test_written_json + test_written_len, buf, len);
    test_written_len += len;
    test_written_json[test_written_len] = '\0';
    return (int)len;
}

__attribute__((visibility("default")))
int Fsys_Flush(void *handle)
{
    if (handle != &test_fsys_handle_dummy)
        return -1;
    test_fsys_flushed = 1;
    return 0;
}

__attribute__((visibility("default")))
int Fsys_Close(void *handle)
{
    return (handle == &test_fsys_handle_dummy) ? 0 : -1;
}

static uint32_t fake_orig_1c3_confirm(void *ctrl, void *ev)
{
    (void)ctrl; (void)ev;
    ++test_orig_1c3_confirms;
    return 1u;
}
static void fake_orig_1c3_view_update(void *view, uint32_t flag)
{
    (void)view; (void)flag;
    ++test_1c3_view_updates;
}
static void fake_set_string_cstr(void *sprite, const char *str)
{
    if (sprite == &test_toggle_sprite_dummy)
        snprintf(test_last_toggle_str, sizeof(test_last_toggle_str), "%s", str ? str : "");
    else if (sprite == &test_action_sprite_dummy)
        snprintf(test_last_action_str, sizeof(test_last_action_str), "%s", str ? str : "");
    else if (sprite == &test_status_sprite_dummy)
        snprintf(test_last_status_str, sizeof(test_last_status_str), "%s", str ? str : "");
    else if (sprite == &test_slot1_sprite_dummy)
        snprintf(test_last_slot1_str, sizeof(test_last_slot1_str), "%s", str ? str : "");
}

static unsigned test_opt_callbacks;
static void test_opt_on_change(PilotUiOption *opt, void *user)
{
    (void)opt;
    (void)user;
    ++test_opt_callbacks;
}

static void *detail_factory(void *buf, void *a1, void *a2, void *a3, uint32_t a4)
{
    (void)buf; (void)a1; (void)a2; (void)a3; (void)a4;
    ++test_detail_factories;
    uintptr_t table = *(uintptr_t *)(g_appfw_base + APPFW_GOT_CTRL_ITEMS_1C3);
    assert(table == (uintptr_t)g_ctrl_items_1c3);
    assert(*(uintptr_t *)(table + 7u * APPFW_CTRL_ITEM_SIZE) ==
           *(uintptr_t *)g_ctrl_items_1c3_stock);
    return test_ctrl;
}

int main(void)
{
    void *mapping = mmap(NULL, 0x5000000, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    g_appfw_base = (uintptr_t)mapping;
    install_function(APPFW_GET_CUR_GROUP_OFF, (void (*)(void))get_group);
    install_function(APPFW_GET_SEL_ROW_OFF, (void (*)(void))get_row);
    install_function(APPFW_GET_CUR_ITEM_OFF, (void (*)(void))get_item);
    install_function(APPFW_CURSOR_GET_INDEX_OFF, (void (*)(void))get_index);
    install_function(APPFW_CURSOR_FOCUS_ITEM_OFF, (void (*)(void))focus_item);
    install_function(APPFW_293_ON_ENTER_OFF, (void (*)(void))native_enter);
    static const PilotLogApi log_api = {.print = test_log};
    static const PilotApi api = {.log = &log_api, .memory_patch = test_patch};
    g_api = &api;
    test_vtable[2] = g_appfw_base + APPFW_293_ON_ENTER_OFF;
    *(uintptr_t **)test_ctrl = test_vtable;
    assert(pilot_attach_list_lifecycle(test_ctrl));
    pilot_ctrl_enter_fn enter = (pilot_ctrl_enter_fn)(*(uintptr_t **)test_ctrl)[2];
    uint32_t source = PILOT_DETAIL_SCREEN_ID;

    /* Repeated returns through the SAME controller: no factory is called. */
    for (unsigned repeat = 0; repeat < 5; ++repeat) {
        test_row = test_item = PILOT_ITEM_ID_BASE + 2u;
        test_index = 14;
        pilot_save_list_return(test_ctrl, test_item);
        test_row = test_item = 8u; /* detail page changes shared parameters */
        enter(test_ctrl, &source);
        assert(test_row == PILOT_ITEM_ID_BASE + 2u);
        assert(test_item == PILOT_ITEM_ID_BASE + 2u);
        assert(test_index == 14u && !g_return_list_pending);
    }
    assert(test_focus_commits == 5u && test_enters == 5u);

    /* Recreated controller also receives the lifecycle hook. */
    pilot_save_list_return(test_ctrl, PILOT_ITEM_ID_BASE + 1u);
    *(uintptr_t **)test_ctrl = test_vtable;
    assert(pilot_attach_list_lifecycle(test_ctrl));
    enter(test_ctrl, &source);
    assert(test_item == PILOT_ITEM_ID_BASE + 1u && test_index == 13u);

    /* Foreign groups and ordinary list entry retain native behavior. */
    test_group = 0x3BC;
    enter(test_ctrl, &source);
    assert(test_focus_commits == 6u);
    test_group = PILOT_GROUP_ID;
    pilot_clear_list_return();
    source = 0x292;
    enter(test_ctrl, &source);
    assert(test_focus_commits == 6u && test_index == 12u);

    /* Validate the live native tree walker and reject silent patch failure. */
    *(uintptr_t *)g_layout_1c3_stock = 0x8344360u;
    *(uintptr_t *)(g_layout_1c3_stock + 7u * APPFW_LAYOUT_ITEM_SIZE) = 0x8343DB0u;
    *(uintptr_t *)(g_layout_1c3_stock + 12u * APPFW_LAYOUT_ITEM_SIZE) = 0x8343B90u;
    *(uintptr_t *)g_ctrl_items_1c3_stock = 0x8344360u;
    *(uintptr_t *)(g_ctrl_items_1c3_stock + 7u * APPFW_CTRL_ITEM_SIZE) = 0x8343DB0u;
    bind_detail_page_to_layout(&g_pilot_version_page);
    /* Display types survive hiding; the controller alone loses Update. */
    assert(*(uintptr_t *)(g_layout_1c3 + 7u * APPFW_LAYOUT_ITEM_SIZE) == 0x8343DB0u);
    assert(*(uintptr_t *)(g_layout_1c3 + 12u * APPFW_LAYOUT_ITEM_SIZE) == 0x8343B90u);
    assert(*(uintptr_t *)(g_ctrl_items_1c3 + 7u * APPFW_CTRL_ITEM_SIZE) == 0x8344360u);
    uint8_t *cursor = test_ctrl + APPFW_293_CURSOR_OBJ_OFF;
    *(uintptr_t *)(cursor + 0x18u) = (uintptr_t)test_ok_node;
    *(uint64_t *)(cursor + 0x30u) = 1;
    *(uint32_t *)(test_ok_node + 0x20u) = 8u;
    assert(pilot_detail_focus_mask(test_ctrl) == (1u << 8u));
    *(uint32_t *)(test_ok_node + 0x20u) = 7u;
    assert(pilot_detail_focus_mask(test_ctrl) == (1u << 7u));
    *(uint32_t *)(test_ok_node + 0x20u) = 8u;
    g_orig_ctrl_factory_1c3 = detail_factory;
    g_orig_ctrl_items_1c3 = 0x12345678u;
    *(uintptr_t *)(g_appfw_base + APPFW_GOT_CTRL_ITEMS_1C3) = g_orig_ctrl_items_1c3;
    test_item = PILOT_ITEM_ID_BASE;
    assert(pilot_1c3_ctrl_factory_hook(NULL, NULL, NULL, NULL, 0) == test_ctrl);
    assert(test_detail_factories == 1u);
    assert(*(uintptr_t *)(g_appfw_base + APPFW_GOT_CTRL_ITEMS_1C3) ==
           g_orig_ctrl_items_1c3);
    test_patch_failure = 1;
    assert(pilot_1c3_ctrl_factory_hook(NULL, NULL, NULL, NULL, 0) == NULL);
    assert(test_detail_factories == 1u);

    /* Validate module [_ON]/[OFF] labels, description, and detail toggle shell. */
    test_patch_failure = 0;
    g_mods_loaded = 0;
    parse_modules_json("{\"modules\":{\"pilot_menu_ui.so\":true,\"pilot_6k.so\":false}}");
    assert(g_mod_count == 2u && g_mod_enabled[0] == 1 && g_mod_enabled[1] == 0);
    snprintf(g_mod_titles[0], sizeof(g_mod_titles[0]), "Menu UI");
    snprintf(g_mod_titles[1], sizeof(g_mod_titles[1]), "6K Mode");
    snprintf(g_mod_desc_bufs[0], sizeof(g_mod_desc_bufs[0]), "Pilot menu");
    snprintf(g_mod_desc_bufs[1], sizeof(g_mod_desc_bufs[1]), "6K oversample");
    g_mods_loaded = 1;
    const PilotUiPage *root = build_root_page(NULL);
    assert(root->component_count == 3u);
    assert(strcmp(root->components[1].label, "Menu UI") == 0);
    assert(strcmp(root->components[1].value, "开") == 0);
    assert(strcmp(root->components[2].label, "6K Mode") == 0);
    assert(strcmp(root->components[2].value, "关") == 0);

    bind_detail_page_to_layout(&g_modules_detail_pages[1]);
    assert(g_detail_has_toggle == 1 && g_detail_module_index == 1);
    assert(*(uintptr_t *)(g_ctrl_items_1c3 + 7u * APPFW_CTRL_ITEM_SIZE) == 0x8343DB0u);
    assert(*(int32_t *)(g_layout_1c3 + 7u * APPFW_LAYOUT_ITEM_SIZE + 0x24u) == 288);
    assert(*(int32_t *)(g_ctrl_items_1c3 + 7u * APPFW_CTRL_ITEM_SIZE + 0x10u) == 288);
    assert(*(uint32_t *)(g_layout_1c3 + 12u * APPFW_LAYOUT_ITEM_SIZE + 0x34u) ==
           PILOT_RID_DETAIL_TOGGLE);

    g_pilot_detail_active = 1;
    g_active_1c3_view = test_1c3_view;
    g_orig_1c3_confirm = fake_orig_1c3_confirm;
    g_orig_view_update_1c3 = fake_orig_1c3_view_update;
    g_orig_set_string_cstr = fake_set_string_cstr;
    pilot_set_string_rid_hook(&test_toggle_sprite_dummy, PILOT_RID_DETAIL_TOGGLE);
    pilot_set_string_rid_hook(&test_status_sprite_dummy, PILOT_RID_SLOT_BASE);
    assert(strcmp(test_last_toggle_str, "模块开关: [OFF]") == 0);

    test_index = 7u;
    assert(pilot_1c3_confirm_hook(test_ctrl, NULL) == 1u);
    assert(test_orig_1c3_confirms == 0u);
    assert(g_mod_enabled[1] == 1);
    assert(test_fsys_flushed == 1);
    assert(strcmp(test_written_path, "A:/pilot/modules.json") == 0);
    assert(strstr(test_written_json, "\"pilot_menu_ui.so\": true") != NULL);
    assert(strstr(test_written_json, "\"pilot_6k.so\": true") != NULL);
    assert(strcmp(test_last_toggle_str, "模块开关: [_ON]") == 0);
    assert(strcmp(g_root_components[2].value, "开") == 0);
    assert(test_1c3_view_updates == 1u);

    /* Write failure must revert in-memory toggle state. */
    test_fsys_fail_write = 1;
    assert(pilot_1c3_confirm_hook(test_ctrl, NULL) == 1u);
    assert(g_mod_enabled[1] == 1);
    test_fsys_fail_write = 0;

    test_index = 8u;
    assert(pilot_1c3_confirm_hook(test_ctrl, NULL) == 1u);
    assert(test_orig_1c3_confirms == 1u);

    /* Validate UTF-8 visual-width truncation for list labels. */
    char trunc_buf[80];
    format_list_label_truncated(trunc_buf, sizeof(trunc_buf), "[_ON] ", "3k_opengate", 20u);
    assert(strcmp(trunc_buf, "[_ON] 3k_opengate") == 0);
    format_list_label_truncated(trunc_buf, sizeof(trunc_buf), "[OFF] ", "hs6k30_6k_ddrcd_ippp_extra", 20u);
    assert(strcmp(trunc_buf, "[OFF] hs6k30_6k_ddrcd_ip..") == 0);
    format_list_label_truncated(trunc_buf, sizeof(trunc_buf), "[_ON] ", "超级长中文模块名称测试溢出", 20u);
    assert(strcmp(trunc_buf, "[_ON] 超级长中文模块名称..") == 0);

    /* Validate per-module native 0x293 option sublist. */
    static const char *const test_modes[] = {"3:2", "16:9", "4:3"};
    static PilotUiOption test_opts[3] = {
        {.label = "Test Toggle", .kind = PILOT_UI_OPT_BOOL, .value = 0, .on_change = test_opt_on_change},
        {.label = "Aspect Mode", .kind = PILOT_UI_OPT_ENUM, .value = 0, .choices = test_modes, .choice_count = 3, .on_change = test_opt_on_change},
        {.label = "Ping Action", .kind = PILOT_UI_OPT_ACTION, .value = 0, .action_text = "Run", .on_change = test_opt_on_change},
    };
    assert(pilot_ui_register_options_v1("pilot_6k.so", test_opts, 3u) == PILOT_STATUS_OK);
    root = build_root_page(NULL);
    assert(root->components[2].target_page == &g_mod_opt_pages[1]);

    g_pending_list_page = &g_mod_opt_pages[1];
    pilot_apply_pending_list_page();
    assert(g_active_page == &g_mod_opt_pages[1] && g_page_stack_depth == 1);
    assert(g_active_page->component_count == 4u);
    assert(strcmp(g_list_strings[0], "Test Toggle") == 0);
    assert(strcmp(g_list_val_strings[0], "关") == 0);
    assert(strcmp(g_list_strings[1], "Aspect Mode") == 0);
    assert(strcmp(g_list_val_strings[1], "3:2") == 0);
    assert(strcmp(g_list_strings[2], "Ping Action") == 0);
    assert(strcmp(g_list_val_strings[2], "Run (0)") == 0);
    assert(strcmp(g_list_strings[3], "模块信息与开关") == 0);
    assert(strcmp(g_list_val_strings[3], "开") == 0);

    pilot_set_string_rid_hook(&test_slot1_sprite_dummy, PILOT_RID_LIST_VAL_BASE + 0u);
    assert(pilot_handle_opt_list_click(test_ctrl, 0u) == 1);
    assert(test_opts[0].value == 1 && test_opt_callbacks == 1u);
    assert(strcmp(test_last_slot1_str, "开") == 0);

    pilot_set_string_rid_hook(&test_slot1_sprite_dummy, PILOT_RID_LIST_VAL_BASE + 1u);
    assert(pilot_handle_opt_list_click(test_ctrl, 1u) == 1);
    assert(test_opts[1].value == 1 && test_opt_callbacks == 2u);
    assert(strcmp(test_last_slot1_str, "16:9") == 0);

    pilot_set_string_rid_hook(&test_slot1_sprite_dummy, PILOT_RID_LIST_VAL_BASE + 2u);
    assert(pilot_handle_opt_list_click(test_ctrl, 2u) == 1);
    assert(test_opts[2].value == 1 && test_opt_callbacks == 3u);
    assert(strcmp(test_last_slot1_str, "Run (1)") == 0);

    /* 模块信息与开关 falls through (returns 0) so generic_open opens 0x1C3. */
    assert(pilot_handle_opt_list_click(test_ctrl, 3u) == 0);

    /* Validate 0x2A7 native option picker descriptor & selection callback. */
    pilot_2a7_prepare_choices("Aspect Mode", test_modes, 3u, 1u);
    assert(pilot_acc_get_group_desc_hook(NULL, PILOT_OPT_GROUP_ID) == g_opt_setting_desc);
    assert(*(uint32_t *)(g_opt_setting_desc + 0x40ull) == 3u);
    assert(pilot_acc_get_setting_val_hook(NULL, PILOT_OPT_GROUP_ID) == 1u);
    assert(pilot_acc_is_row_visible_hook(NULL, PILOT_OPT_CHOICE_KEY_BASE + 2u) == 1u);
    assert(pilot_acc_is_row_visible_hook(NULL, PILOT_OPT_CHOICE_KEY_BASE + 3u) == 0u);
    assert(pilot_acc_item_to_group_hook(NULL, PILOT_OPT_CHOICE_KEY_BASE + 2u) == PILOT_OPT_GROUP_ID);
    g_pilot_2a7_mod_idx = 1u;
    g_pilot_2a7_row_idx = 1u;
    pilot_2a7_apply_selection(2u); /* choose "4:3" */
    assert(test_opts[1].value == 2 && test_opt_callbacks == 4u);
    assert(strcmp(g_list_val_strings[1], "4:3") == 0);

    munmap(mapping, 0x5000000);
    puts("PASS navigation lifecycle, repeated returns, native pass-through, focus diagnostics, patch errors, toggle shell");
    return 0;
}
