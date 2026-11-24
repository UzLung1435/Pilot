# 使用方式

框架发布下载：[Releases](https://github.com/Himpq/Pilot/releases)。适用 Sony ILCE-6700 固件 2.00，以及 Sony ILCE-7CM2（A7C II）固件 2.01（离线适配完成，实机未验收）；模块 ABI 为 2。电脑端工具需要 Python 3.10 或更新版本。

**机型不可混用**：A6700 用 `pilot.so` + `pilot_module_model_a6700`，A7C II 用 `pilot_a7c2.so` + `pilot_module_model_a7c2`。两者的引导导出符号、常驻路径和岛内字符串表都按机型分开，刷写与部署时会先校验，避免装错。机型清单见 [`models.json`](../models.json)。

## 1. 准备文件

从 Releases 下载**与相机机型匹配**的常驻框架：A6700 为 `pilot.so`，A7C II 为 `pilot_a7c2.so`。
下载源码包或克隆仓库，在仓库根目录运行后续命令：

```sh
git clone https://github.com/Himpq/Pilot.git
cd Pilot
python tools/pilot.py --help
```

首次安装或更新常驻框架还需要以下本机材料：

| 材料 | 内容 |
|---|---|
| `patch.json` | 匹配当前相机 AP 基线的完整 `pilot.patch-map.v1` 配置，含完整的 `runtime` 机型块 |
| `pilot_ap_flash.so` | 与配置、目标常驻 DSO 配套的刷写器，内部嵌入待安装的框架 |
| USB transport 目录 | `cpload_run.py` 及其所需的本机 USB 环境 |
| 本机状态目录 | 存放原文件备份和设备操作记录 |

上述材料保留在本机，通过命令参数指定。`patch.a6700.json` 与 `patch.a7c2.json` 是结构说明；安装使用包含目标哈希、框架及刷写器哈希的完整配置。
更新框架版本时，配置和刷写器也要与新的常驻 DSO 一起匹配。

A7C II 2.01 还需准备：`patch.a7c2.image.json`（`pilot.patch.v1` 离线镜像描述）、按 A7C II 四个窗口重建的 `pilot_ap_flash.so`、以及 `PILOT_MODEL=a7c2` 构建的 `pilot_a7c2.so`。完整依据与 `patch.json` 模板见 [A7C II 适配说明](A7C2-PORT.md)：**复制 `runtime` 块时不得删字段**，它决定设备路径、结果头与 DSO 符号校验；默认结果头为 `A7C2PILOTFLASH\n`，刷写器若沿用旧头可在 `patch.json` 中设置 `flash.header` 覆盖。

## 2. 检查与升级常驻框架

相机连接电脑并设置为大容量存储 USB 模式。下面以 A6700 为例（Windows PowerShell）；A7C II 把 `--pilot` 换成 `F:/PilotLocal/pilot_a7c2.so`，`--map` 换成带 `a7c2` `runtime` 块的 `patch.json`，其余参数相同：

```powershell
$pilotArgs = @(
  "--map", "F:/PilotLocal/patch.json",
  "--writer", "F:/PilotLocal/pilot_ap_flash.so",
  "--pilot", "F:/PilotLocal/pilot.so",
  "--transport-root", "F:/PilotLocal/transport",
  "--state-dir", "F:/PilotLocal/device-state"
)

# 读取相机 AP 和常驻 Pilot 的哈希
python tools/pilot.py flash check @pilotArgs

# 已安装 Pilot：更新机型的常驻 DSO（A7C II 为 /usr/lib/pilot_a7c2.so）
python tools/pilot.py flash update-pilot @pilotArgs --yes
```

首次安装启动钩子时，使用同一组参数运行：

```powershell
python tools/pilot.py flash install @pilotArgs --yes
```

工具在真实写入前备份原文件，写入后回读校验。成功后完整断电重启，加载新版框架。
`check` 按配置中的目标版本比对；升级前仍是旧版时，会报告 `persistent bootstrap: mismatch`。
操作记录位于指定状态目录的 `device-state.json`，原文件备份位于其中的 `backups/`。

## 3. 安装扩展模块

将模块文件放入 SD 卡 `/pilot/`，并将文件名加入现有 `/pilot/modules.json` 的 `modules` 数组，保留其他条目及加载顺序。
模块的兼容性、下载和具体使用方式以各自的发布说明为准；**只安装与相机机型相同的模块**。

用工具部署时显式传入机型，会校验模块的 `pilot_module_model_<id>` 标记，避免把 A6700 模块装到 A7C II（或反之）：

```powershell
python tools/pilot.py modules deploy private/a7c2_example.so --card D:/ --model a7c2 `
  --sha256 <模块SHA256> --backup-dir F:/PilotLocal/backups
```

相机端还有第二道检查：框架在 `dlopen` 之后、调用 `pilot_module_init` 之前用 `dlsym` 核对机型标记，不匹配时会在 `/pilot/log.txt` 记录 `model mismatch` 并隔离整个模块包。

## 4. 日志与停用

SD 卡主日志为 `/pilot/log.txt`，模块日志位于 `/pilot/modules/<模块名去掉 .so>/module.log`。
通过读卡器导出到本机：

```powershell
python tools/pilot.py logs --card D:/ --out F:/PilotLocal/logs/session-1
```

在 SD 卡 `/pilot/` 创建名为 `disable` 的空文件，重启后暂停扩展模块加载；移除该标记后重启可恢复。
恢复原启动钩子使用对应安装配置：

```powershell
python tools/pilot.py flash restore @pilotArgs --yes
```

`restore` 恢复该配置记录的 AP 修改窗口，完成后完整断电重启；SD 卡模块和常驻库文件保留。
更多镜像补丁、AP/CP RAM 模块构建与工具参数见 [通用工具说明](../tools/README.md)。

[返回首页](../README.md)
