# Pilot MenuUI — ILCE-7CM2 (A7C II) 2.01 逆向笔记

数据库：`D:\Code\Sony_Cam_Compare\appFw.so.i64`（root `appFw.so`，99,960,744B，imagebase 0，333,346 函数）。
机型由 `oldProjects\FlashDir\backup1\state.json` 确认：`"model": "ILCE-7CM2", "version": "2.01"`。

## 段布局

| 段 | 范围 |
| --- | --- |
| `.text` | `0x1660680 - 0x37041A0` |
| `.rodata` | `0x37041b0 - 0x51A09E4` |
| `.data.rel.ro` | `0x51B82A0 - 0x5B4BB60` |
| `.got` | `0x5B4BE70 - 0x5BC0FE8` |
| `.got.plt` | `0x5BC0FE8 - 0x5BC2B58` |
| `.data` | `0x5BC2B58 - 0x5F63FB8` |
| `.bss` | `0x5F63FB8 - 0x98A1CD0` |
| `extern`（`__imp_*`） | `0x98A1CF0 - 0x98A3A70` |

## 屏幕表（等价 A6700 的 page table）

- GOT 槽 `0x05B4CFD0` → 表基址 `0x051F7810`（`.data.rel.ro`）。
- 项大小 `0x60`，**按 screen_id 直接索引**：`0x051F7810 + 0x60 * sid`。
- 项内：`+0x00` presenter getter、`+0x08` controller getter、`+0x20` screen_id（低 32 位）、`+0x24` screen_type、`+0x58` trailing getter。
- `0x000…0x35E` 共 837 项带匹配 sid，其后到 `0x6FF` 全 0（空闲可扩展）。
- `0x293` @`0x05206F30` → presenter `0x01D54644`、controller `0x01CF0F20`、`+0x20=0x0000000300000293`。
- `0x1C3` @`0x05202130` → presenter `0x01DB0540`、controller `0x01CD8478`、`+0x20=0x00000003000001C3`。
- 该表同时被 `presenter_controller_manager` 当作 per-screen 运行时记录使用（`sub_1C6E7C8` set_close_timer、`sub_1E4B8F8` reprocess_input_event）。

## 菜单组表（等价 A6700 的 menu table）

- GOT 槽 **`0x05B80EC0`** → 表基址 **`0x051DF8C8`**（`.data.rel.ro`）。
- 8 个 category，步长 **`0x2B30`**；`sub_1B59648(a1, idx)` 中可见 `2764 dwords` 步长与 `idx > 7` 判断。
- category 头 `0x18` 字节：
  - `+0x00` u32 标志（cat0 = 0x08，其余 = 0）
  - `+0x0C` u32 category 标题 RID
  - `+0x10` u32 组数（count）
  - `+0x18` 起 15 个组槽，每槽 **`0x2E0`**
- 组槽：
  - `+0x00` u32 x（全局组序号，0…0x3d）
  - `+0x04` u32 组名 RID
  - `+0x08` u32 行数（rows）
  - `+0x10` 起 `rows × 24B`，每行 `{ u64 group_id; u64 setting_desc; u64 non_setting_desc; }`
- 全部 category 与组（cat: title → 组 x/rid/rows）：

```
cat0 0x51df8c8 title=0x02006451 count=1
  g0 @0x51df8e0 x=0x3e rid=0x0200605e rows=6
cat1 0x51e23f8 title=0x02006092 count=1
  g0 @0x51e2410 x=0x40 rid=0x02006054 rows=1
cat2 0x51e4f28 title=0x02006093 count=15
  g0 @0x51e4f40 x=0x00 rid=0x0200606e rows=30
  g1 @0x51e5220 x=0x01 rid=0x02006057 rows=4
  g2 @0x51e5500 x=0x02 rid=0x02006044 rows=10
  g3 @0x51e57e0 x=0x03 rid=0x02006074 rows=19
  g4 @0x51e5ac0 x=0x04 rid=0x0200a59d rows=2
  g5 @0x51e5da0 x=0x05 rid=0x0200603c rows=10
  g6 @0x51e6080 x=0x06 rid=0x02006036 rows=3
  g7 @0x51e6360 x=0x07 rid=0x0200607c rows=11
  g8 @0x51e6640 x=0x08 rid=0x02006080 rows=9
  g9 @0x51e6920 x=0x09 rid=0x02006082 rows=6
  g10 @0x51e6c00 x=0x0a rid=0x02006081 rows=8
  g11 @0x51e6ee0 x=0x0b rid=0x0200608c rows=13
  g12 @0x51e71c0 x=0x0c rid=0x02006077 rows=6
  g13 @0x51e74a0 x=0x0d rid=0x02006055 rows=8
  g14 @0x51e7780 x=0x0e rid=0x0200607a rows=9
cat3 0x51e7a58 title=0x02006090 count=7
  g0 @0x51e7a70 x=0x0f rid=0x0200603f rows=17
  g1 @0x51e7d50 x=0x10 rid=0x02006040 rows=4
  g2 @0x51e8030 x=0x11 rid=0x02006058 rows=4
  g3 @0x51e8310 x=0x12 rid=0x02006046 rows=12
  g4 @0x51e85f0 x=0x13 rid=0x02006087 rows=5
  g5 @0x51e88d0 x=0x14 rid=0x0200604e rows=7
  g6 @0x51e8bb0 x=0x15 rid=0x0200608b rows=2
cat4 0x51ea588 title=0x02006091 count=5
  g0 @0x51ea5a0 x=0x16 rid=0x0200602b rows=16
  g1 @0x51ea880 x=0x17 rid=0x02006048 rows=12
  g2 @0x51eab60 x=0x18 rid=0x0200606c rows=12
  g3 @0x51eae40 x=0x19 rid=0x0200604b rows=8
  g4 @0x51eb120 x=0x1a rid=0x02006067 rows=3
cat5 0x51ed0b8 title=0x0200608f count=7
  g0 @0x51ed0d0 x=0x1b rid=0x02006069 rows=6
  g1 @0x51ed3b0 x=0x1c rid=0x0200603e rows=3
  g2 @0x51ed690 x=0x1d rid=0x0200604f rows=7
  g3 @0x51ed970 x=0x1e rid=0x02006037 rows=4
  g4 @0x51edc50 x=0x1f rid=0x0200604d rows=9
  g5 @0x51edf30 x=0x20 rid=0x02006086 rows=4
  g6 @0x51ee210 x=0x21 rid=0x0200606a rows=12
cat6 0x51efbe8 title=0x02006060 count=14
  g0 @0x51efc00 x=0x22 rid=0x0200607e rows=6
  g1 @0x51efee0 x=0x23 rid=0x02006052 rows=7
  g2 @0x51f01c0 x=0x24 rid=0x0200607f rows=8
  g3 @0x51f04a0 x=0x25 rid=0x0200604c rows=1
  g4 @0x51f0780 x=0x26 rid=0x0200a59c rows=2
  g5 @0x51f0a60 x=0x27 rid=0x02006033 rows=7
  g6 @0x51f0d40 x=0x28 rid=0x02006038 rows=3
  g7 @0x51f1020 x=0x29 rid=0x0200608a rows=6
  g8 @0x51f1300 x=0x2a rid=0x0200602f rows=5
  g9 @0x51f15e0 x=0x2b rid=0x02006088 rows=3
  g10 @0x51f18c0 x=0x2c rid=0x0200a003 rows=2
  g11 @0x51f1ba0 x=0x2d rid=0x02006085 rows=6
  g12 @0x51f1e80 x=0x2e rid=0x021c6b7a rows=2
  g13 @0x51f2160 x=0x2f rid=0x02006061 rows=8
cat7 0x51f2718 title=0x02006454 count=14
  g0 @0x51f2730 x=0x30 rid=0x0200602e rows=3
  g1 @0x51f2a10 x=0x31 rid=0x02006031 rows=2
  g2 @0x51f2cf0 x=0x32 rid=0x02006065 rows=18
  g3 @0x51f2fd0 x=0x33 rid=0x02006039 rows=12
  g4 @0x51f32b0 x=0x34 rid=0x02006083 rows=14
  g5 @0x51f3590 x=0x35 rid=0x02006029 rows=3
  g6 @0x51f3870 x=0x36 rid=0x02006045 rows=9
  g7 @0x51f3b50 x=0x37 rid=0x0200605a rows=4
  g8 @0x51f3e30 x=0x38 rid=0x02006072 rows=12
  g9 @0x51f4110 x=0x39 rid=0x0200603d rows=11
  g10 @0x51f43f0 x=0x3a rid=0x02006032 rows=8
  g11 @0x51f46d0 x=0x3b rid=0x02006084 rows=5
  g12 @0x51f49b0 x=0x3c rid=0x02006042 rows=4
  g13 @0x51f4c90 x=0x3d rid=0x02006030 rows=17
```

表尾 = `0x51f2718 + 0x2B30 = 0x51F5248`，即整表 `0x15480` 字节（`0x51DF8C8 … 0x51F5248`）。

### 12 行组（"操作自定义"候选）

| 地址 | 组名 RID | 行 gid |
| --- | --- | --- |
| `0x51e8310` | `0x02006046` | `1cb 1cc 1cd 1ce 1cf 1d0 1d1 1d2 1d3 1d6 1d7 1d8` |
| `0x51ea880` | `0x02006048` | `1fb … 206` |
| `0x51eab60` | `0x0200606c` | `207 … 212` |
| `0x51ee210` | `0x0200606a` | `251 252 253 254 255 256 259 25a 25b 25c 263 264` |
| `0x51f2fd0` | `0x02006039` | `3be 3bf 3c0 3d9 3da 3db 3dc 3dd 3de 3df 3e0 3e1` |
| `0x51f3e30` | `0x02006072` | `41d … 428` |

全部 6 个组的 `slot[12]`/`slot[13]` 均为 0（空槽，可插入）。
A6700 的 `APPFW_CUSTOM_GROUP_OFF 0xDE68` / `APPFW_CUSTOM_SLOT12_OFF 0xDF98` 相差 `0x130 = 0x10 + 12*24`，与 A7C2 组结构一致。

### desc 结构（初步，两组样本）

```
setting_desc  0x59e3988: +00 0200a353  +04 ffffffff  +08 ffffffff  +0c 0204e13f
                         +10 ffffffff  +14 00000b81  +18 0216004a  +1c 0216004b
                         +20 000000d4  +24 000000d4  +28 00010001  +2c 00010001
non_setting   0x5a80db0: +00 021c6639  +04 021c66c0  +08 ffffffff  +0c 000002fd
                         +10 000002fd  +14 00000001  +18 0000065f  +1c 00000000
                         +20 01bf20e0  +24 00000000  +28 00000000  +2c 00000000
```

A6700 的 desc 是 `+0x00 label RID / +0x0C screen_id / +0x20 visibility / +0x50 0`，A7C2 明显不同，需继续确认字段语义。

## page table 项的真实语义（重要修正）

`+0x00` 与 `+0x08` 不是"工厂"，而是 **getter**：

- `+0x00` presenter getter：`sub_1DB0540(a1)` / `sub_1D54644(a1)` → `v1 = qword_7603168[a1]; if (v1) sub_1Dxxxxx(v1); return v1;`
  —— **presenter 对象在屏幕注册时就已构造好，getter 只是取缓存并做一次 refresh**。hook 这个槽无法改变 UI 构建。
- `+0x08` controller getter：`sub_1CD8478(idx, a2, a3, a4, a5, a6)` → `obj = &unk_7603190 + 20480*idx; sub_1CDxxxx(obj, a2, a3, a4, a5, idx, screen_id_const, a6)` —— **按需构造 controller**，是唯一值得 hook 的槽。

两个槽都只被 page table 数据引用（`XrefsTo` 分别只有 `0x5206F38` / `0x5202138` 一处数据 xref），调用方经函数指针间接调用 → **A7C2 与 A6700 一样，直接把 page table 项改成自己的函数即可，无需知道调用者**。

### controller ctor 的已知字段

`sub_1CF0D88`（`s_o10_001_controller`，0x293）与 `sub_1CD82E0`（`dlg_rec231_controller`，0x1C3）：

```
(obj, a2, a3=i_view*, a4, a5, a6=pool_idx, a7=screen_id_const, a8)
  sub_1AC61F8(obj, ...)            // 基类
  *(u32*)(obj+376) = a7            // 659(0x293) / 451(0x1C3)
  *(u64*)(obj+384) = a2
  *(u64*)(obj+392) = a4
  *(u32*)(obj+400) = a8            // ← 候选：组/模式标识
  *(u64*)obj       = vtable
  *(u64*)(obj+408) = &unk_3939198 / &unk_3939108   // a8 != 0 时取后者
  sub_1A8C328(obj+416)
  *(u64*)(obj+760) = a3            // i_view
  sub_1A8A86C(obj+416, a5)
  *(u16*)(obj+768) = (*(ivtbl+56))(i_view)
```

## desc 结构（A7C2 已确认语义）

非设置项（`0x5a80db0`）：

| 偏移 | 值 | 语义 |
| --- | --- | --- |
| `+0x00` | RID `0x021c6639` | 主标签 RID |
| `+0x04` | RID `0x021c66c0` | 副标签 RID |
| `+0x08` | `0xffffffff` | 空 |
| `+0x0C` | u32 `0x2fd` | 点击打开的 screen_id |
| `+0x10` | u32 `0x2fd` | 同上（重复） |
| `+0x14` | u32 `1` | 类型/标志 |
| `+0x18` | u32 `0x65f` | 目标 id |
| `+0x20` | `0x01bf20e0` | **可用性回调**（`__int64 f(void){return 1;}` 形态） |

设置项（`0x59e3988`）在 `+0x20` 处是 `0xd4`（数值而非指针），`+0x0C` 处是 RID —— 说明结构体按 `+0x14` 的类型标签分支解释。

`desc+0x20` 的可用性回调与 A6700 的 `APPFW_VISIBILITY_OK_OFF` 语义完全一致，可照抄。

## A7C2 的 UI 构建方式与 A6700 的根本差异

`dlg_rec231_presenter` ctor 末尾：

```c
ivtbl[2](i_view, screen_id, prop, 10, orientation, presenter+24, "No10_Background", &unk_3F15578);
ivtbl[10](i_view);
```

即 **A7C2 按名字（`"No10_Background"` 这类常量字符串）向 i_view 申请控件**，控件结构与坐标由 i_view 内部管理；而 A6700 的 0x1C3/0x293 屏是从集中式 layout 数组（13 项 / 12 项 × 0x60）按索引构造。

**推论**：A6700 的 `GOT_LAYOUT_1C3 → 0x4FD7130`、`GOT_LAYOUT_293 → 0x508C270` 在 A7C2 **不存在等价物**（多次全库扫描 `+0x34=RID`、`+0x20..+0x2C` 为合理矩形的 0x60 项，0 命中）。因此"改 layout item 的 RID 来换文本"这条路在 A7C2 上不可行。

## 复刻路线的取舍

| 路线 | 做法 | 可行性 |
| --- | --- | --- |
| A. 全盘复刻 A6700 五件套 | 注入菜单表 + 改 layout + 改 page table factory | **A7C2 无 layout 数组 → 不可行** |
| B. 菜单注入 + `setString` 替换 | 构造 desc（自定义 label RID + screen_id），hook `TextSprite::setString(u32)`，RID 落在 Pilot 自定义区间时替换成 Pilot 文案 | **可行**，与 A6700 的 `pilot_set_string_rid_hook` 同构，不依赖 layout |
| C. 自绘（pilot_canvas） | 只保留菜单入口，UI 全用 canvas 画 | 可行但偏离"复刻 menu_ui" |

**选定 B**，并在 B 上补按键 hook 与 LV 文本槽。

## 仍未解出的关键项

1. **判定"当前在 Pilot 组"**：A6700 用全局 `GET_CUR_GROUP 0x2F731D8`（返回 `const uint32_t*` 的函数），A7C2 没有对应物。`sub_1B5A24C` 里的 `(*(vtable+824))(obj)` 在 controller 对象上取到的是数据指针 `0x5B0D5D8`，说明该 `obj` 不是 controller —— 此线索作废。**候选**：controller ctor 的 `a8`（存 `obj+400`）即组/模式标识。
2. **0x293/0x1C3 屏的项文本控件**：需要知道哪些 `setString` 调用属于这两个屏（RID 白名单或调用栈判定）。
3. **按键 GOT**：确认/返回/左右/DISP 的 PLT 槽（A6700 的 `BTN_CONFIRM 0x4ED84A0` 等对应物）。
4. **LV 文本槽**：A6700 的 `APPFW_LV_LCD_VTABLE_UPDATE_OFF 0x4C6DBF8` / `APPFW_LV_LCD_LAYOUT_GOT_OFF 0x4EF70A8` / `APPFW_LV_LCD_TEXT_IDX 391` 等在 A7C2 的对应地址。
5. **模块选项页与 RID 映射**：`pilot_ui_set_rid_override_v1` 的 RID → 文本表沿用 A6700 常量即可，但需要挂到 A7C2 的 `setString` hook 上。

## TextSprite GOT（`setString` hook 点）

| 函数 | PLT stub | GOT |
| --- | --- | --- |
| `_ZN2ux6gfxeng10TextSprite9setStringEj` | `0x0165EFC0` | `0x05BC1FE8` |
| `_ZN2ux6gfxeng10TextSprite9setStringEPKc` | `0x0165D010` | `0x05BC1010` |
| `_ZN2ux6gfxeng10TextSprite7setFontEPjh` | `0x0165E980` | `0x05BC1CC8` |
| `_ZN2ux6gfxeng10TextSprite8drawTextEv` | `0x0165D470` | `0x05BC1240` |

## WidgetSystem GOT

| 函数 | GOT |
| --- | --- |
| `sys_getInstance` | `0x05BC1388` |
| `createLayer` | `0x05BC1540` |
| `processEvent` | `0x05BC1AF8` |
| `notifyDraw` | `0x05BC2098` |
| `postMouseRelease` | `0x05BC20B8` |
| `updateFrame` | `0x05BC21B0` |
| `postMouseMove` | `0x05BC21F8` |
| `updateCurrentTime` | `0x05BC2578` |
| `postMousePress` | `0x05BC2AC8` |

## 屏幕类

| 屏 | RTTI 类 | vtable | ctor | getter |
| --- | --- | --- | --- | --- |
| `0x293` 列表 | `s_o10_001_controller` | `0x5B0D2C0`（typeinfo `0x5B0D298`，名 @`0x4125B80`） | `sub_1CF0D88`（`v19=659`） | `sub_1CF0F20`（池 `unk_7603190 + 20480*idx`） |
| `0x1C3` 详情 | `dlg_rec231_controller` | `0x59569A8`（typeinfo `0x5956980`，名 @`0x3d7ee58`） | `sub_1CD82E0`（`v19=451`，`a1+416` 字段） | `sub_1CD8478`（池 `unk_7603190 + 20480*idx`） |

`0x293` 的 view：`view_list_menu_group_table::update()` = `0x017FBDB0`；行矩形 `.bss` `0x075935E0`（每行 4 dword x/y/w/h）与 `0x07593800`；行 RID 表 `0x0375FA00`（`0x02002695, 0x02002697, 0x02002699, 0x0200269B`）。

## scr7001_1001..1007 屏族（vtable stride 0x68）

| 类 | ztv | GOT 槽 | ctor | 工厂 | getter |
| --- | --- | --- | --- | --- | --- |
| 1001 | `0x5B13488` | `0x5B638C8` | `0x2170898` | `0x1CF7120` | `0x1CF72B8` |
| 1002 | `0x5B134F0` | `0x5B92940` | `0x21764B8` | `0x1CF72F8` | `0x1CF7490` |
| 1003 | `0x5B13558` | `0x5B57D90` | `0x217DA30` | `0x1CF74D0` | `0x1CF7668` |
| 1004 | `0x5B135C0` | `0x5B513F8` | `0x21824B4` | `0x1CF76A8` | `0x1CF7840` |
| 1005 | `0x5B13628` | `0x5B5BAE0` | `0x2185828` | `0x1CF9EE8` | `0x1CFA07C` |
| 1006 | `0x5B13690` | `0x5B99150` | `0x2189FDC` | `0x1CFA0BC` | `0x1CFA250` |
| 1007 | `0x5B136F8` | `0x5B807A8` | `0x218CF3C` | `0x1CFA290` | `0x1CFA424` |

## 已排除的误判

- `0x051C1230` 不是 ctrl items 数组，是每 `0x50` 一个的工厂函数指针簇（`0x1AC7060, 0x1AC70B0, …`）。
- `0x051F7810` 不只是 factory 表，还是 per-screen 运行时状态表（`presenter_controller_manager` 直接索引）。
- `0x520C730`（A7C2 表基址 + A6700 的 `0xDE68`）不是组结构。

## 待办（按依赖顺序）

1. ~~确认 6 个 12 行组中哪一个是"操作自定义"~~ —— 不需要了：路线 B 不再往"操作自定义"组里插入，而是新建独立组。
2. ~~A7C2 desc 结构~~ —— 已完成，见上文。
3. ~~`0x1C3` 的 4 个文本槽 layout 数组~~ —— **A7C2 不存在，放弃该路线**。
4. ~~`0x293` 的 12 项 layout 数组~~ —— 同上。
5. **菜单表注入**：`0x51DF8C8` 整表 `0x15480` 字节复制到模块自备缓冲，在 cat0（`0x51DF8C8`，count=1，有空组槽 1..14）追加一个组；把 GOT `0x05B80EC0` 改为指向副本。
6. **判定当前组**：从 controller ctor 的 `a8`（`obj+400`）入手；若不成，退化为"对 0x293/0x1C3 全量 hook 且不改显示"，仅做 setString 替换。
7. **按键 hook**：找 0x293/0x1C3 controller vtable 里的 `onConfirm`/`onBack`/`onLeft`/`onRight` 槽。
8. **LV 文本槽**：找 A7C2 的 LV presenter update vtable 槽与 4 个文本 layout 项。
9. **模块选项页与 RID 映射**：把 `pilot_ui_register_options` 的数据渲染到 0x293 屏。

## 代码改造计划

- `offsets_a7c2.h`：填充上表所有 `[OK]` 偏移，`[TODO]` 留 `0` 并在 `.c` 里用 `#if APPFW_*_A7C2 == 0` 编译期报错，避免"编过了但跑不起来"。
- `pilot_menu_ui.c`：把所有 A6700 偏移包进 `#if !PILOT_MODEL_A7C2`，A7C2 分支走路线 B。
- `build.sh`：`MODEL=a7c2` 时 `-DPILOT_MODEL_A7C2`，并复用 pilot_canvas 的 sysroot 自动探测（`$ROOT/../work/pilot-a7c2/sysroot`，`-I $SYSROOT/usr/include -L $SYSROOT/lib`，GLIBC ≤ 2.25 守卫）。

## 十一、A7C II 的控件创建机制（下一步入口，尚未使用）

`dlg_rec231_presenter::dlg_rec231_presenter(...)` 末尾会调用 `i_view` 的
`ivtable+16`，从反编译看签名是：

```c
(*(...(**)())(**(_QWORD **)(a1 + 40) + 16LL))(
    *(_QWORD *)(a1 + 40),      /* i_view& this        */
    *(unsigned int *)(a1 + 56),/* screen_id           */
    *(_QWORD *)(a1 + 16),      /* prop 数组            */
    10,                        /* prop 数量            */
    *(unsigned int *)(a1 + 8), /* orientation         */
    a1 + 24,                   /* 输出缓冲            */
    "No10_Background",         /* 控件名字            */
    &unk_3F15578);             /* 名字表              */
```

- prop 数组有两份：`0x3F15338`（`orientation != 0`）与 `0x3F150F8`，各 10 项。
  dump 出来是 `{1, 0x2094000, 0, 0, 0, 0, 0x82, 0xb81, 0x250e, 0, 0, 0x1e00280, 0, 0, 0x2000080, 0}`，
  其中 `0x280`/`0x1E0` 就是 640×480 的几何，`0x82`、`0xb81`、`0x250e` 是属性 id。
  结构体每一步的步长仍需确认。
- `&unk_3F15578` 是**名字表**：开头 10 个 u32 是相对偏移
  `{0, 0x10, 0x25, 0x36, 0x49, 0x59, 0x6e, 0x83, 0x94, 0xa5}`，紧接着是名字串
  `"No10_Background"`、`"No4_Dialobackground"`、`"No9_Dialogstring"`、…。

这就是 A6700「集中 layout 数组」在 A7C II 上的等价物：一旦把 prop 的字段解开，就能在
0x1C3 上自己 create 一个文本控件（`+0x208` RID、`+0x210` 字符串），也就是真正能在详情页
写自定义内容。目前**没有**用它 —— prop 结构没解全，猜错会在 presenter 构造里崩。

更稳妥的替代路线（尚未验证）：hook `0x1CD8478`（0x1C3 controller getter），
它把第 6 个参数写进 `obj+400`，那很可能就是「本屏要显示哪一项」，拿到它就能在
`view_string` hook 里做「屏幕 + 选中项」的联合判定。

## 十二、命令/控件的"名字表"机制与 A7C II 的 UI 层次（重要结论）

`create_widget` 的最后一个参数不是随便的东西，而是一张**名字表**：

| 表地址 | 被谁指向 | 内容 |
| --- | --- | --- |
| `0x3F15578` | `0x5B4E348`（0x60 结构） | `0`、`"No10_Background"`、`"No4_Dialobackground"`、`"No9_Dialogstring"`、… |
| `0x3F15338` | `0x5B9EBE0` | 几何/属性数组（`0x82`、`0xb81`、`0x250e`、640×480） |
| `0x3F150F8` | `0x5B4CB20` | 同上，竖屏版（`0x1E0×0x280`） |
| `0x3F242E0` | **无人引用** | `"No15_Background"`、`"No14_HeaderArea"`、`"No1_Title"`、`"No4_lensinformation"`、`"No2_bodyinformation"`、… |

前三个各只有 1 个数据引用，都落在 `.data.rel.ro` 一个 0x60 字节的"presenter 构造参数结构"里
（`+0x00` 就是这张表，后面 11 个是字符串/子表指针）。

**`0x3F242E0` 那张表（A6700 详情页的 4 个文本槽名字）在 A7C II 上零引用 —— 是死数据。**
也就是说 A7C II 2.01 **没有** A6700 那样的"列表 + 详情页"两层菜单，`No2_bodyinformation`
这类控件只是旧资源的残留。

旁证：
- 0x2FD 的 controller 是 `scr7186_0001_processing_controller`（"processing" = 处理中对话框），
  而菜单项的 desc `+0x0C` 恰好填 0x2FD → 这个字段是"执行时弹的等待框"，不是"点击打开的详情屏"。
- `.rodata` 里有 `view_list_menu_item_with_value`、`view_list_menu_settingvalue`、
  `view_list_menu_cmn_item` 这些类名 → 值是**内联显示在列表行里**的。

**因此**：A7C II 的菜单是"单层列表 + 行内值"，A6700 是"列表 → 详情页"。
一对一复刻详情页在模块层面做不到；功能等价的做法是把信息和值直接写进行文本（本模块现在
正是这么做的），要还原"详情页"只能走 canvas 自绘（canvas 的 `attach_v2` 可以挂到
presenter 建出来的 widget 上）。

## 十三、desc 的两个变体与真正的"项列表屏" 0x2CB

把菜单组表里全部 96 个 non-setting 行与 394 个 setting 行都展开，取每个 desc 的
`+0x0C` 统计，两种 desc 的语义完全不同：

| desc 类型 | `+0x0C` 的含义 | 实测值 |
| --- | --- | --- |
| setting（`setting_desc`） | 名称 **RID** | `0x021C6604`、`0x0200855B`、`0x02008811`、`0x0204E199`… |
| non-setting（`non_setting_desc`） | 点击后打开的 **screen_id** | `0x2CB`(44)、`0x4B7`(11)、`0x2DA`(5)、`0x2DC`(3)、`0x2CD`(3)、`0x2DD`(2)、`0x2D1`(2)、`0x2DF`(1)、`0x2D7`(1)、`0x2D6`(1) |

**0x2CB 就是 A6700 用 0x293 干的那件事的等价屏**：

- controller = `scr7001_1004_controller`（getter `0x1CF7840`，ctor `0x21824B4`，
  vtable `0x5B135C0`，即"组内项列表"），44 个菜单项都指向它；
- 其余几个：`0x2DA` → ctrl `0x1CF93B0`、`0x2DC` → `0x1CF9758`、`0x2CD` → `0x1CF7BEC`；
- `0x4B7` 的 page table 项**全 0**（presenter 和 controller 都是 0），是动态创建或无效 id。

**更正第七节的判断**：不是"A7C II 没有两级菜单"，而是原来对照的 0x293 找错了屏 ——
0x293 是"某分类下的组表（4 行固定组名）"，真正的项列表是 **0x2CB**。
所以 `pilot_menu_ui_a7c2.c` 里行 desc 的 `+0x0C` 已从 `0x1C3`（其实是
`dlg_rec231` 对话框）改成 `APPFW_ITEM_SCREEN_ID = 0x2CB`，重新构建通过
（a7c2 71,136 B，sha256 `ba3e854b31101f02743270e0ef53fcca7c9afbfc2f0c44dd894113da0b38985a`）。

**仍未解**：`scr7001_1004_controller` 怎么知道"该显示哪个组" —— 这决定了 hook 它之后
能不能把 Pilot 组的行换成我们自己的内容。下一步从 `sub_21824B4`（ctor）的入参
和 `sub_1CF7840` 的第 6 参（写进 `obj+400`）入手。

## 十四、菜单表 category 布局修正

首次实现里把 category 0 当成 `table + 0x18`、组数读 `cat + 0x10`，是错的。
真实布局是：表头只有 **8 字节**，category 0 头在 `table + 0x08`
（`0x051DF8D0`），头部 **0x10 字节**，第一个组槽在 `cat + 0x10`。

原来的算术把目标槽算到 `table + 0x30`，正好落在 cat0 第一个组的行数组里，
`memcmp` 校验非零后放弃注入 —— 这就是菜单里看不到 Pilot 组的原因。

正确的字段表：

| 位置 | 偏移 | 说明 |
| --- | --- | --- |
| 表头 | `table + 0x00` | u32 分类数（8） |
| 分类序号 | `cat + 0x00` | u32，0..7 |
| 分类标题 RID | `cat + 0x04` | u32 |
| 组数 | `cat + 0x08` | u32，`sub_1B5A24C` 读 `*(cat - 2)` |
| 保留 | `cat + 0x0C` | u32，0 |
| 第一个组槽 | `cat + 0x10` | 15 个槽 × `0x2E0` |

八个 category（含组数与首组）：

| cat | 地址 | 标题 RID | 组数 | 首组 x / 行数 |
| --- | --- | --- | --- | --- |
| 0 | `0x051DF8D0` | `0x02006451` | 1 | `0x3e` / 6 |
| 1 | `0x051E2400` | `0x02006092` | 1 | `0x40` / 1 |
| 2 | `0x051E4F30` | `0x02006093` | 15 | `0x00` / 30 |
| 3 | `0x051E7A60` | `0x02006090` | 7 | `0x0f` / 17 |
| 4 | `0x051EA590` | `0x02006091` | 5 | `0x16` / 16 |
| 5 | `0x051ED0C0` | `0x0200608F` | 7 | `0x1b` / 6 |
| 6 | `0x051EFBF0` | `0x02006060` | 14 | `0x22` / 6 |
| 7 | `0x051F2720` | `0x02006454` | 14 | `0x30` / 3 |

host 侧验证：`work/test_menu_inject.c` 读 `work/a7c2_menu_table_head.bin`
（从 `appFw.so` 的 `0x051DF8C8` 起 12288 字节）重放注入算术，15 项断言全过：
目标槽偏移 `0x2F8`、槽内全零、插入后组数 2、邻近组与槽 14 都不变。

## 十五、菜单枚举器与 0x1B765B0 崩溃分析

`sub_1B5AC70(a1 /*menu view*/, a2 /*mode 0..2*/, a3 /*u32 输出缓冲*/)` 是菜单的
"category → 组 → 行" 枚举器，输入是 GOT `0x05B80EC0` 指向的菜单表：

- `v6 = &unk_51DF8E0` = `table + 0x18` = category 0 的第一个组槽（`cat + 0x10`）
- `*(v6 - 4)` = `cat + 0x00` 分类序号；`*(v6 - 2)` = `cat + 0x08` **组数**
- 步进：category `0x2B30`、组槽 `0x2E0`、行 `24` 字节

三个分支（按 `cat + 0x00` 分类代号）：

| 代号 | 路径 |
| --- | --- |
| 0（"我的菜单"） | 内部 `while (v40 != 5)` 循环 + `vtable[0x820]`，`v11 = 3784*v7 + 12` |
| 1 | `memcpy(v42, cat + 0x18, 736)` + `vtable[0x810]` 写到 `&a3[946*v7 + 2]` |
| 其它 | `goto LABEL_14` |

`LABEL_14` 是真正的逐行遍历：

```c
for (i = 0; i < *(v6 - 2); ++i) {            /* 该 category 的全部组 */
    v18 = 736 * i;
    while (v17 < v13[2]) {                    /* 注意：用 slot0 的 rows */
        vtable[2072](a1,
                     table + 0x2B30*v8 + 0x28 + 24*v17 + 736*i,  /* 行 v17 的 gid 地址 */
                     a2,
                     &a3[946*v7 + 6 + 2*v19] + 252*v10,
                     &v43, &v44);
    }
}
```

`vtable[2072]` = `sub_1B60228`，完整调用链：

```
sub_1B5AC70 → sub_1B60228(obj, &row.gid, mode, out, x4, x5)
                LDR W6,[X1]        ; row.gid 低 32 位
                CMP W6,#0xB81      ; 0xB81 = 特殊行标记
                LDR X0,[X1,#8]     ; row.setting_desc
                CBZ X0 → 跳过这一行
              → sub_1B76638(setting_desc)
              → sub_1B765B0(setting_desc)      ; *(u16*)(a1 + 2*idx + 0x2C) == 1
```

崩溃时 `a1 = 0xb2e1`、`idx = 9`、访问 `0xb31f`。`a1` 是奇数而 `2*idx` 必为偶数，
所以 `a1` 只能是垃圾值；`sub_1B5AC70` 里 `X19` 自 `MOV X19, X0` 起没有被改过，
因此垃圾来自它的调用者。

`sub_1B5AC70` 只有两个调用者，且都在数据段（`0x597DD28`、`0x597E8D8`）；
`sub_1B60228` 的另两个槽在 `0x597DD90`、`0x597E940`。同一张函数指针表里连续的
四个槽，说明它们是注册进去的回调，静态追不到调用点。

**推论**：我方注入的行 `setting_desc = 0`，在 `CBZ X0` 处就会被跳过，不会走到
`sub_1B765B0`；`gid` 也不等于 `0xB81`。因此这条链上的崩溃**不像是本模块写入
的数据直接造成的**。判定归属需要一次对照实验：用
`PILOT_EXTRA_CFLAGS=-DPILOT_A7C2_NO_MENU_INJECT` 构建一版完全不改菜单表的产物，
同样的操作序列下如果仍然崩溃，故障就在 appFw 侧。