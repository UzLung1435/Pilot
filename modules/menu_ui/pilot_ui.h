#ifndef PILOT_UI_H
#define PILOT_UI_H

#include "../../patch/pilot.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PILOT_UI_PAGE_DETAIL = 0,
    PILOT_UI_PAGE_LIST = 1
} PilotUiPageKind;

typedef enum {
    PILOT_UI_COMP_TEXT = 1,
    PILOT_UI_COMP_SUBMENU = 2,
    PILOT_UI_COMP_TOGGLE = 3
} PilotUiComponentKind;

typedef struct PilotUiPage PilotUiPage;

typedef struct PilotUiComponent {
    PilotUiComponentKind kind;
    const char *label;
    const char *value;
    const PilotUiPage *target_page;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} PilotUiComponent;

struct PilotUiPage {
    PilotUiPageKind kind;
    const char *title;
    const char *content;
    const char *summary;
    const PilotUiComponent *components;
    size_t component_count;
    void (*on_select)(const PilotUiPage *page, size_t index, void *user);
    const PilotUiPage *(*build_dynamic)(void *user);
    void *user_data;
};

/*
 * Plain-text Live View overlay service exported by pilot_menu_ui.so.
 * The provider validates an AP view_string widget and passes the message to
 * its native setString(const char*) path. Native parameters are restored
 * after each text render; the message does not enable a background control.
 * The default profile is small, left-aligned text near the bottom left of
 * Live View. LCD and EVF use their own native display coordinates.
 *
 * duration_ms == 0 keeps the text visible until pilot_ui_clear_text().
 * Text is UTF-8 and limited to 159 bytes.
 * The service has one shared message; each show call replaces its contents.
 * The provider is resolved at call time, so modules do not need a link-time
 * dependency on pilot_menu_ui.so. Add pilot_menu_ui.so to modules.txt and
 * load it before callers. Missing provider returns PILOT_STATUS_NOT_FOUND;
 * an unavailable Live View hook returns PILOT_STATUS_DISABLED.
 * Example: pilot_ui_show_text("Sensor mode ready", 3000u);
 */
#define PILOT_UI_TEXT_SHOW_SYMBOL "pilot_ui_text_show_v1"
#define PILOT_UI_TEXT_CLEAR_SYMBOL "pilot_ui_text_clear_v1"

typedef PilotStatus (*PilotUiTextShowFn)(const char *text,
                                         uint32_t duration_ms);
typedef PilotStatus (*PilotUiTextClearFn)(void);

static inline PilotStatus pilot_ui_show_text(const char *text,
                                             uint32_t duration_ms)
{
    if (!text || !text[0])
        return PILOT_STATUS_INVALID_ARGUMENT;

    union {
        void *object;
        PilotUiTextShowFn function;
    } resolved;
    resolved.object = dlsym(RTLD_DEFAULT, PILOT_UI_TEXT_SHOW_SYMBOL);
    if (!resolved.object)
        return PILOT_STATUS_NOT_FOUND;
    return resolved.function(text, duration_ms);
}

static inline PilotStatus pilot_ui_clear_text(void)
{
    union {
        void *object;
        PilotUiTextClearFn function;
    } resolved;
    resolved.object = dlsym(RTLD_DEFAULT, PILOT_UI_TEXT_CLEAR_SYMBOL);
    if (!resolved.object)
        return PILOT_STATUS_NOT_FOUND;
    return resolved.function();
}

/*
 * Declarative per-module option menu API exported by pilot_menu_ui.so.
 * Modules register an array of PilotUiOption entries at init time. When the
 * user selects that module in the Pilot menu, pilot_menu_ui opens 0x1C3
 * directly into an interactive option list (with the module's Enable toggle
 * as item 0, followed by the module's options and a Back item).
 * The caller owns the options array (static storage) for the module lifetime.
 */
typedef enum {
    PILOT_UI_OPT_BOOL = 0,
    PILOT_UI_OPT_ENUM = 1,
    PILOT_UI_OPT_ACTION = 2
} PilotUiOptionKind;

typedef struct PilotUiOption {
    const char *label;
    PilotUiOptionKind kind;
    int32_t value;
    const char *const *choices;
    size_t choice_count;
    const char *action_text;
    void (*on_change)(struct PilotUiOption *opt, void *user);
    void *user;
} PilotUiOption;

#define PILOT_UI_REGISTER_OPTIONS_SYMBOL "pilot_ui_register_options_v1"

typedef PilotStatus (*PilotUiRegisterOptionsFn)(const char *module_name,
                                                PilotUiOption *options,
                                                size_t option_count);

static inline PilotStatus pilot_ui_register_options(const char *module_name,
                                                    PilotUiOption *options,
                                                    size_t option_count)
{
    if (!module_name || !module_name[0] || (!options && option_count))
        return PILOT_STATUS_INVALID_ARGUMENT;

    union {
        void *object;
        PilotUiRegisterOptionsFn function;
    } resolved;
    resolved.object = dlsym(RTLD_DEFAULT, PILOT_UI_REGISTER_OPTIONS_SYMBOL);
    if (!resolved.object)
        return PILOT_STATUS_NOT_FOUND;
    return resolved.function(module_name, options, option_count);
}

/*
 * Native modal Processing Dialog (screen 0x260 / scr7187_0004) exported by
 * pilot_menu_ui.so. Shows a dimmed backdrop, native dialog frame, custom
 * message (UTF-8, max 159 bytes), and centered animated processing spinner
 * while a module performs a short hot-switch transition.
 */
#define PILOT_UI_SHOW_BUSY_SYMBOL "pilot_ui_show_busy_v1"
#define PILOT_UI_HIDE_BUSY_SYMBOL "pilot_ui_hide_busy_v1"

typedef PilotStatus (*PilotUiShowBusyFn)(const char *text);
typedef PilotStatus (*PilotUiHideBusyFn)(void);

static inline PilotStatus pilot_ui_show_busy(const char *text)
{
    if (!text || !text[0])
        return PILOT_STATUS_INVALID_ARGUMENT;

    union {
        void *object;
        PilotUiShowBusyFn function;
    } resolved;
    resolved.object = dlsym(RTLD_DEFAULT, PILOT_UI_SHOW_BUSY_SYMBOL);
    if (!resolved.object)
        return PILOT_STATUS_NOT_FOUND;
    return resolved.function(text);
}

static inline PilotStatus pilot_ui_hide_busy(void)
{
    union {
        void *object;
        PilotUiHideBusyFn function;
    } resolved;
    resolved.object = dlsym(RTLD_DEFAULT, PILOT_UI_HIDE_BUSY_SYMBOL);
    if (!resolved.object)
        return PILOT_STATUS_NOT_FOUND;
    return resolved.function();
}

/*
 * Dynamic native UI string resource (RID) override API exported by
 * pilot_menu_ui.so. Replaces TextSprite::setString(uint32_t rid) for the
 * given Sony RID with custom UTF-8 text (max 63 bytes). Pass text == NULL
 * or text[0] == '\0' to clear the override and restore the stock string.
 */
#define PILOT_UI_SET_RID_OVERRIDE_SYMBOL "pilot_ui_set_rid_override_v1"

typedef PilotStatus (*PilotUiSetRidOverrideFn)(uint32_t rid, const char *text);

static inline PilotStatus pilot_ui_set_rid_override(uint32_t rid,
                                                    const char *text)
{
    if (rid == 0u)
        return PILOT_STATUS_INVALID_ARGUMENT;

    union {
        void *object;
        PilotUiSetRidOverrideFn function;
    } resolved;
    resolved.object = dlsym(RTLD_DEFAULT, PILOT_UI_SET_RID_OVERRIDE_SYMBOL);
    if (!resolved.object)
        return PILOT_STATUS_NOT_FOUND;
    return resolved.function(rid, text);
}

#ifdef __cplusplus
}
#endif

#endif
