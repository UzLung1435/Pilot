# pilot_canvas 在 ILCE-7CM2 (A7C II) 2.01 上的可行性

本文记录把 `pilot_canvas` 移植到 A7C II 之前必须解决的两个硬障碍。结论来自对
`appFw.so`（A7C II 2.01，99,960,744 B，imagebase 0）的静态分析与符号核实。

## 一、原生符号核实

`canvas_core.c` 的 `canvas_resolve_symbols()` 需要 39 个原生符号（全部
`dlsym(RTLD_DEFAULT, …)`，真实现在 `libObj.so`；`appFw.so` 里是对应的 PLT stub）。

A7 C II 上 **36 个存在，3 个缺失**：

| 缺失符号 | 用途 | 影响 |
| --- | --- | --- |
| `_ZN2ux6gfxeng6Sprite13setCurRectNumEt` | `Sprite::setCurRectNum(unsigned short)`，设置 sprite 的有效矩形数 | **RECT 后端不可用**：矩形数无法从 0 变成 N，画出的矩形不会显示 |
| `_ZN2ux6wgtsys6WidgetC1Ev` | `Widget::Widget()` | 只有 `WidgetC2Ev`（基类构造）。语义相同，可改用 C2 |
| `_ZN2ux6gfxeng7Element10setVisibleEb` | `Element::setVisible(bool)` | 只用于隐藏/淡出，可选功能 |

已核对 A7 C II 的 `Sprite` 全部 12 个导出方法：`C1`、`D1`、`setRectPos`、
`setRectSize`（3 参与 6 参两个重载）、`setRectColor`、`setRectColorResID`、
`setRectImageOffset`、`setRectImageRotate`、`setBlendDarker`、
`setBlendDarkerLevel`、`setPatchImageCache`。**没有任何设置矩形数量的入口**，也没有
`Num` 系列方法（全库 `gfxeng`/`wgtsys` 里带 `Num` 的只有
`MultiLineTextSprite::getLineNum`）。

RECT 后端还依赖 `sprite_set_count`（就是 `setCurRectNum`）在
`pilot_canvas_create_v1`、`end_batch_v1`、`clear_v1` 里使用，缺失即整条路径不可用。

## 二、渲染宿主

`pilot_canvas_attach_v2(void *widget, int is_evf)` 的关键是
`widget_init_sprite(widget, sprite, count)` —— 它**复用一个已经挂在屏幕上的 widget**，
不自己创建 widget。A6700 上这个 widget 由 `menu_ui` 的 `pilot_lv_text_widget.inc`
通过 Live View presenter 的 `update` vtable 槽拿到，再经 `pilot_canvas_track_widget_v1`
交给 canvas。

A7 C II 上目前没有这个来源：

- 没有 A6700 那种 Live View presenter vtable 偏移（`0x4C6DBF8` 之类）；
- `pilot_lv_text_widget.inc` 里的 view→widget 偏移（`0x5DD0`、`0xA05E00`）是 A6700 的值；
- 名字表 `0x3F242E0`（含 `No2_bodyinformation` 等 Live View 文本槽名）在 A7 C II 上是
  **零引用死数据**；`No1339_FooterTouchBackgroundDefocusSettingBeltValueName`
  （`0x4022B2A`）同样零引用。

自建 widget 树需要 `Widget::Widget()` + `setSize`/`setLocalPos`/`setParent`，而
`setParent` 需要一个已经在屏幕上的父 widget。

## 三、可能的出路

1. **自建 layer**：`WidgetSystem::createLayer(const core::ID&)` 存在（PLT
   `0x165DA70`，GOT `0x05BC1540`）。它的唯一调用者是 `sub_17CCF6C`，一个 view 的
   构造例程，连续调用 5 次创建 5 个层（背景/内容等），签名
   `void *sub_17CCF6C(resmgr, a2, a3, a4)`。若能把这段逻辑复用到自己的 view 上，
   就有了独立的渲染层，不依赖原厂 widget 树。需要继续逆向 `createLayer` 的返回值
   语义与层的挂载方式。
2. **写 Sprite 内部字段**：绕过 `setCurRectNum`，直接写 `Sprite` 对象里表示矩形数的
   字段。需要 `libObj.so` 的 IDB（当前只有 `appFw.so`），成本高且对固件版本敏感。
3. **PIXEL 后端 + `ImageElementBase::setImage`**：`setImage(unsigned, unsigned char)`
   存在（PLT `0x165F100`）。PIXEL 后端已经能 `resmgr::createImage` 生成 image 并把
   像素写进 `image_raw`，若能把 image 资源 id 挂到 `ImageElementBase` 上就能显示。
   瓶颈仍是要有一个宿主 widget。

## 四、结论

`pilot_canvas` 的**代码本身**在 A7 C II 上可以编译（39 个符号里 36 个能解析），但
**运行时的两条路都不通**：RECT 后端缺 `setCurRectNum`，PIXEL 后端缺渲染宿主。

这与 `menu_ui` 遇到的是同一类问题：A7 C II 的 UI 内容全部由 `appFw.so` 内部的
id/名字表驱动，没有 A6700 那种可自由插入的空白位置。区别在于 canvas 还可以尝试
「自建 layer」这条不依赖原厂树的路，是目前最有希望的方向。

## 五、下一步

1. 逆 `createLayer` 的返回值与层的挂载/绘制流程，判断能否脱离原厂 widget 树自建层。
2. 若能自建层：写一个最小探针（创建 1 个 layer + 1 个 sprite/image，画一个纯色矩形，
   看屏幕是否出现），在 qemu 上验证。
3. 若不能：A7 C II 上的 canvas 记为不可用，保留代码与本文档，等后续线索。

## 关键偏移速查（A7 C II 2.01）

| 名称 | 值 |
| --- | --- |
| `WidgetSystem::sys_getInstance` GOT | `0x05BC1388` |
| `WidgetSystem::createLayer` GOT | `0x05BC1540` |
| `WidgetSystem::processEvent` GOT | `0x05BC1AF8` |
| `WidgetSystem::notifyDraw` GOT | `0x05BC2098` |
| `WidgetSystem::postMouseRelease` GOT | `0x05BC20B8` |
| `WidgetSystem::updateFrame` GOT | `0x05BC21B0` |
| `WidgetSystem::postMouseMove` GOT | `0x05BC21F8` |
| `WidgetSystem::updateCurrentTime` GOT | `0x05BC2578` |
| `WidgetSystem::postMousePress` GOT | `0x05BC2AC8` |
| `Sprite::setString(unsigned)` GOT | `0x05BC1FE8` |
| `Sprite::setString(const char*)` GOT | `0x05BC1010` |
| `TextSprite::setFont` GOT | `0x05BC1CC8` |
| `TextSprite::drawText` GOT | `0x05BC1240` |
| `view_string::draw` | `0x1827F8C`（RID `+0x208`，cstr `+0x210`） |
| 菜单组表 GOT → 表基址 | `0x05B80EC0` → `0x051DF8C8` |
| 屏幕/presenter 表 GOT → 表基址 | `0x05B4CFD0` → `0x051F7810`（项 `0x60`） |
| view 层构造例程（5 次 createLayer） | `0x17CCF6C` |

## Fsys 不认长文件名（2026-11-28 实测）

第一次把探针放到卡上时，模块根本没有被加载。console.log 里只有这一行：

```
FSYS_FILE_TRACE op=Open path=A:/pilot/pilot_canvas_probe.so handle=0 flags=0x1 result=-2147483583 hex=0x80000041
```

同一份日志里紧邻的一行是成功的：

```
FSYS_FILE_TRACE op=Open path=A:/pilot/MNUI.so handle=81 flags=0x1 result=0
```

两个文件在卡上都能用 `mdir` 看到，内容也正确，唯一差别是文件名长度：`MNUI.SO` 是 8.3 短名，`pilot_canvas_probe.so` 需要 FAT 长文件名项（`PILOT_~1.SO` + LFN 记录）。

**结论：相机的 Fsys 只认 8.3 文件名，不解析 LFN。** 模块文件名必须满足「主名 <= 8 字符 + `.so`」，否则 `Fsys_Open` 返回 `0x80000041`。

这条限制影响所有模块，不只是探针：

| 名字 | 长度 | 可用 |
| --- | --- | --- |
| `MNUI.so` | 4 + 3 | 可以 |
| `CPROBE.so` | 6 + 3 | 可以 |
| `CANVAS.so` | 6 + 3 | 可以（建议给 canvas 用） |
| `pilot_canvas.so` | 12 + 3 | 不可以 |
| `pilot_canvas_probe.so` | 18 + 3 | 不可以 |

A6700 上没有暴露这个问题，因为那边一直是把模块编成 `pilot_menu_ui.so` / `pilot_canvas.so` 并放到 `/pilot/`，`modules.txt` 也按长名引用 —— 这要么说明 A6700 的 Fsys 支持 LFN，要么说明那份部署从来没有真正走通，值得单独确认。
## modules.txt 的两个硬约束

把探针改成 `CPROBE.SO` 之后，卡上文件确实在（`mdir` 可见，68,568 字节），但 run-jt67d2mb 里模块依然没被加载 —— 这次更彻底，连 `MNUI.so` 都没打开：

```
FSYS_FILE_TRACE op=Open path=A:/pilot/modules.txt handle=41 flags=0x1 result=0
FSYS_FILE_TRACE op=Open path=A:/pilot/log.txt     handle=61 flags=0x2 result=0
```

`modules.txt` 打开成功，之后没有任何 `.so` 的 Open 行，说明清单解析失败、整体被丢弃。

根因在 `patch/pilot_modules.c` 的 `parse_modules_json()`（L42-98）：

- 第一个字符必须是 `{`，**前面不允许空白或换行**（L50 `json_expect(&json, '{')` 前没有 `json_skip_space`）
- 键名必须恰是 `"modules"`（逐字节比对，L54-57）
- 元素字符只允许 `[A-Za-z0-9_.-]`（L69-72），不允许空格、斜杠
- **必须以小写 `.so` 结尾**（L78 `strcmp(names[count] + name_length - 3, ".so") != 0`）—— `CPROBE.SO` 就是死在这一条
- 不能以 `.` 开头、不能含 `..`（L79）
- 结尾必须是 `]}`，之后只允许空白，所以尾随换行没问题（L93-95）

`module_data_name()`（L100-110）和 `valid_filename()`（L112-127）里也有同样的 `.so` 判断，所以这条规则贯穿整个模块层。

**正确写法**（FAT 大小写不敏感，卡上的文件仍显示 `CPROBE.SO`）：

```json
{"modules": ["MNUI.so", "CPROBE.so"]}
```
## 探针实测：自建渲染层可行（2026-11-28，run-82228s3p）

第一次把探针跑起来后，结论和静态分析时完全不同。

### 1. RECT 后端可用

`Sprite::setCurRectNum(unsigned short)`（`_ZN2ux6gfxeng6Sprite13setCurRectNumEt`）在运行时是存在的：

```
_ZN2ux6gfxeng6Sprite13setCurRectNumEt    0x7faa743134
```

它由 `libObj.so` 导出，但在 `appFw.so` 里没有对应的 PLT stub —— 静态扫符号表扫不到，`dlsym(RTLD_DEFAULT)` 却能拿到。`canvas_core.c` 里 `resolve_native()` 用的正是 `dlsym`，所以这条路径是通的。

13 个探针符号里 12 个命中，唯一缺的是 `WidgetSystem::sys_getInstance`，用 `UXEngine::wgtsys()` 代回即可（后者返回 `.bss` 里的静态实例，实测 `0x1741bf0`）。

### 2. 自建层成功

```
layer probe: ui ready after 0 ms, first layer=0x1745b60
layer probe: calling createLayer
layer probe: layer=0x1b1a990
probe complete
```

`genID("PILOT_PROBE")` → `id=0xbd8496eb`，`createLayer` 返回非空的层对象，全程无崩溃。

**UI 就绪判据**：`appFw.so + 0x5F66BF0`（`.bss`）非零。`sub_17CCF6C` 在 UI 启动时建 5 个全局层，第一个的指针就存在这里；只要它非零，`createLayer` 就是安全的。

### 3. createLayer 是成员函数（踩过的坑）

`createLayer(const core::ID&)` 的 `this` 在 x0、`ID&` 在 x1。第一次探针把它当普通函数写成 `(const void *id)`，于是 x0 变成 `&id`、x1 是垃圾，`libObj.so` 里解引用出地址 `0x2`：

```
SEGV (PC=0x7f9404aa00)      ← createLayer + 0x10
DABT (lower EL) (0x92000006) at 0x2
```

真实调用序列在 `sub_17CCF6C` 里可以对照：

```
BL  UXEngine::wgtsys()      ; x0 = widget system
MOV X1, X19                 ; x1 = &id
BL  createLayer
```

### 4. 建层后要做两步配置

`sub_17CCF6C` 每次建层后立刻调两个虚函数：

```
LDR X1, [X0]        ; vptr
LDR X2, [X1, #8]    ; vtable[1]
MOV W1, #1
BLR X2              ; layer->vt[1](1)

LDR X2, [X1, #0x18] ; vtable[3]
MOV W1, #0
BLR X2              ; layer->vt[3](0)
```

最新一版探针会 dump `vt[0..7]` 并尝试这两次调用，之后再试 `Widget::Widget()` 构造，确认 widget 对象能不能独立建出来。

### 5. 对移植的含义

`pilot_canvas` 的 RECT 后端不需要重写：`pilot_canvas_create_v1(w, h, RECT)` 自己 `Sprite` 构造 + `setCurRectNum` + `setRectPos/Size/Color`，缺的只是「把 sprite 挂到一个在屏幕上可见的 widget 上」这一步。A6700 上这一步由 `menu_ui` 递进来的 LV widget 完成；在 A7 C II 上改为自建层 + 自建 widget，把 `widget_init_sprite(widget, sprite, capacity)` 接上即可。