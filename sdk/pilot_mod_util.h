/* Pilot Module SDK — Camera-Agnostic Header-Only Utilities
 *
 * Pure static-inline helpers for external / open-source Pilot modules.
 * Compiled strictly inside each module DSO (completely isolated from pilot.so).
 */
#ifndef PILOT_SDK_MOD_UTIL_H
#define PILOT_SDK_MOD_UTIL_H

#include "pilot.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define PM_PAGE_SIZE 4096ULL

/* --- Little-endian encoding --- */
static inline uint16_t pm_rd_u16(const uint8_t *b, size_t off)
{
    return (uint16_t)b[off] | ((uint16_t)b[off + 1u] << 8);
}

static inline uint32_t pm_rd_u32(const uint8_t *b, size_t off)
{
    return (uint32_t)b[off] | ((uint32_t)b[off + 1u] << 8) |
           ((uint32_t)b[off + 2u] << 16) | ((uint32_t)b[off + 3u] << 24);
}

static inline void pm_wr_u16(uint8_t *b, size_t off, uint16_t v)
{
    b[off] = (uint8_t)(v & 0xFFu);
    b[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline void pm_wr_u32(uint8_t *b, size_t off, uint32_t v)
{
    b[off] = (uint8_t)(v & 0xFFu);
    b[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    b[off + 2u] = (uint8_t)((v >> 16) & 0xFFu);
    b[off + 3u] = (uint8_t)((v >> 24) & 0xFFu);
}

/* --- Timing --- */
static inline void pm_sleep_us(uint32_t us)
{
    struct timespec ts;

    ts.tv_sec = us / 1000000u;
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    nanosleep(&ts, NULL);
}

static inline void pm_sleep_ms(uint32_t ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000u;
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

/* --- Logging & Toast UI --- */
static inline void pm_emit_log(const PilotApi *api, const char *s)
{
    if (!api || !s)
        return;
    if (api->log && api->log->print)
        (void)api->log->print(s);
    if (api->mlog && api->mlog->print)
        (void)api->mlog->print(api->mlog, s);
}

static inline void pm_toast_show(const char *slot,
                                 int x,
                                 int y,
                                 int w,
                                 int h,
                                 const char *text,
                                 uint32_t duration_ms)
{
    typedef PilotStatus (*pm_canvas_text_set_fn)(const char *, int, int, int, int,
                                                 const char *, uint32_t);
    typedef PilotStatus (*pm_ui_text_show_fn)(const char *, uint32_t);
    pm_canvas_text_set_fn fn_ctext =
        (pm_canvas_text_set_fn)dlsym(RTLD_DEFAULT, "pilot_canvas_text_set_v1");

    if (fn_ctext) {
        (void)fn_ctext(slot, x, y, w, h, text, duration_ms);
    } else {
        pm_ui_text_show_fn fn_show =
            (pm_ui_text_show_fn)dlsym(RTLD_DEFAULT, "pilot_ui_text_show_v1");
        if (fn_show)
            (void)fn_show(text, duration_ms);
    }
}

/* --- AArch64 D-Cache & /dev/mem physical memory --- */
static inline void pm_dcache_clean_inval(const void *addr, size_t len)
{
#if defined(__aarch64__)
    uintptr_t p = (uintptr_t)addr & ~63ull;
    uintptr_t end = (uintptr_t)addr + len;

    __asm__ volatile("dsb sy" ::: "memory");
    while (p < end) {
        __asm__ volatile("dc civac, %0" :: "r"(p) : "memory");
        p += 64ull;
    }
    __asm__ volatile("dsb sy" ::: "memory");
#else
    (void)addr;
    (void)len;
#endif
}

static inline int pm_phys_write_verify(int devmem_fd,
                                       uint64_t phys,
                                       const void *buf,
                                       size_t len)
{
    uint8_t rb[512];
    uint64_t page;
    uint64_t off;
    size_t map_len;
    void *m;
    volatile uint8_t *dst;
    const uint8_t *src = (const uint8_t *)buf;

    if (devmem_fd < 0 || !buf || len == 0u || len > sizeof(rb))
        return 0;

    page = phys & ~(PM_PAGE_SIZE - 1ULL);
    off = phys - page;
    map_len = (size_t)(((off + len) + PM_PAGE_SIZE - 1ULL) & ~(PM_PAGE_SIZE - 1ULL));
    m = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED,
             devmem_fd, (off_t)page);
    if (m == MAP_FAILED)
        return 0;

    dst = (volatile uint8_t *)m + off;
    for (size_t i = 0; i < len; ++i)
        dst[i] = src[i];
    pm_dcache_clean_inval((const void *)dst, len);
    munmap(m, map_len);

    if (pread(devmem_fd, rb, len, (off_t)phys) != (ssize_t)len)
        return 0;
    return memcmp(rb, buf, len) == 0;
}

/* --- AP process shared library lookup & RO segment patch --- */
static inline uint64_t pm_find_so_base(const char *soname)
{
    FILE *f;
    char line[512];
    uint64_t base = 0;

    if (!soname || !soname[0])
        return 0;
    f = fopen("/proc/self/maps", "r");
    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f)) {
        unsigned long long lo = 0, hi = 0, off = 0;
        char perms[8] = {0};

        if (!strstr(line, soname))
            continue;
        if (sscanf(line, "%llx-%llx %7s %llx", &lo, &hi, perms, &off) == 4 &&
            off == 0 && perms[0] == 'r') {
            base = (uint64_t)lo;
            break;
        }
    }
    fclose(f);
    return base;
}

static inline const char *pm_ap_patch_ro_multi(uint64_t base,
                                               const uint64_t *offs,
                                               size_t count,
                                               const void *stock,
                                               const void *patch,
                                               size_t len)
{
    int all_stock = 1;
    int all_patch = 1;

    if (!base || !offs || !count || !patch || !len)
        return "INVALID_ARG";

    for (size_t i = 0; i < count; ++i) {
        const uint8_t *p = (const uint8_t *)(uintptr_t)(base + offs[i]);
        if (stock && memcmp(p, stock, len) != 0)
            all_stock = 0;
        if (memcmp(p, patch, len) != 0)
            all_patch = 0;
    }
    if (all_patch)
        return "ALREADY_PATCHED";
    if (stock && !all_stock)
        return "FOREIGN_STATE";

    for (size_t i = 0; i < count; ++i) {
        uint64_t addr = base + offs[i];
        uint64_t page = addr & ~(PM_PAGE_SIZE - 1ULL);
        size_t span = (size_t)(((addr - page) + len + PM_PAGE_SIZE - 1ULL) &
                               ~(PM_PAGE_SIZE - 1ULL));
        if (mprotect((void *)(uintptr_t)page, span, PROT_READ | PROT_WRITE) != 0)
            return "MPROTECT_RW_FAIL";
        memcpy((void *)(uintptr_t)addr, patch, len);
        (void)mprotect((void *)(uintptr_t)page, span, PROT_READ);
    }

    for (size_t i = 0; i < count; ++i) {
        if (memcmp((const void *)(uintptr_t)(base + offs[i]), patch, len) != 0)
            return "VERIFY_FAIL";
    }
    return "APPLY_OK";
}

/* --- CP memory high-level helpers (built on Pilot api->cp_mem) --- */
static inline PilotStatus pm_cp_wait_nonzero_u16(const PilotApi *api,
                                                 uint64_t probe_va,
                                                 size_t probe_len,
                                                 uint32_t max_tries,
                                                 uint32_t step_ms)
{
    uint8_t probe[64];
    PilotStatus rc = PILOT_STATUS_IO;

    if (!api || !api->cp_mem || !api->cp_mem->read ||
        probe_len < 2u || probe_len > sizeof(probe))
        return PILOT_STATUS_INVALID_ARGUMENT;

    for (uint32_t t = 0; t < max_tries; ++t) {
        rc = api->cp_mem->read(probe_va, probe, probe_len);
        if (rc == PILOT_STATUS_OK && pm_rd_u16(probe, 0u) != 0u)
            return PILOT_STATUS_OK;
        pm_sleep_ms(step_ms);
    }
    return rc;
}

static inline PilotStatus pm_cp_patch_idempotent(const PilotApi *api,
                                                 uint64_t va,
                                                 const void *stock,
                                                 const void *patch,
                                                 size_t len,
                                                 uint32_t *out_applied,
                                                 uint32_t *out_skipped)
{
    uint8_t cur[64];
    PilotStatus rc;

    if (!api || !api->cp_mem || !api->cp_mem->read || !api->cp_mem->patch)
        return PILOT_STATUS_UNSUPPORTED;
    if (len == 0u || len > sizeof(cur))
        return PILOT_STATUS_RANGE;

    rc = api->cp_mem->read(va, cur, len);
    if (rc != PILOT_STATUS_OK)
        return rc;
    if (memcmp(cur, patch, len) == 0) {
        if (out_skipped)
            (*out_skipped)++;
        return PILOT_STATUS_OK;
    }
    if (stock && memcmp(cur, stock, len) != 0)
        return PILOT_STATUS_CONFLICT;

    rc = api->cp_mem->patch(va, stock, patch, len);
    if (rc == PILOT_STATUS_OK && out_applied)
        (*out_applied)++;
    return rc;
}

#endif
