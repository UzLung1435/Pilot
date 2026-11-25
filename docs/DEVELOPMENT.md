# 模块开发与构建

模块导出 `pilot_module_init`，接口定义在 `patch/pilot.h`：

```c
#include "pilot.h"

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

`sdk/pilot_mod_util.h` 提供编码、等待、日志、内存和补丁辅助函数。使用 CP 异步接口前，请检查 `PilotCpMemApi.struct_size`，并确保回调使用的数据在回调执行前保持有效。

## 构建与测试

```sh
PILOT_SYSROOT=/path/to/target/sysroot bash patch/build_pilot_dso.sh
PILOT_SYSROOT=/path/to/target/sysroot bash patch/tests/run_pilot_runtime_smoke.sh
```

通过 `PILOT_SYSROOT` 和 `PILOT_CC` 指定目标 sysroot 与交叉编译器。

## 目录

| 路径 | 内容 |
|---|---|
| `patch/` | 框架核心、API 和 AP 相机刷写入口 |
| `patch/cp/uipc_v4/` | CP UIPC v4 处理器源码 |
| `sdk/` | 模块开发辅助函数 |
| `modules/` | 扩展模块源码及构建脚本 |
| `tools/` | 补丁、模块部署和日志工具 |
| `docs/` | 安装、使用和开发文档 |

## 启动日志

`/pilot/log.txt` 用空行分隔每次启动。模块加载记录包含文件名、加载顺序和初始化状态，失败时记录具体模块及错误。

## CP 补丁

根目录 `patch.a6700.json` 中的 `cp.patch` 保存 CP 补丁描述。相机刷写统一使用 `patch/on_camera.py`：

```sh
python -m pip install -r requirements-camera.txt
python tools/pilot.py flash cp-check --map patch.a6700.json
python tools/pilot.py flash cp-install --map patch.a6700.json --yes
python tools/pilot.py flash cp-restore --map patch.a6700.json --yes
```

写入前会备份 CP 文件，完成后检查完整文件并提示重启。当前支持补丁位置为原始状态时安装，以及匹配 v4 时还原；其他状态会显示为 `unknown`。

[返回首页](../README.md)
