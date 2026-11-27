# A7C2 Menu UI 移植 - 完整文档索引

## 🗂️ 文档导航

### 📖 新手入门
1. **[CURRENT_STATUS.md](CURRENT_STATUS.md)** ⭐ **从这里开始！**
   - 当前进度总览
   - 工作流程说明
   - 下一步行动指南

2. **[QUICKSTART.md](QUICKSTART.md)**
   - 5 分钟快速开始
   - 环境设置
   - 基本命令

3. **[README_A7C2.md](README_A7C2.md)**
   - 项目背景
   - 技术细节
   - 完整说明

### 🔧 IDA 分析指南
4. **[IDA_QUICKSTART.md](IDA_QUICKSTART.md)**
   - IDA Pro 入门
   - 基本操作
   - 常用命令

5. **[IDA_OFFSET_GUIDE.md](IDA_OFFSET_GUIDE.md)**
   - 偏移量查找方法
   - 验证技巧
   - 示例演示

6. **[MANUAL_OFFSET_GUIDE.md](MANUAL_OFFSET_GUIDE.md)** ⭐ **详细步骤**
   - 手动定位指南
   - 分步教程
   - 实用技巧

7. **[quick_reference.sh](quick_reference.sh)** ⭐ **快速参考**
   - 命令速查
   - 快捷键列表
   - 常用代码片段

### 🛠️ 工具脚本
8. **[ida_locate_got_entries.py](ida_locate_got_entries.py)** ⭐ **主要工具**
   - 自动化搜索
   - 候选地址生成
   - 结构分析

9. **[extract_offsets_ida.py](extract_offsets_ida.py)**
   - 偏移量提取
   - 已运行，输出见上

10. **[analyze_a7c2_appfw.py](analyze_a7c2_appfw.py)**
    - 基础分析
    - 段信息
    - 符号表

11. **[verify_offsets.sh](verify_offsets.sh)**
    - 偏移验证工具
    - 编译前检查

### 📝 工作记录
12. **[offset_findings.txt](offset_findings.txt)** ⭐ **记录你的工作**
    - 偏移验证记录
    - 笔记模板
    - 进度追踪

13. **[TODO.a7c2.md](TODO.a7c2.md)**
    - 待办事项
    - 任务清单

14. **[CHANGELOG.md](CHANGELOG.md)**
    - 变更历史
    - 版本记录

### 📊 分析报告
15. **[ANALYSIS_PLAN.md](ANALYSIS_PLAN.md)**
    - 分析策略
    - 方法论

16. **[OFFSET_ANALYSIS_RESULTS.md](OFFSET_ANALYSIS_RESULTS.md)**
    - 分析结果
    - 数据汇总

17. **[VERIFICATION_REPORT.md](VERIFICATION_REPORT.md)**
    - 验证报告
    - 测试结果

### 📚 参考文档
18. **[PORTING_SUMMARY.md](PORTING_SUMMARY.md)**
    - 移植总结
    - 技术决策

19. **[WORK_SUMMARY.md](WORK_SUMMARY.md)**
    - 工作总结
    - 经验教训

20. **[FINAL_STATUS.md](FINAL_STATUS.md)**
    - 最终状态
    - 交付清单

---

## 🎯 根据任务选择文档

### 任务：我是新手，不知道从哪开始
→ 阅读 **[CURRENT_STATUS.md](CURRENT_STATUS.md)**
→ 然后 **[QUICKSTART.md](QUICKSTART.md)**

### 任务：我要在 IDA 中查找偏移
→ 先读 **[MANUAL_OFFSET_GUIDE.md](MANUAL_OFFSET_GUIDE.md)**
→ 运行 **[quick_reference.sh](quick_reference.sh)** 获取速查
→ 在 IDA 中运行 **[ida_locate_got_entries.py](ida_locate_got_entries.py)**
→ 记录结果到 **[offset_findings.txt](offset_findings.txt)**

### 任务：我找到了一些偏移，如何验证？
→ 参考 **[IDA_OFFSET_GUIDE.md](IDA_OFFSET_GUIDE.md)** 验证方法
→ 运行 **[verify_offsets.sh](verify_offsets.sh)** 自动检查

### 任务：我完成了偏移验证，如何编译？
→ 阅读 **[README_A7C2.md](README_A7C2.md)** 构建部分
→ 运行 `PILOT_MODEL=a7c2 ./build.sh`

### 任务：编译出错了
→ 检查 **[verify_offsets.sh](verify_offsets.sh)** 输出
→ 查看 **[TODO.a7c2.md](TODO.a7c2.md)** 已知问题

### 任务：我需要快速查命令
→ 运行 **[quick_reference.sh](quick_reference.sh)**

---

## 📂 文件类型说明

### Markdown 文档 (.md)
- 使用任何文本编辑器或 Markdown 查看器打开
- 推荐工具: VS Code, Typora, Obsidian

### Python 脚本 (.py)
- 在 IDA Pro 中运行: `Alt+F7` 或 `File > Script file`
- 或在 IDA Python 控制台中 `execfile("脚本路径")`

### Shell 脚本 (.sh)
- Windows (Git Bash): `bash script.sh`
- Linux/macOS: `./script.sh`

### 文本文件 (.txt)
- 记录和笔记文件
- 使用任何文本编辑器打开

---

## 🔄 工作流程图

```
开始
  ↓
[CURRENT_STATUS.md] 了解当前状态
  ↓
[MANUAL_OFFSET_GUIDE.md] 学习验证方法
  ↓
[quick_reference.sh] 获取快速参考
  ↓
打开 IDA Pro
  ↓
运行 [ida_locate_got_entries.py]
  ↓
获取候选地址列表
  ↓
手动验证每个偏移 ←─────┐
  ↓                     │
记录到 [offset_findings.txt] │
  ↓                     │
更多偏移? ─────────────┘
  ↓ 否
更新 pilot_menu_ui.c
  ↓
运行 [verify_offsets.sh]
  ↓
通过? ──否──→ 回到验证
  ↓ 是
PILOT_MODEL=a7c2 ./build.sh
  ↓
成功? ──否──→ 检查错误
  ↓ 是
上机测试
  ↓
完成！
```

---

## 🎓 学习路径

### 初级 (1-2 小时)
1. 读 CURRENT_STATUS.md
2. 读 QUICKSTART.md
3. 读 IDA_QUICKSTART.md
4. 运行 quick_reference.sh

### 中级 (3-5 小时)
5. 读 MANUAL_OFFSET_GUIDE.md
6. 读 IDA_OFFSET_GUIDE.md
7. 在 IDA 中运行分析脚本
8. 验证前 5 个偏移

### 高级 (完整项目)
9. 验证所有 60 个偏移
10. 更新源代码
11. 编译测试
12. 上机验证

---

## 📞 寻求帮助

### 自助排查
1. 检查 **[TODO.a7c2.md](TODO.a7c2.md)** 已知问题
2. 重读相关文档
3. 查看 **[quick_reference.sh](quick_reference.sh)** 命令

### 需要协助
提供以下信息：
- 你在做什么步骤
- 遇到什么问题
- 错误信息（完整）
- 你已尝试的解决方法

---

## ⚡ 常用快捷跳转

| 我想... | 打开这个 |
|---------|---------|
| 了解整体状态 | [CURRENT_STATUS.md](CURRENT_STATUS.md) |
| 快速开始 | [QUICKSTART.md](QUICKSTART.md) |
| 学习 IDA | [IDA_QUICKSTART.md](IDA_QUICKSTART.md) |
| 查找偏移 | [MANUAL_OFFSET_GUIDE.md](MANUAL_OFFSET_GUIDE.md) |
| 查命令 | [quick_reference.sh](quick_reference.sh) |
| 运行工具 | [ida_locate_got_entries.py](ida_locate_got_entries.py) |
| 记录进度 | [offset_findings.txt](offset_findings.txt) |
| 验证偏移 | [verify_offsets.sh](verify_offsets.sh) |
| 查看待办 | [TODO.a7c2.md](TODO.a7c2.md) |

---

## 📌 重要提醒

### ⚠️ 在开始之前
- [ ] 确认 IDA Pro 已安装并可以打开 appFw.so.i64
- [ ] 确认你有足够的时间 (至少 2-3 小时连续工作)
- [ ] 准备好记录工具 (offset_findings.txt)

### ✅ 验证清单
- [ ] 每个偏移都在正确的段内
- [ ] 每个偏移都有交叉引用验证
- [ ] 所有偏移都记录在 offset_findings.txt
- [ ] verify_offsets.sh 通过
- [ ] 编译成功

### 🎯 质量标准
- 不要使用未验证的估算值
- 不要跳过验证步骤
- 不要忘记记录验证过程
- 不要在疲劳时工作（容易出错）

---

## 📊 文档统计

- 总文档数: **20+**
- 核心指南: **6**
- 工具脚本: **4**
- 参考文档: **10+**
- 总字数: **~50,000+**
- 代码行数: **~3,000+**

---

**祝你移植顺利！ 🚀**

有任何问题，重新阅读相关文档或寻求帮助。

---

*最后更新: 2024 (当前会话)*
