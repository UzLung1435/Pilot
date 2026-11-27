/*
 * Sony A7C2 appFw.so offsets for Pilot menu_ui module
 * 
 * IMPORTANT: A7C2 architecture differs significantly from A6700
 * - PAGE_TABLE stride is 0x60 (not 0x58)
 * - No separate LAYOUT arrays (embedded in presenter/controller objects)
 * - ctrl_items uses direct addressing (no GOT entry)
 * - Menu system architecture is fundamentally different
 * 
 * Status: PARTIAL - PAGE_TABLE confirmed, LAYOUT offsets not applicable
 * 
 * This configuration may require significant code changes to work properly.
 */

// PAGE_TABLE: Confirmed ✓
#define APPFW_GOT_PAGE_TABLE         0x05B4CFD0
#define APPFW_PAGE_TABLE_OFF         0x051F7810

// CTRL_ITEMS: Confirmed ✓ (but no GOT entry)
#define APPFW_CTRL_ITEMS_1C3_OFF     0x051C1230
#define APPFW_GOT_CTRL_ITEMS_1C3     0x00000000  // DOES NOT EXIST - use direct address

// LAYOUT: Architecture different - these offsets do not exist in A7C2
#define APPFW_GOT_LAYOUT_1C3         0x00000000  // NOT APPLICABLE
#define APPFW_LAYOUT_1C3_OFF         0x00000000  // NOT APPLICABLE
#define APPFW_GOT_LAYOUT_293         0x00000000  // NOT APPLICABLE
#define APPFW_LAYOUT_293_OFF         0x00000000  // NOT APPLICABLE

// TextSprite functions: Need to be confirmed
#define APPFW_GOT_SET_STRING_CSTR    0x00000000  // TODO: Find equivalent
#define APPFW_GOT_SET_STRING_RID     0x00000000  // TODO: Find equivalent

// Button handlers: Need to be confirmed
#define APPFW_GOT_BTN_CONFIRM        0x00000000  // TODO: Find equivalent
#define APPFW_GOT_BTN_BACK           0x00000000  // TODO: Find equivalent

// MENU_TABLE: Need to be confirmed
#define APPFW_GOT_MENU_TABLE         0x00000000  // TODO: Find if exists
#define APPFW_MENU_TABLE_OFF         0x00000000  // TODO: Find if exists

/*
 * PAGE_TABLE structure (stride 0x60):
 * 
 * struct page_entry {
 *     void* presenter_factory;     // +0x00
 *     void* controller_factory;    // +0x08
 *     uint64_t unknown_0x10;       // +0x10
 *     uint64_t unknown_0x18;       // +0x18
 *     uint32_t screen_id;          // +0x20 (low 32-bit)
 *     uint32_t param1;             // +0x24
 *     uint64_t param2;             // +0x28
 *     uint64_t param3;             // +0x30
 *     uint64_t param4;             // +0x38
 *     uint64_t param5;             // +0x40
 *     uint64_t param6;             // +0x48
 *     uint64_t param7;             // +0x50
 *     uint64_t param8;             // +0x58
 * };
 * 
 * Example:
 * screen_id 0x1C3 @ 0x5202130:
 *   +0x00: 0x01DB0540 (presenter_factory)
 *   +0x08: 0x01CD8478 (controller_factory)
 *   +0x20: 0x000001C3 (screen_id)
 * 
 * screen_id 0x293 @ 0x5206F30:
 *   +0x00: 0x01D54644 (presenter_factory)
 *   +0x08: 0x01CF0F20 (controller_factory)
 *   +0x20: 0x00000293 (screen_id)
 */

/*
 * CTRL_ITEMS array:
 * 
 * Base: 0x051C1230
 * Size: 187 entries (0x5D8 bytes)
 * Format: array of function pointers
 * Access: Direct address (no GOT indirection)
 * 
 * Menu item handlers load from this array:
 *   ADRL X2, 0x51C1230
 *   LDR  X2, [X2, W0, SXTW#3]
 *   BR   X2
 */

/*
 * Known screen IDs:
 * - 0x1C3: Custom Assign screen
 * - 0x293: Another menu screen
 */
