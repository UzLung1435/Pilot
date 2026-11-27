# IDA Pro 偏移量查找指南

## 准备工作

1. **打开数据库**
   ```
   File -> Open -> 选择 appFw.so.i64
   ```

2. **等待分析完成**
   - IDA 会自动分析二进制文件
   - 查看状态栏确认分析完成

## 查找方法

### 方法 1: 使用 IDA Python 脚本

1. **运行提取脚本**
   ```
   File -> Script file... -> 选择 extract_offsets_ida.py
   ```
   
   或者按 `Alt+F7` 打开脚本命令行，粘贴脚本内容

2. **查看输出**
   - 在 Output 窗口查看结果
   - 记录找到的地址

### 方法 2: 手动查找关键偏移

#### A. 查找 GOT 表项

1. **定位 .got 段**
   ```
   View -> Open subviews -> Segments (Shift+F7)
   找到 .got 和 .got.plt 段
   ```

2. **记录 GOT 段地址**
   - .got 起始地址即为基准
   - 对比 a6700 的相对位置

#### B. 查找菜单表 (MENU_TABLE)

1. **搜索字符串**
   ```
   View -> Open subviews -> Strings (Shift+F12)
   搜索 "Custom" 或 "Menu" 或 "Setting"
   ```

2. **查看交叉引用**
   - 双击字符串跳转
   - 按 `X` 查看交叉引用 (Xrefs to)
   - 找到引用该字符串的数据结构

3. **识别菜单表结构**
   - 菜单表包含一系列结构体
   - 每个条目约 0x18-0x20 字节
   - 包含 group_id, setting_desc, non_setting_desc

4. **搜索 0x535 (PILOT_GROUP_ID)**
   ```
   Search -> Sequence of bytes (Alt+B)
   输入: 35 05 00 00
   ```
   - 这会找到自定义菜单组的可能位置

#### C. 查找页面表 (PAGE_TABLE)

1. **搜索屏幕 ID**
   ```
   Search -> Sequence of bytes
   搜索 0x1C3: C3 01 00 00
   搜索 0x293: 93 02 00 00
   搜索 0x2A7: A7 02 00 00
   搜索 0x260: 60 02 00 00
   ```

2. **识别页面表结构**
   - 页面表包含屏幕工厂函数指针
   - 每个条目约 0x58 字节
   - 结构: screen_id, view_factory, ctrl_factory, ...

#### D. 查找布局表 (LAYOUT_1C3, LAYOUT_293)

1. **从页面表反向追踪**
   - 找到 0x1C3 和 0x293 的页面条目
   - 查看 view_factory 函数
   - 追踪函数中引用的布局数据

2. **搜索布局模式**
   - 布局表包含 UI 组件描述
   - 每个条目约 0x60 字节
   - 包含坐标 (x, y, w, h) 和资源 ID

#### E. 查找工厂函数

1. **搜索函数特征**
   ```
   在 Functions 窗口 (Shift+F3) 搜索:
   - "factory"
   - "create"
   - "allocate"
   ```

2. **查看函数引用**
   - 找到 view/ctrl factory 函数
   - 记录函数地址

#### F. 查找 TextSprite::setString

1. **搜索符号**
   ```
   Jump -> Jump to name (Ctrl+L)
   搜索: TextSprite
   或: setString
   ```

2. **查看虚函数表**
   - TextSprite 类有虚函数表
   - setString 是其中一个虚函数

## 记录偏移量

创建一个文本文件记录找到的偏移：

```c
/* A7C2 appFw.so 偏移量 - 通过 IDA Pro 确认 */

// GOT 表偏移
#define APPFW_GOT_MENU_TABLE      0x____________ull
#define APPFW_GOT_PAGE_TABLE      0x____________ull
#define APPFW_GOT_LAYOUT_1C3      0x____________ull
#define APPFW_GOT_LAYOUT_293      0x____________ull
#define APPFW_GOT_CTRL_ITEMS_1C3  0x____________ull
#define APPFW_GOT_SET_STRING_RID  0x____________ull
#define APPFW_GOT_SET_STRING_CSTR 0x____________ull

// 数据表偏移
#define APPFW_MENU_TABLE_OFF      0x____________ull
#define APPFW_MENU_TABLE_SIZE     0x____________ull
#define APPFW_CUSTOM_GROUP_OFF    0x____________ull
#define APPFW_CUSTOM_SLOT12_OFF   0x____________ull

#define APPFW_PAGE_TABLE_OFF      0x____________ull
#define APPFW_PAGE_ENTRY_SIZE     0x58ull  // 通常不变
#define APPFW_PAGE_STOCK_COUNT    0x____________ull

#define APPFW_LAYOUT_1C3_OFF      0x____________ull
#define APPFW_LAYOUT_1C3_ITEMS    13u  // 通常不变
#define APPFW_LAYOUT_293_OFF      0x____________ull
#define APPFW_LAYOUT_293_ITEMS    12u  // 通常不变

#define APPFW_CTRL_ITEMS_1C3_OFF  0x____________ull

// 工厂函数偏移
#define APPFW_VIEW_FACTORY_1C3_OFF  0x____________ull
#define APPFW_CTRL_FACTORY_1C3_OFF  0x____________ull
#define APPFW_VIEW_FACTORY_293_OFF  0x____________ull
#define APPFW_CTRL_FACTORY_293_OFF  0x____________ull

// 其他函数偏移
#define APPFW_TREE_INSERT_OFF           0x____________ull
#define APPFW_VISIBILITY_OK_OFF         0x____________ull
#define APPFW_GET_CUR_GROUP_OFF         0x____________ull
#define APPFW_GET_SEL_ROW_OFF           0x____________ull
#define APPFW_GET_CUR_ITEM_OFF          0x____________ull
#define APPFW_MENU_ITEM_IS_NAVIGABLE_OFF 0x____________ull
#define APPFW_MENU_ITEM_OPEN_OFF        0x____________ull
#define APPFW_293_GET_FOCUSED_ITEM_OFF  0x____________ull
#define APPFW_293_GENERIC_OPEN_OFF      0x____________ull
#define APPFW_CURSOR_NEXT_OFF           0x____________ull
#define APPFW_CURSOR_PREV_OFF           0x____________ull
```

## 验证偏移量

对于每个找到的偏移：

1. **检查数据合理性**
   - 地址是否在合理范围内
   - 数据结构是否看起来正确
   - 指针是否指向有效地址

2. **对比 a6700**
   - 结构是否相似
   - 相对位置是否合理
   - 大小是否匹配

3. **交叉验证**
   - 通过多种方法确认同一偏移
   - 检查相关数据的一致性

## 高级技巧

### 技巧 1: 使用结构体定义

在 IDA 中定义结构体可以更好地理解数据：

```
Structures 窗口 (Shift+F9)
Insert -> 添加新结构
```

例如菜单条目：
```c
struct menu_row_entry {
    uint64_t group_id;
    uint64_t setting_desc;
    uint64_t non_setting_desc;
};
```

### 技巧 2: 使用交叉引用图

```
右键 -> Xrefs graph to
可视化引用关系
```

### 技巧 3: 对比二进制差异

如果有 a6700 的 IDA 数据库：
```
使用 BinDiff 插件对比两个版本
找到对应的函数和数据
```

### 技巧 4: 使用脚本批量查找

修改 extract_offsets_ida.py 脚本：
- 添加更多搜索模式
- 自动分析数据结构
- 生成报告

## 常见问题

**Q: 找不到某个偏移怎么办？**
A: 尝试：
- 搜索相关字符串
- 查看 a6700 对应位置的特征
- 使用交叉引用追踪

**Q: 如何确认偏移正确？**
A: 
- 检查数据结构的合理性
- 验证指针指向有效地址
- 对比多个相关偏移的一致性

**Q: 偏移量是虚拟地址还是文件偏移？**
A: 代码中使用的是虚拟地址（运行时地址）

## 下一步

找到所有偏移后：
1. 更新 pilot_menu_ui.c 中的定义
2. 运行 ./verify_offsets.sh 检查
3. 编译测试
4. 在相机上验证

---

**重要提示**: 
- 仔细验证每个偏移
- 记录查找过程
- 保持 IDA 数据库备份
- 错误的偏移会导致相机崩溃！
