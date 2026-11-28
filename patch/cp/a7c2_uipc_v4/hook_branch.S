/* Placeholder object, only used to emit the 4 byte b.w that replaces the
 * original prologue at 0x004B3B0A.  linked at exactly that address by
 * branch.ld so the assembler computes the Thumb-2 long branch for us. */
.syntax unified
.cpu cortex-a9
.thumb
.section .hb,"ax",%progbits
.align 1
.thumb_func
branch_site:
    b.w     0x00C63EA5      /* .patch cave + 1 (Thumb entry) */
.size branch_site, .-branch_site
