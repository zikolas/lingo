/* LFLASH.C - PCMCIA linear flash / SRAM memory card reader-writer for DOS.
 * Drives the card directly through an Intel 82365-class PCIC at 0x3E0 (no Card
 * Services needed). Reads any memory card raw; identifies flash chips via the
 * CIS, JEDEC autoselect and CFI; erases + programs Intel CUI flash (28F008SA
 * family, 12V Vpp handled) and AMD 29F-series flash (5V), and writes SRAM
 * cards directly. Polite in the CISDUMP tradition: borrowed memory windows are
 * saved/restored, a card found powered is left exactly as found, and the
 * default READ / INFO paths never write a single byte to the card.
 *
 * Usage:  LFLASH [command] [file] [options]
 *   INFO             show socket + CIS + card facts (default command)
 *        /PROBE      also identify the chip live (JEDEC/CFI/SRAM probe -
 *                    writes ID commands to the card, restores SRAM bytes)
 *   READ  file       dump card -> file (100%% read-only, no probe)
 *   WRITE file       erase + program + verify file -> card
 *   ERASE            erase blocks in range (needs /LEN or /ALL)
 *   VERIFY file      compare card against file
 * Options:
 *   /S n             socket 0 or 1 (default: first socket with a card)
 *   /OFF n           card byte offset (default 0)
 *   /LEN n           length in bytes (default: whole card / whole file)
 *   /SIZE n          card size override (blank-CIS cards)
 *   /BLK n           erase-block size override (combined, bytes)
 *   /TYPE t          force INTEL / AMD / SRAM
 *   /X1 /X2          force 1 or 2 interleaved chips (byte lanes)
 *   /VPP n           programming voltage: 5 or 12 (default: per chip table)
 *   /NOERASE         program without erasing first (pre-erased card)
 *   /NOVERIFY        skip the post-write verify pass
 *   /ALL             with ERASE: the whole card
 *   /SEG n           host window segment (default D000; uses 32K: seg+seg+400)
 *   /Y               don't ask for confirmation
 *   numbers take hex (0x...) and K/M suffixes, e.g. /OFF 0x20000 /LEN 512K
 *
 * Build: C:\WATCOM\BLD.BAT LFLASH   (Open Watcom 1.9, wcc -ms, C89)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

#define PCIC 0x3E0

/* card types */
#define T_UNKNOWN 0
#define T_SRAM    1
#define T_INTEL   2   /* CUI command set: 28F008SA family, Sharp clones */
#define T_AMD     3   /* 29F-series unlock command set (incl. Fujitsu/ST) */
#define T_ROM     4   /* reads stable, ignores writes */
#define T_SERIES1 5   /* pre-CUI 28F010/28F020: detected, not programmable */

static unsigned sockoff;

static void wr(unsigned char i, unsigned char v){ outp(PCIC, i + sockoff); outp(PCIC + 1, v); }
static unsigned char rd(unsigned char i){ outp(PCIC, i + sockoff); return (unsigned char)inp(PCIC + 1); }
static void dly(unsigned n){ while (n--) inp(0x80); }              /* ~1us each */
static unsigned long ticks(void){ return *(volatile unsigned long __far *)MK_FP(0x40, 0x6C); }

/* ---- options ------------------------------------------------------------- */
static int o_sock = -1;
static unsigned o_seg = 0xD000;
static unsigned long o_off = 0, o_len = 0, o_size = 0, o_blk = 0;
static int o_type = 0, o_lanes = 0, o_vpp = -1;
static int o_yes = 0, o_noerase = 0, o_noverify = 0, o_probe = 0, o_all = 0;

/* ---- card facts (probe results / overrides) ------------------------------ */
static int ctype = T_UNKNOWN;
static int nlanes = 1;                    /* interleaved byte lanes: 1 or 2  */
static unsigned char id_mfr = 0, id_dev = 0;
static const char *chip_name = "unknown";
static unsigned long chip_kb = 0;         /* per-chip size, KB (informative) */
static unsigned long blkkb = 0;           /* per-chip erase block, KB        */
static unsigned long blk_bytes = 0;       /* combined erase block, bytes     */
static unsigned long card_size = 0;       /* total card bytes; 0 = unknown   */
static int need_vpp12 = 0;
static unsigned amd_a1 = 0x555, amd_a2 = 0x2AA;
static unsigned long cfi_size = 0, cfi_blk = 0;

/* ---- CIS facts ----------------------------------------------------------- */
static int cis_present = 0;
static int cis_dtype = -1, cis_wps = -1, cis_speed = -1;
static unsigned long cis_size = 0;
static int cis_jmfr = -1, cis_jinfo = -1;
static int cis_manf = -1, cis_prod = -1;

/* ---- polite two-window socket access ------------------------------------ */
/* Window 0 stays on card address 0 (common memory; flipped to attribute for
 * the CIS read) - AMD unlock cycles and probes go through it. Window 1 pages
 * across the whole card in 16KB steps for bulk data. Both are borrowed from
 * the controller's free (disabled) windows and restored on exit, and only a
 * card we powered up ourselves is powered back down.                         */
static unsigned char sv02, sv03, sv06, svwin[2][6];
static int winreg[2], winbit[2];
static unsigned winseg[2];
static int we_powered, was_io;
static unsigned long cur_pageB;

static void pick2(unsigned char wen, int *a, int *b)
{
    int n, got = 0, pick[2];
    pick[0] = 0; pick[1] = 1;
    for (n = 0; n < 5 && got < 2; n++) if (!(wen & (1 << n))) pick[got++] = n;
    for (n = 0; n < 5 && got < 2; n++) {
        if (got == 1 && pick[0] == n) continue;
        pick[got++] = n;                     /* none free: reuse (restored)  */
    }
    *a = pick[0]; *b = pick[1];
}

static void setwin(int w, unsigned long cardoff, int attr)
{
    unsigned page = (unsigned)(cardoff >> 12);
    unsigned woff = ((page - (winseg[w] >> 8)) & 0x3FFF) | (attr ? 0x4000 : 0);
    wr(winreg[w] + 4, woff & 0xFF);
    wr(winreg[w] + 5, (woff >> 8) & 0xFF);
}

static void initwin(int w)
{
    unsigned start = winseg[w] >> 8, stop = (winseg[w] >> 8) + 3;   /* 16KB */
    int b = winreg[w], i;
    for (i = 0; i < 6; i++) svwin[w][i] = rd(b + i);
    wr(b + 0, start & 0xFF); wr(b + 1, (start >> 8) & 0x3F);
    wr(b + 2, stop  & 0xFF); wr(b + 3, (stop  >> 8) & 0x3F);
    setwin(w, 0L, 0);
}

static volatile unsigned char __far *wp8(int w, unsigned off)
{ return (volatile unsigned char __far *)MK_FP(winseg[w], off); }

/* byte access anywhere in common memory, paging window 1 as needed */
static volatile unsigned char __far *cmem(unsigned long addr)
{
    unsigned long page = addr & 0xFFFFC000L;
    if (page != cur_pageB) { setwin(1, page, 0); cur_pageB = page; }
    return wp8(1, (unsigned)(addr & 0x3FFFL));
}

static int open_socket(void)
{
    int i, w0, w1;
    if ((rd(0x01) & 0x0C) != 0x0C) return 0;         /* no card present     */
    sv02 = rd(0x02); sv03 = rd(0x03); sv06 = rd(0x06);
    was_io = (sv03 & 0x20) != 0;
    we_powered = 0;
    if (!(rd(0x01) & 0x40) && !was_io) {
        wr(0x02, 0x95); dly(30000);                  /* 5V power, we own it */
        wr(0x03, 0x40); dly(30000);                  /* mem mode, run       */
        we_powered = 1;
    }
    pick2(sv06, &w0, &w1);
    winreg[0] = 0x10 + w0 * 8;  winreg[1] = 0x10 + w1 * 8;
    winbit[0] = 1 << w0;        winbit[1] = 1 << w1;
    winseg[0] = o_seg;          winseg[1] = o_seg + 0x400;
    for (i = 0; i < 2; i++) initwin(i);
    wr(0x06, sv06 | winbit[0] | winbit[1]);
    dly(20000);
    cur_pageB = 0xFFFFFFFFL;
    return 1;
}

static void close_socket(void)
{
    int i, j;
    for (i = 0; i < 2; i++) for (j = 0; j < 6; j++) wr(winreg[i] + j, svwin[i][j]);
    wr(0x06, sv06);
    if (we_powered) { wr(0x03, sv03); wr(0x02, sv02); }
}

static int wait_ready(void)
{
    unsigned n;
    for (n = 0; n < 60000U; n++) {
        if (rd(0x01) & 0x20) return 1;
        dly(50);
    }
    return 0;
}

/* Vpp 12V only while programming/erasing old Intel chips; restored after */
static unsigned char vpp_sav; static int vpp_on = 0;
static void vpp12(int on)
{
    if (on && !vpp_on) {
        vpp_sav = rd(0x02);
        wr(0x02, (vpp_sav & 0xF0) | 0x0A);           /* Vpp1=Vpp2=12V      */
        vpp_on = 1; dly(30000);
    } else if (!on && vpp_on) {
        wr(0x02, vpp_sav);
        vpp_on = 0; dly(10000);
    }
}

/* ---- CIS ----------------------------------------------------------------- */
static unsigned char cisbuf[512];

static void read_cis(void)
{
    unsigned i;
    setwin(0, 0L, 1); dly(5000);                     /* attribute space     */
    for (i = 0; i < sizeof(cisbuf); i++) cisbuf[i] = *wp8(0, i * 2);
    setwin(0, 0L, 0); dly(5000);                     /* back to common      */
}

static void parse_device_tuple(unsigned char *b, int n)
{
    int p = 0;
    while (p < n) {
        int info = b[p++], code, units;
        if (info == 0xFF) break;
        if (cis_dtype < 0) {
            cis_dtype = (info >> 4) & 0x0F;
            cis_wps   = (info >> 3) & 1;
            cis_speed = info & 7;
        }
        if ((info & 7) == 7) {                       /* extended speed      */
            while (p < n && (b[p] & 0x80)) p++;
            p++;
        }
        if (p >= n) break;
        code  = b[p] & 7;
        units = ((b[p] >> 3) & 0x1F) + 1;
        p++;
        if (code < 7) cis_size += (512UL << (2 * code)) * units;
    }
}

static void parse_cis(int show)
{
    static const char *dt[8] = { "NULL","ROM","OTPROM","EPROM",
                                 "EEPROM","FLASH","SRAM","DRAM" };
    static const char *sp[8] = { "none","250ns","200ns","150ns",
                                 "100ns","?","?","ext" };
    int off = 0, guard = 0;
    cis_present = 0; cis_dtype = -1; cis_wps = -1; cis_speed = -1;
    cis_size = 0; cis_jmfr = -1; cis_jinfo = -1; cis_manf = -1; cis_prod = -1;
    for (;;) {
        int code, link, i;
        unsigned char body[254];
        code = cisbuf[off];
        if (code == 0xFF || off >= (int)sizeof(cisbuf) - 2) break;
        if (code == 0x00) { off++; if (++guard > 256) break; continue; }
        link = cisbuf[off + 1];
        if (link == 0xFF) break;
        for (i = 0; i < link && (off + 2 + i) < (int)sizeof(cisbuf); i++)
            body[i] = cisbuf[off + 2 + i];
        cis_present = 1;
        switch (code) {
        case 0x01:                                   /* CISTPL_DEVICE       */
            parse_device_tuple(body, link);
            break;
        case 0x18:                                   /* CISTPL_JEDEC_C      */
            if (link >= 2) { cis_jmfr = body[0]; cis_jinfo = body[1]; }
            break;
        case 0x20:                                   /* CISTPL_MANFID       */
            if (link >= 4) {
                cis_manf = body[1] << 8 | body[0];
                cis_prod = body[3] << 8 | body[2];
            }
            break;
        case 0x15:                                   /* CISTPL_VERS_1       */
            if (show) {
                printf("    VERS_1: ");
                for (i = 2; i < link; i++) {
                    unsigned char c = body[i];
                    if (c == 0xFF) break;
                    putchar(c ? c : '|');
                }
                printf("\n");
            }
            break;
        default:
            break;
        }
        off += link + 2;
    }
    if (!show) return;
    if (!cis_present) {
        printf("    CIS: none/blank (fresh flash card? use /SIZE for full ops)\n");
        return;
    }
    if (cis_manf >= 0) printf("    MANFID: %04X / %04X\n", cis_manf, cis_prod);
    if (cis_dtype >= 0)
        printf("    DEVICE: %s, speed %s, WPS=%d, size %luK\n",
               dt[cis_dtype & 7], sp[cis_speed & 7], cis_wps, cis_size >> 10);
    if (cis_jmfr >= 0)
        printf("    JEDEC: mfr 0x%02X info 0x%02X\n", cis_jmfr, cis_jinfo);
}

/* ---- chip table ----------------------------------------------------------- */
struct devrec {
    unsigned char mfr, dev;
    const char *name;
    unsigned kb, blk;                                /* per chip, KB        */
    unsigned char kind;                              /* T_INTEL/T_AMD/...   */
    unsigned char vpp12;
};
static struct devrec devtab[] = {
    { 0x89, 0xA2, "Intel 28F008SA",    1024, 64, T_INTEL,   1 },
    { 0xB0, 0xA2, "Sharp LH28F008SA",  1024, 64, T_INTEL,   1 },
    { 0x89, 0xA0, "Intel 28F016SA",    2048, 64, T_INTEL,   0 },
    { 0x89, 0xA6, "Intel 28F008-S",    1024, 64, T_INTEL,   0 },
    { 0x89, 0xB4, "Intel 28F010",       128,  0, T_SERIES1, 1 },
    { 0x89, 0xBD, "Intel 28F020",       256,  0, T_SERIES1, 1 },
    { 0x01, 0xA4, "AMD Am29F040",       512, 64, T_AMD,     0 },
    { 0x04, 0xA4, "Fujitsu MBM29F040",  512, 64, T_AMD,     0 },
    { 0x01, 0xD5, "AMD Am29F080",      1024, 64, T_AMD,     0 },
    { 0x01, 0xAD, "AMD Am29F016",      2048, 64, T_AMD,     0 },
    { 0x20, 0xE2, "ST M29F040",         512, 64, T_AMD,     0 },
    { 0, 0, NULL, 0, 0, 0, 0 }
};

static struct devrec *lookup_dev(unsigned char m, unsigned char d)
{
    int i;
    for (i = 0; devtab[i].name; i++)
        if (devtab[i].mfr == m && devtab[i].dev == d) return &devtab[i];
    return NULL;
}

/* ---- live probe ----------------------------------------------------------- */
/* every common-memory address a probe may WRITE is saved first and restored
 * afterwards unless the card turned out to be command-set flash (where the
 * writes were commands, not data)                                            */
static unsigned short savlist[] = {
    0, 1, 2, 3, 0x55, 0x56, 0xAA, 0xAB, 0x154, 0x155,
    0x2AA, 0x2AB, 0x554, 0x555, 0x556, 0xAAA, 0xAAB, 0xAAC,
    0x1554, 0x1555
};
#define NSAV (sizeof(savlist) / sizeof(savlist[0]))
static unsigned char savbytes[NSAV];

static void save_probe_bytes(void)
{
    unsigned i;
    for (i = 0; i < NSAV; i++) savbytes[i] = *wp8(0, savlist[i]);
}

static void restore_probe_bytes(void)
{
    unsigned i;
    for (i = 0; i < NSAV; i++) {
        volatile unsigned char __far *p = wp8(0, savlist[i]);
        if (*p != savbytes[i]) *p = savbytes[i];
    }
}

static int try_intel(int force)
{
    unsigned char m0, m1, d0, d1;
    *wp8(0, 0) = 0xFF; *wp8(0, 1) = 0xFF; dly(100);
    *wp8(0, 0) = 0x90; *wp8(0, 1) = 0x90; dly(100);
    m0 = *wp8(0, 0); m1 = *wp8(0, 1);
    d0 = *wp8(0, 2); d1 = *wp8(0, 3);
    *wp8(0, 0) = 0xFF; *wp8(0, 1) = 0xFF; dly(100);
    if (!force && m0 != 0x89 && m0 != 0xB0) return 0;
    if (m0 == m1 && d0 == d1) { nlanes = 2; id_mfr = m0; id_dev = d0; }
    else                      { nlanes = 1; id_mfr = m0; id_dev = m1; }
    if (o_lanes) nlanes = o_lanes;
    return 1;
}

static int amd_mfr_ok(unsigned char m)
{
    return m == 0x01 || m == 0x04 || m == 0x20 || m == 0x1F ||
           m == 0xC2 || m == 0xBF || m == 0x98;
}

static int try_amd(int force)
{
    /* variants: x8 chips (555/2AA), x16 chips in byte mode (AAA/555),
     * each singly and as an interleaved pair                                */
    static unsigned va1[4]  = { 0x555, 0xAAA, 0x555, 0xAAA };
    static unsigned va2[4]  = { 0x2AA, 0x555, 0x2AA, 0x555 };
    static int      vln[4]  = { 1, 1, 2, 2 };
    static int      vdo[4]  = { 1, 2, 1, 2 };        /* dev-id byte offset  */
    int v, lane;
    for (v = 0; v < 4; v++) {
        int sh = (vln[v] == 2) ? 1 : 0, ok = 1;
        unsigned char m[2], d[2];
        if (o_lanes && vln[v] != o_lanes) continue;
        m[1] = 0; d[1] = 0;
        for (lane = 0; lane < vln[v]; lane++) {
            *wp8(0, lane) = 0xF0; dly(50);
            *wp8(0, (va1[v] << sh) | lane) = 0xAA;
            *wp8(0, (va2[v] << sh) | lane) = 0x55;
            *wp8(0, (va1[v] << sh) | lane) = 0x90; dly(50);
            m[lane] = *wp8(0, (0 << sh) | lane);
            d[lane] = *wp8(0, (unsigned)(vdo[v] << sh) | lane);
            *wp8(0, lane) = 0xF0; dly(50);
        }
        if (!force) {
            if (!amd_mfr_ok(m[0])) ok = 0;
            if (vln[v] == 2 && m[1] != m[0]) ok = 0;
        }
        if (ok) {
            amd_a1 = va1[v]; amd_a2 = va2[v];
            nlanes = vln[v]; id_mfr = m[0]; id_dev = d[0];
            return 1;
        }
    }
    if (force) {                                     /* /TYPE AMD, no match */
        amd_a1 = 0x555; amd_a2 = 0x2AA;
        nlanes = o_lanes ? o_lanes : 1;
        return 1;
    }
    return 0;
}

static int try_sram(void)
{
    static unsigned tst[2] = { 0, 0x2AB };
    int i;
    for (i = 0; i < 2; i++) {
        volatile unsigned char __far *p = wp8(0, tst[i]);
        unsigned char b = *p, x = (unsigned char)~b;
        *p = x; dly(10);
        if (*p != x) return 0;
        *p = b; dly(10);
        if (*p != b) return 0;
    }
    return 1;
}

static int try_cfi(void)
{
    static unsigned strides[3] = { 1, 2, 4 };
    int si;
    for (si = 0; si < 3; si++) {
        unsigned s = strides[si];
        *wp8(0, 0x55 * s) = 0x98; dly(50);
        if (*wp8(0, 0x10 * s) == 'Q' && *wp8(0, 0x11 * s) == 'R' &&
            *wp8(0, 0x12 * s) == 'Y') {
            unsigned char n27 = *wp8(0, 0x27 * s);
            unsigned long zl = *wp8(0, 0x2F * s), zh = *wp8(0, 0x30 * s);
            unsigned char alg = *wp8(0, 0x13 * s);
            cfi_size = (n27 && n27 < 27) ? (1UL << n27) : 0;
            cfi_blk  = ((zh << 8) | zl) * 256UL;
            *wp8(0, 0) = 0xFF; dly(20);
            *wp8(0, 0) = 0xF0; dly(20);
            if (ctype == T_UNKNOWN) {
                if (alg == 1 || alg == 3) ctype = T_INTEL;
                else if (alg == 2)        ctype = T_AMD;
            }
            return 1;
        }
        *wp8(0, 0) = 0xFF; dly(20);
        *wp8(0, 0) = 0xF0; dly(20);
    }
    return 0;
}

static void resolve_geom(void)
{
    struct devrec *t = lookup_dev(id_mfr, id_dev);
    if (t) {
        chip_name = t->name; chip_kb = t->kb; blkkb = t->blk;
        need_vpp12 = t->vpp12;
        if (t->kind == T_SERIES1) ctype = T_SERIES1;
        else if (ctype == T_UNKNOWN) ctype = t->kind;
    } else if (ctype == T_INTEL || ctype == T_AMD) {
        chip_name = "unknown chip"; blkkb = 64;      /* common default      */
    }
    if (cfi_size && !chip_kb) chip_kb = cfi_size >> 10;
    if (cfi_blk) blkkb = cfi_blk >> 10;
    blk_bytes = o_blk ? o_blk : blkkb * 1024UL * nlanes;
    if (o_size)              card_size = o_size;
    else if (cis_size)       card_size = cis_size;
    else if (chip_kb)        card_size = chip_kb * 1024UL * nlanes;
    if (o_vpp >= 0) need_vpp12 = (o_vpp == 12);
}

static void probe_card(void)
{
    /* fresh state per socket */
    ctype = T_UNKNOWN; nlanes = 1; id_mfr = id_dev = 0;
    chip_name = "unknown"; chip_kb = 0; blkkb = 0;
    cfi_size = 0; cfi_blk = 0; need_vpp12 = 0;
    amd_a1 = 0x555; amd_a2 = 0x2AA;

    if (rd(0x01) & 0x10) {
        /* WP switch on: card ignores all writes, so a live (write-based)
         * probe can't work - fall back to what the CIS says */
        printf("    (WP switch on - live probe skipped, using CIS only)\n");
        if      (cis_dtype == 6) ctype = T_SRAM;
        else if (cis_dtype == 5) ctype = T_UNKNOWN;  /* flash, chip unknown */
        else                     ctype = T_ROM;
        resolve_geom();
        return;
    }

    save_probe_bytes();
    if (o_type == T_SRAM) {
        ctype = T_SRAM;
    } else if (o_type == T_INTEL) {
        ctype = T_INTEL; try_intel(1);
    } else if (o_type == T_AMD) {
        ctype = T_AMD; try_amd(1);
    } else {
        if (cis_dtype == 6 && try_sram()) ctype = T_SRAM;
        if (ctype == T_UNKNOWN && try_intel(0)) ctype = T_INTEL;
        if (ctype == T_UNKNOWN && try_amd(0))   ctype = T_AMD;
        if (ctype == T_UNKNOWN) try_cfi();
        if (ctype == T_UNKNOWN && try_sram())   ctype = T_SRAM;
        if (ctype == T_UNKNOWN) {
            unsigned char a = *wp8(0, 0), b;
            dly(100); b = *wp8(0, 0);
            if (a == b) ctype = T_ROM;
        }
    }
    if ((ctype == T_INTEL || ctype == T_AMD) && !cfi_size) try_cfi();
    if (ctype == T_SRAM || ctype == T_ROM || ctype == T_UNKNOWN)
        restore_probe_bytes();
    resolve_geom();
}

static const char *type_name(int t)
{
    switch (t) {
    case T_SRAM:    return "SRAM";
    case T_INTEL:   return "Intel-CUI flash";
    case T_AMD:     return "AMD-style flash";
    case T_ROM:     return "ROM / write-ignoring";
    case T_SERIES1: return "Intel Series 1 flash (pre-CUI)";
    default:        return "unknown";
    }
}

static void show_probe(void)
{
    printf("    PROBE: %s", type_name(ctype));
    if (ctype == T_INTEL || ctype == T_AMD || ctype == T_SERIES1) {
        printf(", id %02X/%02X = %s x%d", id_mfr, id_dev, chip_name, nlanes);
        if (blk_bytes) printf(", block %luK", blk_bytes >> 10);
        if (ctype == T_INTEL) printf(", Vpp %s", need_vpp12 ? "12V" : "5V");
    }
    if (cfi_size) printf(" [CFI: chip %luK blk %luK]", cfi_size >> 10, cfi_blk >> 10);
    printf("\n");
    if (card_size) printf("    SIZE: %luK%s\n", card_size >> 10,
                          (!o_size && !cis_size) ? " (from chip id - multi-bank cards may be larger)" : "");
    else printf("    SIZE: unknown - pass /SIZE or /LEN for full-card ops\n");
    if (ctype == T_SERIES1)
        printf("    NOTE: 28F010/020 need the old program/erase-verify algorithm - read-only for now\n");
}

/* ---- flash primitives ----------------------------------------------------- */

static int intel_prog(unsigned long addr, unsigned char val)
{
    volatile unsigned char __far *p = cmem(addr);
    unsigned n; unsigned char sr = 0;
    *p = 0x40; *p = val;
    for (n = 0; n < 60000U; n++) { sr = *p; if (sr & 0x80) break; }
    if (!(sr & 0x80)) return -1;                     /* timeout             */
    if (sr & 0x18) return sr;                        /* prog fail / Vpp low */
    return 0;
}

static int intel_erase_blk(unsigned long baddr)
{
    int l;
    for (l = 0; l < nlanes; l++) {
        volatile unsigned char __far *p = cmem(baddr + l);
        unsigned long t0 = ticks(); unsigned char sr;
        *p = 0x20; *p = 0xD0;
        for (;;) {
            sr = *p;
            if (sr & 0x80) break;
            if (ticks() - t0 > 728UL) { *p = 0x50; *p = 0xFF; return -1; }  /* ~40s */
        }
        *p = 0x50; *p = 0xFF;
        if (sr & 0x28) return sr;                    /* erase fail / Vpp    */
    }
    return 0;
}

static void amd_cmd(int lane, unsigned char c)
{
    int sh = (nlanes == 2) ? 1 : 0;
    *wp8(0, (amd_a1 << sh) | lane) = 0xAA;
    *wp8(0, (amd_a2 << sh) | lane) = 0x55;
    *wp8(0, (amd_a1 << sh) | lane) = c;
}

static int amd_prog(unsigned long addr, unsigned char val)
{
    volatile unsigned char __far *p;
    unsigned n; unsigned char r;
    int lane = (nlanes == 2) ? (int)(addr & 1) : 0;
    amd_cmd(lane, 0xA0);
    p = cmem(addr); *p = val;
    for (n = 0; n < 60000U; n++) {
        r = *p;
        if (r == val) return 0;
        if (((r ^ val) & 0x80) && (r & 0x20)) {      /* DQ5: over time      */
            r = *p;
            if (r == val) return 0;
            return -2;
        }
    }
    return -1;
}

static int amd_erase_blk(unsigned long baddr)
{
    int lane, sh = (nlanes == 2) ? 1 : 0;
    for (lane = 0; lane < nlanes; lane++) {
        volatile unsigned char __far *p;
        unsigned long t0;
        amd_cmd(lane, 0x80);
        *wp8(0, (amd_a1 << sh) | lane) = 0xAA;
        *wp8(0, (amd_a2 << sh) | lane) = 0x55;
        p = cmem(baddr + lane); *p = 0x30;
        t0 = ticks();
        for (;;) {
            unsigned char r = *p;
            if (r == 0xFF) break;
            if (r & 0x20) {                          /* DQ5: recheck once   */
                r = *p;
                if (r == 0xFF) break;
                *wp8(0, lane) = 0xF0;
                return -2;
            }
            if (ticks() - t0 > 728UL) { *wp8(0, lane) = 0xF0; return -1; }
        }
    }
    return 0;
}

static void read_array_mode(void)
{
    if (ctype == T_INTEL || ctype == T_SERIES1) {
        *wp8(0, 0) = 0x50; *wp8(0, 1) = 0x50;        /* clear status        */
        *wp8(0, 0) = 0xFF; *wp8(0, 1) = 0xFF;
    } else if (ctype == T_AMD) {
        *wp8(0, 0) = 0xF0; *wp8(0, 1) = 0xF0;
    }
    dly(100);
}

/* a reset command only reaches the chip that owns the address it is written
 * to, so after programming a range on a multi-chip card, hit every erase
 * block in the range (both byte lanes) to be sure no chip is left in
 * status-read mode */
static void read_array_range(unsigned long start, unsigned long end)
{
    unsigned long step = blk_bytes ? blk_bytes : (0x10000UL * nlanes);
    unsigned long a = start - (start % step);
    for (;;) {
        int l;
        for (l = 0; l < nlanes; l++) {
            if (ctype == T_INTEL) { *cmem(a + l) = 0x50; *cmem(a + l) = 0xFF; }
            else if (ctype == T_AMD) *cmem(a + l) = 0xF0;
        }
        if (a + step > end) break;
        a += step;
    }
    dly(100);
}

static int erase_block(unsigned long addr)
{
    if (ctype == T_INTEL) return intel_erase_blk(addr);
    if (ctype == T_AMD)   return amd_erase_blk(addr);
    return 0;
}

static int prog_byte(unsigned long addr, unsigned char val)
{
    if (ctype == T_INTEL) return intel_prog(addr, val);
    if (ctype == T_AMD)   return amd_prog(addr, val);
    return -3;
}

/* ---- bulk helpers ---------------------------------------------------------- */
static unsigned char buf[16384];

static unsigned long crctab[256];
static void crc_init(void)
{
    unsigned long c; int n, k;
    for (n = 0; n < 256; n++) {
        c = (unsigned long)n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : (c >> 1);
        crctab[n] = c;
    }
}
static unsigned long crc_run;
static void crc_start(void){ crc_run = 0xFFFFFFFFUL; }
static void crc_feed(unsigned char *d, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++)
        crc_run = crctab[(unsigned char)(crc_run ^ d[i])] ^ (crc_run >> 8);
}
static unsigned long crc_done(void){ return crc_run ^ 0xFFFFFFFFUL; }

static void card_to_buf(unsigned long addr, unsigned char *dst, unsigned n)
{
    while (n) {
        unsigned off = (unsigned)(addr & 0x3FFFL);
        unsigned run = (unsigned)(0x4000 - off);
        if (run > n) run = n;
        cmem(addr);
        _fmemcpy(dst, MK_FP(winseg[1], off), run);
        addr += run; dst += run; n -= run;
    }
}

static void buf_to_card(unsigned long addr, unsigned char *src, unsigned n)
{
    while (n) {                                      /* SRAM only           */
        unsigned off = (unsigned)(addr & 0x3FFFL);
        unsigned run = (unsigned)(0x4000 - off);
        if (run > n) run = n;
        cmem(addr);
        _fmemcpy(MK_FP(winseg[1], off), src, run);
        addr += run; src += run; n -= run;
    }
}

static void progress(const char *what, unsigned long done, unsigned long total)
{
    printf("\r  %s %luK", what, done >> 10);
    if (total) printf(" / %luK", total >> 10);
    fflush(stdout);
}

static int confirm(void)
{
    int c;
    if (o_yes) return 1;
    printf("  proceed? (Y/N) ");
    c = getch();
    printf("%c\n", c);
    return (c == 'y' || c == 'Y');
}

/* verify card range against open file; returns mismatch count (-1 file err) */
static long verify_range(FILE *f, unsigned long off, unsigned long len,
                         unsigned long *first_bad)
{
    long bad = 0;
    unsigned long a = off, left = len, fb = 0xFFFFFFFFUL;
    while (left) {
        unsigned n = (left > sizeof(buf)) ? sizeof(buf) : (unsigned)left;
        unsigned got = fread(buf, 1, n, f), i;
        if (got == 0) break;
        for (i = 0; i < got; i++) {
            if (*cmem(a + i) != buf[i]) {
                if (fb == 0xFFFFFFFFUL) fb = a + i;
                bad++;
            }
        }
        a += got; left -= got;
        progress("verified", a - off, len);
        if (got < n) break;
    }
    printf("\n");
    *first_bad = fb;
    return bad;
}

/* ---- socket status line ---------------------------------------------------- */
static void show_status(void)
{
    unsigned char s = rd(0x01);
    printf("  card present, %s, %s, WP switch %s",
           we_powered ? "powered by us" : (was_io ? "already enabled as I/O" : "already powered"),
           (s & 0x20) ? "READY" : "NOT READY",
           (s & 0x10) ? "ON" : "off");
    if ((s & 0x03) != 0x03)
        printf(", BVD1=%d BVD2=%d (battery low/dead if SRAM)",
               s & 1, (s >> 1) & 1);
    printf("\n");
}

/* ---- operations ------------------------------------------------------------ */

static int op_info(void)
{
    show_status();
    if (!wait_ready()) { printf("  ! card never came READY\n"); return 1; }
    read_cis();
    parse_cis(1);
    if (o_probe) {
        probe_card();
        show_probe();
    } else {
        printf("    (add /PROBE to identify the chip live)\n");
    }
    return 0;
}

static int op_read(const char *fn)
{
    FILE *f;
    unsigned long len, a, left;
    read_cis(); parse_cis(0);
    resolve_geom();                                  /* CIS size / /SIZE    */
    len = o_len ? o_len : (card_size > o_off ? card_size - o_off : 0);
    if (!len) {
        printf("  ! card size unknown - give /LEN or /SIZE\n");
        return 1;
    }
    if (card_size && o_off + len > card_size) {
        len = card_size - o_off;
        printf("  (clamped to card size: %lu bytes)\n", len);
    }
    f = fopen(fn, "wb");
    if (!f) { printf("  ! cannot create %s\n", fn); return 1; }
    printf("  READ %lu bytes @ 0x%lX -> %s\n", len, o_off, fn);
    crc_start();
    a = o_off; left = len;
    while (left) {
        unsigned n = (left > sizeof(buf)) ? sizeof(buf) : (unsigned)left;
        card_to_buf(a, buf, n);
        crc_feed(buf, n);
        if (fwrite(buf, 1, n, f) != n) {
            printf("\n  ! write error on %s (disk full?)\n", fn);
            fclose(f); return 1;
        }
        a += n; left -= n;
        progress("read", a - o_off, len);
    }
    fclose(f);
    printf("\n  done, CRC-32 %08lX\n", crc_done());
    return 0;
}

static int op_write(const char *fn)
{
    FILE *f;
    unsigned long flen, len, a, left, fb;
    long bad;
    int r;

    read_cis(); parse_cis(0);
    if (rd(0x01) & 0x10) {
        printf("  ! write-protect switch is ON - flip it and retry\n");
        return 1;
    }
    probe_card();
    if (ctype == T_UNKNOWN || ctype == T_ROM || ctype == T_SERIES1) {
        printf("  ! card is %s - cannot write (force with /TYPE if misdetected)\n",
               type_name(ctype));
        return 1;
    }
    f = fopen(fn, "rb");
    if (!f) { printf("  ! cannot open %s\n", fn); return 1; }
    fseek(f, 0L, SEEK_END); flen = (unsigned long)ftell(f); rewind(f);
    len = (o_len && o_len < flen) ? o_len : flen;
    if (!len) { printf("  ! %s is empty\n", fn); fclose(f); return 1; }
    if (card_size && o_off + len > card_size) {
        printf("  ! %lu bytes @ 0x%lX won't fit a %luK card\n",
               len, o_off, card_size >> 10);
        fclose(f); return 1;
    }
    if (ctype != T_SRAM && !o_noerase && !blk_bytes) {
        printf("  ! erase-block size unknown - give /BLK (or /NOERASE)\n");
        fclose(f); return 1;
    }

    printf("  PLAN: WRITE %s (%lu bytes) -> card @ 0x%lX\n", fn, len, o_off);
    printf("        %s", type_name(ctype));
    if (ctype != T_SRAM) {
        printf(" %s x%d", chip_name, nlanes);
        if (!o_noerase) {
            unsigned long b0 = o_off / blk_bytes,
                          b1 = (o_off + len - 1) / blk_bytes;
            printf(", erase blocks %lu-%lu of %luK (data outside the file's"
                   "\n        range inside those blocks is LOST)",
                   b0, b1, blk_bytes >> 10);
        }
        if (ctype == T_INTEL) printf(", Vpp %s", need_vpp12 ? "12V" : "5V");
    }
    printf("\n");
    if (!confirm()) { fclose(f); return 1; }

    if (ctype == T_INTEL && need_vpp12) vpp12(1);

    if (ctype != T_SRAM && !o_noerase) {
        unsigned long b0 = o_off / blk_bytes,
                      b1 = (o_off + len - 1) / blk_bytes, b;
        for (b = b0; b <= b1; b++) {
            printf("\r  erasing block %lu / %lu ", b - b0 + 1, b1 - b0 + 1);
            fflush(stdout);
            r = erase_block(b * blk_bytes);
            if (r) {
                printf("\n  ! erase failed at block %lu (code %d%s)\n", b, r,
                       (ctype == T_INTEL && r > 0 && (r & 0x08)) ?
                       " - Vpp low, try /VPP 12" : "");
                vpp12(0); fclose(f); return 1;
            }
        }
        printf("\n");
    }

    a = o_off; left = len;
    crc_start();
    while (left) {
        unsigned n = (left > sizeof(buf)) ? sizeof(buf) : (unsigned)left;
        unsigned got = fread(buf, 1, n, f), i;
        if (got == 0) break;
        crc_feed(buf, got);
        if (ctype == T_SRAM) {
            buf_to_card(a, buf, got);
        } else {
            for (i = 0; i < got; i++) {
                if (buf[i] == 0xFF) continue;        /* erased state anyway */
                r = prog_byte(a + i, buf[i]);
                if (r) {
                    printf("\n  ! program failed at 0x%lX (code %d%s)\n",
                           a + i, r,
                           (ctype == T_INTEL && r > 0 && (r & 0x08)) ?
                           " - Vpp low, try /VPP 12" : "");
                    read_array_range(o_off, a + i);
                    vpp12(0); fclose(f); return 1;
                }
            }
        }
        a += got; left -= got;
        progress("wrote", a - o_off, len);
        if (got < n) break;
    }
    printf("\n");
    read_array_range(o_off, o_off + len - 1);
    vpp12(0);
    printf("  file CRC-32 %08lX\n", crc_done());

    if (!o_noverify) {
        rewind(f);
        bad = verify_range(f, o_off, len, &fb);
        if (bad) {
            printf("  ! VERIFY FAILED: %ld mismatches, first at 0x%lX\n", bad, fb);
            fclose(f); return 1;
        }
        printf("  verify OK\n");
    }
    fclose(f);
    return 0;
}

static int op_erase(void)
{
    unsigned long len, b0, b1, b;
    int r;
    read_cis(); parse_cis(0);
    if (rd(0x01) & 0x10) {
        printf("  ! write-protect switch is ON - flip it and retry\n");
        return 1;
    }
    probe_card();
    if (ctype == T_UNKNOWN || ctype == T_ROM || ctype == T_SERIES1) {
        printf("  ! card is %s - cannot erase\n", type_name(ctype));
        return 1;
    }
    if (o_all) {
        if (!card_size) { printf("  ! card size unknown - /SIZE needed for /ALL\n"); return 1; }
        o_off = 0; len = card_size;
    } else {
        len = o_len;
        if (!len) { printf("  ! give /LEN (or /ALL for the whole card)\n"); return 1; }
    }

    if (ctype == T_SRAM) {
        unsigned long a = o_off, left = len;
        printf("  PLAN: fill SRAM 0x%lX..0x%lX with 0xFF\n", o_off, o_off + len - 1);
        if (!confirm()) return 1;
        memset(buf, 0xFF, sizeof(buf));
        while (left) {
            unsigned n = (left > sizeof(buf)) ? sizeof(buf) : (unsigned)left;
            buf_to_card(a, buf, n);
            a += n; left -= n;
            progress("filled", a - o_off, len);
        }
        printf("\n  done\n");
        return 0;
    }

    if (!blk_bytes) { printf("  ! erase-block size unknown - give /BLK\n"); return 1; }
    b0 = o_off / blk_bytes;
    b1 = (o_off + len - 1) / blk_bytes;
    printf("  PLAN: ERASE blocks %lu-%lu (%luK each) = 0x%lX..0x%lX, %s x%d%s\n",
           b0, b1, blk_bytes >> 10, b0 * blk_bytes, (b1 + 1) * blk_bytes - 1,
           chip_name, nlanes,
           (ctype == T_INTEL && need_vpp12) ? ", Vpp 12V" : "");
    if (!confirm()) return 1;
    if (ctype == T_INTEL && need_vpp12) vpp12(1);
    for (b = b0; b <= b1; b++) {
        printf("\r  erasing block %lu / %lu ", b - b0 + 1, b1 - b0 + 1);
        fflush(stdout);
        r = erase_block(b * blk_bytes);
        if (r) {
            printf("\n  ! erase failed at block %lu (code %d%s)\n", b, r,
                   (ctype == T_INTEL && r > 0 && (r & 0x08)) ?
                   " - Vpp low, try /VPP 12" : "");
            vpp12(0); return 1;
        }
    }
    read_array_mode();
    vpp12(0);
    printf("\n  done\n");
    return 0;
}

static int op_verify(const char *fn)
{
    FILE *f;
    unsigned long flen, len, fb;
    long bad;
    read_cis(); parse_cis(0);
    resolve_geom();
    f = fopen(fn, "rb");
    if (!f) { printf("  ! cannot open %s\n", fn); return 1; }
    fseek(f, 0L, SEEK_END); flen = (unsigned long)ftell(f); rewind(f);
    len = (o_len && o_len < flen) ? o_len : flen;
    printf("  VERIFY %s (%lu bytes) vs card @ 0x%lX\n", fn, len, o_off);
    bad = verify_range(f, o_off, len, &fb);
    fclose(f);
    if (bad) {
        printf("  ! %ld mismatches, first at 0x%lX\n", bad, fb);
        return 1;
    }
    printf("  match\n");
    return 0;
}

/* ---- CLI ------------------------------------------------------------------- */

static unsigned long parsenum(const char *s)
{
    char *e;
    unsigned long v = strtoul(s, &e, 0);
    if (*e == 'k' || *e == 'K') v <<= 10;
    else if (*e == 'm' || *e == 'M') v <<= 20;
    return v;
}

static void usage(void)
{
    printf("LFLASH - PCMCIA linear flash / SRAM card reader-writer (82365 PCIC @ 3E0)\n");
    printf("Usage: LFLASH [INFO|READ f|WRITE f|ERASE|VERIFY f] [options]\n");
    printf("  INFO [/PROBE]     card facts; /PROBE = live chip id (default cmd)\n");
    printf("  READ file         dump card to file (read-only, no probe)\n");
    printf("  WRITE file        erase + program + verify file onto card\n");
    printf("  ERASE             erase /LEN bytes at /OFF, or /ALL\n");
    printf("  VERIFY file       compare card to file\n");
    printf("Options: /S n socket, /OFF n, /LEN n, /SIZE n, /BLK n (nums: 0x.., K, M)\n");
    printf("  /TYPE INTEL|AMD|SRAM, /X1 /X2 lanes, /VPP 5|12, /SEG n (def D000)\n");
    printf("  /NOERASE /NOVERIFY /ALL /Y (no confirm)\n");
    printf("Supports: Intel 28F008SA-family cards (12V Vpp), AMD 29F-style, SRAM.\n");
}

#define C_INFO 0
#define C_READ 1
#define C_WRITE 2
#define C_ERASE 3
#define C_VERIFY 4

int main(int argc, char **argv)
{
    int cmd = C_INFO, i, sock, found = 0, ret = 0;
    const char *fn = NULL;

    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] == '/' || a[0] == '-') {
            a++;
            if      (!stricmp(a, "PROBE") || !stricmp(a, "P")) o_probe = 1;
            else if (!stricmp(a, "S"))    { if (i+1 < argc) o_sock = atoi(argv[++i]); }
            else if (!stricmp(a, "OFF"))  { if (i+1 < argc) o_off  = parsenum(argv[++i]); }
            else if (!stricmp(a, "LEN"))  { if (i+1 < argc) o_len  = parsenum(argv[++i]); }
            else if (!stricmp(a, "SIZE")) { if (i+1 < argc) o_size = parsenum(argv[++i]); }
            else if (!stricmp(a, "BLK"))  { if (i+1 < argc) o_blk  = parsenum(argv[++i]); }
            else if (!stricmp(a, "SEG"))  { if (i+1 < argc) o_seg  = (unsigned)strtoul(argv[++i], NULL, 16); }
            else if (!stricmp(a, "VPP"))  { if (i+1 < argc) o_vpp  = atoi(argv[++i]); }
            else if (!stricmp(a, "TYPE")) {
                if (i+1 < argc) {
                    char *t = argv[++i];
                    if      (!stricmp(t, "INTEL")) o_type = T_INTEL;
                    else if (!stricmp(t, "AMD"))   o_type = T_AMD;
                    else if (!stricmp(t, "SRAM"))  o_type = T_SRAM;
                    else { printf("unknown /TYPE %s\n", t); return 1; }
                }
            }
            else if (!stricmp(a, "X1")) o_lanes = 1;
            else if (!stricmp(a, "X2")) o_lanes = 2;
            else if (!stricmp(a, "NOERASE"))  o_noerase = 1;
            else if (!stricmp(a, "NOVERIFY")) o_noverify = 1;
            else if (!stricmp(a, "ALL")) o_all = 1;
            else if (!stricmp(a, "Y"))   o_yes = 1;
            else if (!stricmp(a, "?") || !stricmp(a, "H") || !stricmp(a, "HELP")) { usage(); return 0; }
            else { printf("unknown option: %s\n", argv[i]); usage(); return 1; }
        } else if (!stricmp(a, "INFO"))   cmd = C_INFO;
        else if (!stricmp(a, "READ"))   cmd = C_READ;
        else if (!stricmp(a, "WRITE"))  cmd = C_WRITE;
        else if (!stricmp(a, "ERASE"))  cmd = C_ERASE;
        else if (!stricmp(a, "VERIFY")) cmd = C_VERIFY;
        else if (!fn) fn = a;
        else { printf("unexpected argument: %s\n", a); usage(); return 1; }
    }
    if ((cmd == C_READ || cmd == C_WRITE || cmd == C_VERIFY) && !fn) {
        printf("that command needs a filename\n"); usage(); return 1;
    }

    printf("LFLASH 1.0 - linear flash / SRAM card reader-writer\n");

    /* PCIC sanity: identification register reads 0x8x on 82365-compatibles */
    sockoff = 0;
    if ((rd(0x00) & 0xC0) != 0x80) {
        printf("! no 82365-class PCIC found at 0x%X\n", PCIC);
        return 1;
    }
    crc_init();

    if (cmd == C_INFO) {
        for (sock = 0; sock < 2; sock++) {
            if (o_sock >= 0 && sock != o_sock) continue;
            sockoff = sock * 0x40;
            printf("=== Socket %d ===\n", sock);
            if (!open_socket()) { printf("  (no card present)\n"); continue; }
            op_info();
            close_socket();
        }
        return 0;
    }

    /* pick the socket for an operation */
    for (sock = 0; sock < 2 && !found; sock++) {
        if (o_sock >= 0 && sock != o_sock) continue;
        sockoff = sock * 0x40;
        if (open_socket()) found = 1;
    }
    if (!found) { printf("! no card found\n"); return 1; }
    printf("=== Socket %d ===\n", (int)(sockoff / 0x40));
    show_status();
    if (!wait_ready()) {
        printf("  ! card never came READY - aborting\n");
        close_socket();
        return 1;
    }

    switch (cmd) {
    case C_READ:   ret = op_read(fn);   break;
    case C_WRITE:  ret = op_write(fn);  break;
    case C_ERASE:  ret = op_erase();    break;
    case C_VERIFY: ret = op_verify(fn); break;
    }
    close_socket();
    return ret;
}
