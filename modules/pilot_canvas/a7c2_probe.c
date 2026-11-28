/*
 * a7c2_probe.c - minimal probe for pilot_canvas on ILCE-7CM2 (A7C II) 2.01
 *
 * Purpose: before porting the whole canvas engine, verify that the pieces we
 * need actually exist and behave on A7 C II:
 *
 *   1. core::ID::genID(const char *)          - build a layer id from a name
 *   2. WidgetSystem::sys_getInstance()        - widget system singleton
 *   3. WidgetSystem::createLayer(const ID &)  - create our own render layer
 *   4. Widget::setLayerID(const ID &)         - attach a widget to that layer
 *   5. Widget ctor / setSize / initSprite     - build a widget that owns sprite
 *   6. Sprite ctor / setRectColor / setRectPos / setRectSize
 *   7. Sprite::setCurRectNum - expected MISSING on A7 C II (that is why the
 *      RECT backend cannot work as-is on A6700's design)
 *
 * Everything is logged twice: to the module log and to the kernel console
 * (via /dev/kmsg), so the data survives a guest crash.
 *
 * This module is standalone and does not touch the camera UI at all.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <link.h>
#include <unistd.h>

#include "../../patch/pilot.h"

PILOT_DECLARE_MODULE_MODEL();

static const PilotApi *g_api;

/* appFw.so 的装载基址，用来读 .bss 里的引擎状态。 */
static uintptr_t g_appfw_base;

#define APPFW_BSS_FIRST_LAYER 0x5F66BF0ull

static int find_appfw(struct dl_phdr_info *info, size_t size, void *data)
{
    const char *name;
    const char *slash;
    (void)size;
    (void)data;

    name = info->dlpi_name ? info->dlpi_name : "";
    slash = strrchr(name, '/');
    if (strcmp(slash ? slash + 1 : name, "appFw.so") == 0) {
        g_appfw_base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* logging: module log + kernel console                                */
/* ------------------------------------------------------------------ */

static void console_emit(const char *line)
{
    static const char kmsg_prefix[] = "<6>pilot canvas probe: ";
    static const char con_prefix[] = "pilot canvas probe: ";
    const char *prefix;
    int fd;
    ssize_t ignored;

    fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        prefix = kmsg_prefix;
    } else {
        fd = open("/dev/console", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
        prefix = con_prefix;
    }
    if (fd < 0) {
        return;
    }
    ignored = write(fd, prefix, strlen(prefix));
    ignored = write(fd, line, strlen(line));
    ignored = write(fd, "\n", 1);
    (void)ignored;
    close(fd);
}

static void log_msg(const char *fmt, ...)
{
    char buf[320];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (g_api && g_api->mlog && g_api->mlog->print) {
        g_api->mlog->print(g_api->mlog, buf);
    }
    console_emit(buf);
}

static void *resolve(const char *name, int *missing)
{
    void *p = dlsym(RTLD_DEFAULT, name);

    log_msg("  %-64s %p", name, p);
    if (!p && missing) {
        ++*missing;
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* probe                                                               */
/* ------------------------------------------------------------------ */

typedef uint32_t (*fn_gen_id)(const char *name);
typedef void *(*fn_wgtsys)(void);
typedef void *(*fn_create_layer)(void *self, const void *id);
typedef void (*fn_layer_cfg)(void *self, int value);
typedef void *(*fn_widget_ctor)(void *self);
typedef void *(*fn_widget_set_size)(void *self, float w, float h);
typedef void *(*fn_widget_set_layer_id)(void *self, const void *id);
typedef void *(*fn_widget_init_sprite)(void *self, void *sprite, unsigned short count);
typedef void *(*fn_sprite_ctor)(void *self);
typedef void *(*fn_sprite_set_rect_color)(void *self, unsigned short idx, const void *color);
typedef void *(*fn_sprite_set_rect_pos)(void *self, unsigned short idx, const void *pos);
typedef void *(*fn_sprite_set_rect_size)(void *self, unsigned short idx, float w, float h);
typedef void *(*fn_sprite_set_cur_rect_num)(void *self, unsigned short num);

static void probe_symbols(void)
{
    int missing = 0;

    log_msg("== symbol probe ==");

    resolve("_ZN2ux4core2ID5genIDEPKc", &missing);
    resolve("_ZN8UXEngine6wgtsysEv", &missing);
    resolve("_ZN2ux6wgtsys12WidgetSystem9sys_getInstanceEv", &missing);
    resolve("_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE", &missing);
    resolve("_ZN2ux6wgtsys6WidgetC2Ev", &missing);
    resolve("_ZN2ux6wgtsys6Widget7setSizeEff", &missing);
    resolve("_ZN2ux6wgtsys6Widget10setLayerIDERKNS_4core2IDE", &missing);
    resolve("_ZN2ux6wgtsys6Widget10initSpriteERNS_6gfxeng6SpriteEt", &missing);
    resolve("_ZN2ux6gfxeng6SpriteC1Ev", &missing);
    resolve("_ZN2ux6gfxeng6Sprite12setRectColorEtRKN6stella6CColorE", &missing);
    resolve("_ZN2ux6gfxeng6Sprite10setRectPosEtRKNS_4core7Vector2E", &missing);
    resolve("_ZN2ux6gfxeng6Sprite11setRectSizeEtff", &missing);
    resolve("_ZN2ux6gfxeng6Sprite13setCurRectNumEt", &missing);

    log_msg("== symbol probe done, missing=%d ==", missing);
}

static void probe_layer_detail(void *layer, uint32_t id)
{
    void **vt;
    int i;
    fn_widget_ctor widget_ctor;
    static unsigned char widget_buf[8192] __attribute__((aligned(16)));

    vt = *(void ***)layer;
    if (vt == 0) {
        log_msg("layer detail: null vtable, stop");
        return;
    }

    log_msg("layer detail: id=0x%08x vtable=%p", id, (void *)vt);
    for (i = 0; i < 8; ++i)
        log_msg("  vt[%d] = %p", i, vt[i]);

    /* sub_17CCF6C 建完层立刻做这两步：vt[1](layer, 1) 与 vt[3](layer, 0) */
    if (vt[1] != 0) {
        fn_layer_cfg cfg = (fn_layer_cfg)(void *)vt[1];
        log_msg("layer detail: calling vt[1](layer, 1)");
        cfg(layer, 1);
        log_msg("layer detail: vt[1] ok");
    }
    if (vt[3] != 0) {
        fn_layer_cfg cfg = (fn_layer_cfg)(void *)vt[3];
        log_msg("layer detail: calling vt[3](layer, 0)");
        cfg(layer, 0);
        log_msg("layer detail: vt[3] ok");
    }

    /* Widget 构造：先只验证能不能建出来，不调任何虚方法。 */
    widget_ctor = (fn_widget_ctor)dlsym(RTLD_DEFAULT, "_ZN2ux6wgtsys6WidgetC2Ev");
    if (!widget_ctor) {
        log_msg("layer detail: Widget ctor missing");
        return;
    }
    memset(widget_buf, 0, sizeof(widget_buf));
    log_msg("layer detail: calling Widget ctor, buf=%p", (void *)widget_buf);
    widget_ctor(widget_buf);
    log_msg("layer detail: widget vptr=%p", *(void **)widget_buf);
}

static void probe_create_layer(void)
{
    fn_gen_id gen_id;
    fn_wgtsys get_wgtsys;
    fn_create_layer create_layer;
    uint32_t id;
    void *wgtsys;
    void *layer;

    gen_id = (fn_gen_id)dlsym(RTLD_DEFAULT, "_ZN2ux4core2ID5genIDEPKc");
    get_wgtsys = (fn_wgtsys)dlsym(RTLD_DEFAULT, "_ZN8UXEngine6wgtsysEv");
    create_layer = (fn_create_layer)dlsym(
        RTLD_DEFAULT, "_ZN2ux6wgtsys12WidgetSystem11createLayerERKNS_4core2IDE");
    if (!gen_id || !get_wgtsys || !create_layer) {
        log_msg("layer probe: missing callable, skip");
        return;
    }

    log_msg("layer probe: calling genID");
    id = gen_id("PILOT_PROBE");
    log_msg("layer probe: id=0x%08x", id);

    wgtsys = get_wgtsys();
    log_msg("layer probe: wgtsys=%p", wgtsys);
    if (!wgtsys) {
        return;
    }

    /* createLayer 会解引用只有 UI 起来后才存在的引擎状态，所以先等第一个
     * 全局层出现（sub_17CCF6C 在 UI 启动时写入 .bss + 0x5F66BF0）。 */
    if (g_appfw_base == 0u) {
        log_msg("layer probe: appFw base unknown, skip readiness wait");
    } else {
        int waited = 0;
        while (waited < 240) {
            uint64_t first = *(volatile uint64_t *)(uintptr_t)(
                g_appfw_base + APPFW_BSS_FIRST_LAYER);
            if (first != 0u) {
                log_msg("layer probe: ui ready after %d ms, first layer=%p",
                        waited * 250, (void *)(uintptr_t)first);
                break;
            }
            usleep(250000);
            ++waited;
        }
        if (waited >= 240) {
            log_msg("layer probe: ui never came up, aborting");
            return;
        }
    }

    log_msg("layer probe: calling createLayer");
    layer = create_layer(wgtsys, &id);
    log_msg("layer probe: layer=%p", layer);

    if (layer != 0) {
        probe_layer_detail(layer, id);
    }
}

static void *worker(void *arg)
{
    (void)arg;

    log_msg("pilot canvas probe a7c2 %s %s", __DATE__, __TIME__);
    log_msg("worker start");

    (void)dl_iterate_phdr(find_appfw, 0);
    log_msg("appFw base = %p", (void *)g_appfw_base);

    /* Give the UI time to come up before touching the widget system. */
    sleep(8);

    probe_symbols();
    probe_create_layer();

    log_msg("probe complete");

    for (;;) {
        sleep(30);
    }
    return 0;
}

__attribute__((visibility("default")))
PilotStatus pilot_module_init(const PilotApi *api)
{
    static int started;
    pthread_t tid;

    if (!api || api->abi_version != PILOT_ABI_VERSION ||
        api->struct_size < sizeof(PilotApi)) {
        return PILOT_STATUS_INVALID_ARGUMENT;
    }
    if (started) {
        return PILOT_STATUS_OK;
    }
    started = 1;
    g_api = api;

    log_msg("module init");

    if (pthread_create(&tid, 0, worker, 0) != 0) {
        return PILOT_STATUS_SYSTEM;
    }
    pthread_detach(tid);
    return PILOT_STATUS_OK;
}
