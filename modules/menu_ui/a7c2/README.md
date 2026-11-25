# Pilot Menu UI — ILCE-7CM2 (A7C II) 2.01

在 A7C II 原厂菜单里插入一行 Pilot，并用原厂列表屏展示 Pilot 内容。这是
[`../pilot_menu_ui.c`](../pilot_menu_ui.c)（ILCE-6700 2.00）的移植，但两机的菜单结构不同，
所以实现方式也不同：A6700 版自建页面并接管两个原厂屏（`0x293` / `0x1C3`），A7C II 版只在
“设置选项”页追加一行，然后接管该行打开的原生列表屏 `0x2cb`。

**适用机型：Sony ILCE-7CM2（A7C II）固件 2.01。** 所有偏移都取自 2.01 的 `appFw.so`，
模块导出 `pilot_module_model_a7c2`；其它机型的框架会拒绝加载，构建脚本也会拒绝非 `a7c2` 的
`PILOT_MODEL`。

## 菜单位置

“设置 → 设置选项”页末尾会多出第 8 行 `Pilot`，选中后进入原生列表屏：

```
Pilot
├── 状态
│   ├── 机型        ILCE-7CM2 (A7C II)
│   ├── 固件        2.01
│   ├── 框架        pilot_a7c2 - ABI 2
│   └── 入口        设置 - 设置选项
└── 工具
    ├── 操作次数    0 次
    ├── 调试信息    开／关   （可改，仅本机内存）
    ├── 调试状态    已启用／已关闭
    └── 重置计数    动作
```

列表本身由原厂渲染：页头、页码、滚动、左右翻页、返回上一层的焦点恢复都沿用原厂实现，
模块只提供内容与一次写回。

## 来源变体：原厂 / FL23

同一份 2.01 `appFw.so` 有两种在役来源，模块在两种上都成立：

| 来源 | 源镜像 SHA-256 | 装 Pilot 后的 SHA-256 |
|---|---|---|
| 原厂 2.01 | `2ea02f7e…52df539b` | `48da7988…2a3d21dc` |
| FL23（FL2/FL3 Creative Look 移植） | `1aaeb8fb…71e324d7` | `cee59303…c050ef01` |

FL23 只原地改写 8 处（`.text` 的 look→CP 转换器、`.rodata` 的两处选择能力与 Fade 默认值、
普通 look 的 slot16/17 初始绑定、`.data.rel.ro` 的两个菜单产品标志），**与模块校验的全部
地址零重叠**，所以后端无需另一套偏移。安装器（`patch/on_camera.py` +
`patch/on_camera_profiles.py`）按 `patch.a7c2.json` 的 `target.accepted_sources` 认这两个来源，
未知镜像仍会被拒绝；`--check` 会打印 `state: baseline profile=fl23` 这样的行。

模块自己不解整镜像哈希（100 MB 太大），而是对 FL23 的那 8 个窗口做 FNV-1a 64 指纹，
安装时写一行日志：

```
PILOT_MENU_INFO appFw source profile=fl23
PILOT_MENU_READY patches=11 item=0x601 template=0x220 list=0x2cb abi=2 profile=fl23
```

指纹不匹配时只记 `PILOT_MENU_WARN appFw source 0x… is not a recorded image`，**不拒绝安装**——
真正的闸门是逐站点的契约校验（页描述符、7 行 id、列表函数头、两张表、键码记录），任何一处
不符都会整体回滚。指纹的两个常量由 `work/menu_ui_a7c2/fl23_signature.py` 从 FL23 补丁表推导，
该脚本还会回读本目录头文件确认常量没走样。

## 构建

```bash
PILOT_MODEL=a7c2 bash build.sh          # 产物 build/pilot_menu_ui_a7c2.so
```

默认使用 `aarch64-linux-gnu-gcc`。设了 `PILOT_A7C2_SYSROOT` 时用它做 `--sysroot`，否则用
交叉工具链自带的 libc；构建末尾会检查产物没有引用高于 `GLIBC_2.25` 的符号版本——A7C II
本体是 glibc 2.25，用新主机构建时最容易在这里出问题（`dlopen`/`dlsym` 会被记成
`GLIBC_2.34`）。模块因此不使用 `dlopen`，`libObj.so` 的两个入口点由 `dl_iterate_phdr`
按装载基址加固定偏移得到。

## 安装

1. 把 `build/pilot_menu_ui_a7c2.so` 复制到 SD 卡的 `/pilot/`。
2. 在 `/pilot/modules.txt` 的 `modules` 数组里加入 `pilot_menu_ui_a7c2.so`，保留其它条目（文件扩展名必须是 `.txt`：相机 Fsys 对 `.json` 返回参数非法，内容仍是 JSON）：

```json
{
  "modules": ["pilot_menu_ui_a7c2.so"]
}
```

3. 装卡并完整断电重启。
4. 开机后打开“设置 → 设置选项”，第 8 行即 `Pilot`。

框架会在 `dlopen` 之后、`pilot_module_init()` 之前校验 `pilot_module_model_a7c2`，
机型不符会记录 `model mismatch` 并跳过本模块。

## 验证

```bash
python tests/verify_a7c2_offsets.py     # 对原厂 appFw.so 校验全部地址/字节常量
bash   tests/run.sh                     # 主机侧模型测试 + 偏移校验 + 交叉编译
```

`verify_a7c2_offsets.py` 默认读
`oldProjects/FlashDir/backup1.3/original/appFw.so.bin`（ILCE-7CM2 2.01，99,960,744 字节），
`--image` 可指向任何 2.01 副本（例如 FL23 的 `backup1.3/candidate/appFw.so`）。它先用整镜像
SHA-256 对照 `patch.a7c2.json` 的 `target.accepted_sources` 报出 `source: profile=stock|fl23`，
遇到已打 Pilot 的镜像或未记录变体直接退出 1（后者可加 `--allow-unknown-source` 强行校验站点）。检查项：ELF 段/节布局、被克隆的页描述符各字段与空槽、
被替换的 4 条指令与列表屏函数头、安装时写入的每个指针槽（按 `R_AARCH64_RELATIVE` 的
addend，而不是文件里的 0）、原生列表的两张表和键码记录。120 项全部通过才算常量自洽，
stock 与 FL23 两份镜像实测都是 120/120。

模块自己也带同样的预检：`pilot_install()` 在 `memory_patch` 之前核对每一处待改内容，
任何一项不符就整体回滚并返回 `PILOT_STATUS_CONFLICT`，不会在别的固件上留下半套补丁。

## 安装时改动的原厂状态

| 位置 | 内容 |
|---|---|
| `appFw+0x597d578+0x18/+0x730/+0x738/+0x7b0` | 条目 normal/alias/reverse 与 tree builder |
| `appFw+0x5204410`、`+0x5b13598` | presenter 工厂、root enter |
| `appFw+0x5bc1fe8` | `TextSprite::setString(uint32_t)` GOT |
| `appFw+0x2001eb4`、`+0x1e0f990`、`+0x1b6dcd8`、`+0x1b6de94` | 4 处 BL 改到 veneer 页 |
| `appFw+0x597d850`、`+0x597d620`、`+0x5208438`、`+0x5208430` | 列表 children / row value / c 工厂 / p 工厂 |
| `appFw+0x5b13600`、`+0x5b135e0`、`+0x5afaeb0`、`+0x5b0c498`、`+0x5b0c4a0` | 列表 enter / 析构 / refresh / open / close |
| `appFw+0x1b6bec4`、`+0x1b6ae28`、`+0x1d1e448`、`+0x1934974` | 列表调用点与 dispatch 入口改到 veneer 页 |

所有写入都经 `PilotApi.memory_patch`（自带 expected 比较、`mprotect`、清 I-cache、
恢复权限），补丁表保存在 `pilot_menu_observation.patches`，失败路径逆序回滚。

## 已知限制

- **未在实机验证。** 静态校验与主机测试覆盖了地址、内容树和编辑器，但列表接管、
  焦点恢复、按键分发只能在机身上确认。原厂与 FL23 两份镜像都只做了离线校验。
- “调试信息”开关只存在内存里，重启即恢复；模块不做任何持久化。
- 只在 2.01 上成立（原厂与 FL23 两种来源）。换固件后 `verify_a7c2_offsets.py` 会失败，此时必须先按
  `a7c2-re/report/2026-11-05-appfw-source-rebuild/firmware_abi/` 的方法重新取地址，
  不要直接改常量绕过预检。新的 2.01 变体若要进白名单，用
  `work/menu_ui_a7c2/fl23_profile.py --write` 的思路补一条 `accepted_sources`，并在模块侧
  补一条来源指纹；未知指纹只会告警，契约校验仍是闸门。
- 移植自 a7c2-re 的原生列表适配器（屏幕 `0x2cb` 的接管逻辑），原实现只做到
  “新增一个 item + 焦点返回”的切片验收；本模块把同一套传输层接到了自持注册表上，
  两边的新增行为都需要实机回归。
