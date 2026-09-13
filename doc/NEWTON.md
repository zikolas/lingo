# The Newton card experiments: attribute-space surgery, three refusals, one discovery

*2026-07-25, late bench session. Apple Newton 4 MB Flash Storage Card vs the
OmniBook 430's D slot. All three boot attempts failed; the tooling and the
discovery that made them possible are the lasting value.*

## The card

Apple Newton 4 MB Flash Storage Card: two AMD Am29F016/017-class 16-Mbit
chips (`01/3D`) interleaved on the byte lanes, 64 K sectors (128 K combined
erase blocks), 5 V-only, byte-accessible, healthy. Its factory attribute CIS
is a curiosity — JEDEC tuple, DEVICEGEO, an empty VERS_1 v4.1, and a
descriptor for its own attribute EEPROM, but **no `CISTPL_DEVICE` tuple
declaring the common-memory flash at all**, plus vendor tuples and factory
residue ("AMD", scattered characters) beyond the chain.

## The discovery: a writable attribute EEPROM

The attribute space turned out to be **freely writable, persistent
storage**: plain byte writes stick, survive full power cycles, and can be
rewritten in both directions (including back to `FF`) with no erase
sequence and no unlock magic. This is the VEW211 CIS-repair capability
without the pain — a card whose electrical identity can be *edited*.

Toolkit: DEBUG scripts driving the 82365 PCIC directly (power → map a
window with the REG bit → poke → tear down), with the safety drill:
back up the CIS stream first (`CISDUMP /BIN`), test writability at a
far-off harmless address, confirm REG# is respected (common memory
untouched — verified by a full LINGO VERIFY), only then touch real bytes.

## Three attempts on the OB430, all NO POST

The payload throughout: the OB430's own 512 K system image (`OBROM.IMG`,
proven bootable), written to the Newton's common memory and verified.

| # | Attribute state | WP | Result | Theory it killed |
|---|---|---|---|---|
| 1 | factory CIS (no DEVICE tuple) | off | ignored | — (motivated v5: "scan needs a DEVICE tuple") |
| 2 | DEVICE-FLASH-4MB tuple grafted ahead of the factory chain | off | ignored | v5, and v6's Intel-route for AMD silicon |
| 3 | full HP-signature spoof: attribute stream rewritten to the aliased-common view, **verified byte-identical to the OB430 card's own presentation** (`CISDUMP /BIN` on both) | off, then **on** | ignored | v6 (HP route = `0x13` signature) and v6.1 (…plus WP) |

After attempt 3 the Newton matched the machine's own card in every
observable we can enumerate: identical attribute view, identical common
content, identical declared size, write-protect asserted, and strictly
better bus behavior (byte-accessible where the original is word-only).
The OmniBook still declined, silently, exactly as if the slot were empty.

## What survives

*(Written before the mirror-check discovery; superseded — see below.)*
At this stage the suspects were deeper attribute walks and physical
probing. The mirror check (`OMNIBOOK.md`, "What the OmniBook checks")
then explained the single-copy refusals — and prompted two more Newton
attempts.

## Act two: the mirror check changes everything — except the verdict

After the tiled English image booted from both the PRETEC and the SRAM
card, the Newton got the same treatment, escalating to the final
equalization:

| # | Configuration | Result |
|---|---|---|
| 4 | English image **tiled ×8** (perfect mirror), factory attr, WP off | no POST |
| 5 | tiled ×8 **plus attribute space blanked** — byte-for-byte the profile of the SRAM card that boots | **no POST** |

Attempt 5 is the decisive one: every software-visible property — content,
mirroring, attribute space, write-protect, byte-accessibility — now
matched a booting card exactly. The OmniBook still refused. **The machine
distinguishes this AMD flash card from SRAM at the physical layer.**
Leading candidate: the scan writes a probe byte and reads it back (SRAM
answers, flash silently ignores), and the write-ignoring path gates on
something else the Newton fails — READY/WAIT behavior, BVD wiring, or
sense pins. The instrument that settles it is a logic analyzer on the
D-slot bus during POST.

**Update, after the D-slot bus was measured** (`OMNIBOOK.md`, "The bus,
measured"): the probe-byte candidate above is dead, the slot has `WE#` tied
to Vcc and cannot write. The slot also never drives `RESET`, which turned
out to be the VS200's whole problem, but the Newton card pulls its own
`RESET` down and still refuses with a pull-down fitted, so its case is a
second mechanism. The remaining suspects and the next measurements are in
`OMNIBOOK.md`, open threads.

So the AMD verdict stands, now with full rigor: not the CIS, not the
content, not the mirror — the silicon. The consolation prizes stand too:
the writable attribute EEPROM, the toolkit, and a refusal so thoroughly
characterized that the next investigator can start at the bus.

## Restoration (second edition)

The attribute space was blanked for attempt 5 and afterwards restored from
the backup — with one lesson en route: the first restore pass ran against
an **empty socket** (card was out for an OmniBook trip) and vanished into
floating bus; all-`FF` readback was the tell. Always presence-check
(`LINGO /S 0`) before DEBUG attribute work. The second pass, card seated,
restored and re-verified **byte-identical to the factory backup**
(CRC-32 `15371FD0`, SHA-256 match, confirmed after a power cycle).
Common memory currently holds the 8-tile English image — erase before any
return to MessagePad duty.

## Restoration

The Newton's attribute space was restored from the pre-experiment backup
and re-dumped for comparison: **CRC-32 and SHA-256 identical to the
original** (`newtattr.bin` = `NEWTVER.BIN`, CRC-32 `15371FD0`).
Factory state, cryptographically confirmed. Common memory still carries the
OBROM image and a 4 K test block — run `LINGO ERASE /ALL` (or let a Newton
reformat it) before returning it to MessagePad duty.

## Worth keeping

- **Writable attribute EEPROMs exist in the wild** — this card class can
  have its CIS repaired, augmented, or forged at will. That capability
  outlives tonight's failures (CIS repair of dead cards, enabler research,
  identity experiments).
- The **DEBUG attribute toolkit** (`ATTRA`–`ATTRR` scripts on the box)
  generalizes to any card in the PC110's socket.
- LINGO v1.2 wishlist: first-class `ATTR READ/WRITE` commands to replace
  the DEBUG dance.
