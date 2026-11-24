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

## v0.1.1（2026-11-22）

- 每次启动的 SD 日志首行前增加空行。
- 记录清单模块总数、模块文件名和加载顺序。
- 模块暂存、动态加载、API 绑定、数据目录及初始化失败记录具体模块和状态。
- 初始化成功记录模块文件名和位置。
- 增加 QEMU 启动检查，验证启动分隔和模块日志。
- 增加通用补丁 CLI：描述校验、离线镜像修改、RAM 模块构建、SD 模块部署和日志导出。
- 刷写入口支持外部本机配置，写入前备份，设备状态与补丁描述分开保存。
- 实机更新 pilot.so 并重启后，启动分隔与 7 个模块的具名加载记录正常。
