#include <stdint.h>

#define PATCH_COMMAND 0x7Eu
#define PATCH_VER_V3  3u
#define PATCH_VER_V4  4u
#define PATCH_V3_LEN  20u
#define PATCH_V4_HDR  8u
#define PATCH_V4_ENT  6u

/* v4 unified batch frame (8 B header + N * (4 B dst + 2 B size + size B data)):
 *   msg[0]      = 0x7E
 *   msg[1]      = 4
 *   msg[2]      = flags (must be 0)
 *   msg[3]      = reserved (must be 0)
 *   msg[4..7]   = seq (non-zero)
 *   entries[]:  { uint32_t dst_addr; uint16_t size; uint8_t data[size]; }
 */

/* Allowed target ranges (all verified identity-mapped in CP startup MMU):
 * - Code (EXEC_MAP, AP=111 RX): 0x00128000..0x011D8000, excluding self+status 0x00C12D68..0x00C12FBF
 *   (0x00C12FA0..0x00C12FAF is the 16 B status/receipt slot; 0x00C12FC0..0x00C12FFF is RX smoke scratch)
 * - Data (DATA_MAP, AP=001 RW): 0x011DA000..0x01ACF674
 */
#define EXEC_START    0x00128000u
#define EXEC_END      0x011D8000u /* covers .text + .rodata + NX_MAP (incl. 0x010CD634 / 0x010CE250) */
#define SELF_START    0x00C12D68u
#define SELF_END      0x00C12FC0u
#define DATA_START    0x011DA000u
#define DATA_END      0x01ACF674u

#define STATUS_VA     0x00C12FA0u
#define STATUS_MAGIC  0x50544348u /* 'PTCH' */

enum {
    PATCH_OK = 0,
    PATCH_BAD_LENGTH = 1,
    PATCH_BAD_HEADER = 2,
    PATCH_BAD_TARGET = 3,
    PATCH_OLD_VALUE_MISMATCH = 4,
    PATCH_READBACK_MISMATCH = 5,
    PATCH_BAD_SEQUENCE = 6,
    PATCH_BAD_WIDTH = 7,
};

__attribute__((noinline))
static uint32_t classify_range(uint32_t addr, uint32_t size) {
    if (size == 0u)
        return PATCH_BAD_WIDTH;
    uint32_t end = addr + size;
    uint32_t ex_end = EXEC_END;
    if (end >= addr &&
        ((addr >= ex_end + 0x2000u && end <= DATA_END) ||
         (addr >= EXEC_START && end <= ex_end &&
          (end <= SELF_START || addr >= SELF_END))))
        return PATCH_OK;
    return PATCH_BAD_TARGET;
}

static int mem_eq(const volatile uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

typedef uint32_t __attribute__((aligned(1))) u32_u;
typedef uint16_t __attribute__((aligned(1))) u16_u;

static inline uint32_t read_u32(const uint8_t *p) { return *(const u32_u *)p; }
static inline uint32_t read_u16(const uint8_t *p) { return *(const u16_u *)p; }

/* do_apply: 0 = validate entries (returns 0=ok, >1=error)
 *           1 = apply + clean + verify atomically under DACR + IRQ disable */
__attribute__((noinline))
static uint32_t run_entries(const uint8_t *p, uint32_t len, int do_apply) {
#ifndef HOST_USERMODE_TEST
    uint32_t cpsr = 0u, dacr = 0u;
    if (do_apply) {
        uint32_t mgr = 0xFFFFFFFFu;
        __asm__ volatile(
            "mrs %0, cpsr\n\t"
            "cpsid i\n\t"
            "mrc p15, 0, %1, c3, c0, 0\n\t"
            "mcr p15, 0, %2, c3, c0, 0\n\t"
            "isb\n\t"
            : "=&r"(cpsr), "=&r"(dacr)
            : "r"(mgr)
            : "memory");
    }
#endif
    const uint8_t *end = p + len;
    uint32_t res = PATCH_OK;
    do {
        if ((uint32_t)(end - p) < PATCH_V4_ENT)
            return PATCH_BAD_LENGTH;
        uint32_t dst = read_u32(p);
        uint32_t sz = read_u16(p + 4u);
        p += PATCH_V4_ENT;
        if (!do_apply) {
            uint32_t k = classify_range(dst, sz);
            if (k != PATCH_OK)
                return k;
            if (sz > (uint32_t)(end - p))
                return PATCH_BAD_LENGTH;
            p += sz;
        } else {
            volatile uint8_t *d = (volatile uint8_t *)dst;
            uint32_t n = sz;
            while (n--) {
                uint8_t v = *p++;
                *d = v;
                if (*d++ != v)
                    res = PATCH_READBACK_MISMATCH;
            }
#ifndef HOST_USERMODE_TEST
            uint32_t ca = dst & ~31u;
            uint32_t ce = dst + sz;
            do {
                __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(ca) : "memory"); /* DCCMVAC */
                ca += 32u;
            } while (ca < ce);
#endif
        }
    } while (p < end);
#ifndef HOST_USERMODE_TEST
    if (do_apply) {
        uint32_t zero = 0u;
        __asm__ volatile(
            "dsb\n\t"
            "mcr p15, 0, %0, c7, c1, 0\n\t"  /* ICIALLUIS */
            "mcr p15, 0, %0, c7, c1, 6\n\t"  /* BPIALLIS */
            "dsb\n\t"
            "isb\n\t"
            "mcr p15, 0, %1, c3, c0, 0\n\t"  /* restore DACR */
            "isb\n\t"
            "msr cpsr_c, %2\n\t"
            :
            : "r"(zero), "r"(dacr), "r"(cpsr)
            : "memory");
    }
#endif
    return res;
}

int cp_patch_apply(uint8_t *msg, uint32_t length) {
    if (length < PATCH_V4_HDR)
        return PATCH_BAD_LENGTH;

    uint32_t target = 0u;
    uint32_t seq = 0u;
    uint32_t result = PATCH_BAD_HEADER;
    uint8_t ver = msg[1];
    const uint8_t *ent_buf;
    uint32_t ent_len;

#ifdef HOST_USERMODE_TEST
    if (msg[0] != PATCH_COMMAND)
        goto record_and_wipe;
#endif

    if (ver == PATCH_VER_V4) {
        seq = read_u32(msg + 4u);
        if (read_u16(msg + 2u) != 0u)
            goto record_and_wipe;
        ent_buf = msg + PATCH_V4_HDR;
        ent_len = length - PATCH_V4_HDR;
        result = run_entries(ent_buf, ent_len, 0);
        if (result != PATCH_OK)
            goto record_and_wipe;
    } else {
        if (length != PATCH_V3_LEN)
            return PATCH_BAD_LENGTH;
        target = read_u32(msg + 4u);
        seq = read_u32(msg + 16u);
        if (ver != PATCH_VER_V3)
            goto record_and_wipe;
        uint32_t sz = msg[2];
        if (sz != 1u && sz != 2u && sz != 4u) {
            result = PATCH_BAD_WIDTH;
            goto record_and_wipe;
        }
        result = (sz > 1u && (target & 1u) != 0u) ? PATCH_BAD_TARGET
                                                  : classify_range(target, sz);
        if (result != PATCH_OK)
            goto record_and_wipe;
        if (!mem_eq((const volatile uint8_t *)target, msg + 8u, sz)) {
            result = PATCH_OLD_VALUE_MISMATCH;
            goto record_and_wipe;
        }
        *(u32_u *)(msg + 6u) = target;
        *(u16_u *)(msg + 10u) = (uint16_t)sz;
        ent_buf = msg + 6u;
        ent_len = PATCH_V4_ENT + sz;
    }

    if (seq == 0u) {
        result = PATCH_BAD_SEQUENCE;
        goto record_and_wipe;
    }

    result = run_entries(ent_buf, ent_len, 1);

record_and_wipe:
    {
        struct __attribute__((packed)) {
            uint16_t pad;
            uint32_t dst;
            uint16_t sz;
            uint32_t w[4];
        } st;
        st.dst = STATUS_VA;
        st.sz = 16u;
        st.w[0] = STATUS_MAGIC;
        st.w[1] = seq;
        st.w[2] = result;
        st.w[3] = target;
        (void)run_entries((const uint8_t *)&st.dst, PATCH_V4_ENT + 16u, 1);
    }
    if (length > PATCH_V3_LEN)
        length = PATCH_V3_LEN;
    while (length--)
        ((volatile uint8_t *)msg)[length] = 0u;
    return (int)result;
}
