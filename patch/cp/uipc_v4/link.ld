osal_msg_size    = 0x00BCFE2F;
orig_nonzero_cmd = 0x004A7E57;
orig_zero_cmd    = 0x004A7E5F;
orig_free_msg    = 0x004A7E79;
SECTIONS
{
  .patch 0x00c12d68 : {
    *(.hook)
    . = ALIGN(4);
    *(.text*)
  }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.ARM.attributes) }
}
