; AC8ThrottleControl post-prologue bridge.
; The game's first 16 bytes have already made a native stack frame. Replay the
; next four complete instructions, preserve every volatile value, call the
; filter, then resume at RVA 0x4D2C05F.

option casemap:none

EXTERN ac8_throttle_apply:PROC
EXTERN ac8_throttle_mid_continue:QWORD

.code

ac8_throttle_mid_hook PROC
    mov     rdi, rdx
    movaps  xmmword ptr [rax-38h], xmm7
    mov     rbx, rcx
    movaps  xmmword ptr [rax-48h], xmm8

    pushfq
    push    rax
    push    rcx
    push    rdx
    push    r8
    push    r9
    push    r10
    push    r11
    sub     rsp, 080h
    movdqu  xmmword ptr [rsp+020h], xmm0
    movdqu  xmmword ptr [rsp+030h], xmm1
    movdqu  xmmword ptr [rsp+040h], xmm2
    movdqu  xmmword ptr [rsp+050h], xmm3
    movdqu  xmmword ptr [rsp+060h], xmm4
    movdqu  xmmword ptr [rsp+070h], xmm5
    mov     rcx, rbx
    mov     rdx, rdi
    call    ac8_throttle_apply
    movdqu  xmm0, xmmword ptr [rsp+020h]
    movdqu  xmm1, xmmword ptr [rsp+030h]
    movdqu  xmm2, xmmword ptr [rsp+040h]
    movdqu  xmm3, xmmword ptr [rsp+050h]
    movdqu  xmm4, xmmword ptr [rsp+060h]
    movdqu  xmm5, xmmword ptr [rsp+070h]
    add     rsp, 080h
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdx
    pop     rcx
    pop     rax
    popfq
    jmp     qword ptr [ac8_throttle_mid_continue]
ac8_throttle_mid_hook ENDP

PUBLIC ac8_throttle_mid_hook
END
