; =========================================================================
; AuroraOS kernel entry - entered in 64-bit long mode from boot/stage2.asm
; RDI = pointer to boot_info (physical 0x500)
; =========================================================================
bits 64
section .text
global _start
extern kmain
extern __bss_start
extern __bss_end

_start:
    cli
    ; preserve boot_info pointer without using the (dirty) stack
    mov r8, rdi

    ; clear BSS (not present in the flat binary image)
    cld
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor eax, eax
    rep stosb

    mov rsp, stack_top
    mov rdi, r8

    ; early debug marker: "[ENTRY]" on COM1
    mov rsi, msg_entry
.pm:
    lodsb
    test al, al
    jz .done
    mov dx, 0x3FD
.w:
    in al, dx
    test al, 0x20
    jz .w
    mov dx, 0x3F8
    mov al, [rsi-1]
    out dx, al
    jmp .pm
.done:
    call kmain

section .rodata
msg_entry: db 13,10,"[ENTRY] kernel entry reached",13,10,0
section .text

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 65536                  ; 64 KiB kernel stack
stack_top:
