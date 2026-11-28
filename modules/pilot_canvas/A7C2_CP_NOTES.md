# A7 C II 的 CP 侧静态分析

这份笔记记录 ILCE-7CM2（A7 C II）2.01 与 ILCE-6700 2.00 的 CP 固件对照结果，
以及从对照里得到的地址。所有结论都标注了证据；没有证据的一律写成未确证。

## 0. 当前能做什么、不能做什么

| 能力 | 通道 | 需要 CP 补丁吗 | 状态 |
| --- | --- | --- | --- |
| 读 CP RAM（测光网格 OPD） | `/dev/pcidma` + `ioctl 0x40185001` | **不需要** | 地址已定位（本文 §3），待实机验证 |
| 写 CP RAM | osal uipc route `0x90030001` / cmd `0x7E` | 需要 | 补丁代码已编译，**段权限未确证**（§5） |

pilot_canvas 的波形只依赖"读"，因此 §3 的地址是当前唯一的关键路径；
写通道风险高且没有明确用途，建议在拿到实机验证条件之前不要刷写。

## 1. 取得 A6700 的 CP 镜像

A6700 的 `cpapp-b.bin` 不在工作区（工作区里所有 `cpapp-b.bin` 都是 A7 C II 的
18,090,880 B 版本）。从官方固件里解出来：

```bash
python work/fx2-unpack/unpack_dat.py "ILCE-A6700-v2.00-BODYDATA.DAT" work/a6700-unpack
python work/a6700_find_cp2.py work/a6700-unpack/firmware.tar work/a6700-unpack
```

- `unpack_dat.py` 需要 `Cryptodome`（Windows 侧 `pip install pycryptodomex` 已装好）。
- 固件解密后 `firmware.tar` 有 137 个条目，机身镜像在 `0700_part_image/dev/nflasha*`。
- **CP 镜像在 `nflasha8` 偏移 `0x24C000`**，用 loader 头
  `04 10 9f e5 04 10 81 e2 01 f0 a0 e1` 定位，sha256
  `61b7fe5c0ce227ea865d799b81fe77ae359b7c5dcd39d13c9c2241d9cad23559`
  与 `patch.a6700.json` 记录一致。
- 教训：只用 12 字节接收模式 `307818b1 05280ad0 02280cd1` 扫描会出现假阳性
  （`nflasha8` 的 `0x5EBE52` 处也匹配，但取 17,892,024 字节后 hash 不对）。

## 2. 两镜像的对照

| 项 | A6700 2.00 | A7 C II 2.01 |
| --- | --- | --- |
| 大小 | 17,892,024（`0x11102B8`） | 18,090,880（`0x1140B80`） |
| sha256 | `61b7fe5c…` | `60a9de1a…` |
| loader 与首 128 KiB | — | **逐字节相同（共同前缀 `0x20000`）** |
| 接收函数 hook 点 | file `0x39FE52`（VA `0x4A7E52`） | file `0x3ABB0A`（VA `0x4B3B0A`） |
| hook 处 24 字节 | `30 78 18 b1 05 28 0a d0 02 28 0c d1 30 46 29 46 02 f0 a3 fa 07 e0 30 46` | **逐字节相同** |

`bl 0x4b6064` 的相对编码在两边都一样，说明这段代码整体平移了同样的距离
（hook 点差 `0xBCB8`）。由此确定：

- `orig_nonzero_cmd = 0x004B3B0F`、`orig_zero_cmd = 0x004B3B17`、
  `orig_free_msg = 0x004B3B31`（与 A6700 的 `+5 / +13 / +39` 完全一致）。
- A6700 的补丁语义（v4 帧解析、`classify_range`、`cp_patch_apply`）可以直接沿用。

## 3. 测光网格（OPD）表地址 —— 本轮的关键结论

A6700 的 `canvas_cp_sensors.c` 用 `CP_OPD_TABLE_VA 0x015B6398`，步长 `0x70`，
按 `slot 0` 与 `slot 2` 读两个描述符（`+0x58` = bank 物理地址，`+0x60` = 长度）。

在 A6700 镜像里，这个地址由 5 处 `movw/movt` 对生成，出现在一组小函数里：

```
852116: f246 3198   movw  r1, #0x6398
85211a: ebc0 00c0   rsb   r0, r0, r0, lsl #3     ; r0 = r0*7
85211e: f2c0 115b   movt  r1, #0x015b            ; r1 = 0x015B6398
852122: eb01 1000   add.w r0, r1, r0, lsl #4     ; r0 = base + idx*0x70
```

`rsb r0, r0, r0, lsl #3` 后接 `lsl #4` 就是 `idx*0x70`，与
`CP_OPD_TABLE_VA + slot*CP_OPD_ENTRY_STRIDE` 的算法逐步对应。

同一组函数在 A7 C II 镜像里逐字节相同，只换了立即数
（`work/cp_pairs.py` 可以复现）：

```
86d5f2: f241 7138   movw  r1, #0x1738
86d5fa: f2c0 1165   movt  r1, #0x0165            ; r1 = 0x01651738
86d5fe: eb01 1000   add.w r0, r1, r0, lsl #4
```

五个引用点一一对应（`0x852116↔0x86d5D6`、`0x852132↔0x86d5F2`、
`0x85214A↔0x86d60A`、`0x852182↔0x86d642`、`0x852198↔0x86d658`，差值恒为
`0x1B4C0`），且两条指令序列除立即数外完全相同。

**结论：A7 C II 2.01 的 OPD 表基址 = `0x01651738`（A6700 为 `0x015B6398`）。**

已写入 `canvas_cp_sensors.c` 的 `#ifdef PILOT_MODEL_A7C2` 分支。

### 仍需实机确认的部分

- `0x01651738` 是"表基址"这一语义来自 A6700 的实机结论外推，A7 C II 上是同
  一组代码的同一位置，静态证据很强，但没有在 A7 C II 上读过。
- `CP_LENS_INFO_BASE_VA`（`0x016A4998`）与 `CP_SYNCRC_GEOM_VA`（`0x01696490`）
  没有对应过来。A6700 镜像里 `0x016A4xxx` 有 22 处引用（基址可能是
  `0x016A4098` + 字段偏移 `0x900`），A7 C II 的对应基址还没匹配。两个函数在
  读失败时本来就返回"没有值"，所以焦距/几何显示会留空，不影响波形。

## 4. 代码穴：A6700 的地址在 A7 C II 上已被占用

| | A6700 | A7 C II |
| --- | --- | --- |
| cave | VA `0xC12D68`（file `0xB0AD68`），零区长 664 B | 该地址处已是代码（`06 a9 2c 22 …`） |
| 候选 | — | VA `0xC63EA4`（file `0xB5BEA4`），零区长 **8,320 B** |

交叉检查：A6700 在 VA `0xC63EA4` 处是数据（`08 20 10 c1 …`），A7 C II 在
VA `0xC12D68` 处是代码 —— 两版的零区位置不同，必须各自定。

候选 cave 的性质（`work/cp_cave_ctx.py`）：

- 前邻 `0xC63E64`–`0xC63EA4` 是 Thumb 代码；
- 后邻 `0xC65F24` 起是只读字符串（`000000000000%02u%02u`、`[%6d] Loader Err…`）；
- 与 A6700 cave 的"零区夹在代码与数据之间"同型。

引用审计（`work/cp_refs.py`，枚举全镜像的 `movw/movt` 对）：

- A6700 指向自己 cave `0xC12D68..0xC13000` 的引用：**0 处**；
- A7 C II 指向候选 `0xC63EA4..0xC641A4` 的引用：**0 处**。

## 5. 未确证的部分（刷写前必须解决）

1. **段权限**。`classify_range` 沿用的是 A6700 的
   CODE `0x00128000..0x011D8000`、DATA `0x011DA000..0x01ACF674`，A7 C II 的
   段边界没有独立测量过。
2. **可写性**。A6700 的 cave 在 CODE 段内，写入靠 `cpsid i` + DACR 置 manager
   绕过 AP。这条路在 A7 C II 上是否同样成立，没有实证 —— 而且
   `A7C2_FEASIBILITY.md` 里已经记录：**A6700 的 `uipc_v4` 钩子本身从未实机打过**，
   所以连 A6700 的这一前提都是设计推演，不是实测。
3. **跨页执行**。候选 cave 起于 `0xC63EA4`，868 字节补丁会跨过 4 KiB 页边界
   （`0xC63000` → `0xC64000`）。`0xC64000` 页是否映射为可执行未证实 —— 前
   0x15C 字节与已知代码同页，后半段没有依据。

**建议**：波形只需要读通道（`/dev/pcidma`），不涉及 §5 的任何一个风险。先在
A7 C II 上验证 §3 的地址能读出合理数据，再决定要不要推进写通道。

## 6. 工具

| 脚本 | 用途 |
| --- | --- |
| `work/fx2-unpack/unpack_dat.py` | 解 `.DAT` 固件（需 `Cryptodome`） |
| `work/a6700_find_cp2.py` | 按 loader 头 + sha256 从 `firmware.tar` 提取 CP 镜像 |
| `work/cp_pairs.py <lo> <hi> [shift]` | 解码区间内所有 `movw/movt` 对，用于两镜像地址对照 |
| `work/cp_refs.py <image> <lo> <hi>` | 全镜像枚举 `movw/movt`，审计是否有代码引用某地址窗口 |
| `work/cp_cave_ctx.py` | 打印两个 cave 候选前后的字节与零区长度 |
| `work/cp_scan.py` | 扫 hook 模式，打印文件偏移与 16 字节上下文 |
| `work/cp_inspect.sh <file_off>:<len>` | 十六进制查看（VA = 偏移 + `0x108000`） |
| `work/cp_segments.py` | 64 KiB 块的非零/可打印密度图 |

## 7. IDA 参数

- 文件：`cpapp-b.bin`（二进制，无 ELF 头）
- Processor：**ARM**，Little-endian
- Loading address：**`0x108000`**（VA = 文件偏移 + `0x108000`）
- 若以 `0` 载入，执行 `ida_segment.rebase_program(0x108000, 0)`
- **ARM/Thumb 混合**：接收函数 `0x4B3B0A` 一带是 Thumb，`0x5E1F58` 是 ARM
  （`blx` 切换）。IDA 对 raw binary 默认按 ARM 反汇编，Thumb 区域需要
  `ida_bytes.set_flag(ea, ida_bytes.FF_THUMB)` 逐个标记。
- `objdump` 对 raw binary 线性反汇编会错位（`-M force-thumb` 在
  `0x3ABB08` 就解出错误的 `bl`），核对字节请用 `xxd`，指令边界从已知入口重开。
