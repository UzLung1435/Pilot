# Sony A7C2 Pilot Menu UI - Reverse Engineering Project

> **Status**: ✅ Analysis Complete | ⚠️ Porting Required

Sony A7C2 (ILCE-7CM2) appFw.so 逆向工程分析，用于移植 Himpq-Pilot 的 menu_ui 模块。

## 📊 项目概况

**目标**: 将 Pilot 固件补丁框架的菜单系统移植到 Sony A7C2  
**方法**: 逆向工程 appFw.so 二进制文件，确定关键偏移量  
**工具**: IDA Pro 9.4, IDAPython, ARM64 汇编分析  
**结果**: 架构差异显著，需要重新设计 hook 策略

## 🎯 核心发现

### ✅ 已确认的偏移量

```c
// PAGE_TABLE (菜单页面表)
#define APPFW_GOT_PAGE_TABLE    0x05B4CFD0  // GOT 条目
#define APPFW_PAGE_TABLE_OFF    0x051F7810  // 实际表地址
#define PAGE_ENTRY_STRIDE       0x60        // A6700 是 0x58

// CTRL_ITEMS (菜单项处理函数数组)
#define APPFW_CTRL_ITEMS_OFF    0x051C1230  // 187 个条目
// 注意：无 GOT 条目，使用直接地址访问

// Screen 0x1C3 (Custom Assign 菜单)
#define PRESENTER_1C3           0x01DB0540
#define CONTROLLER_1C3          0x01CD8478

// Screen 0x293
#define PRESENTER_293           0x01D54644
#define CONTROLLER_293          0x01CF0F20
```

### ⚠️ 架构差异

| 组件 | A6700 | A7C2 | 影响 |
|------|-------|------|------|
| PAGE_TABLE 步长 | 0x58 | 0x60 | 需要调整访问代码 |
| LAYOUT 数组 | 独立，GOT 访问 | 嵌入对象 | **无法直接移植** |
| CTRL_ITEMS 访问 | GOT 间接 | 直接地址 | 需要修改 hook 方式 |
| 菜单架构 | 静态表驱动 | 动态对象驱动 | **根本性差异** |

### ❌ 不存在的 A6700 偏移量

```c
// 以下偏移量在 A7C2 中不适用或不存在
APPFW_GOT_LAYOUT_1C3        // LAYOUT 嵌入在对象中
APPFW_LAYOUT_1C3_OFF        // 无独立 LAYOUT 数组
APPFW_GOT_LAYOUT_293        // 同上
APPFW_LAYOUT_293_OFF        // 同上
APPFW_GOT_CTRL_ITEMS_1C3    // 使用直接地址，无 GOT
```

## 📁 文件导航

### 📖 快速开始
- **[QUICK_REFERENCE.md](QUICK_REFERENCE.md)** - 关键地址快速查询
- **[A7C2_PORTING_GUIDE.md](A7C2_PORTING_GUIDE.md)** - 移植策略和指南
- **[FILE_INDEX.md](FILE_INDEX.md)** - 完整文件索引

### 📄 详细文档
- **[PROJECT_STATUS_FINAL.md](PROJECT_STATUS_FINAL.md)** - 完整项目状态报告
- **[A7C2_FINAL_SUMMARY.md](A7C2_FINAL_SUMMARY.md)** - 详细技术分析
- **[offsets_a7c2.h](offsets_a7c2.h)** - C 头文件定义

### 🔧 工具
- **IDA Python 脚本**: 30+ 个分析脚本（见 `FILE_INDEX.md`）
- **分析输出**: 20+ 个 `ida-output-*.txt` 文件

## 🚀 移植选项

### 选项 1: 最小化补丁 (推荐快速实验)

**适用场景**: 只需修改少数菜单项

```c
// 直接修改 ctrl_items 数组中的函数指针
uint64_t *ctrl_items = (uint64_t *)0x051C1230;
ctrl_items[X] = (uint64_t)my_custom_handler;  // X = 目标菜单项索引
```

**优点**: 简单直接，不依赖复杂的 hook 系统  
**缺点**: 功能有限，无法添加新菜单页面

### 选项 2: Hook Factory 函数

**适用场景**: 需要修改菜单页面行为

```c
// Hook presenter/controller 工厂函数
hook_function(PRESENTER_1C3, my_presenter_factory);
hook_function(CONTROLLER_1C3, my_controller_factory);
```

**优点**: 控制整个页面的创建过程  
**缺点**: 需要理解 A7C2 对象模型

### 选项 3: 完全重写

**适用场景**: 需要完整功能，添加新菜单

**方案**: 参考 A7C2 原生架构，重新设计 menu_ui 模块

**时间**: 预计需要额外 20-40 小时开发

### 选项 4: 等待社区支持

**观望**: Himpq-Pilot 作者可能计划 A7C2 官方支持

## 🛠️ 使用指南

### 1. 查看关键地址
```bash
cat QUICK_REFERENCE.md
```

### 2. 验证 PAGE_TABLE
在 IDA 中运行：
```python
execfile(r"D:\Code\Sony_Cam_Compare\Pilot\modules\menu_ui\ida_dump_page_table.py")
```

### 3. 分析 ctrl_items
在 IDA 中运行：
```python
execfile(r"D:\Code\Sony_Cam_Compare\Pilot\modules\menu_ui\ida_analyze_ctrl_items.py")
```

### 4. 确定移植策略
阅读 `A7C2_PORTING_GUIDE.md`，根据需求选择方案

## 📊 技术细节

### 二进制信息
```
文件: appFw.so
大小: 99,960,744 bytes
架构: ARM64 (AArch64)
固件: Sony A7C2 v2.01
```

### 内存布局
```
.text        0x01660680 - 0x037041A0  (代码段)
.data.rel.ro 0x051B82A0 - 0x05B4BB60  (只读数据)
.got         0x05B4BE70 - 0x05BC0FE8  (全局偏移表)
.data        0x05BC2B58 - 0x05F63FB8  (可写数据)
.bss         0x05F63FB8 - 0x098A1CD0  (未初始化数据)
```

### PAGE_TABLE 结构
```c
struct page_entry {  // 0x60 bytes
    void* presenter_factory;     // +0x00
    void* controller_factory;    // +0x08
    uint64_t unknown_0x10;       // +0x10
    uint64_t unknown_0x18;       // +0x18
    uint32_t screen_id;          // +0x20
    // ... 其他字段
};
```

## 💡 下一步建议

### 如果你想快速实验
1. 阅读 `QUICK_REFERENCE.md`
2. 确定要修改的菜单项
3. 使用选项 1 (最小化补丁)
4. 编写简单的 hook 代码测试

### 如果你想完整移植
1. 阅读 `A7C2_FINAL_SUMMARY.md`
2. 研究 A7C2 对象模型
3. 设计新的 hook 架构
4. 参考 `A7C2_PORTING_GUIDE.md` 实施

### 如果你想等待
1. 关注 Himpq-Pilot 仓库
2. 联系原作者询问 A7C2 计划
3. 在社区分享本分析结果

## 🤝 贡献

本分析是独立逆向工程的结果，欢迎：
- 验证和补充分析结果
- 分享成功的移植经验
- 报告发现的错误或遗漏

## ⚠️ 免责声明

- 本项目仅用于学习和研究目的
- 修改固件可能导致设备损坏或保修失效
- 使用风险自负，请在充分了解的情况下操作

## 📞 参考资源

- **Himpq-Pilot**: [原始项目仓库]
- **IDA Pro**: [Hex-Rays 官网]
- **Sony 固件**: [官方支持页面]

---

**项目完成时间**: 2024  
**分析工时**: ~8 小时  
**生成文件**: 60+ (脚本 + 文档 + 输出)  
**状态**: ✅ 分析完成，等待移植实施

**快速链接**:
- 📖 [快速参考](QUICK_REFERENCE.md)
- 📋 [文件索引](FILE_INDEX.md)
- 🔧 [移植指南](A7C2_PORTING_GUIDE.md)
- 📊 [项目状态](PROJECT_STATUS_FINAL.md)
