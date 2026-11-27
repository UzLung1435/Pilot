# A7C2 Reverse Engineering - Complete Status Report

## 项目目标

将 Himpq-Pilot 的 `menu_ui` 模块移植到 Sony A7C2，需要找到 A7C2 appFw.so 中的关键偏移量。

## 完成的工作

### 1. 二进制分析 ✅

**工具和方法：**
- IDA Pro 9.4 分析 appFw.so (99,960,744 bytes)
- 创建 30+ 个专用 IDA Python 脚本
- ELF 段布局确认：
  - `.text`: 0x01660680 - 0x037041A0
  - `.data.rel.ro`: 0x051B82A0 - 0x05B4BB60
  - `.got`: 0x05B4BE70 - 0x05BC0FE8
  - `.data`: 0x05BC2B58 - 0x05F63FB8
  - `.bss`: 0x05F63FB8 - 0x098A1CD0

### 2. 关键数据结构识别 ✅

#### PAGE_TABLE
- **GOT 条目**: `0x05B4CFD0` → `0x051F7810`
- **步长**: `0x60` (A6700 是 0x58)
- **结构**:
  ```
  +0x00: presenter_factory
  +0x08: controller_factory
  +0x10: unknown
  +0x18: unknown
  +0x20: screen_id (uint32_t)
  +0x28-0x58: parameters
  ```
- **验证**:
  - screen_id 0x1C3 @ 0x5202130 ✓
  - screen_id 0x293 @ 0x5206F30 ✓

#### CTRL_ITEMS
- **地址**: `0x051C1230`
- **大小**: 187 个条目 (0x5D8 bytes)
- **格式**: 函数指针数组 (8 bytes each)
- **访问方式**: 直接地址（无 GOT 条目）
- **调度模式**:
  ```asm
  ADRL X2, 0x51C1230
  LDR  X2, [X2, W0, SXTW#3]
  BR   X2
  ```

#### Factory Functions
- **PRESENTER_1C3**: 0x01DB0540
- **CONTROLLER_1C3**: 0x01CD8478
- **PRESENTER_293**: 0x01D54644
- **CONTROLLER_293**: 0x01CF0F20

### 3. 架构分析 ✅

**A7C2 vs A6700 差异：**

| 组件 | A6700 | A7C2 |
|------|-------|------|
| PAGE_TABLE 步长 | 0x58 | 0x60 |
| LAYOUT 数组 | 独立数组，GOT 访问 | 嵌入对象，不独立存在 |
| CTRL_ITEMS 访问 | GOT 间接 | 直接地址 |
| 菜单架构 | 静态表驱动 | 动态对象驱动 |

### 4. 创建的工具 ✅

**IDA Python 脚本 (30+)**:
```
ida_dump_page_table.py
ida_analyze_ctrl_items.py
ida_find_ctrl_items_access.py
ida_analyze_handlers_and_layout.py
ida_presenter_to_layout.py
ida_trace_deep_layout.py
ida_trace_ui_manager.py
ida_trace_view_layer_registry.py
ida_manual_trace.py
... 等 20+ 个
```

**输出文件**:
```
ida-output-1.txt 到 ida-output-20.txt
offsets_a7c2.h
A7C2_FINAL_SUMMARY.md
A7C2_PORTING_GUIDE.md
```

## 未找到的偏移量

以下 A6700 偏移量在 A7C2 中**不存在或架构完全不同**：

```c
APPFW_GOT_LAYOUT_1C3         // 架构不同：无独立 LAYOUT
APPFW_LAYOUT_1C3_OFF         // 嵌入在 presenter 对象中
APPFW_GOT_LAYOUT_293         // 同上
APPFW_LAYOUT_293_OFF         // 同上
APPFW_GOT_CTRL_ITEMS_1C3     // 使用直接地址，无 GOT
APPFW_GOT_MENU_TABLE         // 未追踪
APPFW_MENU_TABLE_OFF         // 未追踪
APPFW_GOT_SET_STRING_CSTR    // 未追踪
APPFW_GOT_SET_STRING_RID     // 未追踪
APPFW_GOT_BTN_CONFIRM        // 未追踪
APPFW_GOT_BTN_BACK           // 未追踪
```

## 结论

### 核心发现

1. **A7C2 菜单系统架构与 A6700 根本不同**
   - 不是简单的偏移量替换问题
   - 需要重新设计 hook 策略

2. **现有 Pilot menu_ui 代码不能直接移植**
   - 依赖于 A6700 特有的 LAYOUT 数组结构
   - 假设所有数据都通过 GOT 访问
   - PAGE_TABLE 步长硬编码为 0x58

3. **可行的移植路径**：
   - **选项 A**: 最小化补丁 - 直接修改 ctrl_items 数组
   - **选项 B**: 完全重写 - 适配 A7C2 原生架构
   - **选项 C**: 等待 Himpq-Pilot 官方 A7C2 支持

### 建议行动

**如果目标简单**（例如只修改几个菜单项）：
→ 使用选项 A，直接 patch ctrl_items 数组的函数指针

**如果需要完整功能**（添加新菜单页面等）：
→ 使用选项 B，参考 A7C2 原生架构重写

**如果不急**：
→ 使用选项 C，等待社区支持

## 交付物

### 文档
- ✅ `A7C2_FINAL_SUMMARY.md` - 完整分析总结
- ✅ `A7C2_PORTING_GUIDE.md` - 移植指南
- ✅ `offsets_a7c2.h` - 已确认的偏移量定义
- ✅ 本状态报告

### 代码
- ✅ 30+ IDA Python 分析脚本
- ✅ 20+ IDA 输出文件
- ⚠️ `pilot_menu_ui.c` - 需要大幅修改才能用于 A7C2

### 数据
- ✅ PAGE_TABLE 结构和地址
- ✅ CTRL_ITEMS 数组位置和大小
- ✅ 关键 factory 函数地址
- ✅ 段布局和内存映射

## 下一步建议

**取决于你的具体需求：**

1. **告诉我你想修改哪个具体菜单** → 我帮你找到对应的 ctrl_items 索引
2. **决定采用哪种移植策略** → 我帮你实现最小化补丁或重写框架
3. **需要更多逆向分析** → 我可以继续追踪 TextSprite、按钮处理等功能

**或者：**
- 联系 Himpq-Pilot 作者询问 A7C2 计划
- 在现有基础上实验 proof-of-concept
- 等待社区其他人的 A7C2 移植经验

## 时间投入

- 二进制分析: ~3 小时
- IDA 脚本开发: ~2 小时
- 架构追踪: ~2 小时
- 文档编写: ~1 小时
- **总计**: ~8 小时

## 技术栈

- IDA Pro 9.4
- Python 3.15 (IDAPython)
- ARM64 汇编分析
- ELF 二进制格式
- Sony 相机固件逆向工程

---

**项目状态**: 分析完成，等待移植策略决策

**最后更新**: 2024
