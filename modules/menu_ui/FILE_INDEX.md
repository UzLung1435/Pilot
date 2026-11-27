# A7C2 Reverse Engineering - File Index

## 📋 Documentation Files

### Main Documents
| File | Purpose | Status |
|------|---------|--------|
| `PROJECT_STATUS_FINAL.md` | Complete project status report | ✅ Final |
| `A7C2_FINAL_SUMMARY.md` | Detailed analysis summary | ✅ Complete |
| `A7C2_PORTING_GUIDE.md` | Step-by-step porting guide | ✅ Ready |
| `QUICK_REFERENCE.md` | Quick lookup reference | ✅ Ready |
| `FILE_INDEX.md` | This file | ✅ Current |

### Technical References
| File | Purpose |
|------|---------|
| `offsets_a7c2.h` | C header with confirmed offsets |

## 🔧 IDA Python Scripts (30+)

### Core Analysis
| Script | Purpose | Output |
|--------|---------|--------|
| `ida_manual_trace.py` | Main analysis entry point | `ida-output-18.txt` |
| `ida_dump_page_table.py` | Verify PAGE_TABLE structure | Console |
| `ida_analyze_ctrl_items.py` | Analyze ctrl_items array | Console |
| `ida_find_ctrl_items_access.py` | Find ctrl_items access patterns | Console |

### Deep Tracing
| Script | Purpose | Output |
|--------|---------|--------|
| `ida_trace_ui_manager.py` | Trace UI manager (0x1E4BACC) | `ida-output-20.txt` |
| `ida_trace_deep_layout.py` | Deep presenter→layout trace | Console |
| `ida_presenter_to_layout.py` | Presenter to LAYOUT connection | Console |
| `ida_analyze_handlers_and_layout.py` | Menu handlers + layout search | Console |

### Component Analysis
| Script | Purpose | Output |
|--------|---------|--------|
| `ida_trace_view_layer_registry.py` | View/layer registry init | `ida-output-17.txt` |
| `ida_trace_view_instance.py` | View instance lookup | `ida-output-19.txt` |
| `ida_trace_view_registry.py` | View registry paths | Earlier output |
| `ida_trace_view_global_init.py` | Global view initialization | Earlier output |
| `ida_trace_widget_descriptor.py` | Widget descriptor fields | `ida-output-18.txt` |

### Screen & Factory Analysis
| Script | Purpose | Output |
|--------|---------|--------|
| `ida_dump_scr7001_vtables.py` | scr7001_1004 vtables | `ida-output-1.txt` |
| `ida_find_scr7001_refs.py` | scr7001 references | `ida-output-2.txt` |
| `ida_dump_screen_functions.py` | Screen-related functions | `ida-output-3.txt` |
| `ida_trace_screen_registry.py` | Screen registration | `ida-output-4.txt` |
| `ida_dump_create_screen_tables.py` | create_screen dispatch tables | `ida-output-5.txt` |
| `ida_trace_create_screen_index.py` | create_screen indexing logic | `ida-output-6.txt` |

### Menu System Analysis
| Script | Purpose | Output |
|--------|---------|--------|
| `ida_trace_menu_util.py` | menu::util analysis | `ida-output-7.txt` |
| `ida_dump_menu_util_targets.py` | menu::util dispatch | `ida-output-8.txt` |
| `ida_trace_menu_position.py` | Menu position getter | `ida-output-9.txt` |
| `ida_trace_menu_methods.py` | Menu method analysis | `ida-output-10.txt` |
| `ida_trace_screen_wrappers.py` | Screen ID wrappers | `ida-output-11.txt` |
| `ida_locate_got_entries.py` | GOT entry scanner | `ida-output-12.txt` |

### Additional Tools
| Script | Purpose |
|--------|---------|
| `ida_trace_layout_1c3_293.py` | Layout trace for 0x1C3/0x293 |
| `parse_elf.py` | ELF segment parser |
| `verify_segments.py` | Segment verification |
| `find_offsets_fast.py` | Fast offset scanner |
| `find_offsets_v2.py` | Advanced offset finder |

## 📊 IDA Output Files (20+)

| File | Source Script | Key Findings |
|------|---------------|--------------|
| `ida-output-1.txt` | scr7001 vtables | vtable addresses |
| `ida-output-2.txt` | scr7001 refs | Reference locations |
| `ida-output-3.txt` | screen functions | Function catalog |
| `ida-output-4.txt` | screen registry | Registry structure |
| `ida-output-5.txt` | create_screen tables | 0x051B82A0, 0x051B8790 |
| `ida-output-6.txt` | create_screen indexing | Widget descriptor logic |
| `ida-output-7.txt` | menu::util | 0x597D568 vtable |
| `ida-output-8.txt` | menu::util dispatch | Dispatch entries |
| `ida-output-9.txt` | menu position | 0x1B74470 function |
| `ida-output-10.txt` | menu methods | Method catalog |
| `ida-output-11.txt` | screen wrappers | Wrapper functions |
| `ida-output-12.txt` | GOT scanner | GOT entries |
| `ida-output-13.txt` | (unused) | - |
| `ida-output-16.txt` | view registry | View system paths |
| `ida-output-17.txt` | view layer registry | Layer init (17,518 lines) |
| `ida-output-18.txt` | widget descriptor | Descriptor writes (592K lines) |
| `ida-output-19.txt` | view instance | view_get_instance trace |
| `ida-output-20.txt` | UI manager | 0x1E4BACC analysis |

## 🎯 Key Findings

### Confirmed Addresses
```
PAGE_TABLE:     GOT 0x05B4CFD0 → 0x051F7810 (stride 0x60)
CTRL_ITEMS:     0x051C1230 (187 entries, no GOT)
PRESENTER_1C3:  0x01DB0540
CONTROLLER_1C3: 0x01CD8478
PRESENTER_293:  0x01D54644
CONTROLLER_293: 0x01CF0F20
VIEW_VTABLE:    0x058DBF38
CREATE_SCREEN:  0x017CDB00
```

### Architecture Insights
- PAGE_TABLE stride changed from 0x58 to 0x60
- LAYOUT arrays embedded in objects (not separate)
- ctrl_items uses direct addressing (no GOT)
- Menu system fundamentally different from A6700

## 📁 Directory Structure

```
Pilot/modules/menu_ui/
├── 📄 Documentation (5 files)
│   ├── PROJECT_STATUS_FINAL.md
│   ├── A7C2_FINAL_SUMMARY.md
│   ├── A7C2_PORTING_GUIDE.md
│   ├── QUICK_REFERENCE.md
│   └── FILE_INDEX.md
├── 📄 Code
│   ├── offsets_a7c2.h
│   ├── pilot_menu_ui.c (needs major updates)
│   └── pilot_ui.h
├── 🐍 IDA Scripts (30+ files)
│   ├── ida_manual_trace.py
│   ├── ida_dump_*.py (10+)
│   ├── ida_trace_*.py (15+)
│   ├── ida_analyze_*.py (5+)
│   └── Helper scripts
└── 📊 Output Files
    ├── ida-output-1.txt to ida-output-20.txt
    └── Various analysis logs
```

## 🚀 Usage Guide

### Quick Start
1. Read `QUICK_REFERENCE.md` for key addresses
2. Check `A7C2_PORTING_GUIDE.md` for next steps
3. Review `PROJECT_STATUS_FINAL.md` for full context

### For Detailed Analysis
1. See `A7C2_FINAL_SUMMARY.md` for complete findings
2. Run IDA scripts to verify/extend analysis
3. Check `ida-output-*.txt` for raw data

### For Development
1. Use `offsets_a7c2.h` in your code
2. Reference `A7C2_PORTING_GUIDE.md` for strategies
3. Adapt `pilot_menu_ui.c` as needed

## 📝 Notes

- All IDA scripts tested with IDA Pro 9.4
- Output files are UTF-8 encoded
- Some scripts require manual confirmation before running
- File sizes range from 500 bytes to 2+ MB

## 🔗 Related Files

```
Parent Directory: Pilot/modules/menu_ui/
Binary: D:/Code/Sony_Cam_Compare/oldProjects/FlashDir/backup1/candidate/appFw.so
IDA DB: D:/Code/Sony_Cam_Compare/appFw.so.i64
```

---

**Last Updated**: Current session
**Total Files**: 60+ (docs + scripts + outputs)
**Status**: Analysis complete, ready for porting
