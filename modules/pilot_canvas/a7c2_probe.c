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
#include <unistd.h>

#include "../../patch/pilot.h"

PILOT_DECLARE_MODULE_MODEL();

static const PilotApi *g_api;

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
typedef void *(*fn_create_layer)(const void *id);
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

    log_msg("layer probe: calling createLayer");
    layer = create_layer(&id);
    log_msg("layer probe: layer=%p", layer);
}

static void *worker(void *arg)
{
    (void)arg;

    log_msg("pilot canvas probe a7c2 %s %s", __DATE__, __TIME__);
    log_msg("worker start");

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
