/* A7C II primary-menu glue for Pilot.
 *
 * Included after pilot_menu_model.inc and before pilot_menu_list.inc.  It owns
 * the only three things that need to know about Pilot and about appFw at the
 * same time:
 *
 *   1. the patching primitives (base discovery, /proc/self/maps protection
 *      lookup, atomic writes through the Pilot memory_patch API, branch
 *      encoding and the veneer page);
 *   2. the stock-menu hooks that give the synthetic row its title, record and
 *      value, and that ask the list transport to append the row;
 *   3. the install sequence and its preflight.
 *
 * The reference implementation (a7c2-re/report/2026-11-05-appfw-source-rebuild/
 * firmware_abi/a7c2_201_menu.c) also hooked the presenter factory, the
 * controller member and the controller exit to build a private page.  This port
 * does not need that scope: the row is an ordinary settings row whose record
 * points at the stock list screen, so the stock member opens the list screen and
 * the transport layer takes over there.  Consequently there is no custom_scope,
 * no construction latch and no page title; value_word()/format_value() gate on
 * the row being built instead. */

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Host GNU headers redirect sscanf() to C23; the guest glibc 2.25 exports this. */
extern int __isoc99_sscanf(const char *, const char *, ...) __attribute__((format(scanf, 2, 3)));

/* Transport entry points, defined in pilot_menu_list.inc further down. */
static int menu_list_prepare(void);
static void menu_list_release_unreferenced(void);
static void menu_list_root_entered(void *controller);
static const void *menu_list_record(uint32_t raw);
static int menu_list_owns(uint32_t raw);
static const char *menu_list_text(uint32_t resource);
static int menu_list_validate_catalog(void *util);
static void menu_list_input_release(unsigned int key, unsigned int status, unsigned long tid);

_Static_assert(sizeof(uintptr_t) == 8, "64-bit native ABI required");

volatile struct pilot_a7c2_observation pilot_menu_observation = {
    .magic = PILOT_A7C2_MENU_MAGIC, .abi_version = 1
};

static uintptr_t base;
static void *veneer_page;
static unsigned char own_record[0xc0] __attribute__((aligned(8)));
static uint32_t encoded_value;
static unsigned int catalog_valid, install_attempted;

/* Stock entry points.  Names follow the reference so the shared list transport
 * keeps working unchanged. */
static alias_fn original_alias;
static normal_fn original_normal;
static reverse_fn original_reverse;
static build_fn original_builder;
static void (*original_formatter)(void *);
static void (*original_root_enter)(void *, const void *);
static text_id_fn original_text_id;
static text_bytes_fn original_text_bytes;
static model_lock_fn lock_model, unlock_model;

struct patch {
    uintptr_t address;
    uint64_t before, after;
    unsigned int size, code, applied;
};
static struct patch patches[48];
static unsigned int patch_count, restoration_failed;

/* --- diagnostics -------------------------------------------------------- */

static void trace(const char *kind, const char *format, ...) {
    if (!g_api || !g_api->log || !g_api->log->print)
        return;
    char text[320];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    char line[384];
    int count = snprintf(line, sizeof line, "%s%s", kind, text);
    if (count > 0 && (size_t)count < sizeof line)
        g_api->log->print(line);
}

static void fault(unsigned int code, const char *operation) {
    pilot_menu_observation.last_error = code;
    trace("PILOT_MENU_ERR ", "code=%u operation=%s", code, operation);
}

/* --- primitive accessors ------------------------------------------------ */

static uint32_t u32(uintptr_t address) {
    uint32_t value;
    memcpy(&value, (const void *)address, sizeof value);
    return value;
}

static uintptr_t pointer(uintptr_t address) {
    uintptr_t value;
    memcpy(&value, (const void *)address, sizeof value);
    return value;
}

static int protection(uintptr_t address, size_t size) {
    if (!address || !size || address > UINTPTR_MAX - size)
        return -1;
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps)
        return -1;
    char line[512], permissions[5];
    unsigned long start, end;
    int result = -1;
    while (fgets(line, sizeof line, maps)) {
        if (__isoc99_sscanf(line, "%lx-%lx %4s", &start, &end, permissions) == 3 &&
                address >= start && address + size <= end) {
            result = (permissions[0] == 'r' ? PROT_READ : 0) |
                     (permissions[1] == 'w' ? PROT_WRITE : 0) |
                     (permissions[2] == 'x' ? PROT_EXEC : 0);
            break;
        }
    }
    fclose(maps);
    return result;
}

static void clear_instructions(uintptr_t begin, size_t length) {
#if defined(__aarch64__)
    uintptr_t ctr;
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
    uintptr_t data_line = 4ul << ((ctr >> 16) & 15u);
    uintptr_t instruction_line = 4ul << (ctr & 15u);
    for (uintptr_t at = begin & ~(data_line - 1); at < begin + length; at += data_line)
        __asm__ __volatile__("dc cvau, %0" :: "r"(at) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    for (uintptr_t at = begin & ~(instruction_line - 1); at < begin + length; at += instruction_line)
        __asm__ __volatile__("ic ivau, %0" :: "r"(at) : "memory");
    __asm__ __volatile__("dsb ish; isb" ::: "memory");
#else
    (void)begin;
    (void)length;
#endif
}

/* --- patching ----------------------------------------------------------- */

/* Validates the current contents, then applies the patch through the Pilot
 * memory_patch primitive, which already does mprotect + I-cache + restore and
 * keeps its own promise on the expected bytes. */
static int add_patch(uintptr_t address, uint64_t expected, uint64_t replacement,
                     unsigned int size, unsigned int code) {
    if (patch_count == sizeof patches / sizeof patches[0] || (size != 4 && size != 8) ||
            (address & (size - 1u)) || protection(address, size) < 0 ||
            (size == 4 ? u32(address) != (uint32_t)expected : pointer(address) != expected))
        return -1;
    if (!g_api || !g_api->memory_patch) {
        fault(2, "memory-patch-missing");
        return -1;
    }
    struct patch entry = {address, expected, replacement, size, code, 0};
    if (g_api->memory_patch(address, &entry.before, &entry.after, size) != PILOT_STATUS_OK) {
        fault(3, "memory-patch");
        return -1;
    }
    entry.applied = 1;
    patches[patch_count++] = entry;
    return 0;
}

static void rollback(void) {
    for (unsigned int index = patch_count; index; --index) {
        struct patch *p = &patches[index - 1];
        if (!p->applied)
            continue;
        uint64_t current = 0;
        memcpy(&current, (const void *)p->address, p->size);
        if (current != p->after) {
            fault(90, "patch-content-changed");
            restoration_failed = 1;
            continue;
        }
        if (g_api->memory_patch(p->address, &p->after, &p->before, p->size) != PILOT_STATUS_OK) {
            restoration_failed = 1;
            fault(90, "restore-patch");
        } else {
            p->applied = 0;
        }
    }
}

static int branch_word(uintptr_t from, uintptr_t to, uint32_t *out) {
    int64_t delta = (int64_t)to - (int64_t)from;
    if (delta < -(INT64_C(1) << 27) || delta >= (INT64_C(1) << 27) || (delta & 3))
        return -1;
    *out = 0x94000000u | ((uint32_t)(delta >> 2) & 0x03ffffffu);
    return 0;
}

/* One page that a direct branch reaches from all four patched call sites. */
static void *near_page(uintptr_t call) {
    (void)call;
    static const uintptr_t sites[4] = {PAGE_VALUE_CALL, PAGE_FORMAT_CALL, ALIAS_CALL, REVERSE_CALL};
    const uintptr_t reach = 0x8000000ul;
    uintptr_t highest = 0, lowest = UINTPTR_MAX;
    for (unsigned int i = 0; i < 4; ++i) {
        uintptr_t at = base + sites[i];
        if (at > highest)
            highest = at;
        if (at < lowest)
            lowest = at;
    }
    uintptr_t lo = highest - reach + 0x10000ul;
    uintptr_t hi = lowest + reach - 0x10000ul;
    lo = (lo + 0xfffful) & ~0xfffful;
    for (uintptr_t at = lo; at + 4096 <= hi; at += 0x10000ul) {
        void *page = mmap((void *)at, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED)
            continue;
        if ((uintptr_t)page >= lo && (uintptr_t)page + 4096 <= hi)
            return page;
        munmap(page, 4096);
    }
    return 0;
}

/* --- scopes ------------------------------------------------------------- */

static void latch_ui_thread(void) {
    if (!pilot_menu_observation.ui_tid)
        pilot_menu_observation.ui_tid = (uint32_t)syscall(SYS_gettid);
}

static int ui_thread(void) {
    return pilot_menu_observation.ui_tid &&
           pilot_menu_observation.ui_tid == (uint32_t)syscall(SYS_gettid);
}

static int primary_lookup_scope(void) {
    return catalog_valid && ui_thread();
}

/* True while the stock menu is building our own row.  It is the port's
 * replacement for the reference's custom_scope: the row is rendered by the
 * ordinary settings page, so the only thing that distinguishes it is the item
 * the menu is currently constructing. */
static int pilot_row(void) {
    return pilot_menu_observation.installed && ui_thread() && catalog_valid &&
           u32(base + CURRENT_ITEM) == NEW_ITEM;
}

static int presenter_valid(void *presenter) {
    uintptr_t p = (uintptr_t)presenter;
    return p && protection(p, 0x2b16) >= 0 && pointer(p) == base + PRESENTER_VTABLE &&
           pointer(base + PRESENTER_TABLE) == p;
}

/* --- text --------------------------------------------------------------- */

static void set_text(void *sprite, uint32_t id) {
    latch_ui_thread();
    const char *text = menu_list_text(id);
    if (text) {
        original_text_bytes(sprite, text);
        return;
    }
    if (id == NEW_TITLE && pilot_row()) {
        original_text_bytes(sprite, pilot_menu_title());
        return;
    }
    original_text_id(sprite, id);
}

/* --- item identity ------------------------------------------------------ */

static int32_t alias_key(void *self, int32_t item) {
    if (pilot_menu_observation.installed && primary_lookup_scope() &&
            ((uint32_t)item == NEW_ITEM || menu_list_owns((uint32_t)item)))
        return item;
    return original_alias(self, item);
}

static const void *normal_record(void *self, int32_t key) {
    if (pilot_menu_observation.installed && primary_lookup_scope()) {
        const void *record = menu_list_record((uint32_t)key);
        if (record)
            return record;
        if ((uint32_t)key == NEW_ITEM)
            return own_record;
    }
    return original_normal(self, key);
}

static uint32_t reverse_item(void *self, int32_t key) {
    if (pilot_menu_observation.installed && primary_lookup_scope() &&
            ((uint32_t)key == NEW_ITEM || menu_list_owns((uint32_t)key)))
        return (uint32_t)key;
    return original_reverse(self, key);
}

/* --- row value ---------------------------------------------------------- */

static const uint32_t *value_word(void) {
    if (!pilot_row())
        return (const uint32_t *)(base + ORIGINAL_VALUE);
    ++pilot_menu_observation.value_reads;
    encoded_value = 0x01000000u;
    return &encoded_value;
}

static void format_value(void *presenter) {
    original_formatter(presenter);
    if (!pilot_row() || !presenter_valid(presenter))
        return;
    if (__builtin_bswap32(u32((uintptr_t)presenter + 0xf68)) != encoded_value)
        return;
    const char *label = "Pilot";
    if (strlen(label) + 1u > 9u) {
        fault(22, "value-label-too-long");
        return;
    }
    /* The original formatter installed this nine-byte buffer in control3. */
    memset((unsigned char *)presenter + 0x2b0d, 0, 9);
    memcpy((unsigned char *)presenter + 0x2b0d, label, strlen(label));
}

/* --- row insertion ------------------------------------------------------ */

static int cache_append(uintptr_t cache) {
    static const uint32_t originals[7] = {0x446, 0x44b, 0x44f, 0x450, 0x452, 0x456, 0x457};
    if (!catalog_valid || !ui_thread() ||
            (cache != base + PRIMARY_CACHE && cache != base + PRESENTER_CACHE &&
             cache != base + ITEM_PRESENTER_CACHE))
        return 0;
    unsigned char guard[8] __attribute__((aligned(8))) = {0};
    lock_model(guard);
    uintptr_t group = cache + 8 + 7 * 0xec8, page = cache + 12 + 7 * 0xec8 + 12 * 0xfc;
    uintptr_t entry7 = page + 12 + 7 * 8;
    int result = -1;
    int writable = protection(cache, 0x7668);
    if (writable < 0 || (writable & (PROT_READ | PROT_WRITE)) != (PROT_READ | PROT_WRITE) ||
            u32(cache + 4) != 8 || u32(group) != 13 || u32(cache + 0x7648 + 7 * 4) != 7 ||
            u32(page) != 0x02006030 || u32(page + 4) != 0x3d)
        goto done;
    uint32_t count = u32(page + 8), total = u32(cache);
    if ((count != 7 && count != 8) || total == 0 || total == UINT32_MAX)
        goto done;
    for (unsigned int i = 0; i < 7; ++i)
        if (u32(page + 12 + i * 8) != originals[i] || u32(page + 16 + i * 8) != total - (count == 8))
            goto done;
    if (count == 8) {
        if (u32(entry7) != NEW_ITEM || u32(entry7 + 4) != total)
            goto done;
        result = 0;
        goto done;
    }
    uint32_t old_entry[2];
    memcpy(old_entry, (const void *)entry7, sizeof old_entry);
    uint32_t entry[2] = {NEW_ITEM, total + 1}, next = total + 1, eight = 8;
    memcpy((void *)entry7, entry, sizeof entry);
    memcpy((void *)cache, &next, sizeof next);
    memcpy((void *)(page + 8), &eight, sizeof eight);
    if (u32(entry7) != NEW_ITEM || u32(entry7 + 4) != next || u32(page + 8) != 8 || u32(cache) != next) {
        memcpy((void *)entry7, old_entry, sizeof old_entry);
        memcpy((void *)cache, &total, sizeof total);
        memcpy((void *)(page + 8), &count, sizeof count);
        goto done;
    }
    result = 1;
done:
    if (result >= 0) {
        if (cache == base + PRIMARY_CACHE)
            pilot_menu_observation.primary_rows = 8;
        if (pilot_menu_observation.last_error == 31 || pilot_menu_observation.last_error == 32)
            pilot_menu_observation.last_error = 0;
    }
    unlock_model(guard);
    if (result > 0)
        trace("PILOT_MENU_ROW ", "item=%#x page=12 rows=8 original-rows-preserved=7 cache=appFw+%#lx",
              NEW_ITEM, (unsigned long)(cache - base));
    else if (result < 0)
        fault(31, "primary-cache-preflight");
    return result;
}

static int validate_catalog(void *self) {
    if (catalog_valid)
        return 1;
    unsigned char guard[8] __attribute__((aligned(8))) = {0};
    lock_model(guard);
    int valid = pointer(base + CATALOG_SERIAL) == 1425 && pointer(base + CATALOG_COUNT) == 1537 &&
                original_alias(self, NEW_ITEM) == (int32_t)NEW_ALIAS &&
                original_reverse(self, NEW_ITEM) == NEW_REVERSE &&
                original_normal(self, NEW_ITEM) == (const void *)(base + F_NEW_NORMAL);
    unlock_model(guard);
    if (!valid) {
        fault(32, "runtime-new-id-conflict-or-catalog");
        return 0;
    }
    if (!menu_list_validate_catalog(self))
        return 0;
    catalog_valid = 1;
    return 1;
}

static void build_tree(void *self, int32_t mode, void *cache) {
    original_builder(self, mode, cache);
    if (!pilot_menu_observation.installed || mode != 0)
        return;
    uintptr_t at = (uintptr_t)cache;
    if (at != base + PRIMARY_CACHE && at != base + PRESENTER_CACHE &&
            at != base + ITEM_PRESENTER_CACHE)
        return;
    if (validate_catalog(self))
        cache_append(at);
}

/* --- root focus restore ------------------------------------------------- */

static void root_enter(void *controller, const void *previous_screen) {
    original_root_enter(controller, previous_screen);
    menu_list_root_entered(controller);
    ++pilot_menu_observation.generation;
}

/* --- install ------------------------------------------------------------ */

static int prepare_veneer(void) {
    veneer_page = near_page(base + PAGE_VALUE_CALL);
    if (!veneer_page) {
        fault(4, "near-page");
        return -1;
    }
    unsigned char bytes[64];
    uint32_t jump[2] = {0x58000050u, 0xd61f0200u};
    uintptr_t functions[4] = {(uintptr_t)value_word, (uintptr_t)format_value,
                              (uintptr_t)alias_key, (uintptr_t)reverse_item};
    for (unsigned int i = 0; i < 4; ++i) {
        memcpy(bytes + i * 16, jump, sizeof jump);
        memcpy(bytes + i * 16 + 8, &functions[i], sizeof functions[i]);
    }
    memcpy(veneer_page, bytes, sizeof bytes);
    clear_instructions((uintptr_t)veneer_page, sizeof bytes);
    if (mprotect(veneer_page, 4096, PROT_READ | PROT_EXEC) != 0) {
        munmap(veneer_page, 4096);
        veneer_page = 0;
        fault(5, "veneer-mprotect");
        return -1;
    }
    return 0;
}

static void release_veneer_if_unreferenced(void) {
    for (unsigned int i = 0; i < patch_count; ++i)
        if (patches[i].applied && patches[i].code)
            return;
    if (veneer_page) {
        munmap(veneer_page, 4096);
        veneer_page = 0;
    }
}

static int find_libobj_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    if (!info->dlpi_name || !strstr(info->dlpi_name, LIBOB_NAME))
        return 0;
    *(uintptr_t *)data = (uintptr_t)info->dlpi_addr;
    return 1;
}
static int pilot_install(void) {
    if (!g_api)
        return -1;
    if (install_attempted)
        return -1;
    install_attempted = 1;
    if (!g_appfw_base)
        dl_iterate_phdr(find_appfw_cb, &g_appfw_base);
    base = g_appfw_base;
    if (!base || base > UINTPTR_MAX - 0x9000000ul || sysconf(_SC_PAGESIZE) != 4096) {
        fault(1, "appfw-base");
        return -1;
    }
    pilot_menu_observation.appfw_base = base;
    original_alias = (alias_fn)(base + F_ALIAS);
    original_normal = (normal_fn)(base + F_NORMAL);
    original_reverse = (reverse_fn)(base + F_REVERSE);
    original_builder = (build_fn)(base + F_BUILD);
    original_root_enter = (void (*)(void *, const void *))(base + F_ROOT_ENTER);
    original_formatter = (void (*)(void *))(base + F_FORMATTER);
    lock_model = (model_lock_fn)(base + F_MODEL_LOCK);
    unlock_model = (model_lock_fn)(base + F_MODEL_UNLOCK);

    if (protection(base + INFO_RECORD, 0xc0) < 0 || protection(base + ITEM_UTIL_VTABLE, 0x7b8) < 0 ||
            u32(base + INFO_RECORD) != 0x021c6542 || u32(base + INFO_RECORD + 0x20) != INFO_SCREEN ||
            pointer(base + INFO_RECORD + 0x70) != base + F_INFO_TITLE_GETTER ||
            u32(base + F_INFO_TITLE_GETTER) != 0x52800020 ||
            u32(base + F_INFO_TITLE_GETTER + 4) != 0xd65f03c0 ||
            pointer(base + PRESENTER_VTABLE + 0x10) != base + F_PRESENTER_UPDATE ||
            pointer(base + PRESENTER_VTABLE + 0x28) != base + F_PRESENTER_REBUILD) {
        fault(6, "preflight-info-record");
        return -2;
    }
    const unsigned int zero_slots[] = {0x78, 0x80, 0x98, 0xa8, 0xb0};
    for (unsigned int i = 0; i < sizeof zero_slots / sizeof zero_slots[0]; ++i)
        if (pointer(base + INFO_RECORD + zero_slots[i])) {
            fault(7, "preflight-template-callback");
            return -2;
        }
    memcpy(own_record, (const void *)(base + INFO_RECORD), sizeof own_record);
    uint32_t title = NEW_TITLE;
    memcpy(own_record, &title, sizeof title);

    /* libObj is already resident (the stock app links against it), so its link
     * map entry is enough: the two TextSprite entry points sit at fixed offsets
     * from its load base.  Resolving them here rather than through dlopen keeps
     * the module free of the GLIBC_2.34 libdl stubs. */
    uintptr_t libobj_base = 0;
    dl_iterate_phdr(find_libobj_cb, &libobj_base);
    if (!libobj_base) {
        fault(8, "libObj-missing");
        return -3;
    }
    original_text_id = (text_id_fn)(libobj_base + LIBOB_SET_ID_OFF);
    original_text_bytes = (text_bytes_fn)(libobj_base + LIBOB_SET_BYTES_OFF);
    uintptr_t old_text = pointer(base + TEXT_UINT_SLOT);
    if (old_text != base + 0x165cfd0 && old_text != (uintptr_t)original_text_id) {
        fault(10, "setString-slot");
        return -3;
    }

    if (prepare_veneer() != 0)
        return -4;
    uint32_t value_branch, format_branch, alias_branch, reverse_branch;
    if (branch_word(base + PAGE_VALUE_CALL, (uintptr_t)veneer_page, &value_branch) ||
            branch_word(base + PAGE_FORMAT_CALL, (uintptr_t)veneer_page + 16, &format_branch) ||
            branch_word(base + ALIAS_CALL, (uintptr_t)veneer_page + 32, &alias_branch) ||
            branch_word(base + REVERSE_CALL, (uintptr_t)veneer_page + 48, &reverse_branch)) {
        release_veneer_if_unreferenced();
        fault(11, "branch-encode");
        return -4;
    }

    patch_count = 0;
#define SLOT(va, old, new) add_patch(base + (va), (uintptr_t)(old), (uintptr_t)(new), 8, 0)
    if (SLOT(ITEM_UTIL_VTABLE + 0x730, original_alias, alias_key) ||
            SLOT(ITEM_UTIL_VTABLE + 0x18, original_normal, normal_record) ||
            SLOT(ITEM_UTIL_VTABLE + 0x738, original_reverse, reverse_item) ||
            SLOT(ITEM_UTIL_VTABLE + 0x7b0, original_builder, build_tree) ||
            add_patch(base + TEXT_UINT_SLOT, old_text, (uintptr_t)set_text, 8, 0) ||
            add_patch(base + PAGE_VALUE_CALL, 0x9421e3d5u, value_branch, 4, 1) ||
            add_patch(base + PAGE_FORMAT_CALL, 0x94155c0cu, format_branch, 4, 1) ||
            add_patch(base + ALIAS_CALL, 0x97ffe42du, alias_branch, 4, 1) ||
            add_patch(base + REVERSE_CALL, 0x97ffe451u, reverse_branch, 4, 1) ||
            SLOT(ROOT_ENTER_SLOT, original_root_enter, root_enter)) {
        fault(12, "row-patch");
        rollback();
        release_veneer_if_unreferenced();
        return -5;
    }
#undef SLOT

    /* Silences an unused-function warning for the release hook the port does not
     * register: the stock menu already reports key releases through dispatch. */
    (void)menu_list_input_release;

    if (menu_list_prepare() != 0) {
        fault(13, "list-prepare");
        menu_list_release_unreferenced();
        rollback();
        release_veneer_if_unreferenced();
        return -8;
    }
    pilot_menu_observation.installed = 1;
    pilot_menu_observation.patches = patch_count;
    trace("PILOT_MENU_READY ", "patches=%u item=%#x template=%#x list=0x%x abi=%u", patch_count, NEW_ITEM,
          INFO_SCREEN, PILOT_LIST_SCREEN, (unsigned)PILOT_ABI_VERSION);
    return 0;
}
