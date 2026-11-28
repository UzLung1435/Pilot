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
