#define _GNU_SOURCE
#include "pilot_internal.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PILOT_MOUNT_RETRY_COUNT 5u
#define PILOT_MOUNT_RETRY_NS 50000000l

struct pilot_card_log_job {
    char drive_letter;
    struct pilot_mount_args mount_args;
    pilot_mkdir_fn mkdir_path;
    pilot_write_file_fn write_file;
};

/* struct pilot_mount_args is Pilot's own internal ABI.  The bootstrap island
 * only dlopen()s PILOT_DSO_PATH, so the layout is shared by both bodies while
 * the DSO name and exports are model-specific. */
_Static_assert(offsetof(struct pilot_mount_args, mount_flags) == 8,
               "Pilot mount argument layout changed");

static atomic_flag pilot_started = ATOMIC_FLAG_INIT;
static unsigned int card_worker_state[52];
static pthread_mutex_t card_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t runtime_lock = PTHREAD_MUTEX_INITIALIZER;
static int runtime_active;

struct pilot_runtime g_pilot = {
    .optional_features_enabled = 0,
    .time_feature_enabled = 0,
    .log_lock = PTHREAD_MUTEX_INITIALIZER,
};

/* The island looks this symbol up with dlsym(RTLD_DEFAULT, ...) as a
 * "bootstrap already loaded" guard.  Its name is model-specific, so an
 * a6700 DSO can never satisfy an a7c2 island (or vice versa). */
__attribute__((visibility("default"), used))
const unsigned int PILOT_MARKER_FN = 0x50494c31u;

static void pilot_runtime_log(const char *message)
{
    (void)mkdir("/tmp_wbshd/pilot", 0700);
    int fd = open("/tmp_wbshd/pilot/pilot-runtime.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;

    const char *cursor = message;
    size_t remaining = strlen(message);
    while (remaining) {
        ssize_t written = write(fd, cursor, remaining);
        if (written <= 0) break;
        cursor += written;
        remaining -= (size_t)written;
    }
    close(fd);
}

static int is_drive_letter(unsigned char value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

static unsigned int drive_index(char letter)
{
    if (letter >= 'A' && letter <= 'Z')
        return (unsigned int)(letter - 'A');
    return 26u + (unsigned int)(letter - 'a');
}

static void pilot_set_feature_state(int enabled)
{
    __atomic_store_n(&g_pilot.optional_features_enabled, enabled,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&g_pilot.time_feature_enabled, enabled,
                     __ATOMIC_RELEASE);
}

void pilot_runtime_mount_worker(struct pilot_mount_args mount_args,
                                pilot_mkdir_fn mkdir_path,
                                pilot_write_file_fn write_file,
                                char drive_letter)
{
    pthread_mutex_lock(&runtime_lock);
    if (runtime_active && g_pilot.drive_letter != drive_letter) {
        pthread_mutex_unlock(&runtime_lock);
        return;
    }

    g_pilot.drive_letter = drive_letter;
    g_pilot.mount_flags = mount_args.mount_flags;
    g_pilot.mkdir_path = mkdir_path;
    g_pilot.write_file = write_file;

    char directory[32];
    char marker[40];
    int directory_size = snprintf(directory, sizeof(directory), "%c:/pilot",
                                  drive_letter);
    int marker_size = snprintf(marker, sizeof(marker), "%c:/pilot/disable",
                               drive_letter);
    int log_size = snprintf(g_pilot.log_path, sizeof(g_pilot.log_path),
                            "%c:/pilot/log.txt", drive_letter);
    if (directory_size <= 0 || (size_t)directory_size >= sizeof(directory) ||
        marker_size <= 0 || (size_t)marker_size >= sizeof(marker) ||
        log_size <= 0 || (size_t)log_size >= sizeof(g_pilot.log_path)) {
        pilot_set_feature_state(0);
        pilot_runtime_log("PILOT_GATE=closed reason=path_format\n");
        pthread_mutex_unlock(&runtime_lock);
        return;
    }

    char gate_detail[176];
    const char *missing_symbol = 0;
    struct pilot_fsys_check_diagnostic diagnostic;
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (!pilot_fsys_resolve(&g_pilot.fsys, &missing_symbol)) {
        pilot_set_feature_state(0);
        snprintf(gate_detail, sizeof(gate_detail),
                 "gate=fsys_resolve_failed missing=%s",
                 missing_symbol ? missing_symbol : "unknown");
        char receipt[224];
        snprintf(receipt, sizeof(receipt), "PILOT_GATE=closed %s drive=%c\n",
                 gate_detail, drive_letter);
        pilot_runtime_log(receipt);
    } else {
        int marker_state = -1;
        const struct timespec retry_delay = {0, PILOT_MOUNT_RETRY_NS};
        for (unsigned int attempt = 0; attempt < PILOT_MOUNT_RETRY_COUNT;
             ++attempt) {
            if (mkdir_path) (void)mkdir_path(directory);
            marker_state = pilot_fsys_check_disable(directory, marker,
                                                    &g_pilot.fsys,
                                                    &diagnostic);
            if (marker_state >= 0) break;
            if (attempt + 1u < PILOT_MOUNT_RETRY_COUNT)
                (void)nanosleep(&retry_delay, 0);
        }
        pilot_set_feature_state(marker_state == 0);
        if (marker_state == 1) {
            snprintf(gate_detail, sizeof(gate_detail), "gate=disable_marker");
            pilot_runtime_log("PILOT_GATE=closed reason=pilot/disable\n");
        } else if (marker_state < 0) {
            snprintf(gate_detail, sizeof(gate_detail),
                     "gate=disable_check_error stage=%s status=0x%08X pf2=%d inner=%d",
                     diagnostic.stage ? diagnostic.stage : "unknown",
                     (unsigned int)diagnostic.status,
                     diagnostic.pf2_errno, diagnostic.pf2_inner);
            char receipt[224];
            snprintf(receipt, sizeof(receipt), "PILOT_GATE=closed %s\n",
                     gate_detail);
            pilot_runtime_log(receipt);
        } else {
            snprintf(gate_detail, sizeof(gate_detail), "gate=open");
            pilot_runtime_log("PILOT_GATE=open reason=disable_marker_absent\n");
        }
    }

    char line[384];
    char timestamp[32];
    int enabled = __atomic_load_n(&g_pilot.optional_features_enabled,
                                  __ATOMIC_ACQUIRE);
    if (enabled && pilot_time_utc_impl(timestamp, sizeof(timestamp)) ==
                       PILOT_STATUS_OK) {
        snprintf(line, sizeof(line),
                 "\n%s Pilot startup: Fsys mount succeeded on drive %c (mount_flags=%u, optional_features=enabled, %s)\n",
                 timestamp, drive_letter, (unsigned int)mount_args.mount_flags,
                 gate_detail);
    } else if (enabled) {
        snprintf(line, sizeof(line),
                 "\nPilot startup: Fsys mount succeeded on drive %c (mount_flags=%u, optional_features=enabled, time=unavailable, %s)\n",
                 drive_letter, (unsigned int)mount_args.mount_flags,
                 gate_detail);
    } else {
        snprintf(line, sizeof(line),
                 "\nPilot startup: Fsys mount succeeded on drive %c (mount_flags=%u, optional_features=disabled, %s)\n",
                 drive_letter, (unsigned int)mount_args.mount_flags,
                 gate_detail);
    }

    PilotStatus log_status = PILOT_STATUS_IO;
    const struct timespec retry_delay = {0, PILOT_MOUNT_RETRY_NS};
    for (unsigned int attempt = 0; attempt < PILOT_MOUNT_RETRY_COUNT;
         ++attempt) {
        log_status = pilot_log_append(line, strlen(line), 0);
        if (log_status == PILOT_STATUS_OK) break;
        if (attempt + 1u < PILOT_MOUNT_RETRY_COUNT)
            (void)nanosleep(&retry_delay, 0);
    }
    pilot_runtime_log(log_status == PILOT_STATUS_OK
                          ? "PILOT_SD_LOG=written\n"
                          : "PILOT_SD_LOG=failed\n");

    if (enabled) {
        __atomic_store_n(&runtime_active, 1, __ATOMIC_RELEASE);
        pilot_load_modules();
    }
    pthread_mutex_unlock(&runtime_lock);
}

static void *pilot_card_log_worker(void *opaque)
{
    struct pilot_card_log_job job = *(struct pilot_card_log_job *)opaque;
    free(opaque);
    pilot_runtime_mount_worker(job.mount_args, job.mkdir_path,
                               job.write_file, job.drive_letter);
    return 0;
}

__attribute__((visibility("default"), used))
void PILOT_ON_MOUNT_FN(
    const struct pilot_mount_args *mount_args,
    pilot_get_drive_letter_fn get_drive_letter,
    pilot_mkdir_fn mkdir_path,
    pilot_write_file_fn write_file)
{
    if (!mount_args || !mount_args->drive_descriptor || !get_drive_letter ||
        !mkdir_path || !write_file)
        return;

    char drive_letter = get_drive_letter(mount_args->drive_descriptor);
    if (!is_drive_letter((unsigned char)drive_letter)) return;

    unsigned int index = drive_index(drive_letter);
    pthread_mutex_lock(&card_state_lock);
    if (card_worker_state[index] != 0) {
        pthread_mutex_unlock(&card_state_lock);
        return;
    }
    card_worker_state[index] = 1;
    pthread_mutex_unlock(&card_state_lock);

    struct pilot_card_log_job *job = malloc(sizeof(*job));
    if (!job) {
        pthread_mutex_lock(&card_state_lock);
        card_worker_state[index] = 0;
        pthread_mutex_unlock(&card_state_lock);
        pilot_runtime_log("Pilot mount worker allocation failed\n");
        return;
    }
    job->drive_letter = drive_letter;
    job->mount_args = *mount_args;
    job->mkdir_path = mkdir_path;
    job->write_file = write_file;

    pthread_t thread;
    int result = pthread_create(&thread, 0, pilot_card_log_worker, job);
    if (result == 0) {
        pthread_detach(thread);
        pthread_mutex_lock(&card_state_lock);
        card_worker_state[index] = 2;
        pthread_mutex_unlock(&card_state_lock);
        return;
    }

    free(job);
    pthread_mutex_lock(&card_state_lock);
    card_worker_state[index] = 0;
    pthread_mutex_unlock(&card_state_lock);
    pilot_runtime_log("Pilot mount worker creation failed\n");
}

static int drive_has_card(const struct pilot_fsys_api *fsys, char drive_letter)
{
    char path[32];
    struct stat info;
    snprintf(path, sizeof(path), "%c:/pilot", drive_letter);
    memset(&info, 0, sizeof(info));
    int status = fsys->stat_path(path, &info);
    if (status == 0) return 1;
    if (!pilot_fsys_is_not_found(fsys, status)) return 0;

    snprintf(path, sizeof(path), "%c:/DCIM", drive_letter);
    memset(&info, 0, sizeof(info));
    return fsys->stat_path(path, &info) == 0;
}

static void *pilot_worker(void *unused)
{
    (void)unused;
    pilot_runtime_log("pilot_worker started\n");

    const struct timespec initial_delay = {0, 200000000l};
    const struct timespec poll_delay = {0, 250000000l};
    static const char candidates[] = "BXACDE";

    (void)nanosleep(&initial_delay, 0);
    for (unsigned int round = 0; round < 240u; ++round) {
        if (__atomic_load_n(&runtime_active, __ATOMIC_ACQUIRE))
            return 0;

        struct pilot_fsys_api fsys;
        if (pilot_fsys_resolve(&fsys, 0)) {
            void *mkdir_symbol = dlsym(RTLD_DEFAULT, "Fsys_Mkdir");
            pilot_mkdir_fn fsys_mkdir = 0;
            if (mkdir_symbol && sizeof(fsys_mkdir) == sizeof(mkdir_symbol))
                memcpy(&fsys_mkdir, &mkdir_symbol, sizeof(fsys_mkdir));

            if (fsys_mkdir) {
                for (size_t i = 0; candidates[i] != '\0'; ++i) {
                    char drive = candidates[i];
                    unsigned int index = drive_index(drive);
                    pthread_mutex_lock(&card_state_lock);
                    unsigned int state = card_worker_state[index];
                    pthread_mutex_unlock(&card_state_lock);
                    if (state != 0) continue;

                    if (!drive_has_card(&fsys, drive)) continue;

                    pthread_mutex_lock(&card_state_lock);
                    if (card_worker_state[index] != 0) {
                        pthread_mutex_unlock(&card_state_lock);
                        continue;
                    }
                    card_worker_state[index] = 2;
                    pthread_mutex_unlock(&card_state_lock);

                    struct pilot_mount_args args = {0, 0};
                    pilot_runtime_mount_worker(args, fsys_mkdir, 0, drive);
                    if (__atomic_load_n(&runtime_active, __ATOMIC_ACQUIRE))
                        return 0;
                }
            }
        }
        (void)nanosleep(&poll_delay, 0);
    }
    return 0;
}

__attribute__((constructor))
static void pilot_constructor(void)
{
    if (atomic_flag_test_and_set_explicit(&pilot_started, memory_order_acq_rel))
        return;

    pilot_runtime_log("Pilot " PILOT_MODEL_ID " DSO constructor entered\n");

    pthread_t thread;
    int result = pthread_create(&thread, 0, pilot_worker, 0);
    if (result == 0) {
        pthread_detach(thread);
        return;
    }

    pilot_runtime_log("pilot_worker create failed\n");
}
