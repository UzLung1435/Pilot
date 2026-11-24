#include "../../patch/pilot.h"
#include <pthread.h>
#include <stdio.h>

PILOT_DECLARE_MODULE_MODEL();

PilotStatus pilot_6k_encoder_init(const PilotApi *api);
PilotStatus pilot_6k_buffers_init(const PilotApi *api);

static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
static int attempted;
static PilotStatus init_status = PILOT_STATUS_SYSTEM;

static void log_stage(const PilotApi *api, const char *stage, PilotStatus status)
{
    char line[128];
    (void)snprintf(line, sizeof(line), "PILOT_6K_INIT stage=%s status=%d", stage, (int)status);
    (void)api->log->print(line);
}

__attribute__((visibility("default")))
PilotStatus pilot_module_init(const PilotApi *api)
{
    if (!api || api->abi_version != PILOT_ABI_VERSION || api->struct_size < sizeof(PilotApi))
        return PILOT_STATUS_UNSUPPORTED;
    if (!(api->feature_flags & PILOT_FEATURE_LOG) || !api->log || !api->log->print)
        return PILOT_STATUS_DISABLED;

    pthread_mutex_lock(&init_lock);
    if (!attempted) {
        attempted = 1;
        init_status = pilot_6k_encoder_init(api);
        log_stage(api, "encoder", init_status);
        if (init_status == PILOT_STATUS_OK) {
            init_status = pilot_6k_buffers_init(api);
            log_stage(api, "buffers", init_status);
        }
        if (init_status == PILOT_STATUS_OK)
            (void)api->log->print("PILOT_6K_INIT workers_started=2");
    }
    PilotStatus result = init_status;
    pthread_mutex_unlock(&init_lock);
    return result;
}
