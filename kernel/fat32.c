/* ==========================================================================
 * AuroraOS - FAT32 filesystem (read/write, 8.3 + long file names)
 *
 * Mounts the first FAT partition from the MBR. Directory data is read with
 * its own buffers so it never clobbers the FAT table cache. Writes go to
 * disk immediately (write-through); the FSInfo free-space counter is kept
 * in sync best-effort.
 * ========================================================================== */
#include "include/aurora.h"

#define SECTOR_SIZE  512u
#define DIR_ENT      32u
#define ATTR_RO      0x01
#define ATTR_VOL     0x08
#define ATTR_DIR     0x10
#define ATTR_LFN     0x0F
#define FAT_EOC      0x0FFFFFF8u
#define FAT_MASK     0x0FFFFFFFu

/* ---------------- mount state ---------------- */
static struct {
    uint32_t part_lba, fatstart, fatsz, datastart, totsec;
    uint32_t secperclus, rootclus, fsinfo, nclusters;
    uint32_t free_hint;
    uint32_t free_count;            /* 0xFFFFFFFF = unknown */
    int      mounted;
} fs;

/* ---------------- FAT table cache (one sector) ---------------- */
static uint32_t fc_lba = 0xFFFFFFFF;
static uint8_t  fc_buf[SECTOR_SIZE];
static int      fc_dirty;

static void fat_flush(void) {
    if (!fc_dirty || fc_lba == 0xFFFFFFFF) return;
    uint32_t idx = fc_lba - fs.fatstart;
    for (uint32_t f = 0; f < 2; f++)
        ata_write_nb(fs.fatstart + f * fs.fatsz + idx, 1, fc_buf);
    fc_dirty = 0;
    ata_flush();                       /* one FLUSH per FAT sync, not per copy */
}

static int fat_cache(uint32_t sec_idx) {
    uint32_t lba = fs.fatstart + sec_idx;
    if (fc_lba == lba) return 0;
    fat_flush();
    if (ata_read(lba, 1, fc_buf) != 0) return -1;
    fc_lba = lba;
    return 0;
}

static uint32_t fat_get(uint32_t clus) {
    if (fat_cache((clus * 4) / SECTOR_SIZE) != 0) return FAT_MASK;
    uint32_t off = (clus * 4) % SECTOR_SIZE, v;
    memcpy(&v, fc_buf + off, 4);
    return v & FAT_MASK;
}

static void fat_set(uint32_t clus, uint32_t val) {
    if (fat_cache((clus * 4) / SECTOR_SIZE) != 0) return;
    uint32_t off = (clus * 4) % SECTOR_SIZE, old, v;
    memcpy(&old, fc_buf + off, 4);
    v = (old & 0xF0000000u) | (val & FAT_MASK);
    memcpy(fc_buf + off, &v, 4);
    fc_dirty = 1;
    fat_flush();
}

static uint32_t clus_bytes(void)  { return fs.secperclus * SECTOR_SIZE; }
static uint32_t clus_lba(uint32_t c) { return fs.datastart + (c - 2) * fs.secperclus; }

static uint32_t fat_alloc_cluster(void) {
    for (uint32_t c = fs.free_hint; c < fs.nclusters + 2; c++) {
        if (fat_get(c) == 0) {
            fat_set(c, FAT_EOC);
            uint8_t z[SECTOR_SIZE];
            memset(z, 0, sizeof(z));
            for (uint32_t s = 0; s < fs.secperclus; s++)
                if (ata_write(clus_lba(c) + s, 1, z) != 0) return 0;
            if (fs.free_count != 0xFFFFFFFF && fs.free_count) fs.free_count--;
            fs.free_hint = c + 1;
            return c;
        }
    }
    return 0;
}

static void fat_free_chain(uint32_t clus) {
    while (clus >= 2 && clus < fs.nclusters + 2) {
        uint32_t next = fat_get(clus);
        fat_set(clus, 0);
        if (fs.free_count != 0xFFFFFFFF) fs.free_count++;
        if (clus < fs.free_hint) fs.free_hint = clus;
        clus = next;
    }
}

static void fsinfo_sync(void) {
    if (!fs.fsinfo) return;
    uint8_t buf[SECTOR_SIZE];
    if (ata_read(fs.part_lba + fs.fsinfo, 1, buf) != 0) return;
    uint32_t sig; memcpy(&sig, buf + 484, 4);
    if (sig != 0x61417272u) return;
    uint32_t fc = fs.free_count, nh = fs.free_hint;
    memcpy(buf + 488, &fc, 4);
    memcpy(buf + 492, &nh, 4);
    ata_write(fs.part_lba + fs.fsinfo, 1, buf);
}

/* ---------------- cluster chain walk ---------------- */
/* index of the cluster covering byte offset `idx` in a stream */
static uint32_t chain_clus_at(uint32_t first, uint32_t idx, uint32_t *walked) {
    uint32_t want = idx / clus_bytes();
    uint32_t c = first;
    for (uint32_t i = 0; i < want; i++) {
        if (c < 2 || c >= FAT_EOC) return 0;
        c = fat_get(c);
    }
    if (walked) *walked = want;
    return (c >= 2 && c < FAT_EOC) ? c : 0;
}

/* ---------------- directory entry access ---------------- */
static int dent_read(uint32_t dir_first, uint32_t idx, uint8_t out[32]) {
    uint32_t c = chain_clus_at(dir_first, idx * DIR_ENT, 0);
    if (!c) return -1;
    uint32_t per = clus_bytes() / DIR_ENT;
    uint32_t off = (idx % per) * DIR_ENT;
    uint8_t sec[SECTOR_SIZE];
    if (ata_read(clus_lba(c) + off / SECTOR_SIZE, 1, sec) != 0) return -1;
    memcpy(out, sec + (off % SECTOR_SIZE), DIR_ENT);
    return 0;
}

static int dent_write(uint32_t dir_first, uint32_t idx, const uint8_t in[32]) {
    uint32_t c = chain_clus_at(dir_first, idx * DIR_ENT, 0);
    if (!c) return -1;
    uint32_t per = clus_bytes() / DIR_ENT;
    uint32_t off = (idx % per) * DIR_ENT;
    uint8_t sec[SECTOR_SIZE];
    uint32_t lba = clus_lba(c) + off / SECTOR_SIZE;
    if (ata_read(lba, 1, sec) != 0) return -1;
    memcpy(sec + (off % SECTOR_SIZE), in, DIR_ENT);
    if (ata_write_nb(lba, 1, sec) != 0) return -1;
    return ata_flush();
}

/* decode helpers ----------------------------------------------------- */
static void name83_to(const uint8_t e[32], char *out) {
    int n = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++) out[n++] = (char)e[i];
    if (e[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++) out[n++] = (char)e[i];
    }
    out[n] = 0;
}

static void lfn_add(char *name, const uint8_t e[32]) {
    int seq = (e[0] & 0x1F);
    if (seq < 1 || seq > 20) return;
    char *slot = name + (seq - 1) * 13;
    static const int pos[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
    for (int i = 0; i < 13; i++) {
        uint16_t ch; memcpy(&ch, e + pos[i], 2);
        slot[i] = (ch && ch < 128) ? (char)ch : (ch ? '?' : 0);
        if (!ch) break;
    }
}

static int strcasecmp_(const char *a, const char *b) {
    for (;; a++, b++) {
        int ca = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
        int cb = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (ca != cb || !ca) return ca - cb;
    }
}

/* public types come from include/aurora.h */

/* ---------------- scan primitives ---------------- */
/* Find `want` (case-insensitive, 8.3 or LFN) in directory `dir_first`.
 * Fills `out` + short-entry index. Returns 0 on hit, -1 on miss. */
static int dir_find(uint32_t dir_first, const char *want, fs_dirent_t *out, uint32_t *out_idx) {
    char lfn[64];
    lfn[0] = 0;
    uint32_t total = (dir_first == fs.rootclus) ? 0x10000 : 0x10000; /* safety cap */
    for (uint32_t idx = 0; idx < total; idx++) {
        uint8_t e[32];
        if (dent_read(dir_first, idx, e) != 0) return -1;
        if (e[0] == 0x00) return -1;                /* end of directory */
        if (e[0] == 0xE5) { lfn[0] = 0; continue; } /* deleted */
        if ((e[11] & ATTR_LFN) == ATTR_LFN) {
            if (e[0] & 0x40) lfn[0] = 0;
            lfn_add(lfn, e);
            continue;
        }
        if (e[11] & ATTR_VOL) { lfn[0] = 0; continue; }

        char s83[16];
        name83_to(e, s83);
        const char *disp = lfn[0] ? lfn : s83;
        int hit = (strcasecmp_(disp, want) == 0) || (strcasecmp_(s83, want) == 0);
        if (hit) {
            uint16_t lo, hi; memcpy(&lo, e + 26, 2); memcpy(&hi, e + 20, 2);
            out->first = ((uint32_t)hi << 16) | lo;
            memcpy(&out->size, e + 28, 4);
            out->is_dir = (e[11] & ATTR_DIR) != 0;
            memcpy(out->name, disp, sizeof(out->name));
            out->name[sizeof(out->name) - 1] = 0;
            if (out_idx) *out_idx = idx;
            return 0;
        }
        lfn[0] = 0;
    }
    return -1;
}

/* next entry for readdir: skips deleted/vol/LFN, resolves display names */
static int dir_next(uint32_t dir_first, uint32_t *idx_io, fs_dirent_t *out) {
    char lfn[64];
    lfn[0] = 0;
    for (uint32_t idx = *idx_io; idx < 0x10000; idx++) {
        uint8_t e[32];
        if (dent_read(dir_first, idx, e) != 0) return -1;
        if (e[0] == 0x00) { *idx_io = idx; return -1; }
        if (e[0] == 0xE5) { lfn[0] = 0; continue; }
        if ((e[11] & ATTR_LFN) == ATTR_LFN) {
            if (e[0] & 0x40) lfn[0] = 0;
            lfn_add(lfn, e);
            continue;
        }
        if (e[11] & ATTR_VOL) { lfn[0] = 0; continue; }
        char s83[16];
        name83_to(e, s83);
        uint16_t lo, hi; memcpy(&lo, e + 26, 2); memcpy(&hi, e + 20, 2);
        out->first = ((uint32_t)hi << 16) | lo;
        memcpy(&out->size, e + 28, 4);
        out->is_dir = (e[11] & ATTR_DIR) != 0;
        memcpy(out->name, lfn[0] ? lfn : s83, sizeof(out->name));
        out->name[sizeof(out->name) - 1] = 0;
        *idx_io = idx + 1;
        return 0;
    }
    return -1;
}

/* resolve a path to its parent dir cluster + final component */
static int path_resolve(const char *path, uint32_t *parent, char *leaf, fs_dirent_t *hit, uint32_t *hit_idx) {
    uint32_t dir = fs.rootclus;
    leaf[0] = 0;
    const char *p = path;
    while (*p == '/') p++;
    while (*p) {
        char comp[128];
        int n = 0;
        while (*p && *p != '/' && n < (int)sizeof(comp) - 1) comp[n++] = *p++;
        comp[n] = 0;
        while (*p == '/') p++;
        int last = (*p == 0);
        fs_dirent_t tmp;
        uint32_t idx;
        if (dir_find(dir, comp, &tmp, &idx) != 0) {
            if (last) { *parent = dir; memcpy(leaf, comp, n + 1); return -1; } /* not found: leaf free */
            return -1;
        }
        if (last) {
            *parent = dir;
            memcpy(leaf, comp, n + 1);
            if (hit) *hit = tmp;
            if (hit_idx) *hit_idx = idx;
            return 0;
        }
        if (!tmp.is_dir) return -1;
        dir = tmp.first;
    }
    return -1;   /* root itself is not openable as a file */
}

/* ---------------- mount ---------------- */
int fs_mount(void) {
    uint8_t mbr[SECTOR_SIZE];
    if (ata_read(0, 1, mbr) != 0) return -1;
    uint32_t part = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t *p = mbr + 0x1BE + i * 16;
        uint8_t type = p[4];
        if (type == 0x0B || type == 0x0C) {
            memcpy(&part, p + 8, 4);
            break;
        }
    }
    if (!part) { kprintf("fat32: no FAT partition in MBR\n"); return -1; }

    uint8_t bpb[SECTOR_SIZE];
    if (ata_read(part, 1, bpb) != 0) return -1;
    uint16_t bps;  memcpy(&bps, bpb + 0x0B, 2);
    fs.secperclus = bpb[0x0D];
    uint16_t rsvd; memcpy(&rsvd, bpb + 0x0E, 2);
    uint8_t  nfat = bpb[0x10];
    uint32_t fatsz32; memcpy(&fatsz32, bpb + 0x24, 4);
    uint32_t tot32;   memcpy(&tot32, bpb + 0x20, 4);
    memcpy(&fs.rootclus, bpb + 0x2C, 4);
    memcpy(&fs.fsinfo, bpb + 0x30, 2);

    if (bps != 512 || !fs.secperclus || !fatsz32 || !nfat) {
        kprintf("fat32: unsupported BPB (bps=%u spc=%u)\n", bps, fs.secperclus);
        return -1;
    }
    fs.part_lba  = part;
    fs.fatstart  = part + rsvd;
    fs.fatsz     = fatsz32;
    fs.datastart = part + rsvd + nfat * fatsz32;
    fs.totsec    = tot32 ? tot32 : 0;
    fs.nclusters = (fs.totsec - rsvd - nfat * fatsz32) / fs.secperclus;
    fs.free_hint = 3;
    fs.free_count = 0xFFFFFFFF;
    fs.mounted = 1;
    fc_lba = 0xFFFFFFFF; fc_dirty = 0;

    /* read FSInfo free counter if present */
    uint8_t fi[SECTOR_SIZE];
    if (!ata_read(part + fs.fsinfo, 1, fi)) {
        uint32_t sig, fc; memcpy(&sig, fi + 484, 4); memcpy(&fc, fi + 488, 4);
        if (sig == 0x61417272u) fs.free_count = fc;
    }
    kprintf("fat32: partition @LBA %u, %u clusters x %u sectors, root=%u\n",
            part, fs.nclusters, fs.secperclus, fs.rootclus);
    return 0;
}

int fs_mounted(void) { return fs.mounted; }

uint32_t fs_free_bytes(void) { return fs.free_count * clus_bytes(); }

/* ---------------- file API ---------------- */
int fs_open(const char *path, fs_file_t *f) {
    memset(f, 0, sizeof(*f));
    if (!fs.mounted) return -1;
    fs_dirent_t d;
    uint32_t idx;
    char leaf[128];
    uint32_t parent;
    if (path_resolve(path, &parent, leaf, &d, &idx) != 0) return -1;
    if (d.is_dir) return -1;
    f->used = 1; f->first = d.first; f->size = d.size;
    f->cur = d.first; f->cur_idx = 0; f->pos = 0;
    f->de_dir = parent; f->de_idx = idx;
    memcpy(f->name, d.name, sizeof(f->name));
    return 0;
}

/* create or truncate; file opened for writing at offset 0 */
int fs_create(const char *path, fs_file_t *f) {
    memset(f, 0, sizeof(*f));
    if (!fs.mounted) return -1;
    fs_dirent_t d;
    uint32_t idx, parent;
    char leaf[128];
    int found = (path_resolve(path, &parent, leaf, &d, &idx) == 0);

    if (found) {
        if (d.is_dir) return -1;
        fat_free_chain(d.first);
    } else {
        /* find a free slot in the parent directory */
        if (!parent) return -1;
        idx = 0;
        uint8_t e[32];
        int free_slot = -1;
        for (; idx < 0x10000; idx++) {
            if (dent_read(parent, idx, e) != 0) return -1;
            if (e[0] == 0x00 || e[0] == 0xE5) { free_slot = (int)idx; break; }
        }
        if (free_slot < 0) return -1;
        idx = (uint32_t)free_slot;

        /* build an 8.3 name */
        char nm[16];
        int n = 0;
        for (const char *p = leaf; *p && n < 8; p++) {
            char c = *p;
            if (c == '.') { if (n) break; continue; }
            if (c >= 'a' && c <= 'z') c -= 32;
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
                nm[n++] = c;
        }
        if (!n) { nm[n++] = 'F'; nm[n++] = 'I'; nm[n++] = 'L'; nm[n++] = 'E'; }
        while (n < 8) nm[n++] = ' ';
        const char *dot = strrchr(leaf, '.');
        nm[8] = ' '; nm[9] = ' '; nm[10] = ' ';
        if (dot && dot != leaf) {
            int m = 0;
            for (const char *p = dot + 1; *p && m < 3; p++) {
                char c = *p;
                if (c >= 'a' && c <= 'z') c -= 32;
                nm[8 + m++] = c;
            }
        }

        uint8_t ne[32];
        memset(ne, 0, 32);
        memcpy(ne, nm, 11);
        ne[11] = 0x20;                       /* archive */
        if (dent_write(parent, idx, ne) != 0) return -1;
        d.first = 0; d.size = 0;
    }

    f->used = 1; f->is_dir = 0;
    f->first = 0; f->cur = 0; f->cur_idx = 0; f->size = 0; f->pos = 0;
    f->de_dir = parent; f->de_idx = idx;
    memcpy(f->name, leaf, sizeof(f->name));
    return 0;
}

/* write the short entry backing `f` (size + first cluster) */
static int de_update(fs_file_t *f) {
    uint8_t e[32];
    if (dent_read(f->de_dir, f->de_idx, e) != 0) return -1;
    uint16_t lo = f->first & 0xFFFF, hi = f->first >> 16;
    memcpy(e + 26, &lo, 2);
    memcpy(e + 20, &hi, 2);
    memcpy(e + 28, &f->size, 4);
    return dent_write(f->de_dir, f->de_idx, e);
}

void fs_close(fs_file_t *f) {
    if (f->used) fsinfo_sync();
    f->used = 0;
}

int fs_read(fs_file_t *f, void *vbuf, uint32_t n) {
    if (!f->used) return -1;
    uint8_t *buf = vbuf;
    uint32_t done = 0;
    while (done < n && f->pos < f->size) {
        uint32_t chunk = n - done;
        if (chunk > f->size - f->pos) chunk = f->size - f->pos;
        uint32_t coff = f->pos % clus_bytes();
        uint32_t csec = coff / SECTOR_SIZE;
        uint32_t soff = coff % SECTOR_SIZE;
        uint32_t want = SECTOR_SIZE - soff;
        if (want > chunk) want = chunk;
        if (!f->cur) { f->cur = f->first; f->cur_idx = 0; }
        uint8_t sec[SECTOR_SIZE];
        if (ata_read(clus_lba(f->cur) + csec, 1, sec) != 0) return done ? (int)done : -1;
        memcpy(buf + done, sec + soff, want);
        done += want;
        f->pos += want;
        if (f->pos % clus_bytes() == 0 && f->pos < f->size) {
            f->cur = fat_get(f->cur);
            if (f->cur >= FAT_EOC) break;
        }
    }
    return (int)done;
}

int fs_write(fs_file_t *f, const void *vbuf, uint32_t n) {
    if (!f->used) return -1;
    const uint8_t *buf = vbuf;
    uint32_t done = 0;
    if (!f->cur) { f->cur = f->first; f->cur_idx = 0; }

    while (done < n) {
        uint32_t coff = f->pos % clus_bytes();
        uint32_t csec = coff / SECTOR_SIZE;
        uint32_t soff = coff % SECTOR_SIZE;
        uint32_t want = SECTOR_SIZE - soff;
        if (want > n - done) want = n - done;

        /* need a cluster? */
        if (f->pos == 0 && !f->first) {
            f->first = f->cur = fat_alloc_cluster();
            if (!f->first) break;
        } else if (coff == 0 && f->pos >= f->size && f->pos > 0) {
            /* starting a new cluster at/after EOF: allocate if chain ends */
            uint32_t next = fat_get(f->cur);
            if (next >= FAT_EOC) {
                next = fat_alloc_cluster();
                if (!next) break;
                fat_set(f->cur, next);
            }
            f->cur = next;
        }

        uint8_t sec[SECTOR_SIZE];
        if (soff || want < SECTOR_SIZE) {
            if (ata_read(clus_lba(f->cur) + csec, 1, sec) != 0) break;
        } else {
            memset(sec, 0, SECTOR_SIZE);
        }
        memcpy(sec + soff, buf + done, want);
        if (ata_write_nb(clus_lba(f->cur) + csec, 1, sec) != 0) break;

        done += want;
        f->pos += want;
        if (f->pos > f->size) f->size = f->pos;
    }
    if (done) de_update(f);
    if (done) ata_flush();
    return done ? (int)done : -1;
}

/* ---------------- directory API ---------------- */
int fs_opendir(const char *path, fs_dir_t *d) {
    if (!fs.mounted) return -1;
    if (!path[0] || (path[0] == '/' && !path[1])) {
        d->dir_first = fs.rootclus; d->idx = 0; return 0;
    }
    fs_dirent_t e;
    char leaf[128];
    uint32_t parent;
    if (path_resolve(path, &parent, leaf, &e, 0) != 0) return -1;
    if (!e.is_dir) return -1;
    d->dir_first = e.first; d->idx = 0;
    return 0;
}

int fs_readdir(fs_dir_t *d, fs_dirent_t *out) {
    return dir_next(d->dir_first, &d->idx, out);
}

/* ---------------- unlink / mkdir ---------------- */
int fs_unlink(const char *path) {
    fs_dirent_t e;
    uint32_t idx, parent;
    char leaf[128];
    if (path_resolve(path, &parent, leaf, &e, &idx) != 0) return -1;
    if (e.is_dir) return -1;
    fat_free_chain(e.first);
    /* remove any LFN entries in front of the short entry */
    uint8_t d[32];
    while (idx > 0) {
        if (dent_read(parent, idx - 1, d) != 0) break;
        if ((d[11] & ATTR_LFN) != ATTR_LFN) break;
        d[0] = 0xE5;
        dent_write(parent, idx - 1, d);
        idx--;
    }
    if (dent_read(parent, idx, d) != 0) return -1;
    d[0] = 0xE5;
    int rc = dent_write(parent, idx, d);
    fsinfo_sync();
    return rc;
}

int fs_mkdir(const char *path) {
    fs_dirent_t e;
    uint32_t parent;
    char leaf[128];
    if (path_resolve(path, &parent, leaf, &e, 0) == 0) return e.is_dir ? 0 : -1;

    /* free slot in parent */
    uint8_t d[32];
    uint32_t idx = 0;
    int free_slot = -1;
    for (; idx < 0x10000; idx++) {
        if (dent_read(parent, idx, d) != 0) return -1;
        if (d[0] == 0x00 || d[0] == 0xE5) { free_slot = (int)idx; break; }
    }
    if (free_slot < 0) return -1;

    uint32_t clus = fat_alloc_cluster();
    if (!clus) return -1;

    /* 8.3 name */
    char nm[11];
    memset(nm, ' ', 11);
    int n = 0;
    for (const char *p = leaf; *p && n < 8; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            nm[n++] = c;
    }
    if (!n) return -1;

    memset(d, 0, 32);
    memcpy(d, nm, 11);
    d[11] = ATTR_DIR;
    uint16_t lo = clus & 0xFFFF, hi = clus >> 16;
    memcpy(d + 26, &lo, 2); memcpy(d + 20, &hi, 2);
    if (dent_write(parent, (uint32_t)free_slot, d) != 0) return -1;

    /* "." and ".." inside the new directory */
    uint8_t dot[32];
    memset(dot, 0, 32);
    memset(dot, ' ', 11);
    dot[0] = '.'; dot[11] = ATTR_DIR;
    uint16_t plo = parent & 0xFFFF, phi = parent >> 16;
    memcpy(dot + 26, &lo, 2); memcpy(dot + 20, &hi, 2);
    dent_write(clus, 0, dot);
    memset(dot, ' ', 11);
    dot[0] = '.'; dot[1] = '.'; dot[11] = ATTR_DIR;
    memcpy(dot + 26, &plo, 2); memcpy(dot + 20, &phi, 2);
    dent_write(clus, 1, dot);
    fsinfo_sync();
    return 0;
}
