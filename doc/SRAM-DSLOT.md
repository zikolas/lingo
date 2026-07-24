# Parked idea: SRAM card as a trimmed-down OmniBook D card

*2026-07-24 — parked for a future bench session.*

## The idea

Boot an OmniBook 300/425/430 from a **2 MB battery-backed SRAM card** in the
D slot, carrying a trimmed-down system image. If it works even partially,
the SRAM card becomes the **D-slot laboratory**: SRAM rewrites are instant,
byte-wise and wear-free, so image experiments iterate in seconds instead of
the multi-minute flash erase/program cycle.

## Why it's plausible

- The D slot's one hard requirement is **byte-accessibility**
  (see `OMNIBOOK.md`) — SRAM is the most byte-accessible memory there is.
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
3. `FLINGO WRITE` it to the SRAM card (no erase phase — SRAM writes
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
