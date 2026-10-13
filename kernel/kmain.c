/* =========================================================================
 * AuroraOS kernel main
 * ========================================================================= */
#include "include/aurora.h"
#include "include/gui.h"
#include "apps.h"

static boot_info_t *boot;

static void print_memory_map(void) {
    e820_entry_t *e820 = (e820_entry_t *)boot->e820_ptr;
    uint64_t usable = 0;
    kprintf("Memory map (%u entries):\n", boot->e820_count);
    for (uint32_t i = 0; i < boot->e820_count; i++) {
        kprintf("  %016llx - %016llx  type=%u\n",
                (unsigned long long)e820[i].base,
                (unsigned long long)(e820[i].base + e820[i].length),
                e820[i].type);
        if (e820[i].type == 1) usable += e820[i].length;
    }
    kprintf("  usable RAM: %llu MiB\n", (unsigned long long)(usable >> 20));
}

/* ---------------- M2 self-test: PMM + heap ---------------- */
static void test_memory(void) {
    /* --- physical allocator --- */
    uint64_t a = pmm_alloc_page();
    uint64_t b = pmm_alloc_page();
    uint64_t c = pmm_alloc_pages(16);
    if (!a || !b || !c) panic("pmm: alloc failed");
    if ((a | b | c) & 0xFFF) panic("pmm: unaligned frame");
    if (a == b || b == c) panic("pmm: duplicate frame");
    uint64_t free_before = pmm_free_pages_count();
    pmm_free_page(a);
    pmm_free_page(b);
    pmm_free_pages(c, 16);
    if (pmm_free_pages_count() != free_before + 18)
        panic("pmm: free accounting mismatch (%llu != %llu)",
              (unsigned long long)pmm_free_pages_count(),
              (unsigned long long)(free_before + 18));
    /* the 18 freed frames form one contiguous run; must be reusable */
    uint64_t d = pmm_alloc_pages(18);
    if (!d || (d & 0xFFF)) panic("pmm: contiguous reuse failed");
    pmm_free_pages(d, 18);
    if (pmm_free_pages_count() != free_before + 18)
        panic("pmm: free count drifted");

    /* single-page LIFO reuse */
    uint64_t x = pmm_alloc_page();
    pmm_free_page(x);
    uint64_t y = pmm_alloc_page();
    if (y != x) panic("pmm: single-page reuse failed");
    pmm_free_page(y);
    kprintf("[TEST] pmm: alloc/free/reuse OK\n");

    /* --- kernel heap: 1 MiB arena from the PMM --- */
    uint64_t arena = pmm_alloc_pages(256);
    if (!arena) panic("heap: arena alloc failed");
    heap_init(arena, 256 * 4096);

    void *p1 = kmalloc(100);
    void *p2 = kmalloc(200);
    void *p3 = kmalloc(300);
    if (!p1 || !p2 || !p3) panic("heap: kmalloc failed");
    memset(p1, 0xAA, 100); memset(p2, 0xBB, 200); memset(p3, 0xCC, 300);
    if (((uintptr_t)p1 & 15) || ((uintptr_t)p2 & 15) || ((uintptr_t)p3 & 15))
        panic("heap: misaligned payload");

    kfree(p2);
    void *p4 = kmalloc(150);           /* should reuse the freed gap */
    if (!p4) panic("heap: reuse failed");
    memset(p4, 0xDD, 150);
    kfree(p1); kfree(p3); kfree(p4);

    /* coalesce check: after freeing everything the arena must fit again */
    void *big = kmalloc(256 * 4096 - 4096);
    if (!big) panic("heap: coalesce failed");
    kfree(big);

    void *z = kzalloc(64);
    for (int i = 0; i < 64; i++)
        if (((uint8_t *)z)[i]) panic("heap: kzalloc not zeroed");
    kfree(z);

    void *r = kmalloc(32);
    r = krealloc(r, 4096);
    if (!r) panic("heap: krealloc failed");
    kfree(r);

    kprintf("[TEST] heap: alloc/reuse/coalesce/realloc OK\n");
    /* NOTE: the arena is NOT returned to the PMM - heap_init took ownership
     * of it. Freeing it would let a later PMM allocation alias the heap. */
    (void)arena;
}

/* ---------------- M4 self-test: ATA + FAT32 ---------------- */
static void test_fs(void) {
    /* list the root directory */
    fs_dir_t d;
    if (fs_opendir("/", &d) != 0) panic("fs: opendir / failed");
    fs_dirent_t e;
    int nfiles = 0, ndirs = 0;
    kprintf("root directory:\n");
    while (fs_readdir(&d, &e) == 0) {
        kprintf("  %c %8u  %s\n", e.is_dir ? 'd' : '-', e.size, e.name);
        if (e.is_dir) ndirs++; else nfiles++;
    }
    if (!nfiles && !ndirs) panic("fs: root directory is empty");
    kprintf("[TEST] fs: root has %d files, %d dirs\n", nfiles, ndirs);

    /* read a preloaded file */
    fs_file_t f;
    if (fs_open("/README.TXT", &f) != 0) panic("fs: open /README.TXT failed");
    char buf[128];
    int got = fs_read(&f, buf, sizeof(buf) - 1);
    if (got <= 0) panic("fs: read /README.TXT failed");
    buf[got] = 0;
    if (!strstr(buf, "Welcome to AuroraOS")) panic("fs: bad content: %s", buf);
    fs_close(&f);
    kprintf("[TEST] fs: read /README.TXT (%d bytes) OK\n", got);

    /* write / read back / unlink, crossing sector and cluster boundaries */
    uint32_t free0 = fs_free_bytes();
    if (fs_create("/TESTOUT.TXT", &f) != 0) panic("fs: create failed");
    char pat[300], pat2[300];
    for (int i = 0; i < 300; i++) pat[i]  = (char)('A' + (i % 26));
    for (int i = 0; i < 300; i++) pat2[i] = (char)('a' + (i % 26));
    if (fs_write(&f, pat, 300) != 300) panic("fs: write 1 failed");
    if (fs_write(&f, pat2, 300) != 300) panic("fs: write 2 failed");
    if (f.size != 600) panic("fs: size not updated (%u)", f.size);
    fs_close(&f);

    if (fs_open("/TESTOUT.TXT", &f) != 0) panic("fs: reopen failed");
    char back[600];
    if (fs_read(&f, back, 600) != 600) panic("fs: readback short");
    for (int i = 0; i < 600; i++) {
        char want = (i < 300) ? (char)('A' + (i % 26)) : (char)('a' + ((i - 300) % 26));
        if (back[i] != want)
            panic("fs: data mismatch at %d: got %02x want %02x",
                  i, (uint8_t)back[i], (uint8_t)want);
    }
    fs_close(&f);
    kprintf("[TEST] fs: 600-byte write/readback OK\n");

    /* subdirectories (image may be warm from a previous run, so measure the
     * cluster delta that mkdir actually causes instead of assuming one) */
    uint32_t free_before_dir = fs_free_bytes();
    if (fs_mkdir("/TESTDIR") != 0) panic("fs: mkdir failed");
    uint32_t dir_cost = free_before_dir - fs_free_bytes();
    if (fs_create("/TESTDIR/INNER.TXT", &f) != 0) panic("fs: create in subdir failed");
    if (fs_write(&f, "inner!", 6) != 6) panic("fs: write in subdir failed");
    fs_close(&f);
    if (fs_open("/TESTDIR/INNER.TXT", &f) != 0) panic("fs: reopen subdir file failed");
    char ibuf[8];
    int ig = fs_read(&f, ibuf, 6);
    ibuf[6] = 0;
    if (ig != 6 || strcmp(ibuf, "inner!") != 0) panic("fs: subdir content wrong");
    fs_close(&f);
    if (fs_unlink("/TESTDIR/INNER.TXT") != 0) panic("fs: unlink in subdir failed");
    kprintf("[TEST] fs: mkdir/create/read/unlink in subdir OK\n");

    /* unlink frees the space again */
    if (fs_unlink("/TESTOUT.TXT") != 0) panic("fs: unlink failed");
    if (fs_open("/TESTOUT.TXT", &f) == 0) panic("fs: file still readable after unlink");
    /* /TESTDIR intentionally remains and owns `dir_cost` bytes of clusters;
     * everything else the test created must have been reclaimed exactly. */
    uint32_t free1 = fs_free_bytes();
    if (free1 != free0 - dir_cost)
        panic("fs: free space mismatch (%u -> %u, expected %u, dir_cost %u)",
              free0, free1, free0 - dir_cost, dir_cost);
    kprintf("[TEST] fs: unlink restores space (%u -> %u bytes free)\n", free0, free1);
}

/* The desktop every window proc paints into. */
static compositor_t gui;

void kmain(boot_info_t *bi) {
    serial_init();
    kprintf("\n========================================\n");
    kprintf("  AuroraOS kernel 0.1 booting\n");
    kprintf("========================================\n");

    if (bi->magic != BOOT_INFO_MAGIC)
        panic("bad boot_info magic %08x", bi->magic);
    boot = bi;
    kprintf("boot: video %ux%u pitch=%u bpp=%u lfb=%016llx\n",
            boot->width, boot->height, boot->pitch, boot->bpp,
            (unsigned long long)boot->lfb);
    kprintf("boot: kernel image %u bytes\n", boot->kernel_size);
    print_memory_map();

    gdt_init();     kprintf("[OK] gdt\n");
    pic_init();     kprintf("[OK] pic\n");
    idt_init();     kprintf("[OK] idt\n");
    pit_init(100);
    irq_enable();
    kprintf("[OK] pit 100Hz + interrupts enabled\n");

    /* timer sanity test */
    sleep_ms(50);
    kprintf("[TEST] sleep 50ms -> ticks=%llu (>=5 expected)\n",
            (unsigned long long)timer_ticks());
    if (timer_ticks() < 5) panic("timer test failed");

    kprintf("AURORA-M1-PASS\n");

    /* --- M2: physical memory manager + kernel heap --- */
    pmm_init(bi);
    test_memory();
    kprintf("AURORA-M2-PASS\n");

    /* --- M3: PS/2 input devices --- */
    kbd_init();   kprintf("[OK] ps/2 keyboard\n");
    mouse_init(); kprintf("[OK] ps/2 mouse\n");
    kprintf("AURORA-M3-PASS\n");

    /* --- M4: storage stack (ATA + FAT32) --- */
    if (ata_init() != 0)  panic("ata: no disk found");
    if (fs_mount() != 0)  panic("fat32: mount failed");
    test_fs();
    kprintf("AURORA-M4-PASS\n");

    /* --- M5: GUI compositor (double-buffered windows + cursor) --- */
    if (gui_init(&gui, boot) != 0) panic("gui: init failed");
    kprintf("[OK] gui %ux%u @32bpp, offscreen buffer %p\n",
            gui.screen.width, gui.screen.height, gui.buf);

    /* --- M6: desktop shell + apps (terminal, file manager) --- */
    shell_init(&gui);
    app_terminal_open(&gui, 40, 30);
    app_filemanager_open(&gui, 548, 60, "/");   /* 548+460 = 1008 < 1024 */
    gui_window_set_focus(&gui, gui.windows);   /* topmost = Files */
    gui_compose(&gui);

    /* pixel-level self-test on the offscreen composition buffer */
    {
        uint32_t pitch = gui.screen.pitch;
        uint32_t sw = gui.screen.width, sh = gui.screen.height;
        #define PIX(x,y) (((uint32_t *)((uint8_t *)gui.buf + (y)*pitch))[(x)])
        if (PIX(300, 200) != COLOR_WIN)
            panic("gui: terminal body not composited (%08x)", PIX(300,200));
        if (PIX(800, 300) != COLOR_WIN)
            panic("gui: files body not composited (%08x)", PIX(800,300));
        if (PIX(500, (int)sh - 14) != rgb(0x12, 0x12, 0x1c))
            panic("gui: taskbar missing (%08x)", PIX(500,(int)sh-14));
        if (PIX(500, 600) == COLOR_WIN)
            panic("gui: window leaked into bare desktop");
        /* the topmost window (Files) must own the focus colour */
        if (PIX(700, 65) != COLOR_FOCUS)
            panic("gui: focused titlebar wrong (%08x)", PIX(700,65));
        if (PIX(100, 35) == COLOR_FOCUS)
            panic("gui: unfocused window shows focus color");
        #undef PIX
        (void)sw;
    }
    kprintf("[TEST] gui: compose windows/focus/z-order OK\n");
    kprintf("[TEST] apps: terminal + file manager + taskbar OK\n");
    kprintf("AURORA-M5-PASS\n");
    kprintf("AURORA-M6-PASS\n");

    /* desktop main loop: keyboard to focused window, mouse to compositor */
    for (;;) {
        int ch = kbd_try_getchar();
        if (ch >= 0) {
            serial_putc((char)ch);          /* keep serial echo for tests */
            if (ch == '\n' || ch == '\r') serial_putc('\n');
            gui_notify_key(&gui, ch);       /* focused app consumes it   */
            gui_compose(&gui);
        }
        (void)gui_poll_mouse(&gui);   /* composes internally on change */
        cpu_halt();
    }
}
