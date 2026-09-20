# MSV3 dual-bank DFU: not impossible, a completion-gate race — 2026-09-04

**Question asked:** can the bootloader do the two-bank thing so a failed update reverts to the old
firmware, or has that been impossible with these chips and libraries?

**Short answer.** It is not impossible and it is not a chip or capacity limit. The 2026-09-03
conclusion that "dual-bank activation does not produce a bootable bank 0 at any size" described a
real symptom but attributed it to the wrong cause. `Adafruit_nRF52_Bootloader` 0.11.0's dual-bank
activation path is **definitively defective** — three independent defects are readable in the
source — and it is fixable.

Which of them produced the historical failure is the **leading explanation, not a confirmed one**:
the chronology below best fits every recorded symptom, but promoting it from inference to fact
takes one non-destructive SWD read of the settings page. That read is cheap and is the first thing
to do.

**But it would not give what the question asks for.** SDK11-style dual-bank is *staging*, not
rollback. It protects the old image during download and validation and then destroys it while
committing the new one. It cannot revert after a new application boots and misbehaves. See
"What dual-bank would and would not buy" below before deciding it is worth the work.

Nothing here has been built or installed. **SWD is not attached**, and a bootloader update is the
one failure BLE recovery cannot repair.

## Capacity is not the constraint

Computed from the macros in `dfu_types.h` rather than taken from the earlier doc's estimate:

| | |
|---|---:|
| `BOOTLOADER_REGION_START` (nRF52840) | `0x000F4000` |
| `CODE_REGION_1_START` (S140 6.1.1) | `0x26000` |
| `DFU_APP_DATA_RESERVED` | 40 KB |
| `DFU_REGION_TOTAL_SIZE` | 843,776 |
| `DFU_IMAGE_MAX_SIZE_FULL` | 802,816 |
| **`DFU_IMAGE_MAX_SIZE_BANKED`** | **401,408** each |
| bank 0 / bank 1 | `0x26000` / `0x88000` |
| current application | 132,312 bytes |

The application uses a third of one bank. There is room for two banks three times over.

## The defect

`bootloader.c` has exactly one completion gate for the whole update:

```c
static void pstorage_callback_handler(pstorage_handle_t * p_handle, uint8_t op_code, ...)
{
    if ((m_update_status == BOOTLOADER_SETTINGS_SAVING) && (op_code == PSTORAGE_STORE_OP_CODE))
    {
        m_update_status = BOOTLOADER_COMPLETE;
    }
}
```

`bootloader_dfu_start()`'s main loop returns — and the bootloader resets — as soon as
`m_update_status` is `BOOTLOADER_COMPLETE`. Note what the gate does **not** check: which
`p_handle` the callback belongs to, or which of several settings writes completed. *Any* store
completion arriving while the state is `SETTINGS_SAVING` ends the update.

Now the two settings writes involved in an activation:

| branch | writes | sets `SETTINGS_SAVING`? |
|---|---|---|
| `DFU_BANK_0_ERASED` | `bank_0 = BANK_INVALID_APP` | **no** |
| `DFU_UPDATE_APP_COMPLETE` | `bank_0 = BANK_VALID_APP` | yes |

In **single-bank**, `DFU_BANK_0_ERASED` fires during *prepare*, at the start of the transfer, when
the state is not `SETTINGS_SAVING`. Its callback is correctly ignored. Only one settings write is
ever in flight when the gate is armed. This is why single-bank works.

In **dual-bank**, both fire within microseconds of each other at activation, because the erase and
the bank1→bank0 copy between them are synchronous. Two `pstorage` clear+store pairs end up queued
against the settings page while the gate is armed, and the first store to complete is the
`BANK_INVALID_APP` one. Its callback flips the state to `BOOTLOADER_COMPLETE`, the loop returns,
and the device resets — plausibly before the `BANK_VALID_APP` store has executed.

`BANK_INVALID_APP` is then fatal at the next boot. `bootloader_app_is_valid()` has three outcomes:

- `BANK_VALID_APP` → CRC path
- `0xFFFF` → Adafruit's vector-table fallback, added for debugger-flashed apps
- **anything else → `return false`**

So `BANK_INVALID_APP` is not rescued by the fallback, which is exactly why a full power cycle never
helped.

### Two more defects in the same window

Either could produce the same symptom on its own, and all three want fixing together:

- **A retained pointer to a mutated buffer.** `bootloader_dfu_update_process` uses a
  `static bootloader_settings_t settings`, and `bootloader_settings_save` hands `&settings` to
  `pstorage_store`, which retains the pointer rather than snapshotting. Between the two queued
  stores that buffer is mutated from `INVALID` to `VALID`, so which value reaches flash depends on
  when the SoftDevice consumes the source. The lifetime contract is violated regardless of outcome.
- **Direct NVMC while the SoftDevice is enabled.** `flash_nrf5x_erase` / `flash_nrf5x_write` drive
  `nrfx_nvmc` directly over up to 401 KB, while the SoftDevice is enabled *and* has flash
  operations queued. Nordic does not support this.

### Three categories of evidence, kept apart

**Source-established** (read directly in the pinned 0.11.0 checkout, no hardware needed): the gate
ignores `p_handle`; `DFU_BANK_0_ERASED` omits the `SETTINGS_SAVING` assignment while
`DFU_UPDATE_APP_COMPLETE` sets it; `BANK_INVALID_APP` falls through to `return false`; direct NVMC
runs with the SoftDevice enabled; the settings pointer is retained across a buffer mutation. Also
that `m_image_crc` is declared in **both** `dfu_dual_bank.c` and `dfu_single_bank.c` and in both is
only ever assigned `0` and never computed — so `bank_0_crc` is always 0 and the CRC comparison in
`bootloader_app_is_valid()` is vacuous in every configuration. That last one matters below.

**Hardware-observed** (2026-09-03, on the 1.0.0 dual-bank build): every control-point status
returned success; the board rebooted to `AdaDFU`; a full power cycle did not help; 21 KB and 131 KB
images failed identically.

**Inferred, unverified**: that the settings page ends up holding `BANK_INVALID_APP`, and that the
callback chronology above is what put it there. This is the reading most consistent with the
observations, but **confirming it requires reading the settings page at `0x000FF000` over SWD**,
which was not available.

Be careful about attribution even so. The three defects *jointly* make this path invalid; the
completion-gate chronology is the best explanation of the historical symptom, not a proven sole
cause. A fix has to address all three regardless of which one fired first.

The earlier size test (21 KB and 131 KB failing identically) is *supporting* evidence: it argues
against capacity exhaustion and against length-dependent arithmetic bugs. It does not rule out
copy-integrity bugs generally, and it was over-read at the time as proving dual-bank broken.

## Fixing it

**Do not simply drop the intermediate `BANK_INVALID_APP` write.** That was my first instinct and it
is wrong. Because `bank_0_crc` is always 0, the CRC branch cannot detect a half-copied image, so
leaving `BANK_VALID_APP` standing during the erase-and-copy would let a power cut mid-copy produce
a board that *claims* a valid application and jumps into a partial one.

State it as the invariant it is: **bank 0 must be marked invalid, durably, before the first byte of
it is destroyed.** Note the word durably — the present code expresses that intent but does not
deliver it, because the write is queued asynchronously and may not have landed before the erase
begins. So its presence in source does not make today's activation power-safe. Removing it is
unsafe; keeping it is necessary but not sufficient; making it *synchronous and ordered* is the
actual requirement.

**Waiting harder on the existing gate is not enough either**, because the gate is released by the
wrong operation. A corrected wait would have to be transaction-specific — wait for *that* store,
not for *a* store — which means adding an operation token or an explicit activation phase, since
both writes use the same settings handle.

**The direction worth building** removes asynchronous flash from the activation window entirely:

1. Ensure the Activate response has actually left the radio, not merely been queued.
2. Stop accepting further DFU work.
3. Drain every outstanding `pstorage` / SoftDevice flash operation, waiting on specific
   completions.
4. Disable the SoftDevice, and check the return value.
5. Write `BANK_INVALID_APP` synchronously via direct NVMC — bypassing the `is_ota()` branch in
   `bootloader_settings_save`, which would otherwise route it back through `pstorage`.
6. Erase bank 0, copy bank 1 → bank 0, flush.
7. Write `BANK_VALID_APP` synchronously.
8. Reset.

Steps 1–4 are not optional garnish: without them the activation response can be lost, the
SoftDevice disable can return `NRF_ERROR_BUSY`, or a stale queued settings write can survive into
the direct-NVMC phase and land afterwards.

## What dual-bank would and would not buy

This is the part that matters for the original question, and the three cases must not be blurred:

| | today (single-bank) | with dual-bank fixed |
|---|---|---|
| **transfer interrupted** | bank 0 already overwritten → BLE recovery, probe offline until re-flashed | **old firmware still intact and running** |
| **activation interrupted** | n/a | bank 0 partial → BLE recovery (bank 1 may allow a retry, depending on reboot logic) |
| **new firmware boots but misbehaves** | no revert | **still no revert** |

The win is the first row, and it is a real one: for a coated probe on solar, "keeps running the old
firmware" is much better than "goes offline until the next maintenance window". The destructive
window shrinks from the whole transfer to the activation copy. **How long that copy actually takes
has not been measured** and should be, for the production-sized image, before quoting a figure.

The third row is why this should not be described as "reverts to old firmware if it fails". Nothing
in SDK11 dual-bank gives A/B commit-and-revert. Getting that on this part means a different
bootloader family — MCUboot, or the modern nRF5 SDK secure bootloader — which is a much larger
change because the Adafruit Arduino core expects this bootloader's MBR and flash layout.

## Before any of this is installed

The prerequisites are not negotiable, because a broken bootloader is the one failure BLE recovery
cannot repair:

- **SWD attached and proven** — able to halt the part and read the settings page at `0x000FF000` —
  before an experimental bootloader is written.
- Read the settings page on the current 1.0.0 dual-bank board first. That single readback confirms
  or kills the `BANK_INVALID_APP` inference above for the cost of one attach.
- **Do not let the programmer run a `recover` or mass-erase before that read.** It is the usual
  first move when a part will not attach cleanly, and it would erase the one page holding the
  evidence — turning a five-minute confirmation into a permanently open question.
- A test matrix: clean 21 KB and 132 KB updates, interrupted download, power cuts during erase,
  during copy, and during the final save, with settings and bank readback after each.
- Measured activation duration for the production-sized image.

Note that the bench board currently has a valid application, so USB serial DFU *is* reachable and
could install a bootloader without SWD. That is a reason for discipline, not a shortcut.
