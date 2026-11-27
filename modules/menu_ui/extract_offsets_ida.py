"""
IDA Pro Python 脚本 - 提取 A7C2 appFw.so 偏移量

在 IDA Pro 中加载 appFw.so.i64 后，运行此脚本来提取关键偏移。

使用方法:
1. 在 IDA Pro 中打开 appFw.so.i64
2. File -> Script file... 
3. 选择此脚本运行

或者在 IDA 命令行中:
Alt+F7, 然后粘贴脚本内容
"""

import idaapi
import idautils
import idc

def find_string_xrefs(search_str):
    """查找字符串的交叉引用"""
    results = []
    for s in idautils.Strings():
        if search_str.lower() in str(s).lower():
            addr = s.ea
            # 获取交叉引用
            for xref in idautils.XrefsTo(addr):
                results.append({
                    'string': str(s),
                    'string_addr': addr,
                    'xref_addr': xref.frm,
                    'xref_type': xref.type
                })
    return results

def find_got_entry(symbol_name):
    """查找 GOT 表项"""
    # 搜索符号
    ea = idc.get_name_ea_simple(symbol_name)
    if ea != idaapi.BADADDR:
        return ea
    
    # 尝试在 .got.plt 段搜索
    got_seg = idaapi.get_segm_by_name(".got.plt")
    if got_seg:
        for ea in range(got_seg.start_ea, got_seg.end_ea, 8):
            name = idc.get_name(ea)
            if symbol_name in name:
                return ea
    
    return None

def search_pattern(pattern_bytes):
    """搜索字节模式"""
    results = []
    ea = idaapi.get_imagebase()
    while ea != idaapi.BADADDR:
        ea = idc.find_binary(ea, idc.SEARCH_DOWN, pattern_bytes)
        if ea != idaapi.BADADDR:
            results.append(ea)
            ea = idc.next_head(ea)
    return results

def find_menu_table():
    """查找菜单表结构"""
    print("\n=== 查找菜单表 ===")
    
    # 搜索 "menu" 相关字符串
    menu_strings = find_string_xrefs("menu")
    print(f"找到 {len(menu_strings)} 个 'menu' 字符串引用")
    
    # 搜索 0x535 (PILOT_GROUP_ID)
    pattern = "35 05 00 00"  # 0x535 小端序
    results = search_pattern(pattern)
    print(f"\n找到 {len(results)} 个 0x535 匹配:")
    for i, addr in enumerate(results[:10]):
        print(f"  [{i}] 0x{addr:x}")
    
    return results

def find_page_table():
    """查找页面表"""
    print("\n=== 查找页面表 ===")
    
    # 搜索 "page" 或 "screen" 相关
    page_strings = find_string_xrefs("page")
    screen_strings = find_string_xrefs("screen")
    
    print(f"找到 {len(page_strings)} 个 'page' 引用")
    print(f"找到 {len(screen_strings)} 个 'screen' 引用")
    
    return page_strings + screen_strings

def find_layout_tables():
    """查找布局表"""
    print("\n=== 查找布局表 ===")
    
    # 搜索 0x1C3 和 0x293 (屏幕ID)
    pattern_1c3 = "C3 01 00 00"
    pattern_293 = "93 02 00 00"
    
    results_1c3 = search_pattern(pattern_1c3)
    results_293 = search_pattern(pattern_293)
    
    print(f"找到 {len(results_1c3)} 个 0x1C3 匹配")
    print(f"找到 {len(results_293)} 个 0x293 匹配")
    
    return {
        '0x1C3': results_1c3[:10],
        '0x293': results_293[:10]
    }

def find_got_entries():
    """查找关键的 GOT 表项"""
    print("\n=== 查找 GOT 表项 ===")
    
    # 获取 .got 和 .got.plt 段
    got_seg = idaapi.get_segm_by_name(".got")
    got_plt_seg = idaapi.get_segm_by_name(".got.plt")
    
    if got_seg:
        print(f".got 段: 0x{got_seg.start_ea:x} - 0x{got_seg.end_ea:x}")
    if got_plt_seg:
        print(f".got.plt 段: 0x{got_plt_seg.start_ea:x} - 0x{got_plt_seg.end_ea:x}")
    
    # 尝试找一些关键符号
    symbols = [
        "TextSprite",
        "setString",
        "menu",
        "page",
        "screen",
        "layout"
    ]
    
    found = {}
    for sym in symbols:
        # 在符号表中搜索
        for ea in idautils.Names():
            name = idc.get_name(ea[0])
            if sym.lower() in name.lower():
                found[name] = ea[0]
                print(f"  {name}: 0x{ea[0]:x}")
                if len(found) > 20:  # 限制输出
                    break
        if len(found) > 20:
            break
    
    return found

def analyze_data_structure(addr, size=256):
    """分析指定地址的数据结构"""
    print(f"\n分析地址 0x{addr:x}:")
    
    for i in range(0, min(size, 128), 8):
        ea = addr + i
        value = idc.get_qword(ea)
        # 检查是否是有效指针
        if idaapi.is_loaded(value):
            name = idc.get_name(value)
            if name:
                print(f"  +0x{i:03x}: 0x{value:016x} -> {name}")
            else:
                print(f"  +0x{i:03x}: 0x{value:016x}")
        else:
            print(f"  +0x{i:03x}: 0x{value:016x}")

def main():
    print("=" * 80)
    print("A7C2 appFw.so 偏移量提取脚本")
    print("=" * 80)
    
    # 获取基本信息
    base = idaapi.get_imagebase()
    print(f"\n镜像基址: 0x{base:x}")
    
    # 查找各个表
    menu_results = find_menu_table()
    page_results = find_page_table()
    layout_results = find_layout_tables()
    got_results = find_got_entries()
    
    # 分析一些候选地址
    print("\n=== 分析候选地址 ===")
    if menu_results:
        print("\n分析第一个 0x535 匹配附近:")
        analyze_data_structure(menu_results[0] - 64, 256)
    
    # 输出总结
    print("\n" + "=" * 80)
    print("总结")
    print("=" * 80)
    print(f"找到 {len(menu_results)} 个菜单相关匹配")
    print(f"找到 {len(layout_results.get('0x1C3', []))} 个 0x1C3 匹配")
    print(f"找到 {len(layout_results.get('0x293', []))} 个 0x293 匹配")
    print(f"找到 {len(got_results)} 个符号")
    
    print("\n请根据这些结果手动确认偏移量。")
    print("建议使用 IDA 的交叉引用功能深入分析。")

if __name__ == "__main__":
    main()
