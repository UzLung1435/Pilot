#ifndef PILOT_A7C2_MENU_H
#define PILOT_A7C2_MENU_H

/* ILCE-7CM2 (A7C II) 2.01 primary-menu port of the Pilot menu UI.
 *
 * The A6700 implementation in ../pilot_menu_ui.c drives the original menu
 * through the Pilot page/item tables, a synthetic group and two replacement
 * screens.  That layout does not exist on this body.  The A7C II stores a
 * fixed page-descriptor array and a 64-bit-keyed item cache instead, so this
 * backend inserts one extra row into the stock "custom" group and takes over
 * the native list screen the row opens (screen 0x2cb).
 *
 * Every address below was recovered from the A7C II 2.01 appFw.so and is
 * validated before it is used: an unexpected byte anywhere in the preflight
 * makes the whole install fail instead of patching a different firmware.
 *
 * Addresses are relative to the appFw.so load base (dlpi_addr).  Mapped
 * virtual addresses differ from file offsets by 0x10000 for this image, which
 * only matters when the offsets are checked offline against the DSO. */

#include <stddef.h>
#include <stdint.h>

/* --- entry row ---------------------------------------------------------- */
/* Synthetic raw item id and its title resource.  0x601 is unused by 2.01. */
#define NEW_ITEM 0x601u
#define NEW_TITLE 0x02fefff0u
/* Stock page descriptor used as the template for every owned row. */
#define INFO_SCREEN 0x220u
#define INFO_RECORD 0x5a21d80ul
/* Stock list screen the row opens.  The transport takes it over. */
#define PILOT_LIST_SCREEN 0x2cbu

/* --- primary menu model ------------------------------------------------- */
#define PRIMARY_CACHE 0x7cf5d68ul
#define PRESENTER_CACHE 0x7cee700ul
#define ITEM_PRESENTER_CACHE 0x7cfd510ul
#define GROUP7 (PRIMARY_CACHE + 8u + 7u * 0xec8u)
#define PAGE12 (PRIMARY_CACHE + 12u + 7u * 0xec8u + 12u * 0xfcu)
#define ITEM7 (PAGE12 + 12u + 7u * 8u)
#define CURRENT_ITEM 0x7e68aa8ul
#define ORIGINAL_VALUE 0x7e82e70ul
#define CATALOG_SERIAL 0x7602fc8ul
#define CATALOG_COUNT 0x7603118ul
#define PRESENTER_TABLE 0x7603168ul
#define CONTROLLER_ARENA 0x7603190ul
#define PRESENTER_VTABLE 0x5b08c00ul
#define CONTROLLER_VTABLE 0x5b20620ul
#define ITEM_UTIL_VTABLE 0x597d578ul

/* --- hook sites --------------------------------------------------------- */
#define TEXT_UINT_SLOT 0x5bc1fe8ul
#define FACTORY_SLOT 0x5204410ul
#define ROOT_ENTER_SLOT 0x5b13598ul
#define PAGE_VALUE_CALL 0x2001eb4ul
#define PAGE_FORMAT_CALL 0x1e0f990ul
#define ALIAS_CALL 0x1b6dcd8ul
#define REVERSE_CALL 0x1b6de94ul

/* --- stock functions --------------------------------------------------- */
#define F_ALIAS 0x1b66d8cul
#define F_NORMAL 0x1b6ea38ul
#define F_REVERSE 0x1b66fd8ul
#define F_BUILD 0x1b5ac70ul
#define F_FORMATTER 0x23669cul
#define F_ROOT_ENTER 0x217cbd8ul
#define F_PRESENTER_UPDATE 0x1e0f8d4ul
#define F_PRESENTER_REBUILD 0x1c772a0ul
#define F_MODEL_LOCK 0x2e06cc0ul
#define F_MODEL_UNLOCK 0x2e068ccul
#define F_INFO_TITLE_GETTER 0x1bf20e0ul
#define F_NEW_NORMAL 0x597ee10ul
#define NEW_ALIAS 0x602u
#define NEW_REVERSE 0xb81u

/* --- key codes --------------------------------------------------------- */
#define ENTER_ON 0x0201000cu
#define ENTER_OFF 0x0201000du
#define MENU_ON 0x0201000fu
#define LEFT_ON 0x02010006u
#define RIGHT_ON 0x02010009u

/* --- TextSprite::setString --------------------------------------------- */
#define LIBOB_NAME "libObj.so"
#define LIBOB_SET_ID "_ZN2ux6gfxeng10TextSprite9setStringEj"
#define LIBOB_SET_BYTES "_ZN2ux6gfxeng10TextSprite9setStringEPKc"
#define LIBOB_SET_ID_OFF 0x171944ul
#define LIBOB_SET_BYTES_OFF 0x171750ul

typedef void *(*factory_fn)(int32_t, void *, void *, int32_t, int32_t, int32_t);
typedef uint64_t (*member_fn)(void *, const void *);
typedef void (*exit_fn)(void *);
typedef void (*rebuild_fn)(void *);
typedef void (*update_fn)(void *, uint8_t);
typedef int32_t (*alias_fn)(void *, int32_t);
typedef const void *(*normal_fn)(void *, int32_t);
typedef uint32_t (*reverse_fn)(void *, int32_t);
typedef void (*build_fn)(void *, int32_t, void *);
typedef void (*text_id_fn)(void *, uint32_t);
typedef void (*text_bytes_fn)(void *, const char *);
typedef void (*model_lock_fn)(void *);

/* Diagnostics.  Written by the hooks, read by the host-side verifier and by
 * the "status" page of the menu itself.  Layout is part of the ABI: keep the
 * magic when changing it. */
struct pilot_a7c2_observation {
    uint32_t magic, abi_version;
    uint32_t installed, install_status, patches;
    uint32_t ui_tid, screen, primary_rows, generation;
    uint32_t value_reads, refreshes, confirm_events;
    uint32_t last_error, last_status;
    uint64_t appfw_base, presenter;
};
_Static_assert(sizeof(struct pilot_a7c2_observation) == 72, "observation ABI v1");

/* Native list transport state.  Mirrors the reference implementation's
 * observation record field for field so the host verifier and the reference
 * trace decoder keep working unchanged. */
struct pilot_a7c2_list_observation {
    uint32_t magic, abi_version;
    uint64_t controller, presenter;
    int64_t value;
    uint32_t parent, focused, count, generation;
    uint32_t editing, actions, last_error, scope_event;
    uint32_t depth, refreshes, enters, controller_dtors;
    uint32_t presenter_dtors, in_flight, prepared;
    int32_t callback_code;
    uint32_t last_status, reserved;
};
_Static_assert(sizeof(struct pilot_a7c2_list_observation) == 104, "list observation ABI v2");

#define PILOT_A7C2_MENU_MAGIC 0x314d4c50u /* "PLM1" */
#define PILOT_A7C2_LIST_MAGIC 0x31534c4du /* "PLS1" */
#define MENU_API_EXPORT

/* Root container label and the transport entry point, both defined by the
 * model layer (pilot_menu_model.inc) and consumed by the native list port. */
const char *pilot_menu_title(void);

#endif /* PILOT_A7C2_MENU_H */
