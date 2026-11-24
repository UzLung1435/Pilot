#define _GNU_SOURCE
#include "../pilot.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

PILOT_DECLARE_MODULE_MODEL();

__attribute__((visibility("default")))
PilotStatus pilot_module_init(const PilotApi *api)
{
    if (!api || api->abi_version != PILOT_ABI_VERSION ||
        api->struct_size < sizeof(PilotApi) ||
        !(api->feature_flags & PILOT_FEATURE_LOG) ||
        !(api->feature_flags & PILOT_FEATURE_TIME_UTC) ||
        !(api->feature_flags & PILOT_FEATURE_MODULE_DIR) ||
        !(api->feature_flags & PILOT_FEATURE_MODULE_FILES) ||
        !api->file || !api->file->read || !api->file->write ||
        !api->file->append || !api->file->read_bytes ||
        !api->file->write_bytes || !api->file->append_bytes ||
        !api->log || !api->log->print || !api->mlog || !api->mlog->print)
        return PILOT_STATUS_INVALID_ARGUMENT;

    char module_dir[128];
    if (api->get_module_dir("a6700_pilot_api_smoke", module_dir,
                            sizeof(module_dir)) != PILOT_STATUS_OK ||
        strcmp(module_dir, "A:/pilot/modules/a6700_pilot_api_smoke/") != 0)
        return PILOT_STATUS_IO;

    char contents[256];
    if (api->file->read(api->file, "missing.cfg", contents,
                        sizeof(contents)) != PILOT_STATUS_NOT_FOUND)
        return PILOT_STATUS_IO;
    if (api->file->write(api->file, "settings.cfg", "alpha") !=
            PILOT_STATUS_OK ||
        api->file->append(api->file, "settings.cfg", "\nbeta") !=
            PILOT_STATUS_OK)
        return PILOT_STATUS_IO;
    if (api->file->read(api->file, "settings.cfg", contents,
                        sizeof(contents)) != PILOT_STATUS_OK ||
        strcmp(contents, "alpha\nbeta") != 0)
        return PILOT_STATUS_IO;
    if (api->file->write(api->file, "settings.cfg", "x") !=
            PILOT_STATUS_OK ||
        api->file->read(api->file, "settings.cfg", contents,
                        sizeof(contents)) != PILOT_STATUS_OK ||
        strcmp(contents, "x") != 0)
        return PILOT_STATUS_IO;
    if (api->file->write(api->file, "../escape.cfg", "x") !=
        PILOT_STATUS_INVALID_ARGUMENT)
        return PILOT_STATUS_IO;

    PilotStatus module_log_status = api->file->read(
        api->file, "module.log", contents, sizeof(contents));
    if (module_log_status != PILOT_STATUS_NOT_FOUND &&
        (module_log_status != PILOT_STATUS_OK ||
         !strstr(contents, "module booted\n")))
        return PILOT_STATUS_IO;
    if (api->mlog->print(api->mlog, "module booted") != PILOT_STATUS_OK ||
        api->file->read(api->file, "module.log", contents,
                        sizeof(contents)) != PILOT_STATUS_OK ||
        !strstr(contents, "module booted\n"))
        return PILOT_STATUS_IO;
    if (api->log->print("PILOT_MODULE_INIT_OK") != PILOT_STATUS_OK)
        return PILOT_STATUS_IO;

    const unsigned char payload[] = {0x00, 0x7f, 0xff};
    unsigned char payload_readback[sizeof(payload)] = {0};
    size_t payload_length = 0;
    if (api->file->write_bytes(api->file, "payload.bin", payload,
                               sizeof(payload)) != PILOT_STATUS_OK ||
        api->file->read_bytes(api->file, "payload.bin", payload_readback,
                              sizeof(payload_readback), &payload_length) !=
            PILOT_STATUS_OK ||
        payload_length != sizeof(payload) ||
        memcmp(payload, payload_readback, sizeof(payload)) != 0)
        return PILOT_STATUS_IO;

    char timestamp[32];
    if (api->time_utc(timestamp, sizeof(timestamp)) != PILOT_STATUS_OK)
        return PILOT_STATUS_SYSTEM;

    int fd = open("/tmp/pilot-module-init.marker",
                  O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return PILOT_STATUS_IO;
    ssize_t written = write(fd, timestamp, strlen(timestamp));
    close(fd);
    return written == (ssize_t)strlen(timestamp)
        ? PILOT_STATUS_OK : PILOT_STATUS_IO;
}
