# 使用方式

框架发布下载：[Releases](https://github.com/Himpq/Pilot/releases)。适用 Sony ILCE-6700 固件 2.00 与 Sony ILCE-7CM2 固件 2.01，模块 ABI 为 2；两机的框架与模块互不通用（见下文）。电脑端工具需要 Python 3.10 或更新版本。

## 1. 准备文件

从 Releases 下载所需版本的 `pilot.so`。
下载源码包或克隆仓库，在仓库根目录运行后续命令：

```sh
git clone https://github.com/Himpq/Pilot.git
cd Pilot
python tools/pilot.py --help
```

### 为相机生成 AP 安装配置

安装 `requirements-camera.txt` 的依赖。将相机连接到电脑后，先读取当前 AP 并生成配套文件：

```powershell
python tools/pilot.py flash prepare-ap --map patch.a6700.json --pilot ./pilot.so --output-dir ./ap-plan
```

A7C II 把 `--map` 换成本机描述文件（`patch.a7c2.json`），其余步骤相同。机型身份以 `models.json` 为唯一来源，构建时用 `PILOT_MODEL` 选择；不能把一台机器的 `pilot.so` 或模块用到另一台上——文件名、ON_MOUNT 符号和模块机型标记都不相同。

预检不写入相机。它会输出各补丁位置的当前值与配置差异，并在 `ap-plan/` 保存完整的 `preflight-report.json`、安装前备份、匹配当前相机的 `patch.json`、补丁数据和通用刷写器。检查报告，决定是否覆盖不同的位置后再执行：

```powershell
python tools/pilot.py flash install --map ./ap-plan/patch.json --writer ./ap-plan/pilot_ap_flash.so --pilot ./ap-plan/pilot.so --yes
```

已安装过框架、只想更新常驻库时运行：

```powershell
python tools/pilot.py flash update-pilot --map ./ap-plan/patch.json --writer ./ap-plan/pilot_ap_flash.so --pilot ./ap-plan/pilot.so --yes
```

生成的配置记录本次读取的安装前字节；恢复时使用相同的配置和刷写器。安装前如 AP 文件发生变化，需要重新预检。

首次安装或更新常驻框架还需要以下本机材料：

| 材料 | 内容 |
|---|---|
| `patch.json` | 预检时根据当前相机生成的完整配置，含完整的 `runtime` 机型块 |
| `pilot_ap_flash.so` | 预编译的通用刷写器，读取本次补丁数据和目标常驻 DSO |
| USB 环境 | 安装 `requirements-camera.txt` 中的依赖 |
| 本机状态目录 | 存放原文件备份和设备操作记录 |

上述材料保留在本机，通过命令参数指定。`patch.a6700.json`/`patch.a7c2.json` 描述机型补丁；预检生成的配置记录当前相机、所选 `pilot.so` 和刷写器的校验值。

A7C II 的 `patch.a7c2.json` 还记录 `target.accepted_sources`：同一套补丁窗口在原厂与其他已验证来源（如 FL23）上字节一致，安装工具按窗口比对，不要求整文件等于某个固定哈希。

## 2. 检查与升级常驻框架

相机连接电脑并设置为大容量存储 USB 模式。下面是 Windows PowerShell 示例，将路径替换为自己的本机材料位置：

```powershell
$pilotArgs = @(
  "--map", "F:/PilotLocal/patch.json",
  "--writer", "F:/PilotLocal/pilot_ap_flash.so",
  "--pilot", "F:/PilotLocal/pilot.so",
  "--transport-root", "./patch",
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

将模块文件放入 SD 卡 `/pilot/`，并将文件名加入现有 `/pilot/modules.txt` 的 `modules` 数组，保留其他条目及加载顺序。
相机 Fsys 会拒绝打开 `.json`（`0x80000041`），所以清单文件名是 `modules.txt`、内容仍是 JSON。
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
