#ifndef APPFW_MENU_EXAMPLE_H
#define APPFW_MENU_EXAMPLE_H
#include "menu_api.h"

enum menu_example_id {
    MENU_EXAMPLE_GROUP=1, MENU_EXAMPLE_PAGE, MENU_EXAMPLE_BOOL,
    MENU_EXAMPLE_ENUM, MENU_EXAMPLE_NUMERIC, MENU_EXAMPLE_READONLY,
    MENU_EXAMPLE_MORE, MENU_EXAMPLE_ACTION, MENU_EXAMPLE_CONDITIONAL,
    MENU_EXAMPLE_VISIBILITY, MENU_EXAMPLE_DISABLED,
    MENU_EXAMPLE_SAVE, MENU_EXAMPLE_LOAD, MENU_EXAMPLE_ACTION_COUNT, MENU_EXAMPLE_STORAGE_STATE
};

/* Original UI serialization is supplied by the native backend, never by this example. */
menu_api_status menu_example_initialize(void);
/* Native entry opens this logical container directly; zero keeps the API root. */
uint32_t menu_example_entry_id(void);
menu_api_status menu_example_model_enter(menu_api **model);
menu_api_status menu_example_model_leave(void);
menu_api_status menu_example_children(uint32_t parent, menu_api_snapshot *snapshot);
menu_api_status menu_example_definitions(uint32_t parent, menu_api_info *items, size_t capacity, size_t *count);
/* One native C/P generation pins every current definition until both unbind. */
menu_api_status menu_example_consumer_acquire(void);
menu_api_status menu_example_consumer_release(void);
menu_api_status menu_example_describe(uint32_t id, menu_api_info *info);
menu_api_status menu_example_value(uint32_t id, menu_api_result *result);
menu_api_status menu_example_invoke(uint32_t id, menu_api_result *result);
/* Editors hold a validated preview; MENU cancel never invokes the setter. */
menu_api_status menu_example_edit_begin(uint32_t id, menu_api_result *preview);
menu_api_status menu_example_edit_move(int direction, menu_api_result *preview);
menu_api_status menu_example_edit_commit(menu_api_result *result);
menu_api_status menu_example_edit_cancel(void);
uint32_t menu_example_action_count(void);
void menu_example_storage_counts(uint32_t *saves, uint32_t *reads);
#endif
