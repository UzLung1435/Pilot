#define _GNU_SOURCE
#include "../../patch/pilot.h"
#include "pilot_ui.h"

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Declare module model compatibility */
PILOT_DECLARE_MODULE_MODEL();

#define PILOT_MENU_TITLE_DEFAULT "Pilot"
#define PILOT_UI_DESC_STRING \
    "An a7c2 Firmware AP module trying to provide\n" \
    "an auto reload RAM Patch framework without\n" \
    "Jiritsu USB connection."

#define PILOT_GROUP_ID 0x535u
#define PILOT_ITEM_ID_BASE 0x1F00u
#define PILOT_SUBITEM_ID_BASE 0x1F10u
#define PILOT_LIST_SCREEN_ID 0x293u
#define PILOT_DETAIL_SCREEN_ID 0x1C3u
#define SCREEN_REP_MENU 0x2FFu
#define SCREEN_NONE 0x42Bu

#define PILOT_RID_MENU_LABEL 0x02FFFF00u
#define PILOT_RID_PAGE_TITLE 0x02FFFF01u
#define PILOT_RID_DETAIL_TOGGLE 0x02FFFF02u
#define PILOT_RID_DETAIL_ACTION 0x02FFFF03u
#define PILOT_RID_SLOT_BASE 0x02FFFF10u
#define PILOT_RID_LIST_ITEM_BASE 0x02FFFF20u
#define PILOT_RID_SUBITEM_BASE 0x02FFFF40u
#define PILOT_RID_EMPTY 0x02FFFFFEu

#define PILOT_TEXT_SLOTS_MAX 4u
#define PILOT_LIST_ITEMS_MAX 16u
#define PILOT_PAGE_STACK_MAX 8u

/* ILCE-7CM2 (A7C II) 2.01 appFw.so offsets
 * Note: These offsets need to be verified and updated for a7c2.
 * Currently using a6700 offsets as placeholder - requires reverse engineering
 * of a7c2 appFw.so to determine correct values. */
#define APPFW_GOT_MENU_TABLE 0x4EA8A68ull
#define APPFW_MENU_TABLE_OFF 0x44B8AB0ull
#define APPFW_MENU_TABLE_SIZE 0xF908ull
#define APPFW_CUSTOM_GROUP_OFF 0xDE68ull /* 0x44C6918 - 0x44B8AB0 */
#define APPFW_CUSTOM_SLOT12_OFF 0xDF98ull /* 0x44C6A48 - 0x44B8AB0 */
#define APPFW_CUSTOM_STOCK_COUNT 12u

/* Non-setting row template: 0x4BF0090（上一版 Pilot 行已确认在菜单中显示）。
 * 之前试 0x4C0F878 导致整个 Pilot 行消失，回退。*/
#define APPFW_STOCK_DESC_293_OFF 0x4BF0090ull
#define APPFW_STOCK_ITEM_DESC_OFF 0x4BF8828ull
#define APPFW_DESC_SIZE 0x60ull
#define APPFW_ITEM_ENTRY_SIZE 0x10ull
#define APPFW_ITEM_DESC_SIZE 0xB0ull
#define APPFW_VISIBILITY_OK_OFF 0x2351858ull
#define APPFW_GOT_NONSETTING_TREE 0x4EA32E0ull
#define APPFW_NONSETTING_TREE_OFF 0x8394BE0ull
#define APPFW_TREE_INSERT_OFF 0x236C014ull

#define APPFW_GOT_PAGE_TABLE 0x4E48818ull
#define APPFW_PAGE_TABLE_OFF 0x44CA518ull
#define APPFW_PAGE_ENTRY_SIZE 0x58ull
#define APPFW_PAGE_STOCK_COUNT 0x42Cu

#define APPFW_GOT_LAYOUT_1C3 0x4E4C8C8ull
#define APPFW_LAYOUT_1C3_OFF 0x4FD7130ull
#define APPFW_LAYOUT_1C3_ITEMS 13u

#define APPFW_GOT_CTRL_ITEMS_1C3 0x4E9A470ull
#define APPFW_CTRL_ITEMS_1C3_OFF 0x4AE9210ull
#define APPFW_CTRL_ITEM_SIZE 0x28ull

#define APPFW_GOT_LAYOUT_293 0x4E77818ull
#define APPFW_LAYOUT_293_OFF 0x508C270ull
#define APPFW_LAYOUT_293_ITEMS 12u

#define APPFW_LAYOUT_ITEM_SIZE 0x60ull

#define APPFW_VIEW_FACTORY_1C3_OFF 0x26620E0ull
#define APPFW_CTRL_FACTORY_1C3_OFF 0x24B2624ull
#define APPFW_VIEW_FACTORY_293_OFF 0x25CB2ECull
#define APPFW_CTRL_FACTORY_293_OFF 0x252DD08ull
/* scr7001_1004 (screen 0x293) button-handler GOTs.  All four point into the
 * same controller region (0x29ebxxx); each corresponds to a physical key. */
#define APPFW_GOT_BTN_CONFIRM 0x4ED84A0ull /* -> 0x29eb830 (fires on select) */
#define APPFW_GOT_BTN_RIGHT   0x4E49748ull /* -> 0x29eb148 */
#define APPFW_GOT_BTN_LEFT    0x4E548D0ull /* -> 0x29eb0c8 */
#define APPFW_GOT_BTN_BACK    0x4E666F8ull /* -> 0x29ebb1c */
#define APPFW_GOT_BTN_BACK2   0x4EDE688ull /* -> 0x29ec030 (wrapper calling 0x29ebb1c directly) */
#define APPFW_GOT_1C3_CONFIRM 0x4EB0FE0ull /* -> 0x28fc214 (0x1C3 button confirm) */
#define APPFW_293_GET_FOCUSED_ITEM_OFF 0x29EAAC4ull
#define APPFW_GET_CUR_GROUP_OFF 0x2F731D8ull
#define APPFW_GET_SEL_ROW_OFF 0x2F73228ull
#define APPFW_GET_CUR_ITEM_OFF 0x2F73278ull
#define APPFW_MENU_ITEM_IS_NAVIGABLE_OFF 0x22DC8BCull
#define APPFW_MENU_ITEM_OPEN_OFF 0x22DC94Cull
#define APPFW_MENU_ITEM_RESOLVE_SCREEN_OFF 0x22DC288ull
/* scr7001_1004 low-level "open a screen by sid" primitive (0x29eab8c).  Native
 * confirm's LEAF branch (is_nav==0) reaches it via:
 *   sid = resolve_screen(id);  if sid not {0x42b,0x11e}:
 *   generic_open(ctrl, &{u32 sid, ..0}, &{0x42b,..}, &{0x42b,..}).
 * It dispatches on the per-view model *(ctrl+0x11b28) vtable[0/1] keyed by the
 * cursor ctrl+0x11b20, presenting `sid` through the page table — which is how
 * our already-hooked 0x1c3/0x293 factories render.  Pilot rows only ever fail at
 * the global is_nav(id) gate (our 0x1F00 collides with a native BRANCH id), so
 * we bypass that gate by calling generic_open directly with our computed sid.
 * 0x42b in the two trailing arg structs means "no second/third screen". */
#define APPFW_293_GENERIC_OPEN_OFF 0x29EAB8Cull
#define PILOT_GENERIC_OPEN_SKIP 0x42Bu
/* scr7001_1004 embedded list-cursor object + commit primitives.  The native
 * RIGHT handler (0x29eb148) does `x0 = ctrl + 0x11b38; bl 0x2185b98`, proving
 * the row cursor lives INSIDE the controller at +0x11b38 and that a cursor
 * move (cursor_next/cursor_prev) is what commits firmware's internal
 * "current item" global.  On fresh list entry nothing commits that global, so
 * the FIRST confirm rebuilds its open-context from a stale cur_item and
 * re-opens the list (0x293) instead of the detail (0x1c3).  Within the cursor
 * obj: u32 row index at +328, u32 row count at +336. */
#define APPFW_293_CURSOR_OBJ_OFF 0x11B38ull
#define APPFW_CURSOR_NEXT_OFF 0x2185B98ull
#define APPFW_CURSOR_PREV_OFF 0x2185B00ull
#define APPFW_CURSOR_IDX_OFF 328ull
#define APPFW_CURSOR_COUNT_OFF 336ull
/* GOT the is_nav(0x22DC8BC)/resolve wrappers load their runtime menu-table
 * object from: `adrp 0x4E5B000; ldr x1,[x1,#1072]` => GOT vaddr 0x4E5B430,
 * which relocates to the table object base at static 0x4B4DBD0. */
#define APPFW_GOT_ISNAV_TABLE 0x4E5B430ull

#define APPFW_GOT_SET_STRING_RID 0x4F1FE98ull
#define APPFW_GOT_SET_STRING_CSTR 0x4F1F010ull

#define VIEW_VTABLE_SLOTS 32u

typedef void (*tree_insert_fn)(void *tree_this, const void *start,
                               const void *end);
typedef void *(*ctrl_factory_fn)(void *buf, void *arg1, void *arg2, void *arg3,
                                 uint32_t arg4);
typedef void *(*view_factory_fn)(void *buf, void *arg1, void *arg2,
                                 uint32_t pool_idx, uint32_t screen_id);
typedef void (*view_update_fn)(void *view, uint32_t flag);
typedef void (*text_set_rid_fn)(void *sprite, uint32_t rid);
typedef void (*text_set_cstr_fn)(void *sprite, const char *str);
typedef uint32_t (*ctrl_event_fn)(void *ctrl, void *ev);
typedef void (*generic_open_fn)(void *ctrl, void *a, void *b, void *c);

typedef uint32_t (*menu_item_check_fn)(uint32_t item_id);
typedef void (*menu_item_open_fn)(uint32_t item_id);
typedef void (*cursor_move_fn)(void *cursor_obj);

struct menu_row_entry {
    uint64_t group_id;
    uint64_t setting_desc;
    uint64_t non_setting_desc;
};

struct tree_reg_entry {
    uint64_t group_id;
    uint64_t desc_ptr;
};

/* ---------- Declarative Pages ---------- */

static PilotUiComponent g_version_components[] = {
    {
        .kind = PILOT_UI_COMP_TEXT,
        .label = NULL,
        .x = 48,
        .y = 82,
        .w = 544,
        .h = 42,
    },
    {
        .kind = PILOT_UI_COMP_TEXT,
        .label = PILOT_UI_DESC_STRING,
        .x = 48,
        .y = 138,
        .w = 544,
        .h = 180,
    },
};

static const PilotUiPage g_pilot_version_page = {
    .kind = PILOT_UI_PAGE_DETAIL,
    .title = "Pilot 版本",
    .components = g_version_components,
    .component_count =
        sizeof(g_version_components) / sizeof(g_version_components[0]),
};

static PilotUiComponent g_root_components[PILOT_LIST_ITEMS_MAX];
static PilotUiComponent g_module_detail_comps[PILOT_LIST_ITEMS_MAX][3];
static PilotUiPage g_modules_detail_pages[PILOT_LIST_ITEMS_MAX];
static char g_mod_names[PILOT_LIST_ITEMS_MAX][64];
static char g_mod_titles[PILOT_LIST_ITEMS_MAX][64];
static char g_mod_list_labels[PILOT_LIST_ITEMS_MAX][80];
static char g_mod_status_bufs[PILOT_LIST_ITEMS_MAX][160];
static char g_mod_desc_bufs[PILOT_LIST_ITEMS_MAX][256];
static char g_mod_toggle_bufs[PILOT_LIST_ITEMS_MAX][48];
static int g_mod_enabled[PILOT_LIST_ITEMS_MAX];
static int g_mods_loaded;
static size_t g_mod_count;
static char g_pilot_drive_letter = 'A';
static char g_menu_title[64] = PILOT_MENU_TITLE_DEFAULT;

static const PilotUiPage *build_root_page(void *user);

static PilotUiPage g_pilot_root_page = {
    .kind = PILOT_UI_PAGE_LIST,
    .title = PILOT_MENU_TITLE_DEFAULT,
    .components = g_root_components,
    .component_count = 0,
    .build_dynamic = build_root_page,
};

/* Slot indices in 0x4FD7130 (0x1C3) available for text components */
static const uint32_t g_text_layout_indices[PILOT_TEXT_SLOTS_MAX] = {
    6u,  /* No2_bodyinformation */
    10u, /* No3_versionbody */
    5u,  /* No4_lensinformation */
    9u,  /* No5_versionlens */
};

static const PilotApi *g_api;
static uintptr_t g_appfw_base;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_installed;

static const PilotUiPage *g_active_page = &g_pilot_root_page;
static const PilotUiPage *g_pending_list_page;
static int g_in_pilot_293;
static void *g_active_293_view;
static void *g_active_293_ctrl;
static int g_page_stack_depth;
static const char *g_slot_strings[PILOT_TEXT_SLOTS_MAX];
static const char *g_list_strings[PILOT_LIST_ITEMS_MAX];
static const char *g_subitem_strings[PILOT_LIST_ITEMS_MAX];
static const PilotUiPage *g_detail_page = &g_pilot_version_page;
static int g_pilot_detail_active;
static int g_detail_has_toggle;
static int g_detail_module_index = -1;
static uint32_t g_saved_root_item = PILOT_ITEM_ID_BASE;
static const char *g_toggle_string = "";
static void *g_active_1c3_view;
static void *g_toggle_text_sprite;
static void *g_status_text_sprite;
static void *g_list_item_sprites[PILOT_LIST_ITEMS_MAX];

typedef struct {
    char module_name[64];
    PilotUiOption *options;
    size_t count;
} PilotModOptReg;

static PilotModOptReg g_opt_registry[PILOT_LIST_ITEMS_MAX];
static size_t g_opt_registry_count;
static PilotUiPage g_mod_opt_pages[PILOT_LIST_ITEMS_MAX];
static PilotUiComponent g_mod_opt_comps[PILOT_LIST_ITEMS_MAX][PILOT_LIST_ITEMS_MAX];
static char g_mod_opt_labels[PILOT_LIST_ITEMS_MAX][PILOT_LIST_ITEMS_MAX][80];
static char g_mod_opt_values[PILOT_LIST_ITEMS_MAX][PILOT_LIST_ITEMS_MAX][48];

static void pilot_apply_pending_list_page(void);

static uint8_t g_menu_table[APPFW_MENU_TABLE_SIZE] __attribute__((aligned(16)));
static uint8_t g_desc[APPFW_DESC_SIZE + PILOT_LIST_ITEMS_MAX * APPFW_ITEM_ENTRY_SIZE]
    __attribute__((aligned(16)));
static struct tree_reg_entry g_tree_entry __attribute__((aligned(16)));
static uint8_t g_stock_item_desc[APPFW_ITEM_DESC_SIZE] __attribute__((aligned(16)));
static uint8_t g_stock_nonsetting_desc[APPFW_DESC_SIZE] __attribute__((aligned(16)));
static uint8_t g_item_descs[PILOT_LIST_ITEMS_MAX][APPFW_ITEM_DESC_SIZE]
    __attribute__((aligned(16)));
static uint8_t g_item_nonsetting_descs[PILOT_LIST_ITEMS_MAX]
    [APPFW_DESC_SIZE + PILOT_LIST_ITEMS_MAX * APPFW_ITEM_ENTRY_SIZE]
    __attribute__((aligned(16)));
static struct tree_reg_entry g_item_tree_entries[PILOT_LIST_ITEMS_MAX]
    __attribute__((aligned(16)));
static uint8_t g_subitem_descs[PILOT_LIST_ITEMS_MAX][APPFW_ITEM_DESC_SIZE]
    __attribute__((aligned(16)));
static uint8_t g_subitem_nonsetting_descs[PILOT_LIST_ITEMS_MAX][APPFW_DESC_SIZE]
    __attribute__((aligned(16)));
static struct tree_reg_entry g_subitem_tree_entries[PILOT_LIST_ITEMS_MAX]
    __attribute__((aligned(16)));

static uint8_t g_page_table[APPFW_PAGE_STOCK_COUNT * APPFW_PAGE_ENTRY_SIZE]
    __attribute__((aligned(16)));

static uint8_t g_layout_1c3_stock[APPFW_LAYOUT_1C3_ITEMS * APPFW_LAYOUT_ITEM_SIZE]
    __attribute__((aligned(16)));
static uint8_t g_layout_1c3[APPFW_LAYOUT_1C3_ITEMS * APPFW_LAYOUT_ITEM_SIZE]
    __attribute__((aligned(16)));
static uintptr_t g_orig_layout_1c3;

static uint8_t g_ctrl_items_1c3_stock[APPFW_LAYOUT_1C3_ITEMS * APPFW_CTRL_ITEM_SIZE]
    __attribute__((aligned(16)));
static uint8_t g_ctrl_items_1c3[APPFW_LAYOUT_1C3_ITEMS * APPFW_CTRL_ITEM_SIZE]
    __attribute__((aligned(16)));
static uintptr_t g_orig_ctrl_items_1c3;

static uint8_t g_layout_293_stock[APPFW_LAYOUT_293_ITEMS * APPFW_LAYOUT_ITEM_SIZE]
    __attribute__((aligned(16)));
static uint8_t g_layout_293[APPFW_LAYOUT_293_ITEMS * APPFW_LAYOUT_ITEM_SIZE]
    __attribute__((aligned(16)));
static uintptr_t g_orig_layout_293;

static uintptr_t g_view_vtable_1c3[VIEW_VTABLE_SLOTS] __attribute__((aligned(16)));
static int g_view_vtable_1c3_ready;
static uintptr_t g_view_vtable_293[VIEW_VTABLE_SLOTS] __attribute__((aligned(16)));
static int g_view_vtable_293_ready;

static ctrl_factory_fn g_orig_ctrl_factory_1c3;
static view_factory_fn g_orig_view_factory_1c3;
static view_update_fn g_orig_view_update_1c3;
static ctrl_factory_fn g_orig_ctrl_factory_292;
static ctrl_factory_fn g_orig_ctrl_factory_293;
static view_factory_fn g_orig_view_factory_293;
static view_update_fn g_orig_view_update_293;
static ctrl_event_fn g_orig_btn_confirm;
static ctrl_event_fn g_orig_btn_right;
static ctrl_event_fn g_orig_btn_left;
static ctrl_event_fn g_orig_btn_back;
static ctrl_event_fn g_orig_btn_back2;
static ctrl_event_fn g_orig_1c3_confirm;
static text_set_rid_fn g_orig_set_string_rid;
static text_set_cstr_fn g_orig_set_string_cstr;

static void ui_log(const char *msg)
{
    if (g_api && g_api->log && g_api->log->print)
        (void)g_api->log->print(msg);
}

#include "pilot_ui_navigation.inc"

static int find_appfw_cb(struct dl_phdr_info *info, size_t size, void *opaque)
{
    (void)size;
    uintptr_t *out = opaque;
    if (!info->dlpi_name || !strstr(info->dlpi_name, "appFw.so"))
        return 0;
    *out = (uintptr_t)info->dlpi_addr;
    return 1;
}

static PilotStatus patch_got_ptr(uintptr_t got_addr, uintptr_t expected,
                                 uintptr_t replacement)
{
    return g_api->memory_patch(got_addr, &expected, &replacement,
                               sizeof(uintptr_t));
}

static PilotStatus read_appfw_bytes(uintptr_t addr, void *out, size_t len)
{
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 4096u)
            chunk = 4096u;
        PilotStatus st =
            g_api->memory_read(addr + off, (uint8_t *)out + off, chunk);
        if (st != PILOT_STATUS_OK)
            return st;
        off += chunk;
    }
    return PILOT_STATUS_OK;
}

#include "pilot_ui_option_2a7.inc"
#include "pilot_ui_busy_260.inc"

static void hide_layout_item(uint8_t *layout, uint32_t index)
{
    uint8_t *item = layout + (size_t)index * APPFW_LAYOUT_ITEM_SIZE;
    /* Preserve the display widget's type. Native Version view_update checks
     * widget[7] against the button-background type at 0x28FCE64; changing it
     * triggers the assertion returning to 0x28FCF0C. Focus membership belongs
     * to the separate controller table, not the display layout. */
    memset(item + 0x20u, 0, 16u);
    *(uint32_t *)(item + 0x34u) = PILOT_RID_EMPTY;
    *(uint32_t *)(item + 0x38u) = 0u;
    *(uint32_t *)(item + 0x40u) = 0u;
}

static void set_layout_item_rid(uint8_t *layout, uint32_t index, uint32_t rid)
{
    uint8_t *item = layout + (size_t)index * APPFW_LAYOUT_ITEM_SIZE;
    *(uint32_t *)(item + 0x34u) = rid;
}

static void set_layout_item_rect(uint8_t *layout, uint32_t index, int32_t x,
                                 int32_t y, int32_t w, int32_t h)
{
    uint8_t *item = layout + (size_t)index * APPFW_LAYOUT_ITEM_SIZE;
    *(int32_t *)(item + 0x20u) = x;
    *(int32_t *)(item + 0x24u) = y;
    *(int32_t *)(item + 0x28u) = w;
    *(int32_t *)(item + 0x2Cu) = h;
}

static void set_ctrl_item_rect(uint8_t *ctrl_items, uint32_t index, int32_t x,
                               int32_t y, int32_t w, int32_t h)
{
    uint8_t *item = ctrl_items + (size_t)index * APPFW_CTRL_ITEM_SIZE;
    *(int32_t *)(item + 0x0Cu) = x;
    *(int32_t *)(item + 0x10u) = y;
    *(int32_t *)(item + 0x14u) = w;
    *(int32_t *)(item + 0x18u) = h;
}

static const PilotUiPage *resolve_page(const PilotUiPage *page)
{
    if (page && page->build_dynamic) {
        const PilotUiPage *dyn = page->build_dynamic(page->user_data);
        if (dyn)
            return dyn;
    }
    return page;
}

static const char *skip_ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        ++p;
    return p;
}

static const char *parse_json_str(const char *p, char *out, size_t cap)
{
    p = skip_ws(p);
    if (!p || *p != '"')
        return NULL;
    ++p;
    size_t n = 0;
    while (*p && *p != '"') {
        char ch = *p++;
        if (ch == '\\' && *p) {
            char esc = *p++;
            ch = (esc == 'n') ? '\n' : (esc == 't') ? '\t' : esc;
        }
        if (n + 1 < cap)
            out[n++] = ch;
    }
    out[n] = '\0';
    return (*p == '"') ? (p + 1) : NULL;
}

static void ensure_pilot_drive_letter(void)
{
    if (g_api && g_api->get_module_dir) {
        char dir[128];
        if (g_api->get_module_dir("menu_ui", dir, sizeof(dir)) == PILOT_STATUS_OK &&
            ((dir[0] >= 'A' && dir[0] <= 'Z') ||
             (dir[0] >= 'a' && dir[0] <= 'z'))) {
            g_pilot_drive_letter = dir[0];
        }
    }
}

static int read_pilot_file_fsys(const char *path, char *buf, size_t cap)
{
    if (!path || !buf || cap < 2)
        return 0;

    typedef int (*fsys_open_fn)(const char *, void **, int);
    typedef int (*fsys_read_fn)(void *, void *, size_t);
    typedef int (*fsys_close_fn)(void *);
    fsys_open_fn f_open = (fsys_open_fn)dlsym(RTLD_DEFAULT, "Fsys_Open");
    fsys_read_fn f_read = (fsys_read_fn)dlsym(RTLD_DEFAULT, "Fsys_Read");
    fsys_close_fn f_close = (fsys_close_fn)dlsym(RTLD_DEFAULT, "Fsys_Close");
    if (!f_open || !f_read || !f_close)
        return 0;

    void *h = NULL;
    if (f_open(path, &h, 1) != 0 || !h)
        return 0;

    size_t total = 0;
    while (total + 1 < cap) {
        int n = f_read(h, buf + total, cap - 1 - total);
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    f_close(h);
    buf[total] = '\0';
    return total > 0;
}

static int format_modules_json(char *out, size_t cap)
{
    if (!out || cap < 32u || g_mod_count == 0u)
        return 0;
    size_t pos = 0;
    int n = snprintf(out, cap, "{\n  \"modules\": {\n");
    if (n <= 0 || (size_t)n >= cap)
        return 0;
    pos = (size_t)n;
    for (size_t i = 0; i < g_mod_count; ++i) {
        n = snprintf(out + pos, cap - pos, "    \"%s\": %s%s\n",
                     g_mod_names[i],
                     g_mod_enabled[i] ? "true" : "false",
                     (i + 1u < g_mod_count) ? "," : "");
        if (n <= 0 || (size_t)n >= cap - pos)
            return 0;
        pos += (size_t)n;
    }
    n = snprintf(out + pos, cap - pos, "  }\n}\n");
    if (n <= 0 || (size_t)n >= cap - pos)
        return 0;
    return 1;
}

static int write_pilot_file_fsys(const char *path, const char *data, size_t len)
{
    if (!path || (!data && len))
        return 0;

    typedef int (*fsys_open_fn)(const char *, void **, int);
    typedef int (*fsys_write_fn)(void *, const void *, size_t);
    typedef int (*fsys_flush_fn)(void *);
    typedef int (*fsys_close_fn)(void *);
    fsys_open_fn f_open = (fsys_open_fn)dlsym(RTLD_DEFAULT, "Fsys_Open");
    fsys_write_fn f_write = (fsys_write_fn)dlsym(RTLD_DEFAULT, "Fsys_Write");
    fsys_flush_fn f_flush = (fsys_flush_fn)dlsym(RTLD_DEFAULT, "Fsys_Flush");
    fsys_close_fn f_close = (fsys_close_fn)dlsym(RTLD_DEFAULT, "Fsys_Close");
    if (!f_open || !f_write || !f_flush || !f_close)
        return 0;

    void *h = NULL;
    if (f_open(path, &h, 0xE) != 0 || !h)
        return 0;

    int ok = 1;
    size_t written_total = 0;
    while (written_total < len) {
        int n = f_write(h, data + written_total, len - written_total);
        if (n <= 0 || (size_t)n > len - written_total) {
            ok = 0;
            break;
        }
        written_total += (size_t)n;
    }
    if (ok && f_flush(h) != 0)
        ok = 0;
    if (f_close(h) != 0)
        ok = 0;
    return ok;
}

static int write_modules_json_fsys(void)
{
    char json[2048];
    if (!format_modules_json(json, sizeof(json)))
        return 0;
    char path[64];
    ensure_pilot_drive_letter();
    snprintf(path, sizeof(path), "%c:/pilot/modules.json", g_pilot_drive_letter);
    return write_pilot_file_fsys(path, json, strlen(json));
}

static int read_modules_json_fsys(char *buf, size_t cap)
{
    char path[64];
    ensure_pilot_drive_letter();
    snprintf(path, sizeof(path), "%c:/pilot/modules.json", g_pilot_drive_letter);
    return read_pilot_file_fsys(path, buf, cap);
}

static int read_json_str_field(const char *json, const char *key,
                               char *out, size_t cap)
{
    if (!json || !key || !out || cap < 2)
        return 0;
    char needle[48];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(needle))
        return 0;
    const char *p = strstr(json, needle);
    if (!p)
        return 0;
    p = strchr(p + n, ':');
    if (!p)
        return 0;
    if (parse_json_str(p + 1, out, cap) && out[0])
        return 1;
    out[0] = '\0';
    return 0;
}

static void read_module_meta_fsys(const char *module_name,
                                  char *title_out, size_t title_cap,
                                  char *desc_out, size_t desc_cap)
{
    if (title_out && title_cap)
        title_out[0] = '\0';
    if (desc_out && desc_cap)
        desc_out[0] = '\0';
    if (!module_name)
        return;

    char base[64];
    snprintf(base, sizeof(base), "%.63s", module_name);
    size_t len = strlen(base);
    if (len >= 3 && strcmp(base + len - 3, ".so") == 0)
        base[len - 3] = '\0';

    char base_no_pilot[64] = "";
    if (strncmp(base, "pilot_", 6) == 0)
        snprintf(base_no_pilot, sizeof(base_no_pilot), "%.57s", base + 6);

    const char *names[] = {module_name, base, base_no_pilot};
    char path[192];
    char json[1024];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (!names[i] || !names[i][0])
            continue;
        snprintf(path, sizeof(path), "%c:/pilot/modules/%.63s/config.json",
                 g_pilot_drive_letter, names[i]);
        if (!read_pilot_file_fsys(path, json, sizeof(json)))
            continue;
        if (title_out && title_cap > 1)
            (void)read_json_str_field(json, "title", title_out, title_cap);
        if (desc_out && desc_cap > 1) {
            if (!read_json_str_field(json, "description", desc_out, desc_cap))
                (void)read_json_str_field(json, "desc", desc_out, desc_cap);
        }
        return;
    }
}

static void load_menu_title_from_config(void)
{
    /* Root/entry title is fixed to "Pilot"; module item titles come from each
     * module's config.json in build_root_page(). */
    snprintf(g_menu_title, sizeof(g_menu_title), "%s", PILOT_MENU_TITLE_DEFAULT);
    g_pilot_root_page.title = g_menu_title;
}

static void parse_modules_json(const char *json)
{
    g_mod_count = 0;
    const char *p = json ? strstr(json, "\"modules\"") : NULL;
    if (!p)
        return;
    p = strchr(p + 9, ':');
    if (!p)
        return;
    p = skip_ws(p + 1);

    if (*p == '[') {
        ++p;
        while (g_mod_count < PILOT_LIST_ITEMS_MAX) {
            p = skip_ws(p);
            if (!p || *p == ']' || *p == '\0')
                break;
            if (*p == ',') {
                ++p;
                continue;
            }
            const char *next =
                parse_json_str(p, g_mod_names[g_mod_count],
                               sizeof(g_mod_names[g_mod_count]));
            if (!next)
                break;
            g_mod_enabled[g_mod_count] = 1;
            ++g_mod_count;
            p = next;
        }
    } else if (*p == '{') {
        ++p;
        while (g_mod_count < PILOT_LIST_ITEMS_MAX) {
            p = skip_ws(p);
            if (!p || *p == '}' || *p == '\0')
                break;
            if (*p == ',') {
                ++p;
                continue;
            }
            const char *next =
                parse_json_str(p, g_mod_names[g_mod_count],
                               sizeof(g_mod_names[g_mod_count]));
            if (!next)
                break;
            next = skip_ws(next);
            if (!next || *next != ':')
                break;
            next = skip_ws(next + 1);
            int en = 0;
            if (strncmp(next, "true", 4) == 0) {
                en = 1;
                next += 4;
            } else if (strncmp(next, "false", 5) == 0) {
                en = 0;
                next += 5;
            } else {
                break;
            }
            g_mod_enabled[g_mod_count] = en;
            ++g_mod_count;
            p = next;
        }
    }
}

static void format_default_module_title(const char *module_name,
                                        char *out, size_t cap)
{
    if (!module_name || !out || cap < 2)
        return;
    const char *start = module_name;
    if (strncmp(start, "pilot_", 6) == 0 && start[6] != '\0')
        start += 6;
    snprintf(out, cap, "%.63s", start);
    size_t len = strlen(out);
    if (len > 3 && strcmp(out + len - 3, ".so") == 0)
        out[len - 3] = '\0';
}

#define PILOT_LIST_TITLE_MAX_COLS 20u /* "[_ON] " (6) + 20 = 26 cols */

static size_t utf8_char_len(unsigned char lead)
{
    if ((lead & 0x80u) == 0u) return 1u;
    if ((lead & 0xE0u) == 0xC0u) return 2u;
    if ((lead & 0xF0u) == 0xE0u) return 3u;
    if ((lead & 0xF8u) == 0xF0u) return 4u;
    return 1u;
}

static size_t utf8_measure_cols(const char *s)
{
    size_t cols = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (p && *p) {
        size_t n = utf8_char_len(*p);
        cols += (n == 1u) ? 1u : 2u;
        for (size_t k = 0; k < n && *p; ++k)
            ++p;
    }
    return cols;
}

static void format_list_label_truncated(char *dst, size_t cap,
                                        const char *prefix,
                                        const char *title,
                                        size_t max_title_cols)
{
    if (!dst || cap == 0)
        return;
    size_t pos = 0;
    if (prefix) {
        int n = snprintf(dst, cap, "%s", prefix);
        if (n > 0)
            pos = ((size_t)n < cap) ? (size_t)n : (cap - 1u);
    }
    if (!title || pos + 1u >= cap) {
        dst[pos] = '\0';
        return;
    }

    int need_ellipsis = (utf8_measure_cols(title) > max_title_cols);
    size_t budget = need_ellipsis && max_title_cols > 2u
                        ? (max_title_cols - 2u)
                        : max_title_cols;
    size_t cols = 0;
    const unsigned char *p = (const unsigned char *)title;
    while (*p) {
        size_t n = utf8_char_len(*p);
        size_t avail = 0;
        while (avail < n && p[avail])
            ++avail;
        size_t w = (avail == 1u) ? 1u : 2u;
        if (cols + w > budget)
            break;
        if (pos + avail + (need_ellipsis ? 2u : 0u) + 1u > cap)
            break;
        memcpy(dst + pos, p, avail);
        pos += avail;
        cols += w;
        p += avail;
    }
    if (need_ellipsis && pos + 3u <= cap) {
        dst[pos++] = '.';
        dst[pos++] = '.';
    }
    dst[pos] = '\0';
}

static void refresh_module_ui_strings(size_t i)
{
    if (i >= PILOT_LIST_ITEMS_MAX)
        return;
    const char *tag = g_mod_enabled[i] ? "[_ON]" : "[OFF]";
    format_list_label_truncated(g_mod_list_labels[i],
                                sizeof(g_mod_list_labels[i]),
                                NULL, g_mod_titles[i],
                                PILOT_LIST_TITLE_MAX_COLS);
    snprintf(g_mod_status_bufs[i], sizeof(g_mod_status_bufs[i]),
             "Status: %s\nPath: %c:/pilot/%.63s",
             g_mod_enabled[i] ? "Enabled" : "Disabled",
             g_pilot_drive_letter, g_mod_names[i]);
    snprintf(g_mod_toggle_bufs[i], sizeof(g_mod_toggle_bufs[i]),
             "模块开关: %s", tag);
}

static PilotModOptReg *find_module_opt_reg(const char *module_name);
static const PilotUiPage *build_module_opt_list_page(void *user);

static const PilotUiPage *build_root_page(void *user)
{
    (void)user;
    if (!g_mods_loaded) {
        char json[2048];
        if (read_modules_json_fsys(json, sizeof(json)))
            parse_modules_json(json);
        else
            g_mod_count = 0;

        if (g_mod_count == 0) {
            snprintf(g_mod_names[0], sizeof(g_mod_names[0]), "pilot_menu_ui.so");
            g_mod_enabled[0] = 1;
            g_mod_count = 1;
        }
        for (size_t i = 0; i < g_mod_count; ++i) {
            read_module_meta_fsys(g_mod_names[i],
                                  g_mod_titles[i], sizeof(g_mod_titles[i]),
                                  g_mod_desc_bufs[i], sizeof(g_mod_desc_bufs[i]));
            if (!g_mod_titles[i][0])
                format_default_module_title(g_mod_names[i],
                                            g_mod_titles[i],
                                            sizeof(g_mod_titles[i]));
            if (!g_mod_desc_bufs[i][0])
                snprintf(g_mod_desc_bufs[i], sizeof(g_mod_desc_bufs[i]),
                         "No description.");
        }
        g_mods_loaded = 1;
    }

    size_t count = 0;
    g_root_components[count++] = (PilotUiComponent){
        .kind = PILOT_UI_COMP_SUBMENU,
        .label = "Pilot 版本",
        .target_page = &g_pilot_version_page,
    };

    for (size_t i = 0; i < g_mod_count && count < PILOT_LIST_ITEMS_MAX; ++i) {
        refresh_module_ui_strings(i);

        g_module_detail_comps[i][0] = (PilotUiComponent){
            .kind = PILOT_UI_COMP_TEXT,
            .label = g_mod_status_bufs[i],
            .x = 48,
            .y = 74,
            .w = 544,
            .h = 72,
        };
        g_module_detail_comps[i][1] = (PilotUiComponent){
            .kind = PILOT_UI_COMP_TEXT,
            .label = g_mod_desc_bufs[i],
            .x = 48,
            .y = 154,
            .w = 544,
            .h = 120,
        };
        g_module_detail_comps[i][2] = (PilotUiComponent){
            .kind = PILOT_UI_COMP_TOGGLE,
            .label = g_mod_toggle_bufs[i],
            .x = 179,
            .y = 288,
            .w = 282,
            .h = 54,
        };
        g_modules_detail_pages[i] = (PilotUiPage){
            .kind = PILOT_UI_PAGE_DETAIL,
            .title = g_mod_titles[i],
            .components = g_module_detail_comps[i],
            .component_count = 3u,
            .user_data = (void *)(uintptr_t)i,
        };
        g_mod_opt_pages[i] = (PilotUiPage){
            .kind = PILOT_UI_PAGE_LIST,
            .title = g_mod_titles[i],
            .components = g_mod_opt_comps[i],
            .component_count = 0u,
            .build_dynamic = build_module_opt_list_page,
            .user_data = (void *)(uintptr_t)i,
        };
        PilotModOptReg *reg = find_module_opt_reg(g_mod_names[i]);
        g_root_components[count++] = (PilotUiComponent){
            .kind = PILOT_UI_COMP_SUBMENU,
            .label = g_mod_list_labels[i],
            .value = g_mod_enabled[i] ? "开" : "关",
            .target_page = (reg && reg->count > 0u)
                               ? &g_mod_opt_pages[i]
                               : &g_modules_detail_pages[i],
        };
    }

    g_pilot_root_page.component_count = count;
    return &g_pilot_root_page;
}

static PilotModOptReg *find_module_opt_reg(const char *module_name)
{
    if (!module_name || !module_name[0])
        return NULL;
    for (size_t i = 0; i < g_opt_registry_count; ++i) {
        if (strcmp(g_opt_registry[i].module_name, module_name) == 0)
            return &g_opt_registry[i];
    }
    return NULL;
}

__attribute__((visibility("default")))
PilotStatus pilot_ui_register_options_v1(const char *module_name,
                                         PilotUiOption *options,
                                         size_t option_count)
{
    if (!module_name || !module_name[0] || (!options && option_count))
        return PILOT_STATUS_INVALID_ARGUMENT;

    pthread_mutex_lock(&g_lock);
    PilotModOptReg *reg = find_module_opt_reg(module_name);
    if (!reg) {
        if (g_opt_registry_count >= PILOT_LIST_ITEMS_MAX) {
            pthread_mutex_unlock(&g_lock);
            return PILOT_STATUS_RANGE;
        }
        reg = &g_opt_registry[g_opt_registry_count++];
        snprintf(reg->module_name, sizeof(reg->module_name), "%.63s",
                 module_name);
    }
    reg->options = options;
    reg->count = option_count;
    pthread_mutex_unlock(&g_lock);

    char msg[128];
    snprintf(msg, sizeof(msg), "PILOT_UI_OPT_REG mod=%.63s count=%zu",
             module_name, option_count);
    ui_log(msg);
    return PILOT_STATUS_OK;
}

static void format_option_list_value(char *val_out, size_t val_cap,
                                     const PilotUiOption *opt)
{
    if (!val_out || !val_cap)
        return;
    if (!opt) {
        val_out[0] = '\0';
        return;
    }
    if (opt->kind == PILOT_UI_OPT_BOOL) {
        snprintf(val_out, val_cap, "%s", opt->value ? "开" : "关");
    } else if (opt->kind == PILOT_UI_OPT_ENUM) {
        const char *val = "?";
        if (opt->choices && opt->choice_count > 0u) {
            size_t ci = (opt->value >= 0 && (size_t)opt->value < opt->choice_count)
                            ? (size_t)opt->value
                            : 0u;
            val = opt->choices[ci] ? opt->choices[ci] : "?";
        }
        snprintf(val_out, val_cap, "%.47s", val);
    } else {
        const char *act = (opt->action_text && opt->action_text[0])
                              ? opt->action_text
                              : "Run";
        snprintf(val_out, val_cap, "%.24s (%d)", act, opt->value);
    }
}

static const PilotUiPage *build_module_opt_list_page(void *user)
{
    size_t mod_idx = (size_t)(uintptr_t)user;
    if (mod_idx >= g_mod_count)
        return &g_pilot_root_page;

    PilotModOptReg *reg = find_module_opt_reg(g_mod_names[mod_idx]);
    size_t count = 0u;

    /* Rows 0..opt_count-1: Module registered options (label left, value right) */
    if (reg && reg->options) {
        for (size_t k = 0; k < reg->count && count + 1u < PILOT_LIST_ITEMS_MAX; ++k) {
            const char *lbl = reg->options[k].label ? reg->options[k].label : "Option";
            format_list_label_truncated(g_mod_opt_labels[mod_idx][count],
                                        sizeof(g_mod_opt_labels[mod_idx][count]),
                                        NULL, lbl, PILOT_LIST_TITLE_MAX_COLS);
            format_option_list_value(g_mod_opt_values[mod_idx][count],
                                     sizeof(g_mod_opt_values[mod_idx][count]),
                                     &reg->options[k]);
            g_mod_opt_comps[mod_idx][count] = (PilotUiComponent){
                .kind = PILOT_UI_COMP_TOGGLE,
                .label = g_mod_opt_labels[mod_idx][count],
                .value = g_mod_opt_values[mod_idx][count],
            };
            ++count;
        }
    }

    /* Last row: Module Info & Enable Switch (opens 0x1C3 detail page) */
    snprintf(g_mod_opt_labels[mod_idx][count],
             sizeof(g_mod_opt_labels[mod_idx][count]),
             "模块信息与开关");
    g_mod_opt_comps[mod_idx][count] = (PilotUiComponent){
        .kind = PILOT_UI_COMP_SUBMENU,
        .label = g_mod_opt_labels[mod_idx][count],
        .value = g_mod_enabled[mod_idx] ? "开" : "关",
        .target_page = &g_modules_detail_pages[mod_idx],
    };
    ++count;

    g_mod_opt_pages[mod_idx].kind = PILOT_UI_PAGE_LIST;
    g_mod_opt_pages[mod_idx].title = g_mod_titles[mod_idx];
    g_mod_opt_pages[mod_idx].components = g_mod_opt_comps[mod_idx];
    g_mod_opt_pages[mod_idx].component_count = count;
    g_mod_opt_pages[mod_idx].user_data = (void *)(uintptr_t)mod_idx;
    return &g_mod_opt_pages[mod_idx];
}

static void bind_detail_page_to_layout(const PilotUiPage *page)
{
    memcpy(g_layout_1c3, g_layout_1c3_stock, sizeof(g_layout_1c3));
    set_layout_item_rid(g_layout_1c3, 3u, PILOT_RID_PAGE_TITLE);
    memcpy(g_ctrl_items_1c3, g_ctrl_items_1c3_stock, sizeof(g_ctrl_items_1c3));

    const PilotUiComponent *toggle = NULL;
    if (page) {
        for (size_t i = 0; i < page->component_count; ++i) {
            if (page->components[i].kind == PILOT_UI_COMP_TOGGLE) {
                toggle = &page->components[i];
                break;
            }
        }
    }

    if (toggle) {
        g_detail_has_toggle = 1;
        g_detail_module_index = (int)(uintptr_t)page->user_data;
        g_toggle_string = toggle->label ? toggle->label : "";
        if (toggle->w > 0 && toggle->h > 0) {
            set_layout_item_rect(g_layout_1c3, 7u,
                                 toggle->x, toggle->y, toggle->w, toggle->h);
            set_ctrl_item_rect(g_ctrl_items_1c3, 7u,
                               toggle->x, toggle->y, toggle->w, toggle->h);
            set_layout_item_rect(g_layout_1c3, 12u,
                                 toggle->x + 7, toggle->y + 6,
                                 toggle->w - 14, toggle->h - 12);
            set_ctrl_item_rect(g_ctrl_items_1c3, 12u,
                               toggle->x + 7, toggle->y + 6,
                               toggle->w - 14, toggle->h - 12);
        }
        set_layout_item_rid(g_layout_1c3, 12u, PILOT_RID_DETAIL_TOGGLE);
    } else {
        g_detail_has_toggle = 0;
        g_detail_module_index = -1;
        g_toggle_string = "";
        hide_layout_item(g_layout_1c3, 7u);  /* No9_Backgroundforbodyfirmupdatebutton */
        hide_layout_item(g_layout_1c3, 12u); /* No6_Bodyfirmupdatebutton */

        /* Also remove item 7 (firmware-update button) from the 0x1C3 controller's
         * own 13-entry focus table (0x4AE9210 via GOT 0x4E9A470).  The controller
         * cursor initializer 0x21874E4 scans this table—not the view layout—for
         * entries whose +0x00 type descriptor is 0x8343DB0 (items 7 and 8).
         * Demoting item 7's type descriptor to item 0's static background (0x8344360)
         * leaves only item 8 (OK button) in the 2D cursor tree, so wheel/LEFT/RIGHT
         * cannot move focus off the OK button onto the hidden update button. */
        *(uintptr_t *)(g_ctrl_items_1c3 + 7u * APPFW_CTRL_ITEM_SIZE) =
            *(const uintptr_t *)g_ctrl_items_1c3_stock;
    }

    for (uint32_t s = 0; s < PILOT_TEXT_SLOTS_MAX; ++s) {
        g_slot_strings[s] = "";
        set_layout_item_rid(g_layout_1c3, g_text_layout_indices[s],
                            PILOT_RID_EMPTY);
    }

    if (!page)
        return;

    uint32_t slot = 0;
    if (page->content && page->content[0]) {
        g_slot_strings[slot] = page->content;
        set_layout_item_rid(g_layout_1c3, g_text_layout_indices[slot],
                            PILOT_RID_SLOT_BASE + slot);
        set_layout_item_rect(g_layout_1c3, g_text_layout_indices[slot], 48, 82,
                             544, 240);
        ++slot;
    }

    for (size_t i = 0; i < page->component_count && slot < PILOT_TEXT_SLOTS_MAX;
         ++i) {
        const PilotUiComponent *c = &page->components[i];
        if (c->kind != PILOT_UI_COMP_TEXT)
            continue;
        g_slot_strings[slot] = c->label ? c->label : "";
        set_layout_item_rid(g_layout_1c3, g_text_layout_indices[slot],
                            PILOT_RID_SLOT_BASE + slot);
        if (c->w > 0 && c->h > 0)
            set_layout_item_rect(g_layout_1c3, g_text_layout_indices[slot],
                                 c->x, c->y, c->w, c->h);
        ++slot;
    }
}

/* 让 is_nav(0x22DC8BC) 对 Pilot 子行返回 1 的关键修复。
 * 现场 log 证据：row0 desc modes=[1,1,0,1] → 当前拍摄模式 index=2 的
 * 可用性 halfword(+0x3A + 2*mode)=0 → is_nav=0 → open_item(0x22DC94C)
 * 什么都不做；若改为 fallthrough，则事件落回 scr7001_1004（即原厂
 * "我的菜单"列表控制器），其 confirm/右键对该行的原生语义是"添加项目到
 * 我的菜单"，于是随机弹出"自定义菜单添加"。
 * 本函数把 9 个 per-mode 可用性 halfword 全写 1（原厂"本模式可见"确认值，
 * 见 crash retrospective 关于 0x4B8F898 只 +0x40=1 的分析；不写 0xffff，
 * 避免固件 "== 1" 判据把它当不可见），使 is_nav 在所有模式都返回 1，
 * 从而让 open_item 走正规开屏路径直接开 0x1C3，绕开我的菜单添加逻辑。
 * 仅用于子项 / 孙项 nonsetting desc，绝不用于父 g_desc；不动 +0x14/+0x15
 * （上版清父行 +0x14/+0x15 会导致整个 Pilot 入口消失）；+0x28 顺带清 0
 * （子项本就为 nil，属防御性，避免 target_sid 被回调覆盖）。*/
static void pilot_normalize_nonsetting_desc(uint8_t *desc)
{
    if (!desc)
        return;
    *(uintptr_t *)(desc + 0x28ull) = 0ull;
    for (size_t k = 0; k < 9; ++k)
        *(uint16_t *)(desc + 0x3Aull + k * 2ull) = 1u;
}

static void bind_list_page_to_layout(const PilotUiPage *page)
{
    memcpy(g_layout_293, g_layout_293_stock, sizeof(g_layout_293));
    for (uint32_t i = 0; i < PILOT_LIST_ITEMS_MAX; ++i) {
        g_list_strings[i] = "";
        g_subitem_strings[i] = "";
        g_list_val_strings[i] = NULL;
        g_list_val_sprites[i] = NULL;
    }
    if (!page) {
        *(uint32_t *)(g_desc + 0x50ull) = 0u;
        return;
    }
    size_t count = page->component_count;
    if (count > PILOT_LIST_ITEMS_MAX)
        count = PILOT_LIST_ITEMS_MAX;
    *(uint32_t *)(g_desc + 0x50ull) = (uint32_t)count;
    for (size_t i = 0; i < count; ++i) {
        const PilotUiComponent *c = &page->components[i];
        const PilotUiPage *sub =
            (c->kind == PILOT_UI_COMP_SUBMENU && c->target_page)
                ? resolve_page(c->target_page)
                : NULL;
        uint32_t target_sid =
            (sub && sub->kind == PILOT_UI_PAGE_DETAIL)
                ? PILOT_DETAIL_SCREEN_ID
                : PILOT_LIST_SCREEN_ID;
        g_list_strings[i] = c->label ? c->label : "";
        pilot_bind_list_val_row(i, c->value);

        memcpy(g_item_descs[i], g_stock_item_desc, APPFW_ITEM_DESC_SIZE);
        *(uint32_t *)(g_item_descs[i] + 0x00ull) =
            PILOT_RID_LIST_ITEM_BASE + (uint32_t)i;
        *(uint32_t *)(g_item_descs[i] + 0x0Cull) = PILOT_RID_EMPTY;
        *(uint32_t *)(g_item_descs[i] + 0x20ull) =
            (c->value && c->value[0]) ? PILOT_OPT_SCREEN_ID : target_sid;
        if (c->value && c->value[0]) {
            *(uint32_t *)(g_item_descs[i] + 0x40ull) = 1u;
            *(uintptr_t *)(g_item_descs[i] + 0x48ull) =
                (uintptr_t)g_list_val_rows[i];
            *(uintptr_t *)(g_item_descs[i] + 0x68ull) =
                g_appfw_base + APPFW_VISIBILITY_OK_OFF;
        }
        *(uintptr_t *)(g_item_descs[i] + 0x58ull) =
            g_appfw_base + APPFW_VISIBILITY_OK_OFF;

        memcpy(g_item_nonsetting_descs[i], g_stock_nonsetting_desc,
               APPFW_DESC_SIZE);
        *(uint32_t *)(g_item_nonsetting_descs[i] + 0x00ull) =
            PILOT_RID_LIST_ITEM_BASE + (uint32_t)i;
        *(uint32_t *)(g_item_nonsetting_descs[i] + 0x0Cull) = target_sid;
        *(uintptr_t *)(g_item_nonsetting_descs[i] + 0x20ull) =
            g_appfw_base + APPFW_VISIBILITY_OK_OFF;
        /* Flip per-mode availability halfwords so is_nav(0x22DC8BC)=1 →
         * open_item(0x22DC94C) performs the real 0x1C3 transition. Only on
         * child rows; parent g_desc is never normalized. */
        pilot_normalize_nonsetting_desc(g_item_nonsetting_descs[i]);

        size_t sub_count = 0u;
        if (sub && sub->kind == PILOT_UI_PAGE_LIST) {
            sub_count = sub->component_count;
            if (sub_count > PILOT_LIST_ITEMS_MAX)
                sub_count = PILOT_LIST_ITEMS_MAX;
            for (size_t j = 0; j < sub_count; ++j) {
                const PilotUiComponent *sc = &sub->components[j];
                g_subitem_strings[j] = sc->label ? sc->label : "";
                memcpy(g_subitem_descs[j], g_stock_item_desc,
                       APPFW_ITEM_DESC_SIZE);
                *(uint32_t *)(g_subitem_descs[j] + 0x00ull) =
                    PILOT_RID_SUBITEM_BASE + (uint32_t)j;
                *(uint32_t *)(g_subitem_descs[j] + 0x0Cull) = PILOT_RID_EMPTY;
                *(uint32_t *)(g_subitem_descs[j] + 0x20ull) =
                    PILOT_DETAIL_SCREEN_ID;
                *(uintptr_t *)(g_subitem_descs[j] + 0x58ull) =
                    g_appfw_base + APPFW_VISIBILITY_OK_OFF;

                memcpy(g_subitem_nonsetting_descs[j], g_stock_nonsetting_desc,
                       APPFW_DESC_SIZE);
                *(uint32_t *)(g_subitem_nonsetting_descs[j] + 0x00ull) =
                    PILOT_RID_SUBITEM_BASE + (uint32_t)j;
                *(uint32_t *)(g_subitem_nonsetting_descs[j] + 0x0Cull) =
                    PILOT_DETAIL_SCREEN_ID;
                *(uintptr_t *)(g_subitem_nonsetting_descs[j] + 0x20ull) =
                    g_appfw_base + APPFW_VISIBILITY_OK_OFF;
                *(uint32_t *)(g_subitem_nonsetting_descs[j] + 0x50ull) = 0u;
                pilot_normalize_nonsetting_desc(g_subitem_nonsetting_descs[j]);

                uint8_t *sslot = g_item_nonsetting_descs[i] + APPFW_DESC_SIZE +
                                 j * APPFW_ITEM_ENTRY_SIZE;
                *(uint32_t *)(sslot + 0x00ull) =
                    PILOT_SUBITEM_ID_BASE + (uint32_t)j;
                *(uint32_t *)(sslot + 0x04ull) = 0u;
                *(uintptr_t *)(sslot + 0x08ull) =
                    (uintptr_t)g_subitem_descs[j];
            }
        }
        *(uint32_t *)(g_item_nonsetting_descs[i] + 0x50ull) =
            (uint32_t)sub_count;

        uint8_t *slot =
            g_desc + APPFW_DESC_SIZE + i * APPFW_ITEM_ENTRY_SIZE;
        *(uint32_t *)(slot + 0x00ull) = PILOT_ITEM_ID_BASE + (uint32_t)i;
        *(uint32_t *)(slot + 0x04ull) = 0u;
        *(uintptr_t *)(slot + 0x08ull) = (uintptr_t)g_item_descs[i];
    }
}

#include "pilot_lv_ui.inc"

#define PILOT_RID_OVERRIDES_MAX 16u

typedef struct {
    uint32_t rid;
    int active;
    char text[64];
} PilotRidOverride;

static pthread_mutex_t g_rid_override_lock = PTHREAD_MUTEX_INITIALIZER;
static PilotRidOverride g_rid_overrides[PILOT_RID_OVERRIDES_MAX];
static uint32_t g_rid_override_count = 0u;

static int pilot_rid_override_lookup(uint32_t rid, char *out, size_t cap)
{
    if (__atomic_load_n(&g_rid_override_count, __ATOMIC_ACQUIRE) == 0u)
        return 0;
    int found = 0;
    pthread_mutex_lock(&g_rid_override_lock);
    for (size_t i = 0; i < PILOT_RID_OVERRIDES_MAX; ++i) {
        if (g_rid_overrides[i].active && g_rid_overrides[i].rid == rid) {
            snprintf(out, cap, "%s", g_rid_overrides[i].text);
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_rid_override_lock);
    return found;
}

__attribute__((visibility("default")))
PilotStatus pilot_ui_set_rid_override_v1(uint32_t rid, const char *text)
{
    if (rid == 0u)
        return PILOT_STATUS_INVALID_ARGUMENT;

    pthread_mutex_lock(&g_rid_override_lock);
    int slot = -1;
    int free_slot = -1;
    for (size_t i = 0; i < PILOT_RID_OVERRIDES_MAX; ++i) {
        if (g_rid_overrides[i].active && g_rid_overrides[i].rid == rid) {
            slot = (int)i;
            break;
        }
        if (!g_rid_overrides[i].active && free_slot < 0)
            free_slot = (int)i;
    }

    if (!text || !text[0]) {
        if (slot >= 0) {
            g_rid_overrides[slot].active = 0;
            g_rid_overrides[slot].rid = 0u;
            g_rid_overrides[slot].text[0] = '\0';
            uint32_t cnt = __atomic_load_n(&g_rid_override_count, __ATOMIC_RELAXED);
            if (cnt > 0u)
                __atomic_store_n(&g_rid_override_count, cnt - 1u, __ATOMIC_RELEASE);
        }
        pthread_mutex_unlock(&g_rid_override_lock);
        return PILOT_STATUS_OK;
    }

    if (slot < 0) {
        if (free_slot < 0) {
            pthread_mutex_unlock(&g_rid_override_lock);
            return PILOT_STATUS_RANGE;
        }
        slot = free_slot;
        g_rid_overrides[slot].rid = rid;
        g_rid_overrides[slot].active = 1;
        uint32_t cnt = __atomic_load_n(&g_rid_override_count, __ATOMIC_RELAXED);
        __atomic_store_n(&g_rid_override_count, cnt + 1u, __ATOMIC_RELEASE);
    }
    snprintf(g_rid_overrides[slot].text, sizeof(g_rid_overrides[slot].text),
             "%s", text);
    pthread_mutex_unlock(&g_rid_override_lock);
    return PILOT_STATUS_OK;
}

static void pilot_set_string_rid_hook(void *sprite, uint32_t rid)
{
    if (g_orig_set_string_cstr) {
        if (rid == PILOT_BUSY_MSG_RID && pilot_busy_is_active()) {
            char msg[160];
            pilot_busy_copy_text(msg, sizeof(msg));
            g_orig_set_string_cstr(sprite, msg);
            return;
        }
        if (rid == PILOT_RID_MENU_LABEL) {
            g_orig_set_string_cstr(sprite, g_menu_title);
            return;
        }
        if (rid == PILOT_RID_PAGE_TITLE) {
            const char *t = (g_detail_page && g_detail_page->title)
                                ? g_detail_page->title
                                : g_menu_title;
            g_orig_set_string_cstr(sprite, t);
            return;
        }
        if (rid == PILOT_RID_DETAIL_TOGGLE) {
            g_toggle_text_sprite = sprite;
            g_orig_set_string_cstr(sprite, g_toggle_string ? g_toggle_string : "");
            return;
        }
        if (rid == PILOT_RID_OPT_TITLE) {
            g_orig_set_string_cstr(sprite, g_opt_title_string ? g_opt_title_string : "");
            return;
        }
        if (rid >= PILOT_RID_OPT_CHOICE_BASE &&
            rid < PILOT_RID_OPT_CHOICE_BASE + PILOT_OPT_CHOICES_MAX) {
            const char *s = g_opt_choice_strings[rid - PILOT_RID_OPT_CHOICE_BASE];
            g_orig_set_string_cstr(sprite, s ? s : "");
            return;
        }
        if (rid >= PILOT_RID_LIST_VAL_BASE &&
            rid < PILOT_RID_LIST_VAL_BASE + PILOT_LIST_ITEMS_MAX) {
            uint32_t row = rid - PILOT_RID_LIST_VAL_BASE;
            g_list_val_sprites[row] = sprite;
            const char *s = g_list_val_strings[row];
            g_orig_set_string_cstr(sprite, s ? s : "");
            return;
        }
        if (rid >= PILOT_RID_SLOT_BASE &&
            rid < PILOT_RID_SLOT_BASE + PILOT_TEXT_SLOTS_MAX) {
            uint32_t slot = rid - PILOT_RID_SLOT_BASE;
            if (slot == 0u)
                g_status_text_sprite = sprite;
            const char *s = g_slot_strings[slot];
            g_orig_set_string_cstr(sprite, s ? s : "");
            return;
        }
        if (rid >= PILOT_RID_LIST_ITEM_BASE &&
            rid < PILOT_RID_LIST_ITEM_BASE + PILOT_LIST_ITEMS_MAX) {
            uint32_t row = rid - PILOT_RID_LIST_ITEM_BASE;
            g_list_item_sprites[row] = sprite;
            const char *s = g_list_strings[row];
            g_orig_set_string_cstr(sprite, s ? s : "");
            return;
        }
        if (rid >= PILOT_RID_SUBITEM_BASE &&
            rid < PILOT_RID_SUBITEM_BASE + PILOT_LIST_ITEMS_MAX) {
            const char *s = g_subitem_strings[rid - PILOT_RID_SUBITEM_BASE];
            g_orig_set_string_cstr(sprite, s ? s : "");
            return;
        }
        if (rid == PILOT_RID_EMPTY) {
            g_orig_set_string_cstr(sprite, "");
            return;
        }
        char ov[64];
        if (pilot_rid_override_lookup(rid, ov, sizeof(ov))) {
            g_orig_set_string_cstr(sprite, ov);
            return;
        }
    }
    if (g_orig_set_string_rid)
        g_orig_set_string_rid(sprite, rid);
}

static void pilot_1c3_view_update_hook(void *view, uint32_t flag)
{
    if (view && g_pilot_detail_active) {
        /* 0x28FCC74 copies *(uint8_t *)(view + 0x8FC) to widget[7]+0x3F and
         * widget[12]+0x341, which is the grayed-out/disabled visual state
         * (1 = disabled/gray, 0 = normal active style). Keep it 0 so the
         * toggle button renders with normal active appearance. */
        *((uint8_t *)view + 0x8FCu) = 0u;
    }
    if (g_orig_view_update_1c3)
        g_orig_view_update_1c3(view, flag);
}

static const PilotUiPage *resolve_detail_page_from_menu(int *is_pilot_ctx)
{
    typedef const uint32_t *(*get_id_ptr_fn)(void);
    get_id_ptr_fn get_grp =
        (get_id_ptr_fn)(g_appfw_base + APPFW_GET_CUR_GROUP_OFF);
    get_id_ptr_fn get_item =
        (get_id_ptr_fn)(g_appfw_base + APPFW_GET_CUR_ITEM_OFF);
    uint32_t cur_grp = get_grp ? *get_grp() : 0u;
    uint32_t cur_item = get_item ? *get_item() : 0u;

    char msg[96];
    snprintf(msg, sizeof(msg),
             "PILOT_UI_TRACE resolve_detail cur_grp=0x%x cur_item=0x%x in=%d",
             cur_grp, cur_item, g_in_pilot_293);
    ui_log(msg);

    const PilotUiPage *active = g_active_page ? g_active_page : &g_pilot_root_page;
    int pilot_item = (cur_item >= PILOT_ITEM_ID_BASE &&
                      cur_item < PILOT_ITEM_ID_BASE + active->component_count);
    int pilot_ctx = pilot_item || (cur_grp == PILOT_GROUP_ID) || g_in_pilot_293;
    if (is_pilot_ctx)
        *is_pilot_ctx = pilot_ctx;
    if (!pilot_ctx)
        return NULL;

    /* Item id 0x1F00+idx maps to the ACTIVE list page's component #idx. */
    if (pilot_item) {
        size_t idx = (size_t)(cur_item - PILOT_ITEM_ID_BASE);
        const PilotUiComponent *c = &active->components[idx];
        if (c->target_page)
            return resolve_page(c->target_page);
        if (active == &g_pilot_root_page) {
            if (idx == 0)
                return &g_pilot_version_page;
            if (idx - 1 < g_mod_count)
                return &g_modules_detail_pages[idx - 1];
        }
    }
    return &g_pilot_version_page;
}

static void *pilot_1c3_ctrl_factory_hook(void *buf, void *arg1, void *arg2,
                                         void *arg3, uint32_t arg4)
{
    int is_pilot = 0;
    const PilotUiPage *page = resolve_detail_page_from_menu(&is_pilot);
    if (!is_pilot || !page) {
        return g_orig_ctrl_factory_1c3(buf, arg1, arg2, arg3, arg4);
    }

    g_detail_page = page;
    bind_detail_page_to_layout(g_detail_page);
    ui_log("PILOT_UI_TRACE 1c3_ctrl_factory open_detail");
    if (g_in_pilot_293)
        g_pilot_detail_active = 1;

    uintptr_t ctrl_got = g_appfw_base + APPFW_GOT_CTRL_ITEMS_1C3;
    int patched =
        (patch_got_ptr(ctrl_got, g_orig_ctrl_items_1c3,
                       (uintptr_t)g_ctrl_items_1c3) == PILOT_STATUS_OK);
    if (!patched) {
        ui_log("PILOT_UI_ERR detail focus table patch failed");
        return NULL;
    }

    void *ret = g_orig_ctrl_factory_1c3(buf, arg1, arg2, arg3, arg4);

    if (patch_got_ptr(ctrl_got, (uintptr_t)g_ctrl_items_1c3,
                      g_orig_ctrl_items_1c3) != PILOT_STATUS_OK)
        ui_log("PILOT_UI_ERR detail focus table restore failed");
    pilot_log_detail_focus(ret, g_detail_has_toggle);

    return ret;
}

static void *pilot_1c3_view_factory_hook(void *buf, void *arg1, void *arg2,
                                         uint32_t pool_idx, uint32_t screen_id)
{
    (void)screen_id;
    int is_pilot = 0;
    const PilotUiPage *page = resolve_detail_page_from_menu(&is_pilot);
    if (!is_pilot || !page) {
        ui_log("PILOT_UI_TRACE 1c3_view_factory passthrough_non_pilot");
        return g_orig_view_factory_1c3(buf, arg1, arg2, pool_idx,
                                       PILOT_DETAIL_SCREEN_ID);
    }

    g_detail_page = page;
    g_toggle_text_sprite = NULL;
    g_status_text_sprite = NULL;
    bind_detail_page_to_layout(g_detail_page);
    ui_log("PILOT_UI_TRACE 1c3_view_factory open_detail");
    if (g_in_pilot_293)
        g_pilot_detail_active = 1;

    uintptr_t layout_got = g_appfw_base + APPFW_GOT_LAYOUT_1C3;
    int patched =
        (patch_got_ptr(layout_got, g_orig_layout_1c3,
                       (uintptr_t)g_layout_1c3) == PILOT_STATUS_OK);

    void *ret = g_orig_view_factory_1c3(buf, arg1, arg2, pool_idx,
                                        PILOT_DETAIL_SCREEN_ID);

    if (patched)
        (void)patch_got_ptr(layout_got, (uintptr_t)g_layout_1c3,
                            g_orig_layout_1c3);

    g_active_1c3_view = ret;
    if (ret) {
        *((uint8_t *)ret + 0x8FCu) = 0u;
        uintptr_t *orig_vt = *(uintptr_t **)ret;
        if (!g_view_vtable_1c3_ready && orig_vt) {
            memcpy(g_view_vtable_1c3, orig_vt, sizeof(g_view_vtable_1c3));
            g_orig_view_update_1c3 = (view_update_fn)g_view_vtable_1c3[2];
            g_view_vtable_1c3[2] = (uintptr_t)pilot_1c3_view_update_hook;
            __atomic_store_n(&g_view_vtable_1c3_ready, 1, __ATOMIC_RELEASE);
        }
        if (__atomic_load_n(&g_view_vtable_1c3_ready, __ATOMIC_ACQUIRE))
            *(uintptr_t **)ret = g_view_vtable_1c3;
    }
    return ret;
}

static void pilot_apply_pending_list_page(void)
{
    if (!g_pending_list_page)
        return;
    g_active_page = resolve_page(g_pending_list_page);
    g_pending_list_page = NULL;
    g_page_stack_depth = (g_active_page == &g_pilot_root_page) ? 0 : 1;
    memset(g_list_item_sprites, 0, sizeof(g_list_item_sprites));
    bind_list_page_to_layout(g_active_page);
    if (!g_return_list_pending) {
        uint32_t *row_p = pilot_menu_id_ptr(APPFW_GET_SEL_ROW_OFF);
        uint32_t *item_p = pilot_menu_id_ptr(APPFW_GET_CUR_ITEM_OFF);
        if (row_p)
            *row_p = PILOT_ITEM_ID_BASE;
        if (item_p)
            *item_p = PILOT_ITEM_ID_BASE;
    }
}

static void pilot_293_view_update_hook(void *view, uint32_t flag)
{
    /* NOTE: previously this hook wrote a fixed group id into both the
     * view struct and the global current-group pointer every frame. On
     * camera that clobbered firmware's own screen-open transition when
     * the user pressed confirm while in=1 (row 0 never fired
     * 1c3_view_factory in that state, yet the same firmware handler
     * opened 1c3 correctly once in=0). The initial group alignment
     * performed once inside the 293 view factory is enough. Keep the
     * vtable slot patched so future per-frame state tracking can be
     * re-added safely, but do not touch view state here. */
    if (g_orig_view_update_293)
        g_orig_view_update_293(view, flag);
}

static void *pilot_292_ctrl_factory_hook(void *buf, void *arg1, void *arg2,
                                         void *arg3, uint32_t arg4)
{
    /* Entering main menu (0x292) — e.g. pressing MENU from LiveView after
     * shutter-aborting out of 0x2A7/0x1C3, or backing out of 0x293 — always
     * invalidates any pending submenu return state so the next 0x293
     * ctrl_factory builds its focus tree for the full Pilot root. */
    pilot_clear_list_return();
    g_pending_list_page = NULL;
    g_pilot_detail_active = 0;
    g_pilot_2a7_active = 0;
    pilot_busy_reset_state();
    return g_orig_ctrl_factory_292(buf, arg1, arg2, arg3, arg4);
}

static void *pilot_293_ctrl_factory_hook(void *buf, void *arg1, void *arg2,
                                         void *arg3, uint32_t arg4)
{
    char msg[192];
    uintptr_t a2_vt = arg2 ? *(const uintptr_t *)arg2 : 0;
    uintptr_t a2_vt7 = a2_vt ? *(const uintptr_t *)(a2_vt + 56ull) : 0;
    snprintf(msg, sizeof(msg),
             "PILOT_UI_TRACE 293_ctrl_factory enter buf=%p a1=%p a2=%p(vt=0x%llx,vt7=0x%llx) a3=%p a4=%u",
             buf, arg1, arg2,
             (unsigned long long)(a2_vt ? a2_vt - g_appfw_base : 0),
             (unsigned long long)(a2_vt7 ? a2_vt7 - g_appfw_base : 0),
             arg3, arg4);
    ui_log(msg);

    /* Screen manager invokes ctrl_factory (and ctrl->on_enter 0x29EB2EC)
     * BEFORE view_factory.  Prepare the active page and descriptor layout
     * here when current group is PILOT_GROUP_ID so the controller constructor
     * and 0x29EB2EC see the up-to-date g_desc item table. */
    {
        typedef uint32_t *(*get_id_ptr_fn)(void);
        get_id_ptr_fn get_grp =
            (get_id_ptr_fn)(g_appfw_base + APPFW_GET_CUR_GROUP_OFF);
        get_id_ptr_fn get_row =
            (get_id_ptr_fn)(g_appfw_base + APPFW_GET_SEL_ROW_OFF);
        get_id_ptr_fn get_item =
            (get_id_ptr_fn)(g_appfw_base + APPFW_GET_CUR_ITEM_OFF);
        uint32_t *grp_p = get_grp ? get_grp() : NULL;
        uint32_t *row_p = get_row ? get_row() : NULL;
        uint32_t *item_p = get_item ? get_item() : NULL;
        if (grp_p && *grp_p == PILOT_GROUP_ID) {
            int drill_down = (g_pending_list_page != NULL);
            int returning = (g_return_list_pending != 0);
            g_in_pilot_293 = 1;
            g_pilot_detail_active = 0;
            g_pilot_2a7_active = 0;
            if (g_pending_list_page) {
                g_active_page = resolve_page(g_pending_list_page);
                g_pending_list_page = NULL;
            } else if (returning && g_return_list_page) {
                g_active_page = resolve_page(g_return_list_page);
            } else {
                /* Fresh entry (e.g. from main menu 0x292 or after exiting via
                 * shutter to LiveView): reset to Pilot root BEFORE
                 * g_orig_ctrl_factory_293 builds the controller's focus tree,
                 * otherwise the focus tree keeps the stale submenu item count
                 * while the view renders the full root list. */
                g_active_page = resolve_page(&g_pilot_root_page);
            }
            g_page_stack_depth = (g_active_page == &g_pilot_root_page) ? 0 : 1;
            memset(g_list_item_sprites, 0, sizeof(g_list_item_sprites));
            bind_list_page_to_layout(g_active_page);

            size_t count = g_active_page ? g_active_page->component_count : 0u;
            if (drill_down && !g_return_list_pending) {
                if (row_p)
                    *row_p = PILOT_ITEM_ID_BASE;
                if (item_p)
                    *item_p = PILOT_ITEM_ID_BASE;
            } else {
                uint32_t row_id = row_p ? *row_p : 0u;
                uint32_t item_id = item_p ? *item_p : 0u;
                int row_valid = (row_id >= PILOT_ITEM_ID_BASE &&
                                 row_id < PILOT_ITEM_ID_BASE + count);
                int item_valid = (item_id >= PILOT_ITEM_ID_BASE &&
                                  item_id < PILOT_ITEM_ID_BASE + count);
                if (!row_valid && item_valid && row_p)
                    *row_p = item_id;
                else if (!row_valid && !item_valid) {
                    if (row_p)
                        *row_p = PILOT_ITEM_ID_BASE;
                    if (item_p)
                        *item_p = PILOT_ITEM_ID_BASE;
                }
            }
        }
    }

    void *ret = g_orig_ctrl_factory_293(buf, arg1, arg2, arg3, arg4);
    if (pilot_current_group() && !pilot_attach_list_lifecycle(ret))
        return NULL;
    g_active_293_ctrl = ret;
    ui_log("PILOT_UI_TRACE 293_ctrl_factory ok");
    return ret;
}

static void *pilot_293_view_factory_hook(void *buf, void *arg1, void *arg2,
                                         uint32_t pool_idx, uint32_t screen_id)
{
    char msg[128];
    snprintf(msg, sizeof(msg),
             "PILOT_UI_TRACE 293_factory enter buf=%p pool=%u sid=0x%x",
             buf, pool_idx, screen_id);
    ui_log(msg);

    /* Guard: only enter Pilot mode when the navigating source is Pilot's own
     * group.  If another firmware feature (e.g. function-assignment picker)
     * opens screen 0x293, the current group will NOT be PILOT_GROUP_ID and we
     * must passthrough to avoid hijacking that feature's layout and actions. */
    {
        typedef uint32_t *(*get_id_ptr_fn)(void);
        get_id_ptr_fn get_grp =
            (get_id_ptr_fn)(g_appfw_base + APPFW_GET_CUR_GROUP_OFF);
        uint32_t *grp_p = get_grp ? get_grp() : NULL;
        if (!grp_p || *grp_p != PILOT_GROUP_ID) {
            snprintf(msg, sizeof(msg),
                     "PILOT_UI_TRACE 293_factory non_pilot_grp=0x%x passthrough",
                     grp_p ? *grp_p : 0u);
            ui_log(msg);
            return g_orig_view_factory_293(buf, arg1, arg2, pool_idx,
                                           PILOT_LIST_SCREEN_ID);
        }
    }

    g_in_pilot_293 = 1;
    g_pilot_detail_active = 0;
    g_pilot_2a7_active = 0;
    if (g_pending_list_page || !g_active_page) {
        g_active_page = resolve_page(g_pending_list_page ? g_pending_list_page
                                                         : &g_pilot_root_page);
        g_pending_list_page = NULL;
        g_page_stack_depth = (g_active_page == &g_pilot_root_page) ? 0 : 1;
        memset(g_list_item_sprites, 0, sizeof(g_list_item_sprites));
        bind_list_page_to_layout(g_active_page);
    }
    /* Synchronize sel_row (0x2F73228) and cur_item (0x2F73278) without
     * clobbering the selection that ctrl->on_enter (0x29EB2EC) just set.
     * Note: 0x29EB2EC runs BEFORE view_factory!
     * - On entry from 0x292 (main menu), 0x29EB2EC focuses row 0 and writes
     *   0x1F00 to cur_item (*item_p), but leaves sel_row (*row_p) untouched.
     * - On return from 0x1C3 (detail page), 0x29EB2EC restores focus from
     *   sel_row (*row_p) and writes it to cur_item (*item_p).
     * Unconditionally resetting *row_p and *item_p to 0x1F00 here previously
     * wiped the restored selection after every 0x1C3 return, causing the
     * second consecutive detail entry to reset to the first row. */
    {
        typedef uint32_t *(*get_id_ptr_fn)(void);
        get_id_ptr_fn get_grp =
            (get_id_ptr_fn)(g_appfw_base + APPFW_GET_CUR_GROUP_OFF);
        get_id_ptr_fn get_row =
            (get_id_ptr_fn)(g_appfw_base + APPFW_GET_SEL_ROW_OFF);
        get_id_ptr_fn get_item =
            (get_id_ptr_fn)(g_appfw_base + APPFW_GET_CUR_ITEM_OFF);
        uint32_t *grp_p = get_grp ? get_grp() : NULL;
        uint32_t *row_p = get_row ? get_row() : NULL;
        uint32_t *item_p = get_item ? get_item() : NULL;
        if (grp_p)
            *grp_p = PILOT_GROUP_ID;

        size_t count = g_active_page ? g_active_page->component_count : 0u;
        uint32_t item_id = item_p ? *item_p : 0u;
        uint32_t row_id = row_p ? *row_p : 0u;
        int item_valid = (item_id >= PILOT_ITEM_ID_BASE &&
                          item_id < PILOT_ITEM_ID_BASE + count);
        int row_valid = (row_id >= PILOT_ITEM_ID_BASE &&
                         row_id < PILOT_ITEM_ID_BASE + count);
        uint32_t sync_id = item_valid ? item_id
                                      : (row_valid ? row_id : PILOT_ITEM_ID_BASE);
        if (row_p)
            *row_p = sync_id;
        if (item_p)
            *item_p = sync_id;
    }

    uintptr_t layout_got = g_appfw_base + APPFW_GOT_LAYOUT_293;
    int patched =
        (patch_got_ptr(layout_got, g_orig_layout_293,
                       (uintptr_t)g_layout_293) == PILOT_STATUS_OK);

    void *ret = g_orig_view_factory_293(buf, arg1, arg2, pool_idx,
                                        PILOT_LIST_SCREEN_ID);

    if (patched)
        (void)patch_got_ptr(layout_got, (uintptr_t)g_layout_293,
                            g_orig_layout_293);

    ui_log("PILOT_UI_TRACE 293_factory orig_done");
    g_active_293_view = ret;
    if (ret) {
        uintptr_t *orig_vt = *(uintptr_t **)ret;
        if (!g_view_vtable_293_ready && orig_vt) {
            memcpy(g_view_vtable_293, orig_vt, sizeof(g_view_vtable_293));
            g_orig_view_update_293 = (view_update_fn)g_view_vtable_293[2];
            g_view_vtable_293[2] = (uintptr_t)pilot_293_view_update_hook;
            __atomic_store_n(&g_view_vtable_293_ready, 1, __ATOMIC_RELEASE);
        }
        if (__atomic_load_n(&g_view_vtable_293_ready, __ATOMIC_ACQUIRE))
            *(uintptr_t **)ret = g_view_vtable_293;
    }
    ui_log("PILOT_UI_TRACE 293_factory ok");
    return ret;
}

static void reset_pilot_293_state(void)
{
    pilot_clear_list_return();
    g_page_stack_depth = 0;
    g_active_page = &g_pilot_root_page;
    g_pending_list_page = NULL;
    g_active_293_view = NULL;
    g_active_293_ctrl = NULL;
    g_active_1c3_view = NULL;
    g_toggle_text_sprite = NULL;
    g_status_text_sprite = NULL;
    memset(g_list_item_sprites, 0, sizeof(g_list_item_sprites));
    g_in_pilot_293 = 0;
    g_pilot_detail_active = 0;
    g_pilot_2a7_active = 0;
    pilot_busy_reset_state();
    g_detail_has_toggle = 0;
    g_detail_module_index = -1;
    g_saved_root_item = PILOT_ITEM_ID_BASE;
}

static int is_pilot_item_id(uint32_t id)
{
    return (id >= PILOT_ITEM_ID_BASE &&
            id < PILOT_ITEM_ID_BASE + PILOT_LIST_ITEMS_MAX) ||
           (id >= PILOT_SUBITEM_ID_BASE &&
            id < PILOT_SUBITEM_ID_BASE + PILOT_LIST_ITEMS_MAX);
}

/* Component-owned ENTER: the native confirm decides branch-vs-leaf purely via
 * the GLOBAL is_nav(id) table (0x22DC8BC, keyed by numeric id only — it never
 * sees our view).  Our row ids (0x1F00..) collide with native SETTING ids, so
 * is_nav(0x1F00) returns "branch" and native re-opens 0x293 instead of the
 * detail.  Rather than dodge ids or rebuild the compiled-in setting registry,
 * we consume the ENTER ourselves and drive firmware's own leaf-open primitive
 * generic_open(0x29EAB8C) with the target screen we chose from OUR page model.
 * generic_open dispatches the per-view model *(ctrl+0x11b28) which presents the
 * sid through the page table — i.e. exactly our already-hooked 0x1c3/0x293
 * factories render.  Foreign (non-Pilot) ids still pass through to native. */
static int pilot_pop_to_root_list(void *ctrl)
{
    if (!ctrl || !g_appfw_base)
        return 0;
    generic_open_fn go =
        (generic_open_fn)(g_appfw_base + APPFW_293_GENERIC_OPEN_OFF);
    if (!go)
        return 0;

    g_pending_list_page = &g_pilot_root_page;
    g_page_stack_depth = 0;
    g_return_list_page = &g_pilot_root_page;
    g_return_list_item = g_saved_root_item;
    g_return_list_pending = 1;

    uint32_t a[4] = { PILOT_LIST_SCREEN_ID, 0u, 0u, 0u };
    uint32_t b[4] = { PILOT_GENERIC_OPEN_SKIP, 0u, 0u, 0u };
    uint32_t c[4] = { PILOT_GENERIC_OPEN_SKIP, 0u, 0u, 0u };
    ui_log("PILOT_UI_DBG pop_to_root sid=0x293");
    go(ctrl, a, b, c);
    return 1;
}

static void pilot_2a7_apply_selection(uint32_t choice_index)
{
    size_t mod_idx = g_pilot_2a7_mod_idx;
    size_t row_idx = g_pilot_2a7_row_idx;
    if (mod_idx >= g_mod_count)
        return;

    PilotModOptReg *reg = find_module_opt_reg(g_mod_names[mod_idx]);
    if (!reg || !reg->options || row_idx >= reg->count)
        return;
    PilotUiOption *opt = &reg->options[row_idx];
    if (opt->kind == PILOT_UI_OPT_BOOL) {
        opt->value = (choice_index == 0u) ? 1 : 0;
    } else if (opt->kind == PILOT_UI_OPT_ENUM && choice_index < opt->choice_count) {
        opt->value = (int32_t)choice_index;
    }
    if (opt->on_change)
        opt->on_change(opt, opt->user);
    format_option_list_value(g_mod_opt_values[mod_idx][row_idx],
                             sizeof(g_mod_opt_values[mod_idx][row_idx]),
                             opt);
    g_list_val_strings[row_idx] = g_mod_opt_values[mod_idx][row_idx];
    char msg[160];
    snprintf(msg, sizeof(msg), "PILOT_UI_2A7_SET mod=%s opt=%s val=%d",
             g_mod_names[mod_idx], opt->label ? opt->label : "?", opt->value);
    ui_log(msg);
}

static int pilot_open_2a7_picker(void *ctrl, size_t mod_idx, size_t row_idx,
                                 const char *title,
                                 const char *const *choices, size_t count,
                                 uint32_t current_index)
{
    if (!ctrl || !g_appfw_base || !g_orig_2a7_confirm || count == 0u)
        return 0;
    generic_open_fn go =
        (generic_open_fn)(g_appfw_base + APPFW_293_GENERIC_OPEN_OFF);
    if (!go)
        return 0;

    pilot_2a7_prepare_choices(title, choices, count, current_index);
    g_pilot_2a7_mod_idx = mod_idx;
    g_pilot_2a7_row_idx = row_idx;
    g_pilot_2a7_active = 1;
    pilot_save_list_return(ctrl, PILOT_ITEM_ID_BASE + (uint32_t)row_idx);

    uint32_t *item_p = pilot_menu_id_ptr(APPFW_GET_CUR_ITEM_OFF);
    if (item_p)
        *item_p = PILOT_OPT_GROUP_ID;

    uint32_t a[4] = { PILOT_OPT_SCREEN_ID, 0u, 0u, 0u };
    uint32_t b[4] = { PILOT_GENERIC_OPEN_SKIP, 0u, 0u, 0u };
    uint32_t c[4] = { PILOT_GENERIC_OPEN_SKIP, 0u, 0u, 0u };
    ui_log("PILOT_UI_DBG open_2a7_picker sid=0x2a7");
    go(ctrl, a, b, c);
    return 1;
}

static int pilot_handle_opt_list_click(void *ctrl, size_t row_idx)
{
    if (!g_active_page || g_active_page == &g_pilot_root_page ||
        row_idx >= g_active_page->component_count)
        return 0;

    size_t mod_idx = (size_t)(uintptr_t)g_active_page->user_data;
    if (mod_idx >= g_mod_count)
        return 0;

    const PilotUiComponent *c = &g_active_page->components[row_idx];
    if (c->kind == PILOT_UI_COMP_SUBMENU && c->target_page)
        return 0; /* e.g. Module Info -> fall through to generic_open(0x1C3) */

    if (c->kind == PILOT_UI_COMP_TEXT) {
        return pilot_pop_to_root_list(ctrl);
    }

    if (c->kind != PILOT_UI_COMP_TOGGLE)
        return 0;

    static const char *const k_on_off_choices[2] = { "开", "关" };

    PilotModOptReg *reg = find_module_opt_reg(g_mod_names[mod_idx]);
    if (!reg || !reg->options || row_idx >= reg->count)
        return 1;
    PilotUiOption *opt = &reg->options[row_idx];
    if (opt->kind == PILOT_UI_OPT_BOOL) {
        uint32_t cur = opt->value ? 0u : 1u;
        if (pilot_open_2a7_picker(ctrl, mod_idx, row_idx,
                                  opt->label ? opt->label : "Option",
                                  k_on_off_choices, 2u, cur))
            return 1;
        opt->value = opt->value ? 0 : 1;
    } else if (opt->kind == PILOT_UI_OPT_ENUM && opt->choice_count > 0u) {
        uint32_t cur = (opt->value >= 0 && (size_t)opt->value < opt->choice_count)
                           ? (uint32_t)opt->value
                           : 0u;
        if (pilot_open_2a7_picker(ctrl, mod_idx, row_idx,
                                  opt->label ? opt->label : "Option",
                                  opt->choices, opt->choice_count, cur))
            return 1;
        opt->value = (int32_t)(((size_t)cur + 1u) % opt->choice_count);
    } else {
        opt->value += 1;
    }
    if (opt->on_change)
        opt->on_change(opt, opt->user);
    format_option_list_value(g_mod_opt_values[mod_idx][row_idx],
                             sizeof(g_mod_opt_values[mod_idx][row_idx]),
                             opt);
    g_list_val_strings[row_idx] = g_mod_opt_values[mod_idx][row_idx];

    char msg[160];
    snprintf(msg, sizeof(msg), "PILOT_UI_OPT_ACT mod=%s opt=%s val=%d",
             g_mod_names[mod_idx],
             opt->label ? opt->label : "?", opt->value);
    ui_log(msg);

    if (g_orig_set_string_cstr && g_list_val_sprites[row_idx])
        g_orig_set_string_cstr(g_list_val_sprites[row_idx],
                               g_list_val_strings[row_idx]);
    if (g_active_293_view && g_orig_view_update_293)
        g_orig_view_update_293(g_active_293_view, 1u);
    return 1;
}

static int pilot_open_focused_item(void *ctrl)
{
    char msg[160];
    if (!g_in_pilot_293 || !g_appfw_base || !ctrl) {
        ui_log("PILOT_UI_DBG pilot_open skip");
        return 0;
    }

    /* Live truth-source guard: only act when the firmware's current group is
     * actually Pilot's.  g_in_pilot_293 is sticky (set by the view factory);
     * gating on the live group too means a lingering flag can never hijack a
     * foreign native setting whose id collides with our 0x1F00 row space. */
    {
        typedef uint32_t *(*get_grp_ptr_fn)(void);
        get_grp_ptr_fn get_grp =
            (get_grp_ptr_fn)(g_appfw_base + APPFW_GET_CUR_GROUP_OFF);
        uint32_t *grp_p = get_grp ? get_grp() : NULL;
        if (!grp_p || *grp_p != PILOT_GROUP_ID) {
            snprintf(msg, sizeof(msg),
                     "PILOT_UI_DBG pilot_open foreign_grp=0x%x passthrough",
                     grp_p ? *grp_p : 0u);
            ui_log(msg);
            return 0;
        }
    }

    typedef const uint32_t *(*get_id_ptr_fn)(void);
    get_id_ptr_fn get_focused =
        (get_id_ptr_fn)(g_appfw_base + APPFW_293_GET_FOCUSED_ITEM_OFF);
    if (!get_focused) {
        ui_log("PILOT_UI_DBG pilot_open no_get_focused");
        return 0;
    }
    const uint32_t *focused = get_focused();
    uint32_t item_id = focused ? *focused : 0u;

    if (!is_pilot_item_id(item_id)) {
        snprintf(msg, sizeof(msg),
                 "PILOT_UI_DBG pilot_open foreign=0x%x passthrough", item_id);
        ui_log(msg);
        return 0;
    }

    /* Anchor both sel_row (0x2F73228, read by 0x29EB2EC on return from 0x1C3)
     * and cur_item (0x2F73278, read by resolve_detail_page_from_menu) to the
     * focused item_id before opening the child screen. */
    {
        typedef uint32_t *(*get_mut_id_ptr_fn)(void);
        get_mut_id_ptr_fn get_row =
            (get_mut_id_ptr_fn)(g_appfw_base + APPFW_GET_SEL_ROW_OFF);
        get_mut_id_ptr_fn get_item =
            (get_mut_id_ptr_fn)(g_appfw_base + APPFW_GET_CUR_ITEM_OFF);
        uint32_t *row_p = get_row ? get_row() : NULL;
        uint32_t *item_p = get_item ? get_item() : NULL;
        if (row_p)
            *row_p = item_id;
        if (item_p)
            *item_p = item_id;
    }

    /* In a module's native 0x293 option list, handle inline toggle/enum/action
     * and Back rows directly without leaving the 0x293 list screen. */
    if (g_active_page && g_active_page != &g_pilot_root_page &&
        item_id >= PILOT_ITEM_ID_BASE &&
        item_id < PILOT_ITEM_ID_BASE + g_active_page->component_count) {
        size_t row_idx = (size_t)(item_id - PILOT_ITEM_ID_BASE);
        if (pilot_handle_opt_list_click(ctrl, row_idx))
            return 1;
    }

    /* Choose the target screen from OUR model, not from is_nav/resolve. */
    int is_pilot = 0;
    const PilotUiPage *page = resolve_detail_page_from_menu(&is_pilot);
    uint32_t sid;
    if (page && page->kind == PILOT_UI_PAGE_LIST) {
        sid = PILOT_LIST_SCREEN_ID;   /* drill down: 0x293 factory renders it */
        g_saved_root_item = item_id;
        g_page_stack_depth = 1;
        g_pending_list_page = page;
        pilot_clear_list_return();
    } else {
        sid = PILOT_DETAIL_SCREEN_ID; /* 0x1c3 factory binds from cur_item */
        pilot_save_list_return(ctrl, item_id);
    }

    generic_open_fn go =
        (generic_open_fn)(g_appfw_base + APPFW_293_GENERIC_OPEN_OFF);
    if (!go) {
        ui_log("PILOT_UI_DBG pilot_open no_generic_open");
        g_pending_list_page = NULL;
        return 0;
    }

    /* Native leaf-open marshalling: three arg structs, each with a u32 key at
     * +0.  The first carries the screen id to present; the other two carry the
     * 0x42b sentinel meaning "no second/third screen".  Buffers are zero-filled
     * beyond +0 to match what native leaves there. */
    uint32_t a[4] = { sid, 0u, 0u, 0u };
    uint32_t b[4] = { PILOT_GENERIC_OPEN_SKIP, 0u, 0u, 0u };
    uint32_t c[4] = { PILOT_GENERIC_OPEN_SKIP, 0u, 0u, 0u };
    snprintf(msg, sizeof(msg), "PILOT_UI_DBG pilot_open item=0x%x sid=0x%x",
             item_id, sid);
    ui_log(msg);

    go(ctrl, a, b, c);
    return 1;
}

static uint32_t pilot_btn_confirm(void *ctrl, void *ev)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "PILOT_UI_DBG btn_confirm in=%d view=%p", g_in_pilot_293, g_active_293_view);
    ui_log(msg);
    if (g_in_pilot_293 && g_active_293_view) {
        /* Consume ENTER for Pilot rows via our own generic_open call, bypassing
         * the global is_nav(id) gate that re-opens 0x293 for our colliding ids. */
        if (pilot_open_focused_item(ctrl)) {
            ui_log("PILOT_UI_DBG btn_confirm consumed");
            return 1u;
        }
        ui_log("PILOT_UI_DBG btn_confirm fallthrough");
    }
    uint32_t r = g_orig_btn_confirm ? g_orig_btn_confirm(ctrl, ev) : 1u;
    snprintf(msg, sizeof(msg), "PILOT_UI_DBG btn_confirm orig=0x%x", r);
    ui_log(msg);
    return r;
}

static uint32_t pilot_btn_right(void *ctrl, void *ev)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "PILOT_UI_DBG btn_right in=%d view=%p", g_in_pilot_293, g_active_293_view);
    ui_log(msg);
    /* RIGHT is a MOVE, not an ENTER: the native handler (0x29eb148) is just
     * cursor_next on ctrl+0x11b38 (bl 0x2185b98).  Let it run untouched so row
     * navigation works; ENTER (detail open) is handled solely by CONFIRM. */
    uint32_t r = g_orig_btn_right ? g_orig_btn_right(ctrl, ev) : 1u;
    snprintf(msg, sizeof(msg), "PILOT_UI_DBG btn_right orig=0x%x", r);
    ui_log(msg);
    return r;
}

static uint32_t pilot_btn_left(void *ctrl, void *ev)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "PILOT_UI_DBG btn_left in=%d view=%p", g_in_pilot_293, g_active_293_view);
    ui_log(msg);
    /* LEFT is a MOVE, not an exit: the native handler (0x29eb0c8) is just
     * cursor_prev on ctrl+0x11b38 (bl 0x2185b00), the mirror of RIGHT's
     * cursor_next.  Let it run untouched so row navigation works.  Resetting
     * pilot state here wiped g_in_pilot_293/g_active_293_view on every LEFT,
     * which desynced the list selection after returning from a detail popup
     * and made the next CONFIRM fall through to native (version popup). */
    uint32_t r = g_orig_btn_left ? g_orig_btn_left(ctrl, ev) : 1u;
    snprintf(msg, sizeof(msg), "PILOT_UI_DBG btn_left orig=0x%x", r);
    ui_log(msg);
    return r;
}

static uint32_t pilot_1c3_confirm_hook(void *ctrl, void *ev)
{
    if (g_pilot_detail_active && g_detail_has_toggle && ctrl && g_appfw_base) {
        void *cursor = (uint8_t *)ctrl + APPFW_293_CURSOR_OBJ_OFF;
        uint32_t index = ((pilot_cursor_index_fn)(g_appfw_base +
                           APPFW_CURSOR_GET_INDEX_OFF))(cursor);
        if (index == 7u) {
            if (g_detail_module_index >= 0 &&
                (size_t)g_detail_module_index < g_mod_count &&
                (size_t)g_detail_module_index < PILOT_LIST_ITEMS_MAX) {
                size_t idx = (size_t)g_detail_module_index;
                g_mod_enabled[idx] = !g_mod_enabled[idx];
                int saved = write_modules_json_fsys();
                if (!saved) {
                    g_mod_enabled[idx] = !g_mod_enabled[idx];
                    ui_log("PILOT_UI_ERR modules.json write failed; toggle reverted");
                    return 1u;
                }
                refresh_module_ui_strings(idx);
                g_toggle_string = g_mod_toggle_bufs[idx];
                g_slot_strings[0] = g_mod_status_bufs[idx];
                const char *en_val = g_mod_enabled[idx] ? "开" : "关";
                if (idx + 1u < PILOT_LIST_ITEMS_MAX) {
                    g_root_components[idx + 1u].value = en_val;
                    if (g_active_page == &g_pilot_root_page)
                        g_list_val_strings[idx + 1u] = en_val;
                }
                if (g_active_page == &g_mod_opt_pages[idx] &&
                    g_mod_opt_pages[idx].component_count > 0u) {
                    size_t last = g_mod_opt_pages[idx].component_count - 1u;
                    g_mod_opt_comps[idx][last].value = en_val;
                    g_list_val_strings[last] = en_val;
                }
                if (g_orig_set_string_cstr) {
                    if (g_toggle_text_sprite)
                        g_orig_set_string_cstr(g_toggle_text_sprite,
                                               g_toggle_string);
                    if (g_status_text_sprite)
                        g_orig_set_string_cstr(g_status_text_sprite,
                                               g_mod_status_bufs[idx]);
                }
                if (g_active_1c3_view && g_orig_view_update_1c3)
                    g_orig_view_update_1c3(g_active_1c3_view, 1u);
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "PILOT_UI_TOGGLE idx=%zu mod=%s enabled=%d saved=1",
                         idx, g_mod_names[idx], g_mod_enabled[idx]);
                ui_log(msg);
            }
            return 1u;
        }
    }
    return g_orig_1c3_confirm ? g_orig_1c3_confirm(ctrl, ev) : 1u;
}

static uint32_t pilot_btn_back_common(void *ctrl, void *ev,
                                      ctrl_event_fn orig_fn, const char *tag)
{
    char msg[112];
    snprintf(msg, sizeof(msg), "PILOT_UI_DBG %s in=%d depth=%d view=%p",
             tag, g_in_pilot_293, g_page_stack_depth, g_active_293_view);
    ui_log(msg);
    if (g_in_pilot_293 && !g_pilot_detail_active && g_page_stack_depth > 0 &&
        pilot_current_group()) {
        if (pilot_pop_to_root_list(ctrl)) {
            snprintf(msg, sizeof(msg), "PILOT_UI_DBG %s popped_to_root", tag);
            ui_log(msg);
            return 1u;
        }
    }
    uint32_t r = orig_fn ? orig_fn(ctrl, ev) : 1u;
    /* BACK is the screen-pop key (0x29ebb1c / 0x29ec030): it closes a detail
     * popup OR exits the list to the native menu. Only the latter leaves Pilot.
     * Detect it from the LIVE current-group truth-source after the native pop. */
    if (g_in_pilot_293) {
        typedef uint32_t *(*get_id_ptr_fn)(void);
        get_id_ptr_fn get_grp =
            (get_id_ptr_fn)(g_appfw_base + APPFW_GET_CUR_GROUP_OFF);
        uint32_t *grp_p = get_grp ? get_grp() : NULL;
        uint32_t grp = grp_p ? *grp_p : 0u;
        if (grp != PILOT_GROUP_ID) {
            snprintf(msg, sizeof(msg),
                     "PILOT_UI_DBG %s exit grp=0x%x reset", tag, grp);
            ui_log(msg);
            reset_pilot_293_state();
        } else {
            snprintf(msg, sizeof(msg), "PILOT_UI_DBG %s stay grp=0x535", tag);
            ui_log(msg);
        }
    }
    snprintf(msg, sizeof(msg), "PILOT_UI_DBG %s orig=0x%x", tag, r);
    ui_log(msg);
    return r;
}

static uint32_t pilot_btn_back(void *ctrl, void *ev)
{
    return pilot_btn_back_common(ctrl, ev, g_orig_btn_back, "btn_back");
}

static uint32_t pilot_btn_back2(void *ctrl, void *ev)
{
    return pilot_btn_back_common(ctrl, ev, g_orig_btn_back2, "btn_back2");
}

static PilotStatus install_pilot_menu_ui(void)
{
    if (!g_api || !g_api->memory_read || !g_api->memory_patch)
        return PILOT_STATUS_UNSUPPORTED;

    if (!dl_iterate_phdr(find_appfw_cb, &g_appfw_base) || !g_appfw_base) {
        ui_log("PILOT_UI_ERR appfw not found");
        return PILOT_STATUS_NOT_FOUND;
    }

    uintptr_t got_menu_val = 0, got_page_val = 0, got_tree_val = 0;
    uintptr_t got_layout_1c3 = 0, got_ctrl_items_1c3 = 0, got_layout_293 = 0,
              got_rid_val = 0;
    uintptr_t got_btn_confirm = 0, got_btn_right = 0, got_btn_left = 0,
              got_btn_back = 0, got_btn_back2 = 0, got_1c3_confirm = 0;
    if (g_api->memory_read(g_appfw_base + APPFW_GOT_MENU_TABLE, &got_menu_val,
                           sizeof(got_menu_val)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_PAGE_TABLE, &got_page_val,
                           sizeof(got_page_val)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_NONSETTING_TREE,
                           &got_tree_val, sizeof(got_tree_val)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_LAYOUT_1C3,
                           &got_layout_1c3, sizeof(got_layout_1c3)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_CTRL_ITEMS_1C3,
                           &got_ctrl_items_1c3,
                           sizeof(got_ctrl_items_1c3)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_LAYOUT_293,
                           &got_layout_293, sizeof(got_layout_293)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_BTN_CONFIRM,
                           &got_btn_confirm, sizeof(got_btn_confirm)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_BTN_RIGHT,
                           &got_btn_right, sizeof(got_btn_right)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_BTN_LEFT,
                           &got_btn_left, sizeof(got_btn_left)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_BTN_BACK,
                           &got_btn_back, sizeof(got_btn_back)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_BTN_BACK2,
                           &got_btn_back2, sizeof(got_btn_back2)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_1C3_CONFIRM,
                           &got_1c3_confirm, sizeof(got_1c3_confirm)) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_GOT_SET_STRING_RID,
                           &got_rid_val, sizeof(got_rid_val)) != PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR got read failed");
        return PILOT_STATUS_IO;
    }

    if (got_menu_val != g_appfw_base + APPFW_MENU_TABLE_OFF ||
        got_page_val != g_appfw_base + APPFW_PAGE_TABLE_OFF ||
        got_tree_val != g_appfw_base + APPFW_NONSETTING_TREE_OFF ||
        got_layout_1c3 != g_appfw_base + APPFW_LAYOUT_1C3_OFF ||
        got_ctrl_items_1c3 != g_appfw_base + APPFW_CTRL_ITEMS_1C3_OFF ||
        got_layout_293 != g_appfw_base + APPFW_LAYOUT_293_OFF) {
        ui_log("PILOT_UI_ERR unexpected table GOT");
        return PILOT_STATUS_CONFLICT;
    }
    g_orig_layout_1c3 = got_layout_1c3;
    g_orig_ctrl_items_1c3 = got_ctrl_items_1c3;
    g_orig_layout_293 = got_layout_293;

    load_menu_title_from_config();

    /* 1. Prepare cloned non-setting descriptor for group 0x535 -> screen 0x293 */
    if (g_api->memory_read(g_appfw_base + APPFW_STOCK_DESC_293_OFF,
                           g_stock_nonsetting_desc,
                           APPFW_DESC_SIZE) != PILOT_STATUS_OK ||
        g_api->memory_read(g_appfw_base + APPFW_STOCK_ITEM_DESC_OFF,
                           g_stock_item_desc,
                           APPFW_ITEM_DESC_SIZE) != PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR desc read failed");
        return PILOT_STATUS_IO;
    }
    memcpy(g_desc, g_stock_nonsetting_desc, APPFW_DESC_SIZE);
    /* 父行模板 0x4BF0090 已确认可见，不做归一化，避免回归。 */
    *(uint32_t *)(g_desc + 0x00ull) = PILOT_RID_MENU_LABEL;
    *(uint32_t *)(g_desc + 0x0Cull) = PILOT_LIST_SCREEN_ID;
    *(uintptr_t *)(g_desc + 0x20ull) = g_appfw_base + APPFW_VISIBILITY_OK_OFF;
    *(uint32_t *)(g_desc + 0x50ull) = 0u;

    /* 2. Prepare cloned menu table with 13th row in 操作自定义 */
    if (read_appfw_bytes(got_menu_val, g_menu_table, sizeof(g_menu_table)) !=
        PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR menu table read failed");
        return PILOT_STATUS_IO;
    }
    uint32_t *row_count = (uint32_t *)(g_menu_table + APPFW_CUSTOM_GROUP_OFF + 8u);
    struct menu_row_entry *slot12 =
        (struct menu_row_entry *)(g_menu_table + APPFW_CUSTOM_SLOT12_OFF);
    if (*row_count != APPFW_CUSTOM_STOCK_COUNT ||
        slot12->group_id != 0 || slot12->setting_desc != 0 ||
        slot12->non_setting_desc != 0) {
        ui_log("PILOT_UI_ERR custom group mismatch");
        return PILOT_STATUS_CONFLICT;
    }
    slot12->group_id = PILOT_GROUP_ID;
    slot12->setting_desc = 0;
    slot12->non_setting_desc = (uintptr_t)g_desc;
    *row_count = APPFW_CUSTOM_STOCK_COUNT + 1u;

    /* 3. Read stock 0x1C3 & 0x293 layouts and 0x1C3 controller focus table */
    if (read_appfw_bytes(got_layout_1c3, g_layout_1c3_stock,
                         sizeof(g_layout_1c3_stock)) != PILOT_STATUS_OK ||
        read_appfw_bytes(got_ctrl_items_1c3, g_ctrl_items_1c3_stock,
                         sizeof(g_ctrl_items_1c3_stock)) != PILOT_STATUS_OK ||
        read_appfw_bytes(got_layout_293, g_layout_293_stock,
                         sizeof(g_layout_293_stock)) != PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR layout read failed");
        return PILOT_STATUS_IO;
    }
    bind_detail_page_to_layout(&g_pilot_version_page);
    bind_list_page_to_layout(g_active_page);

    /* 4. Prepare cloned page factory table wrapping 0x1C3 & 0x293 factories */
    if (read_appfw_bytes(got_page_val, g_page_table, sizeof(g_page_table)) !=
        PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR page table read failed");
        return PILOT_STATUS_IO;
    }
    uint8_t *entry_1c3 =
        g_page_table + PILOT_DETAIL_SCREEN_ID * APPFW_PAGE_ENTRY_SIZE;
    g_orig_view_factory_1c3 =
        (view_factory_fn)(g_appfw_base + APPFW_VIEW_FACTORY_1C3_OFF);
    g_orig_ctrl_factory_1c3 =
        (ctrl_factory_fn)(g_appfw_base + APPFW_CTRL_FACTORY_1C3_OFF);
    *(uintptr_t *)(entry_1c3 + 0x00ull) = (uintptr_t)pilot_1c3_view_factory_hook;
    *(uintptr_t *)(entry_1c3 + 0x08ull) = (uintptr_t)pilot_1c3_ctrl_factory_hook;

    uint8_t *entry_292 =
        g_page_table + 0x292ull * APPFW_PAGE_ENTRY_SIZE;
    uintptr_t orig_292_ctrl = *(uintptr_t *)(entry_292 + 0x08ull);
    if (orig_292_ctrl) {
        g_orig_ctrl_factory_292 = (ctrl_factory_fn)orig_292_ctrl;
        *(uintptr_t *)(entry_292 + 0x08ull) =
            (uintptr_t)pilot_292_ctrl_factory_hook;
    }

    uint8_t *entry_293 =
        g_page_table + PILOT_LIST_SCREEN_ID * APPFW_PAGE_ENTRY_SIZE;
    g_orig_view_factory_293 =
        (view_factory_fn)(g_appfw_base + APPFW_VIEW_FACTORY_293_OFF);
    g_orig_ctrl_factory_293 =
        (ctrl_factory_fn)(g_appfw_base + APPFW_CTRL_FACTORY_293_OFF);
    *(uintptr_t *)(entry_293 + 0x00ull) = (uintptr_t)pilot_293_view_factory_hook;
    *(uintptr_t *)(entry_293 + 0x08ull) = (uintptr_t)pilot_293_ctrl_factory_hook;

    uint8_t *entry_260 =
        g_page_table + PILOT_BUSY_SCREEN_ID * APPFW_PAGE_ENTRY_SIZE;
    uintptr_t orig_260_ctrl = *(uintptr_t *)(entry_260 + 0x08ull);
    if (orig_260_ctrl) {
        g_orig_ctrl_factory_260 = (ctrl_factory_fn)orig_260_ctrl;
        *(uintptr_t *)(entry_260 + 0x08ull) =
            (uintptr_t)pilot_260_ctrl_factory_hook;
    }

    /* 5. Hook TextSprite::setString(uint32_t) */
    void *sym_rid = dlsym(RTLD_DEFAULT, "_ZN2ux6gfxeng10TextSprite9setStringEj");
    void *sym_cstr =
        dlsym(RTLD_DEFAULT, "_ZN2ux6gfxeng10TextSprite9setStringEPKc");
    if (!sym_rid || !sym_cstr) {
        ui_log("PILOT_UI_ERR dlsym TextSprite failed");
        return PILOT_STATUS_NOT_FOUND;
    }
    g_orig_set_string_rid = (text_set_rid_fn)sym_rid;
    g_orig_set_string_cstr = (text_set_cstr_fn)sym_cstr;
    g_orig_btn_confirm = (ctrl_event_fn)got_btn_confirm;
    g_orig_btn_right = (ctrl_event_fn)got_btn_right;
    g_orig_btn_left = (ctrl_event_fn)got_btn_left;
    g_orig_btn_back = (ctrl_event_fn)got_btn_back;
    g_orig_btn_back2 = (ctrl_event_fn)got_btn_back2;
    g_orig_1c3_confirm = (ctrl_event_fn)got_1c3_confirm;

    PilotStatus st = patch_got_ptr(g_appfw_base + APPFW_GOT_SET_STRING_RID,
                                   got_rid_val,
                                   (uintptr_t)pilot_set_string_rid_hook);
    if (st != PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR setString GOT patch failed");
        return st;
    }
    (void)patch_got_ptr(g_appfw_base + APPFW_GOT_BTN_CONFIRM, got_btn_confirm,
                        (uintptr_t)pilot_btn_confirm);
    (void)patch_got_ptr(g_appfw_base + APPFW_GOT_BTN_RIGHT, got_btn_right,
                        (uintptr_t)pilot_btn_right);
    (void)patch_got_ptr(g_appfw_base + APPFW_GOT_BTN_LEFT, got_btn_left,
                        (uintptr_t)pilot_btn_left);
    (void)patch_got_ptr(g_appfw_base + APPFW_GOT_BTN_BACK, got_btn_back,
                        (uintptr_t)pilot_btn_back);
    (void)patch_got_ptr(g_appfw_base + APPFW_GOT_BTN_BACK2, got_btn_back2,
                        (uintptr_t)pilot_btn_back2);
    (void)patch_got_ptr(g_appfw_base + APPFW_GOT_1C3_CONFIRM, got_1c3_confirm,
                        (uintptr_t)pilot_1c3_confirm_hook);

    /* 6. Register group 0x535 and item IDs in non-setting runtime tree */
    g_tree_entry.group_id = PILOT_GROUP_ID;
    g_tree_entry.desc_ptr = (uintptr_t)g_desc;
    g_active_page = resolve_page(&g_pilot_root_page);
    bind_list_page_to_layout(g_active_page);
    tree_insert_fn tree_insert =
        (tree_insert_fn)(g_appfw_base + APPFW_TREE_INSERT_OFF);
    tree_insert((void *)got_tree_val, &g_tree_entry, &g_tree_entry + 1);
    for (uint32_t i = 0; i < PILOT_LIST_ITEMS_MAX; ++i) {
        g_item_tree_entries[i].group_id = PILOT_ITEM_ID_BASE + i;
        g_item_tree_entries[i].desc_ptr = (uintptr_t)g_item_nonsetting_descs[i];
        tree_insert((void *)got_tree_val, &g_item_tree_entries[i],
                    &g_item_tree_entries[i] + 1);
        g_subitem_tree_entries[i].group_id = PILOT_SUBITEM_ID_BASE + i;
        g_subitem_tree_entries[i].desc_ptr =
            (uintptr_t)g_subitem_nonsetting_descs[i];
        tree_insert((void *)got_tree_val, &g_subitem_tree_entries[i],
                    &g_subitem_tree_entries[i] + 1);
    }

    /* 7. Publish page factory table GOT, then menu table GOT */
    st = patch_got_ptr(g_appfw_base + APPFW_GOT_PAGE_TABLE, got_page_val,
                       (uintptr_t)g_page_table);
    if (st != PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR page GOT patch failed");
        return st;
    }

    st = patch_got_ptr(g_appfw_base + APPFW_GOT_MENU_TABLE, got_menu_val,
                       (uintptr_t)g_menu_table);
    if (st != PILOT_STATUS_OK) {
        ui_log("PILOT_UI_ERR menu GOT patch failed");
        return st;
    }

    (void)install_pilot_option_2a7();
    install_pilot_lv_ui();

    ui_log("PILOT_UI_READY group=0x535 list=0x293 detail=0x1c3 nav=lifecycle-v1 focus=typed-layout-v2 lv=text-widget-v5");
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default")))
PilotStatus pilot_module_init(const PilotApi *api)
{
    if (!api || api->abi_version < PILOT_ABI_VERSION ||
        api->struct_size < sizeof(PilotApi))
        return PILOT_STATUS_INVALID_ARGUMENT;

    if (!api->version_string)
        return PILOT_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_lock);
    if (!g_installed) {
        g_version_components[0].label = api->version_string;
        g_api = api;
        PilotStatus st = install_pilot_menu_ui();
        if (st == PILOT_STATUS_OK)
            g_installed = 1;
        pthread_mutex_unlock(&g_lock);
        return st;
    }
    pthread_mutex_unlock(&g_lock);
    return PILOT_STATUS_OK;
}
