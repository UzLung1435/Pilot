/* A7 C II 自建渲染宿主。
 *
 * A6700 上 canvas 复用 menu_ui 递进来的 Live View widget；A7 C II 没有那条
 * 链路（LV 文本槽在 .rodata 里是死数据），所以改为自己建：
 *
 *   ID::genID -> WidgetSystem::createLayer -> vt[1]/vt[3]
 *     -> Widget() -> setSize -> setLayerID -> canvas -> initSprite
 *
 * 探针 run-554avepk 已经验证这条链路能画出内容（64 条竖条出现在屏幕上）。
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <link.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "pilot_canvas_internal.h"

#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

/* qemu never writes the SD card back after a crash, so a module.log-only
   message is invisible exactly when it matters most.  Mirror every line to
   the kernel ring buffer (falls back to /dev/console), reopening the device
   per line: the UI framework closes descriptors it does not own. */
static void host_mirror(const char *fmt, ...)
{
    char line[320];
    va_list ap;
    int n;
    int fd;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;

    fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        char out[360];
        int m = snprintf(out, sizeof(out), "<6>pilot_canvas: %s\n", line);
        ssize_t ignored;

        if (m > 0)
            ignored = write(fd, out, (size_t)m);
        (void)ignored;
        close(fd);
        return;
    }

    fd = open("/dev/console", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd >= 0) {
        char out[360];
        int m = snprintf(out, sizeof(out), "pilot_canvas: %s\n", line);
        ssize_t ignored;

        if (m > 0)
            ignored = write(fd, out, (size_t)m);
        (void)ignored;
        close(fd);
    }
}

#define hlog(...) do { mlogf(__VA_ARGS__); host_mirror(__VA_ARGS__); } while (0)

/* Defined in canvas_core.c but absent from pilot_canvas_internal.h. */
PilotStatus pilot_canvas_commit_v1(PilotCanvas *c);
PilotStatus pilot_canvas_bind_widget_v1(PilotCanvas *c, void *widget);

/* sub_17CCF6C 在 UI 启动时把第一个全局层指针写在这里。 */
#define APPFW_BSS_FIRST_LAYER 0x5F66BF0ull

#define HOST_W 600
#define HOST_H 300
#define HOST_WAIT_MS 60000
#define HOST_POLL_US 250000

typedef uint32_t (*fn_gen_id)(const char *name);
typedef void *(*fn_wgtsys)(void);
typedef void *(*fn_create_layer)(void *self, const uint32_t *id);
typedef void (*fn_layer_cfg)(void *self, int value);
typedef void *(*fn_widget_set_layer_id)(void *self, const uint32_t *id);

static uintptr_t g_host_base;
static void *g_host_layer;
static uint32_t g_host_layer_id;
static unsigned char g_host_widget[16384] __attribute__((aligned(16)));

static int host_find_appfw(struct dl_phdr_info *info, size_t size, void *data)
{
    const char *name = info->dlpi_name ? info->dlpi_name : "";
    const char *slash = strrchr(name, '/');

    (void)size;
    (void)data;
    if (strcmp(slash ? slash + 1 : name, "appFw.so") == 0) {
        g_host_base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

static int host_wait_ui(void)
{
    int polls = 0;
    int max_polls = (HOST_WAIT_MS * 1000) / (int)HOST_POLL_US;

    if (g_host_base == 0u) {
        (void)dl_iterate_phdr(host_find_appfw, NULL);
    }
    if (g_host_base == 0u) {
        hlog("HOST a7c2: appFw base unknown");
        return 0;
    }

    while (polls < max_polls) {
        uint64_t first = *(volatile uint64_t *)(uintptr_t)(
            g_host_base + APPFW_BSS_FIRST_LAYER);

        if (first != 0u) {
            hlog("HOST a7c2: ui ready after %d ms (layer0=%p)",
                  polls * 250, (void *)(uintptr_t)first);
            return 1;
        }
        usleep(HOST_POLL_US);
        ++polls;
    }
    hlog("HOST a7c2: ui never came up");
    return 0;
}

static int host_create_layer(void)
{
    fn_gen_id gen_id = (fn_gen_id)dlsym(RTLD_DEFAULT, "_ZN2ux4core2ID5genIDEPKc");
    fn_wgtsys get_wgtsys = (fn_wgtsys)dlsym(RTLD_DEFAULT, "_ZN8UXEngine6wgtsysEv");
    fn_create_layer create_layer = (fn_create_layer)dlsym(
        RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE");
    void *wgtsys;

    if (gen_id == NULL || get_wgtsys == NULL || create_layer == NULL) {
        hlog("HOST a7c2: layer symbols missing");
        return 0;
    }

    wgtsys = get_wgtsys();
    if (wgtsys == NULL) {
        hlog("HOST a7c2: wgtsys null");
        return 0;
    }

    g_host_layer_id = gen_id("PILOT_CV");
    g_host_layer = create_layer(wgtsys, &g_host_layer_id);
    if (g_host_layer == NULL) {
        hlog("HOST a7c2: createLayer returned null");
        return 0;
    }

    {
        void **vt = *(void ***)g_host_layer;

        if (vt != NULL && vt[1] != NULL)
            ((fn_layer_cfg)vt[1])(g_host_layer, 1);
        if (vt != NULL && vt[3] != NULL)
            ((fn_layer_cfg)vt[3])(g_host_layer, 0);
    }

    hlog("HOST a7c2: layer=%p id=0x%08x", g_host_layer,
          (unsigned)g_host_layer_id);
    return 1;
}

static int host_create_widget(void)
{
    fn_widget_ctor ctor = (fn_widget_ctor)dlsym(
        RTLD_DEFAULT, "_ZN2ux6wgtsys6WidgetC2Ev");
    fn_widget_set_size set_size = (fn_widget_set_size)dlsym(
        RTLD_DEFAULT, "_ZN2ux6wgtsys6Widget7setSizeEff");
    fn_widget_set_layer_id set_layer = (fn_widget_set_layer_id)dlsym(
        RTLD_DEFAULT, "_ZN2ux6wgtsys6Widget10setLayerIDERKNS_4core2IDE");

    if (ctor == NULL || set_size == NULL || set_layer == NULL) {
        hlog("HOST a7c2: widget symbols missing");
        return 0;
    }

    memset(g_host_widget, 0, sizeof(g_host_widget));
    ctor(g_host_widget);
    set_size(g_host_widget, (float)HOST_W, (float)HOST_H);
    set_layer(g_host_widget, &g_host_layer_id);

    hlog("HOST a7c2: widget=%p vptr=%p size=%dx%d", (void *)g_host_widget,
          *(void **)g_host_widget, HOST_W, HOST_H);
    return 1;
}

/* Current widget origin on the layer, shared by the renderer and the drag
   handler. */
static int g_widget_x;
static int g_widget_y;

/* Double tapping the canvas cycles through the demo views; there is no DISP
   key wire-up on A7 C II yet, and a tap target is easier to hit anyway. */
static int g_view;
#define HOST_VIEW_COUNT 3

/* Latest metering-grid frame read from CP RAM (valid only when g_opd_ok). */
static PilotOpdFrame g_opd_frame;
static int g_opd_ok;

/* Last controller screen whose getter ran; 0 means "nothing observed yet".
   The page table entries at 0x051F7810 + 0x60*sid are getters that fire every
   time the corresponding screen is constructed, so hooking them reports screen
   switches without having to know what a screen even is. */
static unsigned int g_last_screen;

/* 0x343 is the live view / main screen -- it is the id that open_screen keeps
   returning to.  Everything else observed so far (0x346 menu, 0x2C9, 0x344,
   0x8F, 0xC2) is a sub screen that should own the display outright. */
#define APPFW_MAIN_SCREEN 0x343u

static int host_menu_visible(void)
{
    if (g_last_screen == 0u)
        return 0; /* nothing observed yet: keep the canvas up */
    return g_last_screen != APPFW_MAIN_SCREEN;
}

static void host_draw(PilotCanvas *c, int tick)
{
    char line[32];
    int x;

    pilot_canvas_clear_v1(c, 0xFF101418u);
    if (host_menu_visible()) {
        /* A menu screen owns the display; leave it alone. */
        pilot_canvas_commit_v1(c);
        return;
    }
    pilot_canvas_draw_grid_v1(c, 8, 9, 5, 0xFF203040u);

    /* The metering grid lives in CP RAM and is read through /dev/pcidma, which
       needs no CP-side patch.  Poll it every third frame: a frame is 11 KiB
       spread over three DMAs, cheap enough but not free. */
    if ((tick % 3) == 0) {
        static int logged;
        int ok = canvas_cp_read_opd(&g_opd_frame, SCOPE_GAMMA_REC709, 1);

        g_opd_ok = ok && g_opd_frame.valid;
        if (!logged) {
            logged = 1;
            hlog("HOST a7c2: opd read=%d valid=%d %ux%u",
                 ok, g_opd_ok, (unsigned)g_opd_frame.cols,
                 (unsigned)g_opd_frame.rows);
        }
    }

    switch (g_view % HOST_VIEW_COUNT) {
    case 0:
        if (g_opd_ok && g_opd_frame.cols > 0u) {
            unsigned cols = g_opd_frame.cols;
            int span = HOST_W / (int)cols;

            if (span < 2)
                span = 2;
            for (x = 0; x < (int)cols; ++x) {
                int y = g_opd_frame.y[0][x];
                int py = 24 + (255 - y) * (HOST_H - 52) / 255;

                pilot_canvas_fill_rect_v1(c, x * span, py, span, 2,
                                          0xFF40E0A0u,
                                          PILOT_CANVAS_BLEND_NONE);
            }
        } else {
            for (x = 0; x < HOST_W; ++x) {
                int phase = (x * 2 + tick * 6) % 240;
                int y = phase < 120 ? phase : 240 - phase;

                pilot_canvas_fill_rect_v1(c, x, 20 + y, 1, 2, 0xFF40E0A0u,
                                          PILOT_CANVAS_BLEND_NONE);
            }
        }
        break;
    case 1:
        for (x = 0; x < 32; ++x) {
            int h = ((x * 37 + tick * 3) % 90) + 10;

            pilot_canvas_fill_rect_v1(c, 8 + x * 18, HOST_H - 24 - h, 14, h,
                                      0xFF40A0E0u, PILOT_CANVAS_BLEND_NONE);
        }
        break;
    default:
        break; /* grid only */
    }

    /* OSD text needs no TextSprite: canvas_core draws it as RECT runs from its
       own 5x7 font, so it works on the A7 C II host widget unchanged.  The
       font covers 0-9 A B C D E F G H I L M N O P R S T U V W X Y m x . : - %
       only -- anything else advances the cursor and draws nothing. */
    snprintf(line, sizeof(line), "A7C2 %d %d", g_widget_x, g_widget_y);
    pilot_canvas_draw_osd_text_v1(c, 4, 4, 2, 0xFFFFFFFFu, 0x00000000u, line);
    snprintf(line, sizeof(line), "VIEW %d", g_view % HOST_VIEW_COUNT + 1);
    pilot_canvas_draw_osd_text_v1(c, HOST_W - 84, 4, 2, 0xFFFFE080u,
                                  0x00000000u, line);
    snprintf(line, sizeof(line), "TICK %d", tick & 0xFF);
    pilot_canvas_draw_osd_text_v1(c, 4, HOST_H - 20, 2, 0xFFC0D0E0u,
                                  0x00000000u, line);
    pilot_canvas_commit_v1(c);
}

/* The CP patch only proves a *write* channel; reading CP RAM goes through
   /dev/pcidma and needs nothing from the CP side.  What is missing on A7 C II
   is the OPD table address (A6700: 0x015B6398, stride 0x70).  Probe a few
   candidates here so the log says whether pcidma works at all and whether the
   A6700 address happens to be valid on this firmware. */
static void host_probe_cp(void)
{
    static const uint64_t k_addrs[] = {
        0x015B6398ull,             /* A6700 OPD table base */
        0x015B6398ull + 0x70ull,
        0x015B6398ull + 0xE0ull,
        0x015B6398ull + 0x150ull,
        0x017A0000ull,
        0x01900000ull,
    };
    unsigned char buf[0x70];
    unsigned int i;

    if (g_api == NULL || g_api->cp_mem == NULL || g_api->cp_mem->read == NULL) {
        hlog("HOST a7c2: cp_mem read unavailable");
        return;
    }
    for (i = 0; i < (unsigned int)(sizeof(k_addrs) / sizeof(k_addrs[0])); ++i) {
        PilotStatus st;

        memset(buf, 0, sizeof(buf));
        st = g_api->cp_mem->read(k_addrs[i], buf, sizeof(buf));
        if (st != PILOT_STATUS_OK) {
            hlog("HOST a7c2: cp read %#llx -> %d",
                 (unsigned long long)k_addrs[i], (int)st);
            continue;
        }
        hlog("HOST a7c2: cp read %#llx ok %02x %02x %02x %02x %02x %02x %02x %02x",
             (unsigned long long)k_addrs[i], buf[0], buf[1], buf[2], buf[3],
             buf[4], buf[5], buf[6], buf[7]);
    }
}

/* ---- touch drag ------------------------------------------------------- */

/* postMousePress/Move/Release are WidgetSystem methods; their GOT slots hold
   the libObj.so entry points, so replacing the slot intercepts every caller
   (the input pump included).  Press carries no coordinates: the last move
   event before it does, exactly like A6700's panel code assumes. */
#define APPFW_GOT_POST_MOUSE_MOVE    0x05BC21F8ull
#define APPFW_GOT_POST_MOUSE_PRESS   0x05BC2AC8ull
#define APPFW_GOT_POST_MOUSE_RELEASE 0x05BC20B8ull

typedef void (*fn_mouse_move)(void *self, uint8_t finger, int16_t x, int16_t y,
                              const void *msec);
typedef void (*fn_mouse_btn)(void *self, uint8_t finger, uint8_t btn,
                             const void *msec);
typedef void (*fn_set_local_pos)(void *self, const void *vector2);

static fn_mouse_move g_orig_mouse_move;
static fn_mouse_btn g_orig_mouse_press;
static fn_mouse_btn g_orig_mouse_release;
static fn_set_local_pos g_widget_set_local_pos;

static int g_touch_has_pos;
static int g_touch_x;
static int g_touch_y;
static int g_drag_active;
static int g_drag_dx;
static int g_drag_dy;

/* Double-tap detector: two presses within 350 ms and 24 px of each other. */
static int g_have_last_press;
static unsigned int g_last_press_ms;
static int g_last_press_x;
static int g_last_press_y;

static int host_read_mem(uintptr_t addr, void *dst, size_t len)
{
    if (g_api == NULL || g_api->memory_read == NULL)
        return -1;
    return (g_api->memory_read(addr, dst, len) == PILOT_STATUS_OK) ? 0 : -1;
}

static int host_patch_mem(uintptr_t addr, const void *src, size_t len)
{
    unsigned char old[64];

    if (g_api == NULL || g_api->memory_patch == NULL || len > sizeof(old))
        return -1;
    if (host_read_mem(addr, old, len) != 0)
        return -1;
    return (g_api->memory_patch(addr, old, src, len) == PILOT_STATUS_OK) ? 0 : -1;
}

static void host_place_widget(int x, int y)
{
    struct {
        float x;
        float y;
    } v;

    if (g_widget_set_local_pos == NULL)
        return;
    v.x = (float)x;
    v.y = (float)y;
    g_widget_set_local_pos((void *)g_host_widget, &v);
}

static void host_mouse_move(void *self, uint8_t finger, int16_t x, int16_t y,
                            const void *msec)
{
    g_touch_has_pos = 1;
    g_touch_x = (int)x;
    g_touch_y = (int)y;
    if (g_drag_active) {
        int nx = g_touch_x - g_drag_dx;
        int ny = g_touch_y - g_drag_dy;

        if (nx != g_widget_x || ny != g_widget_y) {
            g_widget_x = nx;
            g_widget_y = ny;
            host_place_widget(nx, ny);
        }
        return; /* the drag owns this finger */
    }
    if (g_orig_mouse_move)
        g_orig_mouse_move(self, finger, x, y, msec);
}

static void host_mouse_press(void *self, uint8_t finger, uint8_t btn,
                             const void *msec)
{
    unsigned int now = msec != NULL ? *(const unsigned int *)msec : 0u;
    int is_double = 0;

    if (g_have_last_press && (now - g_last_press_ms) < 350u) {
        int dx = g_touch_x - g_last_press_x;
        int dy = g_touch_y - g_last_press_y;

        if (dx > -24 && dx < 24 && dy > -24 && dy < 24)
            is_double = 1;
    }
    g_have_last_press = 1;
    g_last_press_ms = now;
    g_last_press_x = g_touch_x;
    g_last_press_y = g_touch_y;

    if (g_touch_has_pos &&
        g_touch_x >= g_widget_x && g_touch_x < g_widget_x + HOST_W &&
        g_touch_y >= g_widget_y && g_touch_y < g_widget_y + HOST_H) {
        if (is_double) {
            g_have_last_press = 0; /* require a fresh pair for the next cycle */
            g_view = (g_view + 1) % HOST_VIEW_COUNT;
            hlog("HOST a7c2: double tap -> view %d", g_view + 1);
            return;
        }
        if (g_widget_set_local_pos != NULL) {
            g_drag_active = 1;
            g_drag_dx = g_touch_x - g_widget_x;
            g_drag_dy = g_touch_y - g_widget_y;
            hlog("HOST a7c2: drag start %d,%d (widget %d,%d)",
                 g_touch_x, g_touch_y, g_widget_x, g_widget_y);
            return;
        }
    }
    if (g_orig_mouse_press)
        g_orig_mouse_press(self, finger, btn, msec);
}

static void host_mouse_release(void *self, uint8_t finger, uint8_t btn,
                               const void *msec)
{
    if (g_drag_active) {
        g_drag_active = 0;
        hlog("HOST a7c2: drag end %d,%d (widget %d,%d)",
             g_touch_x, g_touch_y, g_widget_x, g_widget_y);
        return;
    }
    if (g_orig_mouse_release)
        g_orig_mouse_release(self, finger, btn, msec);
}

static int host_hook_got(uintptr_t offset, void *hook)
{
    uintptr_t slot = g_host_base + offset;
    void *replacement = hook;

    if (host_patch_mem(slot, &replacement, sizeof(replacement)) != 0) {
        hlog("HOST a7c2: got patch %#llx failed", (unsigned long long)slot);
        return 0;
    }
    return 1;
}

static void host_install_touch_hook(void)
{
    union { void *p; fn_mouse_move f; } u_move;
    union { void *p; fn_mouse_btn f; } u_press;
    union { void *p; fn_mouse_btn f; } u_rel;
    union { void *p; fn_set_local_pos f; } u_pos;
    int ok = 0;

    /* The GOT slot holds the import stub, not the implementation, so chaining
       through its old value would jump into an unrelocated slot.  dlsym gives
       the real libObj.so entry point -- same approach as canvas_panel.c. */
    u_move.p = dlsym(RTLD_DEFAULT,
                     "_ZN2ux6wgtsys12WidgetSystem13postMouseMoveEhssNS_4core8MsecTimeE");
    u_press.p = dlsym(RTLD_DEFAULT,
                      "_ZN2ux6wgtsys12WidgetSystem14postMousePressEhhNS_4core8MsecTimeE");
    u_rel.p = dlsym(RTLD_DEFAULT,
                    "_ZN2ux6wgtsys12WidgetSystem16postMouseReleaseEhhNS_4core8MsecTimeE");
    /* Name length is 11 ("setLocalPos"), the 12 form does not resolve. */
    u_pos.p = dlsym(RTLD_DEFAULT,
                    "_ZN2ux6wgtsys6Widget11setLocalPosERKNS_4core7Vector2E");

    if (u_move.p == NULL || u_press.p == NULL || u_rel.p == NULL) {
        hlog("HOST a7c2: touch symbols missing move=%p press=%p rel=%p",
             u_move.p, u_press.p, u_rel.p);
        return;
    }
    g_orig_mouse_move = u_move.f;
    g_orig_mouse_press = u_press.f;
    g_orig_mouse_release = u_rel.f;
    g_widget_set_local_pos = u_pos.f;

    ok += host_hook_got(APPFW_GOT_POST_MOUSE_MOVE, (void *)host_mouse_move);
    ok += host_hook_got(APPFW_GOT_POST_MOUSE_PRESS, (void *)host_mouse_press);
    ok += host_hook_got(APPFW_GOT_POST_MOUSE_RELEASE, (void *)host_mouse_release);

    hlog("HOST a7c2: touch hook %d/3, setLocalPos=%p", ok,
         (void *)g_widget_set_local_pos);
}

/* ---- screen tracking -------------------------------------------------- */

/* presenter_controller_manager::open_screen(this, target, from, restore,
   bool, bool) is only reachable through its vtable slot at 0x05B0C498, so
   swapping that slot sees every screen switch.  The page-table getters tried
   first only fire while screens are being built, which is why they never
   reported a menu opening. */
#define APPFW_VT_OPEN_SCREEN_OFF 0x05B0C498ull

typedef void (*fn_open_screen)(void *self, const int *target, const int *from,
                               const int *restore, int a5, int a6);

static fn_open_screen g_orig_open_screen;

static int host_write_mem(uintptr_t addr, const void *src, size_t len)
{
    long page = sysconf(_SC_PAGESIZE);
    uintptr_t start;
    uintptr_t end;
    int st;

    if (page <= 0)
        return -1;
    start = addr & ~(uintptr_t)(page - 1);
    end = (addr + len + (uintptr_t)page - 1u) & ~(uintptr_t)(page - 1);
    if (mprotect((void *)start, (size_t)(end - start),
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return -1;
    memcpy((void *)addr, src, len);
    __builtin___clear_cache((char *)addr, (char *)addr + len);
    st = mprotect((void *)start, (size_t)(end - start), PROT_READ);
    return st == 0 ? 0 : -1;
}

static void host_open_screen(void *self, const int *target, const int *from,
                             const int *restore, int a5, int a6)
{
    if (target != NULL && (unsigned int)*target != g_last_screen) {
        g_last_screen = (unsigned int)*target;
        hlog("HOST a7c2: screen %#x active (from %#x)", (unsigned)*target,
             from != NULL ? (unsigned)*from : 0u);
    }
    g_orig_open_screen(self, target, from, restore, a5, a6);
}

static void host_install_screen_hooks(void)
{
    uintptr_t slot = g_host_base + APPFW_VT_OPEN_SCREEN_OFF;
    void *cur = NULL;
    void *replacement = (void *)host_open_screen;

    if (host_read_mem(slot, &cur, sizeof(cur)) != 0) {
        hlog("HOST a7c2: open_screen slot read failed");
        return;
    }
    g_orig_open_screen = (fn_open_screen)cur;
    if (host_write_mem(slot, &replacement, sizeof(replacement)) != 0) {
        hlog("HOST a7c2: open_screen slot patch failed");
        return;
    }
    hlog("HOST a7c2: open_screen hooked, original=%p", cur);
}

void pilot_canvas_a7c2_host_run(void)
{
    PilotCanvas *c;
    int tick = 0;

    hlog("HOST a7c2: start");
    host_probe_cp();

    if (!host_wait_ui())
        return;
    if (!host_create_layer())
        return;
    if (!host_create_widget())
        return;
    host_install_touch_hook();
    host_install_screen_hooks();

    c = pilot_canvas_create_v1(HOST_W, HOST_H, PILOT_CANVAS_BACKEND_RECT);
    if (c == NULL) {
        hlog("HOST a7c2: canvas create failed");
        return;
    }
    if (pilot_canvas_bind_widget_v1(c, g_host_widget) != PILOT_STATUS_OK) {
        hlog("HOST a7c2: bind widget failed");
        return;
    }
    hlog("HOST a7c2: canvas=%p bound, entering render loop", (void *)c);

    for (;;) {
        struct timespec ts = { 0, 33000000l };

        host_draw(c, tick++);
        nanosleep(&ts, NULL);
    }
}