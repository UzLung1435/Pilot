# Pilot 适配 A7C II（ILCE-7CM2，固件 2.01）

本文件记录把为 A6700 2.00 设计的 Pilot 启动框架移植到 A7C II 2.01 的全部依据、产物与边界。
**当前状态：离线二进制适配完成并验证；实机刷写、实机加载、模块运行均未验收。**

## 1. 范围

- 已适配：AP 侧 `appFw.so` 的 DT_INIT 启动钩子（注入岛）、ELF 段扩展、四个补丁窗口、离线镜像描述、刷写入口的机型参数化、构建脚本说明。
- 未适配：6K 等机型专用模块。6K 改的是 A6700 的录制档位与 CP 侧参数，A7C II 的档位表、成像器与 CP 布局不同，不能照搬；框架可先独立运行。
- 保持 ABI 不变：`PILOT_ABI_VERSION = 2`、`pilot_module_init`、以及 `pilot_a6700_*` 三个导出名。这些名字是历史命名，现在作为**跨机型 ABI 名**沿用，因此同一个 `pilot.so` 与岛内字符串表在两台机身上通用。

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

## 4. 注入岛重定位

1280 字节的岛不是位置无关数据，内部硬编码了 A6700 的绝对 BL 目标。生成脚本用 capstone 逐条识别**外部 PC 相对分支**，只重编码 4 条岛内指令和 1 条 hook 分支；岛内 `ADR`、条件分支、内部 `BL`、内联字符串与重放的 `ldp` 全部保持不变（相对位移不随整体平移改变）。

| 岛内偏移 | 指令 | A6700 目标 | A7C II 目标 | 字节 |
|---|---|---|---|---|
| `+0x0c0` | `bl dlsym` | `0x176b3d0` | `0x165e810` | `94de1297` |
| `+0x0dc` | `bl dlsym` | `0x176b3d0` | `0x165e810` | `8dde1297` |
| `+0x0ec` | `bl dlopen` | `0x176b090` | `0x165e490` | `a9dd1297` |
| `+0x1dc` | `b <DT_INIT ret>` | `0x1769de0` | `0x165cfc8` | `3bd81217` |
| hook `0x165cfc4` | `b <island>` | `0x4489500` | `0x51a6d00` | `4f27ed14` |

岛的实际行为（反汇编确认）：保存全部寄存器与 NZCV/FP 状态 → `openat` 以 `O_EXCL` 建 `/tmp_wbshd/pilot-boot.bin` 作启动追踪 → `dlsym(RTLD_DEFAULT,"pilot_a6700_v1_marker")` 防重复加载 → `dlsym(RTLD_DEFAULT,"dlerror")` → `dlopen("/usr/lib/pilot.so", RTLD_NOW|RTLD_GLOBAL=0x102)` → 失败时把 `dlerror()` 文本写入追踪文件 → 恢复现场、重放原 `ldp`、跳回 `0x165cfc8` 的 `ret`。岛**不调用** `pilot_a6700_v1_on_mount`；`pilot.so` 的 constructor 自己起 worker 轮询 SD 卡（初始 200 ms，之后 250 ms × 240 轮），所以晚于 DT_INIT 才加载的库不会导致启动失败。

## 5. 四个补丁窗口

| 窗口 | 文件偏移 = VA | 长度 | 原值 | 新值 |
|---|---|---:|---|---|
| `post_fsys_mount_branch` | `0x165cfc4` | 4 | `fd7bc1a8` | `4f27ed14` |
| `post_fsys_mount_dlopen_island` | `0x51a6d00` | 0x500 | 全 0 | 重定位后的岛 |
| `pt_load0_p_filesz` | `0x60` | 8 | `ff6c1a0500000000` | `00721a0500000000` |
| `pt_load0_p_memsz` | `0x68` | 8 | `ff6c1a0500000000` | `00721a0500000000` |

`PT_LOAD[0]` 从 `0x51a6cff` 扩到 `0x51a7200`，仍比下一个 LOAD 的 `0x51a7718` 早 `0x518` 字节，不重叠；**文件总长度不变**（同尺寸窗口写入，与 A7C II 上已有的 `pwrite64` + `fsync` 实践一致，不受 `/usr` 仅约 58.5 MiB 空闲的限制）。岛起点取 `(0x51a6cff+4)&~3 = 0x51a6d00`，因为 AArch64 指令与 `B imm26` 都要求 4 字节对齐。所有节区、动态表与 RELA 表位置不变，不需要按 `DT_RELA` 重取表址（那是整体重排的 HEVC 候选才需要）。

结果镜像 SHA-256：`77bc5da01506d475c96a1e53e00cdbafac4d3608b4b1c66912f2fefbf4169a6c`。

## 6. 产物与复现

| 文件 | 用途 |
|---|---|
| `patch.a7c2.json` | `pilot.patch-map.v1` 结构与字节说明（含基线/结果哈希、分析、重定位记录） |
| `patch.a7c2.image.json` | `pilot.patch.v1` 离线镜像描述，供通用工具直接打补丁/恢复 |
| `work/pilot-a7c2/build_a7c2_patch.py` | 可复现生成脚本（读 A6700 参考岛 + 原厂 A7C II 镜像，自动解析 PLT/GOT） |
| `work/pilot-a7c2/analyze_hook.py` | 岛反汇编与 ELF 事实核对 |
| `work/pilot-a7c2/check_fsys_symbols.py` | 扫描原厂 `/usr` 分区内各 `.so` 的导出符号 |
| `work/pilot-a7c2/out/` | 结果镜像、`verification.json`、CLI 往返副本 |

```sh
# 使用系统 Python 3.10（含 capstone 5）
python work/pilot-a7c2/build_a7c2_patch.py

# 仓库通用工具：描述校验、打补丁、原样恢复
python tools/pilot.py inspect patch.a7c2.image.json
python tools/pilot.py image patch.a7c2.image.json <原厂appFw.so> --out <结果.so>
python tools/pilot.py image patch.a7c2.image.json <结果.so> --restore --out <恢复.so>
```

已完成的离线校验：全文件 SHA-256 基线匹配；hook 指令、`.init` 字节、岛窗口全 0、两个 `PT_LOAD[0]` 字段全部逐字节核对；`validate_spec` 通过；`patch_image` 正向与 `--restore` 往返都回到原基线；对结果镜像重新反汇编，确认 4 条外部分支目标分别为 `0x165e810`/`0x165e490`/`0x165cfc8`、hook 为 `b 0x51a6d00`、尾部重放 `fd7bc1a8`、字符串槽完好、两个段大小均为 `0x51a7200`。

## 7. 运行时环境证据

- **tmpfs**：`/tmp_wbshd` 在 A7C II 上是 tmpfs（`a7c2-re/report/2026-10-16-system-dump/RECOVERY-AND-LESSONS.md`），岛的追踪文件路径可直接沿用。
- **刷写链**：`/tmp/jiritsu`、Jiritsu 帧、`libJit.so` 的 `dlopen` 入口与服务身份 `054c:0336` 在 A7C II 与 A6700 上相同（`07-sources/.../2026-10-18-libjit-exec/README.md`），`on_camera.py` 的路径与 staging 流程无需改机型。
- **Fsys 依赖**：`pilot_fsys_resolve()` 需要的 9 个符号 `Fsys_Stat/Open/Read/Write/Seek/Flush/Close` 与 `pf2_errnum/pf2_inerrnum` **全部由 `/lib/libInfraFsys.so` 导出**（对 `nflasha15` 内 291 个 `.so` 的 PT_DYNAMIC 解析结果）。`appFw.so` 自身只导入 18 个 `Fsys_*`，但 `im` 通过 IMDB 链 `dlopen` `libInfraFsys.so`；Pilot 的 worker 是延迟轮询的，因此解析时机不依赖 DT_INIT 瞬间。
- **路径**：运行时 `/usr/lib/appFw.so` 是 p15 分区内 `/lib/appFw.so` 的挂载视图；`/usr/lib/pilot.so` 同分区，`/usr` 默认只读，写入需 remount（刷写器职责）。

## 8. 构建与安装

构建 `pilot.so` 与 A6700 相同，只需把 sysroot 指向 A7C II 2.01 的目标根：

```sh
PILOT_SYSROOT=/path/to/a7c2-2.01/sysroot bash patch/build_pilot_dso.sh
sha256sum build/pilot.so
```

`patch/build_pilot.sh` 仍检查 `pilot_a6700_v1_on_mount` / `pilot_a6700_v1_marker` / `pilot_a6700_get_api`——这是刻意保留的跨机型 ABI 名。

`flash` 使用本机完整的 `patch.json`。以 `patch.a7c2.json` 为基础，补齐产物哈希后另存（不要修改公开说明文件）：

```json
{
  "schema": "pilot.patch-map.v1",
  "target": {
    "model": "Sony ILCE-7CM2 (A7C II)", "firmware": "2.01",
    "binary_size": 99960744,
    "binary_sha256": "2ea02f7e…539b",
    "patched_binary_sha256": "77bc5da0…a6c"
  },
  "patches": [ "…复制 patch.a7c2.json 的四项…" ],
  "pilot_dso":     { "size_bytes": <N>, "sha256": "<build/pilot.so 的 SHA-256>" },
  "flash_writer":  { "size_bytes": <N>, "sha256": "<pilot_ap_flash.so 的 SHA-256>" }
}
```

`on_camera.py` 现在支持可选的 `flash.header`：默认仍是 `A6700PILOTFLASH\n`（A7C II 沿用同款刷写器时不必填写）；若本机刷写器使用自己的结果头，在 `patch.json` 加 `"flash": {"header": "自定义头\n"}`。writer 与 `pilot.so` 均不得超过 Jiritsu 的 64 KiB 传输上限。

```sh
python tools/pilot.py flash check   --map F:/PilotLocal/a7c2/patch.json --transport-root <transport> --state-dir <state>
python tools/pilot.py flash install --map F:/PilotLocal/a7c2/patch.json --writer <pilot_ap_flash.so> --pilot <pilot.so> --transport-root <transport> --state-dir <state> --yes
```

## 9. 风险与未验项

1. **刷写器不在本工作树**。`pilot_ap_flash.so` 是本机私有产物，内部窗口必须与上面四个窗口一致并按 A7C II 重新构建；本工作树无法验证其字节，因此 `flash install/restore/update-pilot` 一律未验收。
2. **签名与信任链**。修改 `appFw.so` 会破坏该分区的校验；知识库记录了同机公钥、WBI 规则和 3.10 更新事故，`p1chksum` 的消费者仍未定位。**先 QEMU/离线验证，再谈实机**，并保留原文件备份与完整断电恢复手段。
3. **未做实机加载**。DT_INIT 阶段 `dlopen` `pilot.so`、constructor 建线程、`dlsym(RTLD_DEFAULT)` 命中 `libInfraFsys.so`、SD 卡盘符（`BXACDE`）与 disable 标记，都只做了静态与库导出层面的论证，尚无 A7C II 真机日志。
4. **`dlerror` 仅在失败路径使用**：若进程全局解析不到，`x21=0` 会跳过错误文本写入，不影响正常加载。
5. **`/usr` 空间**：本方案是同尺寸窗口（文件长度不变），但新增 `/usr/lib/pilot.so` 仍需几十 KiB 空闲；该分区历史上仅约 58.5 MiB 可用。
6. **QEMU 验证建议**：用 `work/qemu-deploy/` 的 WSL QEMU 环境（`CHECK_RUNTIME_PASS ILCE-7CM2 2.01`）挂载补丁镜像，先确认 DT_INIT 岛执行、`/tmp_wbshd/pilot-boot.bin` 的 PBT1 记录与 `/pilot/log.txt` 启动分隔。
7. **模块边界**：MenuUI 这类纯框架模块原则上可复用；6K 模块涉及 A6700 专属档位/CP 参数，需单独按 A7C II 的 `.data.rel.ro 0x51a82a0` 档位表与成像器能力重新逆向，本次未改动。
