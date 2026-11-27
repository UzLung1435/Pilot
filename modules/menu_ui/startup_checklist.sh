#!/bin/bash
# A7C2 偏移验证 - 快速启动检查清单

clear
echo "╔═══════════════════════════════════════════════════════════════════╗"
echo "║   A7C2 Menu UI 移植 - 偏移验证启动检查清单                        ║"
echo "╚═══════════════════════════════════════════════════════════════════╝"
echo ""

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

check_pass() {
    echo -e "  ${GREEN}✓${NC} $1"
}

check_fail() {
    echo -e "  ${RED}✗${NC} $1"
}

check_warn() {
    echo -e "  ${YELLOW}⚠${NC} $1"
}

echo "═══════════════════════════════════════════════════════════════════"
echo "第一部分：环境检查"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

# 检查 IDA 数据库
if [ -f "../../../appFw.so.i64" ]; then
    check_pass "IDA 数据库存在: appFw.so.i64"
else
    check_fail "IDA 数据库不存在: appFw.so.i64"
    echo "      → 请先在 IDA Pro 中加载并保存 appFw.so"
fi

# 检查源文件
if [ -f "pilot_menu_ui.c" ]; then
    lines=$(wc -l < pilot_menu_ui.c)
    check_pass "源文件存在: pilot_menu_ui.c ($lines 行)"
else
    check_fail "源文件不存在: pilot_menu_ui.c"
fi

# 检查 IDA 脚本
ida_scripts=("ida_locate_got_entries.py" "extract_offsets_ida.py" "analyze_a7c2_appfw.py")
all_scripts_ok=true
for script in "${ida_scripts[@]}"; do
    if [ -f "$script" ]; then
        check_pass "IDA 脚本: $script"
    else
        check_fail "IDA 脚本缺失: $script"
        all_scripts_ok=false
    fi
done

# 检查文档
key_docs=("CURRENT_STATUS.md" "MANUAL_OFFSET_GUIDE.md" "quick_reference.sh")
all_docs_ok=true
for doc in "${key_docs[@]}"; do
    if [ -f "$doc" ]; then
        check_pass "关键文档: $doc"
    else
        check_fail "文档缺失: $doc"
        all_docs_ok=false
    fi
done

echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "第二部分：知识准备"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

echo "请确认你已阅读以下文档："
echo ""
echo "  [ ] CURRENT_STATUS.md        - 了解当前状态和整体流程"
echo "  [ ] MANUAL_OFFSET_GUIDE.md   - 学习偏移验证的详细步骤"
echo "  [ ] IDA_OFFSET_GUIDE.md      - 掌握 IDA 中的查找技巧"
echo "  [ ] quick_reference.sh       - 熟悉常用命令和快捷键"
echo ""

read -p "按 Enter 继续，或 Ctrl+C 退出去阅读文档..."
echo ""

echo "═══════════════════════════════════════════════════════════════════"
echo "第三部分：工作计划确认"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

echo "你即将开始的工作："
echo ""
echo "  阶段 1: 偏移验证 (5-15 小时)"
echo "    → 在 IDA 中验证 60+ 个偏移量"
echo "    → 使用 ida_locate_got_entries.py 自动搜索"
echo "    → 手动确认每个偏移的准确性"
echo "    → 记录在 offset_findings.txt"
echo ""
echo "  阶段 2: 代码更新 (1 小时)"
echo "    → 将验证的偏移更新到 pilot_menu_ui.c"
echo "    → 替换所有 0x________________ 占位符"
echo ""
echo "  阶段 3: 编译测试 (1-2 小时)"
echo "    → 运行 verify_offsets.sh"
echo "    → 编译生成 pilot_menu_ui.prx"
echo ""
echo "  阶段 4: 上机测试 (2-4 小时)"
echo "    → 在真机上加载和测试"
echo ""

read -p "按 Enter 确认你理解整个流程..."
echo ""

echo "═══════════════════════════════════════════════════════════════════"
echo "第四部分：工具准备"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

echo "请确认你有以下工具："
echo ""
echo "  [ ] IDA Pro 已安装并可正常运行"
echo "  [ ] 能够加载 appFw.so.i64 数据库"
echo "  [ ] 熟悉 IDA 的基本操作（跳转、交叉引用等）"
echo "  [ ] 有文本编辑器用于记录 (offset_findings.txt)"
echo "  [ ] 有足够的连续工作时间 (建议 2-3 小时为一个工作块)"
echo ""

read -p "按 Enter 确认工具就绪..."
echo ""

echo "═══════════════════════════════════════════════════════════════════"
echo "第五部分：快速启动指令"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

echo "立即执行的步骤："
echo ""
echo "  1. 打开 IDA Pro"
echo "     → File > Open"
echo "     → 选择: D:\Code\Sony_Cam_Compare\appFw.so.i64"
echo ""
echo "  2. 运行偏移查找脚本"
echo "     → 按 Alt+F7"
echo "     → 选择: ida_locate_got_entries.py"
echo "     → 查看 Output 窗口的结果"
echo ""
echo "  3. 开始验证第一个偏移 (GOT_MENU_TABLE)"
echo "     → 在 IDA Python 控制台运行:"
echo ""
echo "        import ida_bytes, idaapi"
echo "        # 检查第一个候选地址"
echo "        addr = 0xcf8"
echo "        for i in range(0, 0x60, 8):"
echo "            val = ida_bytes.get_qword(addr + i)"
echo "            print(f'+0x{i:02x}: 0x{val:016x}')"
echo ""
echo "  4. 寻找包含 0x535 的结构"
echo "     → 手动观察输出，定位 group_id 字段"
echo "     → 记录菜单表地址"
echo ""
echo "  5. 在 GOT 段查找指向该地址的指针"
echo ""
echo "        menu_table = 0x你找到的地址"
echo "        for got in range(0x5b4be70, 0x5bc0fe8, 8):"
echo "            if ida_bytes.get_qword(got) == menu_table:"
echo "                print(f'GOT_MENU_TABLE = 0x{got:08x}')"
echo ""
echo "  6. 记录到 offset_findings.txt"
echo "     → 记录候选地址、验证方法、确认值"
echo ""
echo "  7. 重复步骤 3-6 直到完成所有偏移"
echo ""

echo "═══════════════════════════════════════════════════════════════════"
echo "第六部分：已知信息速查"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

echo "关键常量:"
echo "  PILOT_GROUP_ID       = 0x535"
echo "  PILOT_ITEM_ID_BASE   = 0x1F00"
echo "  PILOT_LIST_SCREEN    = 0x293"
echo "  PILOT_DETAIL_SCREEN  = 0x1C3"
echo ""

echo "段地址:"
echo "  .got       = 0x05B4BE70 - 0x05BC0FE8"
echo "  .got.plt   = 0x05BC0FE8 - 0x05BC2B58"
echo "  .data.rel.ro = 0x051B82A0 - 0x05B4BB60"
echo ""

echo "候选地址 (需验证):"
echo "  0x535 匹配: 0xcf8, 0x232eba, 0x232ed2, ..."
echo "  0x293 匹配: 0xab68c2, 0xab6952, 0xab6982, ..."
echo "  0x1C3 匹配: 0xdf9f72, 0xdf9f8a, 0xdfb352, ..."
echo ""

echo "═══════════════════════════════════════════════════════════════════"
echo "第七部分：常见问题预警"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

echo "可能遇到的问题:"
echo ""
check_warn "找不到包含 0x535 的结构"
echo "      → 确保在 .data.rel.ro 段搜索"
echo "      → 尝试搜索 0x1F00 (ITEM_ID_BASE)"
echo ""
check_warn "GOT 表项指向的地址看起来不对"
echo "      → 确认地址在 .data.rel.ro 段内"
echo "      → 检查交叉引用，确保有代码使用"
echo ""
check_warn "不知道从哪个偏移开始"
echo "      → 优先级: MENU_TABLE > PAGE_TABLE > LAYOUT"
echo "      → 一次只验证一个"
echo ""
check_warn "验证过程太慢"
echo "      → 找到第一个后，其他类似的会更快"
echo "      → 记录详细，避免重复工作"
echo ""

echo "═══════════════════════════════════════════════════════════════════"
echo "第八部分：最终确认"
echo "═══════════════════════════════════════════════════════════════════"
echo ""

echo "在开始之前，请确认:"
echo ""
echo "  ✓ 我有至少 2-3 小时的连续工作时间"
echo "  ✓ 我已阅读关键文档"
echo "  ✓ 我理解整个工作流程"
echo "  ✓ IDA Pro 可以正常打开数据库"
echo "  ✓ 我准备好记录验证过程"
echo "  ✓ 我知道如果遇到问题去哪里查文档"
echo ""

echo "═══════════════════════════════════════════════════════════════════"
echo ""
echo -e "${GREEN}准备就绪！${NC}"
echo ""
echo "下一步操作："
echo "  1. 打开 IDA Pro"
echo "  2. 加载 appFw.so.i64"
echo "  3. 运行 ida_locate_got_entries.py"
echo "  4. 开始验证偏移"
echo ""
echo "需要帮助时查看："
echo "  - MANUAL_OFFSET_GUIDE.md (详细步骤)"
echo "  - quick_reference.sh (命令速查)"
echo "  - offset_findings.txt (记录模板)"
echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo ""
echo -e "${YELLOW}提示: 记得定期保存你在 offset_findings.txt 中的记录！${NC}"
echo ""

# 创建一个临时的快速参考卡片
cat > quickstart_card.txt << 'EOF'
╔═══════════════════════════════════════════════════════════════════╗
║                  A7C2 偏移验证快速参考卡                           ║
╠═══════════════════════════════════════════════════════════════════╣
║ IDA 快捷键                                                         ║
║   G          - 跳转到地址                                          ║
║   X          - 查看交叉引用                                        ║
║   D          - 转换为数据                                          ║
║   Alt+B      - 二进制搜索                                          ║
║   Alt+F7     - 运行 Python 脚本                                    ║
╠═══════════════════════════════════════════════════════════════════╣
║ 关键常量                                                           ║
║   PILOT_GROUP_ID       = 0x535                                    ║
║   PILOT_ITEM_ID_BASE   = 0x1F00                                   ║
║   PILOT_LIST_SCREEN    = 0x293                                    ║
║   PILOT_DETAIL_SCREEN  = 0x1C3                                    ║
╠═══════════════════════════════════════════════════════════════════╣
║ 段地址                                                             ║
║   .got         = 0x05B4BE70 - 0x05BC0FE8                          ║
║   .data.rel.ro = 0x051B82A0 - 0x05B4BB60                          ║
╠═══════════════════════════════════════════════════════════════════╣
║ 验证步骤                                                           ║
║   1. 搜索候选地址 (ida_locate_got_entries.py)                     ║
║   2. 分析数据结构                                                  ║
║   3. 查找 GOT 表项                                                 ║
║   4. 验证交叉引用                                                  ║
║   5. 记录到 offset_findings.txt                                   ║
╠═══════════════════════════════════════════════════════════════════╣
║ 优先级                                                             ║
║   高: MENU_TABLE, PAGE_TABLE, LAYOUT_1C3, LAYOUT_293             ║
║   中: CTRL_ITEMS, CUSTOM_GROUP, FACTORY 函数                      ║
║   低: 按钮 GOT, 辅助函数                                           ║
╚═══════════════════════════════════════════════════════════════════╝
EOF

echo "✅ 已生成快速参考卡片: quickstart_card.txt"
echo "   (随时可以 cat quickstart_card.txt 查看)"
echo ""
