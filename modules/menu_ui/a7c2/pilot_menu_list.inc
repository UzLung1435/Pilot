/* Native list transport for the A7C II primary menu.
 *
 * Included at the END of pilot_menu_ui_a7c2.c, after the glue and the hooks:
 * this file calls back into them (base, trace, protection, add_patch, own_record,
 * original_alias/reverse/normal) and into the model layer (menu_example_*).
 *
 * Only mechanical changes were made against the reference implementation in
 * a7c2-re/report/2026-11-05-appfw-source-rebuild/firmware_abi:
 *   - the include set points at the vendored registry instead of the report tree;
 *   - the exported a7c2_native_menu_* AP entry points were dropped: this module
 *     is not linked into the firmware, it is dlopen'd by Pilot, so the list
 *     screen is reached through the stock menu instead of through an AP call.
 *   - the observation record is exported as pilot_menu_list_observation.
 * Everything else is the reference code, so the key/rebuild contracts that were
 * machine-verified on hardware keep their exact shape. */
#include "vendor/menu_api.h"
#include "vendor/menu_example.h"

#define LIST_SCREEN 0x2cbu
#define LIST_RAW_FIRST 0x640u
#define LIST_RESOURCE_FIRST 0x02fe0000u
#define LIST_SLOTS MENU_API_NODE_LIMIT
#define LIST_LEFT_ON 0x02010006u
#define LIST_RIGHT_ON 0x02010009u
#define LIST_LEFT_REPEAT 0x02010008u
#define LIST_RIGHT_REPEAT 0x0201000bu
#define LIST_C_VTABLE 0x5b135e0ul
#define LIST_P_VTABLE 0x5afaea0ul
#define LIST_DISPATCH 0x1934974ul

/* These are actual native constructor results, never fabricated C++ objects. */
typedef struct { uint64_t opaque[2]; } list_native_scope;
typedef struct { uint64_t opaque[4]; } list_native_writer;
typedef list_native_writer (*list_make_writer_fn)(uint64_t);
typedef void (*list_scope_fn)(list_native_scope *);
typedef void (*list_notify_fn)(list_native_writer *);
typedef list_native_writer *(*list_write_parent_fn)(list_native_writer *, const uint32_t *);
typedef list_native_writer *(*list_write_focus_fn)(list_native_writer *, const uint32_t *, uint32_t);
typedef void *(*list_c_factory_fn)(int32_t, void *, void *, void *, int32_t, int32_t);
typedef uint64_t (*list_dispatch_fn)(void *, const void *, uint32_t);
typedef uint32_t (*list_children_fn)(void *, uint32_t, uint32_t *, int32_t);
typedef uint32_t (*list_value_fn)(void *, uint64_t, void *);
typedef void (*list_enter_fn)(void *, const uint32_t *);
typedef void (*list_dtor_fn)(void *);
typedef void *(*list_param_fn)(void **);
typedef void *(*list_dict_fn)(void *, int32_t);
typedef void (*list_number_fn)(uint32_t, int32_t, int32_t, int32_t, void *);
typedef void (*list_open_fn)(void *, const uint32_t *, const uint32_t *, const uint32_t *, uint32_t, uint32_t);
_Static_assert(sizeof(list_native_scope) == 16 && sizeof(list_native_writer) == 32, "native sret ABI");

struct list_node {
    menu_api_handle handle;
    uint32_t logical, parent, kind, present, visible, enabled, has_value, count;
    uint32_t children[MENU_API_VISIBLE_LIMIT];
    int64_t number;
    char label[MENU_API_LABEL_MAX + 16u];
    char value[MENU_API_VALUE_MAX + 1u];
    unsigned char record[0xc0] __attribute__((aligned(8)));
};
struct list_generation {
    uint32_t serial;
    struct list_node nodes[LIST_SLOTS + 1u];
};
struct list_frame { uint32_t parent, entry, focused, generation; };

MENU_API_EXPORT volatile struct pilot_a7c2_list_observation pilot_menu_list_observation = {
    .magic = PILOT_A7C2_LIST_MAGIC, .abi_version = 2
};
static struct list_generation list_generations[2];
static menu_api_snapshot list_scratch;
static menu_api_info list_definitions[LIST_SLOTS];
static struct list_frame list_frames[LIST_SLOTS + 1u];
static struct list_frame list_pending_frame;
static menu_api_result list_preview;
static unsigned int list_ready, list_catalog_checked, list_active, list_pending;
static unsigned int list_depth, list_pending_depth, list_next_serial;
static unsigned int list_c_pin, list_p_pin, list_p_constructing, list_closing;
static uint32_t list_c_generation, list_p_generation;
static unsigned int list_enter_latch, list_menu_latch, list_edit_id, list_busy;
static unsigned int list_inflight, list_root_restore, list_semantic_pin;
static void *list_c, *list_p, *list_view, *list_pool, *list_veneer;
static list_dispatch_fn list_original_dispatch;
static list_c_factory_fn list_original_c_factory;
static factory_fn list_original_p_factory;
static list_children_fn list_original_children;
static list_value_fn list_original_value;
static list_enter_fn list_original_enter;
static list_dtor_fn list_original_c_dtor, list_original_p_dtor;

static void list_status(menu_api_status code, int callback_code, const char *operation) {
    pilot_menu_list_observation.last_status = (uint32_t)code;
    pilot_menu_list_observation.callback_code = callback_code;
    trace("MENU_API_LIST_STATUS ", "status=%u callback=%d operation=%s", (unsigned int)code, callback_code, operation);
}
static void list_error(menu_api_status code, const char *operation) {
    list_status(code, 0, operation);
    if (!pilot_menu_list_observation.last_error)
        pilot_menu_list_observation.last_error = (uint32_t)code;
    trace("MENU_API_LIST_ERROR ", "code=%u operation=%s parent=%#x generation=%u", (unsigned int)code,
          operation, pilot_menu_list_observation.parent, pilot_menu_list_observation.generation);
}
static void list_count_inflight(int change) {
    if (change > 0) ++list_inflight;
    else --list_inflight;
    pilot_menu_list_observation.in_flight = list_inflight;
}
static void list_release_semantics_if_detached(void) {
    if (!list_semantic_pin || list_c_pin || list_p_pin || list_p_constructing || list_inflight || list_busy) return;
    menu_api_status status = menu_example_consumer_release();
    if (status != MENU_API_OK) list_error(status, "semantic-release-after-detach");
    else list_semantic_pin = 0;
}
static uint32_t list_raw(uint32_t id) { return id ? LIST_RAW_FIRST + id - 1u : NEW_ITEM; }
static int list_id(uint32_t raw) {
    if (raw == NEW_ITEM) return 0;
    return raw >= LIST_RAW_FIRST && raw < LIST_RAW_FIRST + LIST_SLOTS ? (int)(raw - LIST_RAW_FIRST + 1u) : -1;
}
static uint32_t list_resource(uint32_t id, unsigned int value) { return LIST_RESOURCE_FIRST + id * 2u + value; }
static int list_container(uint32_t kind) {
    return kind == MENU_API_GROUP || kind == MENU_API_PAGE || kind == MENU_API_SUBMENU;
}
static struct list_generation *list_current(void) { return &list_generations[list_active]; }
static uint32_t list_parent(void) { return list_depth ? list_frames[list_depth - 1u].parent : 0; }
static int list_has_child(const struct list_generation *g, uint32_t parent, uint32_t raw) {
    int id = list_id(parent);
    if (id < 0 || !g->nodes[id].present) return 0;
    const struct list_node *n = &g->nodes[id];
    for (uint32_t i = 0; i < n->count; ++i) if (n->children[i] == raw) return 1;
    return 0;
}
static uint32_t list_focus_or_first(const struct list_generation *g, uint32_t parent, uint32_t desired) {
    int id = list_id(parent);
    if (id < 0 || !g->nodes[id].present) return 0;
    if (list_has_child(g, parent, desired)) return desired;
    return g->nodes[id].count ? g->nodes[id].children[0] : 0;
}
static void list_store32(uintptr_t address, uint32_t value) { memcpy((void *)address, &value, 4); }
static void list_store8(uintptr_t address, unsigned int value) {
    unsigned char byte = (unsigned char)value;
    memcpy((void *)address, &byte, 1);
}

static int list_lookup_context(void) {
    return list_ready && list_catalog_checked && catalog_valid && pilot_menu_observation.installed &&
           ui_thread() && (primary_lookup_scope() || pilot_menu_observation.screen == LIST_SCREEN ||
                           list_c_pin || list_p_pin || list_p_constructing);
}
static int menu_list_owns(uint32_t raw) {
    int id = list_id(raw);
    return id >= 0 && list_lookup_context() && list_current()->nodes[id].present;
}
static const void *menu_list_record(uint32_t raw) {
    if (!menu_list_owns(raw)) return 0;
    return raw == NEW_ITEM ? own_record : list_current()->nodes[list_id(raw)].record;
}
static const char *menu_list_text(uint32_t resource) {
    if (resource < LIST_RESOURCE_FIRST || resource >= LIST_RESOURCE_FIRST + (LIST_SLOTS + 1u) * 2u)
        return 0;
    uint32_t id = (resource - LIST_RESOURCE_FIRST) / 2u;
    /* An owned transport ID must NEVER fall through to Sony's resource bank. */
    if (!list_lookup_context() || !list_current()->nodes[id].present) {
        list_error(MENU_API_E_CONTEXT, "owned-text-outside-generation");
        return "[menu unavailable]";
    }
    struct list_node *node = &list_current()->nodes[id];
    return (resource - LIST_RESOURCE_FIRST) & 1u ? node->value : node->label;
}

/* Called under the caller's existing native model scope. No callbacks or locks. */
static int menu_list_validate_catalog(void *util) {
    list_catalog_checked = 0;
    if (!list_ready || pointer(base + 0x7602fc8) != 1425 || pointer(base + 0x7603118) != 1537)
        return 0;
    for (uint32_t i = 1; i <= LIST_SLOTS; ++i) {
        uint32_t raw = list_raw(i);
        if (original_alias(util, (int32_t)raw) != 0x602 ||
                original_reverse(util, (int32_t)raw) != 0xb81 ||
                original_normal(util, (int32_t)raw) != (const void *)(base + 0x597ee10)) {
            list_error(MENU_API_E_DUPLICATE, "native-raw-id-conflict");
            return 0;
        }
    }
    list_catalog_checked = 1;
    return 1;
}

/* Callbacks run here on the serialized owning UI boundary, never in a provider. */
static menu_api_status list_freeze(struct list_generation *g) {
    uint32_t queue[LIST_SLOTS + 1u] = {0};
    size_t read = 0, count = 1;
    list_scratch.callback_code = 0;
    memset(g, 0, sizeof *g);
    g->nodes[0].present = g->nodes[0].visible = g->nodes[0].enabled = 1;
    g->nodes[0].kind = MENU_API_GROUP;
    uint32_t entry=menu_example_entry_id();
    if (entry) {
        menu_api_info info;
        menu_api_status status=menu_example_describe(entry,&info);
        if (status!=MENU_API_OK) return status;
        if (!list_container(info.kind)) return MENU_API_E_TYPE;
        g->nodes[0].logical=entry; g->nodes[0].handle=info.handle; g->nodes[0].kind=info.kind;
    }
    strcpy(g->nodes[0].label, pilot_menu_title());
    /* Unfiltered enumeration closes metadata even for a currently hidden subtree. */
    while (read < count) {
        uint32_t parent = queue[read++];
        size_t definitions = 0;
        menu_api_status status = menu_example_definitions(g->nodes[parent].logical, list_definitions,
                                                         LIST_SLOTS, &definitions);
        if (status != MENU_API_OK) return status;
        if (definitions > LIST_SLOTS) return MENU_API_E_LIMIT;
        for (size_t r = 0; r < definitions; ++r) {
            const menu_api_info *info = &list_definitions[r];
            uint32_t slot = (uint32_t)info->handle;
            if (!slot || slot > LIST_SLOTS || !(info->handle >> 32) || !info->id ||
                    info->parent != g->nodes[parent].handle || g->nodes[slot].present)
                return MENU_API_E_STATE;
            struct list_node *node = &g->nodes[slot];
            node->logical = info->id; node->handle = info->handle; node->parent = list_raw(parent);
            node->kind = (uint32_t)info->kind; node->present = 1;
            memcpy(node->label, info->label, sizeof info->label);
            if (list_container(node->kind)) {
                if (count == LIST_SLOTS + 1u) return MENU_API_E_LIMIT;
                queue[count++] = slot;
            }
        }
    }
    /* Only reachable visible containers may run snapshot condition/value getters. */
    read = 0; count = 1; queue[0] = 0;
    while (read < count) {
        uint32_t parent = queue[read++];
        struct list_node *container = &g->nodes[parent];
        menu_api_status status = menu_example_children(container->logical, &list_scratch);
        if (status != MENU_API_OK) return status;
        if (list_scratch.container != container->handle || list_scratch.count > MENU_API_VISIBLE_LIMIT)
            return MENU_API_E_STATE;
        container->count = (uint32_t)list_scratch.count;
        for (size_t r = 0; r < list_scratch.count; ++r) {
            const menu_api_row *row = &list_scratch.rows[r];
            uint32_t slot = (uint32_t)row->info.handle;
            if (!slot || slot > LIST_SLOTS || !g->nodes[slot].present ||
                    g->nodes[slot].handle != row->info.handle || g->nodes[slot].logical != row->info.id ||
                    row->info.parent != container->handle || g->nodes[slot].visible)
                return MENU_API_E_STATE;
            struct list_node *node = &g->nodes[slot];
            node->visible = 1; node->enabled = row->enabled;
            node->has_value = !list_container(node->kind) && node->kind != MENU_API_ACTION;
            node->number = row->value.number;
            memcpy(node->value, row->value.text, sizeof node->value);
            memcpy(node->label,row->info.label,sizeof row->info.label);
            container->children[r] = list_raw(slot);
            if (list_container(node->kind)) {
                if (count == LIST_SLOTS + 1u) return MENU_API_E_LIMIT;
                queue[count++] = slot;
            }
        }
    }
    if (!g->nodes[0].count) return MENU_API_E_HIDDEN;
    for (uint32_t slot = 0; slot <= LIST_SLOTS; ++slot) {
        struct list_node *node = &g->nodes[slot];
        if (!node->present) continue;
        memcpy(node->record, own_record, sizeof node->record);
        list_store32((uintptr_t)node->record, list_resource(slot, 0));
        list_store32((uintptr_t)node->record + 0x20, list_container(node->kind) ? LIST_SCREEN : 0x4b7u);
    }
    return MENU_API_OK;
}
static int list_same_membership(const struct list_generation *a, const struct list_generation *b) {
    for (uint32_t i = 0; i <= LIST_SLOTS; ++i) {
        const struct list_node *x = &a->nodes[i], *y = &b->nodes[i];
        if (x->present != y->present || x->handle != y->handle || x->logical != y->logical ||
                x->visible != y->visible || x->parent != y->parent || x->kind != y->kind ||
                x->count != y->count || memcmp(x->children, y->children, sizeof x->children)) return 0;
    }
    return 1;
}
static void list_apply_values(struct list_generation *a, const struct list_generation *b) {
    for (uint32_t i = 0; i <= LIST_SLOTS; ++i) {
        a->nodes[i].enabled = b->nodes[i].enabled;
        a->nodes[i].has_value = b->nodes[i].has_value;
        a->nodes[i].number = b->nodes[i].number;
        memcpy(a->nodes[i].label, b->nodes[i].label, sizeof a->nodes[i].label);
        memcpy(a->nodes[i].value, b->nodes[i].value, sizeof a->nodes[i].value);
    }
    if (list_edit_id && list_preview.has_value) {
        a->nodes[list_edit_id].number = list_preview.value.number;
        memcpy(a->nodes[list_edit_id].value, list_preview.value.text, sizeof list_preview.value.text);
    }
}
static int list_controller_valid(void *controller) {
    uintptr_t c = (uintptr_t)controller;
    return list_lookup_context() && list_c_pin && list_c_generation == list_current()->serial && controller == list_c &&
           c == base + CONTROLLER_ARENA && protection(c, 0x1a8) >= 0 &&
           pointer(c) == base + LIST_C_VTABLE && u32(c + 0x178) == LIST_SCREEN &&
           u32(base + CURRENT_ITEM) == list_parent();
}
static int list_presenter_valid(void *presenter) {
    uintptr_t p = (uintptr_t)presenter;
    return presenter && presenter == list_p && (list_p_pin || list_p_constructing) && list_c_pin &&
           list_p_generation == list_current()->serial && list_c_generation == list_current()->serial &&
           protection(p, 0x2978) >= 0 && pointer(p) == base + LIST_P_VTABLE &&
           pointer(base + PRESENTER_TABLE) == p && pointer(p + 0x28) == (uintptr_t)list_view &&
           u32(p + 0xe70) == list_parent();
}
static uint32_t list_focused(void) {
    const uint32_t *raw = ((const uint32_t *(*)(void))(base + 0x21806b0))();
    return (uintptr_t)raw == base + 0x7e68b20 ? *raw : 0;
}
static int list_manager_valid(void) {
    uintptr_t manager = pointer((uintptr_t)list_c + 0x180);
    return protection(manager, 0x3b1) >= 0 && pointer(manager) == base + 0x5b0c480 &&
           pointer(base + 0x5b0c498) == base + 0x1e77dd8 &&
           pointer(base + 0x5b0c4a0) == base + 0x1ca4c0c && u32(manager + 0x1e8) == 0x4b7;
}
static void list_observe(void) {
    pilot_menu_list_observation.controller = (uintptr_t)list_c;
    pilot_menu_list_observation.presenter = (uintptr_t)list_p;
    pilot_menu_list_observation.parent = list_depth ? list_parent() : 0;
    pilot_menu_list_observation.depth = list_depth;
    pilot_menu_list_observation.generation = list_current()->serial;
    pilot_menu_list_observation.editing = list_edit_id;
    pilot_menu_list_observation.actions = menu_example_action_count();
    if (list_depth) {
        int parent = list_id(list_parent());
        if (parent < 0) {
            list_error(MENU_API_E_STATE, "observe-parent");
            return;
        }
        pilot_menu_list_observation.count = list_current()->nodes[parent].count;
        if (list_c_pin) {
            uint32_t focused = u32(base + 0x7e68b20);
            if (list_has_child(list_current(), list_parent(), focused)) {
                pilot_menu_list_observation.focused = focused;
                const struct list_node *node = &list_current()->nodes[list_id(focused)];
                if (node->has_value) pilot_menu_list_observation.value = node->number;
            }
        }
    }
}
static int list_stage(uint32_t parent, uint32_t desired, unsigned int depth) {
    struct list_generation *next = &list_generations[list_active ^ 1u];
    uint32_t focus = list_focus_or_first(next, parent, desired);
    if (!focus || !depth || depth > LIST_SLOTS + 1u || list_pending || list_closing) {
        menu_api_status status = !focus ? MENU_API_E_HIDDEN :
            (!depth || depth > LIST_SLOTS + 1u) ? MENU_API_E_LIMIT : MENU_API_E_BUSY;
        list_status(status, 0, "reconstruct-request-rejected"); return -1;
    }
    if (!list_manager_valid()) { list_error(MENU_API_E_STATE, "reconstruct-manager"); return -1; }
    next->serial = ++list_next_serial;
    list_pending_frame = (struct list_frame){parent, parent, focus, next->serial};
    list_pending_depth = depth;
    list_pending = 1;
    /* Sentinel4b7 keeps the original outer2ca restore tuple; W4/W5 are not force bits. */
    const uint32_t screen = LIST_SCREEN, restore = 0x4b7;
    uintptr_t manager = pointer((uintptr_t)list_c + 0x180);
    ((list_open_fn)(base + 0x1e77dd8))((void *)manager, &screen, &screen, &restore, 1, 0);
    trace("MENU_API_LIST_STAGE ", "parent=%#x focus=%#x generation=%u depth=%u original-open=1", parent, focus,
          next->serial, depth);
    return 0;
}
static int list_reconcile(void) {
    if (list_pending || list_closing) return -1;
    struct list_generation *next = &list_generations[list_active ^ 1u];
    menu_api_status status = list_freeze(next);
    if (status != MENU_API_OK) { list_status(status, list_scratch.callback_code, "refresh-snapshot"); return -1; }
    if (!list_same_membership(list_current(), next)) {
        if (list_edit_id) {
            status = menu_example_edit_cancel();
            if (status != MENU_API_OK) { list_status(status, 0, "reconstruct-cancel-preview"); return -1; }
            list_edit_id = 0;
        }
        if (!list_catalog_checked) return -1;
        uint32_t focus = list_focused();
        if (list_stage(list_parent(), focus, list_depth)) return -1;
        return 1;
    }
    list_apply_values(list_current(), next);
    list_observe();
    return 0;
}
static int list_refresh(void) {
    int changed = list_reconcile();
    if (changed) return changed;
    if (!list_presenter_valid(list_p)) { list_error(MENU_API_E_CONTEXT, "refresh-presenter"); return -1; }
    ((update_fn)(base + 0x1d1e3c8))(list_p, 1);
    ++pilot_menu_list_observation.refreshes;
    return 0;
}

static uint32_t list_children(void *self, uint32_t parent, uint32_t *out, int32_t capacity) {
    int id = list_id(parent);
    if (id < 0 || !list_lookup_context() || !list_depth || parent != list_parent() ||
            (!list_c_pin && !list_busy && !list_p_pin) || !list_current()->nodes[id].present)
        return list_original_children(self, parent, out, capacity);
    if (!out || capacity <= 0) return 0;
    list_count_inflight(1);
    const struct list_node *n = &list_current()->nodes[id];
    uint32_t count = n->count < (uint32_t)capacity ? n->count : (uint32_t)capacity;
    memcpy(out, n->children, count * sizeof *out);
    list_count_inflight(-1);
    return count;
}
static uint32_t list_row_value(void *self, uint64_t raw, void *payload) {
    /* Preserve the native no-method payload construction and every fallback bit. */
    uint32_t original = list_original_value(self, raw, payload);
    if (raw > UINT32_MAX || !menu_list_owns((uint32_t)raw) || !list_depth ||
            !list_has_child(list_current(), list_parent(), (uint32_t)raw)) return original;
    int id = list_id((uint32_t)raw);
    return list_current()->nodes[id].has_value ? list_resource((uint32_t)id, 1) : original;
}

static void list_publish_parent(uint32_t parent) {
    list_native_scope scope;
    ((list_scope_fn)(base + 0x21809e0))(&scope);
    list_native_writer writer = ((list_make_writer_fn)(base + 0x21807d0))(scope.opaque[1]);
    ((list_write_parent_fn)(base + 0x2181bb0))(&writer, &parent);
    ((list_notify_fn)(base + 0x2180d50))(&writer);
    ((list_scope_fn)(base + 0x2180a04))(&scope);
}
static int list_select_focus(void *controller, uint32_t raw) {
    ((void (*)(void *, uint32_t))(base + 0x1a8a844))((unsigned char *)controller + 0x1a0, raw);
    list_native_scope scope;
    ((list_scope_fn)(base + 0x21809e0))(&scope);
    list_native_writer writer = ((list_make_writer_fn)(base + 0x2180730))(scope.opaque[1]);
    ((list_write_focus_fn)(base + 0x2180fe0))(&writer, (const uint32_t *)(base + 0x7e68b50), 0x399);
    ((list_notify_fn)(base + 0x2180bc0))(&writer);
    ((list_scope_fn)(base + 0x2180a04))(&scope);
    if (u32(base + 0x7e68b50) != raw || list_focused() != raw) {
        list_error(MENU_API_E_READBACK, "raw-focus-readback"); return -1;
    }
    pilot_menu_list_observation.focused = raw;
    return 0;
}
static void list_construction_refused(menu_api_status code,const char *operation) __attribute__((noreturn));
static void list_construction_refused(menu_api_status code,const char *operation) {
    list_error(code,operation);
    trace("MENU_API_LIST_FATAL ","operation=%s guest-ap-exit=125 original-factory-has-no-error-result=1",operation);
    _exit(125);
}
static void *list_c_factory(int32_t layer, void *manager, void *arg2, void *arg3, int32_t arg4, int32_t orientation) {
    if (!list_ready || !ui_thread() || layer != 0 || !catalog_valid || !list_catalog_checked || list_c_pin || list_p_pin || list_inflight)
        return list_original_c_factory(layer, manager, arg2, arg3, arg4, orientation);
    if (!list_pending && u32(base + CURRENT_ITEM) != NEW_ITEM)
        return list_original_c_factory(layer, manager, arg2, arg3, arg4, orientation);
    list_release_semantics_if_detached();
    if (list_semantic_pin)
        list_construction_refused(MENU_API_E_STATE,"previous-semantic-pin-not-detached");
    menu_api_status acquired = menu_example_consumer_acquire();
    if (acquired != MENU_API_OK)
        list_construction_refused(acquired,"semantic-acquire-before-construction");
    list_semantic_pin = 1;
    if (!list_pending) {
        /* No old C/P exists: registration may have changed while this list was closed. */
        struct list_generation *next = &list_generations[list_active ^ 1u];
        menu_api_status status = list_freeze(next);
        if (status != MENU_API_OK) {
            /* This acquire has not reached any native constructor. */
            list_release_semantics_if_detached();
            list_construction_refused(status,"entry-snapshot-before-construction");
        }
        next->serial = ++list_next_serial;
        list_active ^= 1u;
    }
    if (list_pending) {
        list_publish_parent(list_pending_frame.parent);
        if (u32(base + CURRENT_ITEM) != list_pending_frame.parent) {
            list_release_semantics_if_detached();
            list_construction_refused(MENU_API_E_READBACK,"model392-readback");
        }
        list_active ^= 1u;
        list_depth = list_pending_depth;
        list_frames[list_depth - 1u] = list_pending_frame;
        list_pending = 0;
    } else {
        if (u32(base + CURRENT_ITEM) != NEW_ITEM) {
            list_release_semantics_if_detached();
            list_construction_refused(MENU_API_E_STALE,"entry-parent-changed-before-construction");
        }
        uint32_t focus = list_focus_or_first(list_current(), NEW_ITEM, 0);
        if (!focus) {
            list_release_semantics_if_detached();
            list_construction_refused(MENU_API_E_HIDDEN,"entry-has-no-focus");
        }
        list_depth = 1;
        list_frames[0] = (struct list_frame){NEW_ITEM, NEW_ITEM, focus, list_current()->serial};
        list_closing = 0;
        list_enter_latch = 1;
    }
    list_busy = 1;
    void *controller = list_original_c_factory(layer, manager, arg2, arg3, arg4, orientation);
    list_busy = 0;
    if ((uintptr_t)controller != base + CONTROLLER_ARENA || pointer((uintptr_t)controller) != base + LIST_C_VTABLE ||
            pointer((uintptr_t)controller + 0x180) != (uintptr_t)manager) {
        /* An unknown constructed owner may still borrow this generation: keep it pinned. */
        list_construction_refused(MENU_API_E_CONTEXT,"controller-factory-owner");
    }
    list_c = controller; list_c_pin = 1; list_c_generation = list_current()->serial;
    list_observe();
    return controller;
}
static void list_enter(void *controller, const uint32_t *previous) {
    list_original_enter(controller, previous);
    if (!list_controller_valid(controller) || list_pending || list_closing) return;
    struct list_frame *frame = &list_frames[list_depth - 1u];
    if (!list_has_child(list_current(), frame->parent, frame->focused)) {
        list_error(MENU_API_E_STATE, "enter-focus-membership"); return;
    }
    list_select_focus(controller, frame->focused);
    ++pilot_menu_list_observation.enters;
    list_observe();
}
static void *list_p_factory(int32_t layer, void *view, void *caution, int32_t constructor_layer, int32_t screen, int32_t orientation) {
    if (layer != 0 || screen != (int32_t)LIST_SCREEN || !ui_thread() || !list_c_pin || list_p_pin ||
            !list_controller_valid(list_c))
        return list_original_p_factory(layer, view, caution, constructor_layer, screen, orientation);
    list_p = (void *)pointer(base + PRESENTER_TABLE);
    list_view = view; list_p_constructing = 1; list_p_generation = list_current()->serial;
    void *presenter = list_original_p_factory(layer, view, caution, constructor_layer, screen, orientation);
    if (presenter != list_p || !list_presenter_valid(presenter)) {
        list_error(MENU_API_E_CONTEXT, "presenter-factory-owner");
    } else {
        list_p_pin = 1;
        list_pool = (void *)pointer((uintptr_t)presenter + 0x30);
    }
    list_p_constructing = 0;
    list_observe();
    return presenter;
}
static void list_cancel_edit(void);
static void list_c_dtor(void *controller) {
    int own = list_c_pin && controller == list_c && ui_thread();
    if (own) {
        list_count_inflight(1);
        if (list_edit_id) list_cancel_edit();
    }
    list_original_c_dtor(controller);
    if (own) {
        list_c_pin = 0; list_c_generation = 0; list_c = 0;
        ++pilot_menu_list_observation.controller_dtors;
        list_count_inflight(-1);
        list_observe();
        list_release_semantics_if_detached();
    }
}
static void list_p_dtor(void *presenter) {
    int own = list_p_pin && presenter == list_p && ui_thread();
    if (own) list_count_inflight(1);
    /* Native complete destructor returns only after view+18 unbind. */
    list_original_p_dtor(presenter);
    if (own) {
        list_p_pin = 0; list_p_generation = 0; list_p = list_view = list_pool = 0;
        ++pilot_menu_list_observation.presenter_dtors;
        list_count_inflight(-1);
        list_observe();
        list_release_semantics_if_detached();
    }
}

static void *list_parameter(uintptr_t pool, unsigned int index, uint32_t tag, size_t size, uintptr_t accessor) {
    uintptr_t entry = pool + index * 16u;
    if (protection(entry, 16) < 0 || u32(entry) != tag) return 0;
    void *param = (void *)pointer(entry + 8);
    int mode = protection((uintptr_t)param, size);
    if (mode < 0 || (mode & (PROT_READ | PROT_WRITE)) != (PROT_READ | PROT_WRITE)) return 0;
    if (accessor) {
        void *entry_pointer = (void *)entry;
        if (((list_param_fn)accessor)(&entry_pointer) != param) return 0;
    }
    return param;
}
static void list_header(void *presenter) {
    if (!list_ready || !ui_thread() || !list_depth || protection((uintptr_t)presenter, 0xe74) < 0 ||
            u32((uintptr_t)presenter + 0xe70) != list_parent()) {
        ((void (*)(void *))(base + 0x2182670))(presenter);
        return;
    }
    /* Never execute the secondary-cache prefix for an owned parent, even on error. */
    if (!list_presenter_valid(presenter) || (list_pool && pointer((uintptr_t)presenter + 0x30) != (uintptr_t)list_pool)) {
        list_error(MENU_API_E_CONTEXT, "header-owner"); return;
    }
    uintptr_t p = (uintptr_t)presenter, pool = pointer(p + 0x30);
    uintptr_t header = (uintptr_t)list_parameter(pool, 6, 0x55, 0x322, base + 0x20b84f4);
    uintptr_t numbers = (uintptr_t)list_parameter(pool, 7, 2, 0x32c, base + 0x1a96908);
    uintptr_t footer = (uintptr_t)list_parameter(pool, 5, 0x18, 0x34, 0);
    uintptr_t touch = (uintptr_t)list_parameter(pool, 4, 0xd, 0x38, 0);
    uintptr_t up = (uintptr_t)list_parameter(pool, 10, 0, 0x2d, base + 0x1ac45ec);
    uintptr_t down = (uintptr_t)list_parameter(pool, 9, 0, 0x2d, base + 0x1ac45ec);
    uintptr_t rows = (uintptr_t)list_parameter(pool, 11, 0x5b, 0x39b8, base + 0x1b57ee8);
    if (!header || !numbers || !footer || !touch || !up || !down || !rows) {
        list_error(MENU_API_E_CONTEXT, "header-native-pool-tags"); return;
    }
    uint32_t parent = list_parent();
    const struct list_generation *g = list_current();
    const struct list_node *node = &g->nodes[list_id(parent)];
    uint32_t logical = ((uint32_t (*)(void *))(base + 0x1ac5800))((unsigned char *)presenter + 0x2320);
    if (logical < 12 || logical - 12 >= node->count) {
        list_error(MENU_API_E_RANGE, "header-native-focus"); return;
    }
    list_count_inflight(1);
    uint32_t page = (logical - 12) / 7 + 1;
    uint32_t pages = ((uint32_t (*)(uint32_t, int32_t))(base + 0x1b6c4d4))(parent, 7);
    if (pages != (node->count + 6) / 7) {
        list_error(MENU_API_E_READBACK, "native-page-count");
        list_count_inflight(-1); return;
    }
    list_dict_fn dict = (list_dict_fn)(base + 0x183bc74);
    uintptr_t current_number = (uintptr_t)dict((void *)(numbers + 0x58), 0);
    uintptr_t total_number = (uintptr_t)dict((void *)(numbers + 0x58), 1);
    list_store32(current_number, 2); list_store32(current_number + 0x88, page);
    list_store32(total_number, 2); list_store32(total_number + 0x88, pages);
    /* Plain private captions have no placeholders; retain a native numeric dictionary. */
    void *element = dict((void *)(header + 0x3c), 0);
    ((list_number_fn)(base + 0x1b6cdc0))(0, 0, (int32_t)node->count, (int32_t)(page - 1), element);
    uint32_t group = 0, page_id = 0, at = (uint32_t)list_id(parent);
    for (uint32_t steps = 0; at && steps < LIST_SLOTS; ++steps) {
        if (g->nodes[at].kind == MENU_API_GROUP) group = at;
        if (!page_id && g->nodes[at].kind == MENU_API_PAGE) page_id = at;
        int next = list_id(g->nodes[at].parent);
        if (next < 0) break;
        at = (uint32_t)next;
    }
    uint32_t captions[3] = {list_resource(group, 0), page_id ? list_resource(page_id, 0) : UINT32_MAX,
                            list_resource((uint32_t)list_id(parent), 0)};
    if (u32(p + 0x13a8) == 1) { captions[0] = captions[1]; captions[1] = captions[2]; captions[2] = UINT32_MAX; }
    for (unsigned int i=1;i<3;++i)
        for (unsigned int j=0;j<i;++j)
            if (captions[i]==captions[j]) captions[i]=UINT32_MAX;
    for (unsigned int i = 0; i < 3; ++i) list_store32(header + 0x30 + i * 4, captions[i]);
    list_store8(header + 0x321, u32(p + 0xe88) == 0);
    list_store32(numbers + 0x48, *(unsigned char *)(p + 0x1018) ? 0x207a001 : 0x207a000);
    list_store32(footer + 0x30, 6); list_store32(touch + 0x34, 0x2010006);
    list_store8(up + 0x2c, pages > 1 && page > 1);
    list_store8(down + 0x2c, pages > 1 && page < pages);
    list_store32(rows + 0x3994, 7); list_store8(rows + 0x3998, u32(p + 0xe88) == 0);
    list_store32(rows + 0x3988, 12); list_store32(rows + 0x398c, logical - 12);
    ((void (*)(void *))(base + 0x2182558))(presenter);
    for (uint32_t i=0;i<node->count;++i) {
        int child=list_id(node->children[i]);
        if (child<1 || !g->nodes[child].present) {
            list_error(MENU_API_E_STATE,"gray-row-membership");
            list_count_inflight(-1);
            return;
        }
    }
    for (uint32_t i=0;i<node->count;++i) {
        const struct list_node *child=&g->nodes[list_id(node->children[i])];
        list_store8(rows+0x3960+i,!child->enabled);
    }
    list_count_inflight(-1);
}

static void menu_list_input_release(unsigned int key, unsigned int status, unsigned long tid) {
    if (!list_ready || status || tid != pilot_menu_observation.ui_tid || !ui_thread()) return;
    if (key == 0x1ac) list_enter_latch = 0;
    if (key == 0x1ab) list_menu_latch = 0;
}
static void list_cancel_edit(void) {
    if (!list_edit_id) return;
    menu_api_status status = menu_example_edit_cancel();
    list_status(status, 0, "preview-cancel");
    if (status == MENU_API_OK) { list_edit_id = 0; memset(&list_preview, 0, sizeof list_preview); }
}
static uint64_t list_handle_enter(void) {
    if (list_enter_latch) return 1;
    list_enter_latch = 1;
    /* Active additions/reused slots must be reconstructed before any callback. */
    if (list_reconcile()) return 1;
    uint32_t raw = list_focused();
    if (!list_has_child(list_current(), list_parent(), raw)) {
        list_status(MENU_API_E_STALE, 0, "enter-focus-membership"); return 1;
    }
    int id = list_id(raw);
    struct list_node *node = &list_current()->nodes[id];
    pilot_menu_list_observation.focused = raw;
    if (!node->enabled) { list_status(MENU_API_E_DISABLED, 0, "enter-disabled"); return 1; }
    list_frames[list_depth - 1u].focused = raw;
    menu_api_status status = MENU_API_OK;
    menu_api_result result = {0};
    if (list_edit_id) {
        if (list_edit_id != (unsigned int)id) { list_status(MENU_API_E_STALE, 0, "editor-focus-changed"); return 1; }
        status = menu_example_edit_commit(&result);
        if (status == MENU_API_OK) list_edit_id = 0;
    } else if (list_container(node->kind)) {
        struct list_generation *next = &list_generations[list_active ^ 1u];
        status = list_freeze(next);
        if (status == MENU_API_OK && list_depth < LIST_SLOTS + 1u) {
            uint32_t focus = list_focus_or_first(next, raw, 0);
            if (!focus) status = MENU_API_E_HIDDEN;
            else if (list_stage(raw, focus, list_depth + 1u)) return 1;
        } else if (status == MENU_API_OK) status = MENU_API_E_LIMIT;
        list_status(status, 0, "open-container");
        return 1;
    } else if (node->kind == MENU_API_ACTION) {
        status = menu_example_invoke(node->logical, &result);
    } else if (node->kind == MENU_API_READONLY) {
        status = menu_example_value(node->logical, &result);
        trace("MENU_API_LIST_READONLY ", "raw=%#x length=%zu original-list=1", raw, strlen(result.value.text));
    } else {
        status = menu_example_edit_begin(node->logical, &list_preview);
        if (status == MENU_API_OK) {
            list_edit_id = (unsigned int)id;
            if (node->kind == MENU_API_BOOL) {
                status = menu_example_edit_move(list_preview.value.number ? -1 : 1, &list_preview);
                if (status == MENU_API_OK) status = menu_example_edit_commit(&result);
                if (status == MENU_API_OK) list_edit_id = 0;
                else list_cancel_edit();
            }
        }
    }
    list_status(status, result.callback_code, "typed-enter");
    trace("MENU_API_LIST_RESULT ", "raw=%#x logical=%u status=%u callback=%d fresh=%u has-value=%u",
          raw, node->logical, (unsigned int)status, result.callback_code, result.fresh, result.has_value);
    if (result.has_value) pilot_menu_list_observation.value = result.value.number;
    list_refresh();
    list_observe();
    return 1;
}
static uint64_t list_handle_menu(void) {
    if (list_menu_latch) return 1;
    list_menu_latch = 1;
    list_status(MENU_API_OK, 0, "menu");
    if (list_edit_id) { list_cancel_edit(); list_refresh(); return 1; }
    if (list_depth > 1) {
        struct list_generation *next = &list_generations[list_active ^ 1u];
        menu_api_status status = list_freeze(next);
        const struct list_frame *back = &list_frames[list_depth - 2u];
        if (status != MENU_API_OK) list_status(status, list_scratch.callback_code, "back-snapshot");
        else list_stage(back->parent, back->focused, list_depth - 1u);
    } else if (list_manager_valid()) {
        list_closing = 1;
        list_root_restore = 1;
        ((void (*)(void *))(base + 0x21809c8))(list_c);
    } else list_error(MENU_API_E_STATE, "outer-close-preflight");
    return 1;
}
static uint64_t list_dispatch(void *controller, const void *event, uint32_t raw) {
    if (!event || protection((uintptr_t)event, 16) < 0 || u32((uintptr_t)event) > 5 ||
            !list_controller_valid(controller) || !list_presenter_valid(list_p))
        return list_original_dispatch(controller, event, raw);
    if (list_busy) {
        if (raw == ENTER_ON || raw == MENU_ON) {
            list_status(MENU_API_E_BUSY, 0, "reentrant-owned-input"); return 1;
        }
        return list_original_dispatch(controller, event, raw);
    }
    list_busy = 1;
    list_count_inflight(1);
    uint32_t previous_event = pilot_menu_list_observation.scope_event;
    pilot_menu_list_observation.scope_event = raw;
    uint64_t result;
    if (raw == ENTER_OFF) { list_enter_latch = 0; result = list_original_dispatch(controller, event, raw); }
    else if (raw == 0x02010010u) { list_menu_latch = 0; result = list_original_dispatch(controller, event, raw); }
    else if (list_pending || list_closing) {
        /* No second request/action while original process_event consumes this one. */
        result = raw == ENTER_ON || raw == MENU_ON ? 1 : list_original_dispatch(controller, event, raw);
    } else if (raw == ENTER_ON) result = list_handle_enter();
    else if (raw == MENU_ON) result = list_handle_menu();
    else if (list_edit_id && (raw == LIST_LEFT_ON || raw == LIST_RIGHT_ON ||
                             raw == LIST_LEFT_REPEAT || raw == LIST_RIGHT_REPEAT)) {
        menu_api_status status = menu_example_edit_move((raw == LIST_RIGHT_ON || raw == LIST_RIGHT_REPEAT) ? 1 : -1, &list_preview);
        list_status(status, list_preview.callback_code, "preview-move");
        list_refresh(); list_observe(); result = 1;
    } else {
        if (list_edit_id && (raw == 0x02010000u || raw == 0x02010003u ||
                             raw == 0x02010002u || raw == 0x02010005u)) {
            list_cancel_edit(); list_refresh();
        }
        result = list_pending ? 1 : list_original_dispatch(controller, event, raw);
        uint32_t focused = list_focused();
        if (list_has_child(list_current(), list_parent(), focused)) {
            list_frames[list_depth - 1u].focused = focused;
            pilot_menu_list_observation.focused = focused;
        }
    }
    pilot_menu_list_observation.scope_event = previous_event;
    list_count_inflight(-1);
    list_busy = 0;
    list_release_semantics_if_detached();
    return result;
}
static void menu_list_root_entered(void *controller) {
    uintptr_t c = (uintptr_t)controller;
    if (!list_root_restore || list_pending || list_c_pin || list_p_pin || list_semantic_pin || !ui_thread() || list_inflight ||
            c != base + CONTROLLER_ARENA || protection(c, 0x1a8) < 0 || u32(c + 0x178) != 0x2ca ||
            !catalog_valid || pilot_menu_observation.primary_rows != 8) return;
    ((void (*)(void *, uint32_t))(base + 0x1a8a844))((unsigned char *)controller + 0x1a0, NEW_ITEM);
    uint32_t selected = ((uint32_t (*)(void))(base + 0x21796f8))();
    if (selected != NEW_ITEM) { list_error(MENU_API_E_READBACK, "primary-selected-readback"); return; }
    pilot_menu_list_observation.focused = selected;
    list_root_restore = 0; list_depth = 0; list_closing = 0;
    list_status(MENU_API_OK, 0, "primary-root-restored");
    list_observe();
    trace("MENU_API_LIST_ROOT ", "focus=%#x original-enter-completed=1 primary-getter=1", selected);
}

/* Every guard passes before the first queued patch; only main applies/rolls back. */
struct list_slot_patch { uintptr_t address, expected, replacement; };
static int menu_list_prepare(void) {
    if (list_ready || list_veneer) return -1;
    list_original_c_factory = (list_c_factory_fn)(base + 0x1cf7840);
    list_original_p_factory = (factory_fn)(base + 0x1d1e1d8);
    list_original_children = (list_children_fn)(base + 0x1b5ea88);
    list_original_value = (list_value_fn)(base + 0x1b598b0);
    list_original_enter = (list_enter_fn)(base + 0x2181a38);
    list_original_c_dtor = (list_dtor_fn)(base + 0x21824b4);
    list_original_p_dtor = (list_dtor_fn)(base + 0x1c335d0);
    const struct list_slot_patch slots[] = {
        {0x597d850, 0x1b5ea88, (uintptr_t)list_children},
        {0x597d620, 0x1b598b0, (uintptr_t)list_row_value},
        {0x5208438, 0x1cf7840, (uintptr_t)list_c_factory},
        {0x5208430, 0x1d1e1d8, (uintptr_t)list_p_factory},
        {0x5b13600, 0x2181a38, (uintptr_t)list_enter},
        {0x5b135e0, 0x21824b4, (uintptr_t)list_c_dtor},
        {0x5afaea0, 0x1c335d0, (uintptr_t)list_p_dtor}
    };
    const uintptr_t calls[] = {0x1b6bec4, 0x1b6ae28, 0x1d1e448};
    const uintptr_t targets[] = {0x1b5ea88, 0x1b598b0, 0x2182670};
    const uintptr_t wrappers[] = {(uintptr_t)list_children, (uintptr_t)list_row_value, (uintptr_t)list_header, (uintptr_t)list_dispatch};
    uint32_t expected[3], replacement[3], entry_branch, return_branch;
    for (unsigned int i = 0; i < sizeof slots / sizeof slots[0]; ++i)
        if (protection(base + slots[i].address, 8) < 0 || pointer(base + slots[i].address) != base + slots[i].expected) return -1;
    for (unsigned int i = 0; i < 3; ++i)
        if (branch_word(base + calls[i], base + targets[i], &expected[i]) ||
                protection(base + calls[i], 4) < 0 || u32(base + calls[i]) != expected[i]) return -1;
    if (protection(base + LIST_DISPATCH, 12) < 0 || u32(base + LIST_DISPATCH) != 0xa9bc7bfdu ||
            u32(base + LIST_DISPATCH + 4) != 0x910003fdu || u32(base + LIST_DISPATCH + 8) != 0xa90153f3u ||
            protection(base + 0x3992a38, 32) < 0 || u32(base + 0x3992a38) != 0x00020004u ||
            u32(base + 0x3992a3c) != 0x00060005u || u32(base + 0x3992a40) != 0x00070002u ||
            u32(base + 0x3992a50) != 0x0002000au || u32(base + 0x3992a54) != 0x000c000bu) return -1;
    const uint32_t key_records[6][4]={
        {2,0x02010000,0x02010002,0x02010001},{6,0x02010003,0x02010005,0x02010004},
        {4,LIST_LEFT_ON,LIST_LEFT_REPEAT,0x02010007},{0x16,LIST_RIGHT_ON,LIST_RIGHT_REPEAT,0x0201000a},
        {0x14,ENTER_ON,0x0201000e,ENTER_OFF},{0x13,MENU_ON,0x20000001,0x02010010}
    };
    if (protection(base+0x5bc2b60,sizeof key_records)<0 ||
            memcmp((const void *)(base+0x5bc2b60),key_records,sizeof key_records)) return -1;
    /* Main already guarded all null normal callbacks. A future template is not accepted silently. */
    const unsigned int nulls[] = {0x78, 0x80, 0x98, 0xa8, 0xb0};
    for (unsigned int i = 0; i < sizeof nulls / sizeof nulls[0]; ++i)
        if (pointer((uintptr_t)own_record + nulls[i])) return -1;
    if (u32((uintptr_t)own_record + 0x20) != INFO_SCREEN ||
            pointer(base + 0x5afaeb0) != base + 0x1d1e3c8 ||
            pointer(base + 0x5b0c498) != base + 0x1e77dd8 || pointer(base + 0x5b0c4a0) != base + 0x1ca4c0c)
        return -1;
    menu_api_status status = menu_example_initialize();
    if (status != MENU_API_OK) { list_error(status, "example-initialize"); return -1; }
    status = list_freeze(&list_generations[0]);
    if (status != MENU_API_OK) { list_error(status, "prepare-snapshot"); return -1; }
    list_veneer = near_page(base + LIST_DISPATCH);
    if (!list_veneer) return -1;
    uintptr_t page = (uintptr_t)list_veneer;
    const uint32_t jump[2] = {0x58000050u, 0xd61f0200u};
    for (unsigned int i = 0; i < 4; ++i) {
        memcpy((void *)(page + i * 16), jump, sizeof jump);
        memcpy((void *)(page + i * 16 + 8), &wrappers[i], 8);
    }
    if (branch_word(page + 68, base + LIST_DISPATCH + 4, &return_branch) ||
            branch_word(base + LIST_DISPATCH, page + 48, &entry_branch)) goto reject;
    return_branch &= ~0x80000000u;
    entry_branch &= ~0x80000000u;
    list_store32(page + 64, 0xa9bc7bfdu);
    list_store32(page + 68, return_branch);
    for (unsigned int i = 0; i < 3; ++i)
        if (branch_word(base + calls[i], page + i * 16, &replacement[i])) goto reject;
    clear_instructions(page, 72);
    if (mprotect(list_veneer, 4096, PROT_READ | PROT_EXEC) != 0) goto reject;
    list_original_dispatch = (list_dispatch_fn)(page + 64);
    for (unsigned int i = 0; i < sizeof slots / sizeof slots[0]; ++i)
        if (add_patch(base + slots[i].address, base + slots[i].expected, slots[i].replacement, 8, 0)) return -1;
    for (unsigned int i = 0; i < 3; ++i)
        if (add_patch(base + calls[i], expected[i], replacement[i], 4, 1)) return -1;
    if (add_patch(base + LIST_DISPATCH, 0xa9bc7bfdu, entry_branch, 4, 1)) return -1;
    list_active = 0; list_next_serial = 1; list_generations[0].serial = 1;
    list_store32((uintptr_t)own_record + 0x20, LIST_SCREEN);
    list_ready = 1;
    pilot_menu_list_observation.prepared = 1;
    trace("MENU_API_LIST_PREPARED ", "patches=11 capacity=20 raw=%#x..%#x original-dispatch-trampoline=1", LIST_RAW_FIRST,
          LIST_RAW_FIRST + LIST_SLOTS - 1u);
    return 0;
reject:
    munmap(list_veneer, 4096); list_veneer = 0;
    return -1;
}
static void menu_list_release_unreferenced(void) {
    /* Caller has verified central rollback; live borrowed consumers still pin us. */
    if (list_c_pin || list_p_pin || list_p_constructing || list_inflight || list_pending || list_edit_id || list_semantic_pin) {
        list_status(MENU_API_E_BUSY, 0, "release-live-consumers"); return;
    }
    if (list_veneer) { munmap(list_veneer, 4096); list_veneer = 0; }
    list_ready = list_catalog_checked = 0;
    pilot_menu_list_observation.prepared = 0;
}
