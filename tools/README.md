# Pilot 通用补丁工具

公开代码与本机研究资料分开使用。Python 工具只依赖标准库，构建 RAM 模块时需要 AArch64 编译器和目标 sysroot。

| 内容 | 位置 |
|---|---|
| 通用 CLI、描述校验、RAM 运行器、合成示例和测试 | 公开仓库 `tools/` |
| 刷写入口与路径配置代码 | 公开仓库 `patch/on_camera.py`、`on_camera_config.py` |
| 实机补丁描述、固件镜像、匹配的刷写器、USB transport | 本机外部目录，通过参数指定 |
| 备份、日志、设备操作记录、生成模块 | 本机输出目录 |

工具不会上传设备文件。公开同步使用 `public-files.json` 中的逐项清单，不同步研究目录或本机 `patch.json`。

## 描述与离线镜像

```sh
python tools/pilot.py inspect tools/examples/file.synthetic.json
python tools/pilot.py image private/patch.json private/appFw.before.so --out private/appFw.after.so
python tools/pilot.py image private/patch.json private/appFw.after.so --restore --out private/appFw.restored.so

# A7C II 2.01 的现成描述（原厂镜像由本机提供）
python tools/pilot.py inspect patch.a7c2.image.json
python tools/pilot.py image patch.a7c2.image.json private/ILCE-7CM2-2.01-appFw.so --out private/appFw.a7c2.so
```

`pilot.patch.v1` 描述要求目标机型、固件、组件、文件大小和完整 SHA-256。
文件补丁还要求结果 SHA-256，每个修改点指定原值和新值。
整组原值和目标文件先验证，输出写入新文件，描述与原镜像保持原样。
`examples/` 使用合成数据，供检查格式与测试流程。

## AP / CP RAM 补丁

```sh
python tools/pilot.py ram source private/ram-patch.json --out private/pilot_example.c
python tools/pilot.py ram build private/ram-patch.json --cc aarch64-linux-gnu-gcc-11 --sysroot /path/to/a6700/sysroot --out private/pilot_example.so
```

RAM 描述使用 `space=ap/cp`。AP 地址是 `appFw.so` 相对地址，加载时转换为实际地址；
CP 地址是绝对地址，目前使用 SDK 支持的可写数据区。每项最多 64 字节，共最多 128 项。

生成模块先计算 `/usr/lib/appFw.so` 的完整文件哈希，匹配目标基线后再执行整组原值检查，
全部通过才按顺序调用 Pilot API 修改，并逐项回读。补丁在模块初始化时应用，作者需确定适用的启动时机。

`--restore` 构建显式恢复模块，交换原值/新值并倒序恢复。
写入失败会停止，日志记录失败项、尝试次数和成功调用次数；不会自动执行恢复。

## SD 卡模块管理

```sh
python tools/pilot.py modules list --card D:/
python tools/pilot.py modules deploy private/pilot_example.so --card D:/ --sha256 <模块SHA256> --backup-dir F:/PilotLocal/backups
python tools/pilot.py logs --card D:/ --out F:/PilotLocal/logs/session-1
```

部署检查模块 SHA-256、ELF64 AArch64 和现有清单，先将原模块和清单备份到卡外目录。
更新模块时保留原有加载顺序，再回读模块并原子替换清单。
日志导出只复制 Pilot 主日志及清单模块对应的 `module.log`。

## 相机刷写入口

`flash` 调用已有 `on_camera.py`；机型由 `--map` 中的 `target` 决定，A6700 2.00 与 A7C II 2.01 共用同一套检查、安装、更新与恢复流程：

```sh
python tools/pilot.py flash check --map F:/PilotLocal/patch.json --transport-root F:/PilotLocal/transport --state-dir F:/PilotLocal/device-state
python tools/pilot.py flash install --map F:/PilotLocal/patch.json --writer F:/PilotLocal/pilot_ap_flash.so --pilot F:/PilotLocal/pilot.so --transport-root F:/PilotLocal/transport --state-dir F:/PilotLocal/device-state --yes
```

刷写入口使用现有 `pilot.patch-map.v1`，与通用补丁描述 `pilot.patch.v1` 分开。
`--map` 必须提供匹配固件的完整描述和产物哈希；公开说明用的 `patch.a6700.json` / `patch.a7c2.json` 不含完整安装产物信息。A7C II 另附 `patch.a7c2.image.json` 供离线镜像校验，刷写器结果头可用 `flash.header` 覆盖（默认 `A6700PILOTFLASH\n`）。
USB transport 由本机提供 `cpload_run.py`。帮助、离线校验和 SD 卡操作不加载该依赖。
真实写入前备份涉及的原文件，设备状态单独写入 `--state-dir/device-state.json`，不改补丁描述。

## 公开版同步与验证

```sh
python tools/sync_public.py --destination /path/to/public-Pilot --write
python tools/sync_public.py --destination /path/to/public-Pilot
python -m unittest discover -s tools/tests -p 'test_*.py'
python -m unittest discover -s patch/tests -p test_on_camera_offline.py
cc -std=c11 -O2 -Wall -Wextra -Werror -I patch -I tools/runtime tools/runtime/pilot_patch_runtime.c tools/tests/test_runtime.c -o /tmp/pilot-runtime-test
/tmp/pilot-runtime-test
```

同步仅修改指定的本地公开仓库目录，不执行 Git 提交或推送。
C SHA-256 测试使用本机 `cc`，Windows 下可在 WSL 运行。
