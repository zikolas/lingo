# probes

Bench tools that support LINGO work but are not part of it. Build on the box
with `C:\WATCOM\BLD.BAT <name>`.

## ATTRIO.C — read / blank / restore attribute memory

Some cards expose a **freely writable attribute EEPROM** — plain writes,
persistent across power cycles. The Apple Newton 4 MB Flash Storage Card is the
known example (see `doc/NEWTON.md`). That makes a card's CIS repairable,
forgeable, and — the reason this exists — *blankable on purpose*.

```
ATTRIO SAVE file      dump LEN dense bytes to file
ATTRIO BLANK [n]      write 0xFF over the first n dense bytes (default 4)
ATTRIO LOAD file      write file back, then verify
  /S n socket 0-7   /W hex window segment   /LEN n bytes (default 1024)
```

Attribute memory implements only even host addresses, so dense byte `i` lives at
window offset `i*2`. All counts are **dense** bytes.

**Always SAVE first.** A blanked CIS is recoverable only from a byte-exact
backup. Every write here is verified by readback, and the tool refuses to run
without a card present and powered — `doc/NEWTON.md` records a restore pass that
ran against an *empty socket* and vanished into floating bus, with an all-`FF`
readback as the only tell.

`BLANK` defaults to 4 bytes rather than the whole space on purpose: killing the
CIS header is enough to make a card read as CIS-less, and it keeps the blast
radius to four bytes instead of a thousand.

### What it was built for

Producing a card that reads all-`FF` in attribute *and* common space, to test
how LINGO behaves when a socket looks blank — the one path in its post-power
settle gate that no ordinary card can exercise. Newton card, 2026-07-27: blanked
4 dense bytes, confirmed LINGO's floor branch fires at 300 ms and reports the
card as blank, then restored byte-exact (CRC-32 `15371FD0`, verified through
CISDUMP after a power cycle).
