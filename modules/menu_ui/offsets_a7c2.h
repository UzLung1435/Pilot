/*
 * Sony ILCE-7CM2 (A7C II) firmware 2.01 -- appFw.so addresses used by the
 * Pilot menu_ui module.
 *
 * Every value below was re-derived from the IDA database of
 *   D:\Code\Sony_Cam_Compare\appFw.so.i64
 * (root file appFw.so, 99,960,744 bytes, imagebase 0, 333,346 functions).
 * The model/version were confirmed from
 *   oldProjects\FlashDir\backup1\state.json  ->  "ILCE-7CM2" / "2.01"
 *
 * Status legend:
 *   [OK]      verified by disassembly / structure inspection
 *   [GUESS]   plausible but not yet exercised on hardware
 *   [TODO]    still unknown
 *
 * Do not trust any other document in this directory about these numbers.
 * See a7c2_reverse_notes.md for the derivation of each entry.
 */

#ifndef PILOT_MENU_UI_OFFSETS_A7C2_H
#define PILOT_MENU_UI_OFFSETS_A7C2_H

/* ------------------------------------------------------------------ */
/* Global offset table (GOT) slots                                     */
/*                                                                     */
/* All offsets are "appFw.so file offsets", i.e. the runtime address   */
/* minus the appFw load base.                                          */
/* ------------------------------------------------------------------ */

/*
 * [OK] Screen (page) table -- equals APPFW_GOT_PAGE_TABLE on the A6700.
 *
 *   GOT slot 0x05B4CFD0 -> 0x051F7810 (.data.rel.ro)
 *   entry(sid) = 0x051F7810 + 0x60 * sid
 *
 * struct a7c2_page_entry {              stride 0x60
 *     void *presenter_getter;           +0x00   (cache lookup only)
 *     void *controller_getter;          +0x08   (constructs on demand)
 *     ...
 *     uint32_t screen_id;               +0x20   low 32-bit == index
 *     uint32_t screen_type;             +0x24   0x03 for 0x293 and 0x1C3
 *     void *trailing_getter;            +0x58
 * };
 *
 * 837 entries carry a matching screen id in 0x000..0x35E; slots up to at
 * least 0x6FF are still zero, so the array can be copied and extended the
 * same way the A6700 page table is.
 *
 * Screen 0x293 @0x05206F30 -> presenter 0x01D54644, controller 0x01CF0F20
 * Screen 0x1C3 @0x05202130 -> presenter 0x01DB0540, controller 0x01CD8478
 */
#define APPFW_GOT_PAGE_TABLE         0x05B4CFD0ull
#define APPFW_PAGE_TABLE_OFF         0x051F7810ull
#define APPFW_PAGE_ENTRY_SIZE        0x60ull
#define APPFW_PAGE_STOCK_COUNT       0x35Full
#define APPFW_PAGE_PRESENTER_OFF     0x00ull
#define APPFW_PAGE_CONTROLLER_OFF    0x08ull
#define APPFW_PAGE_SID_OFF           0x20ull

/*
 * [OK] Menu group table -- equals APPFW_GOT_MENU_TABLE / APPFW_MENU_TABLE_OFF
 * on the A6700, i.e. this is the table the A6700 module copies and appends
 * its own group to.  The A7C2 table is smaller but structurally identical.
 *
 *   GOT slot 0x05B80EC0 -> 0x051DF8C8 (.data.rel.ro), size 0x15480
 *
 * Layout:
 *   table header, 0x18 bytes at 0x051DF8C8:
 *           +0x00  u32  category count (8)
 *           +0x04  u32  0
 *           +0x08  u32  0
 *           +0x0C  u32  table title RID (0x02006451)
 *           +0x10  u32  1
 *           +0x14  u32  0
 *   Category 0 header is at table + 0x08 (0x051DF8D0), confirmed by
 *   sub_1B59648 returning &dword_51DF8D0 + 0x2B30 * category.
 *
 *   category stride 0x2B30, 8 of them:
 *           +0x00  u32  flag (0x08 in category 0, 0 elsewhere)
 *           +0x04  u32  0
 *           +0x08  u32  group count   <- sub_1B5A24C reads *(cat - 2)
 *           +0x0C  u32  0
 *           +0x10  15 group slots of 0x2E0 each (0x2B20 bytes total)
 *
 *   group slot stride 0x2E0:
 *           +0x00  u32  global group index (0x3e next to 0x40, 0x3f free)
 *           +0x04  u32  group name RID
 *           +0x08  u32  row count
 *           +0x0C  u32  0
 *           +0x10  row_count * 24 bytes:
 *                  { u64 group_id; u64 setting_desc; u64 non_setting_desc; }
 *
 * Category 0 (0x051DF8D0) has count=1 and group slots 1..14 are all zero,
 * exactly the "append a group here" situation the A6700 module relies on.
 */
#define APPFW_GOT_MENU_TABLE         0x05B80EC0ull
#define APPFW_MENU_TABLE_OFF         0x051DF8C8ull
#define APPFW_MENU_TABLE_SIZE        0x15480ull
#define APPFW_MENU_CAT_COUNT         8u
#define APPFW_MENU_CAT_SIZE          0x2B30ull
#define APPFW_MENU_CAT_GROUPS        15u
#define APPFW_MENU_CAT_FLAG_OFF      0x00ull
#define APPFW_MENU_CAT_BASE_OFF      0x08ull
#define APPFW_MENU_CAT_COUNT_OFF     0x08ull
#define APPFW_MENU_CAT_GROUP_OFF     0x10ull
#define APPFW_MENU_GROUP_SIZE        0x2E0ull
#define APPFW_MENU_GROUP_INDEX_OFF   0x00ull
#define APPFW_MENU_GROUP_RID_OFF     0x04ull
#define APPFW_MENU_GROUP_ROWS_OFF    0x08ull
#define APPFW_MENU_GROUP_ENTRY_OFF   0x10ull
#define APPFW_MENU_ROW_SIZE          24u
#define APPFW_MENU_MAX_ROWS          30u

/*
 * [OK] TextSprite string setters (`.got.plt`; initial values point at the
 * __imp_* slots in the `extern` segment at 0x098A2xxx / 0x098A3xxx).
 * A7C2 equivalents of APPFW_GOT_SET_STRING_RID / _CSTR.
 */
#define APPFW_GOT_SET_STRING_RID     0x05BC1FE8ull
#define APPFW_GOT_SET_STRING_CSTR    0x05BC1010ull
#define APPFW_GOT_SET_FONT           0x05BC1CC8ull
#define APPFW_GOT_DRAW_TEXT          0x05BC1240ull

/* [OK] WidgetSystem GOT slots (used by the touch / frame hooks). */
#define APPFW_GOT_WIDGET_INSTANCE    0x05BC1388ull
#define APPFW_GOT_POST_MOUSE_PRESS   0x05BC2AC8ull
#define APPFW_GOT_POST_MOUSE_MOVE    0x05BC21F8ull
#define APPFW_GOT_POST_MOUSE_RELEASE 0x05BC20B8ull
#define APPFW_GOT_UPDATE_FRAME       0x05BC21B0ull
#define APPFW_GOT_NOTIFY_DRAW        0x05BC2098ull
#define APPFW_GOT_PROCESS_EVENT      0x05BC1AF8ull
#define APPFW_GOT_CREATE_LAYER       0x05BC1540ull
#define APPFW_GOT_UPDATE_CUR_TIME    0x05BC2578ull

/* ------------------------------------------------------------------ */
/* Menu screens                                                        */
/* ------------------------------------------------------------------ */

/*
 * [OK] 0x293 is the menu list screen.  Its view is
 * `view_list_menu_group_table` (typeinfo 0x58E2A88, view vtable 0x58E2AA8)
 * which renders the *groups* of a category.  The A6700 reuses 0x293 for the
 * Pilot item list; on the A7C2 the item list is a different screen, so this
 * module renders its own text through the setString hook instead of
 * rewriting a layout array -- the A6700 layout arrays do not exist here.
 */
#define APPFW_LIST_SCREEN_ID         0x293u
/*
 * [OK] 0x2CB is the screen that 44 of the 96 non-setting menu entries open.
 * Its controller is scr7001_1004_controller (getter 0x1CF7840, ctor
 * 0x21824B4, vtable 0x5B135C0); it is the generic "item list of a group"
 * screen, i.e. the A7C2 counterpart of the screen the A6700 module reaches
 * through 0x293.  A non-setting row desc whose +0x0C points here opens the
 * item list of the row it was selected from.
 */
#define APPFW_ITEM_SCREEN_ID         0x2CBu
/*
 * [OK] 0x1C3 is dlg_rec231_controller, a dialog -- not the settings detail
 * page the A6700 uses 0x1C3 for.  Kept for reference only.
 */
#define APPFW_DETAIL_SCREEN_ID       0x1C3u
#define APPFW_293_ROW_RID_TABLE      0x0375FA00ull
#define APPFW_293_ROW_RECT_TABLE     0x075935E0ull
#define APPFW_VIEW_293_UPDATE        0x017FBDB0ull

/*
 * [OK] Controller getters found in the page table.
 *
 *   controller: (uint32_t pool_idx, a2, a3=iview, a4, a5, a6)
 *               -> obj = 0x7603190 + 0x5000*pool_idx
 *                  ctor(obj, a2, a3, a4, a5, pool_idx, screen_id, a6)
 *
 *   presenter:  (uint32_t pool_idx)   [cache lookup only, no construction]
 *
 * The controller constructor stores:
 *   obj+376 = screen_id const (0x293 or 0x1C3)
 *   obj+384 = a2, obj+392 = a4, obj+400 = a6, obj+760 = iview
 *
 * Only the controller getter constructs anything, so only that slot is
 * worth hooking (the presenter object already exists when it is asked for).
 */
#define APPFW_CTRL_GETTER_293        0x1CF0F20ull
#define APPFW_CTRL_CTOR_293          0x1CF0D88ull
#define APPFW_CTRL_GETTER_1C3        0x1CD8478ull
#define APPFW_CTRL_CTOR_1C3          0x1CD82E0ull
#define APPFW_PRESENTER_GETTER_293   0x1D54644ull
#define APPFW_PRESENTER_GETTER_1C3   0x1DB0540ull
#define APPFW_CTRL_POOL_OFF          0x7603190ull
#define APPFW_CTRL_STRIDE            0x5000ull
#define APPFW_PRESENTER_POOL_OFF     0x7603168ull

/* ------------------------------------------------------------------ */
/* Menu item descriptors                                               */
/* ------------------------------------------------------------------ */

/*
 * [GUESS] Descriptor layout.  Two samples were decoded:
 *
 *   non_setting 0x5a80db0:
 *     +00 RID 0x021c6639   +04 RID 0x021c66c0   +08 0xffffffff
 *     +0C u32 0x2fd        +10 u32 0x2fd        +14 u32 1
 *     +18 u32 0x65f        +1C 0
 *     +20 fn 0x01bf20e0    +24 0
 *
 *   setting     0x59e3988:
 *     +00 RID 0x0200a353   +04 0xffffffff        +08 0xffffffff
 *     +0C RID 0x0204e13f   +10 0xffffffff        +14 u32 0xb81
 *     +18 RID              +1C RID               +20 u32 0xd4
 *
 * 0x01bf20e0 disassembles to `return 1;` and every xref to it is a
 * descriptor constant in 0x597Exxx, so +0x20 is the availability callback
 * ("can this row be entered"), the same role APPFW_VISIBILITY_OK_OFF has
 * on the A6700.  +0x0C looks like the screen id opened on select.
 */
#define APPFW_DESC_SIZE              0x60ull
#define APPFW_DESC_LABEL_RID_OFF     0x00ull
#define APPFW_DESC_SCREEN_ID_OFF     0x0Cull
#define APPFW_DESC_VISIBILITY_OFF    0x20ull
#define APPFW_VISIBILITY_OK_OFF      0x1BF20E0ull

/* ------------------------------------------------------------------ */
/* Not yet located on the A7C2                                         */
/*                                                                     */
/* The A6700 module also patches:                                      */
/*   - layout arrays for 0x1C3 (13 items) and 0x293 (12 items)         */
/*   - ctrl item arrays for 0x1C3                                      */
/*   - the non-setting tree registration                               */
/*   - confirm / back / left / right button GOT slots                  */
/*   - current group / selected row / current item getters             */
/*   - Live View text slot vtable + layout GOTs                        */
/*                                                                     */
/* On the A7C2 the first three have no equivalent (the UI is built from */
/* named widgets, not layout arrays), the button and cursor hooks are   */
/* still being located, and the Live View slots have not been found.    */
/* Everything that depends on them is compiled out of the A7C2 build    */
/* unless PILOT_A7C2_ENABLE_UNVERIFIED is defined, so a bad guess can   */
/* never reach the camera unnoticed.                                   */
/* ------------------------------------------------------------------ */

#define APPFW_GOT_BTN_CONFIRM        0x00000000ull
#define APPFW_GOT_BTN_BACK           0x00000000ull
#define APPFW_GOT_BTN_RIGHT          0x00000000ull
#define APPFW_GOT_BTN_LEFT           0x00000000ull
#define APPFW_GET_CUR_GROUP_OFF      0x00000000ull
#define APPFW_GET_SEL_ROW_OFF        0x00000000ull
#define APPFW_GET_CUR_ITEM_OFF       0x00000000ull

/* ------------------------------------------------------------------ */
/* view_string::draw -- single text rendering entry point on A7C II    */
/* ------------------------------------------------------------------ */
/* appFw.so has exactly one call site each for                          */
/* TextSprite::setString(unsigned), TextSprite::setString(const char*)  */
/* and ResourceManager::prefetchTextImage, and all three live inside    */
/* view_string::draw.  134 callers (the view_* classes) reach it, so    */
/* patching its prologue covers every text the menu UI draws.           */
/*                                                                     */
/* View object layout around the text fields (from the disassembly):    */
/*   +0x204  u8     visible/valid flag                                  */
/*   +0x208  u32    text RID (0xFFFFFFFF means "use the C string")      */
/*   +0x210  char*  plain C string                                      */
/*   +0x318  ptr    libra::Dictionary* (may be null)                    */
/*   +0x320  u32    number of text segments                             */
/*                                                                     */
/* The hook rewrites (RID -> -1, cstr -> own text) before the original  */
/* prologue runs, guarded by a byte-for-byte prologue check so that a   */
/* different firmware revision disables it instead of crashing.         */
#define A7C2_VS_DRAW_OFF             0x1827F8Cull
#define A7C2_VS_RID_OFF              0x208u
#define A7C2_VS_CSTR_OFF             0x210u

#endif /* PILOT_MENU_UI_OFFSETS_A7C2_H */
