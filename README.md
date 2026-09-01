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

  INFO [/PROBE]     socket + CIS facts; /PROBE adds live chip id (default)
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
  /NOERASE /NOVERIFY /ALL /SEG n /Y
```

Examples:

```
LINGO /PROBE                     what's in the socket?
LINGO READ CARD.IMG              dump the whole card (size from CIS)
LINGO READ CARD.IMG /LEN 2M      dump a blank-CIS card
LINGO WRITE IMAGE.BIN /Y         burn an image, verify, no questions
LINGO ERASE /ALL                 wipe the card
LINGO VERIFY IMAGE.BIN           is the card still the image?
```

READ/WRITE print a **CRC-32** of the data moved — handy for end-to-end
verification against the file on the other side of a serial link.

## What it knows

- **Intel CUI flash** (28F008SA, Sharp LH28F008SA, 28F016 S-series, …):
  block erase + program with status polling; Vpp 12 V switched on only
  during program/erase and restored after (5 V-only parts stay at 5 V).
  Where the chip's CFI advertises a write buffer, programming uses fast
  buffered `0xE8` bursts.
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
  layouts, single or interleaved.
- **SRAM** cards: plain writes, battery status (BVD) reported.
- **Identification**: CIS `DEVICE`/`JEDEC` tuples, JEDEC autoselect, CFI
  query, plus overrides for cards with a blank CIS. The same probes make a
  handy card-triage instrument: address-line faults and dead cards show
  distinctive fingerprints in seconds.
- Intel **Series 1** (28F010/020, pre-CUI) cards are detected and readable
  but not programmable (they need the old erase-verify algorithm).

## Caveats

- Needs an 82365-compatible controller (PC110, TP235, ToPIC in ExCA mode, …).
  No Card Services backend yet. LINGO scans all four index ports —
  `3E0/3E2/3E4/3E6` — and checks each chip's identification register before
  using it, so a machine whose first bridge is in CardBus mode is handled:
  its sibling is found on a higher socket number. Socket `n` lives on the
  chip at `3E0 + (n & ~1)`, bank `(n & 1)`, which is the same numbering the
  enablers and CISDUMP use.
- **Vpp is asserted only around a program or erase**, at the voltage that
  chip needs, and dropped again afterwards. Reading, identifying and CIS
  parsing all run with the programming rail off. If a program or erase comes
  back `Vpp low`, the socket never supplied it — `VPPTEST` and `/VDIAG` show
  what the hardware actually did.
- Uses host memory `SEG:0000..SEG+7FF:000F` (32 K, default `D000`) for its
  two card windows — run from a clean boot or exclude the range from your
  memory manager (`/SEG` moves it).
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
