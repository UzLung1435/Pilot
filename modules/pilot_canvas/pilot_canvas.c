/* Pilot Canvas module entry point.
 *
 * Modular layout:
 *   canvas_core.c         - Native gfxeng/Sprite backend, batch/grid/OSD text
 *   canvas_text.c         - Independent MultiLineTextSprite slots + frame hook
 *   canvas_panel.c        - Reusable PilotPanel container, touch, DISP, config
 *   canvas_cp_sensors.c   - CP RAM OPD 12-bit grid, focal length, geometry
 *   canvas_scope_widget.c - Live View RGB Curve / Parade / Waveform widget
 *   a7c2_host.c           - A7 C II only: build our own layer and widget
 *
 * A6700 takes the widget that menu_ui hands over.  A7 C II has no such link
 * (the Live View text slot tables in .rodata are dead data there), so the
 * A7C2 build creates its own render layer through WidgetSystem::createLayer
 * and drives the canvas from there.
 */

#include "pilot_canvas_internal.h"

/* This Pilot tree refuses a module that does not export the tag of the body
 * loading it, so the tag must come from the same PILOT_MODEL_* macro as the
 * framework build. */
PILOT_DECLARE_MODULE_MODEL();

static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_started;

static void *worker(void *arg)
{
    (void)arg;

#ifdef PILOT_MODEL_A7C2
    /* No Live View widget to reuse: build our own layer/widget and render. */
    pilot_canvas_a7c2_host_run();
    return NULL;
#else
    canvas_scope_widget_init();
    canvas_config_load();
    (void)pilot_canvas_probe_v1();
    (void)canvas_install_touch_hook();
    (void)canvas_install_disp_hook();
    (void)canvas_install_frame_hook();

    /* Sample focal length at ~30 Hz (33ms, 1x 4B DMA) and live OPD 2D RGBY
     * scope at ~10 Hz (100ms, 3x 4KB DMA); flush debounced layout.cfg saves. */
    u32 tick = 0u;
    for (;;) {
        canvas_scope_widget_tick_label();
        if ((tick % 3u) == 0u)
            canvas_scope_widget_tick_opd();
        canvas_config_flush_if_dirty();
        ++tick;
        struct timespec ts = { 0, 33000000l };
        nanosleep(&ts, NULL);
    }
    return NULL;
#endif
}

__attribute__((visibility("default")))
PilotStatus pilot_module_init(const PilotApi *api)
{
    if (!api || api->abi_version != PILOT_ABI_VERSION ||
        api->struct_size < sizeof(PilotApi))
        return PILOT_STATUS_INVALID_ARGUMENT;

    pthread_mutex_lock(&g_init_lock);
    if (g_started) {
        pthread_mutex_unlock(&g_init_lock);
        return PILOT_STATUS_OK;
    }
    g_api = api;
    g_started = 1;
    pthread_mutex_unlock(&g_init_lock);

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&tid, &attr, worker, NULL);
    pthread_attr_destroy(&attr);
    return rc == 0 ? PILOT_STATUS_OK : PILOT_STATUS_SYSTEM;
}
