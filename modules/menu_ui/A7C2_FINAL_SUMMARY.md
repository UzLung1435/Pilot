# A7C2 Offset Discovery - Final Summary

## 已确认的偏移

### 1. PAGE_TABLE (步长 0x60)
```c
#define APPFW_GOT_PAGE_TABLE    0x05B4CFD0
#define APPFW_PAGE_TABLE_OFF    0x051F7810
```

**验证：**
- GOT: 0x5B4CFD0 -> 0x51F7810 ✓
- screen_id 0x1C3 @ 0x5202130:
  - +0x00: presenter_factory = 0x01DB0540 ✓
  - +0x08: controller_factory = 0x01CD8478 ✓
  - +0x20: screen_id = 0x000001C3 ✓
- screen_id 0x293 @ 0x5206F30:
  - +0x00: presenter_factory = 0x01D54644 ✓
  - +0x08: controller_factory = 0x01CF0F20 ✓
  - +0x20: screen_id = 0x00000293 ✓

### 2. CTRL_ITEMS (菜单项处理函数数组)
```c
#define APPFW_CTRL_ITEMS_1C3_OFF  0x051C1230
```

**验证：**
- 范围: 0x051C1230 - 0x051C1808
- 187 个菜单项处理函数 ✓
- 所有条目都是有效的 .text 函数指针 ✓
- 被 controller 调度器引用 ✓

**注意：** 没有对应的 GOT 条目，使用直接地址访问

---

## 架构差异：A7C2 vs A6700

### A6700 架构
```
screen_id → PAGE_TABLE[id * 0x58] → {
    view_factory,
    ctrl_factory,
    ...
} → LAYOUT array (GOT 访问) → menu items
```

### A7C2 架构
```
screen_id → PAGE_TABLE[id * 0x60] → {
    presenter_factory,
    controller_factory,
    screen_id,
    ...
} → ctrl_items array (直接地址) → menu item handlers
```

**关键变化：**

1. **PAGE_TABLE 步长从 0x58 增加到 0x60**
2. **没有独立的 LAYOUT 数组**：菜单布局可能内嵌在 presenter/controller 对象中
3. **ctrl_items 是静态数组**：每个菜单项处理函数直接硬编码数组偏移
4. **大量数据结构不再有 GOT 条目**：使用编译时计算的直接地址

---

## 未找到的 Pilot 预期偏移

以下 A6700 偏移在 A7C2 中不存在或架构完全不同：

```c
// A6700 中存在，A7C2 中架构不同
APPFW_GOT_LAYOUT_1C3        // 不存在独立 LAYOUT 数组
APPFW_LAYOUT_1C3_OFF        // 布局可能内嵌在对象中
APPFW_GOT_LAYOUT_293        // 同上
APPFW_LAYOUT_293_OFF        // 同上
APPFW_GOT_CTRL_ITEMS_1C3    // 不存在，使用直接地址
```

---

## 移植 Pilot 到 A7C2 的选项

### 选项 1：完全重写 menu_ui 模块（推荐）

**原因：**
- A7C2 架构与 A6700 根本不同
- Pilot 的 hook 策略基于替换 PAGE_TABLE 中的 layout/ctrl_items 指针
- A7C2 没有这些可替换的指针

**方案：**
1. 保留 `APPFW_GOT_PAGE_TABLE` 和 `APPFW_PAGE_TABLE_OFF`
2. Hook presenter_factory 或 controller_factory
3. 在 factory 函数中注入自定义菜单逻辑
4. 或者 hook `ctrl_items` 数组中的具体处理函数

### 选项 2：最小化适配（实验性）

**如果只需要修改少数菜单项：**

```c
// pilot_menu_ui.c (A7C2 简化版)
#define APPFW_GOT_PAGE_TABLE         0x05B4CFD0
#define APPFW_PAGE_TABLE_OFF         0x051F7810
#define PAGE_ENTRY_STRIDE            0x60  // 不是 0x58

// 直接 patch ctrl_items 数组中的函数指针
#define CTRL_ITEMS_BASE              0x051C1230
#define CTRL_ITEMS_CUSTOM_ASSIGN     (CTRL_ITEMS_BASE + X * 8)  // 需要确定 X

// Hook 策略：替换 ctrl_items[X] 指向自定义处理函数
void hook_custom_assign() {
    uint64_t *ctrl_items = (uint64_t *)CTRL_ITEMS_BASE;
    ctrl_items[X] = (uint64_t)my_custom_handler;
}
```

### 选项 3：等待更多信息

如果 Himpq-Pilot 的作者有 A7C2 经验或文档，可能有更简单的方案。

---

## 推荐行动

1. **确认需求**：你需要修改哪些具体的菜单项？
2. **确定 hook 点**：
   - 是否只需修改"Custom Assign"菜单？
   - 还是需要添加全新的菜单页面？
3. **选择策略**：
   - 简单修改 → 直接 patch ctrl_items 数组
   - 复杂功能 → 重写 menu_ui 模块

---

## 下一步

如果你想继续，我可以：

1. **创建 A7C2 专用的 pilot_menu_ui.c**，使用简化的 hook 策略
2. **分析特定的菜单项**（如 Custom Assign），找到它在 ctrl_items 中的索引
3. **编写 proof-of-concept**，演示如何 hook 单个菜单项

请告诉我你的目标和偏好方向。
