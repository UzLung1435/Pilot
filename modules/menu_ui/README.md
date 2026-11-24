# Pilot Menu UI

在相机“自定义设置”菜单中增加 Pilot 入口，查看 Pilot 版本、模块列表、启用状态与文件路径。

**适用机型：仅 Sony ILCE-6700 固件 2.00。** 本模块硬编码了 2.00 `appFw.so` 的菜单表与 GOT 偏移，导出 `pilot_module_model_a6700`；其它机型的框架会拒绝加载，构建脚本也会拒绝非 `a6700` 的 `PILOT_MODEL`。

[下载 MenuUI v0.1.0](https://github.com/Himpq/Pilot/releases/tag/menu-ui-v0.1.0)

![Pilot 菜单界面](assets/menu-preview.png)

## 安装

1. 将 `pilot_menu_ui.so` 复制到 SD 卡的 `/pilot/` 目录。
2. 在现有 `/pilot/modules.json` 的 `modules` 数组中加入 `pilot_menu_ui.so`，保留其他模块条目。
3. 装卡并完整断电重启相机。
4. 打开“自定义设置 → Pilot”。

首次创建清单可使用：

```json
{
  "modules": ["pilot_menu_ui.so"]
}
```

## 使用

- 选择“Pilot 版本”查看版本信息。
- 选择模块名称查看清单中的启用状态与文件路径。
- 返回列表后继续选择其他模块。

模块状态来自清单配置，加载结果可查看 SD 卡的 `/pilot/log.txt`。

## 停用

从 `/pilot/modules.json` 中移除 `pilot_menu_ui.so`，然后完整断电重启。
保留其他模块条目，可继续使用对应功能。
