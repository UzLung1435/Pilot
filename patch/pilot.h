#ifndef PILOT_H
#define PILOT_H

#include <stddef.h>
#include <stdint.h>

/* Every build has one model identity; see patch/pilot_model.h and models.json. */
#include "pilot_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PILOT_ABI_VERSION 2u
#define PILOT_MODULE_INIT_SYMBOL "pilot_module_init"
#define PILOT_MODULE_FILE_TRANSFER_MAX (1024u * 1024u)
#define PILOT_CP_MEM_TRANSFER_MAX 4096u

/* Card-side module manifest inside /pilot/.  The camera Fsys validates the
 * extension of every Fsys_Open() call and refuses .json with status
 * 0x80000041 (invalid argument) while accepting .txt and .so, so the manifest
 * keeps JSON content but must not use a .json name.  Measured on an
 * ILCE-7CM2 (a7c2) body; see docs/USAGE.md. */
#define PILOT_MANIFEST_NAME "modules.txt"

/* Optional per-module configuration under /pilot/modules/<stem>/.  Same Fsys
 * extension restriction as PILOT_MANIFEST_NAME: the content stays JSON, only
 * the name is forced onto an extension the camera accepts. */
#define PILOT_MODULE_CONFIG_NAME "config.txt"

typedef int32_t PilotStatus;

enum {
    PILOT_STATUS_OK = 0,
    PILOT_STATUS_DISABLED = -1,
    PILOT_STATUS_INVALID_ARGUMENT = -2,
    PILOT_STATUS_RANGE = -3,
    PILOT_STATUS_CONFLICT = -4,
    PILOT_STATUS_IO = -5,
    PILOT_STATUS_SYSTEM = -6,
    PILOT_STATUS_UNSUPPORTED = -7,
    PILOT_STATUS_NOT_FOUND = -8
};

enum {
    PILOT_FEATURE_LOG = 1u << 0,
    PILOT_FEATURE_TIME_UTC = 1u << 1,
    PILOT_FEATURE_APPFW_MEMORY = 1u << 2,
    PILOT_FEATURE_MODULE_DIR = 1u << 3,
    PILOT_FEATURE_MODULE_FILES = 1u << 4,
    PILOT_FEATURE_CP_MEMORY = 1u << 5
};

typedef struct PilotFileApi PilotFileApi;
typedef struct PilotLogApi PilotLogApi;
typedef struct PilotModuleLogApi PilotModuleLogApi;
typedef struct PilotCpMemApi PilotCpMemApi;

struct PilotFileApi {
    uint32_t struct_size;
    void *context;
    PilotStatus (*read)(const PilotFileApi *file,
                        const char *file_name,
                        char *buffer,
                        size_t capacity);
    PilotStatus (*write)(const PilotFileApi *file,
                         const char *file_name,
                         const char *text);
    PilotStatus (*append)(const PilotFileApi *file,
                          const char *file_name,
                          const char *text);
    PilotStatus (*read_bytes)(const PilotFileApi *file,
                              const char *file_name,
                              void *buffer,
                              size_t capacity,
                              size_t *length_out);
    PilotStatus (*write_bytes)(const PilotFileApi *file,
                               const char *file_name,
                               const void *data,
                               size_t length);
    PilotStatus (*append_bytes)(const PilotFileApi *file,
                                const char *file_name,
                                const void *data,
                                size_t length);
};

struct PilotLogApi {
    PilotStatus (*print)(const char *message);
};

struct PilotModuleLogApi {
    void *context;
    PilotStatus (*print)(const PilotModuleLogApi *log,
                         const char *message);
};

struct PilotCpMemApi {
    uint32_t struct_size;
    uint32_t reserved;
    PilotStatus (*read)(uint64_t cp_address, void *output, size_t length);
    PilotStatus (*write)(uint64_t cp_address, const void *input, size_t length);
    PilotStatus (*patch)(uint64_t cp_address,
                         const void *expected,
                         const void *replacement,
                         size_t length);
};

typedef struct PilotApi {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t feature_flags;
    uint32_t reserved;

    PilotStatus (*time_utc)(char *buffer, size_t capacity);
    PilotStatus (*memory_read)(uintptr_t address, void *output, size_t length);
    PilotStatus (*memory_patch)(uintptr_t address,
                                const void *expected,
                                const void *replacement,
                                size_t length);
    /* Creates the card-side module data directory and returns its Fsys path
     * with a trailing slash, for example X:/pilot/modules/example/.
     * module_name accepts ASCII letters, digits, underscore, dash, and dot;
     * it cannot start with a dot or contain '..'.
     */
    PilotStatus (*get_module_dir)(const char *module_name,
                                  char *buffer,
                                  size_t capacity);
    /* Module-bound namespaces. file and mlog point to persistent tables
     * scoped to the module filename stem from PILOT_MANIFEST_NAME. */
    const PilotFileApi *file;
    const PilotLogApi *log;
    const PilotModuleLogApi *mlog;
    const PilotCpMemApi *cp_mem;
} PilotApi;

typedef PilotStatus (*PilotModuleInit)(const PilotApi *api);

/* The framework bootstrap exports are model-specific.  A module usually does
 * not call this directly: pilot_module_init() receives the API table. */
const PilotApi *PILOT_GET_API_FN(void);

/* Every SD-card module must declare its model tag.  pilot_load_modules()
 * dlsym()s PILOT_MODULE_MODEL_SYMBOL and refuses a module that was built for
 * another body (or carries no tag at all), before calling its init function.
 * Compile the module with the same PILOT_MODEL_* macro as the framework. */
#define PILOT_DECLARE_MODULE_MODEL() \
    __attribute__((visibility("default"), used)) \
    const char PILOT_MODULE_MODEL_FN[] = PILOT_MODEL_ID

#ifdef __cplusplus
}
#endif

#endif
