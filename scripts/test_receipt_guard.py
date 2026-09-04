#!/usr/bin/env python3
"""Deterministic tests for ble_ota_update.ReceiptGuard.

The guard is what stops a DFU transfer that has lost packet-receipt synchronisation from reaching
Validate or Activate. Its failure modes are all timing races that are impractical to provoke on
real hardware, so they are exercised here against the state machine directly. This is the
"deterministic injection" half of the evidence; a real transfer is the other half.

Run:  python scripts/test_receipt_guard.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ble_ota_update import ReceiptGuard  # noqa: E402

FAILURES = []


def check(name, condition, detail=""):
    if condition:
        print(f"  PASS  {name}")
    else:
        print(f"  FAIL  {name} {detail}")
        FAILURES.append(name)


def armed_transfer():
    """A guard part-way through a firmware image, which is the only state that can desync."""
    g = ReceiptGuard()
    g.begin_attempt()
    g.arm()
    return g


print("ReceiptGuard state machine")

# --- the normal case must not trip -------------------------------------------------------------
g = armed_transfer()
for sent in (3904, 7808, 11712):
    g.note_receipt(sent, sent)
check("clean transfer stays synced", not g.desynced)

# --- a dropped receipt must poison -------------------------------------------------------------
g = armed_transfer()
g.note_cancel(3904)
check("dropped receipt poisons", g.desynced)
try:
    g.check()
    check("poisoned guard raises on next write", False, "check() did not raise")
except RuntimeError:
    check("poisoned guard raises on next write", True)

# --- clear()-after-receipt race must NOT poison -------------------------------------------------
g = armed_transfer()
g.note_receipt(3904, 3904)   # receipt beat upstream's clear()
g.note_cancel(3904)          # so the wait looks like a timeout
check("early-receipt race is not a desync", not g.desynced)

# --- a stale receipt (previous window) must poison ----------------------------------------------
g = armed_transfer()
g.note_receipt(3904, 7808)
check("stale receipt poisons", g.desynced, f"reason={g.reason}")

# --- a duplicate receipt must poison once the offset has moved on -------------------------------
g = armed_transfer()
g.note_receipt(3904, 3904)
g.note_receipt(3904, 7808)
check("duplicate receipt poisons at the next offset", g.desynced)

# --- a receipt from the future must poison ------------------------------------------------------
g = armed_transfer()
g.note_receipt(11712, 7808)
check("wrong-offset (ahead) receipt poisons", g.desynced)

# --- connection-phase cancellation must NOT poison ----------------------------------------------
g = ReceiptGuard()
g.begin_attempt()            # never armed: still connecting / init packet
g.note_cancel(0)
check("connection-phase cancellation is ignored", not g.desynced)

# --- post-image cancellation must NOT poison ----------------------------------------------------
g = armed_transfer()
g.disarm()                   # Validate has been issued; image is fully sent
g.note_cancel(134432)
check("cancellation after Validate is ignored", not g.desynced)

# --- a retry must be able to clear a previous attempt's poison ----------------------------------
g = armed_transfer()
g.note_cancel(3904)
g.begin_attempt()            # dfu_lib reconnects and restarts
check("begin_attempt clears a previous attempt's poison", not g.desynced)

# --- but a Start mid-image must NOT clear poison (handled by the caller marking it a fault) ------
g = armed_transfer()
g.note_cancel(3904)
check("still armed after poison, so caller treats a Start as a fault", g.armed and g.desynced)

# --- first failure reason wins ------------------------------------------------------------------
g = armed_transfer()
g.note_receipt(3904, 7808)
first = g.reason
g.note_cancel(11712)
check("first failure reason is retained", g.reason == first, f"reason={g.reason}")

print()
if FAILURES:
    print(f"{len(FAILURES)} FAILED: {', '.join(FAILURES)}")
    sys.exit(1)
print("all guard tests passed")
