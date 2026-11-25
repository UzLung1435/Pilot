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
    bl      osal_msg_size
    mov     r1, r0
    mov     r0, r6
    bl      cp_patch_apply
    b.w     orig_free_msg
.size cp_receiver_hook, .-cp_receiver_hook
