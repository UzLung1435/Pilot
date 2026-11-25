/* Freestanding AArch64 writer for the Pilot appFw patch windows. */
typedef unsigned long usize;
typedef unsigned long long u64;

struct pilot_patch_window {
    u64 offset;
    usize length;
    const unsigned char *before;
    const unsigned char *after;
};

#define PILOT_AP_WINDOW_COUNT 4u
#define PILOT_AP_MAX_WINDOW_SIZE 4096u
#define PILOT_PLAN_MAX (24u + 16u * PILOT_AP_WINDOW_COUNT + 2u * PILOT_AP_WINDOW_COUNT * PILOT_AP_MAX_WINDOW_SIZE)

__attribute__((visibility("default"), used))
const unsigned int pilot_ap_generic_v1 = 0x50414731u;

static unsigned char pilot_plan[PILOT_PLAN_MAX];
static struct pilot_patch_window pilot_patch_windows[PILOT_AP_WINDOW_COUNT];
static u64 pilot_ap_file_size;

#ifndef PILOT_BOOTSTRAP_PATH
#ifdef PILOT_FLASH_TEST
#define PILOT_BOOTSTRAP_PATH "/tmp/pilotflash-test/pilot.so"
#else
#define PILOT_BOOTSTRAP_PATH "/usr/lib/pilot.so"
#endif
#endif

#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define MS_RDONLY 1
#define MS_REMOUNT 32
#define ST_VALID 32
#define ST_RDONLY 1
#define ST_RELATIME 4096

#ifdef PILOT_FLASH_TEST
#ifndef PILOT_FLASH_TEST_APPFW_PATH
#define PILOT_FLASH_TEST_APPFW_PATH "/tmp/pilotflash-test/appFw.so"
#endif
#ifndef PILOT_FLASH_MODE_PATH
#define PILOT_FLASH_MODE_PATH "/tmp/jiritsu/pilotflash.mode"
#endif
#ifndef PILOT_FLASH_RESULT_PATH
#define PILOT_FLASH_RESULT_PATH "/tmp/jiritsu/pilotflash.result"
#endif
#ifndef PILOT_FLASH_PLAN_PATH
#define PILOT_FLASH_PLAN_PATH "/tmp/jiritsu/pilotflash-plan.bin"
#endif
#ifndef PILOT_FLASH_PILOT_PATH
#define PILOT_FLASH_PILOT_PATH "/tmp/jiritsu/pilotflash-pilot.so"
#endif
#define APPFW_PATH PILOT_FLASH_TEST_APPFW_PATH
#else
#define APPFW_PATH "/usr/lib/appFw.so"
#endif

#ifdef PILOT_FLASH_TEST
#define MODE_PATH PILOT_FLASH_MODE_PATH
#define RESULT_PATH PILOT_FLASH_RESULT_PATH
#define PLAN_PATH PILOT_FLASH_PLAN_PATH
#define PILOT_SOURCE_PATH PILOT_FLASH_PILOT_PATH
#else
#define MODE_PATH "/tmp/jiritsu/pilotflash.mode"
#define RESULT_PATH "/tmp/jiritsu/pilotflash.result"
#define PLAN_PATH "/tmp/jiritsu/pilotflash-plan.bin"
#define PILOT_SOURCE_PATH "/tmp/jiritsu/pilotflash-pilot.so"
#endif

static long syscall6(long number, long a0, long a1, long a2,
                     long a3, long a4, long a5)
{
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x4 __asm__("x4") = a4;
    register long x5 __asm__("x5") = a5;
    register long x8 __asm__("x8") = number;
    __asm__ volatile("svc 0"
                     : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
                     : "memory", "cc");
    return x0;
}

static long open_file(const char *path, long flags, long mode)
{
    return syscall6(56, AT_FDCWD, (long)path, flags, mode, 0, 0);
}

static long close_file(long fd)
{
    return syscall6(57, fd, 0, 0, 0, 0, 0);
}

static long read_file(long fd, void *buffer, usize size)
{
    return syscall6(63, fd, (long)buffer, (long)size, 0, 0, 0);
}

static long write_file(long fd, const void *buffer, usize size)
{
    return syscall6(64, fd, (long)buffer, (long)size, 0, 0, 0);
}

static long pread_file(long fd, void *buffer, usize size, u64 offset)
{
    return syscall6(67, fd, (long)buffer, (long)size, (long)offset, 0, 0);
}

static long pwrite_file(long fd, const void *buffer, usize size, u64 offset)
{
    return syscall6(68, fd, (long)buffer, (long)size, (long)offset, 0, 0);
}

#ifndef PILOT_FLASH_TEST
static long statfs_file(const char *path, void *buffer)
{
    return syscall6(43, (long)path, (long)buffer, 0, 0, 0, 0);
}
#endif

static long sync_file(long fd)
{
    return syscall6(82, fd, 0, 0, 0, 0, 0);
}

static long file_size(long fd)
{
    return syscall6(62, fd, 0, 2, 0, 0, 0);
}

static int pread_exact(long fd, void *buffer, usize size, u64 offset)
{
    unsigned char *cursor = buffer;
    usize total = 0;
    while (total < size) {
        long count = pread_file(fd, cursor + total, size - total, offset + total);
        if (count <= 0) return 0;
        total += (usize)count;
    }
    return 1;
}

static int pwrite_exact(long fd, const void *buffer, usize size, u64 offset)
{
    const unsigned char *cursor = buffer;
    usize total = 0;
    while (total < size) {
        long count = pwrite_file(fd, cursor + total, size - total, offset + total);
        if (count <= 0) return 0;
        total += (usize)count;
    }
    return 1;
}

static u64 plan_uint(const unsigned char *data, usize width)
{
    u64 value = 0;
    for (usize i = 0; i < width; ++i) value |= (u64)data[i] << (8u * i);
    return value;
}

static int load_patch_plan(void)
{
    static const unsigned char magic[8] = {'P','I','L','O','T','A','P','1'};
    long fd = open_file(PLAN_PATH, O_RDONLY, 0);
    if (fd < 0) return 0;
    long size = file_size(fd);
    if (size < 88 || size > (long)sizeof(pilot_plan) ||
        !pread_exact(fd, pilot_plan, (usize)size, 0)) {
        (void)close_file(fd);
        return 0;
    }
    (void)close_file(fd);
    for (usize i = 0; i < sizeof(magic); ++i)
        if (pilot_plan[i] != magic[i]) return 0;
    if (plan_uint(pilot_plan + 8, 4) != 1 ||
        plan_uint(pilot_plan + 12, 4) != PILOT_AP_WINDOW_COUNT) return 0;
    pilot_ap_file_size = plan_uint(pilot_plan + 16, 8);
    if (!pilot_ap_file_size) return 0;
    usize payload = 24u + 16u * PILOT_AP_WINDOW_COUNT;
    for (usize i = 0; i < PILOT_AP_WINDOW_COUNT; ++i) {
        const unsigned char *row = pilot_plan + 24u + 16u * i;
        u64 offset = plan_uint(row, 8);
        u64 length = plan_uint(row + 8, 4);
        if (plan_uint(row + 12, 4) != 0 || !length || length > PILOT_AP_MAX_WINDOW_SIZE ||
            offset > pilot_ap_file_size || length > pilot_ap_file_size - offset ||
            payload > (usize)size || length * 2u > (usize)size - payload) return 0;
        for (usize j = 0; j < i; ++j) {
            u64 other = pilot_patch_windows[j].offset;
            u64 other_end = other + pilot_patch_windows[j].length;
            if (offset < other_end && other < offset + length) return 0;
        }
        pilot_patch_windows[i].offset = offset;
        pilot_patch_windows[i].length = (usize)length;
        pilot_patch_windows[i].before = pilot_plan + payload;
        pilot_patch_windows[i].after = pilot_plan + payload + (usize)length;
        payload += (usize)length * 2u;
    }
    return payload == (usize)size;
}

static u64 usr_mount_flags;
static u64 usr_filesystem_magic;
static long usr_statfs_result;
static int usr_statfs_stage;
#ifndef PILOT_FLASH_TEST
static int usr_mount_changed;
#endif

#ifndef PILOT_FLASH_TEST
static u64 read_u64_le(const unsigned char *buffer)
{
    u64 value = 0;
    for (unsigned int i = 0; i < 8; ++i)
        value |= (u64)buffer[i] << (i * 8);
    return value;
}
#endif

static int read_usr_mount_flags(void)
{
#ifdef PILOT_FLASH_TEST
    usr_statfs_result = 0;
    usr_filesystem_magic = 0xef53ULL;
    usr_mount_flags = ST_RDONLY | ST_VALID | ST_RELATIME;
#else
    unsigned char info[120];
    long result = statfs_file("/usr", info);
    usr_statfs_result = result;
    if (result < 0) {
        usr_statfs_stage = 1;
        return (int)result;
    }
    usr_filesystem_magic = read_u64_le(info);
    usr_mount_flags = read_u64_le(info + 80);
#endif
    if (usr_filesystem_magic != 0xef53ULL) {
        usr_statfs_stage = 2;
        return -1;
    }
    if (usr_mount_flags & ~(1ULL | 2ULL | 4ULL | 8ULL | 16ULL |
                            ST_VALID | 64ULL | 1024ULL | 2048ULL |
                            ST_RELATIME)) {
        usr_statfs_stage = 3;
        return -1;
    }
    usr_statfs_stage = 0;
    return 0;
}

static u64 usr_remount_options(int readonly)
{
    u64 flags = MS_REMOUNT | (readonly ? MS_RDONLY : 0);
    flags |= usr_mount_flags & (2ULL | 4ULL | 8ULL | 16ULL | 64ULL |
                                1024ULL | 2048ULL);
    if (usr_mount_flags & ST_RELATIME) flags |= 1ULL << 21;
    return flags;
}

static long remount_usr_rw(void)
{
#ifdef PILOT_FLASH_TEST
    (void)read_usr_mount_flags();
    (void)usr_remount_options(0);
    return 0;
#else
    int result = read_usr_mount_flags();
    if (result < 0) return result;
    if (!(usr_mount_flags & ST_RDONLY)) return 0;
    result = (int)syscall6(40, 0, (long)"/usr", 0,
                           (long)usr_remount_options(0), 0, 0);
    if (result == 0) usr_mount_changed = 1;
    return result;
#endif
}

static long remount_usr_ro(void)
{
#ifdef PILOT_FLASH_TEST
    (void)usr_remount_options(1);
    return 0;
#else
    if (!usr_mount_changed) return 0;
    long result = syscall6(40, 0, (long)"/usr", 0,
                          (long)usr_remount_options(1), 0, 0);
    if (result == 0) usr_mount_changed = 0;
    return result;
#endif
}

static int bytes_equal(const unsigned char *left,
                       const unsigned char *right, usize size)
{
    for (usize index = 0; index < size; ++index)
        if (left[index] != right[index]) return 0;
    return 1;
}

static int text_is(const char *left, const char *right)
{
    usize index = 0;
    while (left[index] && right[index] && left[index] == right[index]) ++index;
    return left[index] == 0 && right[index] == 0;
}

static void emit_result(const char *message)
{
    static const char header[] = "A6700PILOTFLASH\n";
    char output[256];
    usize length = 0;
    for (usize i = 0; i < sizeof(header) - 1; ++i) output[length++] = header[i];
    for (usize i = 0; message[i] && length + 1 < sizeof(output); ++i)
        output[length++] = message[i];
    if (length && output[length - 1] != '\n') output[length++] = '\n';

    long fd = open_file(RESULT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    long written = write_file(fd, output, length);
    (void)written;
    (void)sync_file(fd);
    (void)close_file(fd);
}

static usize append_text(char *output, usize length, const char *text)
{
    while (*text) output[length++] = *text++;
    return length;
}

static usize append_signed(char *output, usize length, long value)
{
    unsigned long magnitude;
    if (value < 0) {
        output[length++] = '-';
        magnitude = 0UL - (unsigned long)value;
    } else {
        magnitude = (unsigned long)value;
    }
    char digits[24];
    usize count = 0;
    do {
        digits[count++] = (char)('0' + magnitude % 10UL);
        magnitude /= 10UL;
    } while (magnitude && count < sizeof(digits));
    while (count) output[length++] = digits[--count];
    return length;
}

static usize append_hex(char *output, usize length, u64 value)
{
    static const char digits[] = "0123456789abcdef";
    int started = 0;
    length = append_text(output, length, "0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned int nibble = (unsigned int)((value >> shift) & 0xfULL);
        if (nibble || started || shift == 0) {
            output[length++] = digits[nibble];
            started = 1;
        }
    }
    return length;
}

static void emit_mount_failure(const char *reason, long result)
{
    static const char header[] = "A6700PILOTFLASH\n";
    char output[256];
    usize length = 0;
    for (usize i = 0; i < sizeof(header) - 1; ++i) output[length++] = header[i];
    length = append_text(output, length, "RESULT=FAIL\nreason=");
    length = append_text(output, length, reason);
    length = append_text(output, length, "\noperation_rc=");
    length = append_signed(output, length, result);
    length = append_text(output, length, "\nstatfs_stage=");
    length = append_signed(output, length, usr_statfs_stage);
    length = append_text(output, length, "\nstatfs_rc=");
    length = append_signed(output, length, usr_statfs_result);
    length = append_text(output, length, "\nfilesystem_magic=");
    length = append_hex(output, length, usr_filesystem_magic);
    length = append_text(output, length, "\nmount_flags=");
    length = append_hex(output, length, usr_mount_flags);
    output[length++] = '\n';

    long fd = open_file(RESULT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    (void)write_file(fd, output, length);
    (void)sync_file(fd);
    (void)close_file(fd);
}

static int read_mode(char *mode, usize capacity)
{
    long fd = open_file(MODE_PATH, O_RDONLY, 0);
    if (fd < 0) return 0;
    usize total = 0;
    while (total < capacity - 1) {
        long count = read_file(fd, mode + total, capacity - 1 - total);
        if (count < 0) {
            (void)close_file(fd);
            return 0;
        }
        if (count == 0) break;
        total += (usize)count;
    }
    (void)close_file(fd);
    if (total == 0 || total >= capacity - 1) return 0;
    mode[total] = 0;
    while (total > 0 && (mode[total - 1] == '\n' || mode[total - 1] == '\r'))
        mode[--total] = 0;
    return 1;
}

/* Return 1 for the recorded camera baseline, 2 for Pilot, or 0 if unknown. */
static int image_state(long fd)
{
    unsigned char buffer[PILOT_AP_MAX_WINDOW_SIZE];
    int all_baseline = 1;
    int all_patched = 1;
    for (usize window_index = 0; window_index < PILOT_AP_WINDOW_COUNT; ++window_index) {
        const struct pilot_patch_window *window = &pilot_patch_windows[window_index];
        if (window->length > sizeof(buffer)) return 0;
        if (!pread_exact(fd, buffer, window->length, window->offset)) return 0;
        if (!bytes_equal(buffer, window->before, window->length)) all_baseline = 0;
        if (!bytes_equal(buffer, window->after, window->length)) all_patched = 0;
    }
    if (all_baseline) return 1;
    if (all_patched) return 2;
    return 0;
}

static int write_state(long fd, int target_patched)
{
    static const usize install_order[] = {1, 2, 3, 0};
    static const usize restore_order[] = {0, 1, 2, 3};
    const usize *order = target_patched ? install_order : restore_order;
    for (usize order_index = 0; order_index < PILOT_AP_WINDOW_COUNT; ++order_index) {
        usize window_index = order[order_index];
        const struct pilot_patch_window *window = &pilot_patch_windows[window_index];
        const unsigned char *bytes = target_patched ? window->after : window->before;
        if (!pwrite_exact(fd, bytes, window->length, window->offset)) return 0;
    }
    return sync_file(fd) == 0;
}

/* Install the staged bootstrap before enabling its AP entry point. */
static long install_bootstrap_mode(int allow_replace)
{
    unsigned char source_buffer[1024], target_buffer[1024];
    long source = open_file(PILOT_SOURCE_PATH, O_RDONLY, 0);
    if (source < 0) return source;
    long source_size = file_size(source);
    if (source_size < 64 || source_size > 65536) {
        (void)close_file(source);
        return -1005;
    }
    long fd = open_file(PILOT_BOOTSTRAP_PATH, O_RDONLY, 0);
    if (fd >= 0) {
        int matches = file_size(fd) == source_size;
        for (usize off = 0; matches && off < (usize)source_size;) {
            usize count = (usize)source_size - off;
            if (count > sizeof(source_buffer)) count = sizeof(source_buffer);
            matches = pread_exact(source, source_buffer, count, off) &&
                      pread_exact(fd, target_buffer, count, off) &&
                      bytes_equal(source_buffer, target_buffer, count);
            off += count;
        }
        (void)close_file(fd);
        if (matches) { (void)close_file(source); return 0; }
        if (!allow_replace) { (void)close_file(source); return -1001; }
        fd = open_file(PILOT_BOOTSTRAP_PATH, O_RDWR | O_TRUNC, 0644);
    } else {
        if (fd != -2) { (void)close_file(source); return fd; }
        fd = open_file(PILOT_BOOTSTRAP_PATH, O_RDWR | O_CREAT | 128, 0644);
    }
    if (fd < 0) { (void)close_file(source); return fd; }
    int ok = 1;
    for (usize off = 0; ok && off < (usize)source_size;) {
        usize count = (usize)source_size - off;
        if (count > sizeof(source_buffer)) count = sizeof(source_buffer);
        ok = pread_exact(source, source_buffer, count, off) &&
             pwrite_exact(fd, source_buffer, count, off);
        off += count;
    }
    long result = ok ? sync_file(fd) : -1002;
    if (!result && file_size(fd) != source_size) result = -1003;
    for (usize off = 0; !result && off < (usize)source_size;) {
        usize count = (usize)source_size - off;
        if (count > sizeof(source_buffer)) count = sizeof(source_buffer);
        if (!pread_exact(source, source_buffer, count, off) ||
            !pread_exact(fd, target_buffer, count, off) ||
            !bytes_equal(source_buffer, target_buffer, count)) result = -1004;
        off += count;
    }
    (void)close_file(fd);
    (void)close_file(source);
    return result;
}

static long install_bootstrap(void)
{
    return install_bootstrap_mode(0);
}

static void run_update_pilot(void)
{
    long rw_result = remount_usr_rw();
    if (rw_result < 0) {
        emit_mount_failure(usr_statfs_stage ? "statfs_preflight" : "remount_rw",
                           rw_result);
        return;
    }
    long bootstrap_result = install_bootstrap_mode(1);
    long ro_result = remount_usr_ro();
    if (bootstrap_result) {
        emit_mount_failure(ro_result < 0 ? "bootstrap_and_remount_ro" : "bootstrap_install",
                           bootstrap_result);
        return;
    }
    if (ro_result < 0) {
        emit_result("RESULT=FAIL\nreason=remount_ro\nrecovery=do_not_reboot");
        return;
    }
    emit_result("RESULT=OK\nmode=pilot\nfsync=0\nreadback=OK\nremount_ro=0");
}

static void run_check(const char *mode)
{
    long fd = open_file(APPFW_PATH, O_RDONLY, 0);
    if (fd < 0) {
        emit_result("RESULT=FAIL\nreason=open_read");
        return;
    }
    if ((u64)file_size(fd) != pilot_ap_file_size) {
        (void)close_file(fd);
        emit_result("RESULT=FAIL\nreason=file_size");
        return;
    }
    int state = image_state(fd);
    (void)close_file(fd);
    if (!state) {
        emit_result("RESULT=FAIL\nreason=patch_windows_unknown");
        return;
    }
    if (text_is(mode, "check")) {
        emit_result(state == 1
                        ? "RESULT=OK\nmode=check\nstate=baseline"
                        : "RESULT=OK\nmode=check\nstate=patched");
    }
}

static void run_write(const char *mode, int target_patched)
{
    long read_fd = open_file(APPFW_PATH, O_RDONLY, 0);
    if (read_fd < 0) {
        emit_result("RESULT=FAIL\nreason=open_read");
        return;
    }
    if ((u64)file_size(read_fd) != pilot_ap_file_size) {
        (void)close_file(read_fd);
        emit_result("RESULT=FAIL\nreason=file_size");
        return;
    }
    int before_state = image_state(read_fd);
    (void)close_file(read_fd);

    int desired_state = target_patched ? 2 : 1;
    if (before_state == desired_state && !target_patched) {
        emit_result(target_patched
                        ? "RESULT=OK\nmode=write\nstate=patched\nalready=1"
                        : "RESULT=OK\nmode=restore\nstate=baseline\nalready=1");
        return;
    }
    int required_state = before_state;
    if (before_state != 1 && before_state != 2) {
        emit_result("RESULT=FAIL\nreason=patch_windows_unknown");
        return;
    }

    long rw_result = remount_usr_rw();
    if (rw_result < 0) {
        emit_mount_failure(usr_statfs_stage ? "statfs_preflight" : "remount_rw",
                           rw_result);
        return;
    }

    if (target_patched) {
        long bootstrap_result = install_bootstrap();
        if (bootstrap_result) {
            long ro_result = remount_usr_ro();
            emit_mount_failure(ro_result < 0 ? "bootstrap_and_remount_ro" : "bootstrap_install",
                               bootstrap_result);
            return;
        }
    }

    long fd = open_file(APPFW_PATH, O_RDWR, 0);
    if (fd < 0) {
        long ro_result = remount_usr_ro();
        emit_result(ro_result < 0
                        ? "RESULT=FAIL\nreason=open_rw_and_remount_ro"
                        : "RESULT=FAIL\nreason=open_rw");
        return;
    }

    int state_after_open = image_state(fd);
    if (state_after_open != required_state) {
        (void)close_file(fd);
        long ro_result = remount_usr_ro();
        emit_result(ro_result < 0
                        ? "RESULT=FAIL\nreason=state_changed_and_remount_ro"
                        : "RESULT=FAIL\nreason=state_changed");
        return;
    }

    int write_ok = write_state(fd, target_patched);
    int state_after_write = image_state(fd);
    if (!write_ok || state_after_write != desired_state) {
        int rollback_ok = write_state(fd, required_state == 2);
        int rollback_state = image_state(fd);
        (void)close_file(fd);
        long ro_result = remount_usr_ro();
        if (!rollback_ok || rollback_state != required_state)
            emit_result("RESULT=FAIL\nreason=rollback_failed\nrecovery=do_not_reboot");
        else if (ro_result < 0)
            emit_result("RESULT=FAIL\nreason=remount_ro_after_rollback\nstate=restored");
        else
            emit_result("RESULT=FAIL\nreason=write_or_readback\nstate=restored");
        return;
    }

    (void)close_file(fd);
    long ro_result = remount_usr_ro();
    if (ro_result < 0) {
        emit_result("RESULT=FAIL\nreason=remount_ro\nstate=patched\nrecovery=do_not_reboot");
        return;
    }
    emit_result(target_patched
                    ? "RESULT=OK\nmode=write\nstate=patched\nwindows=4\nfsync=0\nreadback=OK\nremount_ro=0"
                    : "RESULT=OK\nmode=restore\nstate=baseline\nwindows=4\nfsync=0\nreadback=OK\nremount_ro=0");
    (void)mode;
}

__attribute__((visibility("default"), used))
void _init(void)
{
    char mode[24];
    if (!read_mode(mode, sizeof(mode))) {
        emit_result("RESULT=FAIL\nreason=no_mode");
        return;
    }
    if ((text_is(mode, "check") || text_is(mode, "write") || text_is(mode, "restore")) &&
        !load_patch_plan()) {
        emit_result("RESULT=FAIL\nreason=invalid_plan");
        return;
    }
    if (text_is(mode, "check")) {
        run_check(mode);
    } else if (text_is(mode, "write")) {
        run_write(mode, 1);
    } else if (text_is(mode, "restore")) {
        run_write(mode, 0);
    } else if (text_is(mode, "pilot")) {
        run_update_pilot();
    } else {
        emit_result("RESULT=FAIL\nreason=unknown_mode");
    }
}
