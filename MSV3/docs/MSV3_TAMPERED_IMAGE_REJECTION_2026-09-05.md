# MSV3 tampered-image rejection: measured on hardware — 2026-09-05

**What was open.** `MSV3_BLE_OTA_PROOF_2026-09-02.md` listed one item as blocking: *"Mutated-image
negative tests not done. Corrupt a byte with the original CRC; corrupt and recompute CRC without
re-signing. Both must fail validation and never boot. Until then the integrity story is
'host-side window narrowed', not 'airtight'."*

That framing was right. `ReceiptGuard`, `verify_package_integrity()` and the fleet registry all run
on the **host**, and a host is exactly what a corrupted download, a buggy script or an attacker
replaces. Nothing host-side can be the integrity boundary.

**What was done.** Seven package mutations were built and offered to probe 99. Each one's rejection
stage and status byte was **predicted from the bootloader source first**, then measured. All seven
predictions were correct.

## The two gates are not the same gate, and the difference matters

| | protects | cost of a refusal |
|---|---|---|
| host gate (`ble_ota_update.py` preflight) | **availability** | nothing — the radio is never touched |
| device gate (bootloader) | **execution integrity** | **the running application**, always |

The asymmetry is forced by single-bank DFU and is worth stating plainly, because it is the reason
the host-side checks are not redundant with the device's. Read from the pinned source:

- `dfu_start_pkt_handle()` (`dfu_single_bank.c`) calls `m_functions.prepare(m_image_size)` =
  `dfu_prepare_func_app_erase`. **The application is erased when Start DFU is accepted** — before
  the init packet has even been received.
- `dfu_init_prevalidate()` (`src/dfu_init.c`) SHA-256s the init packet and `uECC_verify()`s the
  P-256 signature. It runs on the first data packet, i.e. *after* the erase.
- `dfu_init_postvalidate()` checks `image_len` against the signed `firmware_length`, then SHA-256s
  `p_image = mp_storage_handle_active->block_id` — **the image as stored in flash** — and
  `memcmp`s it against the signed digest.

So there is no cheap device-side refusal. Even a signature failure, detected before a single image
byte is sent, happens after the erase. **On a coated probe in soil, the host gate is the only thing
that prevents a bad package from costing the running firmware.**

## Status bytes

`nrf_err_code_translate()` (`dfu_transport_ble.c`) maps `NRF_ERROR_INVALID_DATA` to
`BLE_DFU_RESP_VAL_CRC_ERROR` (5) when the procedure is VALIDATE, and to
`BLE_DFU_RESP_VAL_OPER_FAILED` (6) otherwise. That is the whole reason a signature failure and a
digest failure look different on the wire, and it is what made the predictions below possible.

## The seven mutations and what each isolates

Package layout (DFU 0.8): `manifest.json` (host-only) + `.bin` + `.dat`, where `.dat` is a 52-byte
init packet followed by a 64-byte P-256 signature over `init_packet[0:52]`. The init packet carries
`firmware_length` and `firmware_hash`, so the image is covered **transitively**: signature → hash
and length → image. Each case breaks exactly one link.

| case | what is broken | predicted | observed |
|---|---|---|---|
| `payload-corrupt` | one image byte; `.dat` untouched, signature still mathematically valid | Validation / 5 | **Validation / 5** |
| `payload-corrupt-code` | same, but the byte is in executable code | Validation / 5 | **Validation / 5** |
| `image-truncated` | image 32 bytes shorter than the signed `firmware_length` | Validation / 5 | **Validation / 5** |
| `image-extended` | image 32 bytes longer than the signed `firmware_length` | Validation / 5 | **Validation / 5** |
| `payload-rehashed` | one image byte **and** the hash recomputed into `.dat` + manifest, not re-signed | Init Packet / 6 | **Init Packet / 6** |
| `init-tampered` | `application_version` edited in the signed init packet, image genuine | Init Packet / 6 | **Init Packet / 6** |
| `sig-corrupt` | one byte of the 64-byte signature; every parsed field genuine | Init Packet / 6 | **Init Packet / 6** |

`payload-corrupt` and `payload-rehashed` are the two the open item named. The other five exist
because each isolates something the first two cannot:

- `sig-corrupt` is the only case that attacks the signature **without editing a field the
  bootloader parses**. A device that rejected `payload-rehashed` by tripping over the changed hash
  rather than by verifying the signature would be caught here and nowhere else.
- `image-truncated` / `image-extended` exercise the signed-length check, which
  `dfu_init_postvalidate()` performs *before* it hashes — a different code path from the digest
  comparison.
- `payload-corrupt-code` shows the digest covers executable bytes and not merely the tail. It is
  **supplemental**, with a deliberately weaker "never ran" assertion: a corrupted instruction may
  hardfault rather than announce itself.

### Where the corrupted byte goes, and why

By default it lands on a letter inside the image's `FIRMWARE_BUILD_ID` string, replaced with `X`
(`2026-09-05-negtest-base` → `2026-09-05-Xegtest-base`). The string is inert, so a **wrongly
accepted** image still boots and reports its own corrupted build id over the serial banner and the
Device Information Service. A test whose failure mode is silence passes for the wrong reason; this
one's failure mode is a probe telling you its own name is wrong.

The first version used a fixed offset, which landed on the `-` of a date and turned it into a
carriage return — invisible in the very banner the test reads. The site is now chosen by scanning
for a letter, and `test_ota_mutate.py` asserts the result stays printable.

## Results

Every case: host gate refused (exit 2) before touching the radio; device gate refused with the
predicted stage and status; board found in BLE recovery afterwards; good image reinstalled and the
board back to `application` with its wake counter advancing. The corrupted build id was **never**
observed, on serial or over BLE — only the good id, and only after each recovery reflash.

```
PASS  init-tampered      PASS  payload-corrupt   PASS  payload-rehashed
PASS  sig-corrupt        PASS  image-truncated   PASS  image-extended
PASS  payload-corrupt-code
```

Host-gate refusals were specific rather than generic: `"SHA-256 mismatch"` for the payload cases,
`"ECDSA signature does not verify against the key the target bootloader trusts"` for the signature
cases.

An unmodified package was pushed through the **same** recovery transport immediately after every
negative case — the recovery reflash is that control, and it completed every time.

## What this establishes, and what it does not

**Establishes.** On probe 99, this bootloader build returned explicit non-success status responses
for seven package mutations made without generating a new valid signature, at stages consistent
with the inspected validation flow. After each rejection the device remained in bootloader recovery
mode, and the inspected rejection paths do not activate the candidate application. For the payload
mutations carrying the corrupted build id, that id was additionally never observed over serial or
BLE. Source inspection shows `dfu_init_postvalidate()` hashes the candidate image **from flash**
before comparing it with the signed digest — so this build validates the stored payload rather than
relying on a host-supplied hash.

**Does not establish.** This is not proof that every malformed or adversarial package is rejected.
Outside the result: a validly signed malicious image, signing-key compromise, untested parser
defects, and replay or downgrade of an older validly signed release.

**Replay is untested.** `dfu_init.c`'s version-check hook is an unimplemented upstream template, so
the *leading hypothesis* — not a measured fact — is that an older compatible signed release would
be accepted. That would be a freshness limitation, not an integrity failure, and it is the obvious
next test.

Note also that "never executed" is stronger than what was directly observed for the cases whose
build id does not change (`init-tampered`, `sig-corrupt`) or whose corruption is in code
(`payload-corrupt-code`). For those, non-activation follows from the explicit rejection, the
continued recovery-mode advertising, and the inspected control flow — not from an identity
observation.

## Three harness bugs, all of the same dangerous shape

Every one of them would have produced a **false pass**, and they are recorded because the shape
recurs: *a test that concludes "rejected" from the absence of success.*

1. **A failed BLE scan scored as a rejection.** The recovery flasher's 5-second `discover()`
   returned nothing while the bootloader was, seconds later, advertising 244 packets in 25 s. Fixed
   by requiring the "Found recovery bootloader" line before any verdict, and by retrying discovery
   and transport failures instead of scoring them.
2. **A successful install scored as a transport failure.** `transfer_completed()` keyed on
   `"Transfer completed"` — a line `ble_ota_update.py` prints and `ble_dfu_recover.py` never does.
   `dfu_lib` prints `"DFU Complete."`. So a device that **accepted** a tampered image would have
   matched nothing and been classified `rejected`. Caught because a recovery reflash that
   demonstrably worked (the board's own boot banner reported the new build one second later) was
   scored a failure.
3. **A silent device scored as a refusal.** `dfu_lib._wait_for_response()` returns `-1` on timeout
   *and* on a mismatched op code, and its callers only test `status != 1`. So
   `"Validation failed. Status: -1"` means the device said **nothing**, and a substring match on
   the sentence filed it as a refusal. Fixed by parsing the status number and requiring it in
   {2,3,4,5,6}. `"Start DFU sequence failed"` was dropped from the refusal set entirely: `dfu_lib`
   formats it without a status byte, so a refused Start DFU and a timed-out one are textually
   identical and it can never support a claim either way.

A fourth, narrower correction: an explicit refusal proves the device answered, not that the
intended check answered. A stale session answering `INVALID_STATE` (2) is a real refusal that
demonstrates nothing about signature verification. Each case now declares its expected
`(stage, status)`, and a mismatch is reported as **inconclusive for the mechanism** rather than as
a pass.

## Tooling

| file | role |
|---|---|
| `scripts/ota_mutate_package.py` | builds the seven mutations; documents the init-packet layout it edits by offset |
| `scripts/ota_negative_test.py` | drives the two gates on real hardware; refuses to run against a `deployed` probe |
| `scripts/test_ota_mutate.py` | 31 checks; asserts every case is refused by the **real** `verify_package_integrity()` |
| `scripts/test_ota_negative.py` | 34 checks pinning the classifier against real transcripts |

The `deployed` gate is **absolute — there is no override flag.** One was drafted and removed:
`preflight()` already refuses `--allow-identity-mismatch` on a deployed probe on the stated grounds
that fixing the registry "leaves an auditable change behind, where a command-line flag does not",
and that argument applies with more force to a test whose expected outcome is an erased probe. If a
board is genuinely on the bench, say so in `MSV3/fleet.json`.

## Re-running it

```powershell
# 1. Build a bench image for probe 99 (SENSOR_ID = 99, ENABLE_SERIAL_LOGS = true, distinct build id)
#    from a SCRATCH COPY of the sketch -- never by editing the tracked file.
powershell -ExecutionPolicy Bypass -File MSV3\scripts\arduino_msv3_probe.ps1 `
    -Action compile -Sketch <scratch>\MSV3_Moisture_BLE_Sleep
# 2. Sign it with the same key the bootloader trusts (see build_msv3_release.ps1 for the invocation)
# 3. Run every case
python scripts/ota_negative_test.py --probe 99 --good <bench.zip> --case all `
    --outdir <scratch>\packages --port COM9 --json result.json
```

Each payload case takes about six minutes, most of it the two 90-second observation windows (which
must exceed the application's 60-second advertising interval, or a healthy board looks dead).

## Still open

- **Replay / downgrade of an older validly signed release.** Untested; expected to be accepted.
- **Append or prepend beyond the 32-byte length delta** tested here.
- A validly signed image, key compromise, and bootloader parser defects generally — outside what a
  negative test of this shape can reach.
