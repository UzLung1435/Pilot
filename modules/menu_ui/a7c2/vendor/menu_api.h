#ifndef APPFW_MENU_API_H
#define APPFW_MENU_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Owned semantic model only; this header declares no native backend support. */
#define MENU_API_NODE_LIMIT 64u
#define MENU_API_OPTION_LIMIT 20u
#define MENU_API_VISIBLE_LIMIT 20u
#define MENU_API_LABEL_MAX 127u
#define MENU_API_VALUE_MAX 255u
#define MENU_API_NO_SELECTION SIZE_MAX
#define MENU_API_ROOT UINT64_C(0)

typedef uint64_t menu_api_handle;

typedef enum {
    MENU_API_OK = 0,
    MENU_API_E_ARGUMENT,
    MENU_API_E_CONTEXT,
    MENU_API_E_BUSY,
    MENU_API_E_STALE,
    MENU_API_E_NOT_FOUND,
    MENU_API_E_TYPE,
    MENU_API_E_DUPLICATE,
    MENU_API_E_LIMIT,
    MENU_API_E_UTF8,
    MENU_API_E_RANGE,
    MENU_API_E_DISABLED,
    MENU_API_E_HIDDEN,
    MENU_API_E_CALLBACK,
    MENU_API_E_READBACK,
    MENU_API_E_STATE,
    MENU_API_E_ROLLBACK
} menu_api_status;

typedef enum {
    MENU_API_GROUP = 1,
    MENU_API_PAGE,
    MENU_API_SUBMENU,
    MENU_API_ACTION,
    MENU_API_BOOL,
    MENU_API_ENUM,
    MENU_API_NUMERIC,
    MENU_API_READONLY
} menu_api_kind;

typedef struct {
    int64_t number; /* bool 0/1, enum option value, or numeric value */
    char text[MENU_API_VALUE_MAX + 1u]; /* scalar display text, or readonly getter */
} menu_api_value;

/* Zero is success; every other callback code is preserved verbatim. */
typedef int (*menu_api_get_fn)(void *userdata, menu_api_value *value);
typedef int (*menu_api_set_fn)(void *userdata, int64_t number);
typedef int (*menu_api_action_fn)(void *userdata);
typedef int (*menu_api_condition_fn)(void *userdata, bool *value);
/* Optional scalar display text; capacity is bytes, including the required NUL.
 * Output must be UTF-8, may be empty, and must fit within capacity (256 bytes).
 * Locale selection belongs to userdata. The callback must not retain out. */
typedef int (*menu_api_format_fn)(void *userdata, int64_t number,
                                  char *out, size_t capacity);

typedef struct {
    int64_t value;
    const char *label;
    bool enabled;
} menu_api_option_def;

typedef struct {
    uint32_t id; /* nonzero, unique within this registry */
    menu_api_handle parent;
    menu_api_kind kind;
    const char *label;
    void *userdata; /* borrowed until successful unregister */
    menu_api_get_fn get;
    menu_api_set_fn set;
    menu_api_action_fn action;
    menu_api_condition_fn visible;
    menu_api_condition_fn enabled;
    int64_t initial; /* owned scalars only; ignored for bound scalars */
    int64_t minimum, maximum, step; /* numeric only; step must be positive */
    const menu_api_option_def *options; /* copied during enum registration */
    size_t option_count;
    menu_api_format_fn format; /* optional, bool/enum/numeric only */
} menu_api_def;

typedef struct {
    menu_api_handle handle, parent;
    uint32_t id;
    menu_api_kind kind;
    char label[MENU_API_LABEL_MAX + 1u];
    int64_t minimum, maximum, step;
    size_t option_count;
    bool bound;
} menu_api_info;

typedef struct {
    int64_t value;
    char label[MENU_API_LABEL_MAX + 1u];
    bool enabled;
} menu_api_option;

typedef struct {
    int callback_code;
    bool has_value; /* last accepted model observation exists */
    bool fresh; /* this operation read/committed it successfully */
    menu_api_value value;
} menu_api_result;

typedef struct {
    menu_api_info info;
    bool enabled;
    menu_api_value value; /* empty for a container or action */
} menu_api_row;

typedef struct {
    menu_api_handle container;
    size_t total_children, count, enabled_count;
    size_t selected_index; /* only set for the active owned container */
    int callback_code;
    menu_api_row rows[MENU_API_VISIBLE_LIMIT];
} menu_api_snapshot;

typedef struct {
    bool active;
    size_t depth;
    menu_api_handle container, selected;
    size_t selected_index;
} menu_api_navigation;

/* Internal storage is public only to permit static allocation without malloc.
 * Initialize exactly once with {0}; do not inspect, copy or edit these fields.
 * Handles belong to one registry lifetime and must not cross contexts. */
typedef struct {
    menu_api_info info;
    menu_api_option options[MENU_API_OPTION_LIMIT];
    menu_api_value cached;
    void *userdata;
    menu_api_get_fn get;
    menu_api_set_fn set;
    menu_api_action_fn action;
    menu_api_condition_fn visible, enabled;
    menu_api_format_fn format;
    uint64_t order;
    uint32_t generation, consumer_pins;
    bool used, cached_valid;
} menu_api_slot;

typedef struct {
    menu_api_handle container, selected;
    size_t selected_index;
} menu_api_frame;

typedef struct {
    menu_api_slot slots[MENU_API_NODE_LIMIT];
    menu_api_frame frames[MENU_API_NODE_LIMIT];
    uint64_t order;
    size_t depth;
    bool entered, busy;
} menu_api;

/* Caller serialization is mandatory: these functions do not identify a thread,
 * provide a lock, or dispatch to Sony UI. The backend must enter on its UI context.
 * Leave retains registrations/navigation. Nested enter and callback leave fail. */
menu_api_status menu_api_enter(menu_api *api);
menu_api_status menu_api_leave(menu_api *api);

/* Groups attach to root/groups; pages to groups; submenus/items to pages/submenus.
 * Parents are immutable, so registration cannot create cycles. Labels/options are
 * copied. Callbacks/userdata remain borrowed. Scalar get/set must be both present
 * or both absent; actions and readonly text require their respective callback.
 * Format is rejected for containers/actions/readonly. No callbacks execute at
 * registration: an owned scalar's initial cache uses default text until its
 * first successful read/write; peek reports that cache with fresh=false. */
menu_api_status menu_api_register(menu_api *api, const menu_api_def *def,
                                  menu_api_handle *handle);
/* Leaf-only. Refuses nodes on the owned active path or displayed by any frame. */
menu_api_status menu_api_unregister(menu_api *api, menu_api_handle handle);
/* Native consumers pin handles before publishing references and unpin only after
 * actual native destruction/unbind. Pins also prohibit unregister outside owned navigation. */
menu_api_status menu_api_pin(menu_api *api, menu_api_handle handle);
menu_api_status menu_api_unpin(menu_api *api, menu_api_handle handle);

/* These four queries and peek/navigation may run inside callbacks. All other
 * operations reject recursion. Queries return copies, never slot pointers. */
menu_api_status menu_api_describe(const menu_api *api, menu_api_handle handle,
                                  menu_api_info *info);
menu_api_status menu_api_find(const menu_api *api, uint32_t id,
                              menu_api_handle *handle);
/* Registration order. NULL/0 counts only; undersized output is not partly filled. */
menu_api_status menu_api_children(const menu_api *api, menu_api_handle parent,
                                  menu_api_handle *handles, size_t capacity,
                                  size_t *count);
menu_api_status menu_api_option_at(const menu_api *api, menu_api_handle handle,
                                   size_t index, menu_api_option *option);
menu_api_status menu_api_peek(const menu_api *api, menu_api_handle handle,
                              menu_api_result *result);

/* Read invokes a bound getter and any scalar formatter, including owned values;
 * peek reports the last accepted observation only.
 * Once a value handle is resolved, a failed operation carries its cached
 * observation (if any), fresh=false, and the raw failing callback code.
 * A successful bound set must read back exactly
 * the requested value before the cache changes. Setters must be atomic on error;
 * failed readback may mean external state changed: no rollback is claimed.
 * Step reads before calculating and again after a bound write; conditions can be
 * rechecked before writing. Getter/condition callbacks must permit these reads.
 * Result/snapshot outputs are cleared even on context/busy rejection.
 * Readonly getters write text with a NUL within 256 bytes. Scalar getter text is
 * ignored. Without format, numeric text is decimal, bool text is Off/On, and
 * enum text is its label. Format receives the validated exact scalar number;
 * its NUL/UTF-8 output is validated before cache acceptance. Nonzero codes yield
 * E_CALLBACK with the raw code. Owned writes format before changing the value;
 * bound formatting/readback failure can leave external state changed. Format
 * has the same borrowed lifetime, serialization and recursion rules as get/set. */
menu_api_status menu_api_read(menu_api *api, menu_api_handle handle,
                              menu_api_result *result);
menu_api_status menu_api_set(menu_api *api, menu_api_handle handle, int64_t value,
                             menu_api_result *result);
menu_api_status menu_api_step(menu_api *api, menu_api_handle handle, int direction,
                              menu_api_result *result);
menu_api_status menu_api_invoke(menu_api *api, menu_api_handle handle,
                                menu_api_result *result);

/* Conditions inherit visibility/enabled from ancestors. Hidden rows are omitted;
 * disabled rows remain visible. A failing snapshot returns zero rows and the raw
 * callback code. More than 20 visible children is E_LIMIT, never truncation.
 * Successful getter observations can remain cached if a later row fails.
 * Snapshot reconciles focus only when container is the current owned frame. */
menu_api_status menu_api_snapshot_get(menu_api *api, menu_api_handle container,
                                      menu_api_snapshot *snapshot);

/* Calling activate explicitly opts into OWNED MODEL navigation. It does not open
 * a Sony screen. Activate while already active returns E_BUSY.
 * Move accepts +/-1, skips disabled rows and refuses end overflow.
 * Open pushes the selected container; back restores/reconciles its parent frame.
 * Back at the activation boundary is E_STATE, requiring explicit deactivate.
 * Deactivate clears every frame; no native exit callback/routing is promised.
 * Navigation_get returns the last accepted focus without invoking conditions;
 * snapshot_get/move/open/back are the explicit reconciliation points. */
menu_api_status menu_api_activate(menu_api *api, menu_api_handle container,
                                  menu_api_snapshot *snapshot);
menu_api_status menu_api_move(menu_api *api, int direction,
                              menu_api_snapshot *snapshot);
menu_api_status menu_api_open(menu_api *api, menu_api_snapshot *snapshot);
menu_api_status menu_api_back(menu_api *api, menu_api_snapshot *snapshot);
menu_api_status menu_api_deactivate(menu_api *api);
menu_api_status menu_api_navigation_get(const menu_api *api,
                                        menu_api_navigation *navigation);

#define MENU_API_STORAGE_SCHEMA 1u

/* Logical IDs and scalar values only: no handles, pointers, text or Sony IDs.
 * kind is a uint32_t representation of menu_api_kind, not an enum ABI field.
 * This is a bounded in-memory image, NOT a portable on-disk struct format.
 * The caller owns serialization, padding, endian, durability and business policy. */
typedef struct {
    uint32_t id, kind;
    int64_t number;
} menu_api_storage_record;

typedef struct {
    uint32_t schema, count;
    menu_api_storage_record records[MENU_API_NODE_LIMIT];
} menu_api_storage_image;

/* Zero means success; all other codes are retained verbatim. Load must fill an
 * actual image within this fixed capacity and set schema/count explicitly.
 * Neither callback may retain its image pointer or reenter a mutating/read API.
 * Metadata/peek/navigation queries are permitted as in core binding callbacks. */
typedef int (*menu_api_storage_save_fn)(void *userdata,
                                       const menu_api_storage_image *image);
typedef int (*menu_api_storage_load_fn)(void *userdata,
                                       menu_api_storage_image *image);

typedef struct {
    void *userdata;
    menu_api_storage_save_fn save;
    menu_api_storage_load_fn load;
} menu_api_storage_backend;

typedef struct {
    menu_api_status primary_status; /* original failure, even on E_ROLLBACK */
    int callback_code; /* raw first failing storage/binding/condition code */
    uint32_t failed_id; /* zero for backend/image-wide failures */
    uint32_t applied_count; /* writes verified before failure, not final state */
    uint32_t rolled_back_count; /* verified restores; can include failed write */
    menu_api_status rollback_status; /* OK if no rollback failed/was needed */
    int rollback_callback_code; /* first failing rollback callback code */
    uint32_t rollback_failed_id;
    /* Cache observations for the corresponding failed IDs, NOT a promise about
     * external state after failed readback/rollback. fresh is false on failure.
     * Other records' last accepted observations remain available through peek. */
    menu_api_result last_accepted, rollback_last_accepted;
} menu_api_storage_result;

/* Caller serialization/UI context rules still apply. image is caller-owned
 * workspace; all inputs/outputs must be distinct and must not alias the registry.
 * Save reads every registered scalar through the core getter path (including
 * hidden/disabled scalars), calls save, then load, and compares the full record
 * set regardless of order. Both callbacks are required for save. A failed save
 * does not roll back backend storage; its atomicity belongs to backend policy.
 *
 * Load requires load only. It validates schema, bounded count, unique known IDs,
 * exact kinds and the COMPLETE registered scalar set, target grids/options and
 * current conditions before any setter. All old actual values are read and
 * validated for rollback first. Apply and reverse-order rollback use core
 * setters plus readback and condition checks. A failure restores all attempted
 * records, including the current record whose setter/readback may have failed.
 * Setters retain the core atomic-on-error contract. Arbitrary external bindings
 * are not transactional; conditions or rollback errors can prevent restoration.
 * E_ROLLBACK explicitly reports this partial/uncertain outcome while retaining
 * primary_status/callback_code and both failed IDs. Caches change only after
 * accepted getter observations or verified writes, never to an unread request.
 *
 * Containers/actions/readonly values are excluded. Empty images are accepted
 * only if no scalars are registered; callbacks still run. Failure outputs are
 * cleared even on busy/context rejection. No NVRAM/filesystem backend is built in. */
menu_api_status menu_api_storage_save(menu_api *api,
                                      const menu_api_storage_backend *backend,
                                      menu_api_storage_image *image,
                                      menu_api_storage_result *result);
menu_api_status menu_api_storage_load(menu_api *api,
                                      const menu_api_storage_backend *backend,
                                      menu_api_storage_image *image,
                                      menu_api_storage_result *result);

#endif
