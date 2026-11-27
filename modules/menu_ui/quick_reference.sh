#!/bin/bash
# 快速参考：A7C2 偏移量定位工作流

echo "======================================================================"
echo "A7C2 偏移量定位工作流速查"
echo "======================================================================"
echo ""

echo "【当前状态】"
echo "✅ appFw.so 已加载到 IDA Pro"
echo "✅ 初步扫描脚本已运行"
echo "⏳ 需要手动验证和确认偏移量"
echo ""

echo "【已知信息】"
echo "GOT 段:      0x05B4BE70 - 0x05BC0FE8"
echo "GOT.PLT 段:  0x05BC0FE8 - 0x05BC2B58"
echo "data.rel.ro: 0x051B82A0 - 0x05B4BB60"
echo ""

echo "【关键常量】"
echo "PILOT_GROUP_ID       = 0x535"
echo "PILOT_ITEM_ID_BASE   = 0x1F00"
echo "PILOT_LIST_SCREEN    = 0x293"
echo "PILOT_DETAIL_SCREEN  = 0x1C3"
echo ""

echo "======================================================================"
echo "工作流程"
echo "======================================================================"
echo ""

echo "步骤 1: 在 IDA 中运行辅助脚本"
echo "----------------------------------------------------------------------"
echo "  1. 打开 IDA Pro，确保 appFw.so.i64 已加载"
echo "  2. 按 Alt+F7 或 File > Script file"
echo "  3. 选择: ida_locate_got_entries.py"
echo "  4. 查看 Output 窗口的分析结果"
echo ""

echo "步骤 2: 定位菜单表 (MENU_TABLE)"
echo "----------------------------------------------------------------------"
echo "  方法 A - 通过特征码搜索:"
echo "    1. 按 Alt+B (二进制搜索)"
echo "    2. 搜索: 35 05 00 00 (0x535 小端序)"
echo "    3. 在 data.rel.ro 段中查找"
echo ""
echo "  方法 B - 通过脚本输出:"
echo "    根据 extract_offsets_ida.py 输出:"
echo "    - 第一个 0x535 匹配在 0xcf8"
echo "    - 其他候选: 0x232eba, 0x232ed2, ..."
echo ""
echo "  验证菜单表:"
echo "    在 IDA Python 控制台运行:"
echo "      addr = 0x你的候选地址"
echo "      for i in range(0, 0x40, 8):"
echo "          print(f'+0x{i:02x}: 0x{ida_bytes.get_qword(addr+i):016x}')"
echo ""
echo "  寻找 GOT 表项:"
echo "      got = 0x5b4be70"
echo "      while got < 0x5bc0fe8:"
echo "          if ida_bytes.get_qword(got) == addr:"
echo "              print(f'GOT_MENU_TABLE = 0x{got:x}')"
echo "          got += 8"
echo ""

echo "步骤 3: 定位页面表 (PAGE_TABLE)"
echo "----------------------------------------------------------------------"
echo "  搜索屏幕 ID:"
echo "    1. 按 Alt+B"
echo "    2. 搜索: 93 02 00 00 (0x293 小端序)"
echo "    3. 检查周围结构"
echo ""
echo "  根据输出，0x293 匹配在:"
echo "    0xab68c2, 0xab6952, 0xab6982, ..."
echo ""

echo "步骤 4: 定位布局表 (LAYOUT_1C3 / LAYOUT_293)"
echo "----------------------------------------------------------------------"
echo "  搜索屏幕 ID:"
echo "    - 0x1C3: 0xdf9f72, 0xdf9f8a, ..."
echo "    - 0x293: (同上)"
echo ""
echo "  布局表特征:"
echo "    - 包含坐标数据 (0-1920, 0-1080)"
echo "    - 包含控件尺寸"
echo "    - 通常是固定大小的结构体数组"
echo ""

echo "步骤 5: 记录偏移量"
echo "----------------------------------------------------------------------"
echo "  在笔记中记录 (或创建 offset_findings.txt):"
echo ""
echo "  GOT_MENU_TABLE    = 0x________________"
echo "  MENU_TABLE_OFF    = 0x________________"
echo "  GOT_PAGE_TABLE    = 0x________________"
echo "  PAGE_TABLE_OFF    = 0x________________"
echo "  GOT_LAYOUT_1C3    = 0x________________"
echo "  LAYOUT_1C3_OFF    = 0x________________"
echo "  GOT_LAYOUT_293    = 0x________________"
echo "  LAYOUT_293_OFF    = 0x________________"
echo "  ... (其他偏移)"
echo ""

echo "步骤 6: 更新代码"
echo "----------------------------------------------------------------------"
echo "  编辑: pilot_menu_ui.c"
echo "  找到所有 #if PILOT_MODEL_A7C2 条件编译块"
echo "  替换占位符偏移量"
echo ""

echo "步骤 7: 验证"
echo "----------------------------------------------------------------------"
echo "  ./verify_offsets.sh"
echo "  检查输出，确保没有 'NOT READY' 警告"
echo ""

echo "步骤 8: 编译测试"
echo "----------------------------------------------------------------------"
echo "  PILOT_MODEL=a7c2 ./build.sh"
echo ""

echo "======================================================================"
echo "常用 IDA 快捷键"
echo "======================================================================"
echo ""
echo "  G           - 跳转到地址"
echo "  X           - 查看交叉引用 (Xrefs)"
echo "  D           - 转换为数据"
echo "  U           - 取消定义"
echo "  Alt+Q       - 结构体窗口"
echo "  Alt+B       - 二进制搜索"
echo "  Alt+T       - 文本搜索"
echo "  Alt+F7      - 运行 Python 脚本"
echo "  N           - 重命名"
echo "  ;           - 添加注释"
echo "  Shift+F12   - 字符串窗口"
echo ""

echo "======================================================================"
echo "IDA Python 控制台常用命令"
echo "======================================================================"
echo ""
echo "  import ida_bytes, idaapi, idc"
echo ""
echo "  # 读取数据"
echo "  ida_bytes.get_qword(addr)   # 读取 8 字节"
echo "  ida_bytes.get_dword(addr)   # 读取 4 字节"
echo ""
echo "  # 跳转"
echo "  idc.jumpto(0x地址)"
echo ""
echo "  # 交叉引用"
echo "  list(idaapi.xrefs_to(addr))"
echo "  list(idaapi.xrefs_from(addr))"
echo ""
echo "  # 段信息"
echo "  seg = idaapi.getseg(addr)"
echo "  idc.get_segm_name(seg)"
echo ""
echo "  # 函数"
echo "  func = idaapi.get_func(addr)"
echo "  hex(func.start_ea)"
echo ""

echo "======================================================================"
echo "预估偏移（⚠️ 仅供参考，必须手动验证！）"
echo "======================================================================"
echo ""
echo "基于线性 GOT 偏移差 +0xCA3408:"
echo ""
echo "  APPFW_GOT_MENU_TABLE      0x05B4BE70  (需验证)"
echo "  APPFW_MENU_TABLE_OFF      0x0515BEB8  (需验证)"
echo "  APPFW_GOT_PAGE_TABLE      0x05AEBC20  (需验证)"
echo "  APPFW_PAGE_TABLE_OFF      0x0516D920  (需验证)"
echo "  APPFW_GOT_LAYOUT_1C3      0x05AEFCD0  (需验证)"
echo "  APPFW_LAYOUT_1C3_OFF      0x05C7A538  (需验证)"
echo "  APPFW_GOT_LAYOUT_293      0x05B1AC20  (需验证)"
echo "  APPFW_LAYOUT_293_OFF      0x05D2F678  (需验证)"
echo ""
echo "注意: 这些值基于简单的线性偏移计算，实际地址可能不同！"
echo ""

echo "======================================================================"
echo "疑难排查"
echo "======================================================================"
echo ""
echo "问题: 找不到包含 0x535 的菜单表"
echo "  → 检查是否在 .data.rel.ro 段搜索"
echo "  → 尝试搜索 0x1F00 (ITEM_ID_BASE)"
echo "  → 查看 extract_offsets_ida.py 的完整输出"
echo ""
echo "问题: GOT 表项指向的地址看起来不对"
echo "  → 确认地址在 .data.rel.ro 段内"
echo "  → 检查交叉引用，确保有代码使用"
echo "  → 对比 a6700 的相同结构"
echo ""
echo "问题: 偏移量太多，不知道从哪开始"
echo "  → 优先级: MENU_TABLE > PAGE_TABLE > LAYOUT > 其他"
echo "  → 一次只验证一个偏移"
echo "  → 记录每个偏移的验证过程"
echo ""

echo "======================================================================"
echo "下一步操作"
echo "======================================================================"
echo ""
echo "  1. 打开 IDA Pro"
echo "  2. 加载 appFw.so.i64"
echo "  3. 运行 ida_locate_got_entries.py"
echo "  4. 根据输出逐个验证偏移"
echo "  5. 参考 MANUAL_OFFSET_GUIDE.md 详细说明"
echo "  6. 记录你的发现"
echo ""
echo "======================================================================"
echo ""

# 检查是否有临时工作文件
if [ ! -f "offset_findings.txt" ]; then
    echo "创建偏移记录文件: offset_findings.txt"
    cat > offset_findings.txt << 'EOF'
# A7C2 偏移量查找记录
# 记录每个偏移的查找过程和验证结果

## 2024-XX-XX - 初始扫描

### GOT_MENU_TABLE
- 候选地址: 
- 验证方法: 
- 交叉引用: 
- 确认值: 0x________________
- 状态: [ ] 待验证 [ ] 已确认 [ ] 有疑问

### MENU_TABLE_OFF  
- 候选地址:
- 验证方法:
- 结构内容:
- 确认值: 0x________________
- 状态: [ ] 待验证 [ ] 已确认 [ ] 有疑问

### GOT_PAGE_TABLE
- 候选地址:
- 验证方法:
- 交叉引用:
- 确认值: 0x________________
- 状态: [ ] 待验证 [ ] 已确认 [ ] 有疑问

### PAGE_TABLE_OFF
- 候选地址:
- 验证方法:
- 结构内容:
- 确认值: 0x________________
- 状态: [ ] 待验证 [ ] 已确认 [ ] 有疑问

### GOT_LAYOUT_1C3
- 候选地址:
- 验证方法:
- 交叉引用:
- 确认值: 0x________________
- 状态: [ ] 待验证 [ ] 已确认 [ ] 有疑问

### LAYOUT_1C3_OFF
- 候选地址:
- 验证方法:
- 结构内容:
- 确认值: 0x________________
- 状态: [ ] 待验证 [ ] 已确认 [ ] 有疑问

(继续添加其他偏移...)

## 验证清单

- [ ] 所有 GOT 表项在 GOT 段内 (0x05B4BE70 - 0x05BC0FE8)
- [ ] 所有数据表在 data.rel.ro 段内 (0x051B82A0 - 0x05B4BB60)
- [ ] 每个偏移都有交叉引用验证
- [ ] 结构内容包含预期的常量 (0x535, 0x1F00, 0x293 等)
- [ ] verify_offsets.sh 通过
- [ ] 编译成功

## 笔记

(记录你的发现、问题、解决方案)

EOF
    echo "✅ 已创建 offset_findings.txt"
fi

echo "提示: 使用 offset_findings.txt 记录你的查找过程"
echo ""
