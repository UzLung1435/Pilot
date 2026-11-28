/*
 * ILCE-7CM2 2.01 (A7 C II) -- CP side patch.
 *
 * Same idea as ../uipc_v4/link.ld, but the addresses are the ones this
 * firmware actually uses:
 *
 *   hook site   VA 0x004B3B0A  (ldrb r0,[r6]; cbz r0,..; cmp r0,#5; ...)
 *   Thumb entry points into the original code: base+5 / base+13 / base+39,
 *   exactly the same deltas as the A6700 build.
 *
 * The cave at 0x00C63EA4 is a 8,320 byte run of zeroes inside the CP's
 * executable mapping.  The A6700 cave (0x00C12D68) is not free on this
 * firmware, so this image needs its own.
 *
 * osal_msg_size is *not* referenced: the host side sends one entry per frame
 * (8 B header + 6 B entry header + <= 2048 B payload), so cp_patch_apply()
 * can derive the frame length from the first entry instead of asking the
 * OSAL layer.
 */
orig_nonzero_cmd = 0x004B3B0F;
orig_zero_cmd    = 0x004B3B17;
orig_free_msg    = 0x004B3B31;

SECTIONS
{
  .patch 0x00c63ea4 : {
    *(.hook)
    . = ALIGN(4);
    *(.text*)
  }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.ARM.attributes) }
}
