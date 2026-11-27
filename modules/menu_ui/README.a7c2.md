# Pilot Menu UI - A7C II Port

在相机"自定义设置"菜单中增加 Pilot 入口，查看 Pilot 版本、模块列表、启用状态与文件路径。

**重要提示**: 这是从 a6700 移植到 a7c2 的版本。当前使用 a6700 的 appFw 偏移量作为占位符。
需要通过逆向工程 ILCE-7CM2 2.01 固件的 appFw.so 来确定正确的偏移值。

## 需要更新的偏移量

在 `pilot_menu_ui.c` 中，以下偏移量需要针对 a7c2 进行验证和更新：

- `APPFW_GOT_MENU_TABLE` - 菜单表 GOT 指针
- `APPFW_MENU_TABLE_OFF` - 菜单表偏移
- `APPFW_MENU_TABLE_SIZE` - 菜单表大小
- `APPFW_CUSTOM_GROUP_OFF` - 自定义组偏移
- `APPFW_CUSTOM_SLOT12_OFF` - 自定义槽位12偏移
- `APPFW_STOCK_DESC_293_OFF` - 0x293屏幕描述符偏移
- `APPFW_STOCK_ITEM_DESC_OFF` - 项目描述符偏移
- `APPFW_GOT_PAGE_TABLE` - 页面表 GOT 指针
- `APPFW_PAGE_TABLE_OFF` - 页面表偏移
- `APPFW_GOT_LAYOUT_1C3` - 0x1C3布局 GOT 指针
- `APPFW_LAYOUT_1C3_OFF` - 0x1C3布局偏移
- `APPFW_GOT_LAYOUT_293` - 0x293布局 GOT 指针
- `APPFW_LAYOUT_293_OFF` - 0x293布局偏移
- `APPFW_GOT_CTRL_ITEMS_1C3` - 0x1C3控制项 GOT 指针
- `APPFW_CTRL_ITEMS_1C3_OFF` - 0x1C3控制项偏移
- 以及所有其他 `APPFW_*` 相关偏移

这些偏移量通过静态分析 appFw.so 的以下内容获得：
- GOT (Global Offset Table) 表项
- 菜单表结构和布局
- 页面工厂表
- 控制器和视图工厂函数
- 按钮处理器钩子

## 移植状态

- [x] 复制 Himpq-Pilot 的 menu_ui 代码
- [x] 更新 device.json 为 a7c2
- [x] 添加 PILOT_DECLARE_MODULE_MODEL() 声明
- [x] 更新 build.sh 支持 a7c2 编译
- [x] 添加 -DPILOT_MODEL_A7C2 编译标志
- [x] 更新描述文字提及 a7c2
- [ ] **待完成**: 通过逆向工程确定 a7c2 的 appFw.so 偏移量
- [ ] **待完成**: 更新所有 APPFW_* 偏移常量
- [ ] **待完成**: 在实际 a7c2 相机上测试

## 逆向工程方法

要确定 a7c2 的 appFw.so 偏移量，需要：

1. 获取 ILCE-7CM2 2.01 固件的 appFw.so 二进制文件
2. 使用 IDA Pro/Ghidra 等工具分析 ELF 结构
3. 定位 GOT 表中的关键符号引用
4. 找到菜单系统的数据结构（通过字符串引用和函数调用追踪）
5. 确定屏幕工厂函数地址（0x1C3, 0x293, 0x2A7, 0x260等）
6. 验证控制器和布局表的结构和偏移

## 构建

```bash
cd Pilot/modules/menu_ui
export PILOT_MODEL=a7c2
export PILOT_A7C2_SYSROOT=/path/to/a7c2/sysroot
./build.sh
```

或者直接指定输出路径：

```bash
./build.sh ../../build/pilot_menu_ui.so
```

## 安装

**警告**: 在完成偏移量更新并测试之前，不要在实际相机上安装此模块！

1. 将 `pilot_menu_ui.so` 复制到 SD 卡的 `/pilot/` 目录
2. 在 `/pilot/modules.json` 中添加：
   ```json
   {
     "modules": ["pilot_menu_ui.so"]
   }
   ```
3. 完整断电重启相机
4. 打开"自定义设置 → Pilot"

## 使用

- 选择"Pilot 版本"查看版本信息
- 选择模块名称查看状态和详细信息
- 通过模块开关启用/禁用各个模块

## 技术细节

此模块的工作原理：

1. **菜单注入**: 通过修改 appFw.so 的菜单表，在"自定义设置"中插入新的 Pilot 组（group ID 0x535）
2. **页面劫持**: Hook 屏幕工厂函数（0x1C3详情页, 0x293列表页），替换布局和内容
3. **按钮处理**: 拦截确认、返回等按钮事件，实现自定义导航逻辑
4. **动态UI**: 通过 PilotUiPage/PilotUiComponent 声明式API构建界面
5. **文本渲染**: Hook TextSprite::setString() 注入自定义文本

## 兼容性

- 目标相机: Sony ILCE-7CM2 (A7C II)
- 目标固件: 2.01
- Pilot ABI: 2
- 模块标识: pilot_module_model_a7c2

此模块不能在 a6700 上使用，框架会在加载前检查模块标识符并拒绝不匹配的模块。
