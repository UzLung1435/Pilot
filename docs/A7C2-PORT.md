# Pilot 适配 A7C II（ILCE-7CM2，固件 2.01）

本文件记录把为 A6700 2.00 设计的 Pilot 启动框架移植到 A7C II 2.01 的全部依据、产物与边界。
**当前状态：离线二进制适配完成并验证；实机刷写、实机加载、模块运行均未验收。**

## 1. 范围与机型隔离

- 已适配：AP 侧 `appFw.so` 的 DT_INIT 启动钩子（注入岛）、ELF 段扩展、四个补丁窗口、离线镜像描述、刷写入口的机型参数化。
- 已移植模块：MenuUI。A7C II 没有 A6700 那套页/项表，`modules/menu_ui/a7c2/` 改为在“设置选项”页追加一行、并接管该行打开的原生列表屏 `0x2cb`；构建用 `PILOT_MODEL=a7c2`，产物 `pilot_menu_ui_a7c2.so`，内容树与全部地址见 [a7c2/README.md](../modules/menu_ui/a7c2/README.md)。
- 未适配：6K。它硬编码了 ILCE-6700 2.00 的录制档位与 CP 参数，A7C II 的档位表、成像器与 CP 布局不同，构建脚本会直接拒绝非 `a6700` 的机型。
- **不再共用引导产物**：两个机身不再共享 `pilot.so` 或岛内字符串表。机型身份由仓库根的 [`models.json`](../models.json) 唯一定义，`patch/pilot_model.h` 是 C 侧镜像，`tools/pilot_tool/models.py --check` 保证两者不漂移。

| 项目 | A6700 2.00 | A7C II 2.01 |
|---|---|---|
| 机型 ID / 构建宏 | `a6700` / `PILOT_MODEL_A6700` | `a7c2` / `PILOT_MODEL_A7C2` |
| 常驻 DSO | `/usr/lib/pilot.so`（`pilot.so`） | `/usr/lib/pilot_a7c2.so`（`pilot_a7c2.so`） |
| marker 导出 | `pilot_a6700_v1_marker` | `pilot_a7c2_v1_marker` |
| 挂载回调导出 | `pilot_a6700_v1_on_mount` | `pilot_a7c2_v1_on_mount` |
| API 导出 | `pilot_a6700_get_api` | `pilot_a7c2_get_api` |
| 模块机型标记 | `pilot_module_model_a6700` | `pilot_module_model_a7c2` |
| 启动追踪文件 | `/tmp_wbshd/pilot-boot.bin` | `/tmp_wbshd/pilot-a7c2-boot.bin` |
| 刷写器结果头 | `A6700PILOTFLASH\n` | `A7C2PILOTFLASH\n` |

A6700 是已实机验证的构建，**保持逐字节不变**（仍用 `pilot.so` 与 `pilot_a6700_*`）；A7C II 全部换成 `a7c2` 名字。因此把 A6700 的 DSO 或模块拿到 A7C II 上，岛 `dlopen` 会因文件名不同直接失败，即使同名放进去也会被 `dlsym` 判活与模块标记挡住。

`pilot_module_init` 与 `PILOT_ABI_VERSION = 2` 仍是唯一跨机型的模块 ABI 入口；机型隔离由它之外的标记符号承担。

三层防呆（都已离线验证）：

1. **刷写前**：`on_camera.py` 从 `--map` 的 `runtime` 块读取机型身份，校验 `pilot_dso` 的哈希后，再要求 DSO 内含本机型三个导出符号、且不含其它机型 marker；设备路径与结果头同步机型化。
2. **部署时**：`pilot.py modules deploy --model <id>` 要求模块导出且仅导出本机型的 `pilot_module_model_<id>`，并拒绝以其它机型 ID 开头的文件名。
3. **运行时**：`pilot_load_modules()` 在 `dlopen` 后、调用 `pilot_module_init()` 之前用 `dlsym` 检查模块标记，不匹配就记录 `model mismatch expected=… found=…` 并停止整个包的加载（沿用 fuse 隔离机制）。

## 2. 基线与依据

| 项目 | 值 |
|---|---|
| 官方更新包 | `ILCE-7CM2-v2.01-BODYDATA.DAT`（1,139,929,560 B） |
| 原厂 `appFw.so` | `oldProjects/FlashDir/backup1.3/original/appFw.so.bin` |
| 大小 / SHA-256 | 99,960,744 B / `2ea02f7ecbc5bb917316fe8ef4ce5ffd87eb66efaae78ba2ce11420552df539b` |
| 分区来源 | `work/a7c2-unpack/0700_part_image/dev/nflasha15` 内 `/lib/appFw.so`，与上列文件逐字节一致 |
| ELF | ELF64 / AArch64 / ET_DYN，`e_entry=0x1660680`，6 个 program headers，节区表完好 |
| 参考补丁 | `patch.a6700.json`（A6700 2.00，`pilot.patch-map.v1`） |

研究依据（`a7c2-knowledge-base/`、`a7c2-re/`、`work/`）：官方分区提取与布局、整机转储、`APP-START.md`（`im` 先完成 DT_INIT/367 项 INIT_ARRAY 再 dlopen 返回）、`APPFW-SPACE.md`（同尺寸窗口补丁模型）、`20261027-native-integration-active-01a178a6.md`（扩展 RX PT_LOAD 的既有先例）、`2026-10-18-libjit-exec/README.md`（Jiritsu/libJit 入口）、`work/pp-lut-20261020-haita.md` 与 `work/cineei/`（ELF 段与指针布局）。

## 3. A6700 ↔ A7C II 关键差异

| 项目 | A6700 2.00 | A7C II 2.01 |
|---|---|---|
| `appFw.so` 大小 | 89,027,992 | 99,960,744 |
| `PT_LOAD[0]` `p_filesz`/`p_memsz` | `0x4489500` | `0x51a6cff` |
| 下一 LOAD 文件偏移 | `0x4489aa0` | `0x51a7718` |
| 段尾空隙 | `0x5a0` | `0xa19`（4 对齐后 `0xa18`） |
| 字段偏移 | `0x60` / `0x68` | `0x60` / `0x68`（一致） |
| 注入岛 | `0x4489500` / `0x500` | `0x51a6d00` / `0x500` |
| `DT_INIT` / hook | `0x1769dd0` / `0x1769ddc` | `0x165cfb8` / `0x165cfc4` |
| hook 原指令 | `fd7bc1a8` `ldp x29,x30,[sp],#0x10` | **完全相同** |
| `dlsym_plt` / `dlopen_plt` | `0x176b3d0` / `0x176b090` | `0x165e810` / `0x165e490` |
| `dlsym` / `dlopen` GOT VA | `0x4f1fae0` / `0x4f1f940` | `0x5bc1c10` / `0x5bc1a50` |
| VA 规则 | `PT_LOAD[0]` VA = 文件偏移 | `PT_LOAD[0]` VA = 文件偏移；可写段 **VA = 文件偏移 + `0x10000`** |

A7C II 的 `.init` 与 A6700 逐指令同构：`stp / mov x29,sp / bl <原初始化> / ldp x29,x30,[sp],#0x10 / ret`；被替换的 `ldp` 机器码相同。因此注入点、收尾重放和栈帧约定都可沿用。

## 4. 注入岛重定位与字符串表机型化

1280 字节的岛不是位置无关数据，内部硬编码了 A6700 的绝对 BL 目标。生成脚本用 capstone 逐条识别**外部 PC 相对分支**，只重编码 4 条岛内指令和 1 条 hook 分支；岛内 `ADR`、条件分支、内部 `BL` 与重放的 `ldp` 全部保持不变（相对位移不随整体平移改变）。

| 岛内偏移 | 指令 | A6700 目标 | A7C II 目标 | 字节 |
|---|---|---|---|---|
| `+0x0c0` | `bl dlsym` | `0x176b3d0` | `0x165e810` | `94de1297` |
| `+0x0dc` | `bl dlsym` | `0x176b3d0` | `0x165e810` | `8dde1297` |
| `+0x0ec` | `bl dlopen` | `0x176b090` | `0x165e490` | `a9dd1297` |
| `+0x1dc` | `b <DT_INIT ret>` | `0x1769de0` | `0x165cfc8` | `3bd81217` |
| hook `0x165cfc4` | `b <island>` | `0x4489500` | `0x51a6d00` | `4f27ed14` |

岛尾部是固定尺寸的内联字符串表，由 `ADR` 指向槽首并以 NUL 结尾，**替换文本不需要改动任何指令**：

| 槽位 | 槽长 | A6700 原值 | A7C II 新值 |
|---|---:|---|---|
| `+0x400` | 0x20 | `/usr/lib/pilot.so` | `/usr/lib/pilot_a7c2.so` |
| `+0x420` | 0x20 | `pilot_a6700_v1_marker` | `pilot_a7c2_v1_marker` |
| `+0x440` | 0x20 | `pilot_a6700_v1_on_mount` | `pilot_a7c2_v1_on_mount` |
| `+0x460` | 0x30 | `/tmp_wbshd/pilot-boot.bin` | `/tmp_wbshd/pilot-a7c2-boot.bin` |
| `+0x490` | 0x70 | `dlerror` | `dlerror`（不变） |

生成脚本会断言每个槽原值匹配、旧串尾部全 0、新串（含 NUL）不越界，并检查结果岛中不存在任何 A6700 身份字符串。

岛的实际行为（反汇编确认）：保存全部寄存器与 NZCV/FP 状态 → `openat` 以 `O_EXCL` 建 `/tmp_wbshd/pilot-a7c2-boot.bin` 作启动追踪 → `dlsym(RTLD_DEFAULT,"pilot_a7c2_v1_marker")` 防重复加载 → `dlsym(RTLD_DEFAULT,"dlerror")` → `dlopen("/usr/lib/pilot_a7c2.so", RTLD_NOW|RTLD_GLOBAL=0x102)` → 失败时把 `dlerror()` 文本写入追踪文件 → 恢复现场、重放原 `ldp`、跳回 `0x165cfc8` 的 `ret`。岛**不调用** `pilot_a7c2_v1_on_mount`；`pilot_a7c2.so` 的 constructor 自己起 worker 轮询 SD 卡（初始 200 ms，之后 250 ms × 240 轮），所以晚于 DT_INIT 才加载的库不会导致启动失败。

## 5. 四个补丁窗口

| 窗口 | 文件偏移 = VA | 长度 | 原值 | 新值 |
|---|---|---:|---|---|
| `post_fsys_mount_branch` | `0x165cfc4` | 4 | `fd7bc1a8` | `4f27ed14` |
| `post_fsys_mount_dlopen_island` | `0x51a6d00` | 0x500 | 全 0 | 重定位 + 机型化后的岛 |
| `pt_load0_p_filesz` | `0x60` | 8 | `ff6c1a0500000000` | `00721a0500000000` |
| `pt_load0_p_memsz` | `0x68` | 8 | `ff6c1a0500000000` | `00721a0500000000` |

`PT_LOAD[0]` 从 `0x51a6cff` 扩到 `0x51a7200`，仍比下一个 LOAD 的 `0x51a7718` 早 `0x518` 字节，不重叠；**文件总长度不变**（同尺寸窗口写入，与 A7C II 上已有的 `pwrite64` + `fsync` 实践一致，不受 `/usr` 仅约 58.5 MiB 空闲的限制）。岛起点取 `(0x51a6cff+4)&~3 = 0x51a6d00`，因为 AArch64 指令与 `B imm26` 都要求 4 字节对齐。所有节区、动态表与 RELA 表位置不变，不需要按 `DT_RELA` 重取表址。

结果镜像 SHA-256：`48da79886ecef1b8ee4d6250ed54c5e352ab8a9ab5a84b6e00a684182a3d21dc`（机型化字符串表之后；此前共用身份的版本为 `77bc5da0…a6c`，已作废）。

## 6. 产物与复现

| 文件 | 用途 |
|---|---|
| `models.json` | `pilot.models.v1` 机型注册表：身份、导出名、DSO 路径、岛字符串槽与结果头 |
| `patch/pilot_model.h` | C 侧机型宏；`models.py --check` 校验与注册表一致 |
| `tools/pilot_tool/models.py` | 注册表读取、运行时块生成、C 头一致性检查、shell 构建脚本取值 CLI |
| `patch.a7c2.json` | `pilot.patch-map.v1` 结构与字节说明（含 `runtime` 机型块、重定位记录） |
| `patch.a7c2.image.json` | `pilot.patch.v1` 离线镜像描述，供通用工具直接打补丁/恢复 |
| `work/pilot-a7c2/build_a7c2_patch.py` | 可复现生成脚本（读 A6700 参考岛 + 原厂 A7C II 镜像 + 机型注册表） |
| `work/pilot-a7c2/verify_build.sh`、`verify_model_guard.sh` | WSL 主机侧：两机型 DSO 导出符号核对、四种机型组合的运行时护栏验证 |
| `work/pilot-a7c2/out/` | 结果镜像与 `verification.json` |

```sh
# 使用系统 Python 3.10（含 capstone 5）
python work/pilot-a7c2/build_a7c2_patch.py

# 仓库通用工具：描述校验、打补丁、原样恢复
python tools/pilot.py inspect patch.a7c2.image.json
python tools/pilot.py image patch.a7c2.image.json <原厂appFw.so> --out <结果.so>
python tools/pilot.py image patch.a7c2.image.json <结果.so> --restore --out <恢复.so>
```

已完成的离线校验：全文件 SHA-256 基线匹配；hook 指令、`.init` 字节、岛窗口全 0、两个 `PT_LOAD[0]` 字段逐字节核对；`validate_spec` 通过；`patch_image` 正向与 `--restore` 往返都回到原基线；对结果镜像重新反汇编，确认 4 条外部分支目标分别为 `0x165e810`/`0x165e490`/`0x165cfc8`、hook 为 `b 0x51a6d00`、尾部重放 `fd7bc1a8`、五个字符串槽与注册表一致且无 A6700 残留、两个段大小均为 `0x51a7200`。

主机侧（WSL gcc）还验证了：A6700/A7C II 两个机型的框架 DSO 分别只导出 `pilot_a6700_*` / `pilot_a7c2_*` 三个符号；模块分别导出 `pilot_module_model_a6700` / `pilot_module_model_a7c2`；四种「框架机型 × 模块机型」组合中，仅同机型组合会走到 `Pilot module initialized`，异机型组合均在 `pilot_module_init()` 之前被拒绝并记录 `model mismatch`。

## 7. 运行时环境证据

- **tmpfs**：`/tmp_wbshd` 在 A7C II 上是 tmpfs（`a7c2-re/report/2026-10-16-system-dump/RECOVERY-AND-LESSONS.md`），机型化的追踪文件路径可直接沿用。
- **刷写链**：`/tmp/jiritsu`、Jiritsu 帧、`libJit.so` 的 `dlopen` 入口与服务身份 `054c:0336` 在 A7C II 与 A6700 上相同（`07-sources/.../2026-10-18-libjit-exec/README.md`）；`on_camera.py` 的 staging 流程不变，但设备上的常驻库路径改为 `/usr/lib/pilot_a7c2.so`。
- **Fsys 依赖**：`pilot_fsys_resolve()` 需要的 9 个符号 `Fsys_Stat/Open/Read/Write/Seek/Flush/Close` 与 `pf2_errnum/pf2_inerrnum` **全部由 `/lib/libInfraFsys.so` 导出**（对 `nflasha15` 内 291 个 `.so` 的 PT_DYNAMIC 解析结果）。`appFw.so` 自身只导入 18 个 `Fsys_*`，但 `im` 通过 IMDB 链 `dlopen` `libInfraFsys.so`；Pilot 的 worker 是延迟轮询的，因此解析时机不依赖 DT_INIT 瞬间。
- **路径**：运行时 `/usr/lib/appFw.so` 是 p15 分区内 `/lib/appFw.so` 的挂载视图；`/usr/lib/pilot_a7c2.so` 同分区，`/usr` 默认只读，写入需 remount（刷写器职责）。

## 8. 构建与安装

构建 A7C II 的常驻 DSO 时显式指定机型；输出名、soname 和导出符号都由 `models.json` 决定：

```sh
PILOT_MODEL=a7c2 PILOT_SYSROOT=/path/to/a7c2-2.01/sysroot \
    bash patch/build_pilot_dso.sh
# 默认产物：build/pilot_a7c2.so
sha256sum build/pilot_a7c2.so

# 省略 PILOT_MODEL 时仍是已验证的 A6700 构建：build/pilot.so
```

模块用相同的 `PILOT_MODEL` 编译，并用 `PILOT_DECLARE_MODULE_MODEL()` 导出机型标记；MenuUI 已支持 `PILOT_MODEL=a7c2`（另编 `a7c2/pilot_menu_ui_a7c2.c`，产物 `pilot_menu_ui_a7c2.so`），6K 仍会在非 `a6700` 时拒绝构建。

```c
#include "pilot.h"
PILOT_DECLARE_MODULE_MODEL();          /* 导出 pilot_module_model_a7c2 */
PilotStatus pilot_module_init(const PilotApi *api) { /* ... */ }
```

部署到 SD 卡时显式声明机型：

```sh
python tools/pilot.py modules deploy private/a7c2_example.so --card D:/ \
    --model a7c2 --sha256 <模块SHA256> --backup-dir F:/PilotLocal/backups
```

`flash` 使用本机完整的 `patch.json`。以 `patch.a7c2.json` 为基础，**连同整个 `runtime` 块一起复制**（不可删字段），补齐产物哈希后另存（不要修改公开说明文件）：

```json
{
  "schema": "pilot.patch-map.v1",
  "target": {
    "model": "Sony ILCE-7CM2 (A7C II)", "firmware": "2.01",
    "binary_size": 99960744,
    "binary_sha256": "2ea02f7e…539b",
    "patched_binary_sha256": "48da7988…a21dc"
  },
  "runtime": { "…完整复制 patch.a7c2.json 的 runtime 块…" },
  "patches": [ "…复制 patch.a7c2.json 的四项…" ],
  "pilot_dso":     { "size_bytes": <N>, "sha256": "<build/pilot_a7c2.so 的 SHA-256>" },
  "flash_writer":  { "size_bytes": <N>, "sha256": "<pilot_ap_flash.so 的 SHA-256>" }
}
```

`runtime` 块决定设备路径、默认结果头（`A7C2PILOTFLASH\n`）与 DSO 符号校验；若私有刷写器仍输出 `A6700PILOTFLASH\n`，在 `patch.json` 加 `"flash": {"header": "A6700PILOTFLASH\n"}` 覆盖。writer 与常驻 DSO 均不得超过 Jiritsu 的 64 KiB 传输上限。`--pilot` 省略时默认取 `build/<runtime.dso_soname>`。

```sh
python tools/pilot.py flash check   --map F:/PilotLocal/a7c2/patch.json --transport-root <transport> --state-dir <state>
python tools/pilot.py flash install --map F:/PilotLocal/a7c2/patch.json --writer <pilot_ap_flash.so> --pilot <pilot_a7c2.so> --transport-root <transport> --state-dir <state> --yes
```

## 9. 风险与未验项

1. **刷写器不在本工作树**。`pilot_ap_flash.so` 是本机私有产物，内部窗口必须与上面四个窗口一致、按 A7C II 重新构建，并输出 `runtime.flash_header` 指定的结果头；本工作树无法验证其字节，因此 `flash install/restore/update-pilot` 一律未验收。
2. **签名与信任链**。修改 `appFw.so` 会破坏该分区的校验；知识库记录了同机公钥、WBI 规则和 3.10 更新事故，`p1chksum` 的消费者仍未定位。**先 QEMU/离线验证，再谈实机**，并保留原文件备份与完整断电恢复手段。
3. **未做实机加载**。DT_INIT 阶段 `dlopen` `pilot_a7c2.so`、constructor 建线程、`dlsym(RTLD_DEFAULT)` 命中 `libInfraFsys.so`、SD 卡盘符（`BXACDE`）与 disable 标记，都只做了静态与库导出层面的论证，尚无 A7C II 真机日志。
4. **`dlerror` 仅在失败路径使用**：若进程全局解析不到，`x21=0` 会跳过错误文本写入，不影响正常加载。
5. **`/usr` 空间**：本方案是同尺寸窗口（文件长度不变），但新增 `/usr/lib/pilot_a7c2.so` 仍需几十 KiB 空闲；该分区历史上仅约 58.5 MiB 可用。
6. **QEMU 验证建议**：用 `work/qemu-deploy/` 的 WSL QEMU 环境（`CHECK_RUNTIME_PASS ILCE-7CM2 2.01`）挂载补丁镜像，先确认 DT_INIT 岛执行、`/tmp_wbshd/pilot-a7c2-boot.bin` 的 PBT1 记录与 `/pilot/log.txt` 启动分隔。
7. **模块边界**：6K 仍不是机型中立模块（改 A6700 录制档位/CP 参数），A7C II 需要按 `.data.rel.ro 0x51a82a0` 档位表与成像器能力重新逆向。MenuUI 已按本机菜单布局重写后端：`modules/menu_ui/a7c2/` 接管原生列表屏 `0x2cb`，`tests/verify_a7c2_offsets.py` 对原厂 `appFw.so` 校验全部地址常量，**未实机验收**。
8. **DSO 符号检查是字符串级防呆**。`on_camera.py` 与 `deploy.py` 在 PC 端扫描 ELF 中 NUL 结尾的符号名；真正的加载期判定由岛的 `dlsym` 和 `pilot_load_modules()` 完成。故意伪造字符串只能绕过电脑端检查，不能在相机上通过。
9. **6K baseline 复现**：`pilot.h` 新增机型宏不改变 `PilotApi` 布局或原有导出名，但 `modules/6k/baseline.json` 记录的两个中间 `.so` 哈希仍需在原 AArch64 构建环境复验。
