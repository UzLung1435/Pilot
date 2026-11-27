# Pilot Menu UI - A7C2 移植总结

## 项目概述

已将 Himpq-Pilot 中的 menu_ui 模块移植到 Pilot 项目的 a7c2 分支。该模块提供相机菜单界面，允许用户查看和管理 Pilot 框架的模块。

## 完成的工作

### 1. 代码移植 ✅

- **源文件复制**: 从 `Himpq-Pilot/modules/menu_ui/` 复制所有源代码到 `Pilot/modules/menu_ui/`
  - pilot_menu_ui.c (主模块代码，2472行)
  - pilot_ui.h (公共头文件)
  - pilot_lv_ui.inc (Live View UI)
  - pilot_ui_navigation.inc (导航逻辑)
  - pilot_ui_option_2a7.inc (选项界面)
  - pilot_ui_busy_260.inc (忙碌对话框)
  - pilot_lv_text_widget.inc (文本组件)

### 2. 模型适配 ✅

- **设备配置更新**: device.json 从 a6700 改为 a7c2
- **模块声明**: 添加 `PILOT_DECLARE_MODULE_MODEL()` 宏，确保模块身份验证
- **描述文字**: 更新 UI 描述文字提及 a7c2 而非 a6700

### 3. 构建系统 ✅

- **build.sh 更新**: 
  - 支持 `PILOT_MODEL` 环境变量自动检测目标机型
  - 添加 a7c2 和 a6700 的 sysroot 路径配置
  - 根据机型自动设置 `-DPILOT_MODEL_A7C2` 或无额外标志
  - 保持与原 Himpq-Pilot 相同的编译参数和链接配置

### 4. 文档完善 ✅

创建了以下文档：

- **README.a7c2.md**: 详细的移植说明，包括：
  - 移植状态和警告
  - 需要更新的 appFw 偏移量列表
  - 逆向工程方法指导
  - 构建和安装说明
  - 技术实现细节

- **TODO.a7c2.md**: 完整的待办清单，包括：
  - 所有需要验证的偏移量（90+ 项）
  - 分析方法和工具推荐
  - 验证测试步骤
  - 风险和注意事项

- **config.txt**: 模块元数据配置
  - 标识为 a7c2 实验性版本
  - 列出功能特性和导出符号
  - 包含警告信息

### 5. 验证工具 ✅

- **verify_offsets.sh**: 偏移量验证脚本
  - 检测代码中是否还有 a6700 占位符
  - 列出所有偏移常量定义
  - 可选的 appFw.so 基本分析
  - 提供状态报告和后续步骤指导

## 技术架构

### 模块工作原理

此模块通过以下机制实现菜单注入：

1. **菜单表修改**: 在 appFw.so 的自定义菜单组中插入新的 Pilot 组（ID: 0x535）
2. **页面工厂劫持**: Hook 屏幕工厂函数：
   - 0x1C3: 详情页（显示模块信息和开关）
   - 0x293: 列表页（显示模块列表）
   - 0x2A7: 选项对话框（布尔/枚举选择）
   - 0x260: 忙碌对话框（处理提示）
3. **按钮事件拦截**: Hook 确认、返回、左、右按钮处理器
4. **文本渲染**: Hook TextSprite::setString() 注入自定义文本
5. **布局替换**: 动态替换视图布局和控制器焦点表

### 核心数据结构

```c
// 页面模型
typedef struct PilotUiPage {
    PilotUiPageKind kind;      // LIST 或 DETAIL
    const char *title;
    const PilotUiComponent *components;
    size_t component_count;
    // ...
} PilotUiPage;

// UI 组件
typedef struct PilotUiComponent {
    PilotUiComponentKind kind; // TEXT, SUBMENU, TOGGLE
    const char *label;
    const char *value;
    const PilotUiPage *target_page;
    int32_t x, y, w, h;        // 布局坐标
} PilotUiComponent;
```

### 导出的 API

模块导出以下公共 API 供其他模块使用：

- `pilot_ui_show_text()` / `pilot_ui_clear_text()`: Live View 文本覆层
- `pilot_ui_register_options()`: 注册模块选项菜单
- `pilot_ui_show_busy()` / `pilot_ui_hide_busy()`: 显示处理对话框
- `pilot_ui_set_rid_override()`: 覆盖原生字符串资源

## 当前状态

### ✅ 已完成
- 代码结构移植
- 编译系统配置
- 模块身份验证
- 文档和工具

### ⚠️ 待完成（关键）

**必须完成才能使用**:

1. **appFw.so 偏移量更新**: 需要逆向工程 ILCE-7CM2 2.01 固件的 appFw.so，确定 90+ 个偏移常量的正确值
2. **实际测试**: 在 a7c2 相机上验证功能
3. **偏移验证**: 确保每个 hook 点和数据结构访问正确

### 🔍 推荐方法

使用 Himpq-Pilot 中的方案和逻辑作为参考实现：

1. **保持架构不变**: Hook 点、数据流和状态管理逻辑已经在 a6700 上验证可行
2. **仅更新偏移**: 主要工作是将 a6700 的内存地址映射到 a7c2 对应位置
3. **利用现有工具**: 使用 IDA Pro/Ghidra 对比两个固件的相似函数和数据结构
4. **增量验证**: 先验证基础功能（菜单显示），再启用高级功能（选项、UI API）

## 文件清单

```
Pilot/modules/menu_ui/
├── pilot_menu_ui.c                 # 主模块实现 (2472 行)
├── pilot_ui.h                      # 公共 API 头文件
├── pilot_lv_ui.inc                 # Live View UI 实现
├── pilot_lv_text_widget.inc        # 文本组件渲染
├── pilot_ui_navigation.inc         # 导航状态管理
├── pilot_ui_option_2a7.inc         # 选项对话框 (0x2A7)
├── pilot_ui_busy_260.inc           # 忙碌对话框 (0x260)
├── build.sh                        # 构建脚本 (支持 a7c2)
├── device.json                     # 设备配置 (a7c2)
├── config.txt                      # 模块元数据
├── README.a7c2.md                  # 移植文档
├── TODO.a7c2.md                    # 待办清单
├── verify_offsets.sh               # 偏移验证工具
├── LICENSE                         # 许可证
├── assets/                         # 截图和资源
└── tests/                          # 测试文件
```

## 兼容性

- **目标相机**: Sony ILCE-7CM2 (A7C II)
- **固件版本**: 2.01
- **Pilot ABI**: 2
- **模块标识**: pilot_module_model_a7c2
- **不兼容**: 与 a6700 模块互不兼容（框架会拒绝加载）

## 安全警告

⚠️ **重要**: 

- 当前代码使用 a6700 偏移量作为占位符
- **不要在实际 a7c2 相机上安装此模块**，直到偏移量经过验证
- 错误的偏移量会导致：
  - 相机崩溃或死机
  - 内存损坏
  - 需要固件恢复

建议测试流程：
1. 在可恢复的测试环境验证（如SSH连接的开发固件）
2. 准备固件备份和恢复方案
3. 逐步启用功能，每个阶段验证稳定性

## 下一步行动

1. **获取 appFw.so**: 从 a7c2 固件或相机文件系统提取 `/usr/lib/appFw.so`
2. **逆向分析**: 使用 IDA Pro/Ghidra 确定所有偏移量
3. **更新代码**: 修改 `pilot_menu_ui.c` 中的偏移常量
4. **验证**: 运行 `./verify_offsets.sh` 检查更新
5. **编译测试**: 构建模块并在测试环境验证
6. **文档更新**: 记录偏移量来源和验证过程

## 参考资料

- Himpq-Pilot 原始实现: `Himpq-Pilot/modules/menu_ui/`
- Pilot 框架文档: `Pilot/docs/`
- 机型配置: `Pilot/models.json`
- 补丁描述: `Pilot/patch.a7c2.json`
- 知识库: `a7c2-knowledge-base/`

## 联系和支持

如遇到问题或需要帮助：
1. 查看 TODO.a7c2.md 中的详细清单
2. 参考 Himpq-Pilot 的 a6700 实现
3. 检查 a7c2 知识库中的固件分析报告
