# 更新记录

## 未发布（A7C II 2.01 适配 + 机型隔离）

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

## v0.1.1（2026-11-22）

- 每次启动的 SD 日志首行前增加空行。
- 记录清单模块总数、模块文件名和加载顺序。
- 模块暂存、动态加载、API 绑定、数据目录及初始化失败记录具体模块和状态。
- 初始化成功记录模块文件名和位置。
- 增加 QEMU 启动检查，验证启动分隔和模块日志。
- 增加通用补丁 CLI：描述校验、离线镜像修改、RAM 模块构建、SD 模块部署和日志导出。
- 刷写入口支持外部本机配置，写入前备份，设备状态与补丁描述分开保存。
- 实机更新 pilot.so 并重启后，启动分隔与 7 个模块的具名加载记录正常。
