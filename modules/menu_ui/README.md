# Pilot Menu UI

在相机“自定义设置”菜单中增加 Pilot 入口，查看 Pilot 版本、模块列表、启用状态与文件路径。

**适用机型：Sony ILCE-6700 固件 2.00（默认）与 Sony ILCE-7CM2 固件 2.01。** 两机的菜单结构不同，后端也分成两份：

- 本目录的 `pilot_menu_ui.c` 是 A6700 实现：克隆 `appFw` 的菜单表与页表，插入自己的 group/item，导出 `pilot_module_model_a6700`，产物 `pilot_menu_ui.so`。
- [`a7c2/`](a7c2/README.md) 是 A7C II 实现（不含下文的 Live View 纯文本能力）：菜单表结构与 GOT 布局完全不同，改为在“设置选项”页追加一行并接管原生列表屏 `0x2cb`，导出 `pilot_module_model_a7c2`，产物 `pilot_menu_ui_a7c2.so`。

构建时用 `PILOT_MODEL` 选择；框架在加载模块前会核对机型标记，不符的模块不会被加载。

![Pilot 菜单界面](assets/menu-preview.png)

## 安装

1. 将 `pilot_menu_ui.so` 复制到 SD 卡的 `/pilot/` 目录。
2. 在现有 `/pilot/modules.txt` 的 `modules` 数组中加入 `pilot_menu_ui.so`，保留其他模块条目。
3. 装卡并完整断电重启相机。
4. 打开“自定义设置 → Pilot”。

相机 Fsys 会拒绝打开 `.json`（`0x80000041`），所以清单文件名是 `modules.txt`、内容仍是 JSON。首次创建清单可使用：

```json
{
  "modules": ["pilot_menu_ui.so"]
}
```

## 使用

- 选择“Pilot 版本”查看版本信息。
- 选择模块名称查看清单中的启用状态、文件路径，或进入模块注册的开关、枚举与动作子选项（`0x2A7` 选项弹窗与 `0x260` 忙等提示框）：

![模块子菜单选项示例](assets/menu_test_sample.png)

- 返回列表后继续选择其他模块。

模块状态来自清单配置，加载结果可查看 SD 卡的 `/pilot/log.txt`。

## 其他模块显示纯文本

公共头文件位于 `modules/menu_ui/pilot_ui.h`。在其他模块中包含它，可显示或清除 Live View 纯文本：

```c
#include "pilot_ui.h"

PilotStatus status = pilot_ui_show_text("Sensor mode ready", 3000u);
if (status != PILOT_STATUS_OK) return status;
/* duration_ms 为 0 时持续显示，直到清除。 */
status = pilot_ui_show_text("Recording mode active", 0u);
if (status != PILOT_STATUS_OK) return status;
return pilot_ui_clear_text();
```

文本使用 UTF-8，最多 159 字节；所有模块共用一条提示，新提示会替换旧提示。默认显示在左下方，使用原先 Pilot 提示的左对齐样式和录制时间的小字字体。显示时，服务核对原厂 AP `view_string` 对象，并通过 `setString(const char*)` 传入文字；每次绘制后立即恢复文字、位置和字体参数。提示清除或超时后，由正常的原厂刷新恢复显示。

调用模块需链接 `libdl.so.2`；`pilot_menu_ui.so` 必须启用并排在调用模块之前。服务未加载时返回 `PILOT_STATUS_NOT_FOUND`，Live View 钩子不可用时返回 `PILOT_STATUS_DISABLED`。

渲染实现单独位于 `pilot_lv_text_widget.inc`。日志中的 `PILOT_LV_UI_RENDER` 会记录实际进入字符串入口的次数、文字字节数和显示区域；绑定失败会记录 `PILOT_LV_UI_ERR stage=...`。

## 停用

从 `/pilot/modules.txt` 中移除 `pilot_menu_ui.so`，然后完整断电重启。
保留其他模块条目，可继续使用对应功能。
