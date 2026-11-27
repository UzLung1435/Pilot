# Pilot Menu UI - A7C2 移植版

> **状态**: ⚠️ 实验性 - 代码级移植完成，等待 appFw 偏移量更新

Sony ILCE-7CM2 (A7C II) 2.01 固件的 Pilot 菜单界面模块。

## ⚠️ 重要警告

**此模块当前不可用于生产环境！**

- ✅ 代码结构已从 Himpq-Pilot (a6700) 完整移植
- ⚠️ appFw.so 偏移量仍使用 a6700 占位符
- ❌ 未在 a7c2 相机上测试
- ⚠️ 使用错误偏移会导致相机崩溃

**必须先完成偏移量更新才能使用！**

## 快速导航

| 文档 | 用途 | 适合人群 |
|------|------|----------|
| [QUICKSTART.md](QUICKSTART.md) | 快速开始指南 | 想要快速了解的用户 |
| [README.a7c2.md](README.a7c2.md) | 技术说明 | 开发者和逆向工程师 |
| [TODO.a7c2.md](TODO.a7c2.md) | 待办清单 (90+ 项) | 完成移植的贡献者 |
| [PORTING_SUMMARY.md](PORTING_SUMMARY.md) | 完整移植总结 | 项目管理者 |
| [VERIFICATION_REPORT.md](VERIFICATION_REPORT.md) | 验证报告 | 质量保证人员 |
| [WORK_SUMMARY.md](WORK_SUMMARY.md) | 工作总结 | 了解项目背景 |
| [CHANGELOG.md](CHANGELOG.md) | 变更记录 | 维护者 |

## 功能特性

- 📋 Pilot 版本信息显示
- 📦 模块列表和状态管理
- 🔄 模块启用/禁用开关
- ⚙️ 每模块选项配置 (布尔/枚举/动作)
- 📺 Live View 文本覆层 API
- ⏳ 忙碌对话框 API
- 🎨 原生 UI 字符串覆盖 API

## 移植状态

### 已完成 ✅

```
代码移植        ████████████████████ 100%
构建系统        ████████████████████ 100%
文档编写        ████████████████████ 100%
验证工具        ████████████████████ 100%
```

### 待完成 ⏳

```
偏移量更新      ░░░░░░░░░░░░░░░░░░░░   0%
编译测试        ░░░░░░░░░░░░░░░░░░░░   0%
功能验证        ░░░░░░░░░░░░░░░░░░░░   0%
```

**总进度**: 70% (代码级移植完成)

## 快速验证

```bash
# 检查移植状态
cd Pilot/modules/menu_ui
./verify_offsets.sh

# 预期输出:
# ⚠️  WARNING: Found 1 references to a6700
# Status: ❌ NOT READY - Contains a6700 placeholders
```

## 下一步行动

### 如果你想完成移植

1. **阅读** [TODO.a7c2.md](TODO.a7c2.md) 了解需要更新的 60+ 个偏移量
2. **获取** ILCE-7CM2 2.01 固件的 appFw.so
3. **分析** 使用 IDA Pro 或 Ghidra 确定偏移
4. **更新** pilot_menu_ui.c 中的 `APPFW_*` 常量
5. **验证** 运行 `./verify_offsets.sh` 确认无 a6700 残留
6. **测试** 在测试环境验证功能

### 如果你只想了解

1. **阅读** [QUICKSTART.md](QUICKSTART.md) 获取快速概览
2. **查看** [PORTING_SUMMARY.md](PORTING_SUMMARY.md) 了解技术细节
3. **浏览** [VERIFICATION_REPORT.md](VERIFICATION_REPORT.md) 查看验证结果

## 技术概览

### 工作原理

此模块通过以下机制实现相机菜单注入：

1. **菜单表修改**: 在 appFw.so 的自定义菜单组插入 Pilot 组 (ID: 0x535)
2. **页面工厂劫持**: Hook 屏幕工厂函数 (0x1C3, 0x293, 0x2A7, 0x260)
3. **按钮事件拦截**: 拦截确认、返回、导航按钮
4. **文本渲染**: Hook TextSprite::setString() 注入自定义文本
5. **布局替换**: 动态替换视图布局和控制器焦点

### 关键偏移量

需要更新的关键偏移量包括：

- **GOT 表**: 15+ 个全局偏移表指针
- **数据结构**: 10+ 个菜单/页面/布局偏移
- **函数**: 20+ 个工厂和处理器函数偏移

详见 [TODO.a7c2.md](TODO.a7c2.md) 完整清单。

## 构建系统

```bash
# 设置环境变量
export PILOT_MODEL=a7c2
export PILOT_A7C2_SYSROOT=/path/to/sysroot
export AARCH64_CC=aarch64-linux-gnu-gcc-11

# 编译
./build.sh

# 或指定输出路径
./build.sh ../../build/pilot_menu_ui.so
```

**注意**: 编译需要 a7c2 的 sysroot 和交叉编译工具链。

## 文件结构

```
menu_ui/
├── pilot_menu_ui.c          # 主模块 (2472行, 包含所有偏移)
├── pilot_ui.h               # 公共 API
├── pilot_*.inc              # 功能模块 (UI/导航/选项/忙碌)
├── build.sh                 # 构建脚本
├── verify_offsets.sh        # 验证工具
├── device.json              # 设备配置 (a7c2)
├── config.txt               # 模块元数据
└── docs/                    # 7 个详细文档
```

## 兼容性

- **目标相机**: Sony ILCE-7CM2 (A7C II)
- **固件版本**: 2.01
- **Pilot ABI**: 2
- **模块标识**: pilot_module_model_a7c2
- **不兼容**: 与 a6700 模块互不兼容

## 风险提示

⚠️ **使用此模块前必须理解的风险**:

1. **相机崩溃**: 错误的偏移会导致相机死机或重启
2. **内存损坏**: 可能导致数据丢失或固件损坏
3. **恢复困难**: 可能需要 USB 刷写恢复固件
4. **保修失效**: 修改固件可能影响保修

**建议**: 仅在可恢复的测试环境使用，保持固件备份。

## 贡献指南

欢迎贡献！特别是：

- 🔍 appFw.so 逆向分析和偏移量确定
- 🧪 测试和验证
- 📝 文档改进
- 🐛 Bug 修复

提交前请阅读 [TODO.a7c2.md](TODO.a7c2.md)。

## 许可证

遵循原 Himpq-Pilot 和 Pilot 项目的许可证。详见 [LICENSE](LICENSE)。

## 致谢

- **Himpq-Pilot**: 提供了优秀的 a6700 实现
- **Pilot 项目**: 提供了完整的框架支持
- **逆向工程社区**: 提供了工具和技术支持

## 相关资源

- [Himpq-Pilot (a6700 原始实现)](../../Himpq-Pilot/modules/menu_ui/)
- [Pilot 框架文档](../../docs/)
- [A7C2 补丁描述](../../patch.a7c2.json)
- [机型配置](../../models.json)

---

**最后更新**: 2024-11-26  
**状态**: 代码级移植完成，等待偏移更新  
**维护者**: Pilot 项目贡献者
