/* A7C II model layer: the Pilot menu content exposed through the native list
 * screen.
 *
 * The A6700 module renders its own page structure into two synthetic screens.
 * On this body the stock list screen (0x2cb) is reused instead, so the content
 * only has to be a tree of owned definitions.  This file owns that tree and the
 * "model" entry points the native list port (pilot_a7c2_list.inc) calls:
 * definitions, children, values, actions and a single scalar editor.
 *
 * Everything here is body independent on purpose; it links against the vendored
 * menu_api.c registry and never touches appFw.  tests/test_a7c2_model.c compiles
 * this file on the host. */

#include "vendor/menu_api.h"
#include "vendor/menu_example.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Content ids.  Logical only: the transport maps them to raw ids/resources and
 * nothing outside this file may depend on the numbers. */
enum {
    PILOT_ID_GROUP = 1,
    PILOT_ID_STATUS,       /* page "状态" */
    PILOT_ID_MODEL,        /* readonly: body */
    PILOT_ID_FIRMWARE,     /* readonly: firmware */
    PILOT_ID_FRAMEWORK,    /* readonly: Pilot framework */
    PILOT_ID_ENTRY,        /* readonly: where the menu lives */
    PILOT_ID_TOOLS,        /* page "工具" */
    PILOT_ID_ACTION_COUNT, /* readonly: action counter */
    PILOT_ID_DEBUG,        /* bool: extra diagnostics */
    PILOT_ID_DEBUG_STATE,  /* readonly: diagnostics state */
    PILOT_ID_RESET         /* action: reset counters */
};

static menu_api g_registry;
static menu_api_snapshot g_listing;
static int g_initialized, g_editing;
static menu_api_handle g_edited;
static menu_api_info g_edit_info;
static menu_api_value g_preview;
static menu_api_handle g_consumer_handles[MENU_API_NODE_LIMIT];
static size_t g_consumer_count;
static int g_consumer_active;
static uint32_t g_actions;
static int64_t g_debug;

static int bool_format(void *unused, int64_t value, char *out, size_t size) {
    (void)unused;
    int count = snprintf(out, size, "%s", value ? "开" : "关");
    return count >= 0 && (size_t)count < size ? 0 : -1;
}
static int text_body(void *unused, menu_api_value *value) {
    (void)unused;
    strcpy(value->text, "ILCE-7CM2 (A7C II)");
    return 0;
}
static int text_firmware(void *unused, menu_api_value *value) {
    (void)unused;
    strcpy(value->text, "2.01");
    return 0;
}
static int text_framework(void *unused, menu_api_value *value) {
    (void)unused;
    strcpy(value->text, "pilot_a7c2 - ABI 2");
    return 0;
}
static int text_entry(void *unused, menu_api_value *value) {
    (void)unused;
    strcpy(value->text, "设置 - 设置选项");
    return 0;
}
static int text_actions(void *unused, menu_api_value *value) {
    (void)unused;
    return snprintf(value->text, sizeof value->text, "%" PRIu32 " 次", g_actions) < 0 ? -1 : 0;
}
static int text_debug_state(void *unused, menu_api_value *value) {
    (void)unused;
    strcpy(value->text, g_debug ? "已启用" : "已关闭");
    return 0;
}
static int debug_get(void *unused, menu_api_value *value) {
    (void)unused;
    value->number = g_debug;
    return 0;
}
static int debug_set(void *unused, int64_t value) {
    (void)unused;
    g_debug = value != 0;
    return 0;
}
static int action_reset(void *unused) {
    (void)unused;
    ++g_actions;
    g_debug = 0;
    return 0;
}

const char *pilot_menu_title(void) {
    return "Pilot";
}

/* One table, parents by index.  Order matters: a parent must be registered
 * before its children, and parent_index is 1-based into this array (0 = root). */
static menu_api_status register_tree(void) {
    const menu_api_def definitions[] = {
        {.id = PILOT_ID_GROUP, .parent = MENU_API_ROOT, .kind = MENU_API_GROUP, .label = "Pilot"},
        {.id = PILOT_ID_STATUS, .parent = MENU_API_ROOT, .kind = MENU_API_PAGE, .label = "状态"},
        {.id = PILOT_ID_MODEL, .parent = MENU_API_ROOT, .kind = MENU_API_READONLY,
         .label = "机型", .get = text_body},
        {.id = PILOT_ID_FIRMWARE, .parent = MENU_API_ROOT, .kind = MENU_API_READONLY,
         .label = "固件", .get = text_firmware},
        {.id = PILOT_ID_FRAMEWORK, .parent = MENU_API_ROOT, .kind = MENU_API_READONLY,
         .label = "框架", .get = text_framework},
        {.id = PILOT_ID_ENTRY, .parent = MENU_API_ROOT, .kind = MENU_API_READONLY,
         .label = "入口", .get = text_entry},
        {.id = PILOT_ID_TOOLS, .parent = MENU_API_ROOT, .kind = MENU_API_PAGE, .label = "工具"},
        {.id = PILOT_ID_ACTION_COUNT, .parent = MENU_API_ROOT, .kind = MENU_API_READONLY,
         .label = "操作次数", .get = text_actions},
        {.id = PILOT_ID_DEBUG, .parent = MENU_API_ROOT, .kind = MENU_API_BOOL,
         .label = "调试信息", .get = debug_get, .set = debug_set, .format = bool_format},
        {.id = PILOT_ID_DEBUG_STATE, .parent = MENU_API_ROOT, .kind = MENU_API_READONLY,
         .label = "调试状态", .get = text_debug_state},
        {.id = PILOT_ID_RESET, .parent = MENU_API_ROOT, .kind = MENU_API_ACTION,
         .label = "重置计数", .action = action_reset},
    };
    static const uint32_t parents[] = {0, 1, 2, 2, 2, 2, 1, 7, 7, 7, 7};
    const size_t total = sizeof definitions / sizeof definitions[0];
    menu_api_handle registered[sizeof definitions / sizeof definitions[0]] = {0};
    size_t count = 0;
    menu_api_status status = MENU_API_OK;
    for (size_t i = 0; i < total; ++i) {
        menu_api_def def = definitions[i];
        def.parent = parents[i] ? registered[parents[i] - 1] : MENU_API_ROOT;
        status = menu_api_register(&g_registry, &def, &registered[i]);
        if (status != MENU_API_OK)
            break;
        ++count;
    }
    if (status != MENU_API_OK) {
        while (count) {
            menu_api_status removed = menu_api_unregister(&g_registry, registered[--count]);
            if (removed != MENU_API_OK) {
                status = removed;
                break;
            }
        }
    }
    return status;
}

uint32_t menu_example_entry_id(void) {
    return PILOT_ID_GROUP;
}

menu_api_status menu_example_initialize(void) {
    if (g_initialized)
        return MENU_API_OK;
    menu_api_status status = menu_api_enter(&g_registry);
    if (status != MENU_API_OK)
        return status;
    status = register_tree();
    if (status == MENU_API_OK)
        g_initialized = 1;
    menu_api_status left = menu_api_leave(&g_registry);
    return status == MENU_API_OK ? left : status;
}

static menu_api_status enter(void) {
    return g_initialized ? menu_api_enter(&g_registry) : MENU_API_E_STATE;
}
static menu_api_status leave(menu_api_status status) {
    menu_api_status left = menu_api_leave(&g_registry);
    return status == MENU_API_OK ? left : status;
}
menu_api_status menu_example_model_enter(menu_api **model) {
    if (!model)
        return MENU_API_E_ARGUMENT;
    *model = 0;
    menu_api_status status = enter();
    if (status == MENU_API_OK)
        *model = &g_registry;
    return status;
}
menu_api_status menu_example_model_leave(void) {
    return g_initialized ? menu_api_leave(&g_registry) : MENU_API_E_STATE;
}
static menu_api_status find(uint32_t id, menu_api_handle *handle) {
    if (!id) {
        *handle = MENU_API_ROOT;
        return MENU_API_OK;
    }
    return menu_api_find(&g_registry, id, handle);
}
menu_api_status menu_example_children(uint32_t parent, menu_api_snapshot *snapshot) {
    menu_api_handle handle;
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    status = find(parent, &handle);
    if (status == MENU_API_OK)
        status = menu_api_snapshot_get(&g_registry, handle, snapshot);
    return leave(status);
}
menu_api_status menu_example_definitions(uint32_t parent, menu_api_info *items, size_t capacity,
                                         size_t *count) {
    if (!count || (!items && capacity))
        return MENU_API_E_ARGUMENT;
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    menu_api_handle handle, children[MENU_API_NODE_LIMIT];
    status = find(parent, &handle);
    if (status == MENU_API_OK)
        status = menu_api_children(&g_registry, handle, children, MENU_API_NODE_LIMIT, count);
    if (status == MENU_API_OK && items && capacity < *count)
        status = MENU_API_E_LIMIT;
    if (status == MENU_API_OK && items) {
        for (size_t i = 0; i < *count; ++i) {
            status = menu_api_describe(&g_registry, children[i], &items[i]);
            if (status != MENU_API_OK)
                break;
        }
    }
    return leave(status);
}
/* One native controller/presenter generation pins the whole tree, so a stale
 * native reference can never resolve into a recycled registry slot. */
menu_api_status menu_example_consumer_acquire(void) {
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    if (g_consumer_active)
        return leave(MENU_API_E_BUSY);
    menu_api_handle parents[MENU_API_NODE_LIMIT + 1] = {MENU_API_ROOT};
    menu_api_handle children[MENU_API_NODE_LIMIT];
    size_t head = 0, tail = 1;
    while (status == MENU_API_OK && head < tail) {
        size_t count = 0;
        status = menu_api_children(&g_registry, parents[head++], children, MENU_API_NODE_LIMIT, &count);
        for (size_t i = 0; status == MENU_API_OK && i < count; ++i) {
            menu_api_info info;
            status = menu_api_describe(&g_registry, children[i], &info);
            if (status != MENU_API_OK)
                break;
            if (g_consumer_count == MENU_API_NODE_LIMIT) {
                status = MENU_API_E_LIMIT;
                break;
            }
            status = menu_api_pin(&g_registry, children[i]);
            if (status != MENU_API_OK)
                break;
            g_consumer_handles[g_consumer_count++] = children[i];
            if (info.kind == MENU_API_GROUP || info.kind == MENU_API_PAGE ||
                    info.kind == MENU_API_SUBMENU) {
                if (tail == MENU_API_NODE_LIMIT + 1) {
                    status = MENU_API_E_LIMIT;
                    break;
                }
                parents[tail++] = children[i];
            }
        }
    }
    if (status == MENU_API_OK)
        g_consumer_active = 1;
    else
        while (g_consumer_count) {
            menu_api_status released =
                menu_api_unpin(&g_registry, g_consumer_handles[--g_consumer_count]);
            if (released != MENU_API_OK) {
                status = released;
                break;
            }
        }
    return leave(status);
}
menu_api_status menu_example_consumer_release(void) {
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    if (!g_consumer_active)
        return leave(MENU_API_E_STATE);
    while (g_consumer_count) {
        status = menu_api_unpin(&g_registry, g_consumer_handles[g_consumer_count - 1]);
        if (status != MENU_API_OK)
            break;
        --g_consumer_count;
    }
    if (status == MENU_API_OK)
        g_consumer_active = 0;
    return leave(status);
}
menu_api_status menu_example_describe(uint32_t id, menu_api_info *info) {
    menu_api_handle handle;
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    status = find(id, &handle);
    if (status == MENU_API_OK)
        status = menu_api_describe(&g_registry, handle, info);
    return leave(status);
}
menu_api_status menu_example_value(uint32_t id, menu_api_result *result) {
    menu_api_handle handle;
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    status = find(id, &handle);
    if (status == MENU_API_OK)
        status = menu_api_read(&g_registry, handle, result);
    return leave(status);
}
menu_api_status menu_example_invoke(uint32_t id, menu_api_result *result) {
    menu_api_handle handle;
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    status = find(id, &handle);
    if (status == MENU_API_OK)
        status = menu_api_invoke(&g_registry, handle, result);
    return leave(status);
}

static menu_api_status format_preview(void) {
    if (g_edit_info.kind == MENU_API_BOOL)
        return bool_format(0, g_preview.number, g_preview.text, sizeof g_preview.text) == 0
                   ? MENU_API_OK
                   : MENU_API_E_RANGE;
    if (g_edit_info.kind == MENU_API_NUMERIC) {
        char text[MENU_API_VALUE_MAX + 1u];
        int count = snprintf(text, sizeof text, "%" PRId64, g_preview.number);
        if (count < 0 || (size_t)count >= sizeof text)
            return MENU_API_E_RANGE;
        memcpy(g_preview.text, text, (size_t)count + 1u);
        return MENU_API_OK;
    }
    for (size_t i = 0; i < g_edit_info.option_count; ++i) {
        menu_api_option option;
        menu_api_status status = menu_api_option_at(&g_registry, g_edited, i, &option);
        if (status != MENU_API_OK)
            return status;
        if (option.value == g_preview.number) {
            memcpy(g_preview.text, option.label, strlen(option.label) + 1);
            return MENU_API_OK;
        }
    }
    return MENU_API_E_RANGE;
}
static void preview_result(menu_api_result *result) {
    memset(result, 0, sizeof *result);
    result->has_value = true;
    result->fresh = true;
    result->value = g_preview;
}
menu_api_status menu_example_edit_begin(uint32_t id, menu_api_result *preview) {
    if (!preview)
        return MENU_API_E_ARGUMENT;
    memset(preview, 0, sizeof *preview);
    if (g_editing)
        return MENU_API_E_BUSY;
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    menu_api_handle handle;
    menu_api_info info;
    status = find(id, &handle);
    if (status == MENU_API_OK)
        status = menu_api_describe(&g_registry, handle, &info);
    if (status == MENU_API_OK && info.kind != MENU_API_BOOL && info.kind != MENU_API_ENUM &&
            info.kind != MENU_API_NUMERIC)
        status = MENU_API_E_TYPE;
    if (status == MENU_API_OK)
        status = menu_api_snapshot_get(&g_registry, info.parent, &g_listing);
    int found = 0;
    if (status == MENU_API_OK) {
        for (size_t i = 0; i < g_listing.count; ++i)
            if (g_listing.rows[i].info.handle == handle) {
                found = 1;
                if (!g_listing.rows[i].enabled)
                    status = MENU_API_E_DISABLED;
                else
                    g_preview = g_listing.rows[i].value;
                break;
            }
        if (!found)
            status = MENU_API_E_HIDDEN;
    }
    if (status == MENU_API_OK) {
        g_edited = handle;
        g_edit_info = info;
        status = format_preview();
        if (status == MENU_API_OK) {
            g_editing = 1;
            preview_result(preview);
        }
    }
    return leave(status);
}
menu_api_status menu_example_edit_move(int direction, menu_api_result *preview) {
    if (!preview || (direction != -1 && direction != 1))
        return MENU_API_E_ARGUMENT;
    memset(preview, 0, sizeof *preview);
    if (!g_editing)
        return MENU_API_E_STATE;
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    int64_t old = g_preview.number, next = old;
    if (g_edit_info.kind == MENU_API_BOOL) {
        next = direction > 0 ? 1 : 0;
    } else if (g_edit_info.kind == MENU_API_NUMERIC) {
        uint64_t distance = direction > 0 ? (uint64_t)g_edit_info.maximum - (uint64_t)old
                                          : (uint64_t)old - (uint64_t)g_edit_info.minimum;
        if (distance < (uint64_t)g_edit_info.step)
            status = MENU_API_E_RANGE;
        else
            next = direction > 0 ? old + g_edit_info.step : old - g_edit_info.step;
    } else {
        size_t current = g_edit_info.option_count;
        for (size_t i = 0; i < g_edit_info.option_count; ++i) {
            menu_api_option option;
            status = menu_api_option_at(&g_registry, g_edited, i, &option);
            if (status != MENU_API_OK)
                break;
            if (option.value == old)
                current = i;
        }
        int found = 0;
        while (status == MENU_API_OK && current < g_edit_info.option_count &&
               (direction > 0 ? current + 1 < g_edit_info.option_count : current > 0)) {
            current = direction > 0 ? current + 1 : current - 1;
            menu_api_option option;
            status = menu_api_option_at(&g_registry, g_edited, current, &option);
            if (status == MENU_API_OK && option.enabled) {
                next = option.value;
                found = 1;
                break;
            }
        }
        if (status == MENU_API_OK && !found)
            status = MENU_API_E_RANGE;
    }
    if (status == MENU_API_OK) {
        g_preview.number = next;
        status = format_preview();
        if (status != MENU_API_OK)
            g_preview.number = old;
    }
    preview_result(preview);
    preview->fresh = status == MENU_API_OK;
    return leave(status);
}
menu_api_status menu_example_edit_commit(menu_api_result *result) {
    if (!result)
        return MENU_API_E_ARGUMENT;
    if (!g_editing)
        return MENU_API_E_STATE;
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    status = menu_api_set(&g_registry, g_edited, g_preview.number, result);
    if (status == MENU_API_OK) {
        g_editing = 0;
        g_edited = MENU_API_ROOT;
    }
    return leave(status);
}
menu_api_status menu_example_edit_cancel(void) {
    menu_api_status status = enter();
    if (status != MENU_API_OK)
        return status;
    if (!g_editing)
        return leave(MENU_API_E_STATE);
    g_editing = 0;
    g_edited = MENU_API_ROOT;
    memset(&g_preview, 0, sizeof g_preview);
    return leave(MENU_API_OK);
}
uint32_t menu_example_action_count(void) {
    return g_actions;
}
void menu_example_storage_counts(uint32_t *saves, uint32_t *reads) {
    if (saves)
        *saves = 0;
    if (reads)
        *reads = 0;
}
