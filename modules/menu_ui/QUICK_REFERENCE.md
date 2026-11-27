# A7C2 Quick Reference

## 已确认的关键地址

```c
// PAGE_TABLE
#define APPFW_GOT_PAGE_TABLE    0x05B4CFD0  // GOT entry
#define APPFW_PAGE_TABLE_OFF    0x051F7810  // Actual table
#define PAGE_ENTRY_STRIDE       0x60        // Not 0x58!

// CTRL_ITEMS (menu item handlers)
#define APPFW_CTRL_ITEMS_OFF    0x051C1230  // 187 entries, no GOT

// Screen 0x1C3 (Custom Assign)
#define SCREEN_1C3_ENTRY        0x05202130  // PAGE_TABLE + 0x1C3 * 0x60
#define PRESENTER_1C3           0x01DB0540
#define CONTROLLER_1C3          0x01CD8478

// Screen 0x293
#define SCREEN_293_ENTRY        0x05206F30  // PAGE_TABLE + 0x293 * 0x60
#define PRESENTER_293           0x01D54644
#define CONTROLLER_293          0x01CF0F20

// View system
#define VIEW_CREATE_SCREEN      0x017CDB00
#define VIEW_VTABLE             0x058DBF38
#define VIEW_INSTANCE_INIT      0x017CD4F8
```

## 内存布局

```
appFw.so: 99,960,744 bytes

Segments:
  .text        0x01660680 - 0x037041A0  (code)
  .data.rel.ro 0x051B82A0 - 0x05B4BB60  (read-only data)
  .got         0x05B4BE70 - 0x05BC0FE8  (GOT)
  .data        0x05BC2B58 - 0x05F63FB8  (writable data)
  .bss         0x05F63FB8 - 0x098A1CD0  (zero-init)
```

## PAGE_TABLE 结构

```c
struct page_entry {  // 0x60 bytes
    void* presenter_factory;     // +0x00
    void* controller_factory;    // +0x08
    uint64_t unknown_0x10;       // +0x10
    uint64_t unknown_0x18;       // +0x18
    uint32_t screen_id;          // +0x20
    uint32_t param1;             // +0x24
    uint64_t param2;             // +0x28
    uint64_t param3;             // +0x30
    uint64_t param4;             // +0x38
    uint64_t param5;             // +0x40
    uint64_t param6;             // +0x48
    uint64_t param7;             // +0x50
    uint64_t param8;             // +0x58
};

// Access:
// entry_addr = PAGE_TABLE + (screen_id * 0x60)
```

## CTRL_ITEMS 访问模式

```asm
; Menu item dispatcher pattern:
ADRL X2, #0x51C1230          ; Load ctrl_items base
LDR  X2, [X2, W0, SXTW#3]    ; Load handler: ctrl_items[W0]
BR   X2                       ; Jump to handler
```

## 重要发现

### ✅ 存在且已确认
- PAGE_TABLE (步长 0x60)
- CTRL_ITEMS array (187 entries)
- presenter/controller factories

### ❌ 不存在或架构不同
- 独立的 LAYOUT 数组
- CTRL_ITEMS 的 GOT 条目
- MENU_TABLE (A6700 风格)

### ⚠️ 未追踪
- TextSprite::setString functions
- Button handlers (confirm/back)
- String resource IDs
- Menu visibility functions

## 常用 IDA 脚本

```bash
# 验证 PAGE_TABLE
ida_dump_page_table.py

# 分析 ctrl_items
ida_analyze_ctrl_items.py

# 查找数据访问
ida_find_ctrl_items_access.py

# 完整分析
ida_manual_trace.py
```

## 移植策略

### 最小化补丁
```c
// 直接修改 ctrl_items 数组
uint64_t *ctrl_items = (uint64_t *)0x051C1230;
ctrl_items[index] = (uint64_t)my_handler;
```

### Hook factory
```c
// Hook presenter/controller 创建
hook_function(0x01DB0540, my_presenter_factory);
hook_function(0x01CD8478, my_controller_factory);
```

## 文档位置

```
Pilot/modules/menu_ui/
  ├── A7C2_FINAL_SUMMARY.md       # 完整分析总结
  ├── A7C2_PORTING_GUIDE.md       # 移植指南
  ├── PROJECT_STATUS_FINAL.md     # 项目状态
  ├── QUICK_REFERENCE.md          # 本文件
  ├── offsets_a7c2.h              # C 头文件
  ├── ida_*.py                    # 30+ IDA 脚本
  └── ida-output-*.txt            # 分析输出
```

## 下一步选项

1. **找到特定菜单项** → 确定 ctrl_items 索引
2. **实现最小补丁** → 修改单个菜单项
3. **完全重写** → 适配 A7C2 架构
4. **等待社区** → Himpq-Pilot 官方支持

---

快速联系：查看 `A7C2_PORTING_GUIDE.md` 了解详细移植步骤
