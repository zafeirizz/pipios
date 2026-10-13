; =========================================================================
; AuroraOS MBR boot sector - loaded by BIOS at 0x7C00 from LBA 0
; Loads stage2 from LBA 1 to 0x8000 and jumps to it.
; The stage2 sector count lives in a patchable header (magic 'MBH').
; =========================================================================
BITS 16
ORG 0x7C00

start:
    jmp short begin           ; 2 bytes
    nop                       ; 1 byte
mbh_magic:      db 'MBH'      ; offset 3
stage2_sectors: dd 0          ; offset 6  (patched by tools/mkimage.py)

begin:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    ; --- init serial COM1 so early failures are visible ---
    mov dx, 0x3F9
    xor al, al
    out dx, al                ; disable interrupts
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al                ; DLAB on
    mov dx, 0x3F8
    mov al, 0x01
    out dx, al                ; divisor low  (115200)
    mov dx, 0x3F9
    xor al, al
    out dx, al                ; divisor high
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al                ; 8N1
    mov dx, 0x3FC
    mov al, 0x00
    out dx, al                ; no flow control
    mov si, msg_mbr
    call serial_puts

    ; --- read stage2: LBA 1, [stage2_sectors] sectors -> 0000:8000 ---
    mov eax, [stage2_sectors]
    mov [dap_count], ax
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_fail

    mov si, msg_ok
    call serial_puts
    jmp 0x0000:0x8000

disk_fail:
    mov si, msg_fail
    call serial_puts
.hang:
    hlt
    jmp .hang

; serial_puts: DS:SI zero-terminated
serial_puts:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    call serial_putc
    jmp .loop
.done:
    popa
    ret

serial_putc:
    push dx
    push ax
    mov dx, 0x3FD
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    pop ax
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

boot_drive: db 0

align 4
dap:
    db 0x10, 0               ; size
dap_count:
    dw 0                     ; sector count (copied from stage2_sectors at runtime)
    dw 0x8000                ; offset
    dw 0x0000                ; segment
    dq 1                     ; start LBA

msg_mbr:  db 13,10,"[MBR] boot",13,10,0
msg_ok:   db "[MBR] stage2 loaded",13,10,0
msg_fail: db "[MBR] DISK READ ERROR",13,10,0

    times 0x1BE-($-$$) db 0   ; partition table area (written by mkimage.py)
    times 0x1FE-($-$$) db 0
    dw 0xAA55
