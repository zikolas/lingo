# OmniBook D-slot flash cards: the VS200, the PRETEC, and what the OmniBook actually wants

*Bench notes, 2026-07-24 — the night FLINGO was born. HP OmniBook 300/425/430
"D drive" card slot, tested against five PC Cards on an IBM PC110 (82365 PCIC)
and a German-ROM OmniBook.*

## The D slot is not a disk slot

The fact that frames everything else: an OmniBook of this generation **does
not POST without its system card in the D slot**. The card is not just an
application disk — the machine executes system firmware from it, in place,
during boot. A known trait of these machines, and the bench experiments
bear it out:

| Experiment | Result |
|---|---|
| Original HP card in D slot | POSTs and boots |
| Empty D slot | **No POST at all** |
| Foreign card (VS200) in D slot | **No POST** (hangs) |
| Same foreign card in a *user* PCMCIA slot | POSTs fine |

So the D slot is scanned — and fetched from — very early, byte by byte, and
anything the CPU can't read correctly there stalls the machine before video.
A card for this slot must behave *exactly* like memory.

## Anatomy of the original HP card

The reference card (HP 12 MB German system card, Windows 3.1 era) taught us
what "looking like an HP card" really means:

- **No attribute memory at all.** Attribute-space reads just alias common
  memory (every other byte). Whatever the OmniBook checks, it isn't a
  conventional attribute CIS.
- **The CIS lives in common memory at offset 0**: `CISTPL_LINKTARGET`
  (`13 03 "CIS"`), then a DEVICE tuple declaring `FLASH, 200 ns, 24 × 512 K
  = 12 MB`, HP vendor tuples ("Hewlett-Packard Co.", "1.1S ABD"), and an
  `FFS2` marker tuple.
- **Filesystem: Microsoft Flash File System 2**, holding DOS, Windows, and
  the German application set — plus whatever the BIOS itself needs.
- Write-protect switch ON from the factory. Reads don't care.

Everything identity-bearing is in common memory — which means everything
identity-bearing is **clonable** with a raw 12 MB dump and write. No
attribute-space forgery required.

## Why the Intel Value Series 200 failed

The VS200 (16 MB, "5V", eight E28F016S5 in four word-wide pairs) was our
first clone target. The clone was byte-perfect — FLINGO verified all 12 MB —
and the OmniBook still refused to POST with it.

The reason is wired into the card: **the VS200 has no byte-lane steering.
It ignores A0 on byte cycles.** Byte reads return the even byte twice; byte
writes all land on the low lane. It is a word-only card. Fine for hosts that
do 16-bit accesses (FLINGO drives it entirely with word cycles), fatal for
the OmniBook's byte-wise instruction fetches: the CPU asks for byte 5 and
receives byte 4.

Two corollaries worth remembering:

1. **No image can fix it.** The failure is electrical protocol, upstream of
   content. Word-only cards are permanently ineligible for the D slot.
2. **Byte-wise tools corrupt these cards.** Our first (byte-path) write
   AND-ed every odd byte into its even neighbour's cell. Judging by the
   pre-existing content, a previous owner's tool had done exactly the same
   thing to it years ago.

The VS200 is not junk — it's now a verified, word-driven data card for user
slots, where POST never touches it.

## Why the PRETEC Series-2 worked

The winning card: **PRETEC SERIES-2 16 MB** — sixteen Intel 28F008SA in
byte-steered pairs, 200 ns, proper attribute CIS, 128 K combined erase
blocks, 12 V Vpp for programming.

- Same silicon family HP built the original cards from, and crucially:
  **full byte access**. The OmniBook cannot tell it from real memory,
  because it *is* real memory in every access width.
- 12 MB image cloned in ~5 minutes (word-parallel programming at 12 V —
  which the PC110's socket turns out to supply), verified byte-for-byte.
- In the D slot: **POST completes, German DOS boots.** Campaign won.

Two compatibility myths this bust:

- **The attribute CIS doesn't matter.** The PRETEC proudly announces
  "PRETEC SERIES-2 16MB FLASH CARD" in attribute space. The OmniBook
  doesn't care — evidence it reads the in-common-memory CIS (or none).
- **Exact size doesn't matter.** A 16 MB card carrying a 12 MB image (whose
  embedded CIS declares 12 MB) boots fine. The spare 4 MB sits ignored —
  or available for our own second-drive experiments.

## Revision: the word-only ROM card that boots anyway

A later find forced a refinement. The **OB430's own English system card**
(2.0S, a ~400 K **FAT12** DOS volume — `OMNIBOOKROM`, IO.SYS, HP's card
tools — not FFS2 like the German 12 MB card from the OB425) turns out to be
**word-only in our socket, and its OmniBook boots it happily**. Same D slot,
three verdicts:

| Card | Access | Content | OB430 |
|---|---|---|---|
| English 2.0S factory ROM card | word-only | FAT12, 400 K | boots |
| PRETEC clone | byte-accessible | German FFS2 image | boots |
| VS200 clone | word-only | German FFS2 image (byte-identical to the PRETEC's) | hangs POST |

So byte-accessibility is **not** a slot-level absolute — the OmniBook can
evidently read cards with word cycles and mux bytes internally. The
hypothesis that fits all three results: when a card's fixed **attribute CIS
announces flash** (as the VS200's does), the firmware takes a flash-aware
path that issues **byte-wise flash commands** — scrambled on a word-only
card, wedging POST. Factory ROM cards never trigger it; byte-accessible
flash survives it. Untested prediction: the VS200 would hang even carrying
the English FAT12 image.

Practical consequence: unchanged. For *clone targets* (which are flash by
nature), **byte-accessible remains the proven recipe** — the checklist
below stands. But D-card *content* comes in at least two generations, and
the FAT12 kind is far easier to build custom images for than FFS2. The
430 also happily boots the 425's German image via the PRETEC, so images
travel across the 425/430 family.

## The D-slot compatibility checklist (for flash clone targets)

A candidate card qualifies if and only if:

1. **Byte-accessible** — the hard requirement. `FLINGO INFO` verdict line:
   `window: 16-bit OK` = candidate; `WORD-ONLY card` = permanent reject.
2. **≥ the image size** (12 MB for the standard system image).
3. **Healthy** — clean chip IDs, no address-line faults. (A bridged-A1/A2
   card we triaged read plausibly at first glance and only failed under
   FLINGO's per-lane write analysis.)
4. Comparable **speed grade** (the HP card is 200 ns; slower cards are
   untested against the OmniBook's fixed timing — one to watch).

Vpp voltage does **not** matter for the OmniBook (it only reads); it only
determines which *writer* sockets can program the card. Chip vendor doesn't
matter either — it's the card's lane wiring, not the silicon, that decides.
The same chips appear in both compliant and word-only cards.

## The procedure

```
FLINGO /PROBE              qualify the candidate (width, chips, health)
FLINGO READ  ORIG.IMG      dump the donor card (fully passive, WP on)
FLINGO VERIFY ORIG.IMG     second read pass = trustworthy master
FLINGO WRITE ORIG.IMG      one-block test first on unproven cards, then
                           erase + program + verify the full image
```

Then into the D slot. Every transfer prints a CRC-32 for end-to-end
verification across the serial link.

## Card census, night one

| Card | Verdict |
|---|---|
| HP 12 MB German system card | Donor. Dumped passively, untouched, WP on. Image = system firmware + FFS2. |
| Intel Value Series 200 16 MB | Healthy but word-only → D-slot ineligible. Data-card duty. |
| Smart Modular 20 MB (SM9FA520) | A1/A2 address lines bridged (wired-AND) — hardware-dead. Scrapped. |
| PRETEC Series-2 16 MB | **Boots the OmniBook.** The proven recipe. |
| Apple Newton 4 MB (AMD 01/3D) | Healthy, blank, byte-accessible — too small for this job. |
| 2 MB SRAM card | Battery-backed, healthy; bit-rotted DOS ghost. Parked as the D-slot lab card (`SRAM-DSLOT.md`). |
| OB430 English 2.0S ROM card | Word-only, FAT12, 400 K, HP card tools aboard (`OBCRDDRV`, `OBFDISK`, `FORMAT`, `LLREMOTE`). Boots its machine — see the Revision section. Dumped: `obrom.img`. |

## Open threads

- **The FAT12 fast lane**: the 2.0S generation proves plain FAT12 D cards
  are valid — custom images become mtools territory (HP CIS header + boot
  sector + files), no FFS2 archaeology required. HP's own `OBCRDDRV`/
  `OBFDISK`/`FORMAT` from the English card are the factory tooling.
- Test the flash-probe hypothesis: does the VS200 hang even with the
  English FAT12 image aboard?
- **FFS2 format analysis** of the master image → truly custom application
  cards, not just clones.
- The spare region above the image on oversized cards → a read-only second
  drive with our own DOS driver.
- Whether the OmniBook tolerates slower-than-200 ns cards.
- An AMD word engine in FLINGO (AMD-chip cards currently write via the slow
  byte path).
