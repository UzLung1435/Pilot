# A7C2 Porting Guide for pilot_menu_ui

## Current Status

We have successfully reverse-engineered the A7C2 appFw.so and found:

### ✅ Confirmed Offsets

```c
#define APPFW_GOT_PAGE_TABLE    0x05B4CFD0
#define APPFW_PAGE_TABLE_OFF    0x051F7810
#define APPFW_PAGE_ENTRY_SIZE   0x60        // Changed from 0x58 in A6700

#define APPFW_CTRL_ITEMS_1C3_OFF 0x051C1230
// No GOT entry for ctrl_items - uses direct addressing
```

### ❌ Architecture Differences

The A7C2 firmware has a **fundamentally different menu system architecture** compared to A6700:

| Feature | A6700 | A7C2 |
|---------|-------|------|
| PAGE_TABLE stride | 0x58 | 0x60 |
| LAYOUT arrays | Separate arrays via GOT | Embedded in objects |
| CTRL_ITEMS access | GOT indirection | Direct addressing |
| Menu item handlers | Function pointer table | Dispatcher + offset |

### 🚫 Non-Existent Offsets

These A6700 offsets **do not exist** in A7C2:

```c
// These need to be removed or rewritten
APPFW_GOT_LAYOUT_1C3        // No separate layout array
APPFW_LAYOUT_1C3_OFF        // Layout is embedded
APPFW_GOT_LAYOUT_293        // No separate layout array
APPFW_LAYOUT_293_OFF        // Layout is embedded
APPFW_GOT_CTRL_ITEMS_1C3    // Uses direct addressing
APPFW_GOT_MENU_TABLE        // Architecture different
APPFW_MENU_TABLE_OFF        // Architecture different
```

## Required Changes

### Option 1: Minimal Patch (Recommended for Quick Start)

If you only need to modify a few specific menu items:

1. **Identify the target menu item** in `ctrl_items` array (0x051C1230)
2. **Replace the function pointer** directly at runtime
3. **Skip the LAYOUT/PAGE_TABLE hooking**

Example:
```c
// Find which ctrl_items entry corresponds to your target menu
// Then patch it directly:
uint64_t *ctrl_items = (uint64_t *)0x051C1230;
ctrl_items[X] = (uint64_t)my_custom_handler;  // X = index to find
```

### Option 2: Full Rewrite (For Complex Features)

The current `pilot_menu_ui.c` logic assumes:
- Separate LAYOUT arrays for each screen
- GOT-based indirection for all data structures
- Specific PAGE_TABLE structure

For A7C2, you would need to:

1. **Hook at a different level**:
   - Hook presenter_factory (0x01DB0540 for screen 0x1C3)
   - Hook controller_factory (0x01CD8478 for screen 0x1C3)
   - Inject custom logic during object construction

2. **Rewrite menu item handling**:
   - Don't rely on LAYOUT arrays
   - Build menu structure dynamically
   - Use A7C2's native menu APIs

3. **Update data structure sizes**:
   ```c
   #define APPFW_PAGE_ENTRY_SIZE 0x60  // Not 0x58
   ```

## Recommended Action Plan

### Step 1: Determine Your Goal

**Question: What specific menu functionality do you need?**

- [ ] Add a new menu entry to an existing screen?
- [ ] Modify behavior of an existing menu item?
- [ ] Create entirely new menu screens?
- [ ] Other: _______________

### Step 2: Find Target Menu Item

If modifying existing menu:

1. Run IDA script to find which `ctrl_items` index corresponds to your target
2. Identify the handler function
3. Plan your hook strategy

### Step 3: Choose Approach

**For simple modifications:**
- Create a minimal A7C2-specific module
- Patch only the necessary function pointers
- Skip LAYOUT/MENU_TABLE logic

**For complex features:**
- Study A7C2's native menu system more deeply
- Possibly wait for Himpq-Pilot author's A7C2 support
- Or rewrite menu_ui from scratch for A7C2

## Available Tools

We have created IDA scripts in `Pilot/modules/menu_ui/`:

```
ida_dump_page_table.py           - Verify PAGE_TABLE structure
ida_analyze_ctrl_items.py        - Analyze ctrl_items array
ida_find_ctrl_items_access.py    - Find how ctrl_items is accessed
ida_analyze_handlers_and_layout.py - Search for menu handlers
```

## Next Steps

**What do you want to do?**

1. **Try minimal patch** - Tell me which menu item you want to modify
2. **Wait for more info** - Check if Himpq-Pilot has A7C2 plans
3. **Full rewrite** - I can help design an A7C2-specific architecture

Please let me know your preference and specific goals.
