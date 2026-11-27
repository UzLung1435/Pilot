# A7C2 偏移量手动定位指南

## 当前状态

你已经运行了 `extract_offsets_ida.py`，得到了初步的扫描结果。现在需要手动验证和确认关键偏移。

## 已确认的信息

```c
// 段信息（已验证）
.got        起始: 0x5b4be70  结束: 0x5bc0fe8
.got.plt    起始: 0x5bc0fe8  结束: 0x5bc2b58
.data.rel.ro 起始: 0x51b82a0 结束: 0x5b4bb60

// 常量（固定）
PILOT_GROUP_ID = 0x535
PILOT_ITEM_ID_BASE = 0x1F00
PILOT_LIST_SCREEN_ID = 0x293
PILOT_DETAIL_SCREEN_ID = 0x1C3
```

## 第一步：定位菜单表（MENU_TABLE）

### 方法 A：通过 0x535 特征码搜索

1. 在 IDA 中按 `Alt+B` 打开二进制搜索
2. 搜索模式：`35 05 00 00` (0x535 的小端序)
3. 在 `.data.rel.ro` 段中找到的候选地址：
   ```
   根据你的输出，前几个匹配在：
   0xcf8, 0x232eba, 0x232ed2, ...
   ```

4. 对每个候选地址：
   - 按 `G` 跳转到该地址
   - 按 `D` 将数据转换为 Qword 视图
   - 检查周围结构是否符合菜单表特征

### 菜单表结构特征

```c
struct MenuTable {
    void* vtable;           // 通常指向代码段
    uint32_t version;       // 通常是较小的值
    uint32_t group_id;      // = 0x535 ← 搜索目标
    uint32_t item_id_base;  // = 0x1F00
    uint32_t item_count;    // 菜单项数量
    MenuItem* items;        // 指向菜单项数组
    // ... 其他字段
};
```

### 验证步骤

在候选地址执行：

```python
# 在 IDA Python 控制台
addr = 0x你的候选地址

# 读取结构
print("Offset +0x00:", hex(ida_bytes.get_qword(addr)))
print("Offset +0x08:", hex(ida_bytes.get_qword(addr + 8)))
print("Offset +0x10:", hex(ida_bytes.get_qword(addr + 16)))
print("Offset +0x18:", hex(ida_bytes.get_qword(addr + 24)))

# 检查 group_id
dword_at_offset = ida_bytes.get_dword(addr + 某偏移)
if dword_at_offset == 0x535:
    print("找到 group_id = 0x535 at offset", hex(某偏移))
```

### 找到菜单表后，定位对应的 GOT 表项

```python
menu_table_addr = 0x你找到的菜单表地址

# 在 GOT 段搜索指向菜单表的指针
got_start = 0x5b4be70
got_end = 0x5bc0fe8

for got_addr in range(got_start, got_end, 8):
    got_value = ida_bytes.get_qword(got_addr)
    if got_value == menu_table_addr:
        print(f"GOT_MENU_TABLE = {hex(got_addr)}")
        print(f"  指向: {hex(got_value)}")
        # 检查交叉引用
        for xref in idaapi.xrefs_to(got_addr):
            print(f"  被引用自: {hex(xref.frm)}")
```

## 第二步：定位页面表（PAGE_TABLE）

### 通过屏幕 ID 搜索

1. 搜索 `0x293` (LIST_SCREEN_ID)：
   ```
   你的输出显示找到了 10 个匹配，从 0xab68c2 开始
   ```

2. 页面表通常包含：
   ```c
   struct PageTable {
       uint32_t screen_id;     // = 0x293
       uint32_t flags;
       void* layout_ptr;       // 指向布局数据
       void* controller_ptr;   // 指向控制器
       // ...
   };
   ```

3. 验证方法：
   - 检查该地址是否在页面表数组中
   - 查看是否有其他屏幕 ID (0x1C3, 0x2A7, 0x260) 在附近

## 第三步：定位布局表（LAYOUT）

### 布局表特征

```c
struct LayoutEntry {
    uint16_t x, y;          // 控件坐标
    uint16_t width, height; // 控件尺寸
    uint32_t type;          // 控件类型
    // ...
};
```

### 搜索策略

1. 从 页面表的 `layout_ptr` 字段开始
2. 或搜索典型的布局数据模式：
   ```
   坐标值通常在 0-1920 (宽度) 和 0-1080 (高度) 范围内
   ```

## 第四步：定位函数偏移

### 关键函数识别

这些函数通常有明显的字符串引用：

```python
# 搜索字符串
strings_to_search = [
    "menu",
    "insert",
    "visibility",
    "create",
]

for s in strings_to_search:
    addr = ida_search.find_text(0, 0, 0, s, ida_search.SEARCH_DOWN)
    if addr != ida_idaapi.BADADDR:
        print(f"Found '{s}' at {hex(addr)}")
        # 查看使用此字符串的函数
        for xref in idaapi.xrefs_to(addr):
            func = idaapi.get_func(xref.frm)
            if func:
                print(f"  Used by function at {hex(func.start_ea)}")
```

### 常用定位技巧

1. **通过导入表**：
   ```python
   # 查找 TextSprite 相关函数
   import_name = "_ZN2ux6gfxeng10TextSprite9setStringEPKc"
   addr = idc.get_name_ea_simple(import_name)
   print(f"Import {import_name} at {hex(addr)}")
   ```

2. **通过交叉引用链**：
   ```
   GOT 表项 -> 被函数 A 加载 -> 函数 A 被函数 B 调用 -> ...
   ```

3. **通过相对偏移**：
   - A6700 和 A7C2 的代码布局可能相似
   - 计算 A6700 中某函数相对于某标志点的偏移
   - 在 A7C2 中找到同样的标志点，应用相同偏移

## 第五步：记录和验证

### 创建偏移记录表

```markdown
| 名称 | A6700 偏移 | A7C2 偏移 | 验证方法 | 状态 |
|------|-----------|----------|---------|------|
| GOT_MENU_TABLE | 0x4ea8a68 | 0x_______ | GOT 搜索 | ⏳ |
| MENU_TABLE_OFF | 0x44b8ab0 | 0x_______ | 结构分析 | ⏳ |
| GOT_PAGE_TABLE | 0x4e48818 | 0x_______ | GOT 搜索 | ⏳ |
```

### 验证清单

对每个找到的偏移：

- [ ] 地址在正确的段内（GOT / .data.rel.ro / .text）
- [ ] 结构内容符合预期（包含正确的常量）
- [ ] 有合理的交叉引用（被代码使用）
- [ ] 相对于相邻偏移的距离合理

## 实用 IDA 命令速查

```python
# 跳转
idc.jumpto(0x地址)

# 数据类型转换
idc.create_qword(addr)   # 创建 Qword
idc.create_dword(addr)   # 创建 Dword

# 交叉引用
list(idaapi.xrefs_to(addr))    # 谁引用了这个地址
list(idaapi.xrefs_from(addr))  # 这个地址引用了谁

# 函数信息
func = idaapi.get_func(addr)
if func:
    print(hex(func.start_ea), hex(func.end_ea))

# 段信息
seg = idaapi.getseg(addr)
if seg:
    print(idc.get_segm_name(seg))

# 名称
idc.set_name(addr, "my_label")
print(idc.get_name(addr))

# 注释
idc.set_cmt(addr, "这是注释", 0)  # 普通注释
idc.set_cmt(addr, "重复注释", 1)  # 重复注释
```

## 自动化辅助工具

运行你刚才创建的脚本：

```bash
# 在 IDA 中：File > Script file > 选择：
ida_locate_got_entries.py
```

这个脚本会：
1. 自动搜索候选地址
2. 检查 GOT 表项
3. 分析交叉引用
4. 生成详细报告

## 预估值参考（仅供参考！）

基于 GOT 段偏移差异 `+0xCA3408`：

```c
// ⚠️ 这些是机械估算，必须手动验证！
#define APPFW_GOT_MENU_TABLE      0x05B4BE70  // 待验证
#define APPFW_MENU_TABLE_OFF      0x0515BEB8  // 待验证
#define APPFW_GOT_PAGE_TABLE      0x05AEBC20  // 待验证
#define APPFW_PAGE_TABLE_OFF      0x0516D920  // 待验证
```

## 下一步行动

1. ✅ 运行 `ida_locate_got_entries.py` 获取候选地址列表
2. ⏳ 对每个候选地址进行结构分析
3. ⏳ 确定正确的 GOT 表项
4. ⏳ 更新 `pilot_menu_ui.c` 中的偏移定义
5. ⏳ 运行 `verify_offsets.sh` 验证
6. ⏳ 编译测试

## 需要帮助？

如果遇到困难：

1. 截图 IDA 中的候选地址周围数据结构
2. 提供交叉引用信息
3. 说明你已经尝试的步骤
4. 我可以帮你分析具体的内存布局

---

**记住**：这是一个迭代过程，不要期望一次就找对所有偏移。从最明显的特征码开始，逐步扩展。
