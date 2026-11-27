# A7C2 菜单 UI 移植 - 偏移量定位阶段

## 📋 当前状态总览

### ✅ 已完成
1. **源代码迁移** - 所有 Himpq-Pilot 源文件已复制并适配
2. **构建系统** - `build.sh` 支持 a7c2 目标
3. **IDA 分析环境** - appFw.so 已加载，初步扫描完成
4. **辅助工具** - 创建了多个自动化脚本和指南

### ⏳ 当前阶段
**手动偏移量验证** - 需要在 IDA Pro 中确认 60+ 个偏移量

### 🎯 下一步目标
确定所有 `APPFW_*` 偏移量的准确值

---

## 📂 文件清单

### 核心源文件
```
pilot_menu_ui.c           - 主模块代码 (2472 行)
pilot_ui.h                - 头文件定义
pilot_lv_ui.inc           - 列表视图 UI 定义
pilot_lv_text_widget.inc  - 文本控件定义
pilot_ui_navigation.inc   - 导航逻辑
pilot_ui_option_2a7.inc   - 选项屏幕布局
pilot_ui_busy_260.inc     - 忙碌屏幕布局
```

### 配置文件
```
device.json               - 设备配置
config.txt                - 偏移配置模板
```

### 构建脚本
```
build.sh                  - 主构建脚本 (支持 a6700/a7c2)
verify_offsets.sh         - 偏移量验证工具
```

### IDA 分析脚本
```
analyze_a7c2_appfw.py     - 基础分析脚本
ida_find_offsets.py       - 偏移量查找脚本
extract_offsets_ida.py    - 偏移量提取脚本 (已运行)
ida_locate_got_entries.py - GOT 表项定位助手 (新建)
```

### 文档
```
README_A7C2.md            - A7C2 移植说明
QUICKSTART.md             - 快速开始指南
TODO.a7c2.md              - 待办事项清单
PORTING_SUMMARY.md        - 移植总结
CHANGELOG.md              - 变更日志
VERIFICATION_REPORT.md    - 验证报告
WORK_SUMMARY.md           - 工作总结
INDEX.md                  - 文档索引
IDA_QUICKSTART.md         - IDA 快速开始
IDA_OFFSET_GUIDE.md       - IDA 偏移查找指南
MANUAL_OFFSET_GUIDE.md    - 手动偏移定位指南 (新建)
ANALYSIS_PLAN.md          - 分析计划
OFFSET_ANALYSIS_RESULTS.md - 分析结果
FINAL_STATUS.md           - 最终状态
quick_reference.sh        - 快速参考 (新建)
offset_findings.txt       - 偏移记录模板 (新建)
```

---

## 🔍 IDA 分析现状

### 已确认信息
```c
// 段地址 (已验证)
.got        = 0x05B4BE70 - 0x05BC0FE8
.got.plt    = 0x05BC0FE8 - 0x05BC2B58
.data.rel.ro = 0x051B82A0 - 0x05B4BB60

// 固定常量
PILOT_GROUP_ID = 0x535
PILOT_ITEM_ID_BASE = 0x1F00
PILOT_LIST_SCREEN_ID = 0x293
PILOT_DETAIL_SCREEN_ID = 0x1C3
PILOT_OPT_SCREEN_ID = 0x2A7
PILOT_BUSY_SCREEN_ID = 0x260
```

### 初步扫描结果
- 找到 **12,353** 个 `0x535` 匹配
- 找到 **2,091** 个 `0x1C3` 匹配
- 找到 **3,335** 个 `0x293` 匹配
- 识别出 **21** 个 TextSprite 相关符号
- 大量 "custom" 相关字符串引用

### 候选地址 (需验证)
```
0x535 匹配: 0xcf8, 0x232eba, 0x232ed2, ...
0x293 匹配: 0xab68c2, 0xab6952, 0xab6982, ...
0x1C3 匹配: 0xdf9f72, 0xdf9f8a, 0xdfb352, ...
0x2A7 匹配: 0x96ffca, 0x96fffa, 0x970042, ...
0x260 匹配: 0x15efd0a, 0x15efd3a, 0x15f01d2, ...
```

---

## 🛠️ 工作流程

### Phase 1: IDA 偏移验证 ⬅️ **你在这里**

**使用工具:**
1. `ida_locate_got_entries.py` - 自动化搜索候选地址
2. `MANUAL_OFFSET_GUIDE.md` - 详细的手动验证步骤
3. `quick_reference.sh` - 快速参考命令
4. `offset_findings.txt` - 记录验证过程

**工作步骤:**
```
1. 在 IDA 中运行 ida_locate_got_entries.py
2. 对每个候选地址进行结构分析
3. 通过交叉引用确认 GOT 表项
4. 在 offset_findings.txt 记录结果
5. 重复直到所有关键偏移确认
```

**优先级:**
```
高优先级 (必须):
  ✓ GOT_MENU_TABLE / MENU_TABLE_OFF
  ✓ GOT_PAGE_TABLE / PAGE_TABLE_OFF
  ✓ GOT_LAYOUT_1C3 / LAYOUT_1C3_OFF
  ✓ GOT_LAYOUT_293 / LAYOUT_293_OFF

中优先级 (重要):
  ✓ GOT_CTRL_ITEMS_1C3 / CTRL_ITEMS_1C3_OFF
  ✓ CUSTOM_GROUP_OFF / CUSTOM_SLOT12_OFF
  ✓ VIEW_FACTORY / CTRL_FACTORY 偏移

低优先级 (可选):
  ✓ 按钮 GOT 表项
  ✓ 辅助函数偏移
```

### Phase 2: 代码更新
```bash
# 将确认的偏移更新到源代码
vim pilot_menu_ui.c

# 查找并替换所有占位符
# 搜索: 0x________________
# 替换为确认的值
```

### Phase 3: 验证
```bash
# 运行偏移验证
./verify_offsets.sh

# 预期输出:
# ✅ All A7C2 offsets defined
# ✅ All offsets look reasonable
```

### Phase 4: 编译
```bash
# 设置 sysroot
export PILOT_A7C2_SYSROOT=/path/to/a7c2/sysroot

# 编译
PILOT_MODEL=a7c2 ./build.sh

# 预期输出:
# ✅ pilot_menu_ui.prx
```

### Phase 5: 上机测试
```
1. 将 pilot_menu_ui.prx 传输到 a7c2
2. 加载模块
3. 检查菜单是否出现
4. 测试功能
5. 收集日志
```

---

## 📊 进度追踪

### 总体进度
```
代码迁移:     ████████████████████ 100%
构建系统:     ████████████████████ 100%
IDA 环境:     ████████████████████ 100%
偏移验证:     ░░░░░░░░░░░░░░░░░░░░   0%  ⬅️ 当前
代码更新:     ░░░░░░░░░░░░░░░░░░░░   0%
编译测试:     ░░░░░░░░░░░░░░░░░░░░   0%
上机验证:     ░░░░░░░░░░░░░░░░░░░░   0%
```

### 关键偏移验证状态 (0/60)
```
[ ] GOT_MENU_TABLE
[ ] MENU_TABLE_OFF
[ ] MENU_TABLE_SIZE
[ ] CUSTOM_GROUP_OFF
[ ] CUSTOM_SLOT12_OFF
[ ] GOT_PAGE_TABLE
[ ] PAGE_TABLE_OFF
[ ] GOT_LAYOUT_1C3
[ ] LAYOUT_1C3_OFF
[ ] GOT_LAYOUT_293
[ ] LAYOUT_293_OFF
[ ] GOT_CTRL_ITEMS_1C3
[ ] CTRL_ITEMS_1C3_OFF
[ ] STOCK_DESC_293_OFF
... (还有 46 个)
```

---

## 🎓 学习资源

### IDA Pro 使用
- **快捷键**: 见 `quick_reference.sh`
- **Python API**: 见 `MANUAL_OFFSET_GUIDE.md`
- **偏移查找技巧**: 见 `IDA_OFFSET_GUIDE.md`

### 偏移分析方法
1. **特征码搜索** - 搜索已知常量 (0x535, 0x1F00)
2. **结构分析** - 分析数据结构布局
3. **交叉引用追踪** - 追踪代码如何使用数据
4. **GOT 表项定位** - 在 GOT 段查找指针
5. **相对偏移计算** - 基于 a6700 对比

### 验证清单
- ✓ 地址在正确的段内
- ✓ 结构包含预期常量
- ✓ 有合理的交叉引用
- ✓ 相对位置关系合理
- ✓ 多种方法交叉验证

---

## 💡 实用提示

### 时间管理
- 每个偏移平均需要 5-15 分钟验证
- 60 个偏移 ≈ 5-15 小时工作量
- 建议分批完成，优先高优先级偏移

### 效率技巧
1. **批量处理** - 一次验证一类偏移 (如所有 GOT 表项)
2. **模式识别** - 找到第一个后，其他类似的更容易
3. **记录详细** - 在 `offset_findings.txt` 记录过程，避免重复
4. **定期保存** - 每验证几个就更新代码并备份

### 常见陷阱
- ❌ 混淆虚拟地址 (VA) 和文件偏移
- ❌ 在错误的段中搜索
- ❌ 忘记检查交叉引用
- ❌ 使用未验证的估算值

---

## 📞 获取帮助

### 自助资源
```bash
# 查看快速参考
bash quick_reference.sh

# 阅读详细指南
cat MANUAL_OFFSET_GUIDE.md

# 查看 IDA 教程
cat IDA_OFFSET_GUIDE.md
```

### 需要协助时
提供以下信息：
1. 你在验证哪个偏移
2. 候选地址和周围数据
3. 交叉引用信息
4. 你已尝试的方法
5. IDA 截图（如果可能）

---

## 🎯 下一步行动

### 立即开始
```bash
# 1. 打开 IDA Pro
# 2. 加载数据库
File > Open > appFw.so.i64

# 3. 运行辅助脚本
Alt+F7 > 选择 ida_locate_got_entries.py

# 4. 开始验证第一个偏移 (GOT_MENU_TABLE)
# 按照 MANUAL_OFFSET_GUIDE.md 的步骤
```

### 验证第一个偏移的示例
```python
# 在 IDA Python 控制台

# 1. 检查候选地址
addr = 0xcf8  # 第一个 0x535 匹配
for i in range(0, 0x60, 8):
    val = ida_bytes.get_qword(addr + i)
    print(f"+0x{i:02x}: 0x{val:016x}")

# 2. 查找包含 0x535 的位置
# 手动观察输出，找到 group_id 字段

# 3. 在 GOT 段搜索指向该地址的指针
menu_table = 0x你找到的菜单表地址
for got in range(0x5b4be70, 0x5bc0fe8, 8):
    if ida_bytes.get_qword(got) == menu_table:
        print(f"GOT_MENU_TABLE = 0x{got:08x}")
        print(f"MENU_TABLE_OFF = 0x{menu_table:08x}")

# 4. 验证交叉引用
xrefs = list(idaapi.xrefs_to(got))
print(f"GOT entry has {len(xrefs)} xrefs")

# 5. 记录到 offset_findings.txt
```

---

## 📝 总结

你现在拥有：
- ✅ 完整的源代码迁移
- ✅ 配置好的构建环境
- ✅ 加载了 appFw.so 的 IDA 数据库
- ✅ 初步扫描结果和候选地址
- ✅ 完整的工具链和文档

**剩下的工作：**
手动验证 60 个偏移量，然后编译测试。

**预估时间：**
- 偏移验证: 5-15 小时
- 代码更新: 1 小时
- 编译测试: 1-2 小时
- **总计: 1-2 天工作量**

---

**祝你好运！记得在 `offset_findings.txt` 记录你的进展！** 🚀
