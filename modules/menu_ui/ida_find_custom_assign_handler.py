"""
IDA: Find Custom Assign menu item in ctrl_items array
"""
import idaapi, ida_bytes, ida_funcs, idautils, idc

CTRL_ITEMS_BASE = 0x051C1230
CTRL_ITEMS_COUNT = 187

def analyze_handler_function(handler_addr):
    """Analyze what a handler function does"""
    func = ida_funcs.get_func(handler_addr)
    if not func:
        return "Not a function"
    
    # Look for string references
    strings = []
    ea = func.start_ea
    while ea < func.end_ea:
        for ref in idautils.DataRefsFrom(ea):
            # Check if it's a string
            str_val = idc.get_strlit_contents(ref)
            if str_val:
                strings.append(str_val.decode('utf-8', errors='ignore'))
        ea = idc.next_head(ea, func.end_ea)
    
    return strings

def main():
    print("="*80)
    print("SEARCHING FOR CUSTOM ASSIGN MENU HANDLER")
    print("="*80)
    
    # Scan all ctrl_items entries
    for i in range(CTRL_ITEMS_COUNT):
        entry_addr = CTRL_ITEMS_BASE + i * 8
        handler_ptr = ida_bytes.get_qword(entry_addr)
        
        if not (0x01660680 <= handler_ptr < 0x037041A0):
            continue
        
        handler_name = idc.get_func_name(handler_ptr) or f"sub_{handler_ptr:X}"
        
        # Look for handlers that might be related to Custom Assign
        # Check xrefs from the handler
        func = ida_funcs.get_func(handler_ptr)
        if not func:
            continue
        
        # Check for interesting patterns
        has_custom = False
        has_assign = False
        
        ea = func.start_ea
        while ea < func.end_ea:
            for ref in idautils.DataRefsFrom(ea):
                str_val = idc.get_strlit_contents(ref)
                if str_val:
                    s = str_val.decode('utf-8', errors='ignore').lower()
                    if 'custom' in s:
                        has_custom = True
                    if 'assign' in s:
                        has_assign = True
            ea = idc.next_head(ea, func.end_ea)
        
        if has_custom or has_assign:
            print(f"\n[{i:3d}] 0x{entry_addr:x} -> 0x{handler_ptr:x} {handler_name}")
            print(f"      Flags: custom={has_custom}, assign={has_assign}")
    
    # Also check for common menu patterns
    print("\n" + "="*80)
    print("ANALYZING HANDLER PATTERNS")
    print("="*80)
    
    # Sample some handlers to understand the pattern
    for i in [0, 10, 50, 100, 150]:
        if i >= CTRL_ITEMS_COUNT:
            continue
        
        entry_addr = CTRL_ITEMS_BASE + i * 8
        handler_ptr = ida_bytes.get_qword(entry_addr)
        
        if not (0x01660680 <= handler_ptr < 0x037041A0):
            continue
        
        handler_name = idc.get_func_name(handler_ptr) or f"sub_{handler_ptr:X}"
        
        print(f"\n[{i:3d}] 0x{handler_ptr:x} {handler_name}")
        
        # Show function size and complexity
        func = ida_funcs.get_func(handler_ptr)
        if func:
            size = func.end_ea - func.start_ea
            print(f"      Size: 0x{size:x} bytes")
            
            # Count calls
            calls = 0
            ea = func.start_ea
            while ea < func.end_ea:
                if idc.print_insn_mnem(ea) in ['BL', 'BLR']:
                    calls += 1
                ea = idc.next_head(ea, func.end_ea)
            print(f"      Calls: {calls}")

if __name__ == "__main__":
    main()
