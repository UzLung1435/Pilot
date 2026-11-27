#ifndef PILOT_CANVAS_LOCALIZATION_H
#define PILOT_CANVAS_LOCALIZATION_H

#include <dlfcn.h>

#include "pilot_ui.h"

#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif

/*
 * The MenuUI module in this Pilot tree does not advertise a language query, so
 * the canvas asks for the symbol itself and keeps the English labels when the
 * answer is unavailable.  The indices must stay 0 = English, 1 = Simplified
 * Chinese to match the pilot_ui_get_language_v1 contract.
 */
#define PILOT_CANVAS_LANG_EN 0
#define PILOT_CANVAS_LANG_ZH 1

typedef struct PilotCanvasStrings {
    const char *opt_scope_mode;
    const char *opt_false_color;
    const char *opt_gamma_curve;
    const char *opt_wb_calib;
    const char *opt_disp_page;
    const char *opt_lock_pos;
} PilotCanvasStrings;

static const PilotCanvasStrings k_canvas_strings[2] = {
    [PILOT_CANVAS_LANG_EN] = {
        .opt_scope_mode  = "Scope Mode",
        .opt_false_color = "False Color",
        .opt_gamma_curve = "Gamma Curve",
        .opt_wb_calib    = "WB Calib",
        .opt_disp_page   = "DISP Page",
        .opt_lock_pos    = "Lock Pos",
    },
    [PILOT_CANVAS_LANG_ZH] = {
        .opt_scope_mode  = "示波器模式",
        .opt_false_color = "伪色彩",
        .opt_gamma_curve = "伽马曲线",
        .opt_wb_calib    = "白平衡校准",
        .opt_disp_page   = "DISP 页面",
        .opt_lock_pos    = "锁定位置",
    },
};

typedef int (*PilotCanvasGetLangFn)(void);

/* Resolve the language once per call; only the option labels use it. */
static inline int canvas_ui_language(void)
{
    union {
        void *object;
        PilotCanvasGetLangFn function;
    } resolved;
    resolved.object = dlsym(RTLD_DEFAULT, "pilot_ui_get_language_v1");
    if (!resolved.object)
        return PILOT_CANVAS_LANG_EN;
    return resolved.function() == PILOT_CANVAS_LANG_ZH ? PILOT_CANVAS_LANG_ZH
                                                       : PILOT_CANVAS_LANG_EN;
}

static inline const PilotCanvasStrings *canvas_ui_str(void)
{
    return &k_canvas_strings[canvas_ui_language()];
}

#endif
