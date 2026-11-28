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
#include <time.h>

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

static void host_draw(PilotCanvas *c, int tick)
{
    int x;

    pilot_canvas_clear_v1(c, 0xFF101418u);
    pilot_canvas_draw_grid_v1(c, 8, 9, 5, 0xFF203040u);

    for (x = 0; x < HOST_W; ++x) {
        int phase = (x * 2 + tick * 6) % 240;
        int y = phase < 120 ? phase : 240 - phase;

        pilot_canvas_fill_rect_v1(c, x, 20 + y, 1, 2, 0xFF40E0A0u,
                                  PILOT_CANVAS_BLEND_NONE);
    }
    pilot_canvas_commit_v1(c);
}

void pilot_canvas_a7c2_host_run(void)
{
    PilotCanvas *c;
    int tick = 0;

    hlog("HOST a7c2: start");

    if (!host_wait_ui())
        return;
    if (!host_create_layer())
        return;
    if (!host_create_widget())
        return;

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
