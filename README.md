# LFLASH — a PCMCIA linear flash / SRAM card reader-writer for DOS

A DOS tool that reads, writes, erases and identifies **linear memory
PC Cards** — Intel-style linear flash cards (28F008SA family and friends),
AMD 29F-series flash cards, and battery-backed SRAM cards — directly through
an Intel **82365-class PCIC** at `3E0h`. One small `.EXE`, no Card Services,
no Socket Services, no FTL driver.

"Linear" cards are the memory-mapped kind (CIS `DEVICE` type `FLASH`/`SRAM`),
not ATA flash: the card is a flat window of chip memory, and writing flash
means real block erases and byte programming, which this tool does itself —
including switching the socket's **Vpp to 12 V** for the older Intel chips
that need it.

## Polite by default

In the CISDUMP tradition:

- **READ and INFO never write a byte to the card.** A raw dump is completely
  passive; chip identification (which must write ID commands) only happens
  under `/PROBE` or before WRITE/ERASE, and any bytes a probe touches on an
  SRAM card are saved and restored.
- Memory windows are **borrowed** from the controller's free windows, saved,
  and restored exactly. A card found already powered stays powered; only a
  card LFLASH powered up itself is powered back down.
- WRITE shows a full plan (chip, blocks to be erased, Vpp) and asks before
  touching anything (`/Y` skips the prompt for scripted use).

## Usage

```
LFLASH [INFO|READ f|WRITE f|ERASE|VERIFY f] [options]

  INFO [/PROBE]     socket + CIS facts; /PROBE adds live chip id (default)
  READ  file        dump card -> file (read-only)
  WRITE file        erase + program + verify file -> card
  ERASE             erase /LEN bytes at /OFF, or /ALL
  VERIFY file       compare card against file

  /S n              socket 0/1 (default: first with a card)
  /OFF /LEN         range; numbers take 0x-hex and K/M suffixes
  /SIZE n           card size override (blank-CIS cards)
  /BLK n            combined erase-block size override
  /TYPE t           force INTEL / AMD / SRAM
  /X1 /X2           force chip interleave (byte lanes)
  /VPP 5|12         programming voltage override
  /NOERASE /NOVERIFY /ALL /SEG n /Y
```

Examples:

```
LFLASH /PROBE                     what's in the socket?
LFLASH READ CARD.IMG              dump the whole card (size from CIS)
LFLASH READ CARD.IMG /LEN 2M      dump a blank-CIS card
LFLASH WRITE IMAGE.BIN /Y         burn an image, verify, no questions
LFLASH ERASE /ALL                 wipe the card
LFLASH VERIFY IMAGE.BIN           is the card still the image?
```

READ/WRITE print a **CRC-32** of the data moved — handy for end-to-end
verification against the file on the other side of a serial link.

## What it knows

- **Intel CUI flash** (28F008SA, Sharp LH28F008SA, 28F016SA, …): block
  erase + byte program with status polling; Vpp 12 V switched on only during
  program/erase and restored after. Interleaved two-chip cards (x2) are
  detected automatically, including the doubled 128 K erase blocks.
- **AMD-style flash** (Am29F040/080/016, Fujitsu, ST, …): unlock-sequence
  command set, DQ7/DQ5 polling, both x8 and x16-in-byte-mode unlock address
  layouts, single or interleaved.
- **SRAM** cards: plain writes, battery status (BVD) reported.
- **Identification**: CIS `DEVICE`/`JEDEC` tuples, JEDEC autoselect, CFI
  query, plus overrides for cards with a blank CIS.
- Intel **Series 1** (28F010/020, pre-CUI) cards are detected and readable
  but not programmable (they need the old erase-verify algorithm).

## Caveats

- Needs an 82365-compatible controller at `3E0h` (PC110, TP235, ToPIC in
  ExCA mode, …). No Card Services backend yet.
- Uses host memory `SEG:0000..SEG+7FF:000F` (32 K, default `D000`) for its
  two card windows — run from a clean boot or exclude the range from your
  memory manager (`/SEG` moves it).
- A freshly erased flash card has a blank CIS (all `FF`); give `/SIZE` (or
  `/LEN`) until an image with a CIS is written back.
- Multi-bank cards identified only by chip ID (no CIS) report the size of
  the first bank — pass `/SIZE` for the real capacity.

## Build

Open Watcom 1.9, 16-bit real mode, small model:

```
wcc -ms LFLASH.C -fo=LFLASH.obj
wlink system dos name LFLASH.exe file LFLASH.obj
```

(or `BUILD.BAT` / the on-box `C:\WATCOM\BLD.BAT LFLASH`).
