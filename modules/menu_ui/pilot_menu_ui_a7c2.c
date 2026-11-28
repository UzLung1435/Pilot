/*
 * Pilot menu_ui -- Sony ILCE-7CM2 (A7C II) 2.01 backend.
 *
 * The A6700 build (pilot_menu_ui.c) injects its menu through five patches:
 * the monolithic menu table, the non-setting registration tree, the page
 * table, the per-screen layout arrays and the screen factories/buttons.
 *
 * The A7C2 has the same *menu group table* and the same *page table*
 * structures (see offsets_a7c2.h), but its screens are built from named
 * widgets instead of layout arrays, so the A6700 approach cannot be
 * reproduced verbatim.  This backend therefore uses the two mechanisms
 * that do exist on this model:
 *
 *   1. copy the menu group table (0x15480 bytes) and append one Pilot group
 *      in category 0, then point the GOT slot at the copy -- this makes the
 *      Pilot entry show up in the native menu;
 *   2. patch TextSprite::setString(unsigned) so that any RID in the Pilot
 *      private range (0x02FFFxxx) is rendered from this module's text table
 *      instead of the firmware resource -- this is how the Pilot rows get
 *      their labels and live values, and it is the same idea as the A6700
 *      pilot_set_string_rid_hook.
 *
 * See a7c2_reverse_notes.md for the derivation of every offset.
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../../patch/pilot.h"
#include "pilot_ui.h"
#include "offsets_a7c2.h"

PILOT_DECLARE_MODULE_MODEL();

/* ------------------------------------------------------------------ */
/* Pilot identifiers (kept in sync with the A6700 build)               */
/* ------------------------------------------------------------------ */

#define PILOT_GROUP_ID           0x535u
#define PILOT_ITEM_ID_BASE       0x1F00u
#define PILOT_GROUP_INDEX_FREE   0x3Fu /* first unused global group index */

#define PILOT_RID_MENU_LABEL     0x02FFFF00u
#define PILOT_RID_LIST_ITEM_BASE 0x02FFFF20u
#define PILOT_RID_LIST_ITEM_MAX  0x20u

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

static const PilotApi *g_api;
static uintptr_t g_appfw_base;

static uint8_t *g_menu_copy;    /* APPFW_MENU_TABLE_SIZE bytes */
static uint8_t *g_desc_pool;    /* row_count * APPFW_DESC_SIZE */
static size_t g_row_count;

static void *g_orig_set_string_rid;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

struct rid_text {
    uint32_t rid;
    const char *text;
};
static struct rid_text g_rid_texts[PILOT_RID_LIST_ITEM_MAX + 4u];
static size_t g_rid_text_count;

/*
 * rid_text_set() hands the pointer to the renderer, so the storage has to
 * outlive the call.  Every entry owns a fixed slot the string is copied
 * into; callers may therefore pass stack buffers.
 */
#define PILOT_RID_TEXT_MAX 64u
static char g_rid_text_pool[PILOT_RID_LIST_ITEM_MAX + 4u][PILOT_RID_TEXT_MAX];

/* ------------------------------------------------------------------ */
/* Logging and memory helpers                                         */
/* ------------------------------------------------------------------ */

/*
 * The module log file lives on the SD card, and the emulator cannot write the
 * card back once the guest has faulted -- whatever happened right before the
 * crash is lost.  Mirror every message to the kernel ring buffer (or the
 * console device when kmsg is unavailable) so the serial log keeps it.
 */
static int g_console_fd = -1;
static int g_console_is_kmsg;

static void console_emit(const char *line)
{
    char buf[320];
    int len;

    if (g_console_fd < 0) {
        g_console_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
        g_console_is_kmsg = 1;
        if (g_console_fd < 0) {
            /* Non-blocking: an unread console must never stall the hook. */
            g_console_fd = open("/dev/console", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
            g_console_is_kmsg = 0;
        }
        if (g_console_fd < 0)
            return;
    }

    if (g_console_is_kmsg)
        len = snprintf(buf, sizeof(buf), "<6>pilot menu_ui: %s\n", line);
    else
        len = snprintf(buf, sizeof(buf), "pilot menu_ui: %s\n", line);
    if (len > 0) {
        ssize_t ignored = write(g_console_fd, buf, (size_t)len);

        (void)ignored;
    }
}

static void log_msg(const char *fmt, ...)
{
    char buf[320];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (g_api && g_api->mlog && g_api->mlog->print)
        (void)g_api->mlog->print(g_api->mlog, buf);
    console_emit(buf);
}

/* Hex dump helper: the A7C II menu code walks these structures blindly, so a
 * field that is merely "wrong" shows up as a wild pointer deep inside appFw. */
static void log_hex(const char *what, const void *ptr, size_t len)
{
    const unsigned char *p = (const unsigned char *)ptr;
    char out[256];
    size_t i;
    int pos;

    pos = snprintf(out, sizeof(out), "%s:", what);
    for (i = 0; i < len && pos > 0 && (size_t)pos + 3u < sizeof(out); ++i)
        pos += snprintf(out + pos, sizeof(out) - (size_t)pos, " %02x", p[i]);
    log_msg("%s", out);
}

static int appfw_base_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    if (!info || !info->dlpi_name)
        return 0;
    if (!strstr(info->dlpi_name, "appFw.so"))
        return 0;
    *(uintptr_t *)data = (uintptr_t)info->dlpi_addr;
    return 1;
}

static int read_mem_status(uintptr_t off, void *dst, size_t len,
                           PilotStatus *status)
{
    size_t done = 0;

    if (status)
        *status = PILOT_STATUS_INVALID_ARGUMENT;
    if (!g_api || !g_api->memory_read || !g_appfw_base)
        return -1;
    /*
     * pilot_memory_read_impl() rejects any request longer than one page
     * (locate_appfw_range(): "length > 4096"), and the menu table is
     * 0x15480 bytes, so walk it in page sized chunks.
     */
    while (done < len) {
        size_t chunk = len - done;
        PilotStatus rc;

        if (chunk > 4096u)
            chunk = 4096u;
        rc = g_api->memory_read(g_appfw_base + off + done,
                                (uint8_t *)dst + done, chunk);
        if (rc != PILOT_STATUS_OK) {
            if (status)
                *status = rc;
            return -1;
        }
        done += chunk;
    }
    if (status)
        *status = PILOT_STATUS_OK;
    return 0;
}

static int read_mem(uintptr_t off, void *dst, size_t len)
{
    return read_mem_status(off, dst, len, NULL);
}

static int patch_mem(uintptr_t off, const void *src, size_t len)
{
    uint8_t expected[16];

    if (!g_api || !g_api->memory_patch || !g_appfw_base)
        return -1;
    if (len > sizeof(expected))
        return -1;
    if (read_mem(off, expected, len) != 0)
        return -1;
    return g_api->memory_patch(g_appfw_base + off, expected, src, len) ==
                   PILOT_STATUS_OK
               ? 0
               : -1;
}

static uint32_t get_u32(const void *base, size_t off)
{
    uint32_t val = 0;

    memcpy(&val, (const uint8_t *)base + off, sizeof(val));
    return val;
}

static void put_u32(void *base, size_t off, uint32_t val)
{
    memcpy((uint8_t *)base + off, &val, sizeof(val));
}

static void put_u64(void *base, size_t off, uint64_t val)
{
    memcpy((uint8_t *)base + off, &val, sizeof(val));
}

/* ------------------------------------------------------------------ */
/* RID -> text table                                                  */
/* ------------------------------------------------------------------ */

static void rid_text_set(uint32_t rid, const char *text)
{
    size_t i;
    size_t slot;

    if (!text)
        return;

    pthread_mutex_lock(&g_lock);
    for (i = 0; i < g_rid_text_count; ++i) {
        if (g_rid_texts[i].rid == rid) {
            strncpy(g_rid_text_pool[i], text, PILOT_RID_TEXT_MAX - 1u);
            g_rid_text_pool[i][PILOT_RID_TEXT_MAX - 1u] = '\0';
            g_rid_texts[i].text = g_rid_text_pool[i];
            pthread_mutex_unlock(&g_lock);
            return;
        }
    }
    if (g_rid_text_count < sizeof(g_rid_texts) / sizeof(g_rid_texts[0])) {
        slot = g_rid_text_count;
        strncpy(g_rid_text_pool[slot], text, PILOT_RID_TEXT_MAX - 1u);
        g_rid_text_pool[slot][PILOT_RID_TEXT_MAX - 1u] = '\0';
        g_rid_texts[slot].rid = rid;
        g_rid_texts[slot].text = g_rid_text_pool[slot];
        g_rid_text_count++;
    }
    pthread_mutex_unlock(&g_lock);
}

static const char *rid_text_get(uint32_t rid)
{
    size_t i;
    const char *found = NULL;

    pthread_mutex_lock(&g_lock);
    for (i = 0; i < g_rid_text_count; ++i) {
        if (g_rid_texts[i].rid == rid) {
            found = g_rid_texts[i].text;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return found;
}

/* ------------------------------------------------------------------ */
/* TextSprite::setString(unsigned) hook                               */
/* ------------------------------------------------------------------ */

typedef void *(*set_string_rid_fn)(void *self, uint32_t rid);
typedef void *(*set_string_cstr_fn)(void *self, const char *text);

static set_string_cstr_fn g_set_string_cstr;

static void *pilot_set_string_rid_hook(void *self, uint32_t rid)
{
    set_string_rid_fn orig = (set_string_rid_fn)g_orig_set_string_rid;

    if (!orig)
        return NULL;

    if (rid >= PILOT_RID_MENU_LABEL &&
        rid <= PILOT_RID_LIST_ITEM_BASE + PILOT_RID_LIST_ITEM_MAX) {
        const char *text = rid_text_get(rid);

        if (text) {
            if (!g_set_string_cstr)
                g_set_string_cstr = (set_string_cstr_fn)dlsym(
                    RTLD_DEFAULT, "_ZN2ux6gfxeng10TextSprite9setStringEPKc");
            if (g_set_string_cstr)
                return g_set_string_cstr(self, text);
        }
    }
    return orig(self, rid);
}

static int install_set_string_hook(void)
{
    uintptr_t got = 0;
    set_string_rid_fn hook = pilot_set_string_rid_hook;

    if (read_mem(APPFW_GOT_SET_STRING_RID, &got, sizeof(got)) != 0) {
        log_msg("setString GOT unreadable");
        return -1;
    }
    if (!got) {
        log_msg("setString GOT is null");
        return -1;
    }
    if (patch_mem(APPFW_GOT_SET_STRING_RID, &hook, sizeof(hook)) != 0) {
        log_msg("setString GOT patch failed");
        return -1;
    }
    g_orig_set_string_rid = (void *)got;
    log_msg("setString hook installed, original=%p", g_orig_set_string_rid);
    return 0;
}

/* ------------------------------------------------------------------ */
/* view_string::draw hook                                             */
/* ------------------------------------------------------------------ */
/*
 * Build with -DPILOT_A7C2_NO_INLINE_HOOK to keep only the GOT-level
 * setString hook.  Useful as a control build if the inline hook misbehaves
 * on a firmware revision whose prologue bytes we have not verified.
 */
#ifndef PILOT_A7C2_NO_INLINE_HOOK
/*
 * Every piece of menu text in A7C II finally goes through
 * `view_string::draw` at appFw + A7C2_VS_DRAW_OFF: it reads either a
 * text RID (+0x208) or a plain C string (+0x210) from the view object
 * and feeds a TextSprite with it.  Rewriting that pair *before* the
 * original runs is the A7C II equivalent of A6700's "patch the layout
 * item's RID": A7C II builds its screens from named widgets, so there
 * is no layout array to patch, but every text still passes here.
 */

__attribute__((visibility("hidden"))) void *g_a7c2_vs_trampoline;
static unsigned int g_a7c2_vs_bad_calls;
static unsigned int g_a7c2_vs_hits;

__attribute__((noinline, used)) static void a7c2_view_string_fixup(void *view)
{
    uint32_t rid;
    const char *text;

    /*
     * A stray caller would otherwise fault on the very next access, exactly
     * like the original prologue would; ignore anything that cannot be a
     * user-space pointer so the camera keeps running.
     */
    if ((uintptr_t)view < 0x1000u || (uintptr_t)view >= (1ull << 48)) {
        if (g_a7c2_vs_bad_calls++ == 0)
            log_msg("view_string: unexpected view %p, ignored", view);
        return;
    }
    rid = get_u32(view, A7C2_VS_RID_OFF);
    if (rid < PILOT_RID_MENU_LABEL ||
        rid > PILOT_RID_LIST_ITEM_BASE + PILOT_RID_LIST_ITEM_MAX)
        return;
    text = rid_text_get(rid);
    if (!text)
        return;
    put_u32(view, A7C2_VS_RID_OFF, 0xFFFFFFFFu); /* fall back to the C string */
    put_u64(view, A7C2_VS_CSTR_OFF, (uint64_t)(uintptr_t)text);
    if (g_a7c2_vs_hits < 8u) {
        ++g_a7c2_vs_hits;
        log_msg("view_string: rid 0x%x -> \"%s\"", (unsigned)rid, text);
    }
}

/*
 * AArch64 GCC does not implement __attribute__((naked)), so the entry
 * stub is emitted through a global asm block.  It saves the frame, lets
 * the C fixup rewrite the view object, then branches into the relocated
 * prologue in g_a7c2_vs_trampoline.
 */
extern void a7c2_view_string_entry(void);

__asm__(
    ".text\n"
    ".hidden a7c2_view_string_entry\n"
    ".global a7c2_view_string_entry\n"
    ".type   a7c2_view_string_entry, %function\n"
    ".p2align 2\n"
    "a7c2_view_string_entry:\n"
    "  stp x29, x30, [sp, #-32]!\n"
    "  mov x29, sp\n"
    "  stp x0, x1, [sp, #16]\n"
    "  bl  a7c2_view_string_fixup\n"
    "  ldp x0, x1, [sp, #16]\n"
    "  ldp x29, x30, [sp], #32\n"
    "  adrp x16, g_a7c2_vs_trampoline\n"
    "  ldr  x16, [x16, :lo12:g_a7c2_vs_trampoline]\n"
    "  br  x16\n"
    ".size a7c2_view_string_entry, .-a7c2_view_string_entry\n");

static int install_view_string_hook(void)
{
    static const uint32_t prologue[4] = {
        0xA9AD7BFDu, /* stp x29, x30, [sp, #-0x130]! */
        0x910003FDu, /* mov x29, sp                 */
        0x39481001u, /* ldrb w1, [x0, #0x204]       */
        0xA90153F3u, /* stp x19, x20, [sp, #-0x120] */
    };
    uintptr_t target = g_appfw_base + A7C2_VS_DRAW_OFF;
    uint8_t patch[16];
    uint32_t seen[4];
    uint32_t header[2];
    uint64_t cont;
    uint64_t hook;
    uint32_t *page;
    void *raw;

    if (g_api->memory_read((uint64_t)target, seen, sizeof(seen)) !=
        PILOT_STATUS_OK) {
        log_msg("view_string: prologue unreadable");
        return -1;
    }
    if (memcmp(seen, prologue, sizeof(prologue)) != 0) {
        log_msg("view_string: unexpected prologue, hook skipped");
        return -1;
    }

    raw = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) {
        log_msg("view_string: mmap failed");
        return -1;
    }
    page = (uint32_t *)raw;
    cont = (uint64_t)target + 16u;
    page[0] = prologue[0];
    page[1] = prologue[1];
    page[2] = prologue[2];
    page[3] = prologue[3];
    page[4] = 0x58000050u; /* ldr x16, #8 */
    page[5] = 0xD61F0200u; /* br  x16     */
    memcpy(&page[6], &cont, sizeof(cont));
    g_a7c2_vs_trampoline = page;
    __builtin___clear_cache((char *)page, (char *)page + 32);

    hook = (uint64_t)(uintptr_t)a7c2_view_string_entry;
    header[0] = 0x58000050u;
    header[1] = 0xD61F0200u;
    memcpy(patch, header, sizeof(header));
    memcpy(&patch[8], &hook, sizeof(hook));
    if (g_api->memory_patch((uint64_t)target, seen, patch, sizeof(patch)) !=
        PILOT_STATUS_OK) {
        log_msg("view_string: patch failed, hook skipped");
        return -1;
    }
    __builtin___clear_cache((char *)target, (char *)target + 16);
    log_msg("view_string hook at %p", (void *)target);
    return 0;
}
#endif /* PILOT_A7C2_NO_INLINE_HOOK */

/* ------------------------------------------------------------------ */
/* Menu descriptors                                                   */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Module list from the Pilot manifest                                */
/* ------------------------------------------------------------------ */
/*
 * The A6700 backend reads <drive>:/pilot/modules.json through the native
 * Fsys_* entry points and turns every entry into one list row.  The A7C II
 * exposes the same entry points, so the read is replayed here.  The
 * manifest is modules.txt because the camera refuses to open .json, while
 * its contents are still a JSON object.
 */

#define PILOT_ROWS_MAX 16u
#define PILOT_ROWS_FALLBACK 4u

static char g_mod_names[PILOT_ROWS_MAX][32];
static size_t g_mod_count;
static unsigned char g_mod_disabled[PILOT_ROWS_MAX];
static int g_all_disabled;

static char pilot_drive_letter(void)
{
    char dir[128];

    if (g_api && g_api->get_module_dir &&
        g_api->get_module_dir("menu_ui", dir, sizeof(dir)) ==
            PILOT_STATUS_OK &&
        ((dir[0] >= 'A' && dir[0] <= 'Z') ||
         (dir[0] >= 'a' && dir[0] <= 'z')))
        return dir[0];
    return 'A';
}

/* True when the path can be opened at all.  The launcher marks a disabled
 * module with an empty "disable" file, so existence is the whole signal. */
static int pilot_path_exists(const char *path)
{
    typedef int (*fsys_open_fn)(const char *, void **, int);
    typedef int (*fsys_close_fn)(void *);
    fsys_open_fn f_open;
    fsys_close_fn f_close;
    void *handle = NULL;

    if (!path)
        return 0;
    f_open = (fsys_open_fn)dlsym(RTLD_DEFAULT, "Fsys_Open");
    f_close = (fsys_close_fn)dlsym(RTLD_DEFAULT, "Fsys_Close");
    if (!f_open)
        return 0;
    if (f_open(path, &handle, 1) != 0 || !handle)
        return 0;
    if (f_close)
        f_close(handle);
    return 1;
}

static int read_manifest(const char *path, char *buf, size_t cap)
{
    typedef int (*fsys_open_fn)(const char *, void **, int);
    typedef int (*fsys_read_fn)(void *, void *, size_t);
    typedef int (*fsys_close_fn)(void *);
    fsys_open_fn f_open;
    fsys_read_fn f_read;
    fsys_close_fn f_close;
    void *handle = NULL;
    size_t total = 0;

    if (!path || !buf || cap < 2u)
        return 0;

    f_open = (fsys_open_fn)dlsym(RTLD_DEFAULT, "Fsys_Open");
    f_read = (fsys_read_fn)dlsym(RTLD_DEFAULT, "Fsys_Read");
    f_close = (fsys_close_fn)dlsym(RTLD_DEFAULT, "Fsys_Close");
    if (!f_open || !f_read || !f_close)
        return 0;

    if (f_open(path, &handle, 1) != 0 || !handle)
        return 0;

    while (total + 1u < cap) {
        int n = f_read(handle, buf + total, cap - 1u - total);

        if (n <= 0)
            break;
        total += (size_t)n;
    }
    f_close(handle);
    buf[total] = '\0';
    return total > 0u;
}

static void add_module_name(const char *start, size_t len)
{
    char name[sizeof(g_mod_names[0])];
    size_t n = len;

    if (n >= sizeof(name))
        n = sizeof(name) - 1u;
    memcpy(name, start, n);
    name[n] = '\0';
    if (n >= 3u && strcmp(name + n - 3u, ".so") == 0)
        name[n - 3u] = '\0';
    if (!name[0])
        return;

    n = strlen(name);
    if (n >= sizeof(g_mod_names[0]))
        n = sizeof(g_mod_names[0]) - 1u;
    memcpy(g_mod_names[g_mod_count], name, n);
    g_mod_names[g_mod_count][n] = '\0';
    ++g_mod_count;
}

static void parse_manifest(const char *json)
{
    const char *p;

    if (!json)
        return;
    p = strstr(json, "\"modules\"");
    if (!p)
        return;
    p = strchr(p, '[');
    if (!p)
        return;         /* object form: not a list of module names */
    ++p;

    while (*p && g_mod_count < PILOT_ROWS_MAX) {
        const char *start;

        while (*p && *p != '"')
            ++p;
        if (!*p)
            break;
        start = ++p;
        while (*p && *p != '"')
            ++p;
        if (!*p)
            break;
        add_module_name(start, (size_t)(p - start));
        ++p;
        while (*p && *p != ',' && *p != ']')
            ++p;
        if (*p != ',')
            break;
        ++p;
    }
}

static void load_modules(void)
{
    static const char *const k_leaves[] = {"txt", "json"};
    char json[2048];
    char path[128];
    char drive = pilot_drive_letter();
    size_t i;

    memset(g_mod_names, 0, sizeof(g_mod_names));
    g_mod_count = 0;
    for (i = 0; i < sizeof(k_leaves) / sizeof(k_leaves[0]); ++i) {
        snprintf(path, sizeof(path), "%c:/pilot/modules.%s", drive,
                 k_leaves[i]);
        if (!read_manifest(path, json, sizeof(json)))
            continue;
        parse_manifest(json);
        if (g_mod_count)
            break;
    }
    snprintf(path, sizeof(path), "%c:/pilot/disable", drive);
    g_all_disabled = pilot_path_exists(path);
    for (i = 0; i < g_mod_count; ++i) {
        snprintf(path, sizeof(path), "%c:/pilot/modules/%.*s/disable", drive,
                 (int)(sizeof(g_mod_names[0]) - 1u), g_mod_names[i]);
        g_mod_disabled[i] = (unsigned char)pilot_path_exists(path);
    }
    log_msg("manifest: %u module(s), global disable=%d",
            (unsigned)g_mod_count, g_all_disabled);
}

/*
 * Build one non-setting descriptor.  The layout follows the decoded A7C2
 * samples: +0x00 label RID, +0x0C screen id opened on select, +0x20 the
 * availability callback.  Everything else stays zero, which is what the
 * A6700 module does as well.
 */
static void fill_desc(uint8_t *desc, uint32_t label_rid, uint32_t screen_id)
{
    if (!desc)
        return;

    memset(desc, 0, APPFW_DESC_SIZE);
    put_u32(desc, APPFW_DESC_LABEL_RID_OFF, label_rid);
    put_u32(desc, APPFW_DESC_SCREEN_ID_OFF, screen_id);
    put_u64(desc, APPFW_DESC_VISIBILITY_OFF,
            (uint64_t)(g_appfw_base + APPFW_VISIBILITY_OK_OFF));
}

/* ------------------------------------------------------------------ */
/* Menu group injection                                               */
/* ------------------------------------------------------------------ */

static int install_menu_group(void)
{
    uintptr_t got = 0;
    uintptr_t table_addr;
    uint8_t *cat0;
    uint8_t *group;
    uint8_t zero[APPFW_MENU_GROUP_SIZE];
    uint32_t cat_count;
    size_t i;

    if (read_mem(APPFW_GOT_MENU_TABLE, &got, sizeof(got)) != 0) {
        log_msg("menu table GOT unreadable");
        return -1;
    }
    if (got != g_appfw_base + APPFW_MENU_TABLE_OFF) {
        log_msg("menu table GOT mismatch: %p != %p", (void *)got,
                (void *)(g_appfw_base + APPFW_MENU_TABLE_OFF));
        return -1;
    }

    g_menu_copy = calloc(1, APPFW_MENU_TABLE_SIZE);
    if (!g_menu_copy) {
        log_msg("menu table copy allocation failed");
        return -1;
    }
    {
        size_t copied = 0;

        while (copied < (size_t)APPFW_MENU_TABLE_SIZE) {
            size_t chunk = (size_t)APPFW_MENU_TABLE_SIZE - copied;
            PilotStatus rc;

            if (chunk > 4096u)
                chunk = 4096u;
            if (read_mem_status((uintptr_t)(APPFW_MENU_TABLE_OFF + copied),
                                g_menu_copy + copied, chunk, &rc) != 0) {
                log_msg("menu table copy failed at +0x%x (%u bytes): %d",
                        (unsigned)copied, (unsigned)chunk, (int)rc);
                return -1;
            }
            copied += chunk;
        }
    }

    cat0 = g_menu_copy + APPFW_MENU_CAT_BASE_OFF;
    cat_count = get_u32(cat0, APPFW_MENU_CAT_COUNT_OFF);
    log_msg("menu table copied: %p -> %p, cat0=%p count=%u", (void *)got,
            (void *)g_menu_copy, (void *)cat0, (unsigned)cat_count);
    if (cat_count >= APPFW_MENU_CAT_GROUPS) {
        log_msg("category 0 is full (count=%u)", (unsigned)cat_count);
        return -1;
    }

    group = cat0 + APPFW_MENU_CAT_GROUP_OFF +
            (size_t)cat_count * APPFW_MENU_GROUP_SIZE;
    memset(zero, 0, sizeof(zero));
    if (memcmp(group, zero, sizeof(zero)) != 0) {
        log_msg("group slot %u is not empty, refusing to overwrite",
                (unsigned)cat_count);
        log_hex("slot-head", group, 32);
        return -1;
    }

    g_row_count = g_mod_count ? g_mod_count : PILOT_ROWS_FALLBACK;
    g_desc_pool = calloc(g_row_count, APPFW_DESC_SIZE);
    if (!g_desc_pool) {
        log_msg("descriptor pool allocation failed");
        return -1;
    }

    put_u32(group, APPFW_MENU_GROUP_INDEX_OFF, PILOT_GROUP_INDEX_FREE);
    put_u32(group, APPFW_MENU_GROUP_RID_OFF, PILOT_RID_MENU_LABEL);
    put_u32(group, APPFW_MENU_GROUP_ROWS_OFF, (uint32_t)g_row_count);

    for (i = 0; i < g_row_count; ++i) {
        uint8_t *desc = g_desc_pool + i * APPFW_DESC_SIZE;
        size_t row_off = APPFW_MENU_GROUP_ENTRY_OFF + i * APPFW_MENU_ROW_SIZE;

        fill_desc(desc, PILOT_RID_LIST_ITEM_BASE + (uint32_t)i,
                  APPFW_ITEM_SCREEN_ID);
        put_u64(group, row_off + 0u, PILOT_ITEM_ID_BASE + i);
        put_u64(group, row_off + 8u, 0);            /* setting_desc     */
        put_u64(group, row_off + 16u, (uint64_t)(uintptr_t)desc);
    }

    put_u32(cat0, APPFW_MENU_CAT_COUNT_OFF, cat_count + 1u);

    /* Dump what the UI is about to walk: if the crash lands inside appFw
     * these bytes are the only evidence of what it was handed. */
    log_msg("cat0 count -> %u, slot=%u, rows=%u, desc_pool=%p",
            (unsigned)(cat_count + 1u), (unsigned)cat_count,
            (unsigned)g_row_count, (void *)g_desc_pool);
    log_hex("group", group, 48);
    log_hex("row0", group + APPFW_MENU_GROUP_ENTRY_OFF, 24);
    log_hex("desc0", g_desc_pool, 48);

    table_addr = (uintptr_t)g_menu_copy;
    if (patch_mem(APPFW_GOT_MENU_TABLE, &table_addr, sizeof(table_addr)) !=
        0) {
        log_msg("menu table GOT patch failed");
        return -1;
    }

    log_msg("group injected: category 0 slot %u, %u rows",
            (unsigned)cat_count, (unsigned)g_row_count);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Pilot option pages                                                 */
/* ------------------------------------------------------------------ */

static const char *const k_themes[] = {"Dark", "Light", "Auto"};

static void on_theme_change(PilotUiOption *opt, void *user)
{
    (void)user;
    if (opt && opt->value >= 0 &&
        (size_t)opt->value < sizeof(k_themes) / sizeof(k_themes[0]))
        rid_text_set(PILOT_RID_LIST_ITEM_BASE + 1u, k_themes[opt->value]);
}

static PilotUiOption g_options[] = {
    {"Pilot menu theme", PILOT_UI_OPT_ENUM, 0, k_themes,
     sizeof(k_themes) / sizeof(k_themes[0]), NULL, on_theme_change, NULL},
    {"Pilot overlay", PILOT_UI_OPT_BOOL, 1, NULL, 0, NULL, NULL, NULL},
};

static void register_options(void)
{
    PilotStatus st = pilot_ui_register_options(
        "pilot_menu_ui.so", g_options,
        sizeof(g_options) / sizeof(g_options[0]));

    if (st == PILOT_STATUS_OK)
        log_msg("options registered");
    else
        log_msg("option registration failed, status=%d", (int)st);
}

/* Static text for the Pilot rows.  Real values are refreshed later, once
 * the menu screen tells us which row it is painting. */
static void seed_texts(void)
{
    size_t i;

    rid_text_set(PILOT_RID_MENU_LABEL, "Pilot");
    if (g_mod_count == 0u) {
        rid_text_set(PILOT_RID_LIST_ITEM_BASE + 0u, "Pilot Canvas");
        rid_text_set(PILOT_RID_LIST_ITEM_BASE + 1u, k_themes[0]);
        rid_text_set(PILOT_RID_LIST_ITEM_BASE + 2u, "Overlay: on");
        rid_text_set(PILOT_RID_LIST_ITEM_BASE + 3u, "Version: 0.1.0-a7c2");
        return;
    }
    /*
     * Functional parity with the A6700 page: the row carries the state as
     * well as the name, because the A7C II has no detail screen to put it
     * on.  The "[on]"/"[off]" suffix mirrors the disable marker files.
     */
    for (i = 0; i < g_mod_count; ++i) {
        char text[sizeof(g_mod_names[0]) + 8u];

        snprintf(text, sizeof(text), "%.*s [%s]",
                 (int)(sizeof(g_mod_names[0]) - 1u), g_mod_names[i],
                 (g_all_disabled || g_mod_disabled[i]) ? "off" : "on");
        rid_text_set(PILOT_RID_LIST_ITEM_BASE + (uint32_t)i, text);
    }
}

/* ------------------------------------------------------------------ */
/* Worker                                                             */
/* ------------------------------------------------------------------ */

static void *worker(void *arg)
{
    (void)arg;
    log_msg("worker start");

    g_appfw_base = 0;
    if (!dl_iterate_phdr(appfw_base_cb, &g_appfw_base) || !g_appfw_base) {
        log_msg("appFw.so not found");
        return NULL;
    }
    log_msg("pilot menu_ui a7c2 %s %s", __DATE__, __TIME__);
    log_msg("appFw base = %p", (void *)g_appfw_base);

    load_modules();
    log_msg("manifest: %u module(s)", (unsigned)g_mod_count);
    if (install_menu_group() != 0)
        log_msg("menu group NOT installed");
    seed_texts();
    if (install_set_string_hook() != 0)
        log_msg("setString hook NOT installed");
#ifndef PILOT_A7C2_NO_INLINE_HOOK
    if (install_view_string_hook() != 0)
        log_msg("view_string hook NOT installed");
#else
    log_msg("view_string hook disabled at build time");
#endif

    register_options();
    log_msg("init complete");

    for (;;)
        sleep(1);
}

/* ------------------------------------------------------------------ */
/* API exported to other modules                                      */
/* ------------------------------------------------------------------ */
/*
 * A6700 exports the whole pilot_ui_* surface; other modules resolve it at
 * run time through dlsym(RTLD_DEFAULT).  The A7C II backend keeps the two
 * that still mean something here:
 *
 *   pilot_ui_set_rid_override_v1 -- text for a RID, honoured by the
 *       view_string hook, so it takes effect on the next repaint.
 *   pilot_ui_register_options_v1 -- collect the option table; the A7C II
 *       backend cannot render a detail page yet, so entries are recorded
 *       and logged instead of shown.
 *
 * Busy-dialog and live-view-text entry points are not exported: without a
 * window system hook they would have to lie about success.
 */

#define A7C2_OPT_MODULES_MAX 8u

struct a7c2_opt_reg {
    char module_name[40];
    PilotUiOption *options;
    size_t count;
};

static struct a7c2_opt_reg g_opt_registry[A7C2_OPT_MODULES_MAX];
static size_t g_opt_reg_count;

__attribute__((visibility("default"))) PilotStatus
pilot_ui_set_rid_override_v1(uint32_t rid, const char *text)
{
    if (!text)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (rid < PILOT_RID_MENU_LABEL ||
        rid > PILOT_RID_LIST_ITEM_BASE + PILOT_RID_LIST_ITEM_MAX)
        return PILOT_STATUS_RANGE;

    rid_text_set(rid, text);
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default"))) PilotStatus
pilot_ui_register_options_v1(const char *module_name, PilotUiOption *options,
                             size_t count)
{
    struct a7c2_opt_reg *slot;
    size_t i;

    if (!module_name || (!options && count))
        return PILOT_STATUS_INVALID_ARGUMENT;

    for (i = 0; i < g_opt_reg_count; ++i) {
        if (strcmp(g_opt_registry[i].module_name, module_name) == 0) {
            g_opt_registry[i].options = options;
            g_opt_registry[i].count = count;
            return PILOT_STATUS_OK;
        }
    }
    if (g_opt_reg_count >= A7C2_OPT_MODULES_MAX)
        return PILOT_STATUS_RANGE;

    slot = &g_opt_registry[g_opt_reg_count];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->module_name, module_name, sizeof(slot->module_name) - 1u);
    slot->options = options;
    slot->count = count;
    ++g_opt_reg_count;

    log_msg("options registered: %s (%u entries)", module_name,
            (unsigned)count);
    return PILOT_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* Module entry                                                       */
/* ------------------------------------------------------------------ */

__attribute__((visibility("default"))) PilotStatus
pilot_module_init(const PilotApi *api)
{
    pthread_t tid;

    if (!api || api->abi_version != PILOT_ABI_VERSION ||
        api->struct_size < sizeof(PilotApi))
        return PILOT_STATUS_INVALID_ARGUMENT;

    if (!api->memory_read || !api->memory_patch)
        return PILOT_STATUS_UNSUPPORTED;

    g_api = api;

    if (pthread_create(&tid, NULL, worker, NULL) != 0)
        return PILOT_STATUS_SYSTEM;

    pthread_detach(tid);
    return PILOT_STATUS_OK;
}
