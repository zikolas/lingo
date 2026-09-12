# OmniBook D-slot cards: what the machine wants, and how to clone one

*Bench notes for the HP OmniBook 300/425/430 "D drive" system card. Cards
were read and written on an IBM PC110 and a ThinkPad 235 (82365-class PCIC)
with LINGO, and tested in a German-ROM OmniBook 425 and an OmniBook 430.*

## The D slot is not a disk slot

An OmniBook of this generation does not POST without its system card in
the D slot. The machine executes firmware from the card, in place, during
boot. An empty slot gives no POST; a card the CPU cannot read correctly
hangs before video. The same card in a user slot is never touched at POST.

## Inside the original card

One HP card was opened. It is a mask-ROM card, not flash:

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

## What the OmniBook checks

Two image generations, two rules.

FFS2 generation (the 425's 10/12 MB cards, `1.1S`): written as-is; the card
may be larger than the image; the card must be byte-accessible. The
firmware fetches byte-wise, and a word-only card returns the even byte for
every odd address — the CPU asks for byte 5 and receives byte 4. No image
fixes that.

FAT12 generation (the 430's 512 K card, `2.0S`): the original is 512 K of
physical ROM whose reads past the end wrap, and the loader checks for that.
A single copy on a larger card reads blank past 512 K and fails. The image
must be tiled to fill the card (`LINGO /TILE`). This loader runs on the
430's own word-only ROM card, but word-only flash still fails: the VS200
carrying the tiled image does not POST. The VS200 and the 430 card fail
8-bit-mode (`A0`) byte reads identically, and the VS200 reads correctly by
the two paths a 16-bit host uses — word reads and odd bytes via `CE2#`
alone — with no extra wait states. Since POST does not write, its missing
write-lane isolation (`LINGO LANETEST`) is not the cause either. Whatever
it trips on is on the D-slot bus and not reproduced by a PCIC.

Images travel across the family: the 430 boots the 425's German FFS2 image
from a PRETEC.

What turned out not to matter, each disproved by a card that boots without
it: the attribute CIS (the PRETEC advertises itself as a PRETEC), the
write-protect state, the exact card size, and attribute space existing at
all (the SRAM lab card has none). Several nights went into attribute-space
theories before a control experiment showed the FAT12 refusals were the
mirror check.

POST does not write to the card. The original card is mask ROM, so nothing
on the boot path can depend on a write succeeding; the 512 KB SRAM card,
verified after booting the 430, is unchanged.

What remains unexplained — the AMD wall: the Apple Newton card (AMD
`01/3D`, byte-accessible, healthy) is refused with the same content,
tiling, attribute presentation and WP state as the SRAM card that boots.
The machine tells AMD flash from SRAM and Intel flash below any byte we
can present. Intel-family silicon is the only proven class. The
instrument for this is a logic analyzer on the card during POST; the
opened board makes that easy — clip a ROM's `CE#`/`OE#` and the traces into
the decoder rather than the 68-pin edge, and watch whether `WE#` or `REG#`
is ever asserted.

## Card verdicts

| Card | Access | Result |
|---|---|---|
| PRETEC Series-2 16 MB — 16 × Intel 28F008SA, byte-steered pairs, 200 ns, 12 V Vpp | 16-bit OK | Boots. FFS2 images as-is; 430 image tiled ×32. The proven recipe. |
| 2 MB SRAM card | byte-accessible | Boots the 430 with the 430 image tiled ×4. Instant-rewrite lab card (`SRAM-DSLOT.md`). |
| 512 KB SRAM card | byte-accessible | Boots the 430 with a single copy: the ROM's own size, wraps in hardware, the closest stand-in there is. |
| OB430 `2.0S` factory card | word-only | Boots its machine. 512 K ROM, wraps. |
| Intel Value Series 200 16 MB — E28F016S5 (`89/14`), x16, 4 banks | word-only; reads OK on every path, no write lane isolation | Hangs POST with FFS2 images and with the tiled 430 image. Byte-path tools corrupt it. User-slot data card. |
| Viking VPK1216T5200 24 MB — E28F016S5 (`89/14`), x16, 6 banks. Also sold as Cisco `MEM-C6K-FLC24M`, "Series 200 Compatible" | word-only | Same signature as the VS200. Tested by another OmniBook owner with the 425 ABA image: no POST. |
| Apple Newton 4 MB — AMD Am29F017 pair | 16-bit OK | Refused in every configuration (`NEWTON.md`). |
| Smart Modular 20 MB SM9FA520 | — | A1/A2 bridged. Dead. |

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

The target must report `window: 16-bit OK` and Intel-family silicon
(`89/A2` or Sharp `B0/A2`). A word-only card holds the image perfectly and
still hangs POST.

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

Two filters. A card needs both.

1. Byte-steered. Intel's Series 2 card specification put the 8/16-bit
   steering on the card, and faithful Series 2 designs inherit it. The
   Value Series dropped it: the VS200 is four unsteered word pairs. Read
   labels carefully: "Series 2 compatible" is the good family, "Series 200
   compatible" is the Value Series and fails. Pretec
   has since discontinued its dual cards and sells "8-bit only" or "16-bit
   only"; "16-bit only" is most plausibly word-only, "8-bit only" is
   untested in the D slot.
2. Intel-family silicon: 28F008SA (`89/A2`) or Sharp LH28F008SA (`B0/A2`).
   The Newton shows byte access is not enough.

Plus 12 MB or more for the 425 images, and 200 ns.

Candidates, best first. All are candidates until probed.

- PRETEC `FR2016` (also `FR2008`, `FR2004`): the proven family. `FJX016M6W`
  is a different family.
- Intel `iMC016FLSA` / `iMC020FLSA`: Series 2, the reference design.
- Series 2+ (`iMC0xxFLSP`, 28F016SA/SV): reported working; not verified
  here. This is what most cheap Cisco RSP
  and 2500 flash cards are. Series 2+ is not one chip: `28F016SA` is 12 V
  Vpp, `28F016SV` is 5/12 V SmartVoltage. Confirm the ID and Vpp on the
  first one to arrive and correct LINGO's table.
- Centennial `FLxxM-20-11138-xx`: the `-11138` drawing number is sold as
  "Series 2" in the 2 MB size and spans 2-20 MB, which is the 28F008SA
  family's range; Centennial's AMD D-series cards carry `-11113` instead.
  The 20 MB `FL20M-20-11138` clears every 425 image. Unverified.
- Sharp-branded cards: Series 2 compatibles.
- Smart Modular `SM9FLA` = Series 2 (cross-referenced to Centennial
  `-11138`); `SM9FA5xx` = the Cisco 7500 RSP 20 MB cards, Series 2+ class
  (the dead `SM9FA520` was a bad unit, not a bad family); `SM9FCSC` = the
  Cisco 1600/1700 cards, 2-16 MB, Intel family, silicon unconfirmed.
  `SM9AMD` is AMD, `SM9DRS` is DRAM — neither is a candidate.
- Simple Technology, Kingston: made compatibles; part-specific.

Pretec's own prefixes decode the chip directly: `F62` = Series II
(28F008SA, the good one), `F63` = Series II+, `FN5` = Series 100 (28F016S5,
the VS200 class), `F6C`/`F6D` = AMD C/D, `F61` = Series I. A `-08` or `-16`
suffix means a single-width card; the discontinued dual cards carry no
suffix. `FR2016` predates this scheme and is the proven card. `FJX016M6W`
is not in Pretec's scheme and has no web presence — unplaceable, probe only.

Avoid: Intel Value Series 100/200 and anything "Series 200 compatible" —
Viking `VPK1216T5200`, Cisco `MEM-C6K-FLC24M` (word-only, tested);
AMD-based cards — AMD `AmC0xxFLKA`, Fujitsu MBM29F, the Newton (the wall);
StrataFlash (`28F128J3`, `28F640J3`).

On arrival:

```
LINGO /PROBE
    window: 16-bit OK             byte-steered
    PROBE: ... id 89/A2 or B0/A2  Intel-family silicon
    WORD-ONLY card                reject (the VS200 fails both generations)
    id 01/xx                      AMD: reject for the D slot
```

Then a 425 image in the D slot is the test.

## Open threads

- First custom FAT12 D card: HP CIS header + own FAT12 volume + firmware
  at the original offsets, `/TILE`, written with LINGO. The original goal.
- First custom 1.1S card: a read-only FFS2 volume; format analysis of the
  German master.
- The VS200 and the AMD wall may be one thing. Every card that boots —
  mask ROM, PRETEC, both SRAMs — is bare memory plus decode; both that fail
  carry a controller ASIC, and a controller can drive `WAIT#` where bare
  cards never do. A PCIC honours `WAIT#`; a ROM-oriented D slot may not.
  One scope probe on pin 59 during a PC110 read of each card would show
  it. Failing that, the logic analyzer on the opened card.
- Logic analyzer on the opened card during POST: the AMD wall.
- A Series 2+ card in the D slot, to verify the report.
- The spare region above the image on oversized cards as a second drive.
- Slower-than-200 ns cards.
- LINGO: `ATTR` commands (`ATTRIO` covers it), an AMD word engine.
