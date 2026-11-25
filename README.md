# Pilot

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-AArch64%20%2F%20BIONZ%20XR-blue)](README.md)
[![Status](https://img.shields.io/badge/Status-Experimental-orange)](README.md)

<table>
  <tr>
    <td width="50%"><img width="400" alt="Pilot 界面" src="https://github.com/user-attachments/assets/01ff953c-8194-448d-98f2-8f38e3fe164e" /></td>
    <td width="50%"><img width="400" alt="Pilot 菜单" src="modules/menu_ui/assets/menu-preview.png" /></td>
  </tr>
</table>


Pilot 是一个面向索尼相机固件的扩展框架，为模块提供非 Jiritsu 依赖与运行时内存访问与便捷日志功能。

## 使用方式

[安装、升级与使用指南](docs/USAGE.md)：准备文件、更新框架、安装模块、导出日志及停用恢复。

## 支持机型

| 机型 | 固件 | 补丁映射 | 常驻 DSO / 模块标记 | 状态 |
|---|---|---|---|---|
| Sony ILCE-6700 | 2.00 | `patch.a6700.json` | `pilot.so` / `pilot_module_model_a6700` | 实机验证 |
| Sony ILCE-7CM2（A7C II） | 2.01 | `patch.a7c2.json` | `pilot_a7c2.so` / `pilot_module_model_a7c2` | 离线适配完成，实机未验收 |

A7C II 的固件依据、注入岛重定位、四个补丁窗口与未验项见 [A7C II 适配说明](docs/A7C2-PORT.md)。
`patch.a7c2.image.json` 是配套的 `pilot.patch.v1` 离线镜像描述。

## 机型隔离

**每个机身有自己的引导产物，不混用。** [`models.json`](models.json) 是机型身份的唯一来源：常驻 DSO 路径与 soname、三个引导导出符号（`pilot_<机型>_v1_marker` / `_v1_on_mount` / `_get_api`）、启动追踪文件、模块标记和刷写器结果头。`patch/pilot_model.h` 是 C 侧的镜像，`tools/pilot_tool/models.py --check` 校验两者一致。

构建时通过 `PILOT_MODEL=a6700|a7c2` 选择机型（默认 `a6700`）；模块必须调用 `PILOT_DECLARE_MODULE_MODEL()` 导出对应标记。刷写前、部署时、运行时各有一次机型校验，装错机型的 DSO 或模块会在加载前被拒绝。A6700 已验证的产物保持原样，A7C II 使用全套 `a7c2` 命名。

6K 仍是 ILCE-6700 2.00 专用模块，非 `a6700` 的构建会被脚本拒绝；MenuUI 已有 A7C II 后端，用 `PILOT_MODEL=a7c2` 构建，两机的实现与菜单入口不同，见 [A7C II 菜单说明](modules/menu_ui/a7c2/README.md)。

## 特性

- 引导模块精简，常驻部分只负责加载调度
- 挂载成功后按清单加载扩展模块
- 模块先复制到 tmpfs 再加载，不占用可移动介质挂载
- 卡上 `disable` 标记可关闭扩展加载
- `PilotApi` 提供 `file`、`log` 和 `mlog` 命名空间，以及时间和内存接口

## 启动日志

`/pilot/log.txt` 在每次启动记录前写入一个空行，便于区分不同启动。
模块加载记录包含清单文件名、加载顺序和初始化状态；加载失败时记录具体模块及错误。

```text

2026-11-22T00:00:00Z Pilot startup: ...
2026-11-22T00:00:00Z Pilot modules.txt loaded count=1
2026-11-22T00:00:00Z name=pilot_menu_ui.so Pilot module loading index=1/1 path=/tmp_wbshd/pilot/modules/pilot_menu_ui.so
2026-11-22T00:00:00Z name=pilot_menu_ui.so Pilot module initialized index=1/1 status=0
```

## 目录结构

| 路径 | 内容 |
|---|---|
| `patch/` | 框架核心、API、机型宏和相机刷写入口 |
| `patch.*.json` | 各机型启动补丁映射与离线镜像描述 |
| `models.json` | 机型注册表（身份、符号、路径、岛字符串） |
| `modules/` | 扩展模块源码及其构建脚本 |
| `tools/` | 通用补丁、模块部署和日志工具 |
| `docs/` | 安装使用文档与机型适配说明 |
| `build/` | 构建产物 |
| `patch/tests/` | 测试脚本 |

## 模块

模块导出 `pilot_module_init`，并用 `PILOT_DECLARE_MODULE_MODEL()` 声明适用的机型；接口定义在 `patch/pilot.h`。框架在调用初始化函数前会用 `dlsym` 核对机型标记。

```c
#include "pilot.h"

PILOT_DECLARE_MODULE_MODEL();

PilotStatus pilot_module_init(const PilotApi *api)
{
    if (!api || api->abi_version != PILOT_ABI_VERSION)
        return PILOT_STATUS_UNSUPPORTED;

    api->file->write(api->file, "settings.cfg", "enabled=1\n");
    api->log->print("pilot module loaded");
    api->mlog->print(api->mlog, "module initialized");
    return PILOT_STATUS_OK;
}
```

## 构建

```sh
# A6700（默认）
PILOT_SYSROOT=/path/to/a6700/sysroot bash patch/build_pilot_dso.sh
PILOT_SYSROOT=/path/to/a6700/sysroot bash patch/tests/run_pilot_runtime_smoke.sh

# A7C II：产物 build/pilot_a7c2.so，导出 pilot_a7c2_*
PILOT_MODEL=a7c2 PILOT_SYSROOT=/path/to/a7c2-2.01/sysroot \
    bash patch/build_pilot_dso.sh
```

通过 `PILOT_MODEL`、`PILOT_SYSROOT`、`PILOT_CC` 等环境变量指定机型、sysroot 与交叉编译器。模块构建必须用相同的 `PILOT_MODEL`。

## 许可

仅限在合法授权的设备上使用，本仓库仅供个人研究学习，避免复杂的Jiritsu注入。

## 通用补丁工具

[`tools/README.md`](tools/README.md) 提供补丁描述校验、离线镜像修改、AP/CP RAM 模块构建、SD 模块部署与日志导出。相机刷写通过外部本地配置调用 `on_camera.py`；研究资料与设备记录保留在本机。

## Menu UI

[菜单模块使用说明](modules/menu_ui/README.md)：在相机菜单中查看 Pilot 版本和模块列表。[下载 MenuUI v0.1.0](https://github.com/Himpq/Pilot/releases/tag/menu-ui-v0.1.0)。

## 6K

[6K 模块使用说明](modules/6k/README.md)：将 4K HS 24p 替换为 5760×3240／23.976p 录制。[下载 6K v0.1.0](https://github.com/Himpq/Pilot/releases/tag/6k-v0.1.0)。
