/* ==========================================================================
 * AuroraOS - ATA PIO driver (primary channel)
 *
 * Polling PIO (no IRQ): stable everywhere and plenty fast for our needs.
 * Supports 28-bit LBA, up to 256 sectors per call.
 * ========================================================================== */
#include "include/aurora.h"

#define ATA_DATA      0x1F0
#define ATA_SECCOUNT  0x1F2
#define ATA_LBA0      0x1F3
#define ATA_LBA1      0x1F4
#define ATA_LBA2      0x1F5
#define ATA_DRIVE     0x1F6
#define ATA_STATUS    0x1F7
#define ATA_CMD       0x1F7
#define ATA_ALTSTATUS 0x3F6

#define SR_BSY  0x80
#define SR_DRDY 0x40
#define SR_DRQ  0x08
#define SR_ERR  0x01

#define CMD_READ   0x20
#define CMD_WRITE  0x30
#define CMD_FLUSH  0xE7
#define CMD_ID     0xEC

#define ATA_TIMEOUT_MS 15000      /* hard deadline per wait (host fsync can be slow) */
#define ATA_ITERS_MAX  200000000UL /* fallback bound if the timer is dead */

static int ata_avail;

/* Wait until BSY clears; optionally require DRQ. Returns 0 on success. */
static int ata_wait(int need_drq) {
    /* Poll until BSY clears (and DRQ if needed). FLUSH CACHE can legitimately
     * take hundreds of milliseconds on a slow backing store, so bound the
     * wait by wall-clock guest time rather than iteration count. */
    uint64_t start = timer_ms();
    for (unsigned long i = 0; i < ATA_ITERS_MAX; i++) {
        uint8_t st = inb(ATA_ALTSTATUS);
        if (!(st & SR_BSY)) {
            if (st & SR_ERR) return -1;
            if (!need_drq || (st & SR_DRQ)) return 0;
        }
        if (timer_ms() - start > ATA_TIMEOUT_MS) return -1;
    }
    return -1;
}

static void ata_delay400(void) {          /* 400ns delay after command write */
    inb(ATA_ALTSTATUS); inb(ATA_ALTSTATUS);
    inb(ATA_ALTSTATUS); inb(ATA_ALTSTATUS);
}

static void ata_setup_lba(uint32_t lba, uint8_t count, int drive) {
    outb(ATA_DRIVE, 0xE0 | (uint8_t)((lba >> 24) & 0xF) | (drive << 4));
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));
}

int ata_init(void) {
    /* look for a drive on the master slot */
    outb(ATA_DRIVE, 0xA0);
    ata_delay400();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0); outb(ATA_LBA1, 0); outb(ATA_LBA2, 0);
    outb(ATA_CMD, CMD_ID);
    ata_delay400();

    uint8_t st = inb(ATA_STATUS);
    if (st == 0) { ata_avail = 0; return -1; }   /* nothing there */
    if (ata_wait(1) != 0) { ata_avail = 0; return -1; }

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_DATA);

    uint64_t sectors = ((uint32_t)id[61] << 16) | id[60];
    kprintf("ata: master disk, %llu sectors (%llu MiB)\n",
            (unsigned long long)sectors,
            (unsigned long long)(sectors >> 11));
    ata_avail = 1;
    return 0;
}

static void ata_xfer_words(int write, void *buf, uint32_t words) {
    if (write) {
        uint16_t *p = buf;
        for (uint32_t i = 0; i < words; i++) outw(ATA_DATA, p[i]);
    } else {
        uint16_t *p = buf;
        for (uint32_t i = 0; i < words; i++) p[i] = inw(ATA_DATA);
    }
}

/* Wait until the device is ready to ACCEPT a command. Writing the command
 * register while BSY is set is illegal (ATA spec) and silently ignored by
 * emulators/real drives alike, which then times out every later transfer. */
static int ata_wait_idle(void) {
    uint64_t start = timer_ms();
    for (;;) {
        uint8_t st = inb(ATA_ALTSTATUS);
        if (!(st & (SR_BSY | SR_DRQ))) return 0;
        if (st & SR_ERR) { inb(ATA_STATUS); return -1; }  /* clear ERR */
        if (timer_ms() - start > ATA_TIMEOUT_MS) return -1;
    }
}

int ata_read(uint32_t lba, uint16_t count, void *buf) {
    if (!ata_avail || count == 0) return -1;
    uint8_t *dst = buf;
    while (count) {
        uint16_t chunk = count > 255 ? 255 : count;
        if (ata_wait_idle() != 0) return -1;
        ata_setup_lba(lba, (uint8_t)chunk, 0);
        outb(ATA_CMD, CMD_READ);
        ata_delay400();
        for (uint16_t s = 0; s < chunk; s++) {
            if (ata_wait(1) != 0) return -1;
            ata_xfer_words(0, dst, 256);
            dst += 512;
        }
        lba += chunk; count -= chunk;
    }
    return 0;
}

int ata_write(uint32_t lba, uint16_t count, const void *buf) {
    int rc = ata_write_nb(lba, count, buf);
    if (rc != 0) return rc;
    return ata_flush();
}

/* Write without the trailing FLUSH CACHE - callers batch these and call
 * ata_flush() once at a sync point. */
int ata_write_nb(uint32_t lba, uint16_t count, const void *buf) {
    if (!ata_avail || count == 0) return -1;
    const uint8_t *src = buf;
    while (count) {
        uint16_t chunk = count > 255 ? 255 : count;
        if (ata_wait_idle() != 0) return -1;
        ata_setup_lba(lba, (uint8_t)chunk, 0);
        outb(ATA_CMD, CMD_WRITE);
        ata_delay400();
        for (uint16_t s = 0; s < chunk; s++) {
            if (ata_wait(1) != 0) return -1;
            ata_xfer_words(1, (void *)src, 256);
            src += 512;
        }
        if (ata_wait(0) != 0) return -1;      /* wait command completion */
        lba += chunk; count -= chunk;
    }
    return 0;
}

/* FLUSH CACHE: commit the write cache to the medium. Can be slow on real
 * hardware and on emulated drives backed by fdatasync. */
int ata_flush(void) {
    if (!ata_avail) return -1;
    if (ata_wait_idle() != 0) return -1;
    outb(ATA_CMD, CMD_FLUSH);
    ata_delay400();
    return ata_wait(0);
}

int ata_present(void) { return ata_avail; }
