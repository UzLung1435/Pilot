# 使用方式

框架发布下载：[Releases](https://github.com/Himpq/Pilot/releases)。适用 Sony ILCE-6700 固件 2.00，以及 Sony ILCE-7CM2（A7C II）固件 2.01（离线适配完成，实机未验收）；模块 ABI 为 2。电脑端工具需要 Python 3.10 或更新版本。

## 1. 准备文件

从 Releases 下载所需版本的 `pilot.so`。
下载源码包或克隆仓库，在仓库根目录运行后续命令：

```sh
git clone https://github.com/Himpq/Pilot.git
cd Pilot
python tools/pilot.py --help
```

首次安装或更新常驻框架还需要以下本机材料：

| 材料 | 内容 |
|---|---|
| `patch.json` | 匹配当前相机 AP 基线的完整 `pilot.patch-map.v1` 配置 |
| `pilot_ap_flash.so` | 与配置、目标 `pilot.so` 配套的刷写器，内部嵌入待安装的框架 |
| USB transport 目录 | `cpload_run.py` 及其所需的本机 USB 环境 |
| 本机状态目录 | 存放原文件备份和设备操作记录 |

上述材料保留在本机，通过命令参数指定。`patch.a6700.json` 与 `patch.a7c2.json` 是结构说明；安装使用包含目标哈希、框架及刷写器哈希的完整配置。
更新框架版本时，配置和刷写器也要与新的 `pilot.so` 一起匹配。

A7C II 2.01 还需准备：`patch.a7c2.image.json`（`pilot.patch.v1` 离线镜像描述，可用 `pilot.py image` 生成和恢复补丁镜像）、按 A7C II 四个窗口重建的 `pilot_ap_flash.so`、以及指向 A7C II 2.01 sysroot 构建的 `pilot.so`。完整依据与 `patch.json` 模板见 [A7C II 适配说明](A7C2-PORT.md)。刷写器若使用自定义结果头，可在 `patch.json` 中设置 `flash.header`；默认沿用 `A6700PILOTFLASH\n`。

## 2. 检查与升级常驻框架

相机连接电脑并设置为大容量存储 USB 模式。下面是 Windows PowerShell 示例，将路径替换为自己的本机材料位置：

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

# 已安装 Pilot：更新 /usr/lib/pilot.so
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
模块的兼容性、下载和具体使用方式以各自的发布说明为准。

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
