# LINGO — a PCMCIA linear flash / SRAM card reader-writer for DOS

A DOS tool that reads, writes, erases and identifies **linear memory
PC Cards** — Intel-style linear flash cards (28F008SA family and friends),
AMD 29F-series flash cards, and battery-backed SRAM cards — directly through
an Intel **82365-class PCIC**. One small `.EXE`, no Card Services,
no Socket Services, no FTL driver.

"Linear" cards are the memory-mapped kind (CIS `DEVICE` type `FLASH`/`SRAM`),
not ATA flash: the card is a flat window of chip memory, and writing flash
means real block erases and byte programming, which this tool does itself —
including switching the socket's **Vpp** to whatever the chip wants, 12 V
included for the older Intel parts.

## What it needs

- **An 82365-class PCIC.** LINGO scans all four index ports —
  `3E0/3E2/3E4/3E6` — and checks each chip's identification register before
  using it, so a machine whose first bridge is in CardBus mode is handled:
  its sibling is found on a higher socket number. Socket `n` is the chip at
  `3E0 + (n & ~1)`, bank `(n & 1)` — the same numbering the enablers and
  CISDUMP use. (PC110, TP235, ToPIC in ExCA mode, …) No Card Services
  backend yet.
- **32 K of free upper memory at `D000`-`D7FF`** — two 16 K host windows, at
  `SEG` and `SEG+0x400`. It needs the *full* 32 K; excluding only 16 K is not
  enough. `/SEG` moves it.
  ⚠️ **If a memory manager holds that range as UMB, the window never reaches
  the card and every read comes back as zeroes** — host RAM reads `00`, where
  an erased card or an empty socket reads `FF`. The dump still completes and
  still prints a confident checksum. Exclude the range (`X=D000-D7FF` for
  JemmEx/EMM386), move it with `/SEG`, or run from a clean boot. LINGO warns
  when a dump is one repeated byte, but on an irreplaceable card check the
  CRC against a known master.
- **Two of the PCIC's five memory windows.** LINGO borrows two, preferring
  ones the controller has left disabled, and restores their registers exactly
  afterwards. If fewer than two are free it reuses ones already in use —
  still restored, but a resident driver holding a card mapped there will have
  it moved under its feet, so prefer a clean boot when Card Services or an
  enabler is loaded.

## Polite by default

In the CISDUMP tradition:

- **READ and INFO never write a byte to the card.** A raw dump is completely
  passive; chip identification (which must write ID commands) only happens
  under `/PROBE` or before WRITE/ERASE, and any bytes a probe touches on an
  SRAM card are saved and restored.
- Memory windows are **borrowed** from the controller's free windows, saved,
  and restored exactly. A card found already powered stays powered; only a
  card LINGO powered up itself is powered back down.
- WRITE shows a full plan (chip, blocks to be erased, Vpp) and asks before
  touching anything (`/Y` skips the prompt for scripted use).

## Usage

```
LINGO [INFO|READ f|WRITE f|ERASE|VERIFY f] [options]

  INFO [/PROBE]     socket + CIS facts (default command)
  /PROBE            also ask the chip itself - WRITES ID commands to the
                    card; INFO on its own never writes
  READ  file        dump card -> file (read-only)
  WRITE file        erase + program + verify file -> card
  ERASE             erase /LEN bytes at /OFF, or /ALL
  VERIFY file       compare card against file
  VPPTEST           report what the socket's Vpp switch accepts (writes nothing)

  /S n              socket 0-7 (default: first with a card)
  /OFF /LEN         range; numbers take 0x-hex and K/M suffixes
  /SIZE n           card size override (blank-CIS cards)
  /BLK n            combined erase-block size override
  /TYPE t           force INTEL / AMD / SRAM
  /X1 /X2           force chip interleave (byte lanes)
  /VPP 0|5|12       programming voltage override (0 = leave the rail alone)
  /W8               plain 8-bit window cycles only
  /W16              force word-only card handling (see below)
  /WS n             force n window wait states (0-3; default: auto-tuned)
  /NOBUF            no 0xE8 buffered writes
  /NOCRC            skip the CRC-32 on READ
  /VDIAG            trace every Vpp change (reports PCIC reg 0x02)
  /TILE             repeat the file to fill the card (WRITE and VERIFY)
  /NOERASE /NOVERIFY /ALL /SEG n /Y
```

Examples:

```
LINGO                            what's in the socket? (passive)
LINGO /PROBE                     ...and ask the chip - WRITES ID commands
LINGO READ CARD.IMG              dump the whole card (size from CIS)
LINGO READ CARD.IMG /LEN 2M      dump a blank-CIS card
LINGO WRITE IMAGE.BIN /Y         burn an image, verify, no questions
LINGO ERASE /ALL                 wipe the card
LINGO VERIFY IMAGE.BIN           is the card still the image?
```

`/TILE` writes the image over and over until the card is full, which is what
a card has to look like when it stands in for a small ROM: a ROM with only
its low address lines wired answers address *X* with `image[X mod L]`, and
some loaders check for exactly that. It reproduces it for any card size —
whole copies, then a partial tail if the image does not divide the card —
so it is not limited to power-of-two ratios, and it needs no intermediate
file. VERIFY takes `/TILE` too, reading the same wrapped stream, so a tiled
card is checked against the image it was built from. Needs `/SIZE` when the
card has no CIS to say how big it is.

READ/WRITE print a **CRC-32** of the data moved — handy for end-to-end
verification against the file on the other side of a serial link.

## Worked example: cloning an OmniBook 425 system card

The HP OmniBook 300/425/430 will not POST without its system card in the
"D drive" slot — it executes firmware from the card during boot. So the card
is dumped in **another machine's** PCMCIA slot, not in the OmniBook.

These cards have **no conventional attribute memory**: their CIS lives in
*common* memory at offset 0, so `INFO` shows no CIS and cannot work out the
size. Read the header first and take the size from the `DEVICE` tuple —
byte 8, where `(byte >> 3) + 1` counts 512 K units on these cards:

Before dumping anything irreplaceable, check `D000-D7FF` is free — see
[What it needs](#what-it-needs). A window that never reaches the card gives
you a full-size file of zeroes with a checksum under it.

```
LINGO READ HEAD.BIN /LEN 512 /SIZE 16M     grab the header
```

```
13 03 43 49 53  01 03 52 BD FF  ...  "Hewlett-Packard Co." "1.1S ABD"
^^^^^^^^^^^^^^  LINKTARGET "CIS"        ^^ BD>>3 = 23, +1 = 24 x 512K = 12 MB
```

The trailing string is the ROM code, which is the language: `ABA` US English,
`ABB` British, `ABD` German. Observed sizes: `BD` = 12 MB (`1.1S ABD` German
and `1.1S ABB` British), `9D` = 20 x 512 K = 10 MB (`1.1S ABA` US English).
Then dump it, twice:

```
LINGO READ 425.IMG /SIZE 12M               dump (donor untouched, WP on)
LINGO VERIFY 425.IMG /SIZE 12M             second pass = trustworthy master
```

Do **not** add `/PROBE` to a donor: reading is passive, identification is
not, and you do not need the chip identity to dump a card.

To write the clone, the target must be **byte-accessible** — `INFO` must say
`window: 16-bit OK`, not `WORD-ONLY card`. A word-only card can hold the
image perfectly and still hang the OmniBook's POST:

```
LINGO /PROBE                               qualify the target (writes ID cmds)
LINGO WRITE 425.IMG                        erase + program + verify
```

A 16 MB card carrying a 12 MB image is fine; the spare space is ignored.

The 430's own English card is a different generation — a 512 K FAT12 volume
whose loader requires reads past the end to **wrap**, so a single copy on a
larger card fails to boot. That one needs `/TILE`:

```
LINGO WRITE 430.IMG /TILE /SIZE 2M         fill a 2 MB flash or SRAM card
```

Full story, including which cards are eligible and why, in
[doc/OMNIBOOK.md](doc/OMNIBOOK.md).

## What it knows

- **Intel CUI flash** (28F008SA, Sharp LH28F008SA, 28F016 S-series, …):
  block erase + program with status polling. Vpp is off except around a
  program or erase, and is then asserted at the voltage the chip actually
  wants — 12 V for the older parts, 5 V otherwise. Where the chip's CFI
  advertises a write buffer, programming uses fast buffered `0xE8` bursts.
- **Chip organization, detected live**: single x8 chips, two x8 chips
  interleaved on the byte lanes, and word-organized x16 parts each get the
  correct command addressing, status masks and erase-block geometry, driven
  through 16-bit window cycles when the socket supports them (both chips of
  a pair program in parallel).
- **Word-only cards**: some cards (e.g. Intel Value Series 200) ignore `A0`
  and cannot do byte cycles at all — byte reads silently double every even
  byte and byte writes corrupt. LINGO detects this and switches everything
  to word cycles; any byte-oriented tool would trash such a card.
- **Slow cards**: window wait states are auto-tuned against stale-read
  behavior on tight back-to-back cycles (`/WS` overrides).
- **AMD-style flash** (Am29F040/080/016/017, Fujitsu, ST, …): unlock-sequence
  command set, DQ7/DQ5 polling, both x8 and x16-in-byte-mode unlock address
  layouts, single or interleaved. These parts are single-supply — an
  Am29F017 card was measured programming and erasing with Vpp at 0 V — so a
  socket with no Vpp switch at all can still write them, where a 12 V-only
  Intel part cannot be written there.
- **SRAM** cards: plain writes, battery status (BVD) reported.
- **Identification**: CIS `DEVICE`/`JEDEC` tuples, JEDEC autoselect, CFI
  query, plus overrides for cards with a blank CIS. The same probes make a
  handy card-triage instrument — address-line faults and dead cards show
  distinctive fingerprints in seconds — but they work by writing commands
  to the card, so they stay opt-in behind `/PROBE` and never run during a
  plain INFO or READ.
- Intel **Series 1** (28F010/020, pre-CUI) cards are detected and readable
  but not programmable (they need the old erase-verify algorithm).

## Caveats

- **Vpp is asserted only around a program or erase**, at the voltage that
  chip needs, and dropped again afterwards. Reading, identifying and CIS
  parsing all run with the programming rail off. If a program or erase comes
  back `Vpp low`, the socket never supplied it — `VPPTEST` and `/VDIAG` show
  what the hardware actually did.
- A freshly erased flash card has a blank CIS (all `FF`); give `/SIZE` (or
  `/LEN`) until an image with a CIS is written back.
- Multi-bank cards identified only by chip ID (no CIS) report the size of
  the first bank — pass `/SIZE` for the real capacity.

## Build

Open Watcom 1.9, 16-bit real mode, small model — `BUILD.BAT`, or:

```
wcc -ms -ox LINGO.C -fo=LINGO.obj
wlink system dos name LINGO.exe file LINGO.obj
```

## The name

**LIN**ear + **GO**, in the fleet's enabler tradition — and it speaks
every card's *lingo*: Intel CUI, AMD unlock sequences, word-only,
byte-only, slow, interleaved, FFS2, FAT12, and the OmniBook's own
mirror-check dialect. (Briefly named FLINGO, until it was pointed out
that's a dating app.)
