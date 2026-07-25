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
untouched — verified by a full FLINGO VERIFY), only then touch real bytes.

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

The acceptance mechanism reads something beyond attribute content, common
content, WP state, and declared size. Ranked suspects:

1. **Deeper attribute reads than the spoof covers.** The spoof wrote 256
   stream bytes; a real HP card aliases common memory into attribute space
   *indefinitely*. If the firmware walks attribute space past the spoof's
   edge it hits the divergence. Cheap future test: measure the Newton
   EEPROM's full extent and extend the spoof to fill it — though a small
   EEPROM can never impersonate megabytes of aliasing if the scan reads far.
2. **Active/physical probing** — write-response behavior, mirror/sizing
   checks, sense pins, timing. Not spoofable in software from this side.

Empirically the standing scoreboard: **Intel-silicon flash cards (PRETEC
route) and HP originals boot; everything else is refused** by a mechanism
not yet identified. See `OMNIBOOK.md` for the full acceptance history.

## Restoration

The Newton's attribute space was restored from the pre-experiment backup
and re-dumped for comparison: **CRC-32 and SHA-256 identical to the
original** (`newtattr.bin` = `NEWTVER.BIN`, CRC-32 `15371FD0`).
Factory state, cryptographically confirmed. Common memory still carries the
OBROM image and a 4 K test block — run `FLINGO ERASE /ALL` (or let a Newton
reformat it) before returning it to MessagePad duty.

## Worth keeping

- **Writable attribute EEPROMs exist in the wild** — this card class can
  have its CIS repaired, augmented, or forged at will. That capability
  outlives tonight's failures (CIS repair of dead cards, enabler research,
  identity experiments).
- The **DEBUG attribute toolkit** (`ATTRA`–`ATTRR` scripts on the box)
  generalizes to any card in the PC110's socket.
- FLINGO v1.2 wishlist: first-class `ATTR READ/WRITE` commands to replace
  the DEBUG dance.
