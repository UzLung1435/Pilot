# 6K

为相机增加 6K 内录，模块文件为 `pilot_6k.so`。

**适用机型：仅 Sony ILCE-6700 固件 2.00。** 本模块硬编码了 2.00 的录制档位表、GOT 与 CP 地址，导出 `pilot_module_model_a6700`；A7C II 等其它机型的框架会拒绝加载，构建脚本也会拒绝非 `a6700` 的 `PILOT_MODEL`。

[下载 6K v0.1.0](https://github.com/Himpq/Pilot/releases/tag/6k-v0.1.0)

## 功能

将 **XAVC HS 4K 24p** 档位的录制输出替换为 **5760×3240、23.976p**。
相机菜单仍显示原来的 4K 24p，录制文件为 6K 分辨率。

## 安装

请先安装 Pilot。

1. 将 `pilot_6k.so` 复制到 SD 卡 `/pilot/`。
2. 在现有 `/pilot/modules.txt` 的 `modules` 数组中加入 `pilot_6k.so`，保留其他模块条目。
3. 装卡并完整断电重启相机。

单独加载本模块的清单示例：

```json
{
  "modules": ["pilot_6k.so"]
}
```

从旧版两模块升级时，将以下两项从清单移除，改用 `pilot_6k.so`：

- `pilot_hs6k30_6k_ddrcd_ippp.so`
- `pilot_cp_6k_og.so`

旧文件可以保留在卡上。

## 使用

1. 文件格式选择 **XAVC HS 4K**。
2. 录制帧率选择 **24p**。
3. 按录制键开始，完成后正常停止录制。

安装 MenuUI 后，模块列表显示为 **6K**。
加载结果可查看 `/pilot/log.txt`，模块日志位于 `/pilot/modules/pilot_6k/module.log`。

## 停用

从 `/pilot/modules.txt` 中移除 `pilot_6k.so`，保留其他模块条目，然后完整断电重启。
原来的 4K HS 录制恢复。
