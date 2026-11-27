# Pilot Menu UI

在相机“自定义设置”菜单中增加 Pilot 入口，查看 Pilot 版本、模块列表、启用状态与文件路径。

![Pilot 菜单界面](assets/menu-preview.png)

## 适用机型

| 机型 | 固件 | 源码 | 状态 |
| --- | --- | --- | --- |
| ILCE-6700 | 2.00 | `pilot_menu_ui.c` | 实机验证 |
| ILCE-7CM2 (A7C II) | 2.01 | `pilot_menu_ui_a7c2.c` | 离线适配完成，功能见 [A7C2_PORT_STATUS.md](A7C2_PORT_STATUS.md) |

两份源码由 `build.sh` 按 `PILOT_MODEL` 选择，产物互不通用（模块内带机型标记，
框架会拒绝加载异机型模块）。

## 构建

```bash
# ILCE-6700 2.00
PILOT_MODEL=a6700 bash modules/menu_ui/build.sh

# ILCE-7CM2 2.01
PILOT_MODEL=a7c2 bash modules/menu_ui/build.sh
```

`build.sh` 会依次尝试 `PILOT_SYSROOT`、对应机型的 `PILOT_A6700_SYSROOT` /
`PILOT_A7C2_SYSROOT`，以及仓库旁的 `work/pilot-a7c2/sysroot`。它显式传
`-I $SYSROOT/usr/include -L $SYSROOT/lib`：交叉工具链自带的头文件是宿主 glibc
（2.38+），只有显式路径才能拿到相机的 glibc 2.25，否则产物会出现
`GLIBC_2.34`/`__isoc23_*` 依赖。构建末尾会校验 AArch64、无 TEXTREL、
`pilot_module_init`/`pilot_module_model_<model>` 符号，并拒绝 glibc 要求高于 2.25
的产物。

## 安装

1. 将 `pilot_menu_ui.so` 复制到 SD 卡的 `/pilot/` 目录。
2. 在 `/pilot/modules.txt` 的 `modules` 数组中加入 `pilot_menu_ui.so`，保留其他模块条目
   （相机文件系统不接受 `.json` 后缀，清单名为 `modules.txt`，内容仍是 JSON）。
3. 装卡并完整断电重启相机。
4. 打开“自定义设置 → Pilot”。

首次创建清单可使用：

```json
{
  "modules": ["pilot_menu_ui.so"]
}
```

## 使用

### ILCE-6700

- 选择“Pilot 版本”查看版本信息。
- 选择模块名称查看清单中的启用状态、文件路径，或进入模块注册的开关、枚举与动作子选项（`0x2A7` 选项弹窗与 `0x260` 忙等提示框）：

![模块子菜单选项示例](assets/menu_test_sample.png)

- 返回列表后继续选择其他模块。

模块状态来自清单配置，加载结果可查看 SD 卡的 `/pilot/log.txt`。

### ILCE-7CM2 (A7C II)

按“功能等价”实现：A7C II 没有 A6700 那种详情页，信息直接内联在列表行上。

- 菜单里出现 `Pilot` 组，组内每个已装模块一行。
- 行文本是 `模块名 [on]` / `模块名 [off]`；全局 `/pilot/disable` 或该模块目录下的
  `disable` 文件存在时显示 `[off]`。
- 开关目前通过这两个标记文件完成（改完需完整断电重启），状态会反映到行文本上。
- 版本页、模块选项弹窗与 Live View 纯文本仅在 ILCE-6700 上可用。

## 其他模块显示纯文本（仅 ILCE-6700）

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

## A7C II 说明

A7C II 的菜单 UI 与 A6700 同源（索尼四代机共用一套设计），但没有 A6700 那种集中
layout 数组，文本走单一入口 `view_string::draw`。因此 A7C II 后端采用「注入菜单组 +
inline hook 文本入口」的方式：

- 菜单组注入到 category 0 的空组槽（组表基址 `0x051DF8C8`，GOT `0x05B80EC0`）；
- 行描述符的 `+0x0C` 指向 `0x2CB`（`scr7001_1004`，组内项列表屏），而不是 A6700 用的
  `0x1C3`——在 A7C II 上 `0x1C3` 是 `dlg_rec231` 对话框；
- 行文本经 `view_string::draw` 入口 hook 替换（RID 区间 `0x02FFFxxx` → 模块内字符串，
  文本由模块持有，不依赖相机资源）。

细节与实测偏移见 [A7C2_PORT_STATUS.md](A7C2_PORT_STATUS.md) 与
[a7c2_reverse_notes.md](a7c2_reverse_notes.md)。

移植过程中的早期草稿（`A7C2_FINAL_SUMMARY.md`、`A7C2_PORTING_GUIDE.md`、`README_A7C2*.md`、
`CURRENT_STATUS.md`、`ida_tools/` 等）已归档到工作区 `tmp/menu_ui-legacy-docs/`，
其结论已被上面两份文档取代。

## 停用

从 `/pilot/modules.txt` 中移除 `pilot_menu_ui.so`，然后完整断电重启。
保留其他模块条目，可继续使用对应功能。
