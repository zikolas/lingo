/* ATTRIO.C - read / blank / restore a PC Card's ATTRIBUTE memory (82365 PCIC).
 *
 * Some cards (the Apple Newton 4MB Flash Storage Card among them) expose a
 * freely writable attribute EEPROM - plain writes, persistent across power
 * cycles. That makes the CIS repairable, forgeable, and - the reason this
 * tool exists - blankable on purpose, which is the only way to produce a
 * CIS-less card for testing how a tool behaves when a socket reads all-FF.
 *
 * ALWAYS SAVE FIRST. A blanked CIS is only recoverable from a byte-exact
 * backup, and doc/NEWTON.md records a restore pass that ran against an EMPTY
 * SOCKET and vanished into floating bus - all-FF readback was the only tell.
 * So this tool refuses to do anything without a card present and powered,
 * and verifies every write by reading it back.
 *
 * Usage:  ATTRIO SAVE file          dump LEN dense bytes to file
 *         ATTRIO BLANK [n]          write 0xFF over the first n dense bytes
 *                                   (default 4 - just enough to kill the CIS
 *                                   header; keeps the blast radius small)
 *         ATTRIO LOAD file          write file back, then verify
 *   /S n    socket (default 0)      /W hex  window segment (default D000)
 *   /LEN n  bytes for SAVE/LOAD (default 1024)
 *
 * Attribute memory implements only even host addresses: dense byte i lives at
 * window offset i*2. All counts here are DENSE bytes.
 *
 * Build: C:\WATCOM\BLD.BAT ATTRIO
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

/* Socket s lives on the chip at 0x3E0 + (s & ~1), bank (s & 1) * 0x40 - the
 * numbering LINGO, CISDUMP and the enablers all use. A bridge left in CardBus
 * mode does not answer at its index port and floats every read to 0xFF, which
 * reads back as a present, powered socket, so check the ID register first. */
#define PCIC_BASE 0x3E0
#define MAXLEN 1024

static unsigned pcic = PCIC_BASE, sockoff = 0, memseg = 0xD000;
static void wr(unsigned char i, unsigned char v){ outp(pcic, i + sockoff); outp(pcic + 1, v); }
static unsigned char rd(unsigned char i){ outp(pcic, i + sockoff); return (unsigned char)inp(pcic + 1); }
static void sel_sock(unsigned s){ pcic = PCIC_BASE + (s & ~1); sockoff = (s & 1) * 0x40; }
static int pcic_present(void){ return (rd(0x00) & 0xC0) == 0x80; }
static void dly(unsigned n){ while (n--) inp(0x80); }
#define MS(x) dly((unsigned)(x) * 1000U)

static volatile unsigned char __far *AW(void)
{ return (volatile unsigned char __far *)MK_FP(memseg, 0); }

static unsigned char sv02, sv03, sv06, svwin[6];
static int we_powered;

/* power-good can take hundreds of ms on a soft-switched bridge */
static int wait_power_good(void)
{
    int t;
    for (t = 0; t < 50; t++) { MS(10); if (rd(0x01) & 0x40) return 1; }
    return 0;
}

/* Attribute memory lags socket power by a host-specific time and the READY
 * bit asserts before it (see ~/Projects/pcmcia-cis-ff-bug.md). Gate on the
 * data holding still - NOT on it being non-FF, because a blanked card is
 * all-FF on purpose here. Floor of 300ms covers the worst host measured. */
static void settle(void)
{
    volatile unsigned char __far *p = AW();
    unsigned char a[4], b[4];
    unsigned t, k;
    for (k = 0; k < 4; k++) a[k] = p[k * 2];
    for (t = 20; t <= 5000; t += 20) {
        MS(20);
        for (k = 0; k < 4; k++) b[k] = p[k * 2];
        for (k = 0; k < 4 && a[k] == b[k]; k++) ;
        if (k == 4 && t >= 300) break;
        for (k = 0; k < 4; k++) a[k] = b[k];
    }
}

/* Attribute-memory programming is what Vpp1 is for, so a write pass raises
 * it to Vcc - but only a write pass. SAVE reads with the rail down: a tool
 * has no business putting a programming voltage on a card it is reading. */
static unsigned char vpp_sav; static int vpp_lvl = -1;
static void vpp_set(int volts)
{
    unsigned char f = (volts == 12) ? 0x0A : (volts == 5) ? 0x05 : 0x00;
    if (vpp_lvl < 0) vpp_sav = rd(0x02);
    if (vpp_lvl == volts) return;
    wr(0x02, (unsigned char)((rd(0x02) & 0xF0) | f));
    vpp_lvl = volts; MS(20);
}
static void vpp_restore(void)
{
    if (vpp_lvl < 0) return;
    wr(0x02, vpp_sav); vpp_lvl = -1; MS(10);
}

static int open_card(void)
{
    unsigned start, stop, woff;
    int i;
    if ((rd(0x01) & 0x0C) != 0x0C) { printf("! no card in socket %u\n", sockoff / 0x40); return 0; }
    sv02 = rd(0x02); sv03 = rd(0x03); sv06 = rd(0x06);
    we_powered = 0;
    if (!(rd(0x01) & 0x40)) {
        wr(0x02, 0x90);                  /* Vcc on, Vpp off until needed */
        if (!wait_power_good()) { wr(0x02, 0x00); printf("! socket never came power-good\n"); return 0; }
        wr(0x03, 0x40); MS(20);
        we_powered = 1;
    }
    for (i = 0; i < 6; i++) svwin[i] = rd(0x10 + i);
    start = memseg >> 8; stop = (memseg >> 8) + 3;
    woff  = ((unsigned)(0 - (memseg >> 8)) & 0x3FFF) | 0x4000;   /* attribute */
    wr(0x10, start & 0xFF); wr(0x11, (start >> 8) & 0x3F);
    wr(0x12, stop  & 0xFF); wr(0x13, (stop  >> 8) & 0x3F);
    wr(0x14, woff  & 0xFF); wr(0x15, (woff  >> 8) & 0xFF);
    wr(0x06, sv06 | 0x01);
    MS(20);
    settle();
    return 1;
}

static void close_card(void)
{
    int i;
    vpp_restore();
    for (i = 0; i < 6; i++) wr(0x10 + i, svwin[i]);
    wr(0x06, sv06);
    if (we_powered) { wr(0x03, sv03); wr(0x02, sv02); }
}

int main(int argc, char **argv)
{
    unsigned len = 1024, blank = 4, i, bad = 0;
    int mode = 0;                                  /* 1 save 2 blank 3 load */
    int socksel = -1; unsigned usesock = 0;
    char *fn = 0;
    unsigned char buf[MAXLEN];
    volatile unsigned char __far *p;
    FILE *f;

    for (i = 1; i < (unsigned)argc; i++) {
        char *a = argv[i];
        if      (!stricmp(a, "SAVE")  && i + 1 < (unsigned)argc) { mode = 1; fn = argv[++i]; }
        else if (!stricmp(a, "LOAD")  && i + 1 < (unsigned)argc) { mode = 3; fn = argv[++i]; }
        else if (!stricmp(a, "BLANK")) { mode = 2;
                  if (i + 1 < (unsigned)argc && argv[i+1][0] != '/') blank = (unsigned)atoi(argv[++i]); }
        else if (!stricmp(a, "/S") && i + 1 < (unsigned)argc) socksel = atoi(argv[++i]);
        else if (!stricmp(a, "/W") && i + 1 < (unsigned)argc) memseg = (unsigned)strtol(argv[++i], 0, 16);
        else if (!stricmp(a, "/LEN") && i + 1 < (unsigned)argc) len = (unsigned)atoi(argv[++i]);
    }
    if (!mode || len > MAXLEN) {
        printf("ATTRIO - PC Card attribute memory read/blank/restore\n"
               "  ATTRIO SAVE file | BLANK [n] | LOAD file  [/S n] [/W hex] [/LEN n]\n"
               "  /S n = socket 0-7 (chip 3E0+(n&~1), bank n&1); default: first with a card\n"
               "  ALWAYS SAVE before BLANK. Every write is verified by readback.\n");
        return 1;
    }
    {   /* find the socket: the named one, else the first holding a card */
        unsigned sk; int nfound = 0, got = 0;
        for (sk = 0; sk < 8 && !got; sk++) {
            if (socksel >= 0 && (int)sk != socksel) continue;
            sel_sock(sk);
            if (!pcic_present()) continue;
            nfound++;
            if ((rd(0x01) & 0x0C) == 0x0C) { got = 1; usesock = sk; }
        }
        if (!nfound) {
            printf("! no 82365-class PCIC found (scanned 3E0/3E2/3E4/3E6)\n"
                   "  A bridge in CardBus mode does not answer here - its sibling\n"
                   "  may still be in PCIC mode on a higher socket number.\n");
            return 1;
        }
        if (!got) { printf("! no card found\n"); return 1; }
        sel_sock(usesock);
        printf("socket %u (PCIC 0x%03X, bank 0x%02X)\n", usesock, pcic, sockoff);
    }
    if (!open_card()) return 1;
    p = AW();

    if (mode == 1) {
        for (i = 0; i < len; i++) buf[i] = p[i * 2];
        f = fopen(fn, "wb");
        if (!f) { printf("! cannot create %s\n", fn); close_card(); return 1; }
        fwrite(buf, 1, len, f); fclose(f);
        printf("saved %u dense attribute bytes -> %s\n", len, fn);
        printf("first 8: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
    } else if (mode == 2) {
        vpp_set(5);
        printf("about to write 0xFF over the first %u dense attribute bytes\n", blank);
        printf("(currently %02X %02X %02X %02X) - proceed? [y/N] ", p[0], p[2], p[4], p[6]);
        fflush(stdout);              /* prompt has no newline: force it out
                                        before blocking, or the operator is
                                        answering a question they cannot see */
        i = (unsigned)getche();
        printf("\n");
        if (i != 'y' && i != 'Y') { printf("aborted, nothing written\n"); close_card(); return 2; }
        for (i = 0; i < blank; i++) { p[i * 2] = 0xFF; MS(5); }
        for (i = 0; i < blank; i++) if (p[i * 2] != 0xFF) bad++;
        printf("blanked %u bytes, %u did not take\n", blank, bad);
        printf("first 8 now: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               p[0], p[2], p[4], p[6], p[8], p[10], p[12], p[14]);
    } else {
        f = fopen(fn, "rb");
        if (!f) { printf("! cannot open %s\n", fn); close_card(); return 1; }
        len = (unsigned)fread(buf, 1, MAXLEN, f); fclose(f);
        printf("restoring %u dense bytes from %s...\n", len, fn);
        vpp_set(5);
        for (i = 0; i < len; i++) {
            if (p[i * 2] != buf[i]) { p[i * 2] = buf[i]; MS(5); }
        }
        for (i = 0; i < len; i++) if (p[i * 2] != buf[i]) bad++;
        printf("restored, %u byte(s) still wrong\n", bad);
        printf("first 8: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               p[0], p[2], p[4], p[6], p[8], p[10], p[12], p[14]);
    }
    close_card();
    return bad ? 3 : 0;
}
