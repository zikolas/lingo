# OmniBook D-slot flash cards: the VS200, the PRETEC, and what the OmniBook actually wants

*Bench notes, 2026-07-24 — the night LINGO was born. HP OmniBook 300/425/430
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
first clone target. The clone was byte-perfect — LINGO verified all 12 MB —
and the OmniBook still refused to POST with it.

The reason is wired into the card: **the VS200 has no byte-lane steering.
It ignores A0 on byte cycles.** Byte reads return the even byte twice; byte
writes all land on the low lane. It is a word-only card. Fine for hosts that
do 16-bit accesses (LINGO drives it entirely with word cycles), fatal for
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

1. **Byte-accessible** — the hard requirement. `LINGO INFO` verdict line:
   `window: 16-bit OK` = candidate; `WORD-ONLY card` = permanent reject.
2. **≥ the image size** (12 MB for the standard system image).
3. **Healthy** — clean chip IDs, no address-line faults. (A bridged-A1/A2
   card we triaged read plausibly at first glance and only failed under
   LINGO's per-lane write analysis.)
4. Comparable **speed grade** (the HP card is 200 ns; slower cards are
   untested against the OmniBook's fixed timing — one to watch).

Vpp voltage does **not** matter for the OmniBook (it only reads); it only
determines which *writer* sockets can program the card. Chip vendor doesn't
matter either — it's the card's lane wiring, not the silicon, that decides.
The same chips appear in both compliant and word-only cards.

## The procedure

The donor comes **out of the OmniBook** to be read — the machine will not
POST without it, so there is no running system to dump it from.

**Sizing comes first, and `INFO` cannot help.** An HP card has no attribute
CIS, so LINGO shows no CIS and no size. Read the header and decode the
`DEVICE` tuple in *common* memory at offset 0 — byte 8, `(byte >> 3) + 1`
units of 512 K on these cards. Observed: `BD` = 24 x 512 K = 12 MB (German
`1.1S ABD` German, `1.1S ABB` British), `9D` = 20 x 512 K = 10 MB (`1.1S ABA`
US English). The trailing ROM code is the language: `ABA` US English, `ABB`
British, `ABD` German.
Guessing the size gives a truncated or padded file that still verifies
against itself.

```
LINGO READ  HEAD.BIN /LEN 512 /SIZE 16M   header first, for the size byte
LINGO READ  ORIG.IMG /SIZE 12M            dump the donor (passive, WP on)
LINGO VERIFY ORIG.IMG /SIZE 12M           second read pass = real master
LINGO /PROBE                              qualify the TARGET (writes ID cmds)
LINGO WRITE ORIG.IMG                      erase + program + verify
```

No `/PROBE` on the donor — reading is passive, identification writes ID
commands, and the chip identity is not needed to dump a card.

For a **FAT12-generation** image (the 430's 512 K English card) add `/TILE`,
which mirrors it across the whole card in one pass — see the third revision
below for why that is required:

```
LINGO WRITE 430.IMG /TILE /SIZE 2M        fill a 2 MB flash or SRAM card
LINGO VERIFY 430.IMG /TILE /SIZE 2M       check the whole card against it
```

Then into the D slot. Every transfer prints a CRC-32 for end-to-end
verification across the serial link.

⚠️ **Check the memory manager before dumping anything irreplaceable.** LINGO
maps 32 K of upper memory for its card windows (default `D000`). If a memory
manager holds that range as UMB, the window never reaches the card and the
whole dump reads back as **zeroes** — and it completes normally, printing a
confident CRC-32. This happened here: 12 MB of nothing, CRC `01FB2CCD`,
which is exactly the CRC of that many zero bytes. Exclude the full 32 K
(`X=D000-D7FF` — 16 K is not enough), move it with `/SEG`, or dump from a
clean boot. LINGO 1.8 and later flag a dump that is one repeated byte, but
the surest check is the CRC against a known master.

## Card census, night one

| Card | Verdict |
|---|---|
| HP 12 MB German system card | Donor. Dumped passively, untouched, WP on. Image = system firmware + FFS2. |
| HP 10 MB US English card (`1.1S ABA`) | Donor. Same structure, 10 MB — size byte `9D` where the German reads `BD`. |
| HP 12 MB British card (`1.1S ABB`) | Donor. Dumped 2026-09-08; cloned to a PRETEC that boots. |
| Intel Value Series 200 16 MB | Healthy but word-only → D-slot ineligible. Data-card duty. |
| Smart Modular 20 MB (SM9FA520) | A1/A2 address lines bridged (wired-AND) — hardware-dead. Scrapped. |
| PRETEC Series-2 16 MB | **Boots the OmniBook.** The proven recipe. |
| Apple Newton 4 MB (AMD 01/3D) | Healthy, byte-accessible, writable attr EEPROM — but refused by the D slot in every configuration (see `NEWTON.md`). Attr restored to factory; carries a tiled English image awaiting erase. |
| 2 MB SRAM card | **Boots the OB430** with the English image tiled ×4 — the instant-rewrite D-slot lab card (`SRAM-DSLOT.md`). |
| OB430 English 2.0S ROM card | Word-only, FAT12, 400 K, HP card tools aboard (`OBCRDDRV`, `OBFDISK`, `FORMAT`, `LLREMOTE`). Boots its machine. Dumped: `obrom.img`. |

### The masters

Each was read twice and the second pass verified against the first, so the
CRC-32 below is what a correct re-dump must produce. They are the reference
for "did this dump actually work" — a read that never reaches the card still
completes and still prints a checksum.

| Image | Machine / ROM | Size | CRC-32 |
|---|---|---|---|
| `425-ABD.IMG` | 425 German `1.1S ABD` | 12 MB | `035C1680` |
| `425-ABA.IMG` | 425 US English `1.1S ABA` | 10 MB | `9E447D28` |
| `425-ABB.IMG` | 425 British `1.1S ABB` | 12 MB | `096B063B` |
| `430.IMG` | 430 `2.0S`, FAT12 | 512 K | `77794F28` |

The FFS2-generation images (12/10 MB) are written as-is. The 430's 512 K
FAT12 image needs `/TILE`.

## Second revision: the acceptance mechanism resists identification

Follow-up experiments (see `NEWTON.md` and `SRAM-DSLOT.md`) pushed further
and failed further: a byte-accessible SRAM card carrying the complete
bootable 512 K image was ignored (its attribute space is an unwritable
void), and the Newton AMD card was refused through three escalations
culminating in an attribute presentation **byte-identical to the OB430
card's own** plus write-protect asserted. At this point the suspects were
deeper attribute walks or physical probing — until the third revision
below found the real variable hiding in plain sight.

## Third revision: the mirror check (the FAT12-generation answer)

The breakthrough came from a control experiment that "should" have worked:
the **PRETEC — the proven-bootable card — carrying the English 512 K image
did not POST.** Same card that boots the German 12 MB FFS2 image; a single
copy of the FAT12-generation image; refused. For the first time, evidence
that **image content participates in acceptance**.

The theory that fits: the original English card is 512 K of physical ROM
whose reads past the end **wrap** — address 512 K reads as address 0. The
FAT12-generation loader evidently *checks* for that behavior. A 16 MB card
with one copy of the image reads blank past 512 K and fails the check.

The fix is content, not hardware: **tile the image to fill the card.**
32 copies across the PRETEC's 16 MB make every read at every offset return
exactly what a wrapping 512 K ROM would return.

**LINGO does this itself since 1.9: `/TILE`.** It streams the image straight
from the source, so there is no scratch file the size of the card, and it is
not limited to power-of-two ratios the way the old `COPY /B` doubling
(512 K → 1 M → 2 M → 4 M → 8 M → 16 M) was — what is being emulated is
`image[X mod L]`, so any card size works, whole copies then a partial tail.
VERIFY takes `/TILE` too. Proven on the 2 MB SRAM card: the tiled stream came
back CRC-32 `CA0C097D`, byte-identical to the `430-T2.IMG` built the old way,
and the card boots.

Results, in order:

| Experiment | Result |
|---|---|
| PRETEC + English image, single copy | no POST |
| PRETEC + English image **tiled ×32** | **boots** |
| SRAM 2 MB + English image **tiled ×4** | **boots** — despite blank, unwritable attribute space |
| Newton 4 MB + English image tiled ×8 | no POST |
| Newton, tiled ×8, attribute space blanked to match the SRAM profile | no POST |

The SRAM boot is the theory-killer for everything that came before: a card
with **no attribute space at all** boots, so the attribute-based theories
(v4–v6.1) were red herrings top to bottom. The single-copy refusals of the
SRAM and Newton cards had been the mirror check all along.

**The rules as now known:**

- **FFS2 generation** (German-style): image written as-is; no mirror
  check; card may be larger than the image. Byte-accessible flash proven.
- **FAT12 generation** (English-style): image **tiled to fill the card**;
  the loader runs happily on word-only cards (its own ROM card is one),
  so this generation should even suit word-only flash — untested but
  predicted (see open threads).
- Attribute space: irrelevant. Write-protect: irrelevant.

**What remains unexplained — the AMD wall**: the Newton card, with
content, attribute space, byte-accessibility and WP state all equalized
against the booting SRAM card, is still refused. The machine distinguishes
AMD flash from SRAM below the level of any byte we can present. Leading
candidate: the scan performs a write-and-readback (SRAM answers, flash
ignores) and the write-ignoring path gates on something the Newton fails —
READY/WAIT behavior, BVD wiring, sense pins. The definitive instrument is
a **logic analyzer on the D-slot bus during POST**, which would show the
scan sequence outright.

## Open threads

- **Build the first custom FAT12 D card** — every ingredient proven: HP
  CIS header + own FAT12 volume (mtools) + firmware blob at original
  offsets, tiled to fill the card, written with LINGO. The original
  campaign goal, now recipe work.
- **VS200 + English image tiled ×32**: prognosis upgraded to *good* — the
  FAT12 loader provably runs on word-only cards (its own ROM card is one),
  and the VS200's hang happened with the byte-reading FFS2 path. If it
  boots, every card class in the drawer has a working recipe.
- **The logic-analyzer expedition**: capture the D-slot bus during POST to
  identify how the machine distinguishes AMD flash from SRAM (the last
  unexplained refusal).
- **FFS2 format analysis** of the German master image → custom application
  cards for the 1.1S generation too.
- LINGO: first-class `ATTR READ/WRITE` commands (the `ATTRIO` probe covers
  this for now), and the AMD word engine. *(`/TILE` shipped in 1.9.)*
- The spare region above the image on oversized cards → a read-only second
  drive with our own DOS driver.
- Whether the OmniBook tolerates slower-than-200 ns cards.
- An AMD word engine in LINGO (AMD-chip cards currently write via the slow
  byte path).
