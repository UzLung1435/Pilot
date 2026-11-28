/* CP RAM Live Sensor Readers (A6700 2.00):
 *   - OPD 12-bit linear 2D Bayer RGBY metering grid (0x015B6398 -> 3x 4KB DMA)
 *     calibrated with live CP White Balance R/B gains (0x016A4998 + 0x334/0x336)
 *     and exact Rec.709 (BT.709) / S-Log3 OETF transfer LUTs
 *   - Live lens actual focal length (0x016A4998 + 0x120, Sony dec16 float)
 *   - SyncRCCalc active sensor/output geometry fallback (0x01696490) */

#include "pilot_canvas_internal.h"

#ifdef PILOT_MODEL_A7C2
/* A7 C II 2.01.  Found by matching the five movw/movt pairs that materialise
 * the table base in the CP firmware: A6700 uses 0x015B6398, the A7 C II build
 * uses 0x01651738 in five byte-identical helper functions (see
 * A7C2_CP_NOTES.md).  The slot arithmetic base + slot*0x70 matches the A6700
 * code exactly (rsb r0, r0, r0, lsl #3 / add.w r0, r1, r0, lsl #4). */
#define CP_OPD_TABLE_VA      0x01651738ull
#else
#define CP_OPD_TABLE_VA      0x015B6398ull
#endif
#define CP_OPD_ENTRY_STRIDE  0x70u
#define OPD_READ_BYTES       11264u /* max(32*352, 26*384) = 11,264 B (3 DMAs) */

/* A7 C II: these two consumer addresses were not located in 2.01 yet.  Reads
 * hit unmapped CP RAM and return an error, which both callers already treat as
 * "no value", so the lens and geometry readouts simply stay blank. */
#define CP_LENS_INFO_BASE_VA 0x016A4998ull
#define CP_WB_GAIN_OFF       0x334ull /* u16 RawWbRGain, u16 RawWbBGain */
#define CP_SYNCRC_GEOM_VA    0x01696490ull

static u16 rd_u16(const u8 *b, u32 off)
{
    return (u16)b[off] | ((u16)b[off + 1u] << 8);
}

static u32 rd_u32(const u8 *b, u32 off)
{
    return (u32)b[off] | ((u32)b[off + 1u] << 8) |
           ((u32)b[off + 2u] << 16) | ((u32)b[off + 3u] << 24);
}

static u64 rd_u64(const u8 *b, u32 off)
{
    return (u64)rd_u32(b, off) | ((u64)rd_u32(b, off + 4u) << 32);
}

static u64 vdf_pa_to_cp_addr(u64 pa)
{
    if (pa >= 0x0500000000ull && pa < 0x057E000000ull)
        return (pa - 0x0500000000ull) + 0x80000000ull;
    if (pa >= 0x0800000000ull && pa < 0x0880000000ull)
        return pa - 0x0800000000ull;
    return pa;
}

/* BT.709 OETF: 12-bit linear [0..4095] -> [0..255] (= 0..100 IRE)
 * Checkpoints: 0% -> 0 IRE, 18% -> 41 IRE, 50% -> 71 IRE, 100% -> 100 IRE */
static const u8 k_lut_rec709[257] = {
      0u,   4u,   9u,  13u,  18u,  22u,  27u,  30u,  34u,  37u,  40u,  43u,  45u,  48u,  51u,  53u,
     55u,  57u,  60u,  62u,  64u,  66u,  68u,  70u,  71u,  73u,  75u,  77u,  78u,  80u,  82u,  83u,
     85u,  86u,  88u,  89u,  91u,  92u,  94u,  95u,  96u,  98u,  99u, 100u, 102u, 103u, 104u, 105u,
    107u, 108u, 109u, 110u, 112u, 113u, 114u, 115u, 116u, 117u, 118u, 120u, 121u, 122u, 123u, 124u,
    125u, 126u, 127u, 128u, 129u, 130u, 131u, 132u, 133u, 134u, 135u, 136u, 137u, 138u, 139u, 140u,
    141u, 142u, 143u, 144u, 144u, 145u, 146u, 147u, 148u, 149u, 150u, 151u, 152u, 152u, 153u, 154u,
    155u, 156u, 157u, 158u, 158u, 159u, 160u, 161u, 162u, 162u, 163u, 164u, 165u, 166u, 166u, 167u,
    168u, 169u, 169u, 170u, 171u, 172u, 173u, 173u, 174u, 175u, 176u, 176u, 177u, 178u, 178u, 179u,
    180u, 181u, 181u, 182u, 183u, 183u, 184u, 185u, 186u, 186u, 187u, 188u, 188u, 189u, 190u, 190u,
    191u, 192u, 192u, 193u, 194u, 194u, 195u, 196u, 196u, 197u, 198u, 198u, 199u, 200u, 200u, 201u,
    202u, 202u, 203u, 203u, 204u, 205u, 205u, 206u, 207u, 207u, 208u, 208u, 209u, 210u, 210u, 211u,
    212u, 212u, 213u, 213u, 214u, 215u, 215u, 216u, 216u, 217u, 217u, 218u, 219u, 219u, 220u, 220u,
    221u, 222u, 222u, 223u, 223u, 224u, 224u, 225u, 226u, 226u, 227u, 227u, 228u, 228u, 229u, 229u,
    230u, 231u, 231u, 232u, 232u, 233u, 233u, 234u, 234u, 235u, 235u, 236u, 237u, 237u, 238u, 238u,
    239u, 239u, 240u, 240u, 241u, 241u, 242u, 242u, 243u, 243u, 244u, 244u, 245u, 245u, 246u, 246u,
    247u, 247u, 248u, 249u, 249u, 250u, 250u, 251u, 251u, 252u, 252u, 253u, 253u, 254u, 254u, 255u,
    255u,
};

/* Sony S-Log3 OETF (legal IRE, 0.18 -> 41 IRE, 0.90 -> 61 IRE, 1.00 -> 62.4 IRE):
 * Exact Sony S-Log3 formula on linear [0..1] (same exposure reference as k_lut_rec709). */
static const u8 k_lut_slog3[257] = {
      9u,  17u,  24u,  32u,  37u,  42u,  46u,  50u,  53u,  56u,  59u,  61u,  64u,  66u,  68u,  70u,
     72u,  74u,  75u,  77u,  78u,  80u,  81u,  82u,  84u,  85u,  86u,  87u,  88u,  89u,  90u,  91u,
     92u,  93u,  94u,  95u,  96u,  97u,  98u,  98u,  99u, 100u, 101u, 101u, 102u, 103u, 104u, 104u,
    105u, 106u, 106u, 107u, 107u, 108u, 109u, 109u, 110u, 110u, 111u, 111u, 112u, 112u, 113u, 113u,
    114u, 114u, 115u, 115u, 116u, 116u, 117u, 117u, 118u, 118u, 119u, 119u, 119u, 120u, 120u, 121u,
    121u, 122u, 122u, 122u, 123u, 123u, 123u, 124u, 124u, 125u, 125u, 125u, 126u, 126u, 126u, 127u,
    127u, 127u, 128u, 128u, 128u, 129u, 129u, 129u, 130u, 130u, 130u, 130u, 131u, 131u, 131u, 132u,
    132u, 132u, 133u, 133u, 133u, 133u, 134u, 134u, 134u, 134u, 135u, 135u, 135u, 136u, 136u, 136u,
    136u, 137u, 137u, 137u, 137u, 138u, 138u, 138u, 138u, 138u, 139u, 139u, 139u, 139u, 140u, 140u,
    140u, 140u, 141u, 141u, 141u, 141u, 141u, 142u, 142u, 142u, 142u, 142u, 143u, 143u, 143u, 143u,
    144u, 144u, 144u, 144u, 144u, 145u, 145u, 145u, 145u, 145u, 145u, 146u, 146u, 146u, 146u, 146u,
    147u, 147u, 147u, 147u, 147u, 148u, 148u, 148u, 148u, 148u, 148u, 149u, 149u, 149u, 149u, 149u,
    149u, 150u, 150u, 150u, 150u, 150u, 150u, 151u, 151u, 151u, 151u, 151u, 151u, 152u, 152u, 152u,
    152u, 152u, 152u, 153u, 153u, 153u, 153u, 153u, 153u, 153u, 154u, 154u, 154u, 154u, 154u, 154u,
    154u, 155u, 155u, 155u, 155u, 155u, 155u, 155u, 156u, 156u, 156u, 156u, 156u, 156u, 156u, 157u,
    157u, 157u, 157u, 157u, 157u, 157u, 158u, 158u, 158u, 158u, 158u, 158u, 158u, 158u, 159u, 159u,
    159u,
};

static inline u8 lin12_to_lut(int32_t raw, const u8 *lut)
{
    if (raw <= 0)
        return lut[0];
    if (raw >= 4095)
        return lut[256];
    u32 idx = (u32)raw >> 4;
    u32 frac = (u32)raw & 0xFu;
    u32 a = lut[idx];
    u32 b = lut[idx + 1u];
    return (u8)(a + (((b - a) * frac + 8u) >> 4));
}

/* Track 4 candidate OPD banks:
 *   0: OPD[0] Bank 0 (+0x0000)   1: OPD[0] Bank 1 (+0xA000)
 *   2: OPD[2] Bank 0 (+0x0000)   3: OPD[2] Bank 1 (+0xA000) */
#define OPD_CAND_COUNT 4u
static u64 g_opd_cand_addr[OPD_CAND_COUNT];
static u32 g_opd_last_hash[OPD_CAND_COUNT];
static int g_opd_active = 0;
static u32 g_opd_stale_cnt;

static u32 fnv1a_32(const u8 *p, size_t n)
{
    u32 h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static int opd_refresh_table(void)
{
    if (!g_api || !g_api->cp_mem || !g_api->cp_mem->read)
        return 0;
    int any = 0;
    static const u32 slots[2] = { 0u, 2u };
    for (u32 s = 0; s < 2u; ++s) {
        u8 ent[CP_OPD_ENTRY_STRIDE] = {0};
        u64 b0 = 0u, b1 = 0u;
        if (g_api->cp_mem->read(CP_OPD_TABLE_VA + (u64)slots[s] * CP_OPD_ENTRY_STRIDE,
                                ent, sizeof(ent)) == PILOT_STATUS_OK) {
            u64 pa = rd_u64(ent, 0x58u);
            u32 sz = rd_u32(ent, 0x60u);
            if (pa && sz >= OPD_READ_BYTES) {
                b0 = vdf_pa_to_cp_addr(pa);
                if (sz >= 0xA000u + OPD_READ_BYTES)
                    b1 = b0 + 0xA000ull;
                any = 1;
            }
        }
        g_opd_cand_addr[s * 2u + 0u] = b0;
        g_opd_cand_addr[s * 2u + 1u] = b1;
    }
    return any;
}

static int opd_ensure_addr(void)
{
    if (g_opd_cand_addr[g_opd_active])
        return 1;
    if (!opd_refresh_table())
        return 0;
    for (int i = 0; i < (int)OPD_CAND_COUNT; ++i) {
        if (g_opd_cand_addr[i]) {
            g_opd_active = i;
            mlogf("LV_SCOPE_INIT cand=%d cp=0x%llx",
                  i, (unsigned long long)g_opd_cand_addr[i]);
            return 1;
        }
    }
    return 0;
}

/* Probe all 4 candidate OPD banks (OPD[0]/OPD[2] x Bank0/Bank1) and switch to
 * whichever bank is actively updating. */
static void opd_try_switch_live_slot(void)
{
    (void)opd_refresh_table();
    u32 hs[OPD_CAND_COUNT] = {0};
    for (int i = 0; i < (int)OPD_CAND_COUNT; ++i) {
        if (!g_opd_cand_addr[i])
            continue;
        u8 probe[256] = {0};
        if (g_api->cp_mem->read(g_opd_cand_addr[i] + 2048u, probe, sizeof(probe)) != PILOT_STATUS_OK)
            continue;
        u32 h = fnv1a_32(probe, sizeof(probe));
        hs[i] = h;
        if (i != g_opd_active && g_opd_last_hash[i] != 0u && h != g_opd_last_hash[i]) {
            g_opd_last_hash[i] = h;
            g_opd_active = i;
            g_opd_stale_cnt = 0u;
            mlogf("LV_SCOPE_SWITCH cand=%d cp=0x%llx",
                  i, (unsigned long long)g_opd_cand_addr[i]);
            return;
        }
        g_opd_last_hash[i] = h;
    }
    if (g_opd_stale_cnt == 5u) {
        mlogf("LV_SCOPE_STALE cur=%d h=%08x/%08x/%08x/%08x",
              g_opd_active, hs[0], hs[1], hs[2], hs[3]);
    }
}

/* Read live CP White Balance R/B gains (Q8 fixed point: 256 = 1.0x).
 * Fallback: IMX684 (A6700) neutral D55 sensor WB multipliers (R=2.15x, G=1.00x, B=1.65x)
 * because pre-WB Bayer OPD Green micro-filters have ~2x higher sensitivity than R/B. */
#define IMX684_D55_R_Q8 550u /* 2.15x */
#define IMX684_D55_B_Q8 422u /* 1.65x */

static void read_wb_gains_q8(u32 *r_q8, u32 *b_q8, u16 *raw_r_out, u16 *raw_b_out)
{
    *r_q8 = IMX684_D55_R_Q8;
    *b_q8 = IMX684_D55_B_Q8;
    *raw_r_out = 0u;
    *raw_b_out = 0u;
    if (!g_api || !g_api->cp_mem || !g_api->cp_mem->read)
        return;

    u8 wbuf[4] = {0};
    if (g_api->cp_mem->read(CP_LENS_INFO_BASE_VA + CP_WB_GAIN_OFF, wbuf, sizeof(wbuf)) !=
        PILOT_STATUS_OK)
        return;

    u16 rg = rd_u16(wbuf, 0u);
    u16 bg = rd_u16(wbuf, 2u);
    *raw_r_out = rg;
    *raw_b_out = bg;
    if (rg == 0u || bg == 0u)
        return;

    /* Sony CP stores WB gains in Q8 (0x0100 = 1.0x) or Q10 (0x0400 = 1.0x).
     * Auto-detect unit scale safely and clamp to [0.5x .. 3.5x]. */
    u32 unit = (rg >= 0x0300u || bg >= 0x0300u) ? 1024u : 256u;
    u32 rq = ((u32)rg * 256u) / unit;
    u32 bq = ((u32)bg * 256u) / unit;
    if (rq < 128u) rq = 128u;
    if (rq > 896u) rq = 896u;
    if (bq < 128u) bq = 128u;
    if (bq > 896u) bq = 896u;
    *r_q8 = rq;
    *b_q8 = bq;
}

/* Auto-detect whether s_opd_span holds 44-col (352B, Photo 3:2) or 48-col (384B, Movie 16:9)
 * using vertical row-to-row L1 correlation strictly within rows 1..18 (<= 7,296 B,
 * always inside active pixels in both modes — never touching the post-grid trailer). */
static int detect_opd_is_48x26(const u8 *buf, int prev_is_48, u64 *out_e352, u64 *out_e384)
{
    u64 err352 = 0u, err384 = 0u;
    for (u32 r = 1u; r < 18u; ++r) {
        const u8 *a352 = buf + r * 352u;
        const u8 *b352 = buf + (r + 1u) * 352u;
        const u8 *a384 = buf + r * 384u;
        const u8 *b384 = buf + (r + 1u) * 384u;
        for (u32 x = 0u; x < 176u; x += 2u) {
            int d1 = (int)(int16_t)rd_u16(a352, x) - (int)(int16_t)rd_u16(b352, x);
            int d2 = (int)(int16_t)rd_u16(a384, x) - (int)(int16_t)rd_u16(b384, x);
            err352 += (u64)(d1 < 0 ? -d1 : d1);
            err384 += (u64)(d2 < 0 ? -d2 : d2);
        }
    }
    if (out_e352) *out_e352 = err352;
    if (out_e384) *out_e384 = err384;
    if (err384 * 100u < err352 * 90u) return 1;
    if (err352 * 100u < err384 * 90u) return 0;
    return prev_is_48;
}

int canvas_cp_read_opd(PilotOpdFrame *out, int gamma_mode, int wb_enable)
{
    if (!out || !opd_ensure_addr())
        return 0;

    /* Periodically re-read 0x015B6398 (~1 Hz) so Photo <-> Movie dial switches
     * immediately follow any reallocated OPD physical address. */
    static u32 s_refresh_tick;
    if ((++s_refresh_tick % 10u) == 0u) {
        u64 prev = g_opd_cand_addr[g_opd_active];
        (void)opd_refresh_table();
        if (g_opd_cand_addr[g_opd_active] && g_opd_cand_addr[g_opd_active] != prev) {
            mlogf("LV_SCOPE_REBASE cand=%d cp=0x%llx",
                  g_opd_active, (unsigned long long)g_opd_cand_addr[g_opd_active]);
        }
    }

    /* Batch-read 11,264 B in 3 DMAs from active bank. */
    static u8 s_opd_span[OPD_READ_BYTES];
    u64 cur_addr = g_opd_cand_addr[g_opd_active];
    for (u32 off = 0u; off < OPD_READ_BYTES; off += 4096u) {
        u32 chunk = (OPD_READ_BYTES - off < 4096u) ? (OPD_READ_BYTES - off) : 4096u;
        if (g_api->cp_mem->read(cur_addr + (u64)off, s_opd_span + off, chunk) !=
            PILOT_STATUS_OK)
            return 0;
    }

    /* Detect frozen/stale bank */
    u32 h = fnv1a_32(s_opd_span + 2048u, 256u);
    if (h == g_opd_last_hash[g_opd_active]) {
        if (++g_opd_stale_cnt >= 2u) {
            opd_try_switch_live_slot();
            if (g_opd_cand_addr[g_opd_active] != cur_addr && g_opd_cand_addr[g_opd_active]) {
                cur_addr = g_opd_cand_addr[g_opd_active];
                for (u32 off = 0u; off < OPD_READ_BYTES; off += 4096u) {
                    u32 chunk = (OPD_READ_BYTES - off < 4096u) ? (OPD_READ_BYTES - off) : 4096u;
                    if (g_api->cp_mem->read(cur_addr + (u64)off, s_opd_span + off, chunk) !=
                        PILOT_STATUS_OK)
                        return 0;
                }
            }
        }
    } else {
        g_opd_last_hash[g_opd_active] = h;
        g_opd_stale_cnt = 0u;
    }

    static int s_is_48x26 = -1;
    u64 e352 = 0u, e384 = 0u;
    int next_48 = detect_opd_is_48x26(s_opd_span, s_is_48x26 < 0 ? 0 : s_is_48x26, &e352, &e384);
    int geom_changed = (next_48 != s_is_48x26);
    s_is_48x26 = next_48;

    const u32 cols       = s_is_48x26 ? 48u : 44u;
    const u32 max_rows   = s_is_48x26 ? 26u : 32u;
    const u32 row_bytes  = cols * 4u;
    const u32 pair_bytes = row_bytes * 2u;

    u32 r_q8 = 256u, b_q8 = 256u;
    u16 raw_rg = 0u, raw_bg = 0u;
    if (wb_enable)
        read_wb_gains_q8(&r_q8, &b_q8, &raw_rg, &raw_bg);

    const u8 *lut = (gamma_mode == SCOPE_GAMMA_SLOG3) ? k_lut_slog3 : k_lut_rec709;

    static int s_wb_logged;
    u64 sum_c0 = 0, sum_c1 = 0, sum_c2 = 0, sum_c3 = 0;

    memset(out, 0, sizeof(*out));
    out->cols = cols;
    u32 valid_rows = 0u;
    for (u32 r = 0; r < max_rows; ++r) {
        const u8 *pair_buf = s_opd_span + r * pair_bytes;
        /* Stop before any post-grid binary trailer (non-pixel words < -256 or > 4095) */
        if (r >= 20u) {
            u32 bad = 0u;
            for (u32 o = 0u; o < pair_bytes; o += 4u) {
                int16_t v = (int16_t)rd_u16(pair_buf, o);
                if (v < -256 || v > 4095) {
                    if (++bad >= 4u) break;
                }
            }
            if (bad >= 4u)
                break;
        }
        for (u32 c = 0; c < cols; ++c) {
            u32 off = c * 4u;
            int32_t c0 = (int16_t)rd_u16(pair_buf, off);
            int32_t c1 = (int16_t)rd_u16(pair_buf, off + 2u);
            int32_t c2 = (int16_t)rd_u16(pair_buf, row_bytes + off);
            int32_t c3 = (int16_t)rd_u16(pair_buf, row_bytes + off + 2u);
            if (c0 < 0 || c0 > 4095) c0 = 0;
            if (c1 < 0 || c1 > 4095) c1 = 0;
            if (c2 < 0 || c2 > 4095) c2 = 0;
            if (c3 < 0 || c3 > 4095) c3 = 0;
            if (!s_wb_logged) {
                sum_c0 += (u64)c0;
                sum_c1 += (u64)c1;
                sum_c2 += (u64)c2;
                sum_c3 += (u64)c3;
            }
            int32_t lin_r = (int32_t)(((((u32)c0 * r_q8) >> 8) * 7u) >> 2);
            int32_t lin_g = (int32_t)((((u32)(c1 + c2) >> 1) * 7u) >> 2);
            int32_t lin_b = (int32_t)(((((u32)c3 * b_q8) >> 8) * 7u) >> 2);
            /* IMX684 Camera-RGB -> Linear sRGB 3x3 CCM (Q8, each row sums to 256):
             * separates overlapping Bayer filter spectra while keeping neutral gray exact. */
            if (wb_enable) {
                int32_t mr = ( 448 * lin_r - 154 * lin_g -  38 * lin_b) >> 8;
                int32_t mg = ( -64 * lin_r + 384 * lin_g -  64 * lin_b) >> 8;
                int32_t mb = ( -13 * lin_r - 115 * lin_g + 384 * lin_b) >> 8;
                lin_r = mr;
                lin_g = mg;
                lin_b = mb;
            }
            u8 rv = lin12_to_lut(lin_r, lut);
            u8 gv = lin12_to_lut(lin_g, lut);
            u8 bv = lin12_to_lut(lin_b, lut);
            out->r[r][c] = rv;
            out->g[r][c] = gv;
            out->b[r][c] = bv;
            out->y[r][c] = (u8)(((u32)rv * 54u + (u32)gv * 183u + (u32)bv * 19u) >> 8);
        }
        valid_rows = r + 1u;
    }
    out->rows = valid_rows ? valid_rows : max_rows;
    if (geom_changed) {
        mlogf("LV_SCOPE_GEOM mode=%ux%u(%s) e352=%llu e384=%llu",
              out->cols, out->rows, s_is_48x26 ? "Movie" : "Photo",
              (unsigned long long)e352, (unsigned long long)e384);
    }

    if (!s_wb_logged && (sum_c0 + sum_c1 + sum_c2 + sum_c3) > 0u) {
        s_wb_logged = 1;
        u64 n = (u64)out->rows * cols;
        mlogf("SCOPE_WB_INIT geom=%ux%u raw_r=0x%x raw_b=0x%x avg=(%u,%u,%u,%u)",
              cols, out->rows, (unsigned)raw_rg, (unsigned)raw_bg,
              (unsigned)(sum_c0 / n), (unsigned)(sum_c1 / n),
              (unsigned)(sum_c2 / n), (unsigned)(sum_c3 / n));
    }

    out->valid = 1;
    return 1;
}

/* Decode Sony 16-bit decimal float (exp = v>>12, mant = v&0xFFF, mm = mant * 10^(exp-13))
 * into tenths of a millimeter (mm10). */
static u32 sony_dec16_to_mm10(u16 v)
{
    u32 exp = (u32)(v >> 12) & 0xFu;
    u32 mant = (u32)(v & 0x0FFFu);
    if (mant == 0u)
        return 0u;
    switch (exp) {
    case 0xAu: return (mant + 50u) / 100u; /* 0.001mm */
    case 0xBu: return (mant + 5u) / 10u;   /* 0.01mm  (0..40.95mm) */
    case 0xCu: return mant;                /* 0.1mm   (0..409.5mm) */
    case 0xDu: return mant * 10u;          /* 1mm     (0..4095mm) */
    case 0xEu: return mant * 100u;         /* 10mm */
    default:   return 0u;
    }
}

int canvas_cp_read_focal_mm(char *out, size_t out_sz)
{
    if (!out || out_sz < 8u || !g_api || !g_api->cp_mem || !g_api->cp_mem->read)
        return 0;
    out[0] = '\0';

    u8 fbuf[4] = {0};
    if (g_api->cp_mem->read(CP_LENS_INFO_BASE_VA + 0x11Eull, fbuf, sizeof(fbuf)) !=
        PILOT_STATUS_OK)
        return 0;

    u16 eq35 = rd_u16(fbuf, 0u);
    u16 act  = rd_u16(fbuf, 2u);
    u32 mm10 = sony_dec16_to_mm10(act);

    static int s_focal_logged;
    if (!s_focal_logged && mm10 > 0u) {
        s_focal_logged = 1;
        mlogf("CANVAS_FOCAL_INIT act=0x%04x eq35=0x%04x mm10=%u",
              (unsigned)act, (unsigned)eq35, mm10);
    }

    if (mm10 > 0u && mm10 <= 20000u) {
        if ((mm10 % 10u) == 0u)
            snprintf(out, out_sz, "%umm", mm10 / 10u);
        else
            snprintf(out, out_sz, "%u.%umm", mm10 / 10u, mm10 % 10u);
        return 1;
    }
    return 0;
}

int canvas_cp_read_sync_geom(char *out, size_t out_sz)
{
    if (!out || out_sz < 12u || !g_api || !g_api->cp_mem || !g_api->cp_mem->read)
        return 0;
    out[0] = '\0';

    u8 blk[20] = {0};
    if (g_api->cp_mem->read(CP_SYNCRC_GEOM_VA, blk, sizeof(blk)) != PILOT_STATUS_OK)
        return 0;
    u32 in_w  = rd_u16(blk, 0x00);
    u32 in_h  = rd_u16(blk, 0x02);
    u32 out_w = rd_u16(blk, 0x0C);
    u32 out_h = rd_u16(blk, 0x0E);
    u32 mode  = rd_u32(blk, 0x10);
    if (in_w == 0 && out_w == 0)
        return 0;
    if (out_w && out_h)
        snprintf(out, out_sz, "%ux%u", out_w, out_h);
    else if (in_w && in_h)
        snprintf(out, out_sz, "%ux%u", in_w, in_h);
    else
        snprintf(out, out_sz, "[%04x]", mode & 0xFFFFu);
    return 1;
}
