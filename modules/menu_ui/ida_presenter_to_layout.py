"""
IDA: Deep analysis of presenter construction to find LAYOUT references
"""
import idaapi, ida_bytes, ida_funcs, idautils, idc

PRESENTER_1C3 = 0x01DB0540
PRESENTER_293 = 0x01D54644

# Potential LAYOUT areas
LAYOUT_CANDIDATES = [
    0x051B82A0,  # create_screen dispatch
    0x05215DA0,  # Large array area
    0x05216AA0,
]

def trace_presenter_deep(ea, label, depth=5):
    """Deeply trace presenter to find all data array references"""
    print("\n" + "="*80)
    print(f"DEEP PRESENTER TRACE: 0x{ea:x} - {label}")
    print("="*80)
    
    seen = set()
    queue = [(ea, 0)]
    all_data_refs = {}
    
    while queue:
        current, d = queue.pop(0)
        if current in seen or d > depth:
            continue
        seen.add(current)
        
        func = ida_funcs.get_func(current)
        if not func:
            continue
        
        # Collect all data references in this function
        inst_ea = func.start_ea
        while inst_ea < func.end_ea:
            for ref in idautils.DataRefsFrom(inst_ea):
                # Focus on .data.rel.ro range
                if 0x051B82A0 <= ref < 0x05B4BB60:
                    val = ida_bytes.get_qword(ref)
                    
                    # Check if this points to a large array
                    if 0x051B82A0 <= val < 0x05300000:
                        # Estimate array size
                        array_size = 0
                        test_ea = val
                        while test_ea < val + 0x1000:
                            test_val = ida_bytes.get_qword(test_ea)
                            if 0x01660680 <= test_val < 0x037041A0:
                                array_size += 1
                                test_ea += 8
                            else:
                                break
                        
                        if array_size >= 10:
                            if val not in all_data_refs:
                                all_data_refs[val] = []
                            all_data_refs[val].append({
                                'got_or_ref': ref,
                                'func': func.start_ea,
                                'depth': d,
                                'array_size': array_size,
                                'inst': inst_ea
                            })
            
            # Queue callees
            if d < depth:
                for call in idautils.CodeRefsFrom(inst_ea, False):
                    if call != inst_ea + 4:
                        queue.append((call, d + 1))
            
            inst_ea = idc.next_head(inst_ea, func.end_ea)
    
    # Report findings
    print(f"\nFound {len(all_data_refs)} large array references:")
    for array_va in sorted(all_data_refs.keys()):
        refs = all_data_refs[array_va]
        first = refs[0]
        
        array_name = idc.get_name(array_va) or f"array_{array_va:X}"
        got_name = idc.get_name(first['got_or_ref']) or f"ref_{first['got_or_ref']:X}"
        
        print(f"\n  Array: 0x{array_va:x} {array_name}")
        print(f"    Size: ~{first['array_size']} entries ({first['array_size']*8} bytes)")
        print(f"    GOT/Ref: 0x{first['got_or_ref']:x} {got_name}")
        print(f"    Referenced {len(refs)} time(s):")
        
        for ref in refs[:3]:
            func_name = idc.get_func_name(ref['func']) or f"sub_{ref['func']:X}"
            print(f"      @ 0x{ref['inst']:x} in {func_name} (depth {ref['depth']})")

def main():
    print("A7C2 PRESENTER -> LAYOUT TRACE")
    
    # Trace both presenters
    trace_presenter_deep(PRESENTER_1C3, "PRESENTER_1C3", depth=5)
    trace_presenter_deep(PRESENTER_293, "PRESENTER_293", depth=5)
    
    print("\n" + "="*80)
    print("SUMMARY")
    print("="*80)
    print("\nLikely LAYOUT arrays are those with:")
    print("  - 20-100+ function pointer entries")
    print("  - Referenced by presenter construction chain")
    print("  - Have corresponding GOT entries")

if __name__ == "__main__":
    main()
