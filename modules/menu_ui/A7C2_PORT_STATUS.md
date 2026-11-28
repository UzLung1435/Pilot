# A7C II (ILCE-7CM2 2.01) menu_ui 移植状态

本文件记录 `modules/menu_ui` 的 A7C II 后端（`pilot_menu_ui_a7c2.c`）的移植依据、
已经落地的部分、以及仍然无法在离线阶段解决的部分。所有偏移都是对
`ILCE-7CM2 2.01` 的 `appFw.so`（`oldProjects/FlashDir/backup1/candidate/appFw.so`）
实测得到的，不是从 A6700 猜来的。

## 一、结论摘要

| 项目 | A6700 2.00 | A7C II 2.01 | 影响 |
| --- | --- | --- | --- |
| 屏幕表 | GOT `0x4E48818` → `0x44CA518`，项 `0x58` | GOT `0x05B4CFD0` → `0x051F7810`，项 `0x60` | 可原样替换指针 |
| 菜单组表 | GOT `0x4EA8A68` → `0x44B8AB0`，`0xF908` 字节 | GOT `0x05B80EC0` → `0x051DF8C8`，`0x15480` 字节 | 可原样替换指针 |
| 菜单行渲染 | 集中 layout 数组，`+0x34` 是文本 RID | **没有 layout 数组**，控件按名字创建 | A6700 的「改 layout 项」手法失效 |
| 文本绘制 | `TextSprite::setString(u32)` 遍地调用 | 全库只有 **`view_string::draw`（`0x1827F8C`）** 一处调用 | hook 点唯一且明确 |
| 当前组读取 | 全局函数 `0x2F731D8` | 没有对应的全局函数 | 无法直接判断「当前是不是 Pilot 组」 |

## 二、已完成

1. **`offsets_a7c2.h`（12011 字节）** —— 全部来自实测的偏移，未定位的项显式写
   `0x00000000ull` 并在注释里说明「依赖它们的代码会被编译掉，除非定义
   `PILOT_A7C2_ENABLE_UNVERIFIED`」，避免猜测值进相机。
2. **`pilot_menu_ui_a7c2.c`** —— 独立的 A7C II 后端，不改动 A6700 代码路径：
   - 菜单组注入：复制 `0x15480` 字节菜单表 → 在 category 0 的空槽写入
     `index=0x3F`、`rows=N` 的新组 → 改回 GOT。
   - 模块清单：用原生 `Fsys_Open/Fsys_Read/Fsys_Close` 读
     `<盘符>:/pilot/modules.txt`（失败再试 `modules.json`），解析 JSON 里的
     字符串数组，每个模块一行；读不到时回退到 4 行固定内容。
   - 文本注入：见第三节。
   - 选项注册：`pilot_ui_register_options_v1`。
3. **`build.sh`** —— `PILOT_MODEL=a7c2|a6700` 双分支；自动探测相机 sysroot
   （`$PILOT_SYSROOT` → `$PILOT_A7C2_SYSROOT` → `../work/pilot-a7c2/sysroot`），
   显式 `-I $SYSROOT/usr/include -L $SYSROOT/lib`（Debian 交叉 gcc 会把
   `$prefix/aarch64-linux-gnu/{include,lib}` 排在 `--sysroot` 之前，只有显式
   路径才能拿到相机 glibc 2.25 的头和库），并带 glibc 守卫
   （`__isoc23_*` 或 `GLIBC_x.y > 2.25` 直接失败）。
4. **两个变体都构建通过**（WSL `aarch64-linux-gnu-gcc` + `work/pilot-a7c2/sysroot`）：
   - a7c2 → `pilot_menu_ui.so` 71304 字节，`GLIBC_2.17`
   - a6700 → `pilot_menu_ui.so` 79456 字节，`GLIBC_2.17`
5. **导出 `pilot_ui_*` API**（与 A6700 的符号表对齐）：`pilot_ui_set_rid_override_v1`（改写指定 RID 的文本，下一次重绘即被 view_string hook 生效）与 `pilot_ui_register_options_v1`（收集其他模块的选项表并记日志）。A6700 的忙等框与 Live View 文本入口没有导出——A7C II 上还没有对应钩子，导出只会撒谎。`pilot_ui_get_language_v1` 两边都不导出，调用方按缺省回退英文。
6. **菜单清单解析器**有独立单测：`work/test_manifest_parse.sh`（数组/对象/空/无空格/
   `.so` 边界六种输入全部通过）。
7. **进度与依据文档**：本文件 + `a7c2_reverse_notes.md`（结构解构全过程）。

## 三、A7C II 的文本路径（已逆向，已 hook）

A7C II 的 appFw.so 里：

- `TextSprite::setString(unsigned)`（PLT `0x165EFC0`）、
  `TextSprite::setString(const char*)`（PLT `0x165D010`）、
  `ResourceManager::prefetchTextImage`（PLT `0x165EE20`）
  **各只有一处引用，全部落在 `0x1827F8C`（`view_string::draw`）内部**。
- `0x1827F8C` 有 134 个调用者（`sub_17Cxxxx`–`sub_17Dxxxx` 的各 view 类），
  菜单的每一段文字都要经过它。

视图对象上与之相关的字段：

| 偏移 | 类型 | 含义 |
| --- | --- | --- |
| `+0x1F0` / `+0x1F4` | f32 | 位置 x / y |
| `+0x1F8` / `+0x1FC` | f32 | 尺寸 w / h |
| `+0x204` | u8 | 可见/有效标志 |
| `+0x208` | u32 | 文本 RID（`0xFFFFFFFF` 表示不用 RID） |
| `+0x210` | char* | 直接字符串（RID 为 -1 时使用） |
| `+0x318` | ptr | `libra::Dictionary*`（可空） |
| `+0x320` | u32 | 文本段数量 |

注入方式（路线 A）：inline hook `0x1827F8C` 入口。函数序言 16 字节全部是
**非 PC 相对**指令，可以安全搬到别处执行：

```
0x1827F8C  A9AD7BFD  stp  x29, x30, [sp, #-0x130]!
0x1827F90  910003FD  mov  x29, sp
0x1827F94  39481001  ldrb w1, [x0, #0x204]
0x1827F98  A90153F3  stp  x19, x20, [sp, #-0x120]
```

实现在 `pilot_menu_ui_a7c2.c`：

1. 读 16 字节，与上表逐字节比对；不一致就**放弃安装并记日志**（固件版本不符时
   不会崩，只是失去自定义文本）。
2. `mmap` 一页 `PROT_READ|WRITE|EXEC`，写入「原 16 字节 + `ldr x16,#8; br x16` +
   原函数 +0x10」，作为 trampoline。
3. 把入口替换成 `ldr x16,#8; br x16; .quad hook`。
4. hook 先跑 C 函数 `a7c2_view_string_fixup(view)`：若 `+0x208` 落在本模块的
   RID 区间（`0x02FFFF00`–`0x02FFFF40`），就把 RID 改成 `0xFFFFFFFF`、
   把 `+0x210` 指向本模块的字符串，然后跳进 trampoline 执行原序言。

> AArch64 GCC **不支持** `__attribute__((naked))`（会报
> `'naked' attribute directive ignored`），入口 stub 用 global asm 块定义。

## 四、仍然缺失的偏移（离线阶段无法确认）

| 项 | 用途 | 状态 |
| --- | --- | --- |
| 按键 GOT（确认/返回/左右） | 拦截确认键打开自己的页面 | 未定位 |
| 当前组读取 | 判断「现在在不在 Pilot 组」，决定是否接管工厂 | 未定位（A7C2 无 A6700 那种全局函数） |
| 选中行 / 当前项 | 详情页显示「哪一行」的内容 | 未定位 |
| `0x1C3` 的 4 个文本槽 | 详情页正文（A6700 用 layout 索引 6/10/5/9） | 不存在 layout 数组，需另一条路 |
| 实时取景文本槽 | `pilot_lv_text_widget.inc` 用的 LV 文本控件 | 未定位 |

这些项缺失的后果是**功能范围**而非**稳定性**：菜单里会出现 Pilot 组和每个模块
一行，点进去打开的是原生详情屏；详情页正文、按键接线、LV 叠加暂时仍走原生逻辑。

## 五、构建与验证

```bash
# A7C II（需要相机 sysroot）
PILOT_MODEL=a7c2 bash modules/menu_ui/build.sh
# A6700
PILOT_MODEL=a6700 bash modules/menu_ui/build.sh
```

离线可做的验证：

- `work/menu_ui_a7c2_syntax.sh` —— 两种 model flag 下的 `-fsyntax-only`。
- `work/verify_menu_ui_build.sh` —— 交叉构建两个变体 + readelf + glibc 守卫。
- `work/test_manifest_parse.sh` —— 清单解析器单测。
- `aarch64-linux-gnu-objdump -d` 复核 hook stub 的机器码
  （`adrp x16,…` / `ldr x16,[x16,#…]` 指向 `g_a7c2_vs_trampoline`）。

**没有实机**，所以以下两点只能上机确认：菜单表复制后组是否出现在「操作自定义」
列表里、`view_string::draw` hook 是否真的改变了 RID 文本。

## 六、附：A7C II 关键偏移速查

```
屏幕表      GOT 0x05B4CFD0 -> 0x051F7810, 项 0x60, sid 在 +0x20（低 32 位）
  0x293     +0x00 presenter 工厂 0x01D54644 / +0x08 controller 工厂 0x01CF0F20
  0x1C3     +0x00 presenter 工厂 0x01DB0540 / +0x08 controller 工厂 0x01CD8478
菜单组表    GOT 0x05B80EC0 -> 0x051DF8C8, 0x15480 字节
  表头       +0x00 u32=8, +0x0C RID 0x02006451, +0x10 u32=1
  category   8 个, 步长 0x2B30；头 0x18：
             +0x00 flag, +0x08 cat_id, +0x0C 标题 RID, +0x10 组数,
             +0x18 起 15 个组槽 x 0x2E0
  组槽       +0x00 组序号, +0x04 组名 RID, +0x08 行数,
             +0x10 起 行 x 24B {u64 gid; u64 setting_desc; u64 non_setting_desc}
描述符 0x60  +0x00 标签 RID, +0x0C 打开屏 screen_id, +0x20 可用性回调
              （0x01BF20E0 = "return 1"）
文本绘制     view_string::draw 0x1827F8C
             view +0x204 可见 / +0x208 RID / +0x210 cstr / +0x318 dict / +0x320 段数
TextSprite   setString(u32)   PLT 0x0165EFC0  GOT 0x05BC1FE8
             setString(cstr)  PLT 0x0165D010  GOT 0x05BC1010
             setFont          PLT 0x0165E980  GOT 0x05BC1CC8
             drawText         PLT 0x0165D470  GOT 0x05BC1240
WidgetSystem sys_getInstance  0x05BC1388   postMousePress  0x05BC2AC8
             postMouseMove    0x05BC21F8   postMouseRelease 0x05BC20B8
             updateFrame      0x05BC21B0   notifyDraw       0x05BC2098
             processEvent     0x05BC1AF8   createLayer      0x05BC1540
```

## 七、决定性结论：A7C II 没有 A6700 那样的详情页

本轮把 A6700 详情页的四个文本槽名字（`No2_bodyinformation`、`No3_versionbody`、
`No4_lensinformation`、`No5_versionlens`）在 A7C II 的 `.rodata` 里找到了，但它们所属的
名字表（`0x3F242E0`）**零引用**，是旧资源残留；真正在用的三张名字表
（`0x3F15578` / `0x3F15338` / `0x3F150F8`）只被 `0x5B4E348` / `0x5B9EBE0` / `0x5B4CB20`
三个 presenter 构造参数结构引用。另外 `desc+0x0C` 指向的 0x2FD 是
`scr7186_0001_processing_controller`（处理中对话框），不是详情屏。

A7C II 的菜单是**单层列表 + 行内值**（`.rodata` 里有 `view_list_menu_item_with_value`、
`view_list_menu_settingvalue`），A6700 是**列表 → 详情页**两层。

**因此**「完整复刻」在 UI 层不能照搬五件套：本模块能做到的是
Pilot 组 + 每模块一行 + 行文本由本模块接管（已实现、已构建通过）。
要真正画出"详情页/选项弹窗"，只能走 canvas 自绘（`pilot_canvas_attach_v2` 可挂到
presenter 创建的 widget 上），那是下一步的独立工作量。

## 八、功能对照矩阵（A6700 2.00 → A7C II 2.01）

逐项对照 `pilot_menu_ui.c` 的全部对外行为。状态列的含义：
**完成** = 代码已实现并通过离线构建；**未验证** = 已实现但没有实机证据；
**缺失** = 没有实现（括号里写原因）。

| # | A6700 功能 | A7C II 状态 | 说明 |
| --- | --- | --- | --- |
| 1 | 在根菜单里出现 "Pilot" 组 | 完成 | 注入 category 0 的空组槽，`rows` 动态 |
| 2 | 组内列出已装模块 | 完成 | 解析 `<盘符>:/pilot/modules.txt`；行文本内联 `[on]`/`[off]`（`/pilot/disable` 与每模块 `disable` 标记） |
| 3 | 行文本由本模块接管 | 完成 | `view_string::draw` inline hook 替换 RID → cstr |
| 4 | `pilot_ui_set_rid_override_v1` | 完成 | 区间校验 + `rid_text_set`，已导出 |
| 5 | 模块详情页（名称/版本/描述） | 缺失 | A7C II 没有详情页（见第七节），无处承载 |
| 6 | 模块开关（toggle 写回清单） | 缺失 | 同上，没有可点击的控件 |
| 7 | 其他模块注册的选项页 | 部分 | 表已收（`g_opt_registry[8]`），但无页面展示 |
| 8 | 版本页 | 缺失 | A6700 走独立 detail 页，A7C II 无 |
| 9 | `pilot_ui_show_busy` / `hide_busy` | 缺失 | 0x260 忙等屏在 A7C II 上未验证，故不导出 |
| 10 | `pilot_ui_text_show` / `text_clear`（4 槽浮层） | 缺失 | 依赖详情页文本槽，同上 |
| 11 | Live View 文本控件 | 缺失 | `pilot_lv_text_widget.inc` 的 A6700 偏移尚未迁移 |
| 12 | DISP 虚拟页切换 | 缺失 | 依赖 DISP 键 hook，A7C II 的 GOT 未定位 |
| 13 | 中/英文本 | 部分 | 模块本身走英文；语言探测缺失即回退 EN |

**结论**：1、2、3、4 已完成；5–12 全部卡在"没有可控的屏幕内容"这一条根因上，
不是逐项可独立补的。要推进只能选一条：

- **路线 C（canvas 自绘）**：用 `pilot_canvas` 在菜单屏上画自家列表/详情/选项，
  需要先拿到 presenter 建的 widget 指针（`pilot_canvas_attach_v2` 的入参）；
- **路线 D（行内值）**：逆向 `view_list_menu_item_with_value` 的行结构，把状态直接写进
  列表行的"值"字段，不做独立详情页，最贴合 A7C II 原生交互。

两条都要实机才能确认，且路线 D 的成本明显更低。

## 实机日志：view_string hook 崩溃与修复

A7C II 模拟器上首个菜单绘制即触发 SEGV，崩溃点落在本模块 mmap 的 trampoline 上：

```
SEGV (PC=0x7e5552b008)
DABT at 0xff00a3bf, ESV 0x92000005
r0 = 0x00000000ff00a1bb
maps: 0x7e5552b000-0x7e5552c000 rwx   (4 KiB 匿名页 = trampoline)
```

`PC` 正好是 trampoline + 8，即搬迁过来的第三条指令 `ldrb w1, [x0, #0x204]`；
`0xff00a1bb + 0x204 = 0xff00a3bf` 与 DABT 地址吻合，说明 `x0` 是垃圾值。

根因：入口 stub 用 `bl a7c2_view_string_fixup` 调用 C 函数，但 **没有保存 x0**。
`x0` 是 caller-saved，而 fixup 会把 `x0` 当临时寄存器复用（反汇编里可见
`mov x0, #0xfffffffff000` 之类的常量装载），返回后 `x0` 已不是 view 指针，
trampoline 第一条访存指令立刻踩空。

修复：

- stub 改为 `stp x29,x30,[sp,#-32]!` + `stp x0,x1,[sp,#16]`，调用后再
  `ldp x0,x1` 恢复，保证进入搬迁序言时寄存器状态与原函数一致；
- fixup 增加指针合法性检查（`< 0x1000` 或 `>= 2^48` 直接忽略并只记一次日志），
  避免异常调用点把整机带走；
- GOT hook 里的 `setString(const char*)` 入口改为首次解析后缓存，不再每次
  `dlsym`。

修复后产物 71,392 B（sha256 `fe5eec28…`），入口 stub 反汇编：

```
ae0: stp x29, x30, [sp, #-32]!
ae4: mov x29, sp
ae8: stp x0, x1, [sp, #16]
aec: bl  1c00 <a7c2_view_string_fixup>
af0: ldp x0, x1, [sp, #16]
af4: ldp x29, x30, [sp], #32
af8: adrp x16, 20000
afc: ldr x16, [x16, #3912]
b00: br  x16
```

如需把 inline hook 排除掉做对照，构建时加 `-DPILOT_A7C2_NO_INLINE_HOOK`
（或 `PILOT_CFLAGS` 传递），此时只剩 GOT 层的 `setString` 钩子；日志会打印
`view_string hook disabled at build time`。

教训：任何从手写汇编进入 C 再回到“继续执行被搬迁指令”的钩子，都必须把
后续指令会读取的寄存器原样恢复；`x0-x7`、`x16/x17` 都是 caller-saved，
不能假设 C 函数会保留。
## 实机日志：菜单里看不到 Pilot 组

第二次实机运行（qemu）的 `LOG.TXT` 显示模块正常加载：

```
1970-01-01T00:00:28Z name=MNUI.so Pilot module initialized index=1/1 status=0
PILOT_FUSE state=COMPLETE package=C5C971E1450AF6CB
```

但菜单里没有 Pilot 组。根因是菜单表 `category` 结构的三个偏移全错了一位：

| 字段 | 错误实现 | 正确值 |
| --- | --- | --- |
| category 0 基址 | `table + 0x18` | `table + 0x08`（`0x051DF8D0`） |
| 组数 | `cat + 0x10` | `cat + 0x08` |
| 第一个组槽 | `cat + 0x18` | `cat + 0x10` |

于是目标槽被算成 `table + 0x30`，正好落在 cat0 第一个组的行数组上；
那里有非零的 `setting_desc` 指针，`memcmp` 校验失败后代码主动放弃注入，
所以菜单里什么都没有，日志也只留一行失败原因。

修正后：`install_menu_group()` 读 `table + 0x08` 的分类头、`cat + 0x08` 的
组数（cat0 = 1）、目标槽落在 `table + 0x2F8`（`0x051DFBC0`），该槽 0x2E0
字节全零。

离线验证加进仓库：`tests/test_menu_table_layout.c` 用真实菜单表前 0x3000
字节重放同一套算术，15 项断言全部通过（目标槽偏移、槽内全零、插入后组数
变为 2、原有组与槽 14 不受影响）。数据用 IDAPython 导出：

```python
open('a7c2_menu_table_head.bin','wb').write(get_bytes(0x051DF8C8, 0x3000))
```

修复后产物 71,392 B，sha256
`c12aa32427ed19b284e0f20385c07d9d35d74f6eb4bb223ae3bdce8bc806ac12`。
注入的组落在**第一个菜单分类**（cat0，标题 RID `0x02006451`）的最后一个位置；
组名 RID `0x02FFFF00` 在相机资源里不存在，靠 `setString` / `view_string` 钩子
替换成 "Pilot"，若看不到组名只剩空行，说明钩子没生效。

## 实机日志（第二轮）：menu table copy failed

换上新构建后 `MODULE.LOG` 给出了真正的第一层原因：

```
1970-01-01T00:00:28Z appFw base = 0x7f67f74000
1970-01-01T00:00:28Z menu table copy failed
1970-01-01T00:00:28Z menu group NOT installed
1970-01-01T00:00:28Z setString hook installed, original=0x7f96134944
1970-01-01T00:00:28Z view_string hook at 0x7f6979bf8c
```

`appFw base + 0x1827F8C` 正好等于 `0x7f6979bf8c`，钩子地址无误；失败的是菜单表
整表读取。原因是 Pilot API 的读接口有单页上限 ——
`patch/pilot_memory.c` 的 `locate_appfw_range()` 第一句就是

```c
if (!address || !length || length > 4096 || address > UINTPTR_MAX - length)
    return 0;
```

而 A7C2 的菜单表是 `0x15480` 字节，一次读完必然返回 `PILOT_STATUS_RANGE`。
上一提交修掉的 category 偏移错误是第二层问题：副本读不出来，就根本没走到
那一步校验。

修法是把 `read_mem()` 改成按 4 KiB 分块，所有调用点自动受益：

```c
while (done < len) {
    size_t chunk = len - done;
    if (chunk > 4096u) chunk = 4096u;
    if (g_api->memory_read(g_appfw_base + off + done,
                           (uint8_t *)dst + done, chunk) != PILOT_STATUS_OK)
        return -1;
    done += chunk;
}
```

修好后的产物 71,424 B，sha256
`a802b893ab23d6d3b50d0699af09378fe1a83b2d94551a5a63fd2a7e3a2bdf72`。

## 调试通道：把日志送到串口

qemu 崩溃后 SD 卡写不回去，`/pilot/modules/<名>/module.log` 只会保留崩溃前最后一次成功写入，注入过程本身反而看不到。模块因此把每条日志同时写进内核环形缓冲：

- 首选 `/dev/kmsg`，行首带 `<6>` 优先级前缀；
- 打不开时回退 `/dev/console`，用 `O_NONBLOCK` 打开，避免无人读的 console 把钩子卡住；
- 串口里所有行以 `pilot menu_ui:` 开头，`grep 'pilot menu_ui' console.log` 即可。

正常启动的先后顺序：

```
pilot menu_ui: worker start
pilot menu_ui: pilot menu_ui a7c2 <date> <time>
pilot menu_ui: appFw base = 0x...
pilot menu_ui: manifest: 1 module(s), global disable=0
pilot menu_ui: menu table copied: ... -> ..., cat0=... count=1
pilot menu_ui: cat0 count -> 2, slot=1, rows=1, desc_pool=0x...
pilot menu_ui: group: ...
pilot menu_ui: row0: ...
pilot menu_ui: desc0: ...
pilot menu_ui: group injected: category 0 slot 1, 1 rows
pilot menu_ui: menu group installed
pilot menu_ui: setString hook installed, original=0x...
pilot menu_ui: view_string hook at 0x...
pilot menu_ui: options registered
pilot menu_ui: init complete
```

`view_string: rid 0x... -> "..."` 最多打印 8 行，用来确认行文本接管确实生效。崩溃前最后一条 `pilot menu_ui:` 就是嫌疑位置：缺 `group injected` 说明卡在表拷贝或槽校验，缺 `init complete` 说明卡在钩子安装。
## 崩溃归属的对照实验（诊断构建）

qemu 上按 Menu 键时 appFw 在 `sub_1B765B0` 崩（`a1 = 0xb2e1`、`idx = 9`、访问 `0xb31f`）。
`a7c2_reverse_notes.md` 第十五节记录了枚举路径：崩溃函数的入参来自
`row.setting_desc`，而本模块写入的行 `setting_desc` 恒为 0，在 `sub_1B60228` 的
`CBZ` 处就被跳过，`gid` 也不等于特殊标记 `0xB81`。

要确认故障归属，构建两个产物：

| 产物 | 构建命令 | 行为 |
| --- | --- | --- |
| `pilot_menu_ui.so` | `bash modules/menu_ui/build.sh` | 注入菜单组（默认） |
| `pilot_menu_ui_noinject.so` | `PILOT_EXTRA_CFLAGS=-DPILOT_A7C2_NO_MENU_INJECT bash modules/menu_ui/build.sh <out>` | 只加载、枚举模块、注册选项，**不改菜单表** |

把 `noinject` 版复制成 `/pilot/MNUI.so` 后重现同样操作：

- **仍然崩溃** → 故障在 appFw/qemu 侧，与本模块注入无关；
- **不再崩溃** → 故障来自注入。此时优先换注入点：`cat0` 的分类代号是 0，走内部
  `while (v40 != 5)` 特殊路径；代号 1 的 category 走 `memcpy` 分支，是更安全的落点。

两份产物都在启动时往串口打印 `pilot menu_ui a7c2 <日期> <时间>`，用它确认卡上
跑的到底是哪一版。

### 串口日志缓存的教训

第一版 `console_emit()` 缓存了 `/dev/kmsg` 的描述符，前 8 行正常、之后全部丢失
（只剩 `FSYS_FILE_TRACE op=Open` 的痕迹）—— 框架会关闭或复用不属于自己的描述符。
现在每行重新 `open`/`write`/`close`，单条消息最多 320 字节缓冲区，代价可以忽略。
## 最终结论（2026-11-28）：菜单项路线停止

实测证明 A7 C II 的菜单条目名称与点击目标由**条目 id** 查表决定，描述符里的
label/target 不参与；文本又是预渲染图片。把行 id 换成真实项 `0x8` 立刻显示
「影像质量设置」并能进入其页面，换回自定义 id 就是空行 + 「此功能被禁止」。
完整证据见 `a7c2_reverse_notes.md` 第十七节。

因此**在 A7 C II 菜单里放 Pilot 入口这件事不可行**，与实现细节无关。

### 当前状态

- 菜单注入改为 opt-in（`-DPILOT_A7C2_ENABLE_MENU_INJECT`），**默认关闭**，默认产物
  不写原生菜单表。
- 默认产物保留三件事：读 `modules.txt` 得到模块清单、注册两个选项
  （`pilot_ui_register_options_v1`）、装 `setString` 与 `view_string` 两个 hook。
- 产物 `Pilot/build/MNUI.so`：71,672 B，sha256
  `6f760319787058512723166db420cc19c2531bed3dbb699ffbec02f54823de94`，
  `glibc <= GLIBC_2.17`。A6700 变体仍为 79,456 B / `0cf89a76…`，未受影响。
- 构建：`PILOT_MODEL=a7c2 bash modules/menu_ui/build.sh <out>`
  （opt-in 版加 `PILOT_EXTRA_CFLAGS=-DPILOT_A7C2_ENABLE_MENU_INJECT`）。

### 保留的知识

注入机制、逐行门控 hook、non-setting 描述符模板、`view_string::draw` 的文本替换
能力都已跑通并保留在代码里。若将来在 A7 C II 上找到可承载自定义内容的屏，这些是
现成的积木。