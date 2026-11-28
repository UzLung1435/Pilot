# A7 C II (ILCE-7CM2 2.01) 的 CP 补丁

这个目录给 A7 C II 的 CP 固件加一条 **uipc v4 写入通道**，结构与
`patch/cp/uipc_v4/`（A6700）一致，只把地址换成 A7 C II 2.01 的值。

## 它解决什么

`PilotCpMemApi` 的读路径走 `/dev/pcidma`（`ioctl 0x40185001`），不依赖 CP 补丁。
**写** CP 的 RAM 需要 CP 侧有一个能接收帧并落地的处理函数，原厂固件没有，
所以要在 CP 里插一个：

```
主机 pilot_cp_mem.c           CP 固件
  osal uipc route 0x90030001 → cmd 0x7E → cp_receiver_hook
                                             ├ 非 0x7E：原样放行
                                             ├ 0x00    ：原样放行
                                             └ 0x7E    ：cp_patch_apply(msg)
```

## 两个补丁窗口

| 名称 | 文件偏移 | VA | 改动 |
| --- | --- | --- | --- |
| `receiver-hook` | `0x003ABB0A` | `0x004B3B0A` | `30 78 18 b1` → `b0 f3 cb b1`（`b.w 0x00C63EA4`） |
| `uipc-v4-handler` | `0x00B5BEA4` | `0x00C63EA4` | 868 字节零 → 补丁代码 |

`0x003ABB0A` 处原本是 `ldrb r0,[r6]; cbz r0,0x4B3B16`，与 A6700 的
`0x0039FE52` 是同一个函数形状（`cmp r0,#5` / `cmp r0,#2` 两条分支、末尾
`movs r0,#38; blx free`）。镜像里 `30 78 18 b1` 只有这一处。

`0x00C63EA4` 之后 868 字节在镜像里全零，且落在 A6700 用过的 CODE 窗口
（`0x00128000..0x011D8000`）之内；A6700 用的是 `0x00C12D68`，那一段在
A7 C II 上不是零区。

## 与 A6700 版本的两处差异

1. **不再调用 `osal_msg_size`**。A6700 的 hook 先查消息长度再交给处理函数，
   A7 C II 上这个函数还没定位（A6700 是 `0x00BCFE2E`）。主机侧的 v4 发送是
   单条目（`8 B 头 + 6 B 条目头 + ≤2048 B 数据`，`_Static_assert` 保证），
   所以 `cp_patch_apply(msg, 0)` 走单条目模式：只解析第一条，不做长度越界。
   A6700 的 20 字节探测帧因此改成 8 字节头。
2. **cave 与 self 窗口全部换址**（见上表）。`pilot_cp_mem.c` 里
   `#ifdef PILOT_MODEL_A7C2` 会切到新值。

## 构建

```sh
bash patch/cp/a7c2_uipc_v4/build.sh
```

用 `arm-linux-gnueabi-gcc`（Thumb，cortex-a9，freestanding），产物：

- `patch.bin` 868 B —— 写到 `0x00B5BEA4`
- `branch.bin` 4 B = `b0 f3 cb b1` —— 写到 `0x003ABB0A`

`branch.bin` 由 `hook_branch.S` + `branch.ld` 生成。注意 `.align 2` 会在
`b.w` 前塞 2 字节填充、把目标地址算错，必须用 `.align 1`。

生成 `patch.a7c2.json` 的 `cp` 段：

```sh
python patch/cp/a7c2_uipc_v4/gen_patch.py \
    --cpapp <原厂 cpapp-b.bin> --out /tmp/cp.json
```

脚本会逐字节核对 hook 处的原值、cave 是否全零、`branch.bin` 是否等于
`b0 f3 cb b1`，然后写出 `cp.patch`（`kind=file`、`component=cpapp-b.bin`、
`sha256=60a9de1a…`→`patched_sha256=332095f8…`）。把结果整体放进
`patch.a7c2.json` 的 `cp.patch` 即可。

## 刷写与回滚

```sh
python patch/on_camera.py --cp-check    # 只读，判断 original/installed
python patch/on_camera.py --cp-install  # 写入并整镜像回读校验
python patch/on_camera.py --cp-restore  # 还原
```

写入走 `cpdelta.so`（与 A6700 同一份，`A6700D01` 是 delta 格式标识，不是机型）。
完成后必须**断电重启**相机，CP 才会从新镜像启动。刷写前脚本会先把整个
`cpapp-b.bin` 备份到 `work/cp-backups/`。

## 还没验证的部分

- `cp_patch_apply` 的 `DATA` 窗口沿用 A6700 的 `0x011DA000..0x01ACF674`，
  `CODE` 窗口沿用 `0x00128000..0x011D8000`。A7 C II 的 CP 镜像只有
  18,090,880 B（VA 上界 `0x1248B80`），这两条边界是照 A6700 搬的，没有实测
  过 A7 C II 自己映射到了哪里 —— 如果以后要写这个范围之外的目标，先量出来
  再放宽。
- 补丁代码只做了静态验证（`arm-linux-gnueabi-objdump` 反汇编目标地址、符号表、
  cave 全零、`branch.bin` 编码），**没有在相机上跑过**。
- `STATUS_VA 0x00C64180` 是补丁自己写的状态页（`'PTCH'` 魔数），
  `cp_uipc_probe_v4_locked()` 用 `SELF_END` 发 size=0 探测帧来区分 v4 固件。
