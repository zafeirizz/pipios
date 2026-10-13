; =========================================================================
; AuroraOS - interrupt service routine stubs
; =========================================================================
bits 64
section .text

extern isr_dispatch

%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

%macro IRQ_STUB 2
global isr%1
isr%1:
    push qword 0
    push qword %2
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

IRQ_STUB 32, 32    ; PIT timer
IRQ_STUB 33, 33    ; keyboard
IRQ_STUB 34, 34    ; COM2
IRQ_STUB 35, 35    ; COM1
IRQ_STUB 36, 36    ; LPT2
IRQ_STUB 37, 37    ; floppy
IRQ_STUB 38, 38    ; LPT1
IRQ_STUB 39, 39    ; RTC
IRQ_STUB 40, 40    ; redirect (irq8)
IRQ_STUB 41, 41    ; irq9
IRQ_STUB 42, 42    ; irq10
IRQ_STUB 43, 43    ; irq11
IRQ_STUB 44, 44    ; PS/2 mouse
IRQ_STUB 45, 45    ; irq13 (FPU)
IRQ_STUB 46, 46    ; IDE primary
IRQ_STUB 47, 47    ; irq15

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    call isr_dispatch
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dq isr %+ i
%assign i i+1
%endrep
