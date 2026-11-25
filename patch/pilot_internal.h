#ifndef PILOT_INTERNAL_H
#define PILOT_INTERNAL_H

#include "pilot.h"

#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>

#define PILOT_MODULE_MAX_COUNT 16u

typedef int (*pilot_mkdir_fn)(const char *path);
typedef int (*pilot_write_file_fn)(const char *path,
                                   const void *data,
                                   size_t size);
typedef int (*pilot_fsys_stat_fn)(const char *path, void *stat_buffer);
typedef int (*pilot_fsys_open_fn)(const char *path,
                                  void **handle_out,
                                  int flags);
typedef int (*pilot_fsys_read_fn)(void *handle,
                                  void *buffer,
                                  size_t length);
typedef int (*pilot_fsys_write_fn)(void *handle,
                                   const void *buffer,
                                   size_t length);
typedef int (*pilot_fsys_seek_fn)(void *handle,
                                  int64_t offset,
                                  int whence,
                                  int64_t *position_out);
typedef int (*pilot_fsys_flush_fn)(void *handle);
typedef int (*pilot_fsys_close_fn)(void *handle);
typedef int (*pilot_pf2_error_fn)(void);

struct pilot_fsys_api {
    pilot_fsys_stat_fn stat_path;
    pilot_fsys_open_fn open_file;
    pilot_fsys_read_fn read_file;
    pilot_fsys_write_fn write_file;
    pilot_fsys_seek_fn seek_file;
    pilot_fsys_flush_fn flush_file;
    pilot_fsys_close_fn close_file;
    pilot_pf2_error_fn pf2_errnum;
    pilot_pf2_error_fn pf2_inerrnum;
};

struct pilot_fsys_check_diagnostic {
    const char *stage;
    int status;
    int pf2_errno;
    int pf2_inner;
};

enum pilot_fuse_state {
    PILOT_FUSE_STATE_NONE = 0,
    PILOT_FUSE_STATE_ARMED = 1,
    PILOT_FUSE_STATE_COMPLETE = 2,
    PILOT_FUSE_STATE_TRIPPED = 3,
    PILOT_FUSE_STATE_INVALID = 4
};

struct pilot_mount_args {
    void *drive_descriptor;
    unsigned char mount_flags;
};

typedef char (*pilot_get_drive_letter_fn)(void *drive_descriptor);

struct pilot_runtime {
    char drive_letter;
    unsigned char mount_flags;
    int optional_features_enabled;
    int time_feature_enabled;
    char log_path[48];
    pilot_mkdir_fn mkdir_path;
    pilot_write_file_fn write_file;
    struct pilot_fsys_api fsys;
    pthread_mutex_t log_lock;
    void *module_handles[16];
    size_t module_count;
};

extern struct pilot_runtime g_pilot;

int pilot_fsys_resolve(struct pilot_fsys_api *api,
                       const char **missing_symbol_out);
int pilot_fsys_is_not_found(const struct pilot_fsys_api *api, int status);
int pilot_fsys_check_disable(const char *directory,
                            const char *marker,
                            const struct pilot_fsys_api *api,
                            struct pilot_fsys_check_diagnostic *diagnostic);
PilotStatus pilot_fsys_read_all(const struct pilot_fsys_api *api,
                                const char *path,
                                void *buffer,
                                size_t capacity,
                                size_t *length_out,
                                int *not_found_out);
PilotStatus pilot_fsys_write_new_file(const char *path,
                                      const void *data,
                                      size_t length);
PilotStatus pilot_log_append(const char *line,
                             size_t length,
                             int optional_api_call);
PilotStatus pilot_fuse_read_state(enum pilot_fuse_state *state_out,
                                  uint64_t *package_out);
PilotStatus pilot_fuse_append_state(enum pilot_fuse_state state,
                                    uint64_t package);

PilotStatus pilot_time_utc_impl(char *buffer, size_t capacity);
PilotStatus pilot_memory_read_impl(uintptr_t address,
                                   void *output,
                                   size_t length);
PilotStatus pilot_memory_patch_impl(uintptr_t address,
                                    const void *expected,
                                    const void *replacement,
                                    size_t length);
PilotStatus pilot_cp_mem_read_impl(uint64_t cp_address,
                                   void *output,
                                   size_t length);
PilotStatus pilot_cp_mem_write_impl(uint64_t cp_address,
                                    const void *input,
                                    size_t length);
PilotStatus pilot_cp_mem_patch_impl(uint64_t cp_address,
                                    const void *expected,
                                    const void *replacement,
                                    size_t length);
PilotStatus pilot_cp_mem_write_async_impl(uint64_t cp_address,
                                          const void *input,
                                          size_t length,
                                          PilotCpMemCallback callback,
                                          void *user_data);
PilotStatus pilot_cp_mem_patch_async_impl(uint64_t cp_address,
                                          const void *expected,
                                          const void *replacement,
                                          size_t length,
                                          PilotCpMemCallback callback,
                                          void *user_data);
PilotStatus pilot_get_module_dir_impl(const char *module_name,
                                     char *buffer,
                                     size_t capacity);
PilotStatus pilot_module_file_read_impl(const char *module_name,
                                       const char *file_name,
                                       void *buffer,
                                       size_t capacity,
                                       size_t *length_out);
PilotStatus pilot_module_file_write_impl(const char *module_name,
                                        const char *file_name,
                                        const void *data,
                                        size_t length);
PilotStatus pilot_module_file_append_impl(const char *module_name,
                                         const char *file_name,
                                         const void *data,
                                         size_t length);
PilotStatus pilot_api_for_module(const char *module_name,
                                 const PilotApi **api_out);
PilotStatus pilot_file_read(const PilotFileApi *file,
                            const char *file_name,
                            char *buffer,
                            size_t capacity);
PilotStatus pilot_file_write(const PilotFileApi *file,
                             const char *file_name,
                             const char *text);
PilotStatus pilot_file_append(const PilotFileApi *file,
                              const char *file_name,
                              const char *text);
PilotStatus pilot_file_read_bytes(const PilotFileApi *file,
                                  const char *file_name,
                                  void *buffer,
                                  size_t capacity,
                                  size_t *length_out);
PilotStatus pilot_file_write_bytes(const PilotFileApi *file,
                                   const char *file_name,
                                   const void *data,
                                   size_t length);
PilotStatus pilot_file_append_bytes(const PilotFileApi *file,
                                    const char *file_name,
                                    const void *data,
                                    size_t length);
PilotStatus pilot_module_log_print(const PilotModuleLogApi *log,
                                   const char *message);
void pilot_load_modules(void);
void pilot_runtime_mount_worker(struct pilot_mount_args mount_args,
                                pilot_mkdir_fn mkdir_path,
                                pilot_write_file_fn write_file,
                                char drive_letter);

#endif
