#define _GNU_SOURCE
#include "../pilot.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#define PILOT_FSYS_NOT_FOUND_STATUS ((int32_t)0x80000051u)
#define PILOT_FSYS_IO_STATUS ((int32_t)0x80000083u)

typedef char (*get_drive_letter_fn)(void *drive_descriptor);
typedef int (*mkdir_fn)(const char *path);
typedef int (*write_file_fn)(const char *path, const void *data, size_t size);
typedef void (*mount_callback_fn)(const void *mount_args,
                                 get_drive_letter_fn get_drive_letter,
                                 mkdir_fn mkdir_path,
                                 write_file_fn write_file);
typedef const PilotApi *(*get_api_fn)(void);
typedef void (*fake_dispatch_fn)(void *object);

struct mount_args {
    void *drive_descriptor;
    unsigned char mount_flags;
};

static int drive_token;
static const char *card_root;
static const char *scenario;
static unsigned int mkdir_calls;
static unsigned int write_calls;
static __thread int pf2_errno_value;
static __thread int pf2_inerrnum_value;

extern int __xstat(int version, const char *path, struct stat *stat_buffer);

static int path_to_host(const char *path, char *output, size_t capacity)
{
    if (!path || path[0] != 'A' || path[1] != ':' || path[2] != '/')
        return 0;
    const char *relative = path + 3;
    if (strstr(relative, "..") || strchr(relative, '\\')) return 0;
    int size = snprintf(output, capacity, "%s/%s", card_root, relative);
    return size > 0 && (size_t)size < capacity;
}

static int copy_file(const char *source, const char *destination)
{
    int input = open(source, O_RDONLY);
    if (input < 0) return 0;
    int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (output < 0) {
        close(input);
        return 0;
    }

    char buffer[4096];
    int ok = 1;
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        if (count < 0) {
            ok = 0;
            break;
        }
        if (count == 0) break;
        ssize_t offset = 0;
        while (offset < count) {
            ssize_t written = write(output, buffer + offset,
                                    (size_t)(count - offset));
            if (written <= 0) {
                ok = 0;
                break;
            }
            offset += written;
        }
        if (!ok) break;
    }
    if (fsync(output) != 0) ok = 0;
    close(input);
    close(output);
    return ok;
}

static char fake_get_drive_letter(void *drive_descriptor)
{
    return drive_descriptor == &drive_token ? 'A' : '\0';
}

static int fake_fsys_mkdir(const char *path)
{
    char host_path[512];
    if (!path_to_host(path, host_path, sizeof(host_path))) return -1;
    ++mkdir_calls;
    if (mkdir(host_path, 0755) < 0 && errno != EEXIST) return -1;
    return 0;
}

static int fake_fsys_write_file(const char *path,
                                const void *data,
                                size_t size)
{
    char host_path[512];
    if (!path_to_host(path, host_path, sizeof(host_path))) return -1;
    ++write_calls;
    int fd = open(host_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    const char *cursor = data;
    size_t remaining = size;
    while (remaining) {
        ssize_t written = write(fd, cursor, remaining);
        if (written <= 0) {
            close(fd);
            return -1;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    int result = fsync(fd);
    close(fd);
    return result;
}

int Fsys_Mkdir(const char *path)
{
    return fake_fsys_mkdir(path);
}

int Fsys_Stat(const char *path, void *stat_buffer)
{
    char host_path[512];
    if (!path_to_host(path, host_path, sizeof(host_path)) || !stat_buffer)
        return -1;
    int result = __xstat(0, host_path, (struct stat *)stat_buffer);
    if (result == 0) return 0;
    pf2_errno_value = errno;
    pf2_inerrnum_value = 0;
    return pf2_errno_value == ENOENT
        ? PILOT_FSYS_NOT_FOUND_STATUS : PILOT_FSYS_IO_STATUS;
}

int Fsys_Open(const char *path, void **handle_out, int flags)
{
    char host_path[512];
    if (!path_to_host(path, host_path, sizeof(host_path)) || !handle_out) {
        pf2_errno_value = EINVAL;
        pf2_inerrnum_value = 0;
        return PILOT_FSYS_NOT_FOUND_STATUS;
    }

    if (strcmp(scenario, "error") == 0 &&
        strcmp(path + 3, "pilot/disable") == 0) {
        pf2_errno_value = EIO;
        pf2_inerrnum_value = 0;
        return PILOT_FSYS_IO_STATUS;
    }

    if (strcmp(scenario, "status-only") == 0 &&
        strcmp(path + 3, "pilot/disable") == 0) {
        pf2_errno_value = 0;
        pf2_inerrnum_value = 0;
        return PILOT_FSYS_NOT_FOUND_STATUS;
    }

    int open_flags = flags == 1 ? O_RDONLY
        : (flags == 0xE ? (O_WRONLY | O_CREAT | O_TRUNC) : O_WRONLY);
    int fd = open(host_path, open_flags, 0644);
    if (fd < 0) {
        pf2_errno_value = errno;
        pf2_inerrnum_value = 0;
        return pf2_errno_value == ENOENT
            ? PILOT_FSYS_NOT_FOUND_STATUS : PILOT_FSYS_IO_STATUS;
    }
    *handle_out = (void *)(intptr_t)fd;
    pf2_errno_value = 0;
    pf2_inerrnum_value = 0;
    return 0;
}

int Fsys_Read(void *handle, void *buffer, size_t length)
{
    ssize_t result = read((int)(intptr_t)handle, buffer, length);
    if (result < 0) return PILOT_FSYS_IO_STATUS;
    return (int)result;
}

int Fsys_Write(void *handle, const void *buffer, size_t length)
{
    ssize_t result = write((int)(intptr_t)handle, buffer, length);
    if (result < 0) return PILOT_FSYS_IO_STATUS;
    return (int)result;
}

#ifndef PILOT_SMOKE_OMIT_FSYS_SEEK
int Fsys_Seek(void *handle, int64_t offset, int whence, int64_t *position_out)
{
    off_t position = lseek((int)(intptr_t)handle, (off_t)offset, whence);
    if (position < 0) return PILOT_FSYS_IO_STATUS;
    if (position_out) *position_out = (int64_t)position;
    return 0;
}
#endif

int Fsys_Flush(void *handle)
{
    return fsync((int)(intptr_t)handle) == 0 ? 0 : PILOT_FSYS_IO_STATUS;
}

int Fsys_Close(void *handle)
{
    return close((int)(intptr_t)handle) == 0 ? 0 : PILOT_FSYS_IO_STATUS;
}

int pf2_errnum(void)
{
    return pf2_errno_value;
}

int pf2_inerrnum(void)
{
    return pf2_inerrnum_value;
}

static int file_contains(const char *path, const char *needle)
{
    char content[4096];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t size = read(fd, content, sizeof(content) - 1);
    close(fd);
    if (size < 0) return 0;
    content[size] = '\0';
    return strstr(content, needle) != 0;
}

static int file_equals(const char *path,
                       const char *expected,
                       size_t expected_length)
{
    char content[128];
    if (expected_length > sizeof(content)) return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    size_t used = 0;
    while (used < sizeof(content)) {
        ssize_t count = read(fd, content + used, sizeof(content) - used);
        if (count < 0) {
            close(fd);
            return 0;
        }
        if (count == 0) break;
        used += (size_t)count;
    }
    close(fd);
    return used == expected_length &&
           memcmp(content, expected, expected_length) == 0;
}

static uint64_t smoke_hash_update(uint64_t hash,
                                 const void *data,
                                 size_t length)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static int smoke_package_hash(const char *manifest,
                              size_t manifest_length,
                              const char *module_name,
                              const char *module_path,
                              uint64_t *hash_out)
{
    uint64_t hash = smoke_hash_update(14695981039346656037ull,
                                      manifest, manifest_length);
    hash = smoke_hash_update(hash, module_name, strlen(module_name) + 1);
    int fd = open(module_path, O_RDONLY);
    if (fd < 0) return 0;
    unsigned char buffer[4096];
    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            close(fd);
            return 0;
        }
        if (count == 0) break;
        hash = smoke_hash_update(hash, buffer, (size_t)count);
    }
    close(fd);
    *hash_out = hash;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 5 && argc != 6) {
        fprintf(stderr,
                "usage: %s <pilot.so> <card-root> <module.so> <allow|disable|error|resolve|status-only|probe|fuse-armed> [appFw.so]\n",
                argv[0]);
        return 2;
    }

    card_root = argv[2];
    scenario = argv[4];
    if (strcmp(scenario, "allow") != 0 && strcmp(scenario, "disable") != 0 &&
        strcmp(scenario, "error") != 0 && strcmp(scenario, "resolve") != 0 &&
        strcmp(scenario, "status-only") != 0 &&
        strcmp(scenario, "dtinit") != 0 &&
        strcmp(scenario, "probe") != 0 &&
        strcmp(scenario, "fuse-armed") != 0)
        return 2;

    const char *module_name = strrchr(argv[3], '/');
    module_name = module_name ? module_name + 1 : argv[3];
    size_t module_name_length = strlen(module_name);
    if (module_name_length < 4 || module_name_length >= 65 ||
        strcmp(module_name + module_name_length - 3, ".so") != 0 ||
        strstr(module_name, "..") || strchr(module_name, '\\'))
        return 2;
    int is_probe_module = strcmp(module_name, "pilot_menu_ui_probe.so") == 0;
    if ((strcmp(scenario, "probe") == 0) != is_probe_module) return 2;
    if (argc == 6 && !is_probe_module) return 2;

    void *probe_appfw_handle = 0;
    fake_dispatch_fn probe_dispatch = 0;
    if (argc == 6) {
        probe_appfw_handle = dlopen(argv[5], RTLD_NOW | RTLD_GLOBAL);
        if (!probe_appfw_handle) {
            const char *error = dlerror();
            fprintf(stderr, "fake appFw load failed: %s\n",
                    error ? error : "unknown");
            return 7;
        }
        void *symbol = dlsym(probe_appfw_handle, "fake_menu_dispatch");
        if (sizeof(probe_dispatch) != sizeof(symbol)) return 7;
        memcpy(&probe_dispatch, &symbol, sizeof(probe_dispatch));
        if (!probe_dispatch) return 7;
    }

    char pilot_dir[512];
    char manifest_path[512];
    char module_card_path[512];
    char staged_module_path[512];
    char disable_path[512];
    char log_path[512];
    char fuse_path[512];
    if (snprintf(pilot_dir, sizeof(pilot_dir), "%s/pilot", card_root) <= 0 ||
        snprintf(manifest_path, sizeof(manifest_path),
                 "%s/pilot/" PILOT_MANIFEST_NAME, card_root) <= 0 ||
        snprintf(module_card_path, sizeof(module_card_path),
                 "%s/pilot/%s", card_root, module_name) <= 0 ||
        snprintf(staged_module_path, sizeof(staged_module_path),
                 "/tmp_wbshd/pilot/modules/%s", module_name) <= 0 ||
        snprintf(disable_path, sizeof(disable_path), "%s/pilot/disable", card_root) <= 0 ||
        snprintf(log_path, sizeof(log_path), "%s/pilot/log.txt", card_root) <= 0 ||
        snprintf(fuse_path, sizeof(fuse_path), "%s/pilot/fuse.log", card_root) <= 0)
        return 3;
    if (mkdir(card_root, 0755) < 0 && errno != EEXIST) return 3;
    if (mkdir(pilot_dir, 0755) < 0 && errno != EEXIST) return 3;

    unlink("/tmp/pilot-module-init.marker");
    unlink(staged_module_path);
    if (strcmp(scenario, "disable") == 0) {
        int marker_fd = open(disable_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (marker_fd < 0) return 4;
        close(marker_fd);
    } else {
        unlink(disable_path);
    }

    int manifest_fd = open(manifest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (manifest_fd < 0) return 5;
    char manifest[128];
    int manifest_size = snprintf(manifest, sizeof(manifest),
                                 "{\"modules\":[\"%s\"]}\n", module_name);
    if (manifest_size <= 0 || (size_t)manifest_size >= sizeof(manifest) ||
        write(manifest_fd, manifest, (size_t)manifest_size) != manifest_size) {
        close(manifest_fd);
        return 5;
    }
    close(manifest_fd);
    if (!copy_file(argv[3], module_card_path)) return 6;

    if (strcmp(scenario, "fuse-armed") == 0 &&
        access(fuse_path, F_OK) != 0) {
        uint64_t package_hash = 0;
        if (!smoke_package_hash(manifest, (size_t)manifest_size, module_name,
                                module_card_path, &package_hash))
            return 6;
        char record[96];
        int record_length = snprintf(record, sizeof(record),
                                     "\nPILOT_FUSE state=ARMED package=%016llX\n",
                                     (unsigned long long)package_hash);
        if (record_length <= 0 || (size_t)record_length >= sizeof(record))
            return 6;
        int fuse_fd = open(fuse_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fuse_fd < 0 || write(fuse_fd, record, (size_t)record_length) != record_length ||
            fsync(fuse_fd) != 0) {
            if (fuse_fd >= 0) close(fuse_fd);
            return 6;
        }
        close(fuse_fd);
    }

    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        const char *error = dlerror();
        fprintf(stderr, "dlopen failed: %s\n", error ? error : "unknown");
        return 10;
    }

    dlerror();
    mount_callback_fn on_mount = (mount_callback_fn)dlsym(
        handle, PILOT_ON_MOUNT_SYMBOL);
    const char *error = dlerror();
    get_api_fn get_api = (get_api_fn)dlsym(handle, PILOT_GET_API_SYMBOL);
    if (error || !on_mount || !get_api) return 11;

    char empty_path[512];
    snprintf(empty_path, sizeof(empty_path), "%s/pilot/should-not-exist", card_root);
    struct mount_args absent = {0, 0};
    on_mount(&absent, fake_get_drive_letter, fake_fsys_mkdir,
             fake_fsys_write_file);
    if (access(empty_path, F_OK) == 0 || mkdir_calls || write_calls) {
        fprintf(stderr, "absent card unexpectedly triggered SD I/O\n");
        return 12;
    }

    if (strcmp(scenario, "dtinit") == 0) {
        char dcim_host[512];
        if (!path_to_host("A:/DCIM", dcim_host, sizeof(dcim_host)) ||
            (mkdir(dcim_host, 0755) < 0 && errno != EEXIST)) {
            fprintf(stderr, "failed to create DCIM directory\n");
            return 12;
        }
    } else {
        struct mount_args mounted = {&drive_token, 7};
        on_mount(&mounted, fake_get_drive_letter, fake_fsys_mkdir,
                 fake_fsys_write_file);
    }

    int log_ready = 0;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        if (access(log_path, F_OK) == 0) {
            log_ready = 1;
            break;
        }
        usleep(50000);
    }
    if (!log_ready) {
        fprintf(stderr, "startup log was not created\n");
        return 13;
    }

    FILE *startup_log = fopen(log_path, "rb");
    int first_log_byte = startup_log ? fgetc(startup_log) : EOF;
    if (startup_log) fclose(startup_log);
    if (first_log_byte != '\n') {
        fprintf(stderr, "startup separator missing from log\n");
        return 31;
    }

    const PilotApi *api = 0;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        api = get_api();
        int want_enabled = strcmp(scenario, "allow") == 0 ||
                           strcmp(scenario, "status-only") == 0 ||
                           strcmp(scenario, "dtinit") == 0 ||
                           strcmp(scenario, "probe") == 0;
        if (!!(api->feature_flags & PILOT_FEATURE_TIME_UTC) == want_enabled)
            break;
        usleep(10000);
    }

    int enabled = strcmp(scenario, "allow") == 0 ||
                  strcmp(scenario, "status-only") == 0 ||
                  strcmp(scenario, "dtinit") == 0 ||
                  strcmp(scenario, "probe") == 0;
    char timestamp[32];
    PilotStatus time_status = api->time_utc(timestamp, sizeof(timestamp));
    if ((enabled && time_status != PILOT_STATUS_OK) ||
        (!enabled && time_status != PILOT_STATUS_DISABLED)) {
        fprintf(stderr, "time gate mismatch: scenario=%s status=%d flags=%u\n",
                scenario, time_status, api->feature_flags);
        return 14;
    }

    char module_dir[128];
    PilotStatus module_dir_status = api->get_module_dir(
        "pilot_sd_log_smoke", module_dir, sizeof(module_dir));
    if ((enabled && (module_dir_status != PILOT_STATUS_OK ||
                     strcmp(module_dir, "A:/pilot/modules/pilot_sd_log_smoke/") != 0)) ||
        (!enabled && module_dir_status != PILOT_STATUS_DISABLED)) {
        fprintf(stderr, "module directory API gate mismatch: scenario=%s status=%d\n",
                scenario, module_dir_status);
        return 19;
    }
    char module_dir_host[512];
    if (enabled && (!path_to_host(module_dir, module_dir_host,
                                  sizeof(module_dir_host)) ||
                    access(module_dir_host, F_OK) != 0)) {
        fprintf(stderr, "module directory was not created on the card shim\n");
        return 20;
    }
    if (!api->log || !api->log->print) {
        fprintf(stderr, "global log namespace missing: scenario=%s\n", scenario);
        return 21;
    }
    PilotStatus global_log_status = api->log->print("PILOT_GLOBAL_LOG_API_OK");
    if ((enabled && global_log_status != PILOT_STATUS_OK) ||
        (!enabled && global_log_status != PILOT_STATUS_DISABLED)) {
        fprintf(stderr, "global log gate mismatch: scenario=%s status=%d\n",
                scenario, global_log_status);
        return 22;
    }

    int module_loaded = 0;
    const char *probe_init_log = probe_dispatch
        ? "MENU_UI_PROBE_ARMED"
        : "MENU_UI_PROBE_APPFW_NOT_FOUND";
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        if (is_probe_module)
            module_loaded = file_contains(log_path, probe_init_log);
        else
            module_loaded = access("/tmp/pilot-module-init.marker", F_OK) == 0;
        if (module_loaded == enabled) break;
        usleep(50000);
    }
    if (module_loaded != enabled) {
        fprintf(stderr, "module gate mismatch: scenario=%s loaded=%d\n",
                scenario, module_loaded);
        return 15;
    }
    if (enabled && access(staged_module_path, F_OK) != 0) {
        fprintf(stderr, "staged module not found under /tmp_wbshd/pilot/modules\n");
        return 16;
    }
    if (enabled && !is_probe_module) {
        char module_files_dir[128];
        char module_files_dir_host[512];
        char settings_path[600];
        char module_log_path[600];
        int files_dir_length = snprintf(
            module_files_dir, sizeof(module_files_dir),
            "A:/pilot/modules/a6700_pilot_api_smoke");
        if (files_dir_length <= 0 ||
            (size_t)files_dir_length >= sizeof(module_files_dir) ||
            !path_to_host(module_files_dir, module_files_dir_host,
                          sizeof(module_files_dir_host))) {
            fprintf(stderr, "module files directory path was invalid\n");
            return 23;
        }
        int settings_length = snprintf(settings_path, sizeof(settings_path),
                                       "%s/settings.cfg",
                                       module_files_dir_host);
        int module_log_length = snprintf(module_log_path,
                                         sizeof(module_log_path),
                                         "%s/module.log",
                                         module_files_dir_host);
        if (settings_length <= 0 ||
            (size_t)settings_length >= sizeof(settings_path) ||
            module_log_length <= 0 ||
            (size_t)module_log_length >= sizeof(module_log_path) ||
            !file_equals(settings_path, "x", 1) ||
            !file_contains(module_log_path, "module booted\n")) {
            fprintf(stderr,
                    "module files were not persisted under the module SD directory\n");
            return 23;
        }
    }
    if (enabled && (!file_contains(log_path, "T") ||
                    (is_probe_module
                         ? !file_contains(log_path, probe_init_log)
                         : !file_contains(log_path,
                                          "PILOT_MODULE_INIT_OK")))) {
        fprintf(stderr, "timestamped module log line missing\n");
        return 17;
    }
    if (enabled) {
        int complete_log = 0;
        int complete_fuse = 0;
        for (unsigned int attempt = 0; attempt < 100; ++attempt) {
            complete_log = file_contains(log_path,
                                         "PILOT_FUSE state=COMPLETE");
            complete_fuse = file_contains(fuse_path,
                                          "PILOT_FUSE state=COMPLETE");
            if (complete_log && complete_fuse) break;
            usleep(50000);
        }
        if (!is_probe_module &&
            (!file_contains(log_path, "name=a6700_pilot_api_smoke.so Pilot module loading index=1/1") ||
             !file_contains(log_path, "name=a6700_pilot_api_smoke.so Pilot module initialized index=1/1 status=0"))) {
            fprintf(stderr, "named module lifecycle missing from log\n");
            return 32;
        }
        if (!complete_log || !complete_fuse) {
            fprintf(stderr,
                    "completed module traversal was not journaled log=%d fuse=%d\n",
                    complete_log, complete_fuse);
            return 30;
        }
    }
    if (!enabled && strcmp(scenario, "fuse-armed") == 0 &&
        !file_contains(log_path, "PILOT_FUSE state=TRIPPED")) {
        fprintf(stderr, "fuse trip record missing from log\n");
        return 18;
    }
    if (!enabled && strcmp(scenario, "fuse-armed") != 0 &&
        !file_contains(log_path, "optional_features=disabled")) {
        fprintf(stderr, "disabled startup status missing from log\n");
        return 18;
    }
    const char *expected_gate = strcmp(scenario, "allow") == 0 ||
                                strcmp(scenario, "status-only") == 0 ||
                                strcmp(scenario, "dtinit") == 0 ||
                                strcmp(scenario, "probe") == 0 ||
                                strcmp(scenario, "fuse-armed") == 0
        ? "gate=open"
        : strcmp(scenario, "disable") == 0
            ? "gate=disable_marker"
            : strcmp(scenario, "resolve") == 0
                ? "gate=fsys_resolve_failed missing=Fsys_Seek"
                : "gate=disable_check_error stage=open_marker status=0x80000083 pf2=5 inner=0";
    if (!file_contains(log_path, expected_gate)) {
        fprintf(stderr, "gate diagnostic missing: scenario=%s expected=%s\n",
                scenario, expected_gate);
        return 29;
    }

    if (probe_dispatch) {
        uintptr_t site = (uintptr_t)probe_dispatch;
        char site_hex[48];
        snprintf(site_hex, sizeof(site_hex), "site=0x%016llX",
                 (unsigned long long)site);
        if (!file_contains(log_path, site_hex)) return 28;
        long page_size_value = sysconf(_SC_PAGESIZE);
        if (page_size_value <= 0) return 19;
        uintptr_t page_size = (uintptr_t)page_size_value;
        uintptr_t page = site & ~(page_size - 1u);
        uint32_t original_tail[2] = {
            *(volatile uint32_t *)(site + 4u),
            *(volatile uint32_t *)(site + 8u),
        };
        if (original_tail[0] != 0xF0015BE1u ||
            original_tail[1] != 0x910003FDu ||
            mprotect((void *)page, (size_t)page_size,
                     PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
            return 20;

        uint32_t return_stub[2] = {0xA8C27BFDu, 0xD65F03C0u};
        memcpy((void *)(site + 4u), return_stub, sizeof(return_stub));
        __builtin___clear_cache((char *)(site + 4u), (char *)(site + 12u));
        if (mprotect((void *)page, (size_t)page_size,
                     PROT_READ | PROT_EXEC) != 0)
            return 21;

        unsigned char *object = calloc(1, 0x11B30u);
        uintptr_t vtable[5] = {0};
        uintptr_t parent_slot = (uintptr_t)vtable;
        uint64_t payload = 0xA5B6C7D8u;
        vtable[4] = 0x1122334455667788ull;
        if (!object) return 22;
        memcpy(object + 0x11B20u, &payload, sizeof(payload));
        uintptr_t parent_address = (uintptr_t)&parent_slot;
        memcpy(object + 0x11B28u, &parent_address, sizeof(parent_address));
        probe_dispatch(object);

        int hit_logged = 0;
        for (unsigned int attempt = 0; attempt < 100; ++attempt) {
            if (file_contains(log_path, "MENU_UI_PROBE_HIT")) {
                hit_logged = 1;
                break;
            }
            usleep(50000);
        }
        if (!hit_logged) return 23;

        char object_hex[32];
        snprintf(object_hex, sizeof(object_hex), "%016llX",
                 (unsigned long long)(uintptr_t)object);
        char payload_hex[32];
        snprintf(payload_hex, sizeof(payload_hex), "%016llX",
                 (unsigned long long)payload);
        if (!file_contains(log_path, "group_id=") ||
            !file_contains(log_path, object_hex))
            return 24;

        int stopped = 0;
        for (unsigned int attempt = 0; attempt < 150; ++attempt) {
            if (file_contains(log_path, "MENU_UI_PROBE_STOP reason=idle")) {
                stopped = 1;
                break;
            }
            usleep(50000);
        }
        if (!stopped || *(volatile uint32_t *)site != 0xA9BE7BFDu)
            return 25;

        if (mprotect((void *)page, (size_t)page_size,
                     PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
            return 26;
        memcpy((void *)(site + 4u), original_tail, sizeof(original_tail));
        __builtin___clear_cache((char *)(site + 4u), (char *)(site + 12u));
        if (mprotect((void *)page, (size_t)page_size,
                     PROT_READ | PROT_EXEC) != 0)
            return 27;
        printf("PILOT_PROBE_HIT_OK site=0x%llX object=0x%llX\n",
               (unsigned long long)site,
               (unsigned long long)(uintptr_t)object);
        free(object);
    }

    printf("PILOT_RUNTIME_OK scenario=%s enabled=%d module_loaded=%d path=%s\n",
           scenario, enabled, module_loaded, log_path);
    return 0;
}
