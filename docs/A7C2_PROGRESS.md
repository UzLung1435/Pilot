# A7 C II (ILCE-7CM2 2.01) 适配进度

最后更新：2026-11-29。对应代码见 `main` 分支 `acb45b9`。

本文是**阶段性状态快照**，不是设计文档。它记录：哪些能力已经实测通过、
哪些已经确认但还没做完、哪些只是猜测。已写明的技术细节都带来源（代码路径、
实测日志、IDA 反编译），猜测单独成节。

其它细节文档：

- `modules/pilot_canvas/A7C2_FEASIBILITY.md` —— 画布后端的符号与宿主可行性
- `modules/pilot_canvas/A7C2_CP_NOTES.md` —— CP 协处理器的静态分析过程
- `modules/menu_ui/A7C2_PORT_STATUS.md` —— 菜单后端的结论与构建
- `modules/menu_ui/a7c2_reverse_notes.md` —— 菜单系统的逆向笔记

---

## 0. 一句话现状

**画布（pilot_canvas）已经能在 A7 C II 上跑起来并实测通过**：自建渲染层、自绘内容、
触摸拖动、双击切页、OSD 文字、只在主界面显示，全部在 qemu 上验证过，零崩溃。

**菜单（menu_ui）陷入死结**：把条目注入菜单表是可行的（行会出现、会被遍历、
UI 会给它分配位置），但**条目文字显示不出来、点击被拒**。根因是 A7 C II 把菜单项
的名字与点击目标编译成了「gid 查表 + 预渲染图片」，不是 A6700 那种"desc 里放一个
RID 字段"。有一个外部开发者声称做到了带自定义文字的条目，但方法未知，见 §4.3。

**CP 协处理器**：读通道（测光网格 OPD）地址已定位但**未在实机验证**（qemu 没有 CP 核）；
写通道（RAM 补丁）代码已写完并生成 patched 镜像，但**三项前提未确证，一旦出错会黑砖**，
见 §4.2 与 §6。

---

## 1. 目标与硬约束

| 项 | 内容 |
| --- | --- |
| 目标 | 把 Himpq-Pilot 的 `pilot_canvas` 与 `menu_ui` 移植到 ILCE-7CM2 (A7C II) 2.01 |
| 框架 | Pilot v0.2.0，模块 ABI 2，`PILOT_MODEL=a7c2` |
| 构建 | WSL `aarch64-linux-gnu-gcc` + `work/pilot-a7c2/sysroot`（相机 glibc 2.25） |
| glibc 上限 | 产物必须 ≤ `GLIBC_2.17`（构建脚本里有硬校验） |
| 文件名 | 相机 Fsys 只认 **8.3 短名**；`modules.txt` 里的扩展名必须写**小写 `.so`** |
| 安全线 | CP 侧写错会**黑砖不可恢复**，任何 CP 写操作必须先有确证 |

---

## 2. 能力矩阵

图例：**✅ 实测通过**（有 qemu 日志）／**🟡 已实现未验证**／**❌ 已证伪**／**⬜ 未开始**

### 2.1 pilot_canvas

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 自建渲染层 + widget | ✅ | `createLayer` + `Widget()` + `setLayerID` + `initSprite`，画出 64 条竖条 |
| RECT 后端绘制（clear/grid/fill/line） | ✅ | 复用 `canvas_core.c` 原有实现，只需换宿主 |
| 触摸拖动 | ✅ | hook `postMousePress/Move/Release` 三个 GOT 槽，`setLocalPos` 移动 widget |
| 双击切页 | ✅ | 间隔 <350 ms、位移 <24 px，三个视图循环 |
| OSD 文字 | ✅ | 用 `canvas_core.c` 内置 5×7 位图字库，走 RECT 后端 |
| 主界面守卫（菜单打开时隐藏） | ✅ | hook `presenter_controller_manager::open_screen`，`0x343` 是主界面 |
| 真波形视图（OPD） | 🟡 | 代码已接入 `canvas_cp_read_opd`，OPD 表地址算出 `0x01651738`，**qemu 无 CP 核 → 读到 -8** |
| 多行文本叠加（TextSprite 槽位） | ⬜ | A7 C II 没有 A6700 的 LV 文本槽，方案待定 |
| DISP 键切虚拟页 | ⬜ | A6700 的 DISP GOT 槽在 A7 C II 上未定位 |
| 伪色 / 示波器五视图 | ⬜ | 依赖 OPD 数据源，等 OPD 验证 |

### 2.2 menu_ui

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 解析 `modules.txt` 列出模块 | ✅ | 字符串数组形式 |
| 注入菜单组/条目到菜单表 | ✅ | 数据写进去了、原生遍历也读到了（日志有 `pilot row accepted`） |
| 条目显示自定义文字 | ❌ | 空行。根因见 §3.2 |
| 条目点击进入自定义页面 | ❌ | 「此功能被禁止」 |
| 导出 `pilot_ui_*` API | 🟡 | 导出了 `set_rid_override` / `register_options`，但没有消费方 |
| 详情页 / 选项弹窗 / Live View 文本 | ❌ | A7 C II 没有 A6700 的集中 layout 数组 |

### 2.3 CP 协处理器

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 读（pcidma `/dev/pcidma`） | 🟡 | 通道本身与补丁无关；qemu 里节点不存在（-8），只能实机验证 |
| OPD 测光网格地址 | 🟡 | `0x01651738`，由 A6700 的 `0x015B6398` 逐指令对照推出，见 §3.6 |
| 焦距 / 几何 | ⬜ | `0x016A4998` / `0x01696490` 未迁移，读失败时留空不影响波形 |
| 写（uipc v4 补丁） | 🟡 | 补丁代码 868 B + 分支 4 B 已编译、patched 镜像已生成，**三项前提未确证** |
| 实机刷写 | ⬜ | 未做 |

---

## 3. 已确证的技术事实

这一节只写有直接证据的（反编译结果、实测日志、字节校验）。

### 3.1 appFw.so 基础

| 项 | 值 | 来源 |
| --- | --- | --- |
| 模块型号 | ILCE-7CM2 / 2.01 | `oldProjects\FlashDir\backup1\state.json` |
| 软件基址（运行时） | 由 `dl_iterate_phdr` 匹配 `appFw.so` 取得 | 实测日志 `appFw base = 0x...` |
| `.text` | `0x1660680`–`0x37041A0` | `readelf -SW` |
| `.rodata` | `0x37041B0`–`0x51A09E4` | 同上 |
| `.data.rel.ro` | `0x51B82A0`–`0x5B4BB60` | 同上 |
| `.got` | `0x5B4BE70`–`0x5BC0FE8` | 同上 |
| `.got.plt` | `0x5BC0FE8`–`0x5BC2B58` | 同上 |
| `.data` | `0x5BC2B58`–`0x5F63FB8` | 同上 |
| `.bss` | `0x5F63FB8`–`0x98A1CD0` | 同上 |

**已定位的 GOT 槽**（改这些 qword 即可拦截，不需要 inline hook）：

| 函数 | GOT 槽 |
| --- | --- |
| `WidgetSystem::sys_getInstance` | `0x05BC1388` |
| `WidgetSystem::createLayer` | `0x05BC1540` |
| `WidgetSystem::processEvent` | `0x05BC1AF8` |
| `WidgetSystem::notifyDraw` | `0x05BC2098` |
| `WidgetSystem::postMouseRelease` | `0x05BC20B8` |
| `WidgetSystem::updateFrame` | `0x05BC21B0` |
| `WidgetSystem::postMouseMove` | `0x05BC21F8` |
| `WidgetSystem::updateCurrentTime` | `0x05BC2578` |
| `WidgetSystem::postMousePress` | `0x05BC2AC8` |
| `TextSprite::setString(unsigned)` | `0x05BC1FE8` |
| `TextSprite::setString(const char*)` | `0x05BC1010` |
| `TextSprite::setFont` | `0x05BC1CC8` |
| `TextSprite::drawText` | `0x05BC1240` |
| 菜单组表指针 | `0x05B80EC0` → `0x051DF8C8` |
| 屏幕页面表指针 | `0x05B4CFD0` → `0x051F7810` |

> **教训**：GOT 槽里存的是 **PLT 导入桩**（指向 `extern` 段的 `__imp_*`），不是
> `libObj.so` 里的实现。hook 时 `orig` 必须来自 `dlsym(RTLD_DEFAULT, ...)`，
> 否则转发到桩上会崩在 `ld-2.25.so+0xCC04`。

### 3.2 菜单系统

**菜单组表** `0x051DF8C8`，共 `0x15480` 字节：

```
+0x00  u32  分类数 = 8
+0x08  分类 0 的头
        +0x00 u32 分类序号      +0x04 u32 标题 RID
        +0x08 u32 组数          +0x0C u32 0
        +0x10 起 15 个组槽 × 0x2E0
                +0x00 u32 全局组序号   +0x04 u32 组名 RID
                +0x08 u32 行数         +0x10 起 行 × 24B
                                          {u64 gid; u64 setting_desc; u64 non_setting_desc}
分类头间距 0x2B30
```

8 个分类实测（地址 / 标题 RID / 组数 / 首组 x-行数）：cat0 `0x051DF8D0`/`0x02006451`/1/`0x3e`-6、
cat1 `0x051E2400`/`0x02006092`/1/`0x40`-1、cat2 `0x051E4F30`/`0x02006093`/15/`0x00`-30、
cat3 `0x051E7A60`/`0x02006090`/7/`0x0f`-17、cat4 `0x051EA590`/`0x02006091`/5/`0x16`-16、
cat5 `0x051ED0C0`/`0x0200608F`/7/`0x1b`-6、cat6 `0x051EFBF0`/`0x02006060`/14/`0x22`-6、
cat7 `0x051F2720`/`0x02006454`/14/`0x30`-3。

> **重要**：这张表在代码里被 **6 处**引用 —— 5 处是 `adrp/add` 硬编码
> （`0x1B5A278`、`0x1B5ACC8`、`0x1B5B054`、`0x1B5B15C`、`0x1B5FC38`），只有 1 处走 GOT。
> A6700 的「复制整表 → 改副本 → 重定向 GOT」在这里只让 1/6 的读者看到副本，
> 其余仍读原生表 → **必须原地改写原生表**。

**条目的名字与点击目标由 gid 决定**（这是死结的核心）：

```c
// sub_1AC2700 —— 取类别
if ((gid & 0xFF000000) == 0x01000000) return unk_3D81F08[gid & 0xFFF];
if ((gid & 0xFF000000) == 0x04000000) return unk_3D82CA0[gid & 0xFFF];
return gid & 0xFFFFF000;

// sub_1AC2748 —— 取索引
if ((gid & 0xFF000000) == 0x01000000) return unk_3D83978[gid & 0xFFF];
if ((gid & 0xFF000000) == 0x04000000) return unk_3D84710[gid & 0xFFF];
return gid & 0xFFF;
```

即 **gid = 类别 << 12 | 索引**。`sub_18E9328`（`vtable[1848]`，6937 字符）按类别
选渲染函数，类别不匹配就 `return 0` → 空行。

三个对照实验（都用 `cat0 slot0` 接管最后一项，其余条件相同）：

| 实验 | 改动 | 结果 |
| --- | --- | --- |
| 1 | 区域可见性 19 个 u16 全写 1 | 仍禁止 |
| 2 | desc 改成原生模板（RID `0x021C6639`、screen `0x2FD`），gid 仍是 `0x1F00` | 空行 + 禁止 |
| 3 | gid 换成真实原生项 `8` | **显示「影像质量设置」、点击能进** |

→ **desc 的 `+0x00`(RID) 与 `+0x0C`(screen) 不参与名字与点击校验；gid 才是开关。**
我们的 `0x1F00` 解出来类别是 `0x1000`，不在任何分支里 → 必然空行。

### 3.3 屏幕与呈现

**页面表** `0x051F7810`，`0x60` 字节一项，按 screen_id 直接索引：

```
+0x00  presenter 缓存 getter（已存在对象，只 refresh）
+0x08  controller getter（按需构造，池 &unk_7603190 + 0x5000*idx）
+0x20  screen_id（低 32 位）  +0x24 screen_type
```

**屏幕 id（实测，与 A6700 完全不同）**：

| id | 含义 |
| --- | --- |
| `0x343` | **主界面 / Live View**（`open_screen` 反复回到它） |
| `0x346` | 菜单 |
| `0x2C9` / `0x344` / `0x8F` / `0xC2` | 各子屏幕 |

> A6700 的 `0x1C3`(详情页) / `0x293`(列表) / `0x2CB`(项列表) 在 A7 C II 上**没有任何对应**，
> 按 A6700 的 id 猜隐藏名单全部无效。

**屏幕切换拦截点**：`presenter_controller_manager::open_screen`

```
实现 0x1E77DD8，签名 (this, const ds_screen_id *target, const ds_screen_id *from,
                     const ds_screen_id *restore, bool, bool)
vtable 槽 0x05B0C498（XrefsTo 只有这 1 处，且是数据 → 改 qword 即可）
```

**已证伪**：页面表 `+0x08` 的 controller getter **只在屏幕首次构造时被调用**，
hook 它看不到切换（日志 `screen hooks 4/4` 但零命中）。

### 3.4 渲染后端

A7 C II 的 `appFw.so` 里 `ux::gfxeng` / `ux::wgtsys` 全是 **PLT 导入桩**，真实现在
`libObj.so`（3,274,464 B，`ux6gfxeng` 导出 43 个）。所以 `dlsym(RTLD_DEFAULT, ...)`
能拿到实现，而静态扫 `appFw.so` 的符号表会漏掉它们。

**自建渲染层的完整链路（实测通过）**：

```
1. dl_iterate_phdr 拿 appFw.so 基址
2. 轮询 *(u64*)(base + 0x5F66BF0) != 0     // UI 就绪
3. UXEngine::wgtsys() → WidgetSystem*
4. core::ID::genID("PILOT_CV")
5. createLayer(this=wgtsys, &id)           // 成员函数！this 在 x0、id 在 x1
6. vt[1](layer, 1)   vt[3](layer, 0)
7. Widget() → setSize(600,300) → setLayerID(&id)
8. pilot_canvas_create_v1(600,300,RECT) + bind_widget_v1(widget)
9. 33 ms 一帧：clear / draw_grid / fill_rect / commit
```

**关键陷阱**：`createLayer` 是**成员函数**，写成 `(const void *id)` 会让 x0 拿到
`&id`、x1 是垃圾，`libObj.so` 解引用出错地址 → `PC = createLayer+0x10` 处 SEGV。

**可用的 Widget 方法**（`dlsym` 全都能拿到）：`C2` 构造、`setSize`、`setLocalPos`
（注意 mangled 名是 `11setLocalPos`，**11 个字符**，写成 12 会 dlsym 返回 NULL）、
`initSprite`、`setLayerID`、`setParent`。

### 3.5 文本渲染

`TextSprite::setString(unsigned)` 在全库里**只有 1 个调用点**：`view_string::draw`
（`0x1827F8C`）。A6700 上这个函数被到处调用。

`view_string` 对象字段：`+0x204` 可见标志、**`+0x208` 文本 RID（`0xFFFFFFFF` 表示
不用 RID）**、**`+0x210` const char\***（RID 为 -1 时生效）、`+0x318` Dictionary*。

已实现的 hook：改 `0x1827F8C` 的 16 字节序言 → trampoline，命中我方 RID 时把
`+0x208` 写成 `0xFFFFFFFF`、`+0x210` 指向我方字符串。**但这个 hook 从未打印过命中日志**
→ 菜单项文字在屏幕构造时渲染完，之后走缓存，模块加载（更晚）时已不再重绘。

`ResourceManager::prefetchTextImage` 是**唯一的文本图片入口**，签名里没有 `const char*`，
只接受 RID + Dictionary。**没有"注册文本"的 API。**

### 3.6 CP 协处理器

| 项 | A6700 2.00 | A7 C II 2.01 | 证据 |
| --- | --- | --- | --- |
| 镜像大小 | 17,892,024 | 18,090,880 | - |
| sha256 | `61b7fe5c…` | `60a9de1a…` | - |
| 公共前缀 | **`0x20000` 字节逐字节相同** | 同左 | 逐字节对照 |
| 接收函数 hook 点 | `0x4A7E52` | `0x4B3B0A` | 24 字节完全相同（含 `bl` 编码） |
| 三个 orig 目标 | `0x4A7E57`/`0x4A7E5F`/`0x4A7E79` | `0x4B3B0F`/`0x4B3B17`/`0x4B3B31` | 偏移 +5/+13/+39 一致 |
| 代码穴（cave） | `0xC12D68`（零区长 664 B） | **不可用**（已是代码） | 字节对照 |
| 候选 cave | - | `0xC63EA4`（零区长 8,320 B，`movw/movt` 引用 0 处） | 引用审计 |
| OPD 表基址 | `0x015B6398` | **`0x01651738`** | 5 处 `movw/movt` 逐指令对照，指令序列除立即数外完全相同 |

OPD 地址的推导：A6700 的一处典型序列是

```
852116: movw r1, #0x6398
85211a: rsb  r0, r0, r0, lsl #3      ; r0 = r0*7
85211e: movt r1, #0x015b             ; r1 = 0x015B6398
852122: add.w r0, r1, r0, lsl #4     ; r0 = base + idx*0x70
```

A7 C II 的对应位置是 `86d5f2: movw r1,#0x1738` / `86d5fa: movt r1,#0x0165` →
`0x01651738`，五个引用点一一对应，文件偏移差恒为 `0x1B4C0`。

**CP 固件是 ARM/Thumb 混合**：`0x4B3B0A` 一带是 Thumb，`0x5E1F58` 是 ARM（`blx` 切）。
IDA 载入 RAW binary 要先 `ida_segment.rebase_program(0x108000, 0)`，Thumb 区用
`ida_bytes.set_flag(ea, ida_bytes.FF_THUMB)` 标记。

---

## 4. 未完成项

分三类：**4.1 已确证只差写代码**／**4.2 有线索待验证**／**4.3 纯猜测需实验**。

### 4.1 已确证、只差实现

**（1）画布的 OPD 真波形视图**

- 现状：`a7c2_host.c` 已接入 `canvas_cp_read_opd`，视图 0 在 `g_opd_ok` 时画真实波形，
  地址 `0x01651738` 已写进 `canvas_cp_sensors.c` 的 `#ifdef PILOT_MODEL_A7C2` 分支。
- 卡在：qemu 没有 CP 核 → `/dev/pcidma` 不存在 → 读返回 `-8`（`PILOT_STATUS_NOT_FOUND`）。
- 需要：**实机**跑一次，看日志 `HOST a7c2: opd read=0 valid=1 48x26`。

**（2）焦距与屏幕几何**

- A6700 用 `CP_LENS_INFO_BASE_VA 0x016A4998`、`CP_SYNCRC_GEOM_VA 0x01696490`。
- A7 C II 的对应值没算：A6700 镜像里 `0x016A4xxx` 有 22 处引用，`0x016A4998 - 0x016A4098
  = 0x900` → 基址大概率是 `0x016A4098` + 字段偏移。
- 方法：用 `tools/cp/cp_pairs.py` 对比 A6700 与 A7 C II 在那些引用点的 `movw/movt` 立即数。
- 影响面：读失败时调用方本就返回"没有值"，焦距/几何留空，不影响波形。

**（3）把 UI 文案补齐**

- `modules/pilot_canvas/README.md`、`A7C2_FEASIBILITY.md` 已写；缺一份"画布能做什么/
  不能做什么"的用户向说明（当前 README 混了实现细节）。

### 4.2 有线索、待验证

**（1）CP 写通道的三项前提（最高风险）**

补丁代码（`patch/cp/a7c2_uipc_v4/`，`patch.bin` 868 B、`branch.bin` 4 B `b0 f3 cb b1`）
已编译，spec 已生成，`build/cpapp-b.a7c2-patched.bin` 已产出（18,090,880 B，
sha256 `332095f8…`）。但以下三项**没有证据**：

| 未确证项 | 为什么重要 | 怎么验 |
| --- | --- | --- |
| cave `0xC63EA4` 所在区域的 **MMU 权限** | 如果不是 RX，跳过去就取指中止 → 黑砖 | 需要 CP 的 MMU 表；或先在 qemu 里跑 CP 核（当前不支持） |
| 那段零区在**运行时是否可写** | 补丁代码要写 `STATUS_VA` | 同上 |
| 868 字节代码**跨 4 KiB 页**执行 | `0xC63EA4` 起 868 B 会横跨两页 | 需要确认两页都是 RX |

**（2）A6700 的 CP 补丁是否在实机跑过**

`patch/cp/uipc_v4/README` 与 `A7C2_FEASIBILITY.md` 都提到"从未实机打过"。如果
A6700 的补丁本身也没上过实机，那 A7 C II 的补丁就是**首次实战**，风险等级要再提一档。

**（3）菜单项的文本来源**

外部开发者做到了带自定义文字的条目（见 §4.3）。我们已经知道 gid 决定名字，
但不知道"名字 → 图片"这一步在哪里发生。可能的验证路径：

- hook `sub_1B60228` 后观察 `*a4`（32 位）被谁读取。`sub_1B5AC70` 是虚函数，
  通过 `0x597DD28` 那张函数表被调用，输出缓冲布局已知（`a3[946*v7+6+2*v19] + 252*v10`
  是行数据指针）。需要顺着 `a3` 找到 UI 侧的消费者。
- 或者：查 `ml_*` 系列（菜单资源数据库）是否有运行时注册文本的接口。

**（4）A7 C II 的 prop 表格式**

`dlg_rec231_presenter` 用 `create_widget(sid, prop, 10, orientation, out, "No10_Background",
名字表)` 按名字建控件。prop 数组两份：`0x3F15338`（orientation≠0）/ `0x3F150F8`，
名字表 `0x3F15578`（10 个 u32 相对偏移 + 字符串）。**每项步长未定** ——
`0x3F15578 - 0x3F15338 = 0x240` 与 10 项不符，不能按 8 字节硬算。
解开它就能在 0x1C3 自建文本控件（`+0x208` RID / `+0x210` cstr）。

### 4.3 纯猜测，需要实验

**（1）外部开发者的实现方式**

用户转述（m04267）："有别的开发者测试了，确认了可以增加一个带自定义文字的条目。
他是在二级菜单中新增的，他所加的目录为拍摄>影像质量/记录>扩展视频格式（这个是他新增的），
位置是第三组第一项。"

**这条推翻了我"自定义 gid 必然空名"的结论。** 但方法未知。我的猜测：

- **猜测 A（较可能）**：他的 gid 落在**某个合法类别**里，于是整条渲染链跑通；文字
  来自 desc 的 `+0x00` RID，而这个 RID 指向**已有的文本资源**（"扩展视频格式"可能是
  某个原生字符串的复用）。
- **猜测 B**：他在更上游 hook 了 `sub_1B60228` 并构造了 `*a4` 的完整语义（不仅仅是填
  一个 RID），让 UI 把它当合法显示值。
- **猜测 C**：他改的不是菜单表，而是 `sub_18E9328` 的分派表（`off_5920900` 系列 +
  `unk_38C4xxx` 索引表），凭空加了一个类别。

**需要问到的信息**（已在对话里问过，等回复）：① 那条目的 gid 值与 category/group 索引；
② 文字是"重启后第一次进菜单就显示"还是"进出一次后才显示"（前者=构造时渲染，后者=重绘路径）。

**（2）A7 C II 是否存在可自由定义的详情页**

A6700 有 0x1C3（`dlg_stock_000` 风格的长文本详情页 + 集中 layout）。A7 C II 的
`dlg_rec231`（`0x1C3`）只有 4 个文本槽的名字（`No2_bodyinformation` 等）**零引用 = 死数据**，
实际在用的是三张名字表（`0x3F15578`/`0x3F15338`/`0x3F150F8`）。是否能用 `dlg_rec231`
承载我们自己的 4 行文本，未验证。

**（3）A6700 的 layout 数组在 A7 C II 上是否真的不存在**

我用"`+0x34` 是 RID、`+0x20..+0x2C` 是合理矩形"的规则扫了 `.data.rel.ro` 全库，0 命中。
但如果 A7 C II 改了字段布局（比如 RID 挪到 `+0x30`），规则就会失效。**没有换字段布局重扫过。**

---

## 5. 已证伪的路线（避免重走）

| 路线 | 为什么不行 | 证据 |
| --- | --- | --- |
| 复制菜单表 + 重定向 GOT | 5 处硬编码直接读原生表，只有 1 处走 GOT → 两份数据不一致 → 按 Menu 崩在 `sub_1B765B0` | 对照实验：noinject 不崩、inject 崩在同一 PC |
| 增加 category 的组数（count 1→2） | `sub_1B5AC70` 会按新组数索引调用方分配的缓冲 → 越界 → 后续读到垃圾指针 | `-DPILOT_A7C2_NO_COUNT_BUMP` 变体不崩 |
| hook 页面表 controller getter 追踪屏幕 | getter 只在屏幕首次构造时调用 | `screen hooks 4/4` 但零命中 |
| 给 `*a4` 自定义 RID 让菜单项显示文字 | `prefetchTextImage` 查不到资源 → 空行 | 实验 2 |
| hook `TextSprite::setString` 换 cstr | A7 C II 全库只有 1 个调用点，且在 `view_string::draw` 内部；菜单行不经过它 | 符号扫描 |
| 按 A6700 的 screen id 猜隐藏名单 | A7 C II 的屏幕 id 体系完全不同（`0x343` 才是主界面） | `open_screen` 实测日志 |

---

## 6. 风险

| 风险 | 等级 | 说明 |
| --- | --- | --- |
| CP 写通道黑砖 | **高** | 三项前提未确证；CP 侧一错不可恢复。建议在实机前先完成 §4.2(1) 的验证 |
| 菜单表原地改写 | 中 | 当前默认关闭（`PILOT_A7C2_ENABLE_MENU_INJECT` 未定义），且注入只改 `cat0 slot0` 的已有行 |
| 自建渲染层 | 低 | 实测稳定，33 ms 循环 + 触摸 + 切页连续跑无 fault |
| 固件版本漂移 | 中 | 所有偏移都绑定 2.01；换固件必须重新核对，尤其 `0x1B765C8` 那类崩溃点 |

---

## 7. 下一步建议（按性价比排序）

1. **实机验证 OPD 读**（低风险、高价值）：装当前 `CANVAS.so` 到实机，看
   `HOST a7c2: opd read=0 valid=1 48x26`。如果这个通了，波形视图立刻可用。
2. **补齐焦距/几何地址**（低风险）：用 `tools/cp/cp_pairs.py` 对照 A6700 的
   `0x016A4xxx` 引用点。
3. **继续挖 §4.2(3) 的文本来源**：从 `sub_1B5AC70` 的输出缓冲 `a3` 往上找消费者。
   这是让菜单项显示自定义文字的唯一出路。
4. **CP 写通道**：等 §4.2(1) 的 MMU 权限有证据再说。在此之前不要刷写。
5. **多行文本叠加 / DISP 切页**：A7 C II 上缺少对应 hook 点，优先级低于上面几项。

---

## 8. 需要外部输入的事

1. **外部开发者那条目的 gid 与索引**（§4.3(1)）—— 能省掉大量逆向。
2. **CP 的 MMU 配置**（是否有人 dump 过 CP 启动后的页表）—— 决定写通道能否推进。
3. **A6700 的 CP 补丁是否在实机验证过** —— 决定 A7 C II 补丁的风险基线。

---

## 9. 文件与工具索引

### 模块

| 路径 | 说明 |
| --- | --- |
| `modules/pilot_canvas/A7C2_FEASIBILITY.md` | 画布符号与宿主可行性 |
| `modules/pilot_canvas/A7C2_CP_NOTES.md` | CP 静态分析（§0–§7） |
| `modules/pilot_canvas/a7c2_host.c` | A7 C II 的宿主：建层、建 widget、绘制、触摸、切页、屏幕守卫 |
| `modules/pilot_canvas/a7c2_probe.c` | 早期探针（保留作参考） |
| `modules/menu_ui/A7C2_PORT_STATUS.md` | 菜单后端结论 |
| `modules/menu_ui/a7c2_reverse_notes.md` | 菜单逆向笔记（17 节） |
| `modules/menu_ui/offsets_a7c2.h` | 菜单后端用到的全部偏移 |
| `patch/cp/a7c2_uipc_v4/` | CP 补丁工程（`link.ld`/`hook.S`/`patch_handler.c`/`gen_patch.py`） |
| `patch/cp/a7c2_uipc_v4/README.md` | CP 补丁的构建与刷写 |

### 工具（`tools/cp/`）

| 脚本 | 用途 |
| --- | --- |
| `unpack_dat.py` | Sony `.DAT` 固件解包（第四代 FDAT 解密） |
| `a6700_find_cp2.py` | 从 `firmware.tar` 的 `nflasha*` 里按 loader 头找 CP 镜像 |
| `dump_ext2_file.sh` | 从 ext2 分区按 inode 提取文件（**连续读会拿到碎片垃圾**） |
| `cp_pairs.py` | 枚举某区间内的 `movw/movt` 立即数对 |
| `cp_refs.py` | 审计某 VA 被多少处 `movw/movt` 引用 |
| `cp_cave_ctx.py` | 打印代码穴前后文 |
| `cp_scan.py` | 按字节模式扫 hook 点 |
| `cp_segments.py` | 64 KiB 块的非零/可打印密度图 |
| `cp_inspect.sh` | 十六进制查看器（VA = off + 0x108000） |

### 构建

```bash
# 画布（A7 C II）
cd Pilot && PILOT_MODEL=a7c2 bash modules/pilot_canvas/build.sh build/CANVAS.so

# 菜单
cd Pilot && PILOT_MODEL=a7c2 bash modules/menu_ui/build.sh build/MNUI.so

# 框架
cd Pilot && PILOT_MODEL=a7c2 \
  PILOT_CC=$PWD/../work/cc_a7c2_sysroot.sh \
  PILOT_SYSROOT=$PWD/../work/pilot-a7c2/sysroot \
  bash patch/build_pilot_dso.sh
```
