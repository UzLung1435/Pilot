/* Host-side check of the A7C2 menu-group injection arithmetic.
 *
 * Reads the first 0x3000 bytes of the real A7C2 menu table (dumped from
 * appFw.so at VA 0x051DF8C8) and replays exactly what
 * install_menu_group() does in pilot_menu_ui_a7c2.c, so the offsets can be
 * validated without a camera.
 *
 * Regenerate the dump with IDAPython on appFw.so (ILCE-7CM2 2.01):
 *   open('a7c2_menu_table_head.bin','wb').write(get_bytes(0x051DF8C8, 0x3000))
 * then: gcc -std=c11 -o t test_menu_table_layout.c && ./t a7c2_menu_table_head.bin
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_HEAD_SIZE      0x08u
#define CAT_SIZE             0x2B30u
#define CAT_COUNT_OFF        0x08u
#define CAT_GROUP_OFF        0x10u
#define GROUP_SIZE           0x2E0u
#define GROUP_INDEX_OFF      0x00u
#define GROUP_RID_OFF        0x04u
#define GROUP_ROWS_OFF       0x08u
#define GROUP_ENTRY_OFF      0x10u
#define ROW_SIZE             24u
#define MAX_ROWS             30u

/* Expected absolute layout of the real firmware image, used as a cross-check. */
#define FW_TABLE_VA          0x051DF8C8u
#define FW_CAT0_VA           0x051DF8D0u
#define FW_SLOT1_VA          0x051DFBC0u

static uint32_t get_u32(const uint8_t *base, size_t off)
{
    uint32_t value;
    memcpy(&value, base + off, sizeof(value));
    return value;
}

static void put_u32(uint8_t *base, size_t off, uint32_t value)
{
    memcpy(base + off, &value, sizeof(value));
}

static int failures;

static void check(int cond, const char *what, long long got, long long want)
{
    if (cond) {
        printf("  ok    %-42s 0x%llx\n", what, (unsigned long long)got);
    } else {
        printf("  FAIL  %-42s 0x%llx (want 0x%llx)\n", what,
               (unsigned long long)got, (unsigned long long)want);
        ++failures;
    }
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "a7c2_menu_table_head.bin";
    uint8_t *table;
    uint8_t *cat0;
    uint8_t *group;
    uint8_t zero[GROUP_SIZE];
    uint32_t cat_count;
    size_t i;
    FILE *fp;
    long size;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0x1000) {
        fprintf(stderr, "%s is too small (%ld bytes)\n", path, size);
        fclose(fp);
        return 1;
    }
    table = calloc(1, (size_t)size);
    if (!table || fread(table, 1, (size_t)size, fp) != (size_t)size) {
        fprintf(stderr, "short read on %s\n", path);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    printf("table dump: %s (%ld bytes)\n", path, size);

    printf("\n[1] table header\n");
    check(get_u32(table, 0x00u) == 8u, "category count", get_u32(table, 0x00u),
          8);
    check(get_u32(table, 0x04u) == 0u, "reserved", get_u32(table, 0x04u), 0);

    printf("\n[2] category 0 header\n");
    cat0 = table + TABLE_HEAD_SIZE;
    check((unsigned)((uintptr_t)cat0 - (uintptr_t)table) ==
              FW_CAT0_VA - FW_TABLE_VA,
          "cat0 offset", (long long)((uintptr_t)cat0 - (uintptr_t)table), 0x08);
    check(get_u32(cat0, 0x00u) == 0u, "cat flag", get_u32(cat0, 0x00u), 0);
    check(get_u32(cat0, 0x04u) == 0x2006451u, "cat title RID",
          get_u32(cat0, 0x04u), 0x2006451);
    cat_count = get_u32(cat0, CAT_COUNT_OFF);
    check(cat_count == 1u, "group count", cat_count, 1);

    printf("\n[3] target slot\n");
    group = cat0 + CAT_GROUP_OFF + (size_t)cat_count * GROUP_SIZE;
    check((unsigned)((uintptr_t)group - (uintptr_t)table) ==
              FW_SLOT1_VA - FW_TABLE_VA,
          "slot offset", (long long)((uintptr_t)group - (uintptr_t)table),
          FW_SLOT1_VA - FW_TABLE_VA);
    memset(zero, 0, sizeof(zero));
    check(memcmp(group, zero, sizeof(zero)) == 0, "slot is all zero", 1, 1);

    printf("\n[4] replay the injection\n");
    put_u32(group, GROUP_INDEX_OFF, 0x3Fu);
    put_u32(group, GROUP_RID_OFF, 0x02FFFF00u);
    put_u32(group, GROUP_ROWS_OFF, 4u);
    for (i = 0; i < 4u; ++i) {
        size_t row = GROUP_ENTRY_OFF + i * ROW_SIZE;
        memcpy(group + row, &(uint64_t){0x1F00u + i}, 8);
        memcpy(group + row + 16u, &(uint64_t){0}, 8);
    }
    put_u32(cat0, CAT_COUNT_OFF, cat_count + 1u);

    check(get_u32(cat0, CAT_COUNT_OFF) == 2u, "group count after insert",
          get_u32(cat0, CAT_COUNT_OFF), 2);
    check(get_u32(group, GROUP_INDEX_OFF) == 0x3Fu, "group index",
          get_u32(group, GROUP_INDEX_OFF), 0x3f);
    check(get_u32(group, GROUP_RID_OFF) == 0x02FFFF00u, "group name RID",
          get_u32(group, GROUP_RID_OFF), 0x02ffff00);
    check(get_u32(group, GROUP_ROWS_OFF) == 4u, "group rows",
          get_u32(group, GROUP_ROWS_OFF), 4);

    printf("\n[5] neighbours untouched\n");
    /* group slot 0 must still look like a group: index 0x3e, six rows */
    check(get_u32(cat0, CAT_GROUP_OFF) == 0x3eu, "existing group index",
          get_u32(cat0, CAT_GROUP_OFF), 0x3e);
    check(get_u32(cat0, CAT_GROUP_OFF + GROUP_ROWS_OFF) == 6u,
          "existing group rows", get_u32(cat0, CAT_GROUP_OFF + GROUP_ROWS_OFF),
          6);
    /* 15 slots fit exactly between the category header and the next one */
    check((unsigned)((uintptr_t)(cat0 + CAT_GROUP_OFF + 15u * GROUP_SIZE) -
                     (uintptr_t)table) ==
              FW_CAT0_VA - FW_TABLE_VA + 0x2B30u,
          "slot 14 ends at cat1 header",
          (long long)((uintptr_t)(cat0 + CAT_GROUP_OFF + 15u * GROUP_SIZE) -
                      (uintptr_t)table),
          FW_CAT0_VA - FW_TABLE_VA + 0x2B30u);
    check(memcmp(cat0 + CAT_GROUP_OFF + 14u * GROUP_SIZE, zero, GROUP_SIZE) == 0,
          "slot 14 is all zero", 1, 1);

    free(table);
    printf("\nRESULT: %s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
