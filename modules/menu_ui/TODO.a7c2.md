# TODO: A7C2 Menu UI 移植待办事项

## 关键任务

### 1. 逆向工程 a7c2 appFw.so 偏移量 ⚠️ 高优先级

需要分析 ILCE-7CM2 2.01 固件的 appFw.so，确定以下偏移量：

#### GOT 表偏移
- [ ] `APPFW_GOT_MENU_TABLE` (0x4EA8A68ull in a6700)
- [ ] `APPFW_GOT_PAGE_TABLE` (0x4E48818ull in a6700)
- [ ] `APPFW_GOT_NONSETTING_TREE` (0x4EA32E0ull in a6700)
- [ ] `APPFW_GOT_LAYOUT_1C3` (0x4E4C8C8ull in a6700)
- [ ] `APPFW_GOT_LAYOUT_293` (0x4E77818ull in a6700)
- [ ] `APPFW_GOT_CTRL_ITEMS_1C3` (0x4E9A470ull in a6700)
- [ ] `APPFW_GOT_SET_STRING_RID` (0x4F1FE98ull in a6700)
- [ ] `APPFW_GOT_SET_STRING_CSTR` (0x4F1F010ull in a6700)
- [ ] `APPFW_GOT_BTN_CONFIRM` (0x4ED84A0ull in a6700)
- [ ] `APPFW_GOT_BTN_RIGHT` (0x4E49748ull in a6700)
- [ ] `APPFW_GOT_BTN_LEFT` (0x4E548D0ull in a6700)
- [ ] `APPFW_GOT_BTN_BACK` (0x4E666F8ull in a6700)
- [ ] `APPFW_GOT_BTN_BACK2` (0x4EDE688ull in a6700)
- [ ] `APPFW_GOT_1C3_CONFIRM` (0x4EB0FE0ull in a6700)
- [ ] `APPFW_GOT_ISNAV_TABLE` (0x4E5B430ull in a6700)

#### 数据结构偏移
- [ ] `APPFW_MENU_TABLE_OFF` (0x44B8AB0ull in a6700)
- [ ] `APPFW_MENU_TABLE_SIZE` (0xF908ull in a6700)
- [ ] `APPFW_CUSTOM_GROUP_OFF` (0xDE68ull in a6700)
- [ ] `APPFW_CUSTOM_SLOT12_OFF` (0xDF98ull in a6700)
- [ ] `APPFW_CUSTOM_STOCK_COUNT` (12u in a6700)

#### 模板偏移
- [ ] `APPFW_STOCK_DESC_293_OFF` (0x4BF0090ull in a6700)
- [ ] `APPFW_STOCK_ITEM_DESC_OFF` (0x4BF8828ull in a6700)
- [ ] `APPFW_DESC_SIZE` (0x60ull in a6700)
- [ ] `APPFW_ITEM_ENTRY_SIZE` (0x10ull in a6700)
- [ ] `APPFW_ITEM_DESC_SIZE` (0xB0ull in a6700)

#### 函数偏移
- [ ] `APPFW_VISIBILITY_OK_OFF` (0x2351858ull in a6700)
- [ ] `APPFW_TREE_INSERT_OFF` (0x236C014ull in a6700)
- [ ] `APPFW_293_GET_FOCUSED_ITEM_OFF` (0x29EAAC4ull in a6700)
- [ ] `APPFW_GET_CUR_GROUP_OFF` (0x2F731D8ull in a6700)
- [ ] `APPFW_GET_SEL_ROW_OFF` (0x2F73228ull in a6700)
- [ ] `APPFW_GET_CUR_ITEM_OFF` (0x2F73278ull in a6700)
- [ ] `APPFW_MENU_ITEM_IS_NAVIGABLE_OFF` (0x22DC8BCull in a6700)
- [ ] `APPFW_MENU_ITEM_OPEN_OFF` (0x22DC94Cull in a6700)
- [ ] `APPFW_MENU_ITEM_RESOLVE_SCREEN_OFF` (0x22DC288ull in a6700)
- [ ] `APPFW_293_GENERIC_OPEN_OFF` (0x29EAB8Cull in a6700)
- [ ] `APPFW_293_CURSOR_OBJ_OFF` (0x11B38ull in a6700)
- [ ] `APPFW_CURSOR_NEXT_OFF` (0x2185B98ull in a6700)
- [ ] `APPFW_CURSOR_PREV_OFF` (0x2185B00ull in a6700)

#### 页面表和布局
- [ ] `APPFW_PAGE_TABLE_OFF` (0x44CA518ull in a6700)
- [ ] `APPFW_PAGE_ENTRY_SIZE` (0x58ull in a6700)
- [ ] `APPFW_PAGE_STOCK_COUNT` (0x42Cu in a6700)
- [ ] `APPFW_LAYOUT_1C3_OFF` (0x4FD7130ull in a6700)
- [ ] `APPFW_LAYOUT_1C3_ITEMS` (13u in a6700)
- [ ] `APPFW_CTRL_ITEMS_1C3_OFF` (0x4AE9210ull in a6700)
- [ ] `APPFW_CTRL_ITEM_SIZE` (0x28ull in a6700)
- [ ] `APPFW_LAYOUT_293_OFF` (0x508C270ull in a6700)
- [ ] `APPFW_LAYOUT_293_ITEMS` (12u in a6700)
- [ ] `APPFW_LAYOUT_ITEM_SIZE` (0x60ull in a6700)

#### 工厂函数
- [ ] `APPFW_VIEW_FACTORY_1C3_OFF` (0x26620E0ull in a6700)
- [ ] `APPFW_CTRL_FACTORY_1C3_OFF` (0x24B2624ull in a6700)
- [ ] `APPFW_VIEW_FACTORY_293_OFF` (0x25CB2ECull in a6700)
- [ ] `APPFW_CTRL_FACTORY_293_OFF` (0x252DD08ull in a6700)

### 2. 分析方法

推荐使用以下工具和步骤：

1. **获取 appFw.so**
   ```bash
   # 从 a7c2 固件提取或从相机文件系统获取
   # 路径: /usr/lib/appFw.so (运行时) 或 p15:/lib/appFw.so (固件包)
   ```

2. **IDA Pro 分析**
   - 加载 appFw.so (AArch64 ELF)
   - 定位 GOT 段 (.got.plt)
   - 搜索菜单相关字符串 ("Custom", "Pilot", "Menu")
   - 交叉引用追踪到菜单表数据结构

3. **Ghidra 分析**
   - 创建新项目，导入 appFw.so
   - 使用 Function Call Trees 追踪屏幕工厂
   - 分析数据布局和结构体

4. **比对 a6700 和 a7c2**
   - 寻找相似的函数序列和数据模式
   - 通过符号名称和字符串定位关键点
   - 验证结构大小和布局是否一致

### 3. 验证和测试

- [ ] 在模拟器或测试环境中验证偏移
- [ ] 创建最小测试用例验证每个hook点
- [ ] 逐步启用功能，确保稳定性

### 4. 文档更新

- [ ] 更新 README.md 说明偏移量来源
- [ ] 记录分析过程和发现
- [ ] 创建 a7c2 特定的配置说明

## 次要任务

### 代码质量
- [ ] 添加更多错误检查和日志
- [ ] 改进偏移验证逻辑
- [ ] 添加运行时版本检查

### 功能增强
- [ ] 支持更多UI组件类型
- [ ] 改进导航体验
- [ ] 添加配置持久化

### 构建系统
- [ ] 优化 build.sh 脚本
- [ ] 添加自动化测试
- [ ] 创建发布流程

## 风险和注意事项

⚠️ **警告**: 
- 错误的偏移量会导致相机崩溃或死机
- 建议先在可恢复的环境中测试（如通过SSH连接的测试固件）
- 保持固件备份和恢复方案
- 逐步验证每个功能模块

## 参考资料

- Himpq-Pilot a6700 实现
- Pilot 框架文档
- a7c2 逆向工程报告
- Sony 相机固件结构文档
