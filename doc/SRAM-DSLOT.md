# SRAM card as an OmniBook D card — dead, buried, and resurrected

*2026-07-24 idea; first execution 2026-07-25 failed; resurrected the same
night by the mirror-check discovery. **The lab is operational.***

## Resurrection: it boots

The original failure below was misdiagnosed. The real cause was the
**mirror check** (see `OMNIBOOK.md`, "What the OmniBook checks"): the
FAT12-generation loader requires reads past the image's declared 512 K to
wrap back to the image, as they do on the original ROM card. The first
attempt carried a *single* copy — blank past 512 K — and failed the check.
Attribute space had nothing to do with it.

With the English image **tiled ×4** to fill the card (`T2.IMG`), the SRAM
card **boots the OB430** — blank, unwritable attribute space and all.

The dream from the original pitch is therefore real: **a D card that
rewrites in ~15 seconds, wear-free.** Iterate custom images at interactive
speed: edit on the Mac or box, `LINGO WRITE`, walk it over. The only
caveats are the battery dependency (lab tool, not a keeper card) and the
tiling requirement (any custom image must fill the card with repeats —
trivial with `COPY /B` doubling).

The account below is preserved as a record of the misdiagnosis — a good
example of a confident theory built on a confounded experiment.

## Outcome

The experiment ran with a stronger payload than the trimmed-image plan
below: the OB430's own **complete 512 K English system card image**
(`OBROM.IMG` — FAT12 + firmware, proven bootable, no truncation guesswork,
and its CIS even declares device type *SRAM*). Written to the 2 MB SRAM
card and verified byte-exact. **The OB430 does not POST with it** — WP on
or off — and a post-attempt verify showed the machine wrote nothing: it
looked and silently declined, exactly as if the slot were empty.

Diagnosis at the time: the SRAM card's **attribute space is a void**. Both
HP cards present their CIS in attribute space (aliased from common memory);
this card reads all-`FF` there, and a direct write test (PCIC window mapped
to attribute space via DEBUG) proved there is **no attribute storage at
all** — writes vanish, nothing to mirror into. The card is indistinguishable
from an empty socket to any scan that starts with attribute space.

**Revised in light of the Newton experiments (`NEWTON.md`)**: blank
attribute space can no longer be called *the proven cause*. The Newton card
was subsequently given a byte-identical copy of the OB430 card's attribute
presentation — and was rejected too. So attribute visibility is evidently
**necessary-looking but not sufficient**: the OmniBook's acceptance reads
something beyond all attribute and common content we can present. For this
SRAM card the blank attribute space remains a real and unfixable
difference — it simply may not be the only thing that would have kept it
out.

Consequences (unchanged in practice):
- The instant-rewrite D-slot laboratory needs, at minimum, a card whose
  attribute space aliases common memory or is writable — this card has
  neither — and per the Newton results possibly more than that. The
  **PRETEC is the working lab card** (~5 min per rewrite cycle).
- Corollary for card shopping, as it stood then: attribute-space behavior
  joins byte-accessibility on the qualification list. Both have since been
  struck off; the D slot never reads attribute space and only reads words
  (`OMNIBOOK.md`, "What turned out not to matter").

The original idea and plan follow, for context.

## The idea

Boot an OmniBook 300/425/430 from a **2 MB battery-backed SRAM card** in the
D slot, carrying a trimmed-down system image. If it works even partially,
the SRAM card becomes the **D-slot laboratory**: SRAM rewrites are instant,
byte-wise and wear-free, so image experiments iterate in seconds instead of
the multi-minute flash erase/program cycle.

## Why it's plausible

- The D slot only ever reads 16-bit words and never writes (see
  `OMNIBOOK.md`, "The bus, measured") — SRAM answers that trivially and
  has no controller to sit in reset.
- Everything the OmniBook checks lives in **common memory**, which we fully
  control: the CIS, the FFS2 store, and the firmware the machine executes
  during POST. The attribute CIS is provably ignored.
- Declaring the smaller size is a **one-byte edit**: the HP image's DEVICE
  tuple size byte `0xBD` (24 × 512 K = 12 MB) becomes `0x1D` (4 × 512 K =
  2 MB). If the OmniBook respects the declared size it will never read past
  2 M, so the SRAM's address wrap never shows.

## The unknowns

1. **Where the POST-critical firmware lives** in the 12 MB image, and how
   big it is. If it's within the first 2 MB (boot-critical code usually
   sits low), a truncated image should at least POST.
2. **FFS2 consistency** — a naively truncated store has dangling
   structures. A *fully booting* 2 MB card needs the FFS2 format analysis
   (also parked) to build a minimal DOS-only store.
3. Whether anything in the boot path objects to RAM that isn't flash
   (e.g. an FFS2 driver probing for flash behavior). POST itself shouldn't
   care — reads are reads.
4. Battery dependency: an SRAM D card dies with its cell. Lab tool, not a
   keeper card.

## The experiment (cheap, ~5 minutes, zero risk)

1. Archive the SRAM card's current contents (they're already bit-rot from a
   past battery failure, but politeness is free).
2. Build the probe image: first 2 MB of `HPCARD.IMG`, DEVICE tuple patched
   to declare 2 MB.
3. `LINGO WRITE` it to the SRAM card (no erase phase — SRAM writes
   directly) and verify.
4. Try the D slot:
   - **POST completes** → the firmware lives low; bisect toward a minimal
     bootable image, then build a proper 2 MB FFS2 store for it.
   - **No POST** → the firmware (or a size check) lives high; bisect the
     boundary by including progressively more of the image.

Either outcome maps the D slot's real requirements more precisely than
anything short of disassembling the OmniBook BIOS.

## Test hardware

The 2 MB SRAM card on hand: byte-accessible, healthy addressing, wrap at
2 M confirmed, battery currently good, contents = bit-rotted remains of an
MS-DOS 5 FAT volume (recognizable boot-sector fossils, nothing worth
saving beyond the archival dump).
