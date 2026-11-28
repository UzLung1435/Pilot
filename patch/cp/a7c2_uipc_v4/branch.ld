/* Only purpose: put the branch stub object at the hook site so the assembler
 * resolves the b.w target.  Not used at runtime. */
SECTIONS
{
  .hb 0x004B3B0A : { *(.hb) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.ARM.attributes) *(.text*) *(.data*) *(.bss*) *(.rodata*) }
}
