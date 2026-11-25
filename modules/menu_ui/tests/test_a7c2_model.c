/* Host test for the A7C II model layer.
 *
 * The model is the body independent half of the port: it owns the Pilot content
 * tree and implements the menu_example_* entry points the native list transport
 * calls.  Nothing here touches appFw, so the whole file runs on the build host
 * against the vendored registry (../a7c2/vendor/menu_api.c).
 *
 * What this covers, and what it cannot: the tree shape, the value callbacks,
 * the scalar editor and the consumer pin lifetime are checked here; the native
 * list transport, the patch slots and the on-camera wiring are not, because
 * they only exist inside appFw.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../a7c2/pilot_menu_model.inc"

static const char *label_of(uint32_t parent, size_t index) {
    static menu_api_snapshot snapshot;
    assert(menu_example_children(parent, &snapshot) == MENU_API_OK);
    assert(index < snapshot.count);
    return snapshot.rows[index].info.label;
}

static void test_empty_registry(void) {
    menu_api_snapshot snapshot;
    menu_api_info items[4];
    size_t count = 99;
    assert(menu_example_children(PILOT_ID_GROUP, &snapshot) == MENU_API_E_STATE);
    assert(menu_example_definitions(PILOT_ID_GROUP, items, 4, &count) == MENU_API_E_STATE);
    assert(count == 99);
    assert(menu_example_action_count() == 0);
    assert(strcmp(pilot_menu_title(), "Pilot") == 0);
}

static void test_tree_shape(void) {
    menu_api_snapshot snapshot;
    menu_api_info items[MENU_API_NODE_LIMIT];
    size_t count = 0;

    assert(menu_example_initialize() == MENU_API_OK);
    /* Initialisation is idempotent: the module installs once per boot. */
    assert(menu_example_initialize() == MENU_API_OK);
    assert(menu_example_entry_id() == PILOT_ID_GROUP);

    /* The root is the entry group, so definitions(0) is that single group. */
    assert(menu_example_definitions(0, items, MENU_API_NODE_LIMIT, &count) == MENU_API_OK);
    assert(count == 1);
    assert(items[0].kind == MENU_API_GROUP);
    assert(strcmp(items[0].label, "Pilot") == 0);

    /* Two pages under the group, both containers. */
    assert(menu_example_children(PILOT_ID_GROUP, &snapshot) == MENU_API_OK);
    assert(snapshot.count == 2);
    assert(snapshot.rows[0].info.kind == MENU_API_PAGE);
    assert(strcmp(snapshot.rows[0].info.label, "状态") == 0);
    assert(strcmp(snapshot.rows[1].info.label, "工具") == 0);

    /* Every row is navigable: the transport skips non-enabled rows. */
    for (size_t i = 0; i < snapshot.count; ++i)
        assert(snapshot.rows[i].enabled);

    assert(menu_example_children(PILOT_ID_STATUS, &snapshot) == MENU_API_OK);
    assert(snapshot.count == 4);
    for (size_t i = 0; i < snapshot.count; ++i) {
        assert(snapshot.rows[i].info.kind == MENU_API_READONLY);
        assert(snapshot.rows[i].enabled);
        assert(snapshot.enabled_count == snapshot.count);
    }
    assert(strcmp(label_of(PILOT_ID_STATUS, 0), "机型") == 0);
    assert(strcmp(label_of(PILOT_ID_STATUS, 1), "固件") == 0);
    assert(strcmp(label_of(PILOT_ID_STATUS, 2), "框架") == 0);
    assert(strcmp(label_of(PILOT_ID_STATUS, 3), "入口") == 0);

    assert(menu_example_children(PILOT_ID_TOOLS, &snapshot) == MENU_API_OK);
    assert(snapshot.count == 4);
    assert(snapshot.rows[0].info.kind == MENU_API_READONLY);
    assert(snapshot.rows[1].info.kind == MENU_API_BOOL);
    assert(snapshot.rows[2].info.kind == MENU_API_READONLY);
    assert(snapshot.rows[3].info.kind == MENU_API_ACTION);
}

static void test_values(void) {
    menu_api_result result;
    menu_api_snapshot snapshot;

    assert(menu_example_value(PILOT_ID_MODEL, &result) == MENU_API_OK);
    assert(result.has_value);
    assert(strcmp(result.value.text, "ILCE-7CM2 (A7C II)") == 0);
    assert(menu_example_value(PILOT_ID_FIRMWARE, &result) == MENU_API_OK);
    assert(strcmp(result.value.text, "2.01") == 0);
    assert(menu_example_value(PILOT_ID_FRAMEWORK, &result) == MENU_API_OK);
    assert(strcmp(result.value.text, "pilot_a7c2 - ABI 2") == 0);
    assert(menu_example_value(PILOT_ID_ENTRY, &result) == MENU_API_OK);
    assert(strcmp(result.value.text, "设置 - 设置选项") == 0);

    /* The value column of a BOOL row goes through its formatter. */
    assert(menu_example_children(PILOT_ID_TOOLS, &snapshot) == MENU_API_OK);
    assert(snapshot.rows[1].value.number == 0);
    assert(strcmp(snapshot.rows[1].value.text, "关") == 0);
    assert(strcmp(snapshot.rows[0].value.text, "0 次") == 0);
    assert(strcmp(snapshot.rows[2].value.text, "已关闭") == 0);
}

static void test_editor(void) {
    menu_api_result preview, result;

    /* Only scalars can be edited; the transport only opens them on ENTER. */
    assert(menu_example_edit_begin(PILOT_ID_MODEL, &preview) == MENU_API_E_TYPE);
    assert(menu_example_edit_begin(PILOT_ID_GROUP, &preview) == MENU_API_E_TYPE);
    assert(menu_example_edit_move(1, &preview) == MENU_API_E_STATE);
    assert(menu_example_edit_commit(&result) == MENU_API_E_STATE);
    assert(menu_example_edit_cancel() == MENU_API_E_STATE);

    assert(menu_example_edit_begin(PILOT_ID_DEBUG, &preview) == MENU_API_OK);
    assert(preview.has_value && preview.value.number == 0);
    assert(strcmp(preview.value.text, "关") == 0);
    /* One editor at a time. */
    assert(menu_example_edit_begin(PILOT_ID_DEBUG, &preview) == MENU_API_E_BUSY);

    assert(menu_example_edit_move(1, &preview) == MENU_API_OK);
    assert(preview.value.number == 1);
    assert(strcmp(preview.value.text, "开") == 0);
    /* A BOOL only has two states, so moving up again is a no-op at the limit. */
    assert(menu_example_edit_move(1, &preview) == MENU_API_OK);
    assert(preview.value.number == 1);
    assert(menu_example_edit_move(-1, &preview) == MENU_API_OK);
    assert(preview.value.number == 0);
    assert(menu_example_edit_move(1, &preview) == MENU_API_OK);

    assert(menu_example_edit_commit(&result) == MENU_API_OK);
    assert(menu_example_value(PILOT_ID_DEBUG, &result) == MENU_API_OK);
    assert(result.value.number == 1);
    assert(menu_example_value(PILOT_ID_DEBUG_STATE, &result) == MENU_API_OK);
    assert(strcmp(result.value.text, "已启用") == 0);

    /* Cancelling a preview must not publish it. */
    assert(menu_example_edit_begin(PILOT_ID_DEBUG, &preview) == MENU_API_OK);
    assert(menu_example_edit_move(-1, &preview) == MENU_API_OK);
    assert(menu_example_edit_cancel() == MENU_API_OK);
    assert(menu_example_value(PILOT_ID_DEBUG, &result) == MENU_API_OK);
    assert(result.value.number == 1);
}

static void test_action(void) {
    menu_api_result result, preview;
    menu_api_snapshot snapshot;

    assert(menu_example_invoke(PILOT_ID_RESET, &result) == MENU_API_OK);
    assert(menu_example_action_count() == 1);
    assert(menu_example_value(PILOT_ID_ACTION_COUNT, &result) == MENU_API_OK);
    assert(strcmp(result.value.text, "1 次") == 0);
    /* The reset also drops the diagnostics flag, which the row reports live. */
    assert(menu_example_value(PILOT_ID_DEBUG, &result) == MENU_API_OK);
    assert(result.value.number == 0);
    assert(menu_example_children(PILOT_ID_TOOLS, &snapshot) == MENU_API_OK);
    assert(strcmp(snapshot.rows[2].value.text, "已关闭") == 0);

    assert(menu_example_invoke(PILOT_ID_RESET, &result) == MENU_API_OK);
    assert(menu_example_action_count() == 2);
    /* Actions are not editable. */
    assert(menu_example_edit_begin(PILOT_ID_RESET, &preview) == MENU_API_E_TYPE);
}

static void test_consumer(void) {
    /* A native controller generation pins the whole tree, so a stale raw id can
     * never resolve into a recycled registry slot. */
    assert(menu_example_consumer_acquire() == MENU_API_OK);
    assert(g_consumer_active);
    assert(g_consumer_count == 11);
    assert(menu_example_consumer_acquire() == MENU_API_E_BUSY);
    assert(menu_example_consumer_release() == MENU_API_OK);
    assert(!g_consumer_active && g_consumer_count == 0);
    assert(menu_example_consumer_release() == MENU_API_E_STATE);

    /* Pins must survive a full re-read of the tree (the transport re-freezes on
     * every list build). */
    assert(menu_example_consumer_acquire() == MENU_API_OK);
    assert(menu_example_initialize() == MENU_API_OK);
    assert(menu_example_value(PILOT_ID_MODEL, &(menu_api_result){0}) == MENU_API_OK);
    assert(menu_example_consumer_release() == MENU_API_OK);
}

static void test_argument_guards(void) {
    menu_api_snapshot snapshot;
    menu_api_result result;
    size_t count = 0;

    assert(menu_example_children(PILOT_ID_GROUP, 0) == MENU_API_E_ARGUMENT);
    assert(menu_example_definitions(PILOT_ID_GROUP, 0, 4, &count) == MENU_API_E_ARGUMENT);
    assert(menu_example_definitions(PILOT_ID_GROUP, 0, 0, 0) == MENU_API_E_ARGUMENT);
    assert(menu_example_model_enter(0) == MENU_API_E_ARGUMENT);
    assert(menu_example_edit_begin(PILOT_ID_DEBUG, 0) == MENU_API_E_ARGUMENT);
    assert(menu_example_edit_move(0, &result) == MENU_API_E_ARGUMENT);
    assert(menu_example_edit_commit(0) == MENU_API_E_ARGUMENT);

    /* Unknown ids are not resolved into anything. */
    assert(menu_example_children(999, &snapshot) != MENU_API_OK);
    assert(menu_example_value(999, &result) != MENU_API_OK);
    assert(menu_example_invoke(999, &result) != MENU_API_OK);

    /* A model scope hands the caller the raw registry for one generation; the
     * menu_example_* wrappers enter and leave it themselves, so they must not be
     * mixed inside it (menu_api_enter is not nestable). */
    menu_api *model = 0;
    assert(menu_example_model_enter(&model) == MENU_API_OK);
    assert(model == &g_registry);
    menu_api_handle handle = MENU_API_ROOT;
    assert(menu_api_find(model, PILOT_ID_STATUS, &handle) == MENU_API_OK);
    assert(handle != MENU_API_ROOT);
    assert(menu_api_snapshot_get(model, handle, &snapshot) == MENU_API_OK);
    assert(snapshot.count == 4);
    assert(menu_example_children(PILOT_ID_STATUS, &snapshot) == MENU_API_E_BUSY);
    assert(menu_example_model_leave() == MENU_API_OK);
    assert(menu_example_children(PILOT_ID_STATUS, &snapshot) == MENU_API_OK);
}

int main(void) {
    test_empty_registry();
    test_tree_shape();
    test_values();
    test_editor();
    test_action();
    test_consumer();
    test_argument_guards();
    puts("a7c2 model: ok");
    return 0;
}
