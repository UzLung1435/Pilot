/*
 * ILCE-7CM2 2.01 (A7 C II) CP receiver hook.
 *
 * Entered by a 4 byte b.w that replaces the original
 *   ldrb r0, [r6] / cbz r0, <zero command>
 * at 0x004B3B0A.  r6 is the incoming OSAL message (its first byte is the
 * command / uipc payload), r5 is the caller's context.
 *
 * Differences from the A6700 hook:
 *   - no osal_msg_size(): the frame is a single entry, so r1 = 0 tells
 *     cp_patch_apply() to derive the length from the entry header.
 *   - the "not for us" targets are this firmware's own addresses.
 */
.syntax unified
.cpu cortex-a9
.thumb
.section .hook,"ax",%progbits
.align 2
.global cp_receiver_hook
.type cp_receiver_hook,%function
.thumb_func
cp_receiver_hook:
    ldrb    r0, [r6]
    cmp     r0, #0x7e
    beq     .Lpatch
    cbz     r0, .Lzero_command
    b.w     orig_nonzero_cmd
.Lzero_command:
    b.w     orig_zero_cmd
.Lpatch:
    mov     r0, r6
    movs    r1, #0
    bl      cp_patch_apply
    b.w     orig_free_msg
.size cp_receiver_hook, .-cp_receiver_hook
