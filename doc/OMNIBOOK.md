# OmniBook D-slot cards: what the machine wants, and how to clone one

*Bench notes for the HP OmniBook 300/425/430 "D drive" system card. Cards
were read and written on an IBM PC110 and a ThinkPad 235 (82365-class PCIC)
with LINGO, and tested in a German-ROM OmniBook 425 and an OmniBook 430.
The D-slot bus itself was measured on the 425 through a PCMCIA extender
with a meter and a logic analyser.*

## The D slot is not a disk slot

An OmniBook of this generation does not POST without its system card in
the D slot. The machine executes firmware from the card, in place, during
boot. An empty slot gives no POST; a card that does not answer hangs
before video. The same card in a user slot is never touched at POST.

## Inside the original card

An HP card photographed opened — one from the wild, not ours — is a
mask-ROM card, not flash:

- Five Sharp `LH537xxx` mask ROMs, date code week 19 of 1993, each with a
  different sequential custom part number (`YLS43820A`…`60A`) — one mask
  per chip. The board has six positions on the front and more unpopulated
  footprints on the back; five populated is consistent with the 10 MB card,
  six with the 12 MB.
- One glob-top die on the reverse: the decoder. No other logic.
- A spring contact at the rear edge for the write-protect slider.
- No attribute EEPROM. The decoder does not decode `REG#`, so attribute
  reads alias common memory (every other byte).

What that means for the images:

- The CIS lives in common memory at offset 0: `CISTPL_LINKTARGET`
  (`13 03 "CIS"`), a DEVICE tuple, HP strings (`Hewlett-Packard Co.`,
  `1.1S ABD`), an FFS2 marker.
- The DEVICE tuple says `FLASH, 200 ns`. That is a driver convention — it
  makes the Microsoft FFS2 driver mount the volume — not the silicon.
- The 1.1S cards hold an FFS2 volume as a read-only snapshot. A custom 1.1S
  image only has to be a valid read-only FFS2 volume.
- The 425 ROM code is the language: `ABA` US English, `ABB` British, `ABD`
  German.

Everything identity-bearing is in common memory, so a raw dump and write
clones the card.

## Two image generations, two rules

FFS2 generation (the 425's 10/12 MB cards, `1.1S`): written as-is; the card
may be larger than the image.

FAT12 generation (the 430's 512 K card, `2.0S`): the original is 512 K of
physical ROM whose reads past the end wrap, and the loader checks for that.
A single copy on a larger card reads blank past 512 K and fails. The image
must be tiled to fill the card (`LINGO /TILE`).

Images travel across the family: the 430 boots the 425's German FFS2 image
from a cloned flash card.

## The masters

Each read twice, the second pass verified against the first. A correct
re-dump must produce these; a read that never reaches the card completes
and prints a checksum too.

| Image | ROM | Size | CRC-32 |
|---|---|---|---|
| `425-ABD.IMG` | 425 German `1.1S ABD` | 12 MB | `035C1680` |
| `425-ABA.IMG` | 425 US English `1.1S ABA` | 10 MB | `9E447D28` |
| `425-ABB.IMG` | 425 British `1.1S ABB` | 12 MB | `096B063B` |
| `430.IMG` | 430 `2.0S`, FAT12 | 512 K | `77794F28` |

## The cards on the bench

| Card | Silicon | Access |
|---|---|---|
| PRETEC Series-2 16 MB | 16 × Intel 28F008SA (`89/A2`), byte-steered pairs, 200 ns, 12 V Vpp | 16-bit OK |
| 2 MB SRAM card | SRAM, wraps at 2 MB | byte-accessible |
| 512 KB SRAM card | SRAM, wraps at 512 K | byte-accessible |
| OB430 `2.0S` factory card | 512 K mask ROM, wraps | word-only |
| Intel Value Series 200 16 MB ("VS200") | 8 × E28F016S5 (`89/14`), x16, 4 banks, controller ASIC | word-only; reads OK on every path, no write lane isolation |
| Viking VPK1216T5200 24 MB | E28F016S5 (`89/14`), x16, 6 banks. Also sold as Cisco `MEM-C6K-FLC24M`, "Series 200 Compatible". Another OmniBook owner's card. | word-only |
| Apple Newton 4 MB | AMD Am29F017 pair (`01/3D`), controller ASIC | 16-bit OK |
| Smart Modular 20 MB SM9FA520 | 10 × Sharp LH28F016SC, Smart controller ASIC | A1/A2 bridged, dead |

"16-bit OK" and "word-only" are LINGO's `INFO` verdicts on how the card
answers byte cycles. They decide how LINGO drives the card; whether they
matter to the OmniBook is answered below.

## What the OmniBook checks

### The bus, measured

The D slot was measured on a 425 through a PCMCIA extender: a meter on
the control pins with the machine off, then a logic analyser on the bus
with the PRETEC in the slot, armed before power-on. It is a ROM bus, not a
PC Card socket.

| Pin | Finding |
|---|---|
| `OE#` (9) | tied to ground. Output enable is permanently asserted. |
| `WE#` (15), `REG#` (61) | tied to Vcc. No write cycle and no attribute cycle can ever happen. |
| `WAIT#` (59) | open. No pull-up, no pull-down, nothing listening. |
| `RESET` (58) | not driven. 0 V bare, 5 V through 1 kΩ to Vcc with no card in. |
| Vpp1, Vpp2 | 5 V from power-on, on every OmniBook measured. |

On the analyser every access is a 16-bit word read, `CE1#` and `CE2#`
both low, `CE#` low for 300 ns, 60 ns between cycles. Odd addresses appear
with `A0` high and both `CE#` low, which is still a word cycle. The
analyser's record begins at the first `CE1#` high it sees, 40 ns before
the first read, so the rail's timing is not in it; from that first cycle
onward the traffic is code fetch, sequential words with jumps, starting
at an entry point rather than at the CIS. There are no byte cycles by
either byte path, no writes, no attribute cycles, and `RESET` never moves.

Two consequences fall straight out of the copper. The D drive can never be
modified in place, on any silicon at any Vpp, because `WE#` never goes
low; writable flash volumes belong in the user slots. And the attribute
CIS can never be read there, because `REG#` never goes low.

### The reset wall

The slot never drives `RESET`. HP's mask-ROM cards have no reset input, so
the pin was left unconnected. A card with a controller chip does have one,
and with nothing on the line it drifts to Vcc on leakage alone: the VS200
reads 4.94 V there, and 2.1 mV through 1 kΩ to ground, so its own pull is
in the megohm range. The controller sits in reset for ever and the card
never answers. That was the hang before video from every controller card.

The fix is one resistor from `RESET` to ground, anywhere from 1 kΩ to
100 kΩ. On the extender it took the VS200 from no POST to booting the
tiled 430 image and then the ABA FFS2 image, and the PRETEC boots
unchanged with it in place. The control that found it: a meter on volts
across `RESET` and ground is a 10 MΩ load and the machine hangs; the same
meter on ohms is a low-impedance path and the machine POSTs.

Where the resistor can live:

- Inside the OmniBook, from the D-slot connector's `RESET` pin to ground.
  Fixes that machine for every card.
- Inside a card, from its `RESET` pin to ground. Fixes that card for every
  OmniBook, and is the only option for a card sent to someone else.

Bare cards, mask ROM, PRETEC and both SRAMs, have no reset input and are
unaffected either way.

### What turned out not to matter

Each disproved by a card that boots without it: the attribute CIS (the
PRETEC advertises itself as a PRETEC, and `REG#` is tied high so attribute
space is never read), the write-protect state, the exact card size,
attribute space existing at all (the 2 MB SRAM card has none), and byte
access (the VS200). Several nights went into attribute-space theories
before a control experiment showed the FAT12 refusals were the mirror
check, and two months into byte-access theories before the bus was
measured.

Byte access is not a requirement. This document said for two months that
the firmware fetches byte-wise and a word-only card returns the wrong
byte. The bus says otherwise, and the VS200, a word-only card, boots both
generations once its real problem is fixed. Word-only matters to LINGO's
byte-path tools, not to the OmniBook.

POST does not write to the card, and cannot: `WE#` is tied to Vcc. The
original card is mask ROM, the 512 KB SRAM card verified unchanged after
booting the 430, and the hardware agrees.

### The AMD case

The Apple Newton card (AMD `01/3D`) still does not POST with the reset
pull-down fitted. It carries its own pull-down on `RESET` and reads 0 V
there with nothing added, so the reset wall was never its problem, and its
image verifies on the PC110. This is a second mechanism, not the same one,
and it is parked: one AMD card in a niche use. The candidates and the next
measurements are in the open threads.

## Card verdicts

| Card | Result |
|---|---|
| PRETEC Series-2 16 MB | Boots. FFS2 images as-is; 430 image tiled ×32. The proven recipe. |
| 2 MB SRAM card | Boots the 430 with the 430 image tiled ×4. Instant-rewrite lab card (`SRAM-DSLOT.md`). |
| 512 KB SRAM card | Boots the 430 with a single copy: the ROM's own size, wraps in hardware, the closest stand-in there is. |
| OB430 `2.0S` factory card | Boots its machine. 512 K ROM, wraps. |
| VS200 | Boots both generations with `RESET` pulled down: the tiled 430 image, then the ABA FFS2 image. Without the pull-down it sits in reset and hangs POST. Byte-path tools corrupt it; LINGO handles it. |
| Viking 24 MB | Same silicon and signature as the VS200. Tested by its owner with the 425 ABA image and no pull-down: no POST. Expected to boot with the reset fix; unverified. |
| Apple Newton 4 MB | Refused in every configuration, with and without the reset pull-down. Pulls its own `RESET` down. A second mechanism, parked (`NEWTON.md`). |
| Smart Modular 20 MB SM9FA520 | Never reached the slot: A1/A2 bridged. |

## Cloning a card

The donor comes out of the OmniBook; there is no running system to dump
it from.

Size first. `INFO` shows no CIS on an HP card. Read the header and decode
byte 8 of the DEVICE tuple: `(byte >> 3) + 1` units of 512 K. `BD` = 12 MB
(ABD, ABB); `9D` = 10 MB (ABA). A guessed size gives a truncated or padded
file that verifies against itself.

```
LINGO READ  HEAD.BIN /LEN 512 /SIZE 16M   header, for the size byte
LINGO READ  ORIG.IMG /SIZE 12M            dump the donor (passive, WP on)
LINGO VERIFY ORIG.IMG /SIZE 12M           second pass = master
LINGO /PROBE                              qualify the TARGET (writes ID cmds)
LINGO WRITE ORIG.IMG                      erase + program + verify
```

No `/PROBE` on the donor: reading is passive, the probe writes ID
commands, and the chip identity is not needed to dump. (A mask-ROM donor
ignores writes anyway; keep the habit.)

The target needs Intel-family silicon and room for the image. Word-only
cards (`WORD-ONLY card` in `INFO`) are fine: LINGO writes them through
word cycles and the OmniBook only ever reads words. A card with a
controller chip, the VS200 and its relatives, also needs the D-slot reset
fix, one resistor, see "The reset wall". The one AMD card tried does not
boot for a reason not yet found.

For the 430 image, tile it:

```
LINGO WRITE 430.IMG /TILE /SIZE 2M        fill a 2 MB flash or SRAM card
LINGO VERIFY 430.IMG /TILE /SIZE 2M       check the whole card against it
```

Check the memory manager before dumping anything irreplaceable. LINGO maps
32 K of upper memory at `D000`; if a memory manager holds it as UMB the
window never reaches the card and the dump is zeroes — completing normally,
with a checksum. It happened here: 12 MB of nothing, CRC `01FB2CCD`, the
CRC of that many zero bytes. Exclude `D000-D7FF` (16 K is not enough), use
`/SEG`, or dump from a clean boot. LINGO 1.8+ flags a one-byte dump; the
CRC against a master is the real check.

## Finding more cards

Three filters.

1. Intel-family silicon. Proven in the D slot: 28F008SA (`89/A2`, the
   PRETEC) and 28F016S5 (`89/14`, the VS200, with the reset fix). Reported:
   Series 2+ (28F016SA/SV). The one AMD card tried fails for a reason not
   yet found, so AMD is not a candidate until one boots.
2. Size: 10 MB or more for the ABA image, 12 MB for ABB and ABD. Any size
   for the 430 image, tiled.
3. 200 ns.

Byte steering, once the first filter here, is not a requirement. "Series 2
compatible" versus "Series 200 compatible", once called a one-character
trap, only tells you which Intel chip is inside; both boot. What does
matter is whether the card carries a controller chip, and that needs the
reset fix. It is not visible from a listing: if a card hangs before video,
fit the resistor before blaming the card.

Candidates. All are candidates until probed.

- PRETEC `FR2016` (also `FR2008`, `FR2004`): the proven family. `FJX016M6W`
  is a different family.
- Intel `iMC016FLSA` / `iMC020FLSA`: Series 2, the reference design.
- Intel Value Series 100/200 and "Series 200 compatible" cards, Viking
  `VPK1216T5200`, Cisco `MEM-C6K-FLC24M`: the VS200 class, 28F016S5,
  plentiful and cheap. Need the reset fix.
- Series 2+ (`iMC0xxFLSP`, 28F016SA/SV): reported working; not verified
  here. This is what most cheap Cisco RSP and 2500 flash cards are. Series
  2+ is not one chip: `28F016SA` is 12 V Vpp, `28F016SV` is 5/12 V
  SmartVoltage. Confirm the ID and Vpp on the first one to arrive and
  correct LINGO's table. These cards carry a controller; expect to need
  the reset fix.
- Centennial `FLxxM-20-11138-xx`: the `-11138` drawing number is sold as
  "Series 2" in the 2 MB size and spans 2-20 MB, which is the 28F008SA
  family's range; Centennial's AMD D-series cards carry `-11113` instead.
  The 20 MB `FL20M-20-11138` clears every 425 image. Unverified.
- Sharp-branded cards: Series 2 compatibles.
- Smart Modular `SM9FLA` = Series 2 (cross-referenced to Centennial
  `-11138`); `SM9FA520-C7500S` = the Cisco 7500 RSP 20 MB card, which
  Cisco's own install note specifies as Intel Series 2+ (the dead `SM9FA520`
  was a bad unit, not a bad family; its board carries a Smart controller,
  so expect the reset fix); `SM9FCSC` = the Cisco 1600/1700 cards, 2-16 MB,
  Intel family, silicon unconfirmed. `SM9AMD` is AMD, `SM9DRS` is DRAM,
  neither is a candidate.
- Simple Technology, Kingston: made compatibles; part-specific.

Pretec's own prefixes decode the chip directly: `F62` = Series II
(28F008SA), `F63` = Series II+, `FN5` = Series 100 (28F016S5, the VS200
class), `F6C`/`F6D` = AMD C/D, `F61` = Series I. A `-08` or `-16` suffix
means a single-width card; the discontinued dual cards carry no suffix.
`FR2016` predates this scheme and is the proven card. `FJX016M6W` is not in
Pretec's scheme and has no web presence: unplaceable, probe only.

Avoid: AMD-based cards until one boots (AMD `AmC0xxFLKA`, Fujitsu MBM29F,
the Newton); StrataFlash (`28F128J3`, `28F640J3`), untested; DRAM cards.

On arrival:

```
LINGO /PROBE
    PROBE: ... id 89/xx or B0/xx   Intel-family silicon: candidate
    WORD-ONLY card                 fine, LINGO uses word cycles
    id 01/xx                       AMD: unproven in the D slot
```

Then a 425 image in the D slot is the test. A hang before video means the
reset fix first, then judge.

## Open threads

- The permanent reset fix: a resistor inside a 425, D-slot connector
  `RESET` to ground, or inside a card. The extender version is proven;
  neither permanent form has been built.
- The AMD case. The Newton card pulls its own `RESET` down, so it may need
  an actual reset pulse rather than a level: a capacitor from `RESET` to
  Vcc with the pull-down gives one at power-up. That only works if the
  card's Vcc is up well before the first read; if the slot is switched
  and read within a cycle, the pulse overlaps the first fetches. The other
  suspects are `WAIT#` from its controller, invisible until the pin is
  pulled up, and `OE#` tied low, which would need the extender's `OE#` cut
  and driven from `CE1#`. Next measurement: a POST capture with the data
  bus on the analyser, PRETEC then Newton, first bytes side by side.
- Whether the D slot's Vcc is switched on by the firmware or up from
  power-on: a meter on the extender's `VCC` during power-on with no card.
  The capture cannot say; its record starts 40 ns before the first read.
- Controller cards without the fix: the Viking/Cisco 24 MB card and the
  Series 2+ cards are expected to need it. Verify on the first one.
- First custom FAT12 D card: HP CIS header + own FAT12 volume + firmware
  at the original offsets, `/TILE`, written with LINGO. The original goal.
- First custom 1.1S card: a read-only FFS2 volume; format analysis of the
  German master. Enlarging the volume to fill a bigger card is the same
  analysis.
- The spare region above the image on oversized cards as a second drive.
- Slower-than-200 ns cards.
- LINGO: `ATTR` commands (`ATTRIO` covers it), an AMD word engine.
