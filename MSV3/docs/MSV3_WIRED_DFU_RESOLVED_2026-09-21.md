# MSV3 wired DFU: resolved, and what it cost to get there

**Date:** 2026-09-21
**Board:** probe 8 (MSV3-MOISTURE-008), on the bench, USB-powered throughout. No SWD was used or
available at any point; every bootloader in this story was delivered over BLE OTA.
**Outcome:** both flashing paths work. Bootloader `msv3-1.0.24`, application v65589.

## What now works

The **unmodified stock client** — what the Arduino IDE upload button runs, nothing monkey-patched,
no timing constants touched:

```
python -m nordicsemi dfu serial -pkg <signed zip> -p COM19 -b 115200 -sb
#############...   259 progress marks
Device programmed.
exited 0 after 9.1s
```

Independently verified afterwards, because a client saying "programmed" is not evidence:

- the application re-enumerated its USB CDC port
- a 150 s scan saw 26 packets, an advancing wake counter, 4310 mV, both phases, OTA gate open
- DIS read back `2026-09-21-bl1024` — the exact build tag the cable delivered

BLE OTA was re-proven on every one of the nine deliveries made during this work, including the one
that installed the fix.

## The root cause was ours

Bootloader **1.0.4** attached USB *inside* the SoftDevice-enabled branch of `src/main.c`, so a probe
whose SWD pads are under conformal coating would keep a wired way back in. Stock 0.11.0 brings up
exactly one transport:

```c
if (_ota_dfu) { mbr_init_sd(); ble_stack_init(); }   // SoftDevice ENABLED
else          { usb_init(serial_only_dfu); }          // wired: SoftDevice NEVER enabled
```

A **stock wired session therefore has no SoftDevice**. `is_ota()` is false, `dfu_single_bank.c`
takes the synchronous `flash_nrf5x_*` path, and `dfu_start_pkt_handle()`'s application-bank erase
completes before it returns. By the time `adafruit-nrfutil`'s fixed `get_erase_wait_time()` sleep
expires, the state machine is already `DFU_STATE_RDY`.

1.0.4 made wired DFU asynchronous. Every symptom chased across two days descends from that single
change, and it explains the original report exactly: **Arduino IDE uploads broke *after* OTA
started working.**

**Do not "simplify" the transport arbitration back out.** It is what makes 1.0.4's goal safe.

## The four real defects

All four were confirmed by measurement on hardware, not by inference.

### 1. One `hci_mem_pool`, two transports (the shared-pool defect)

`dfu_transport_ble.c` and `dfu_transport_serial.c` use the same pool.
`dfu_transport_ble_update_start()` calls `hci_mem_pool_open()` — which zeroes `read_index`,
`write_index` and every count — **after** the serial transport has already produced a buffer and
registered it with the SLIP layer. From then on `read_index` runs one ahead of the buffer SLIP is
filling, so every extract after the first returns a produced-but-never-written buffer whose first
byte reads as `INVALID_PACKET`. That is the `NRF_ERROR_INVALID_PARAM` that nine diagnostic releases
chased.

It also explains why START always worked and nothing after it did: after the reset,
`rx_data_size_set()` writes slot 15 instead of slot 0, so START is extracted with a nonsense length
too — but its payload *pointer* is correct and `dfu_start_pkt_handle()` reads the image sizes from
the packet struct, not from that length.

**Fixed in 1.0.16** by exclusive ownership claimed at first ingress. All nine values in the 1.0.14
trace matched the replay exactly.

`hci_transport_rx_pkt_extract()` also adjusted pointer and length *unconditionally*, turning a
bookkeeping slip into silently corrupt frame data rather than a clean refusal. That fail-closed
guard is kept as defence in depth.

### 2. Unbounded `memcpy` in `dfu_init_prevalidate()` (security-relevant, not the blocker)

The extended-init-packet length was derived by pointer subtraction from two host-supplied values
and `memcpy`'d into `m_extended_packet[104]` with no upper bound, **before any signature is
verified**, with the `uint8_t` length variable declared immediately after the array.

**Hardened in 1.0.17.** Worth keeping on its own merits — but it was *not* what broke USB. The
transport's integer division (`(122/4) - 1` words) already discarded the stock client's two padding
bytes, so `prevalidate` saw an identical 116-byte packet either way. This was predicted to be the
next blocker and the measurement said otherwise.

### 3. The async-erase race at INIT

With the SoftDevice enabled the erase is asynchronous. The serial transport has no reply channel
for init, so the host does not wait for an answer — it sleeps a fixed estimate
(`max(0.5, (size/4096 + 1) * 0.0897)` ≈ 2.96 s against a ~2.8 s erase) and sends the init packet
regardless. `dfu_init_pkt_complete()` then returns `NRF_ERROR_INVALID_STATE` and the serial path's
`APP_ERROR_CHECK` **reboots the device**. That is the COM port vanishing mid-transfer.

Proven by multiplying only the *host's* flash-timing constants by six — which does nothing but
lengthen that sleep — after which init was accepted and real firmware bytes reached flash for the
first time (application page CRC moved from the erased `0x0FE1` to a partially written `0x4573`).

**Fixed at the root in 1.0.24** by restoring the stock configuration, not by tolerating the
asynchrony. Three releases that tried to tolerate it — **1.0.18, 1.0.19, 1.0.20 — are WITHDRAWN**
and `build_msv3_release.ps1` throws on them.

### 4. No watchdog feed across the blocking erase

`flash_nrf5x_erase()` blocks the CPU for ~2.8 s over 33 pages and nothing reloaded the WDT, which
survives soft reset and may have been started by the application. **Latent rather than observed** —
`RESETREAS` never showed `DOG` — but fixed regardless, because a multi-second blocking erase must
not be the one place in the image that starves the watchdog.

## How a wired session works now

`MSV3_SERIAL_STOCK_MODE` in `src/main.c`:

1. First CDC traffic **requests** the mode. Nothing is drained until it is established — the bytes
   stay in the CDC FIFO, which NAKs the host at the USB layer. No host-side change is required.
2. The **main loop only** — never a USB, BLE or scheduler callback — closes the BLE transport,
   drains queued SoftDevice work, calls `sd_softdevice_disable()` **and checks its return**, and
   only then clears `_sd_inited` and `_ota_dfu`.
3. Flash is now synchronous. The START erase completes before it returns.
4. A hard guard at START and INIT resets rather than ever running a serial session on the async
   path.

Ordering is not optional. 1.0.9 already proved that forcing the synchronous flash path while the
SoftDevice is still enabled faults immediately.

## The lease is a safety property, not an optimisation

Once the SoftDevice is disabled there is **no BLE until the next reset**, and on this hardware BLE
is the only recovery channel. It is **not** true that every serial failure resets — a stray CDC
write, a cable pulled mid-frame, or a host that dies after START has already erased the application
would otherwise leave the board awake, wired-only, with no route back.

So entering serial mode arms a lease: losing VBUS, or a long serial silence, forces
`NVIC_SystemReset()`.

**Proven on hardware:** a single junk byte written to the DFU port tore down the SoftDevice, and the
board reset itself and came back advertising with its application intact.

### Do not time the lease from RTC1

1.0.23 did, and it fired after 3.4 s against a nominal 10 s — **killing the transfer it existed to
protect.**

This module has **no 32.768 kHz crystal** and runs the internal LFRC, which is the same constraint
the LFCLK compile guards enforce and which
[the LFCLK/RTC1 investigation](MSV3_LFCLK_RTC1_HANG_2026-08-21.md) already documents.
`sd_softdevice_disable()` disturbs RTC1, the counter restarts, and the unsigned subtraction wraps to
an enormous value.

1.0.24 counts **main-loop passes** instead — a clock that stops or restarts cannot fool it — and
renews the lease from inside `flash_nrf5x_erase()`'s page loop, because a multi-second erase is the
device *working*, not the host falling silent. The dangerous case, a pulled cable, is caught
immediately by the VBUS register read rather than by that counter.

## Reading the trace: the layout is an ABI

The scan-response layout is shared between `scan_response_init()` in `dfu_transport_ble.c` and
`MSV3/scripts/msv3_read_dfu_trace.py`. **Three bytes have been reused across builds:**

| byte | 1.0.14–1.0.17 | 1.0.18–1.0.20 (withdrawn) | 1.0.21+ |
|---|---|---|---|
| +10 | `fail_completed_hdr0` | `init_retry` | `fail_completed_hdr0` |
| +14 | `fail_extracted_hdr0` | — | `reset_reas` (1.0.22+) |
| +7  | `good_completed_slot` | — | `reset_tag` (1.0.23+) |

The reader keys every reused byte on `build_id` for exactly this reason. **Four readings were
mislabelled during this work before that was done**, one of which printed a confident conclusion
about a field that did not exist in the build being read.

**Always check `build_id` and the nonce before believing anything else in the record.** The nonce is
the only field deliberately not cleared on arm; if it has not moved since the previous read, nothing
below it is evidence.

## What this cost, and why

Twenty-plus bootloader releases. An honest split:

**Load-bearing:** 1.0.14 (the measurement that proved the buffer mismatch), 1.0.16 (the pool fix),
1.0.24 (stock mode plus a lease that does not depend on a clock this board does not have). Roughly
four builds of real work.

**Wasted, and why:**

- **1.0.7, 1.0.8** — raised queue capacity and added an ingress gate. The queue was never full;
  `alloc_fail_rc` was `INVALID_PARAM`, not `NO_MEM`, and `data_queue_element_alloc()` tests the type
  byte *before* it tests fullness. Both fixed a problem that did not exist.
- **1.0.7–1.0.10** — evidence invalidated retroactively. The trace had no nonce, so four readings
  treated as fresh may all have been stale. This is why the nonce exists now.
- **1.0.13** — a real latent defect (`transmit_buffer()` strands `SLIP_TRANSMITTING` forever on USB
  because the only resume lives in the non-USB branch) that is never reached in this failure.
  Correct code, wrong bug. Kept.
- **1.0.18–1.0.20** — withdrawn entirely. All three tried to tolerate an asynchrony that should not
  exist.
- **1.0.21–1.0.23** — the RTC1 lease.

**The two process failures underneath all of it:**

1. **Upstream was never checked.** Stock wired DFU has worked for years on Feather nRF52840 boards.
   One look at that `if (_ota_dfu) … else usb_init()` branch would have shown on day one that a
   stock wired session has no SoftDevice, no async flash, and therefore none of the races that were
   being invented fixes for. The maintainer had to ask for this explicitly before it happened.
2. **Own instrumentation was trusted over its source.** The stale-trace readings, the inverted
   transport constant, a spurious "ring has drifted" warning printed from un-latched sentinels, and
   four mislabelled bytes all come from the same habit.

Both are now standing rules in `CLAUDE.md` / `AGENTS.md`.

## Next steps

- Probes 4, 5, 6 and 7 are untouched and still on their previous bootloaders. Each needs its own
  `uicr` read before any 1.0.4+ delivery; probe 6 is coated and should be last.
- The retained trace is 96 bytes at `0x2003FF98`, bought with 32 bytes of bootloader stack that the
  linker asserts **do not** validate — they prove non-overlap only. Give that space back now the
  diagnosis is closed.
- The withdrawn patches (1.0.9, 1.0.15, 1.0.18–1.0.20) are kept on disk as records of disproven
  approaches. The build script throws on each.
