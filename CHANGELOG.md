# 更新记录

## 未发布（A7C II 2.01 适配 + 机型隔离）

- **A7C II menu_ui 结论：无法新增自定义菜单条目。** 对照实验：同一行只改 id，`0x1F00` 显示空行并报「此功能被禁止」，换成真实 id `0x8` 立刻显示「影像质量设置」并能进入其页面。原因是条目名称与点击目标都由 id 查表得到（`menu::util::util_instance::get_name_resid_raw_file_type` 的 switch 编译在 `.text` 内），文本又经 `ResourceManager::prefetchTextImage` 预渲染成图片，自定义 RID 没有对应图片。菜单注入因此改为编译期 opt-in（`-DPILOT_A7C2_ENABLE_MENU_INJECT`，默认关闭）；默认产物只读 `modules.txt`、注册选项、安装 `setString`/`view_string` 两个 hook，不改原生菜单表。A7C II 产物 71,672 B / SHA-256 `6f760319…`；A6700 仍 79,456 B / `0cf89a76…`。
- 新增 `patch.a7c2.json` 与 `patch.a7c2.image.json`：ILCE-7CM2 2.01 的 DT_INIT 启动钩子映射与离线镜像描述。
- 注入岛按 A7C II 的 `DT_INIT 0x165cfb8`、`dlsym/dlopen` PLT 与 `PT_LOAD[0]` 段尾空隙重定位，四个补丁窗口与原厂基线逐字节校验。
- **机型不再共用引导产物**：新增 `models.json`（`pilot.models.v1` 注册表）、`patch/pilot_model.h`（C 侧宏）与 `tools/pilot_tool/models.py`（读取、运行时块生成、C 头一致性检查）。
- A7C II 改用 `pilot_a7c2.so` / `/usr/lib/pilot_a7c2.so`、`pilot_a7c2_*` 三个引导导出、`pilot_module_model_a7c2` 模块标记、机型化岛内字符串表与启动追踪文件；A6700 已实机验证的产物保持逐字节不变。
- 三层机型防呆：刷写前校验常驻 DSO 符号并机型化设备路径/结果头；`modules deploy --model` 校验模块标记与文件名；`pilot_load_modules()` 在 `pilot_module_init` 之前用 `dlsym` 拒绝异机型模块。
- 构建脚本改由 `PILOT_MODEL` 选择机型（默认 `a6700`），输出名、soname、导出符号检查同步；6K 与 MenuUI 因硬编码 A6700 地址，拒绝其它机型构建。
- 结果镜像 SHA-256 更新为 `48da7988…a21dc`（机型化岛字符串表后；旧的共用身份版本已作废）。
- 离线验收：`pilot.py inspect/image/--restore` 往返通过；WSL 主机侧两机型 DSO 导出符号与四种机型组合的运行时护栏全部验证；工具与刷写入口测试通过（WSL 全绿）；实机刷写与加载未验收。
- **SD 卡清单改名为 `/pilot/modules.txt`**：相机 Fsys 校验扩展名，`.json` 一律返回 `0x80000041`（参数非法），而 `.txt`/`.so` 可读（A7C II 2.01 实测，见 `docs/USAGE.md`）。内容仍是 JSON；`pilot.h` 新增 `PILOT_MANIFEST_NAME` 与 `PILOT_MODULE_CONFIG_NAME`（模块配置同步改为 `/pilot/modules/<名>/config.txt`）。旧版加载器把该错误当成"可选功能不可用"静默禁用整套模块且不写日志，是"模块装了但菜单里没有"的实际原因。
- `tools/pilot.py modules deploy/list/logs` 改读 `modules.txt`，卡上遗留的 `modules.json` 仍会读取并在下次部署时迁移（同时写入两个文件）。
- **A7C II 支持两种在役来源（原厂 / FL23）**：新增 `patch/on_camera_profiles.py`，`patch.json` 的 `target` 可用 `accepted_sources` 按 `profile` 登记多份 `binary_sha256`/`patched_binary_sha256`；安装器与 `--check` 按整镜像哈希判定来源并打印 `profile=stock|fl23`，未登记镜像仍被拒绝。公开的 `patch.a7c2.json` 已登记 `stock`（`2ea02f7e…`→`48da7988…`）与 `fl23`（`1aaeb8fb…`→`cee59303…`）。
- FL23 是 A7C II 2.01 的 FL2/FL3 Creative Look 移植，对 `appFw.so` 的 8 处原地改写与 MenuUI 校验的全部地址零重叠，因此同一套补丁窗口与同一份模块在两种来源上都成立；`modules/menu_ui/tests/verify_a7c2_offsets.py` 先按哈希认来源，两份镜像各 120 项全通过，并新增 `--allow-unknown-source`。
- MenuUI 的 A7C II 后端在安装时对 FL23 的 8 个窗口做 FNV-1a 64 指纹，日志记 `appFw source profile=stock|fl23`；指纹不符只告警，逐站点契约校验仍是闸门。指纹常量由 `work/menu_ui_a7c2/fl23_signature.py` 推导并可回读头文件自检。
- **修正 A7 C II 按 Menu 键崩溃**：菜单组表在 appFw 里有六处引用 —— 五处代码直接 `adrp/add` 到 `0x051DF8C8`（`0x1B5A278`、`0x1B5ACC8`、`0x1B5B054`、`0x1B5B15C`、`0x1B5FC38`），只有第六处走 `.got` 槽 `0x05B80EC0`。先前按 A6700 的方式「复制整表 → 改副本 → 重定向 GOT」只有一处吃到副本，其余五处仍读原生表，菜单系统看到两份不一致的数据，按下 Menu 键即崩在 `sub_1B765B0`（`base + 0x1B765C8`，`LDRH W1,[X0,#0x2C]`，`x0` 是错位字段里取出的垃圾值 `0xb2e1`/`0xaeb1`）。同一张卡上 `-DPILOT_A7C2_NO_MENU_INJECT` 诊断构建不崩，对照实验确认了因果。后端改为**原地写原生表**：GOT 只用来定位和校验，组槽与 `count` 直接改进 `.data.rel.ro`；写入走模块自己的 `mprotect` 窗口（`pilot_memory_patch_impl()` 限制 64 字节，组槽是 `0x2E0`），页保护从 `/proc/self/maps` 读出后原样恢复。产物 `72064` B，`sha256 25e5057c…`，`glibc requirement: <= GLIBC_2.17`。- **合并上游 main（`98e3528`）**：引入 `patch/prepare_ap.py`（读相机 AP 生成配套 `patch.json`、补丁数据与通用刷写器）、通用刷写器 `patch/pilot_ap_flash.c`/`.so`（`pilot_ap_generic_v1`）、CP 刷写（`patch/cp_flash.py`、`cpdelta.so`、`cpload_run.py`）、Live View 纯文本叠加（`pilot_lv_ui.inc`、`pilot_lv_text_widget.inc`、`0x2A7` 选项弹窗、`0x260` 忙等框）与 `modules/video_modes`。冲突的 5 个文件按上游侧重建后重放本分支改动。
- 安装器状态判定改为**补丁窗口拼接哈希**：`patches[].original_bytes/target_bytes` 决定 `baseline`/`patched`/`unknown`，窗口字节相同的来源（原厂、FL23）不必登记即可安装；窗口不符时可用 `--force` 按实机窗口继续。`accepted_sources` 转为来源命名与离线校验用途，`load_map()` 仍校验其格式，`flash check/install` 打印 `profile=stock|fl23|unknown`。
- `flash install/update-pilot` 把补丁计划（`pilotflash-plan.bin`）、常驻 DSO（`pilotflash-pilot.so`）与 writer 分开上传并在结束后校验清理；`pilot_dso`/`flash_writer` 的哈希元数据变为可选。三者各自仍受 64 KiB 单文件限制。
- MenuUI 双后端共存于一份构建脚本：`PILOT_MODEL=a6700` 编 A6700 后端（含 Live View 纯文本与选项弹窗），`PILOT_MODEL=a7c2` 编 `a7c2/pilot_menu_ui_a7c2.c`（不含 LV）；两者共用 `PILOT_MANIFEST_NAME`（`modules.txt`）与 `PILOT_MODULE_CONFIG_NAME`（`config.txt`）。
- 离线验收：WSL 下 `patch/tests` + `tools/tests` 55 项通过，`modules/menu_ui/tests/run.sh` 全绿（模型层单测 + 两份 `appFw.so` 各 120 项地址校验 + A7C II 交叉构建），A6700 后端交叉编译通过；实机刷写与加载仍未验收。
- 常驻 DSO 瘦身到上传上限内：`patch/build_pilot_dso.sh` 改为按函数/数据分段编译并`--gc-sections --strip-all`，有 lld 时用 `-fuse-ld=lld` 让段紧排，无 lld 时退到 `-z norelro`；`build/pilot_a7c2.so` 从 75,528 B 降到 39,808 B（`8612eec6…`，仅 `GLIBC_2.17`，`GNU_RELRO` 保留），A6700 的 `pilot.so` 同样降到 39,808 B。链接后脚本断言 ≤65536 B。已用相机 glibc 2.25 在 `qemu-aarch64` 下 `dlopen(RTLD_NOW)` 冒烟通过。
- 新增 `modules/pilot_canvas`（由 Himpq-Pilot 移植）：Live View 原生画布、CP RAM OPD 实时波形/示波器、触摸拖拽与 `DISP` 虚拟显示页，并向其他模块导出绘图与多槽位文本接口。适配本仓库约定：补 `PILOT_DECLARE_MODULE_MODEL()` 机型标记；语言查询改为模块内自行 `dlsym("pilot_ui_get_language_v1")` 并回退英文，不再依赖 MenuUI 头里的声明；示例清单改为 `modules.txt` 的字符串数组；`build.sh` 按 `PILOT_MODEL`/`PILOT_SYSROOT` 选择 sysroot，非 `a6700` 目标直接拒绝，链接后校验 AArch64、无 `TEXTREL`、`pilot_module_init`/`pilot_module_model_a6700`/`pilot_canvas_probe_v1` 导出，并拒绝引用 `__isoc23_*` 或高于相机 glibc 2.25 的版本需求。相机 sysroot 的头文件与 2.25 桩库分别用 `-I`/`-L` 显式指定（`--sysroot` 会被交叉 gcc 内置的 `aarch64-linux-gnu/{include,lib}` 压过，编出桌面 glibc 2.38 的产物），产物只依赖 `GLIBC_2.17`。
- MenuUI 的 A7C II 后端按「功能等价」路线推进：菜单行文本内联模块启用状态（`/pilot/disable` 全局标记与 `/pilot/modules/<名>/disable` 单模块标记都会让该行显示 `[off]`）；行描述符的打开目标从 `0x1C3`（实为 `dlg_rec231` 对话框）改为 `0x2CB`（`scr7001_1004`，96 个 non-setting 菜单项里有 44 个指向它的「组内项列表」屏）。同时修掉 `rid_text_set()` 只保存指针、调用方传栈缓冲会悬垂的问题，文本改由每槽 64 字节的内部缓冲持有。A7C II 产物 71,304 B（`0604f951…`），仅 `GLIBC_2.17`。
- A7C II 的 MenuUI 后端把运行日志同时写进内核环形缓冲（`/dev/kmsg`，不可用时回退 `open("/dev/console", O_NONBLOCK)`），行首统一为 `pilot menu_ui:`：qemu 崩溃时 SD 卡无法回写，`module.log` 永远停在写入前，串口是唯一能看到"模块走到哪一步"的通道。除原有阶段日志外，注入过程会 dump 菜单表副本地址、category 0 的组计数、目标组槽与首行/首个描述符的十六进制内容，`view_string` 钩子在前 8 次命中时打印被替换的 RID 与文本。
- 串口日志的 `console_emit()` 不再缓存 `/dev/kmsg` 描述符，改为每行 `open`/`write`/`close`：实测前 8 行正常、之后全部静默丢失，框架会关闭或复用不属于自己的描述符。
- 新增诊断构建开关 `PILOT_EXTRA_CFLAGS=-DPILOT_A7C2_NO_MENU_INJECT`：只加载模块、枚举清单、注册选项，完全不改菜单表，用于把 appFw 侧的故障与本模块写入的数据区分开。菜单枚举器 `sub_1B5AC70` 的完整遍历路径已写进 `a7c2_reverse_notes.md` 第十五节 —— 它按 `*(cat+0x08)`（组数）× `slot0` 的 rows 双循环，`vtable[2072]` = `sub_1B60228(obj, &row.gid, mode, out, …)`，在 `CBZ row.setting_desc` 之后才调 `sub_1B76638`/`sub_1B765B0`；崩溃点 `a1 = 0xb2e1` 是奇数，不可能是合法对象指针，而本模块写入的行 `setting_desc` 恒为 0（会在 `CBZ` 处被跳过），因此需要对照实验判定归属。默认产物 71,688 B，诊断版 71,256 B，都只依赖 `GLIBC_2.17`。- **新增 pilot_canvas 的 A7 C II 后端**：A6700 上 canvas 复用 `menu_ui` 递入的 Live View widget，而 A7 C II 的对应文本槽（`0x3F242E0` 名字表、`No1339_Footer…`）在 `.rodata` 里零引用、是死数据，因此改为自建渲染宿主 `a7c2_host.c`：`dl_iterate_phdr` 取 `appFw.so` 基址 → 轮询 `base + 0x5F66BF0`（`sub_17CCF6C` 写入第一个全局层指针处）等 UI 就绪 → `ID::genID` + `WidgetSystem::createLayer(this, &id)` + `vt[1]`/`vt[3]` → `Widget()`+`setSize`+`setLayerID` → `pilot_canvas_create_v1` + `pilot_canvas_bind_widget_v1` → 33 ms 一帧绘制。踩过两个坑：`createLayer` 是成员函数（`this` 在 x0、`ID&` 在 x1，按非成员调用会让 libObj 解引用地址 2 而崩）；`Sprite::setCurRectNum` 在 `appFw.so` 里没有 PLT stub，静态扫符号表会误判缺失，`dlsym(RTLD_DEFAULT)` 实际拿得到，RECT 后端无需重写。`build.sh` 增 `PILOT_MODEL=a7c2` 分支（只链接 `pilot_canvas.c`/`canvas_core.c`/`a7c2_host.c`），产物 71,304 B（`sha256 4e2d985d…`），仅依赖 `GLIBC_2.17`；A6700 产物保持 78,088 B 不变。**卡上文件名必须 8.3**：相机 `Fsys` 打不开长文件名（实测 `A:/pilot/pilot_canvas_probe.so` 返回 `0x80000041`），A7 C II 用 `CANVAS.so`，且 `modules.txt` 里的扩展名必须小写 —— `patch/pilot_modules.c:78` 用 `strcmp(..., ".so")` 校验，写成大写会让整份清单作废。触摸拖动、DISP 虚拟页、多行文本槽、CP RAM OPD 示波器尚未移植，明细见 `modules/pilot_canvas/A7C2_FEASIBILITY.md`。## v0.2.0（2026-11-24）

- 支持运行时修改 CP 代码区。
- 优化 CP 内存读写和框架。

## v0.1.1（2026-11-22）

- 每次启动的 SD 日志首行前增加空行。
- 记录清单模块总数、模块文件名和加载顺序。
- 模块暂存、动态加载、API 绑定、数据目录及初始化失败记录具体模块和状态。
- 初始化成功记录模块文件名和位置。
- 增加 QEMU 启动检查，验证启动分隔和模块日志。
- 增加通用补丁 CLI：描述校验、离线镜像修改、RAM 模块构建、SD 模块部署和日志导出。
- 刷写入口支持外部本机配置，写入前备份，设备状态与补丁描述分开保存。
- 实机更新 pilot.so 并重启后，启动分隔与 7 个模块的具名加载记录正常。

- **MenuUI 增加独立的 A7C II 后端**：新增 `modules/menu_ui/pilot_menu_ui_a7c2.c`（`PILOT_MODEL=a7c2` 时由 `build.sh` 选择，A6700 路径不变）。A7C II 与 A6700 的菜单 UI 同源，但没有集中 layout 数组，文本只经过单一入口 `view_string::draw`（`appFw.so+0x1827F8C`，134 个 view 调用者；全库对 `TextSprite::setString` 的两处重载与 `prefetchTextImage` 的引用都只在该函数内）。后端因此改为「复制菜单组表并插入 Pilot 组 + inline hook 文本入口」：hook 前 16 字节序言先与基线逐字节比对，不符则放弃安装；trampoline 用 `mmap` 分配，入口被改写为 `ldr x16,#8; br x16`。视图对象字段为 `+0x204` 可见、`+0x208` 文本 RID、`+0x210` 字符串指针、`+0x318` Dictionary、`+0x320` 段数。
- `modules/menu_ui/offsets_a7c2.h` 重写为实测偏移：屏幕表 `GOT 0x05B4CFD0 → 0x051F7810`（项 `0x60`）、菜单组表 `GOT 0x05B80EC0 → 0x051DF8C8`（`0x15480` 字节，8 个 category × `0x2B30`，组 `0x2E0`）、描述符 `0x60`（`+0x00` 标签 RID、`+0x0C` 屏号、`+0x20` 可用性回调）、`0x1C3`/`0x293` 的 presenter/controller 工厂与 `TextSprite` 四个 GOT。未定位项（按键 GOT、当前组、选中行）保持 `0x00000000ull`，依赖它们的代码在 A7C II 构建中被编译掉，除非显式定义 `PILOT_A7C2_ENABLE_UNVERIFIED`。
- A7C II 后端的模块清单复用 A6700 的方式：`dlsym` 原生 `Fsys_Open/Fsys_Read/Fsys_Close` 读 `<盘符>:/pilot/modules.txt`（回退 `modules.json`），解析 `modules` 字符串数组，每个模块一行；读不到时回退四行固定内容。清单解析器有独立单测 `work/test_manifest_parse.sh`。
- `modules/menu_ui/build.sh` 改为双机型：`PILOT_MODEL=a7c2|a6700` 选择源文件、`-DPILOT_MODEL_A7C2` 与默认 sysroot；sysroot 探测顺序 `PILOT_SYSROOT` → 机型变量 → `../work/pilot-a7c2/sysroot`；显式 `-I $SYSROOT/usr/include -L $SYSROOT/lib`（交叉 gcc 自带的头文件是宿主 glibc，`--sysroot` 会被内置路径压过）；构建末尾校验 AArch64、无 `TEXTREL`、`pilot_module_init` 与 `pilot_module_model_<model>` 符号，并拒绝 `__isoc23_*` 或高于 `GLIBC_2.25` 的产物。两个变体在 WSL + 相机 sysroot 下均构建通过（A7C II 70,904 B、A6700 79,456 B，两者都只依赖 `GLIBC_2.17`）。
- `A6700` 后端的 `pilot_menu_ui.c:2127` 检查补 `(size_t)g_detail_module_index < PILOT_LIST_ITEMS_MAX`，消除 gcc 15 对 `g_mod_enabled[16]` 的 `-Werror=array-bounds` 误报（行为等价）。
- `modules/menu_ui/device.json` 改为双机型；`config.txt` 重写为准确的机型、状态与 A7C II 限制说明；`README.md` 补充适用机型表、构建命令与 sysroot/glibc 说明，并新增 `A7C2_PORT_STATUS.md`（结论摘要、已逆向的文本路径、仍缺的 5 个偏移、偏移速查）与 `a7c2_reverse_notes.md`（结构解构全过程）。
- 已知限制（A7C II，离线未验收）：详情页正文尚未自定义、确认/返回/左右按键未接管、Live View 文本槽未接线；菜单列表本身可显示 Pilot 组与每个模块一行。
- **MenuUI 的 A7C II 后端补齐 `pilot_ui_*` 导出**：`pilot_ui_set_rid_override_v1`（模块指定的 RID 文本在下一次重绘时被 `view_string` hook 替换，其他模块不必直连 appFw）与 `pilot_ui_register_options_v1`（收集选项表并记日志）。A6700 还导出 `pilot_ui_show_busy_v1`/`pilot_ui_hide_busy_v1`/`pilot_ui_text_show_v1`/`pilot_ui_text_clear_v1`/`pilot_lv_show_toast`/`pilot_ui_lv_force_refresh_v1`，A7C II 上没有对应钩子，故意不导出：调用方按 `PILOT_STATUS_NOT_FOUND` 处理，与「服务未加载」语义一致，不会误以为文本已经显示。两个后端都不导出 `pilot_ui_get_language_v1`，调用方回退英文。
