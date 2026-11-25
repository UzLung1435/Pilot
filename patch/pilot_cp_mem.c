#define _GNU_SOURCE
#include "pilot_internal.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define PILOT_CP_PCIDMA_DEV "/dev/pcidma"
#define PILOT_CP_PCIDMA_TRANSFER 0x40185001u
#define PILOT_CP_PCIE_CH 0u
#define PILOT_CP_DMAC_CH 7u
#define PILOT_CP_PCIDMA_RD 0u
#define PILOT_CP_PCIDMA_WR 1u
#define PILOT_CP_IMAGE_RO_END 0x01000000ull
#define PILOT_CP_CODE_START   0x00128000ull
#define PILOT_CP_CODE_END     0x011D8000ull
#define PILOT_CP_SELF_START   0x00C12D68ull
#define PILOT_CP_SELF_END     0x00C12FC0ull
#define PILOT_CP_UIPC_ROUTE          0x90030001u
#define PILOT_CP_UIPC_CMD            0x7Eu
#define PILOT_CP_UIPC_VER_V3         3u
#define PILOT_CP_UIPC_VER_V4         4u
#define PILOT_CP_UIPC_V4_HDR_BYTES   8u
#define PILOT_CP_UIPC_V4_ENT_BYTES   6u
#define PILOT_CP_UIPC_V4_INLINE_MAX  2048u
#define PILOT_CP_UIPC_BYTES          20u
#define PILOT_CP_UIPC_TIMEOUT        1000
#define PILOT_CP_UIPC_STAT_VA        0x00C12FA0ull
#define PILOT_CP_UIPC_MAGIC          0x50544348u
#define PILOT_CP_UIPC_WAIT_US        1000000u
#define PILOT_CP_UIPC_FAST_STEP_US   200u
#define PILOT_CP_UIPC_SLOW_STEP_US   1000u
#define PILOT_CP_UIPC_FAST_BUDGET_US 5000u

_Static_assert(PILOT_CP_MEM_TRANSFER_MAX <= 4096u,
               "bounce buffer fits transfer max");
_Static_assert(PILOT_CP_UIPC_V4_HDR_BYTES + PILOT_CP_UIPC_V4_ENT_BYTES +
               PILOT_CP_UIPC_V4_INLINE_MAX <= 4096u,
               "v4 batch message fits osal_valloc_msg_wait 4096 B limit");

typedef int (*pilot_osal_valloc_fn)(uint32_t route, void **out_msg,
                                    uint32_t size, int32_t timeout);
typedef void (*pilot_osal_set_src_fn)(void *msg, uint32_t src);
typedef int (*pilot_osal_snd_fn)(uint32_t route, void *msg);
typedef int (*pilot_osal_free_fn)(uint32_t route, void *msg);

struct pilot_pcidma_param {
    uint8_t pcie_ch;
    uint8_t dmac_ch;
    uint8_t dir;
    uint8_t pad;
    uint32_t size;
    uint64_t src;
    uint64_t dst;
};

_Static_assert(sizeof(struct pilot_pcidma_param) == 24,
               "pilot_pcidma_param layout");

struct cp_async_job {
    uint64_t cp_address;
    size_t length;
    int is_patch;
    int has_expected;
    PilotCpMemCallback callback;
    void *user_data;
    uint8_t data[];
};

static pthread_mutex_t g_cp_mem_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_cp_uipc_seq = 0;
/* 0 = try v4 first, 1 = v4 confirmed, -1 = fallback to v3 */
static int g_cp_uipc_v4_state = 0;

/* Persistent DMA resources and cached OSAL symbols (guarded by g_cp_mem_lock). */
static int g_pcidma_fd = -1;
static uint8_t *g_bounce_buf = NULL;
static uint64_t g_bounce_phys = 0;
static int g_osal_resolved = 0;
static pilot_osal_valloc_fn g_fn_valloc = NULL;
static pilot_osal_set_src_fn g_fn_set_src = NULL;
static pilot_osal_snd_fn g_fn_snd = NULL;
static pilot_osal_free_fn g_fn_free = NULL;

static void cp_dcache_clean_inval(const void *addr, size_t len)
{
    uintptr_t p = (uintptr_t)addr & ~63ull;
    uintptr_t end = (uintptr_t)addr + len;

    __asm__ volatile("dsb sy" ::: "memory");
    while (p < end) {
        __asm__ volatile("dc civac, %0" :: "r"(p) : "memory");
        p += 64ull;
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static void cp_sleep_us(uint32_t us)
{
    struct timespec ts;

    ts.tv_sec = us / 1000000u;
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    nanosleep(&ts, NULL);
}

static PilotStatus cp_alloc_bounce(uint8_t **buf_out, uint64_t *phys_out)
{
    uint8_t *buf;
    int pg;
    uint64_t entry = 0;
    uint64_t pfn;
    off_t off;

    buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED)
        return PILOT_STATUS_SYSTEM;

    memset(buf, 0xa5, 4096);
    (void)mlock(buf, 4096);

    pg = open("/proc/self/pagemap", O_RDONLY);
    if (pg < 0) {
        munmap(buf, 4096);
        return PILOT_STATUS_SYSTEM;
    }

    off = (off_t)(((uintptr_t)buf >> 12) * sizeof(uint64_t));
    if (pread(pg, &entry, sizeof(entry), off) != (ssize_t)sizeof(entry)) {
        close(pg);
        munmap(buf, 4096);
        return PILOT_STATUS_IO;
    }
    close(pg);

    if ((entry & (1ULL << 63)) == 0) {
        munmap(buf, 4096);
        return PILOT_STATUS_SYSTEM;
    }
    pfn = entry & ((1ULL << 55) - 1);
    if (pfn == 0) {
        munmap(buf, 4096);
        return PILOT_STATUS_SYSTEM;
    }

    *buf_out = buf;
    *phys_out = (pfn << 12) | ((uintptr_t)buf & 0xfffull);
    return PILOT_STATUS_OK;
}

static PilotStatus cp_run_ioctl(int fd,
                                uint8_t dir,
                                uint32_t size,
                                uint64_t src,
                                uint64_t dst)
{
    struct pilot_pcidma_param param;

    memset(&param, 0, sizeof(param));
    param.pcie_ch = PILOT_CP_PCIE_CH;
    param.dmac_ch = PILOT_CP_DMAC_CH;
    param.dir = dir;
    param.size = size;
    param.src = src;
    param.dst = dst;

    if (ioctl(fd, PILOT_CP_PCIDMA_TRANSFER, &param) < 0)
        return PILOT_STATUS_IO;
    return PILOT_STATUS_OK;
}

static void cp_reset_pcidma_fd_locked(void)
{
    if (g_pcidma_fd >= 0) {
        close(g_pcidma_fd);
        g_pcidma_fd = -1;
    }
}

static PilotStatus cp_ensure_dma_locked(void)
{
    PilotStatus status;

    if (!g_bounce_buf) {
        status = cp_alloc_bounce(&g_bounce_buf, &g_bounce_phys);
        if (status != PILOT_STATUS_OK)
            return status;
    }

    if (g_pcidma_fd < 0) {
        g_pcidma_fd = open(PILOT_CP_PCIDMA_DEV, O_RDWR | O_CLOEXEC);
        if (g_pcidma_fd < 0)
            return PILOT_STATUS_NOT_FOUND;
    }

    return PILOT_STATUS_OK;
}

static PilotStatus cp_read_locked(uint64_t cp_address,
                                  void *output,
                                  size_t length)
{
    PilotStatus status;

    status = cp_ensure_dma_locked();
    if (status != PILOT_STATUS_OK)
        return status;

    cp_dcache_clean_inval(g_bounce_buf, length);
    status = cp_run_ioctl(g_pcidma_fd, PILOT_CP_PCIDMA_RD, (uint32_t)length,
                          cp_address, g_bounce_phys);
    if (status != PILOT_STATUS_OK) {
        cp_reset_pcidma_fd_locked();
        if (cp_ensure_dma_locked() == PILOT_STATUS_OK) {
            cp_dcache_clean_inval(g_bounce_buf, length);
            status = cp_run_ioctl(g_pcidma_fd, PILOT_CP_PCIDMA_RD,
                                  (uint32_t)length, cp_address, g_bounce_phys);
        }
    }
    if (status == PILOT_STATUS_OK) {
        cp_dcache_clean_inval(g_bounce_buf, length);
        memcpy(output, g_bounce_buf, length);
    }

    return status;
}

static PilotStatus cp_write_locked(uint64_t cp_address,
                                   const void *input,
                                   size_t length)
{
    PilotStatus status;

    status = cp_ensure_dma_locked();
    if (status != PILOT_STATUS_OK)
        return status;

    memcpy(g_bounce_buf, input, length);
    cp_dcache_clean_inval(g_bounce_buf, length);

    status = cp_run_ioctl(g_pcidma_fd, PILOT_CP_PCIDMA_WR, (uint32_t)length,
                          g_bounce_phys, cp_address);
    if (status != PILOT_STATUS_OK) {
        cp_reset_pcidma_fd_locked();
        if (cp_ensure_dma_locked() == PILOT_STATUS_OK) {
            status = cp_run_ioctl(g_pcidma_fd, PILOT_CP_PCIDMA_WR,
                                  (uint32_t)length, g_bounce_phys, cp_address);
        }
    }

    return status;
}

PilotStatus pilot_cp_mem_read_impl(uint64_t cp_address,
                                   void *output,
                                   size_t length)
{
    PilotStatus status;

    if (!__atomic_load_n(&g_pilot.optional_features_enabled, __ATOMIC_ACQUIRE))
        return PILOT_STATUS_DISABLED;
    if (!output || !length)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (length > PILOT_CP_MEM_TRANSFER_MAX)
        return PILOT_STATUS_RANGE;

    pthread_mutex_lock(&g_cp_mem_lock);
    status = cp_read_locked(cp_address, output, length);
    pthread_mutex_unlock(&g_cp_mem_lock);
    return status;
}

static int cp_is_code_range(uint64_t addr, size_t length)
{
    uint64_t end = addr + (uint64_t)length;

    if (end < addr)
        return 0;
    if (addr < PILOT_CP_CODE_START || end > PILOT_CP_CODE_END)
        return 0;
    if (end > PILOT_CP_SELF_START && addr < PILOT_CP_SELF_END)
        return 0;
    return 1;
}

static uint32_t cp_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void cp_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t cp_read_le_width(const uint8_t *p, uint8_t width)
{
    if (width == 1u)
        return p[0];
    if (width == 2u)
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    return cp_get_le32(p);
}

static int cp_resolve_osal_locked(void)
{
    if (!g_osal_resolved) {
        g_fn_valloc =
            (pilot_osal_valloc_fn)dlsym(RTLD_DEFAULT, "osal_valloc_msg_wait");
        g_fn_set_src =
            (pilot_osal_set_src_fn)dlsym(RTLD_DEFAULT, "osal_set_msg_src");
        g_fn_snd =
            (pilot_osal_snd_fn)dlsym(RTLD_DEFAULT, "osal_snd_msg");
        g_fn_free =
            (pilot_osal_free_fn)dlsym(RTLD_DEFAULT, "osal_free_msg");
        if (g_fn_valloc && g_fn_snd && g_fn_free)
            g_osal_resolved = 1;
    }
    return g_osal_resolved;
}

static uint32_t cp_uipc_next_seq_locked(void)
{
    uint32_t seq = ++g_cp_uipc_seq;
    if (seq == 0u)
        seq = ++g_cp_uipc_seq;
    return seq;
}

static PilotStatus cp_uipc_wait_ack_locked(uint32_t seq, uint32_t *cp_res_out)
{
    uint8_t st[16];
    uint32_t waited_us = 0;

    while (waited_us < PILOT_CP_UIPC_WAIT_US) {
        uint32_t step_us = (waited_us < PILOT_CP_UIPC_FAST_BUDGET_US)
                           ? PILOT_CP_UIPC_FAST_STEP_US
                           : PILOT_CP_UIPC_SLOW_STEP_US;

        cp_sleep_us(step_us);
        waited_us += step_us;
        if (cp_read_locked(PILOT_CP_UIPC_STAT_VA, st, sizeof(st)) == PILOT_STATUS_OK &&
            cp_get_le32(st) == PILOT_CP_UIPC_MAGIC &&
            cp_get_le32(st + 4) == seq) {
            if (cp_res_out)
                *cp_res_out = cp_get_le32(st + 8);
            return PILOT_STATUS_OK;
        }
    }
    return PILOT_STATUS_IO;
}

static PilotStatus cp_uipc_map_result(uint32_t res)
{
    if (res == 0u)
        return PILOT_STATUS_OK;
    if (res == 2u)
        return PILOT_STATUS_UNSUPPORTED;
    if (res == 4u)
        return PILOT_STATUS_CONFLICT;
    if (res == 3u || res == 7u)
        return PILOT_STATUS_RANGE;
    return PILOT_STATUS_IO;
}

static PilotStatus cp_uipc_send_word_locked(uint32_t addr,
                                            uint8_t width,
                                            uint32_t expected,
                                            uint32_t replacement)
{
    void *msg = NULL;
    uint8_t *b;
    uint32_t seq;
    uint32_t res = 0;
    PilotStatus status;

    if (!cp_resolve_osal_locked())
        return PILOT_STATUS_UNSUPPORTED;

    seq = cp_uipc_next_seq_locked();

    if (g_fn_valloc(PILOT_CP_UIPC_ROUTE, &msg,
                    PILOT_CP_UIPC_BYTES, PILOT_CP_UIPC_TIMEOUT) != 0 || !msg)
        return PILOT_STATUS_IO;

    if (g_fn_set_src)
        g_fn_set_src(msg, PILOT_CP_UIPC_ROUTE);

    b = (uint8_t *)msg;
    b[0] = PILOT_CP_UIPC_CMD;
    b[1] = PILOT_CP_UIPC_VER_V3;
    b[2] = width;
    b[3] = 0u;
    cp_put_le32(b + 4, addr);
    cp_put_le32(b + 8, expected);
    cp_put_le32(b + 12, replacement);
    cp_put_le32(b + 16, seq);

    if (g_fn_snd(PILOT_CP_UIPC_ROUTE, msg) != 0) {
        (void)g_fn_free(PILOT_CP_UIPC_ROUTE, msg);
        return PILOT_STATUS_IO;
    }

    status = cp_uipc_wait_ack_locked(seq, &res);
    if (status != PILOT_STATUS_OK)
        return status;
    return cp_uipc_map_result(res);
}

static PilotStatus cp_uipc_send_v4_inline_locked(uint32_t addr,
                                                 const uint8_t *data,
                                                 uint32_t size,
                                                 uint32_t *cp_res_out)
{
    void *msg = NULL;
    uint8_t *b;
    uint32_t seq;
    uint32_t res = 0;
    uint32_t msg_bytes;
    PilotStatus status;

    if (cp_res_out)
        *cp_res_out = 0;
    if (!cp_resolve_osal_locked())
        return PILOT_STATUS_UNSUPPORTED;

    seq = cp_uipc_next_seq_locked();
    /* size == 0 is the 20-byte v4/v3 instant probe packet (seq at both +4 and +16). */
    msg_bytes = (size == 0u)
                ? PILOT_CP_UIPC_BYTES
                : (PILOT_CP_UIPC_V4_HDR_BYTES + PILOT_CP_UIPC_V4_ENT_BYTES + size);

    if (g_fn_valloc(PILOT_CP_UIPC_ROUTE, &msg,
                    msg_bytes, PILOT_CP_UIPC_TIMEOUT) != 0 || !msg)
        return PILOT_STATUS_IO;

    if (g_fn_set_src)
        g_fn_set_src(msg, PILOT_CP_UIPC_ROUTE);

    b = (uint8_t *)msg;
    b[0] = PILOT_CP_UIPC_CMD;
    b[1] = PILOT_CP_UIPC_VER_V4;
    b[2] = 0u;
    b[3] = 0u;
    cp_put_le32(b + 4, seq);
    cp_put_le32(b + 8, addr);
    b[12] = (uint8_t)(size & 0xFFu);
    b[13] = (uint8_t)((size >> 8) & 0xFFu);
    if (size == 0u) {
        b[14] = 0u;
        b[15] = 0u;
        cp_put_le32(b + 16, seq);
    } else {
        memcpy(b + PILOT_CP_UIPC_V4_HDR_BYTES + PILOT_CP_UIPC_V4_ENT_BYTES,
               data, size);
    }

    if (g_fn_snd(PILOT_CP_UIPC_ROUTE, msg) != 0) {
        (void)g_fn_free(PILOT_CP_UIPC_ROUTE, msg);
        return PILOT_STATUS_IO;
    }

    status = cp_uipc_wait_ack_locked(seq, &res);
    if (status != PILOT_STATUS_OK)
        return status;
    if (cp_res_out)
        *cp_res_out = res;
    return cp_uipc_map_result(res);
}

static void cp_uipc_probe_v4_locked(void)
{
    uint32_t cp_res = 0;

    if (g_cp_uipc_v4_state != 0)
        return;

    /* 20-byte size=0 probe:
     * - v4 firmware checks classify_range(dst, 0) and ACKs PATCH_BAD_WIDTH (7)
     * - v3 firmware passes length==20 check and ACKs PATCH_BAD_HEADER (2)
     * Neither modifies CP RAM and both respond in ~0.25 ms. */
    (void)cp_uipc_send_v4_inline_locked((uint32_t)PILOT_CP_SELF_END,
                                        NULL, 0u, &cp_res);
    g_cp_uipc_v4_state = (cp_res == 7u) ? 1 : -1;
}

static PilotStatus cp_code_patch_locked(uint64_t cp_address,
                                        const uint8_t *expected,
                                        const uint8_t *replacement,
                                        size_t length)
{
    uint8_t current[PILOT_CP_MEM_TRANSFER_MAX];
    uint8_t verify[PILOT_CP_MEM_TRANSFER_MAX];
    PilotStatus status;
    size_t first = 0;
    size_t last = length;
    size_t off;

    status = cp_read_locked(cp_address, current, length);
    if (status != PILOT_STATUS_OK)
        return status;
    if (expected && memcmp(current, expected, length) != 0)
        return PILOT_STATUS_CONFLICT;
    if (memcmp(current, replacement, length) == 0)
        return PILOT_STATUS_OK;

    while (first < length && current[first] == replacement[first])
        ++first;
    while (last > first && current[last - 1u] == replacement[last - 1u])
        --last;

    if (g_cp_uipc_v4_state == 0)
        cp_uipc_probe_v4_locked();

    if (g_cp_uipc_v4_state > 0) {
        size_t pos = first;

        while (pos < last) {
            uint32_t chunk = (uint32_t)(last - pos);

            if (chunk > PILOT_CP_UIPC_V4_INLINE_MAX)
                chunk = PILOT_CP_UIPC_V4_INLINE_MAX;

            status = cp_uipc_send_v4_inline_locked(
                (uint32_t)(cp_address + pos), replacement + pos, chunk, NULL);
            if (status != PILOT_STATUS_OK)
                return status;
            pos += chunk;
        }
        goto verify_readback;
    }

    off = 0;
    while (off < length) {
        uint32_t addr = (uint32_t)(cp_address + off);
        size_t rem = length - off;
        uint8_t w = 1u;
        uint32_t cur_w;
        uint32_t rep_w;

        if (rem >= 4u && (addr & 1u) == 0u)
            w = 4u;
        else if (rem >= 2u && (addr & 1u) == 0u)
            w = 2u;

        cur_w = cp_read_le_width(current + off, w);
        rep_w = cp_read_le_width(replacement + off, w);
        if (cur_w == rep_w) {
            off += w;
            continue;
        }

        status = cp_uipc_send_word_locked(addr, w, cur_w, rep_w);
        if (status != PILOT_STATUS_OK)
            return status;
        off += w;
    }

verify_readback:
    status = cp_read_locked(cp_address, verify, length);
    if (status == PILOT_STATUS_OK && memcmp(verify, replacement, length) != 0)
        return PILOT_STATUS_IO;
    return status;
}

PilotStatus pilot_cp_mem_write_impl(uint64_t cp_address,
                                    const void *input,
                                    size_t length)
{
    PilotStatus status;
    int is_code;

    if (!__atomic_load_n(&g_pilot.optional_features_enabled, __ATOMIC_ACQUIRE))
        return PILOT_STATUS_DISABLED;
    if (!input || !length)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (length > PILOT_CP_MEM_TRANSFER_MAX)
        return PILOT_STATUS_RANGE;

    is_code = cp_is_code_range(cp_address, length);
    if (!is_code && cp_address < PILOT_CP_IMAGE_RO_END)
        return PILOT_STATUS_RANGE;

    pthread_mutex_lock(&g_cp_mem_lock);
    if (is_code)
        status = cp_code_patch_locked(cp_address, NULL, (const uint8_t *)input, length);
    else
        status = cp_write_locked(cp_address, input, length);
    pthread_mutex_unlock(&g_cp_mem_lock);
    return status;
}

PilotStatus pilot_cp_mem_patch_impl(uint64_t cp_address,
                                    const void *expected,
                                    const void *replacement,
                                    size_t length)
{
    uint8_t current[PILOT_CP_MEM_TRANSFER_MAX];
    uint8_t verify[PILOT_CP_MEM_TRANSFER_MAX];
    PilotStatus status;
    int is_code;

    if (!__atomic_load_n(&g_pilot.optional_features_enabled, __ATOMIC_ACQUIRE))
        return PILOT_STATUS_DISABLED;
    if (!replacement || !length)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (length > PILOT_CP_MEM_TRANSFER_MAX)
        return PILOT_STATUS_RANGE;

    is_code = cp_is_code_range(cp_address, length);
    if (!is_code && cp_address < PILOT_CP_IMAGE_RO_END)
        return PILOT_STATUS_RANGE;

    pthread_mutex_lock(&g_cp_mem_lock);
    if (is_code) {
        status = cp_code_patch_locked(cp_address,
                                      (const uint8_t *)expected,
                                      (const uint8_t *)replacement,
                                      length);
    } else {
        status = cp_read_locked(cp_address, current, length);
        if (status == PILOT_STATUS_OK && expected &&
            memcmp(current, expected, length) != 0) {
            status = PILOT_STATUS_CONFLICT;
        }
        if (status == PILOT_STATUS_OK &&
            memcmp(current, replacement, length) != 0) {
            status = cp_write_locked(cp_address, replacement, length);
            if (status == PILOT_STATUS_OK) {
                status = cp_read_locked(cp_address, verify, length);
                if (status == PILOT_STATUS_OK &&
                    memcmp(verify, replacement, length) != 0)
                    status = PILOT_STATUS_IO;
            }
        }
    }
    pthread_mutex_unlock(&g_cp_mem_lock);
    return status;
}

static void *cp_async_worker(void *arg)
{
    struct cp_async_job *job = (struct cp_async_job *)arg;
    const void *replacement = job->data;
    const void *expected = job->has_expected ? (job->data + job->length) : NULL;
    PilotStatus status;

    if (job->is_patch)
        status = pilot_cp_mem_patch_impl(job->cp_address, expected, replacement, job->length);
    else
        status = pilot_cp_mem_write_impl(job->cp_address, replacement, job->length);

    if (job->callback)
        job->callback(status, job->user_data);
    free(job);
    return NULL;
}

static PilotStatus cp_submit_async(uint64_t cp_address,
                                   const void *expected,
                                   const void *replacement,
                                   size_t length,
                                   int is_patch,
                                   PilotCpMemCallback callback,
                                   void *user_data)
{
    int is_code;
    size_t payload_bytes;
    struct cp_async_job *job;
    pthread_t tid;
    pthread_attr_t attr;
    int rc;

    if (!__atomic_load_n(&g_pilot.optional_features_enabled, __ATOMIC_ACQUIRE))
        return PILOT_STATUS_DISABLED;
    if (!replacement || !length)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (length > PILOT_CP_MEM_TRANSFER_MAX)
        return PILOT_STATUS_RANGE;

    is_code = cp_is_code_range(cp_address, length);
    if (!is_code && cp_address < PILOT_CP_IMAGE_RO_END)
        return PILOT_STATUS_RANGE;

    payload_bytes = expected ? (length * 2u) : length;
    job = (struct cp_async_job *)malloc(sizeof(*job) + payload_bytes);
    if (!job)
        return PILOT_STATUS_SYSTEM;

    job->cp_address = cp_address;
    job->length = length;
    job->is_patch = is_patch;
    job->has_expected = (expected != NULL);
    job->callback = callback;
    job->user_data = user_data;
    memcpy(job->data, replacement, length);
    if (expected)
        memcpy(job->data + length, expected, length);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    rc = pthread_create(&tid, &attr, cp_async_worker, job);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(job);
        return PILOT_STATUS_SYSTEM;
    }
    return PILOT_STATUS_OK;
}

PilotStatus pilot_cp_mem_write_async_impl(uint64_t cp_address,
                                          const void *input,
                                          size_t length,
                                          PilotCpMemCallback callback,
                                          void *user_data)
{
    return cp_submit_async(cp_address, NULL, input, length, 0, callback, user_data);
}

PilotStatus pilot_cp_mem_patch_async_impl(uint64_t cp_address,
                                          const void *expected,
                                          const void *replacement,
                                          size_t length,
                                          PilotCpMemCallback callback,
                                          void *user_data)
{
    return cp_submit_async(cp_address, expected, replacement, length, 1, callback, user_data);
}
