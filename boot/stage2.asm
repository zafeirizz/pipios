; =========================================================================
; AuroraOS stage2 - loaded by MBR at 0x8000 (real mode)
;  1. collect E820 memory map
;  2. program a VESA VBE LFB graphics mode
;  3. enter 32-bit protected mode, load the kernel from disk via ATA PIO
;  4. build identity page tables, enter long mode, jump to the kernel
;
; Patchable header (magic 'S2H'): kernel_lba, kernel_sectors.
; =========================================================================
BITS 16
ORG 0x8000

stage2_start:
    jmp short real_start      ; 2 bytes
    nop                       ; 1 byte
s2_magic:       db 'S2H'      ; offset 3
kernel_lba:     dd 0          ; offset 6
kernel_sectors: dd 0          ; offset 10

; ---- boot_info handed to the kernel (physical address 0x500) ----
BI_MAGIC   equ 0x42534F41     ; 'AOSB'
BI_OFF     equ 0x500
BI_WIDTH   equ 0x08
BI_HEIGHT  equ 0x0C
BI_PITCH   equ 0x10
BI_BPP     equ 0x14
BI_LFB     equ 0x18
BI_E820N   equ 0x20
BI_E820P   equ 0x28
BI_KSIZE   equ 0x30

real_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    mov si, msg_s2
    call puts16

    ; ---------------------------------------------------------------
    ; E820 memory map -> table at 0x7000 (24-byte entries, max 128)
    ; ---------------------------------------------------------------
    mov di, 0x7000
    xor ebx, ebx
    xor bp, bp
.e820:
    mov eax, 0xE820
    mov edx, 0x534D4150       ; 'SMAP'
    mov ecx, 24
    mov dword [di+20], 1      ; extended attr must be set to 1
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc bp
    add di, 24
    cmp bp, 128
    jae .e820_done
    test ebx, ebx
    jnz .e820
.e820_done:
    mov [e820_count], bp

    ; ---------------------------------------------------------------
    ; VESA VBE: find + set a 32bpp LFB mode (prefer 1024x768)
    ; ---------------------------------------------------------------
    mov ax, 0x4F00
    mov di, 0x4000            ; controller info block
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail

    les si, [0x4000 + 14]      ; mode list far ptr
    mov word [best_score], 0
    mov word [best_mode], 0
.mode_loop:
    mov cx, [es:si]
    cmp cx, 0xFFFF
    je .modes_end
    push es
    push si
    push cx
    mov ax, 0x4F01
    mov di, 0x4200            ; mode info block
    int 0x10
    pop cx
    pop si
    pop es
    cmp ax, 0x004F
    jne .next_mode
    mov ax, [0x4200]          ; attributes: bit0 supported, bit7 LFB
    and ax, 0x0081
    cmp ax, 0x0081
    jne .next_mode
    cmp byte [0x4200 + 0x1B], 6   ; memory model = direct color
    jne .next_mode
    cmp byte [0x4200 + 0x19], 32  ; 32 bits per pixel
    jne .next_mode
    ; score this mode
    mov ax, 1
    mov bx, [0x4200 + 0x12]    ; width
    mov dx, [0x4200 + 0x14]    ; height
    cmp bx, 1024
    jne .sc
    cmp dx, 768
    jne .sc
    mov ax, 100
.sc:
    cmp ax, [best_score]
    jbe .next_mode
    mov [best_score], ax
    mov [best_mode], cx
    mov ax, [0x4200 + 0x10]
    mov [best_pitch], ax
    mov ax, [0x4200 + 0x12]
    mov [best_width], ax
    mov ax, [0x4200 + 0x14]
    mov [best_height], ax
    mov eax, [0x4200 + 0x28]
    mov [best_lfb], eax
    cmp word [best_score], 100
    je .modes_end             ; perfect match found, stop early
.next_mode:
    add si, 2
    jmp .mode_loop
.modes_end:
    cmp word [best_mode], 0
    je vbe_fail
    mov ax, 0x4F02
    mov bx, [best_mode]
    or bx, 0x4000             ; use LFB
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail
    mov si, msg_vesa_ok
    call puts16
    jmp vesa_done

vbe_fail:
    mov si, msg_vesa_fail
    call puts16
.hang:
    hlt
    jmp .hang

vesa_done:
    ; ---------------------------------------------------------------
    ; Fill boot_info at 0x500
    ; ---------------------------------------------------------------
    mov dword [BI_OFF + 0], BI_MAGIC
    mov dword [BI_OFF + 4], 1
    movzx eax, word [best_width]
    mov [BI_OFF + BI_WIDTH], eax
    movzx eax, word [best_height]
    mov [BI_OFF + BI_HEIGHT], eax
    movzx eax, word [best_pitch]
    mov [BI_OFF + BI_PITCH], eax
    mov dword [BI_OFF + BI_BPP], 32
    mov eax, [best_lfb]
    mov [BI_OFF + BI_LFB], eax
    mov dword [BI_OFF + BI_LFB + 4], 0
    movzx eax, word [e820_count]
    mov [BI_OFF + BI_E820N], eax
    mov dword [BI_OFF + BI_E820P], 0x7000
    mov dword [BI_OFF + BI_E820P + 4], 0
    mov eax, [kernel_sectors]
    shl eax, 9
    mov [BI_OFF + BI_KSIZE], eax

    ; ---------------------------------------------------------------
    ; Enable A20
    ; ---------------------------------------------------------------
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al

    ; ---------------------------------------------------------------
    ; Enter 32-bit protected mode
    ; ---------------------------------------------------------------
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm32

; =========================================================================
; 16-bit helpers
; =========================================================================
puts16:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    call putc16
    jmp .loop
.done:
    popa
    ret

putc16:
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

boot_drive:   db 0
e820_count:   dw 0
best_mode:    dw 0
best_score:   dw 0
best_width:   dw 0
best_height:  dw 0
best_pitch:   dw 0
best_lfb:     dd 0

msg_s2:        db "[S2] stage2 running",13,10,0
msg_pm32:      db "[S2] protected mode",13,10,0
msg_ata_ok:    db "[S2] kernel loaded",13,10,0
msg_pg_ok:     db "[S2] paging ready",13,10,0
msg_lm:        db "[S2] entering long mode",13,10,0
msg_lm64:      db "[S2] long mode OK, jumping to kernel",13,10,0
msg_vesa_ok:   db "[S2] VESA LFB mode set",13,10,0
msg_vesa_fail: db "[S2] VESA FAILED",13,10,0
msg_ata_err:   db "[S2] ATA ERROR",13,10,0
msg_long:      db "[S2] long mode -> kernel",13,10,0

; =========================================================================
; GDT
; =========================================================================
align 8
gdt64:
    dq 0x0000000000000000    ; 0x00 null
    dq 0x00CF9A000000FFFF    ; 0x08 32-bit code
    dq 0x00CF92000000FFFF    ; 0x10 32-bit data
    dq 0x00AF9A000000FFFF    ; 0x18 64-bit code
    dq 0x00CF92000000FFFF    ; 0x20 data
gdt_end:
gdt_desc:
    dw gdt_end - gdt64 - 1
    dd gdt64

; =========================================================================
; 32-bit protected mode
; =========================================================================
BITS 32
pm32:
    cld
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000

    mov esi, msg_pm32
    call puts32

    ; --- load kernel from disk with ATA PIO: LBA -> 0x100000 ---
    mov esi, [kernel_lba]
    mov ecx, [kernel_sectors]
    mov edi, 0x100000
    call ata_read
    cmp byte [ata_error], 0
    je .ata_ok
    mov esi, msg_ata_err
    call puts32
    jmp $
.ata_ok:
    mov esi, msg_ata_ok
    call puts32

    ; --- identity-map first 4 GiB with 2 MiB pages ---
    ; PML4@0x1000 PDPT@0x2000 PD0..PD3@0x3000..0x6000
    mov edi, 0x1000
    mov ecx, 0x1800          ; 6 pages
    xor eax, eax
    rep stosd

    ; each entry is 8 bytes: low dword + high dword
    mov dword [0x1000], 0x2003       ; PML4[0] -> PDPT @0x2000
    mov dword [0x1004], 0
    mov dword [0x2000], 0x3003       ; PDPT[0] -> PD0 @0x3000 (0-1GiB)
    mov dword [0x2004], 0
    mov dword [0x2008], 0x4003       ; PDPT[1] -> PD1 @0x4000 (1-2GiB)
    mov dword [0x200C], 0
    mov dword [0x2010], 0x5003       ; PDPT[2] -> PD2 @0x5000 (2-3GiB)
    mov dword [0x2014], 0
    mov dword [0x2018], 0x6003       ; PDPT[3] -> PD3 @0x6000 (3-4GiB)
    mov dword [0x201C], 0

    mov edi, 0x3000           ; first PD
    xor eax, eax              ; current 2MiB phys
.fill_pd:
    mov edx, eax
    or edx, 0x83              ; present | writable | page size
    mov [edi], edx
    mov dword [edi+4], 0
    add eax, 0x200000
    add edi, 8
    cmp edi, 0x7000
    jb .fill_pd

    mov esi, msg_pg_ok
    call puts32
    mov eax, 0x1000
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5            ; CR4.PAE
    mov cr4, eax
    mov ecx, 0xC0000080       ; IA32_EFER
    rdmsr
    or eax, 1 << 8            ; LME
    wrmsr
    mov esi, msg_lm
    call puts32
    mov eax, cr0
    or eax, 0x80000001        ; PG | PE
    mov cr0, eax
    jmp 0x18:lm64

; --- 32-bit serial output (debug) ---
puts32:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    push ax
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    pop ax
    mov dx, 0x3F8
    out dx, al
    jmp .loop
.done:
    popa
    ret

; ---------------------------------------------------------------------------
; ata_read: ESI = LBA, ECX = sector count, EDI = destination (32-bit)
; ---------------------------------------------------------------------------
ata_error: db 0

; ---------------------------------------------------------------------------
; ata_read: ESI = LBA, ECX = sector count, EDI = destination (32-bit)
; ---------------------------------------------------------------------------
ata_read:
    push ebx
    push ebp
    mov byte [ata_error], 0
.next_chunk:
    test ecx, ecx
    jz .done
    mov ebp, ecx
    cmp ebp, 256
    jbe .chunk_ok
    mov ebp, 256
.chunk_ok:
.wait_bsy:
    mov dx, 0x1F7
    in al, dx
    test al, 0x80
    jnz .wait_bsy
    mov eax, esi
    shr eax, 24
    and al, 0x0F
    or al, 0xE0
    mov dx, 0x1F6
    out dx, al
    mov eax, ebp
    cmp eax, 256
    jb .cnt
    xor eax, eax
.cnt:
    mov dx, 0x1F2
    out dx, al
    mov eax, esi
    mov dx, 0x1F3
    out dx, al
    shr eax, 8
    mov dx, 0x1F4
    out dx, al
    shr eax, 8
    mov dx, 0x1F5
    out dx, al
    mov dx, 0x1F7
    mov al, 0x20
    out dx, al
    mov ebx, ebp
.sector:
    mov dx, 0x1F7
.wait_drq:
    in al, dx
    test al, 0x80
    jnz .wait_drq
    test al, 0x21
    jnz .error
    test al, 0x08
    jz .wait_drq
    mov dx, 0x1F0
    push ecx
    mov ecx, 128
    rep insd
    pop ecx
    dec ebx
    jnz .sector
    add esi, ebp
    sub ecx, ebp
    jmp .next_chunk
.error:
    mov byte [ata_error], 1
.done:
    pop ebp
    pop ebx
    ret

; =========================================================================
; 64-bit long mode
; =========================================================================
BITS 64
puts64:
    push rax
    push rdx
    push rsi
.loop:
    lodsb
    test al, al
    jz .done
    push rax
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    pop rax
    mov dx, 0x3F8
    out dx, al
    jmp .loop
.done:
    pop rsi
    pop rdx
    pop rax
    ret

lm64:
    mov esi, msg_lm64
    call puts64
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x90000
    mov rdi, 0x500            ; boot_info pointer
    mov rax, 0x100000
    jmp rax

times (512*120)-($-$$) db 0   ; stage2 occupies 120 reserved sectors (60 KiB)
