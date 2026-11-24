#include "pilot_internal.h"

#include <stdio.h>
#include <string.h>

#define PILOT_API_MODULE_NAME_CAPACITY 65u

struct pilot_api_module_context {
    int active;
    char module_name[PILOT_API_MODULE_NAME_CAPACITY];
    PilotFileApi file;
    PilotModuleLogApi mlog;
    PilotApi api;
};

static pthread_mutex_t module_api_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pilot_api_module_context module_api_contexts[PILOT_MODULE_MAX_COUNT];

static PilotStatus api_log_write(const char *message, size_t length)
{
    if (!message || !length || length > 768)
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (!__atomic_load_n(&g_pilot.optional_features_enabled, __ATOMIC_ACQUIRE))
        return PILOT_STATUS_DISABLED;

    char line[896];
    char timestamp[32];
    size_t used = 0;
    if (pilot_time_utc_impl(timestamp, sizeof(timestamp)) == PILOT_STATUS_OK) {
        int count = snprintf(line, sizeof(line), "%s ", timestamp);
        if (count <= 0 || (size_t)count >= sizeof(line))
            return PILOT_STATUS_RANGE;
        used = (size_t)count;
    }

    if (length > sizeof(line) - used - 2) return PILOT_STATUS_RANGE;
    memcpy(line + used, message, length);
    used += length;
    if (line[used - 1] != '\n') line[used++] = '\n';
    return pilot_log_append(line, used, 1);
}

static PilotStatus api_log_print(const char *message)
{
    if (!message) return PILOT_STATUS_INVALID_ARGUMENT;
    return api_log_write(message, strlen(message));
}

static const PilotLogApi g_log_api = {
    .print = api_log_print,
};

static const PilotCpMemApi g_cp_mem_api = {
    .struct_size = sizeof(PilotCpMemApi),
    .reserved = 0,
    .read = pilot_cp_mem_read_impl,
    .write = pilot_cp_mem_write_impl,
    .patch = pilot_cp_mem_patch_impl,
};

static PilotApi g_api = {
    .abi_version = PILOT_ABI_VERSION,
    .struct_size = sizeof(PilotApi),
    .feature_flags = 0,
    .reserved = 0,
    .time_utc = pilot_time_utc_impl,
    .memory_read = pilot_memory_read_impl,
    .memory_patch = pilot_memory_patch_impl,
    .get_module_dir = pilot_get_module_dir_impl,
    .file = 0,
    .log = &g_log_api,
    .mlog = 0,
    .cp_mem = &g_cp_mem_api,
};

static uint32_t api_base_features(void)
{
    if (!__atomic_load_n(&g_pilot.optional_features_enabled, __ATOMIC_ACQUIRE))
        return 0;
    return PILOT_FEATURE_LOG | PILOT_FEATURE_TIME_UTC |
           PILOT_FEATURE_APPFW_MEMORY | PILOT_FEATURE_MODULE_DIR |
           PILOT_FEATURE_CP_MEMORY;
}

static int valid_module_name(const char *name)
{
    if (!name || !*name || name[0] == '.') return 0;
    size_t length = strlen(name);
    if (length >= PILOT_API_MODULE_NAME_CAPACITY || strstr(name, ".."))
        return 0;

    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
              ch == '.'))
            return 0;
    }
    return 1;
}

PilotStatus pilot_api_for_module(const char *module_name,
                                 const PilotApi **api_out)
{
    if (api_out) *api_out = 0;
    if (!api_out || !valid_module_name(module_name))
        return PILOT_STATUS_INVALID_ARGUMENT;
    if (!__atomic_load_n(&g_pilot.optional_features_enabled, __ATOMIC_ACQUIRE))
        return PILOT_STATUS_DISABLED;

    pthread_mutex_lock(&module_api_lock);
    struct pilot_api_module_context *free_context = 0;
    for (size_t i = 0; i < PILOT_MODULE_MAX_COUNT; ++i) {
        struct pilot_api_module_context *context = &module_api_contexts[i];
        if (context->active && strcmp(context->module_name, module_name) == 0) {
            *api_out = &context->api;
            pthread_mutex_unlock(&module_api_lock);
            return PILOT_STATUS_OK;
        }
        if (!context->active && !free_context) free_context = context;
    }
    if (!free_context) {
        pthread_mutex_unlock(&module_api_lock);
        return PILOT_STATUS_CONFLICT;
    }

    memset(free_context, 0, sizeof(*free_context));
    size_t name_length = strlen(module_name);
    memcpy(free_context->module_name, module_name, name_length + 1);

    free_context->file.struct_size = sizeof(free_context->file);
    free_context->file.context = free_context->module_name;
    free_context->file.read = pilot_file_read;
    free_context->file.write = pilot_file_write;
    free_context->file.append = pilot_file_append;
    free_context->file.read_bytes = pilot_file_read_bytes;
    free_context->file.write_bytes = pilot_file_write_bytes;
    free_context->file.append_bytes = pilot_file_append_bytes;

    free_context->mlog.context = free_context->module_name;
    free_context->mlog.print = pilot_module_log_print;

    free_context->api = g_api;
    free_context->api.feature_flags = api_base_features() |
                                      PILOT_FEATURE_MODULE_FILES;
    free_context->api.file = &free_context->file;
    free_context->api.log = &g_log_api;
    free_context->api.mlog = &free_context->mlog;
    free_context->active = 1;
    *api_out = &free_context->api;
    pthread_mutex_unlock(&module_api_lock);
    return PILOT_STATUS_OK;
}

__attribute__((visibility("default"), used))
const PilotApi *PILOT_GET_API_FN(void)
{
    g_api.feature_flags = api_base_features();
    return &g_api;
}
