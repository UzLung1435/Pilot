# Menu UI 移植变更清单

## 概述

将 Himpq-Pilot 的 menu_ui 模块移植到 Pilot 项目的 a7c2 分支。

**源项目**: Himpq-Pilot/modules/menu_ui (a6700)
**目标项目**: Pilot/modules/menu_ui (a7c2)
**移植日期**: 2024-11-26
**状态**: 代码级移植完成，需要偏移量更新

## 文件变更

### 新增文件

| 文件 | 大小 | 说明 |
|------|------|------|
| `Pilot/modules/menu_ui/pilot_menu_ui.c` | 97KB | 主模块代码 (从Himpq-Pilot复制) |
| `Pilot/modules/menu_ui/pilot_ui.h` | 7KB | 公共API头文件 |
| `Pilot/modules/menu_ui/pilot_lv_ui.inc` | 12KB | Live View UI实现 |
| `Pilot/modules/menu_ui/pilot_lv_text_widget.inc` | 14KB | 文本组件渲染 |
| `Pilot/modules/menu_ui/pilot_ui_navigation.inc` | 6.6KB | 导航状态管理 |
| `Pilot/modules/menu_ui/pilot_ui_option_2a7.inc` | 15KB | 选项对话框 (0x2A7) |
| `Pilot/modules/menu_ui/pilot_ui_busy_260.inc` | 6.7KB | 忙碌对话框 (0x260) |
| `Pilot/modules/menu_ui/build.sh` | 1.7KB | 构建脚本 (已修改支持a7c2) |
| `Pilot/modules/menu_ui/device.json` | 26B | 设备配置 (a7c2) |
| `Pilot/modules/menu_ui/config.txt` | 1.2KB | 模块元数据 |
| `Pilot/modules/menu_ui/verify_offsets.sh` | 3.5KB | 偏移验证工具 (新建) |
| `Pilot/modules/menu_ui/README.a7c2.md` | 3.8KB | A7C2移植说明 (新建) |
| `Pilot/modules/menu_ui/TODO.a7c2.md` | 4.8KB | 待办清单 (新建) |
| `Pilot/modules/menu_ui/PORTING_SUMMARY.md` | 6.9KB | 移植总结 (新建) |
| `Pilot/modules/menu_ui/QUICKSTART.md` | 5.2KB | 快速开始 (新建) |
| `Pilot/modules/menu_ui/CHANGELOG.md` | (本文件) | 变更清单 |
| `Pilot/modules/menu_ui/assets/` | - | 资源文件目录 |
| `Pilot/modules/menu_ui/tests/` | - | 测试文件目录 |
| `Pilot/modules/menu_ui/LICENSE` | 1.1KB | 许可证 |
| `Pilot/modules/menu_ui/README.md` | 2.5KB | 原始README (保留) |

### 代码修改

#### pilot_menu_ui.c

```diff
+ /* Declare module model compatibility */
+ PILOT_DECLARE_MODULE_MODEL();

  #define PILOT_UI_DESC_STRING \
-     "An a6700 Firmware AP module trying to provide\n" \
+     "An a7c2 Firmware AP module trying to provide\n" \

- /* ILCE-6700 2.00 appFw.so offsets */
+ /* ILCE-7CM2 (A7C II) 2.01 appFw.so offsets
+  * Note: These offsets need to be verified and updated for a7c2.
+  * Currently using a6700 offsets as placeholder - requires reverse engineering
+  * of a7c2 appFw.so to determine correct values. */
```

**关键点**:
- 添加了 `PILOT_DECLARE_MODULE_MODEL()` 宏，用于模块身份验证
- 更新描述文字提及 a7c2
- 添加了关于偏移量需要更新的警告注释
- 保留了所有 a6700 的偏移量作为占位符

#### build.sh

```diff
+ # Auto-detect model from parent directory or use environment variable
+ MODEL="${PILOT_MODEL:-a7c2}"
+ if [ "$MODEL" = "a6700" ]; then
+     SYSROOT="${PILOT_A6700_SYSROOT:-...}"
+ elif [ "$MODEL" = "a7c2" ]; then
+     SYSROOT="${PILOT_A7C2_SYSROOT:-/opt/a7c2-sysroot}"
+ else
+     echo "Unknown model: $MODEL" >&2
+     exit 1
+ fi

+ # Set model-specific compile flags
+ if [ "$MODEL" = "a7c2" ]; then
+     MODEL_FLAGS="-DPILOT_MODEL_A7C2"
+ else
+     MODEL_FLAGS=""
+ fi

  "$CC" -std=c11 -O2 -march=armv8-a -mno-outline-atomics \
      -fno-stack-protector -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
+     $MODEL_FLAGS -I "$ROOT/patch" -c "$MODULE_DIR/pilot_menu_ui.c" \
```

**关键点**:
- 支持 `PILOT_MODEL` 环境变量
- 自动选择对应的 sysroot 路径
- 为 a7c2 添加 `-DPILOT_MODEL_A7C2` 编译标志
- 保持向后兼容 a6700

#### device.json

```diff
  {
-   "devices": ["a6700"]
+   "devices": ["a7c2"]
  }
```

## 技术变更

### 编译系统

**before**:
```bash
# 仅支持 a6700
./build.sh
```

**after**:
```bash
# 支持多机型
export PILOT_MODEL=a7c2
./build.sh
```

### 模块标识

**before** (a6700):
- 导出符号: `pilot_module_model_a6700`
- 无显式声明

**after** (a7c2):
- 导出符号: `pilot_module_model_a7c2`
- 使用 `PILOT_DECLARE_MODULE_MODEL()` 宏
- 框架会验证模块标识，拒绝不匹配的模块

### 目录结构

```
Pilot/modules/menu_ui/
├── 源代码 (从 Himpq-Pilot 复制)
├── 构建脚本 (修改支持 a7c2)
├── 配置文件 (更新为 a7c2)
├── 验证工具 (新增)
└── 文档 (新增 a7c2 专用文档)
```

## 兼容性变更

### 破坏性变更

1. **模块标识不兼容**
   - a6700 模块: `pilot_module_model_a6700`
   - a7c2 模块: `pilot_module_model_a7c2`
   - 框架会拒绝加载不匹配的模块

2. **appFw 偏移量不同**
   - a6700 和 a7c2 的固件结构不同
   - 偏移量必须针对各自固件单独确定
   - 使用错误偏移会导致崩溃

### 保持兼容

1. **API 接口**
   - `pilot_ui.h` 保持不变
   - 所有导出函数签名相同
   - 其他模块的调用代码无需修改

2. **配置格式**
   - `modules.json` 格式相同
   - `config.txt` 格式相同

## 待完成工作

### 关键任务 (必须)

- [ ] **逆向工程 appFw.so**: 确定所有偏移量
- [ ] **更新偏移常量**: 修改 pilot_menu_ui.c 中的定义
- [ ] **实际测试**: 在 a7c2 相机验证功能

### 优化任务 (可选)

- [ ] 添加运行时版本检查
- [ ] 改进错误处理和日志
- [ ] 创建自动化测试
- [ ] 优化构建流程

## 测试状态

| 测试项 | 状态 | 说明 |
|--------|------|------|
| 代码编译 | ⚠️ 未测试 | 需要 a7c2 sysroot |
| 语法检查 | ✅ 通过 | 代码结构完整 |
| 模块标识 | ✅ 配置 | PILOT_DECLARE_MODULE_MODEL() 已添加 |
| 偏移验证 | ❌ 失败 | 仍使用 a6700 占位符 |
| 功能测试 | ⬜ 未开始 | 等待偏移更新 |
| 相机测试 | ⬜ 未开始 | 等待偏移更新 |

## 风险评估

### 高风险

⚠️ **appFw 偏移错误**
- 影响: 相机崩溃、内存损坏
- 缓解: 完整验证每个偏移，逐步测试

⚠️ **固件版本不匹配**
- 影响: 偏移失效、功能异常
- 缓解: 锁定目标固件版本 (2.01)

### 中风险

⚠️ **结构差异**
- 影响: 数据布局不兼容
- 缓解: 对比 a6700/a7c2 固件结构

⚠️ **行为差异**
- 影响: 菜单逻辑变化
- 缓解: 参考原厂菜单行为

### 低风险

⚠️ **UI 渲染差异**
- 影响: 显示异常
- 缓解: 调整坐标和字体参数

## 回滚方案

如需回滚:

1. **移除模块**
   ```bash
   # 从 SD 卡删除
   rm /sdcard/pilot/pilot_menu_ui.so
   # 从 modules.json 移除
   ```

2. **恢复固件**
   ```bash
   # 如果相机无法启动
   # 使用 USB 刷写器恢复固件
   ```

3. **代码回滚**
   ```bash
   cd Pilot/modules
   rm -rf menu_ui
   # 或恢复到移植前的 git commit
   ```

## 相关资源

### 源代码
- Himpq-Pilot: `Himpq-Pilot/modules/menu_ui/`
- Pilot (本项目): `Pilot/modules/menu_ui/`

### 文档
- 移植总结: `PORTING_SUMMARY.md`
- 快速开始: `QUICKSTART.md`
- 待办清单: `TODO.a7c2.md`
- 技术说明: `README.a7c2.md`

### 配置
- 机型定义: `Pilot/models.json`
- 补丁描述: `Pilot/patch.a7c2.json`
- 设备配置: `device.json`

### 工具
- 构建脚本: `build.sh`
- 验证脚本: `verify_offsets.sh`

## 贡献者

- 移植: Kiro (AI Assistant)
- 原始实现: Himpq-Pilot 项目
- 框架: Pilot 项目

## 许可证

遵循原 Himpq-Pilot 和 Pilot 项目的许可证。

---

**最后更新**: 2024-11-26
**版本**: 1.0.0-a7c2-port
**状态**: 实验性 - 不可用于生产
