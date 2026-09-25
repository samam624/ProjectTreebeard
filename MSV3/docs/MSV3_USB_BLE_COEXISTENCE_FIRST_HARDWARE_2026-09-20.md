# MSV3 bootloader on hardware: coexistence proven, and the wired update's root cause found

> **SUPERSEDED 2026-09-21. Read [the resolution](MSV3_WIRED_DFU_RESOLVED_2026-09-21.md) first.**
>
> Both flashing paths now work on probe 8 with bootloader 1.0.24, proven with the unmodified stock
> client. The cause was local, not upstream: 1.0.4 attached USB inside the SoftDevice-enabled
> branch, which made wired DFU asynchronous. **Most of the hypotheses in this document were later
> disproven by measurement** -- the queue was never full, the transmit path was never at fault, and
> the host was always building correct frames. It is kept as the record of the search, not as a
> description of the defect.

**Date:** 2026-09-20
**Board:** probe 8 (MSV3-MOISTURE-008), on the bench, USB-powered throughout.
**Owner of the acceptance criteria:** `MSV3_USB_AND_BLE_OTA_COEXISTENCE_REQUIREMENT_2026-09-14.md`.
This document reports measurements against it; it does not amend it.

## Summary

Bootloader 1.0.4 is installed on a real board for the first time, delivered **entirely over BLE with
no SWD**. In DFU mode it runs **USB CDC and BLE DFU at the same time** — observed simultaneously,
not inferred. BLE DFU is unaffected by USB being live.

**The wired update itself does not complete.** USB CDC enumerates in DFU mode and accepts the start
of a serial transfer, then fails reproducibly about 13–14 packets in. That is an open defect, not a
configuration mistake, and it is the reason this document is not titled "requirement met."

> **UPDATE, same day, after nine diagnostic bootloaders.** The cause is now known and is one missing
> callback, in upstream SDK code, on the USB path only. See
> [THE ROOT CAUSE](#the-root-cause-a-truncated-acknowledgement-that-nothing-ever-resumes) below.
> Bootloader 1.0.13 carries the fix. Everything between here and that section is the search, kept
> because most of it is a record of hypotheses that were WRONG, and two of them cost five releases.

## THE ROOT CAUSE: a truncated acknowledgement that nothing ever resumes

**`hci_slip.c`'s `transmit_buffer()` abandons a frame the instant the CDC TX FIFO refuses a byte,
and under `NRF_USBD` nothing ever resumes it.**

```c
static void transmit_buffer(void)
{
    while (m_tx_buffer_index < m_tx_buffer_length)
    {
        err_code = send_tx_byte();
        if (err_code == NRF_ERROR_NO_MEM || err_code == NRF_ERROR_BUSY)
        {
            // No memory left in UART TX buffer. Abort and wait for APP_UART_TX_EMPTY to continue.
            return;                      // <-- leaves m_current_state == SLIP_TRANSMITTING
        }
    }
    ...
    m_current_state = SLIP_READY;
}
```

The comment names its own contract: *wait for `APP_UART_TX_EMPTY` to continue*. The only code that
honours it is in the **`#else`** branch of the same file:

```c
if (uart_event->evt_type == APP_UART_TX_EMPTY && m_current_state == SLIP_TRANSMITTING)
    transmit_buffer();
```

Under `NRF_USBD` that branch is not compiled. There is no `APP_UART_TX_EMPTY`, no
`tud_cdc_tx_complete_cb`, and no other caller. So the first time the CDC TX FIFO is full,
`m_current_state` sticks at `SLIP_TRANSMITTING` **permanently**, and from that moment
`hci_slip_write()` answers every acknowledgement with `NRF_ERROR_NO_MEM`:

```c
case SLIP_TRANSMITTING:
    return NRF_ERROR_NO_MEM;
```

`hci_transport.c`'s `ack_transmit()` discards that return **by design** — its own comment says
acknowledgement packets "are considered to be from system design point of view unreliable packets."
On a UART that reasoning holds, because the ISR always comes back. On USB it does not, and so the
device stops acknowledging with nothing anywhere reporting an error.

### How that becomes `alloc_fail_type 0x00`

The host is what notices. `adafruit-nrfutil`'s `get_ack_nr()` waits one second, gives up, and does
this:

```python
if is_timeout(start, DfuTransportSerial.ACK_PACKET_TIMEOUT):
    HciPacket.sequence_number = 0          # reset the host's counter
    self._send_event(DfuEvent.TIMEOUT_EVENT, ...)
    break
```

The device's `packet_number_expected` keeps counting from where it was. The two are now desynced,
and the wreckage downstream is what the trace had been reporting all along.

### Why five releases were spent on the wrong thing

The decisive value was one byte, and it arrived only in 1.0.11:

| field | value | what it rules out |
|---|---|---|
| `alloc_fail_rc` | `0x07` `NRF_ERROR_INVALID_PARAM` | **not** `NO_MEM`. The DFU queue was never full. |
| `alloc_fail_type` | `0x00` | the packet-type byte was rejected outright |
| `rx_seen` / queued / dropped | 16 / 1 / 15 | frames arrived and were thrown away |
| `sched_fail` | 0 | the scheduler was never the constraint |

`data_queue_element_alloc()` tests the type byte **before** it tests capacity:

```c
if (INVALID_PACKET == packet_type)  retval = NRF_ERROR_INVALID_PARAM;
else if (DATA_QUEUE_FULL())         retval = NRF_ERROR_NO_MEM;
```

So **1.0.7 (queue depth 4 -> 17) and 1.0.8 (ingress gate) were both fixing capacity, and capacity
was never the constraint.** Both are still in the tree and both are still defensible on their own
terms, but neither had anything to do with this defect. They were built because the failure looked
like backpressure — and it *is* backpressure, just in the opposite direction: the **transmit** side,
not the receive side.

### Why it looked like a timing bug

It is pacing-sensitive, and that sent the search toward races:

| host pacing | result |
|---|---|
| default | 1 frame queued, 15 dropped, device soft-resets, COM port vanishes |
| 100 ms between frames | 15 queued instead of 1 — still 15 `INVALID_PARAM` drops |
| stop-and-wait, 3 frames 4 s apart | **no reset at all** |

That is exactly what a full transmit FIFO predicts: give the host enough spacing and the FIFO drains
between frames, so the stall never happens.

### The fix, in bootloader 1.0.13

`MSV3/bootloader/patches/msv3-1.0.13-serial-tx-pump.patch`:

1. **`hci_slip_tx_pump()`** — flushes the CDC FIFO and re-enters `transmit_buffer()` whenever the
   state machine is stuck at `SLIP_TRANSMITTING`. This is the missing resume.
2. **Called from `bootloader.c`'s main loop after `tud_task()`**, not from a TX-complete callback.
   A periodic pump also covers "the FIFO has room now but no callback fired."
3. **`hci_slip_rx_pump()` will not drain while a transmission is stalled.** This is correctness, not
   politeness: `ack_transmit()` encodes into a single `static uint8_t ack_packet[4]`, so decoding
   another frame while the previous ACK is half-sent would overwrite the bytes still in flight.
   Leaving bytes in the CDC FIFO NAKs the host at the USB layer, which simply waits.
4. **The host is untouched**, which is the whole point — the goal is the stock Arduino IDE upload
   button, not a patched client.

Counters added so the fix can be *proved* rather than assumed: `tx_block_count`,
`tx_resume_count`, `ack_frames_done` (compared against `rx_seen`), `produce_nomem`, and the first
HCI sequence mismatch. Read them with `MSV3/scripts/msv3_read_dfu_trace.py --probe N`.

The retained trace grew from 64 to 96 bytes to hold them. The boot-guard record above it is an
application ABI and cannot move, so the base moved **down** to `0x2003FF98`, costing 32 bytes of
bootloader stack. Both the linker `ASSERT`s and `build_msv3_release.ps1`'s map check were updated,
and the map check is version-aware so a 1.0.13 image cannot link at the old base.

## What was proven

**1. A bootloader can be delivered over the air.** The design document's "one unrecoverable window"
paragraph was withdrawn on the basis of source reading; this is the hardware half. Probe 8 went from
1.0.3 to 1.0.4 as two signed DFU sessions out of one package, with no SWD at any point:

| Phase | Payload | Time |
|---|---|---|
| 1 | SD+BL, 190,016 B (SD 151,016 + BL 39,000) | 33 s |
| 2 | application, 132,672 B | 30 s |

Phase 2 correctly found the bootloader at the **recovery** address `DB:AD:4D:9D:21:BB`, which is
where a bootloader with no valid application advertises.

**2. The precondition held, and was measured rather than assumed.** `UICR.NRFFW[1] = 0x000FE000`
read off probe 8 over its USB console before anything was sent. That is the MBR params page, which
is what lets an interrupted `SD_MBR_COMMAND_COPY_BL` resume on the next boot.

**3. USB and BLE are live together in DFU mode.** One 30-second observation, both at once:

```
BLE   DB:AD:4D:9D:21:BB  name 'AdaDFU'  300 packets in 30 s
      service_uuids=['00001530-1212-efde-1523-785feabcd123']   (Nordic legacy DFU)
USB   USB\VID_239A&PID_00DA\1A2F0613682FD239  ->  COM19
```

`PID 0x00DA` is the **bootloader** product id; the application uses `0x80DA`. So that COM port is the
bootloader's, not the application's — the distinction the requirement document specifically warns
against getting wrong ("USB enumeration by a valid application must not be mistaken for proof that
the bootloader can accept a wired firmware image"). 300 packets in 30 s is continuous advertising,
not the application's 5-second-per-minute window. DFU entry is persistent: the board held it through
20 s of polling and did not fall out.

**4. BLE DFU still works with a cable attached and enumerated.** Design document risk #1. Three
separate BLE transfers completed this way. The one anomaly — a late final packet receipt at
132672/132672 bytes — is pre-existing: `ble_ota_update.py` has carried a named case for that
end-of-image flush since before 1.0.4 existed, so it is not evidence of USB interference.

## The open defect

`adafruit-nrfutil 0.5.3.post16`, signed `0x3356` application package, board already in DFU:

```
adafruit-nrfutil dfu serial -pkg <pkg>.zip -p COM19 -b 115200 --singlebank
```

| Attempt | Flags | Packets | Outcome |
|---:|---|---:|---|
| 1 | no `--singlebank` (dual-bank flow) | 13 | `Timed out waiting for acknowledgement from device` |
| 2 | `--singlebank` | 13 | `ClearCommError PermissionError(13)` |
| 3 | `--singlebank` | 14 | `ClearCommError PermissionError(13)` |

`--singlebank` **is required** — without it the dual-bank flow desyncs immediately against this
single-bank bootloader, so attempt 1 tells us little beyond that.

The single-bank failures are the interesting ones, and the error matters: `ClearCommError` /
`PermissionError(13)` is **the COM port disappearing mid-transfer**, i.e. a device-side reset or USB
drop, not a host-side timeout waiting on a silent device.

### RESOLVED LATER THE SAME DAY: the stall was host-side timing, and the real blocker is elsewhere

Two further measurements moved this on, and both matter more than the table above.

**The transport is fine; the stock client is impatient.** Rerunning with
`FLASH_PAGE_WRITE_TIME` scaled 6x (0.1024 s -> 0.6144 s) and the serial read timeout raised from
1.0 s to 10.0 s, changing **nothing else** — same package, same signature, same device type, same
bootloader — carried all **259 frames** and printed `Activating new firmware`. The stopping point
moved from frame 13 to completion, which is exactly the prediction the flash-stall hypothesis made,
so treat it as confirmed: with the SoftDevice enabled and the radio advertising continuously,
bootloader flash writes are deferred behind radio activity and the client's fixed sleep and 1 s read
timeout are too short. **No bootloader change is indicated.**

**But the image still did not take.** After that "successful" wired transfer the board had **no valid
application**: it advertised `AdaDFU` persistently at the RECOVERY address `DB:AD:4D:9D:21:BB`, 152
packets in 20 s, and never came back as an application. The same package then installed cleanly over
BLE and the probe returned healthy, so the package itself is good.

So the wired path now splits into two separate problems, and only the first is solved:

| | state |
|---|---|
| USB CDC enumerates in DFU mode | **works** |
| Serial transport carries a full image | **works, with a patient client** |
| Delivered image passes on-device validation | **FAILS — this is the real blocker** |

**A promising suspect was tested and RULED OUT — do not spend time on it again.**
`DfuTransportSerial.send_init_packet()` appends `int16_to_bytes(0x0000)` after the init packet,
legacy padding for the original 14-byte unsigned SDK init packet. A DFU 0.8 signed init packet is
116 bytes (52-byte init + 64-byte P-256 signature over `init_packet[0:52]`), and the BLE transport
sends it verbatim — `dfu_lib.py` does `write_gatt_char(DFU_PACKET_UUID, self.dat_data)` with no
padding — so "serial pads, BLE does not" looked like the whole answer.

It is not. Sending the init packet **verbatim at 116 bytes with no padding**, together with the
timing fix, still produced a transfer that reported `Activating new firmware` and still left the
board with **no valid application**, advertising persistent recovery at `:BB`. Three wired attempts
now:

| Attempt | Timing | Init packet | Transfer | Result on device |
|---:|---|---|---|---|
| A | stock | padded | stalls at frame 13/259 | app erased, recovery |
| B | 6x sleep, 10 s timeout | padded | **completes** | **no valid app**, recovery |
| C | 6x sleep, 10 s timeout | **verbatim, 116 B** | **completes** | **no valid app**, recovery |

The same package installed cleanly over BLE after every one of them, so the package is good and the
bootloader's validation of it is good. The difference is confined to what the serial path does.

### Six wired attempts, and what each eliminates

Worked through with a Codex consultation (`msv3-usb-serial-dfu-validate`). One board, one signed
package, a BLE restore to healthy after every single attempt:

| | Configuration | Result |
|---|---|---|
| A | 512 B chunks, stock client timing, padded init | stalls at frame 13/260, **COM port drops** |
| B | 512 B, patient timing (x6 page sleep, 10 s read timeout) | completes, **no valid app** |
| C | B + verbatim 116 B init packet, no 2-byte padding | completes, **no valid app** |
| D | C + 100 ms sleep after **every** frame | completes, **no valid app** |
| E | 128 B chunks + 15 ms/frame + verbatim init | completes, **no valid app** |
| F | 512 B + verbatim init + **5.0 s hold before STOP_DATA**, port open | completes, **no valid app** |

Six wired failures, six BLE successes, minutes apart, same package, same bootloader.

**Eliminated, and stated at the strength the evidence actually supports** — the calibration here is
Codex's, and it matters, because two of these are weaker than they first look:

- **Client timing/impatience** — eliminated as the cause of the *completed-but-invalid* failures. It
  was real and it did cause A: A is an independent backpressure/reset problem that still deserves
  its own fix.
- **The 2-byte init padding** — eliminated outright (C).
- **Queue overflow** — *very unlikely*, not eliminated. D makes ordinary saturation implausible, but
  only an on-device allocation-failure counter proves it.
- **Packet geometry** — narrow to "the 512-byte maximum-payload hypothesis is disfavored" (E). Both
  512 and 128 are word-aligned, so a shared length-unit, payload-offset or pointer-lifetime bug
  survives this test.
- **Final flash write not drained before validation** — F strongly excludes *normal* write latency.
  It does **not** exclude a write that was never submitted, never completed, or completed with an
  ignored error.

## ROOT CAUSE, found by instrumentation and proven on hardware

The instrumented bootloader (1.0.6) answered it in one run. Read passively out of scan-response
manufacturer data — it never touched the USB endpoint under investigation:

```
transport      SERIAL
stage          1 = ARMED (never reached validate)
m_dfu_state    before=255 after=255        (255 = "not written")
data_received  0
image_size     0
validate_rc    not reached
activate_rc    not reached
frames_dropped 15
```

**`STOP_DATA_PACKET` was never processed.** `dfu_image_validate()` and `dfu_image_activate()` were
never called at all. Every one of the six host-side experiments above was tuning the delivery of
packets into a handler that was never entered.

**The mechanism.** Stock `dfu_transport_serial.c` sizes its packet queue at `MAX_BUFFERS = 4`, which
`DATA_QUEUE_FULL()` turns into **3 usable entries**, while the HCI receive pool is
`HCI_RX_BUF_QUEUE_SIZE = 16`. HCI **acknowledges a frame at receive time**;
`rpc_transport_event_handler()` runs afterwards, and any frame it cannot queue is consumed and
discarded:

```c
retval = data_queue_element_alloc(&element_index, p_rpc_cmd_buffer[0]);
if (NRF_SUCCESS == retval) { ...queue it...; app_sched_event_put(...); }
if (p_rpc_cmd_buffer != NULL && NRF_SUCCESS != retval) {
    retval = hci_transport_rx_pkt_consume(p_rpc_cmd_buffer);   // silently DROPPED, already ACKed
}
```

That path does not discriminate by packet type. START, INIT, DATA and **STOP** are equally
droppable, and STOP is a single packet with no retry — so losing it once loses the session while the
host sees an ACK for everything it sent. Combined with stock discarding both validate and activate
return values, a wired update can deliver every byte, report success, and leave no application.
That is the worst possible failure shape for a probe with no SWD, and it is why this took so long to
see from the host.

> **Be precise about what is and is not proven.** `stage == ARMED` proves STOP was never
> **processed**. It does **not** prove STOP was one of the 15 queue-allocation failures. STOP could
> equally have been admitted to the queue and then stranded by a missing scheduler event, or never
> have reached the transport callback at all because USB went away first — the stock client dying
> around frame 13 with the port disappearing makes that a live possibility. So: *the transport
> defect is confirmed*; *the specific fate of this STOP is not*. Those are two different claims and
> only the first is established.

### The fix so far, and why it is not finished

**1.0.7** raises the queue credit from 3 to 16 to match the HCI pool, so exhausting the pool
produces real USB-level backpressure rather than silent loss. It also repairs a stock leak: a failed
`app_sched_event_put()` left an allocated queue entry pointing at a buffer the tail of the same
function then consumed. Cost: RAM +156 B, FLASH +16 B.

**It is not sufficient, and it must not ship as a production fix.** A repeat wired attempt on 1.0.7
read back **exactly 15 drops again, byte-identical to 1.0.6 in every field**. Raising usable credit
from 3 to 16 changed nothing whatsoever.

**The first thing to suspect there is the diagnostic, not the device.** The record carries no
attempt nonce and no build id, so a byte-identical reading is equally consistent with *the 1.0.7
attempt never having armed a fresh record at all* — in which case the scan simply re-read the 1.0.6
one. That must be ruled out before a single conclusion is drawn from the repeat.

And "exactly 15" most likely names a **16-entry ring with one sentinel slot**, not a phase of the
transfer. It does not implicate the application erase on its own: a producer that admits a fixed
burst before failing will drop the same count regardless of how much downstream capacity exists.

Retaining sixteen HCI buffers may also be *aggravating* transport starvation, which is consistent
with the stock `adafruit-nrfutil` client still dying around frame 13 with the COM port disappearing.
The scheduler-ownership repair in 1.0.7 is worth keeping; the capacity bump is not established and
**`MAX_BUFFERS` should not be raised again**.

## The defect, finally located: buffer lifetime, not flow control

Three more bootloaders narrowed it to one thing.

**1.0.8** added real flow control: `hci_slip.c` stops draining the USB CDC FIFO while the DFU packet
queue is near capacity, so TinyUSB NAKs the host at the USB layer instead of HCI acknowledging
frames that would then be discarded. It is pumped from the bootloader event loop because once the
gate closes the host is NAKed and no rx callback can reopen it. **This is correct and worth
keeping** — but the wired transfer still died around frame 13 with the COM port disappearing.

That symptom is the tell. `ClearCommError`/`PermissionError(13)` is the **device dropping off USB**,
not a stalled endpoint — so the board was *resetting*, which is what this file's `APP_ERROR_CHECK()`
does with any flash error.

**Here is the actual mechanism.** `dfu_data_pkt_handle()` branches on `is_ota()`:

```c
if ( is_ota() ) { ...pstorage_store(..., p_data, ...)... }   /* asynchronous, SoftDevice-mediated */
else            { flash_nrf5x_write(..., p_data, ...); }      /* synchronous, direct */
```

`is_ota()` returns the global `_ota_dfu`, which `DEFAULT_TO_OTA_DFU` forces true whenever there is
no valid application — i.e. throughout any wired update. Stock never noticed, because stock brought
up **one** transport: if serial was running, `_ota_dfu` was false and the synchronous branch ran.
**1.0.4 made both transports live, so wired transfers began taking the asynchronous branch.**

And the asynchronous branch is incompatible with this transport's buffer lifetime. `pstorage_store()`
is handed `p_data`, which on the serial path points **into the HCI receive buffer** — and
`process_dfu_packet()` calls `data_queue_element_free()` as soon as the handler returns, which
consumes that buffer and hands it straight back to the pool. The SoftDevice is then writing flash
out of memory the transport has already recycled. The BLE transport is immune because it copies
into its own buffer first.

### 1.0.9 was the wrong fix, and is WITHDRAWN

1.0.9 forced the synchronous path for wired sessions. On hardware it made things **strictly worse**:
the transfer died on the *first* write instead of around frame 13. The reason is that `is_ota()`
does not select a transport — it selects **whether flash goes through the SoftDevice**. With the
SoftDevice enabled the asynchronous path is *mandatory*, and a direct `flash_nrf5x_*` call faults
immediately.

`build_msv3_release.ps1` now **refuses to build 1.0.9**. The patch is retained only as the record of
a disproven approach.

### STOP: the trace readings are not trustworthy, and I kept building on them

Bootloaders **1.0.6, 1.0.7, 1.0.8 and 1.0.10 all reported byte-identical records** — `stage=ARMED`,
`frames_dropped=15`, validate and activate not reached. Four materially different builds, including
one that adds an ingress gate specifically to prevent drops, cannot all produce the same drop count.

The record carries **no build id and no attempt nonce** — precisely the gap the consultation flagged
before any of these builds. So either it is not being re-armed and every reading after 1.0.6 is
stale, or the drops are not queue-full at all.

That second possibility deserves weight: `data_queue_element_alloc()` also returns
`NRF_ERROR_INVALID_PARAM` when the packet-type byte is `INVALID_PACKET`. A **type rejection** would
drop frames regardless of how much queue space exists, and would look identical to queue pressure
from the outside — which would explain why raising capacity (1.0.7) and adding a gate (1.0.8)
changed nothing.

**Treat the `15` as measured for 1.0.6 only.** Nothing after that is evidence.

What *is* directly observed and reliable, on every build: the wired transfer dies around frame 13 of
260 with the **COM port disappearing** (`ClearCommError`/`PermissionError(13)`), which is a device
reset, and the board is left with no application.

### What was built, and what is worth keeping

| Release | Change | Verdict |
|---|---|---|
| 1.0.5 | Quiesce the radio before activate, mirroring the BLE path | Harmless, keep |
| 1.0.6 | Retained DFU trace + scan-response readout | Found the silent-success mode; **needs a nonce** |
| 1.0.7 | Queue credit 3 → 16, sched-failure leak repair | Leak repair worth keeping; capacity unproven |
| 1.0.8 | USB ingress gate (CDC FIFO not drained without credit) | Principled; **verified active** in the link map — the strong `msv3_serial_rx_may_drain` in `dfu_transport_serial.o` won over the weak one in `hci_slip.o`, so it is not inert |
| 1.0.9 | Force the synchronous flash path | **WITHDRAWN**, made it strictly worse, build script refuses it |
| 1.0.10 | Defer the DATA free to the write-completion callback | Principled, keep |

None of them fixed the symptom.

### Do this before writing another bootloader

Five of the six above were guesses dressed as fixes, because the instrument was never validated.
**Validate it first:**

1. Add a **build id and per-attempt nonce** to the trace, confirm `frames_dropped` is not saturating,
   and confirm it re-arms on every serial attempt.
2. Then the per-opcode table — `rx_seen` / `queued` / `processed` / `alloc_fail` *with its exact
   return value* / `sched_fail`, for START, INIT, DATA and STOP — plus `RESETREAS`. The exact
   `alloc_fail` return code settles queue-full versus type-rejection in one run.

### The earlier fix hypothesis, retained

Keep the async path — it is required — and fix the lifetime instead. Either:

1. **Defer the free.** The serial transport already receives `m_data_pkt_cb(DATA_PACKET, result,
   p_data)` from `pstorage_callback_handler()` on write completion. That callback, not the end of
   `process_dfu_packet()`, is where `data_queue_element_free()` belongs. This is the principled fix
   and it also makes `dfu_cb_handler()`'s currently-pointless `APP_ERROR_CHECK(result)` meaningful.
2. **Bounce-copy** into a stable buffer before `pstorage_store()`. Simpler, but needs enough buffers
   for every write pstorage may have queued at once, so it trades a lifetime bug for a capacity one.

Option 1, with 1.0.8's ingress gate already limiting how many frames can be in flight, is the
combination to try next.

### Earlier next steps, retained

1. **Validate the diagnostic itself.** Add a build id and a per-attempt nonce, confirm
   `frames_dropped` is not saturating, confirm it resets on every new serial attempt, and confirm
   the scanner is showing fresh bytes. Until this is done the 1.0.7 repeat proves nothing.
2. **Capture reset/disconnect evidence**: `RESETREAS`, boot count, last callback reached, and
   whether the USB disconnect happens *before* STOP is sent. Given the stock client's port
   disappearance, endpoint starvation or a reset is at least as likely as queue overflow.
3. **Count aggregate packet flow per opcode**, which is the decisive table:

   | opcode | rx_seen | queued | processed | alloc_fail | sched_fail |
   |---|---|---|---|---|---|
   | START / INIT / DATA / STOP | | | | | |

   plus queue depth and return code at the first rejection, first/last rejected ordinal,
   `STOP_SEEN_AT_RX` vs `STOP_PROCESSED`, and the host timestamp showing whether STOP was ever
   transmitted.
4. **Verify what `HCI_RX_BUF_QUEUE_SIZE == 16` actually sizes** — it may be a completed-buffer ring
   rather than the retainable buffer pool, and it may sacrifice a slot of its own. The assumption
   that it means sixteen simultaneously retainable buffers is not established.
5. **Only then choose the backpressure layer.** If STOP reaches the callback but admission fails,
   retain acknowledged frames and gate USB ingress or HCI ACKs. If STOP never reaches the callback
   because USB disappeared, no amount of application queue will fix it.

Acceptance must be stronger than one successful install: under a deliberately exaggerated stall,
**zero acknowledged frames may be discarded**, and the stock client must either succeed or fail
loudly — never report false success.

Do not special-case STOP_DATA to paper over this. All four packet types need ordered, lossless
delivery, and rescuing STOP alone would validate incomplete state while hiding the transport
violation.

### The earlier plan, which the instrumentation replaced

Every remaining hypothesis lives after `STOP_DATA` inside the device, and the device currently
reports nothing: `dfu_image_validate()` and `dfu_image_activate()` both have their return values
discarded, and the host's `send_validate_firmware()`/`send_activate_firmware()` send nothing at all.
There is no host-side experiment left that can distinguish the survivors.

Build one instrumented bootloader that answers all of it in a single install — it is deliverable
over BLE, so no SWD is needed. Record into `.noinit` with a magic, schema version, session counter
and checksum, and **read it before doing the BLE recovery**, since `.noinit` does not survive power
loss:

- reset reason, captured early, before anything clears it; active transport/owner identity
- `dfu_init_pkt_handle()` and `dfu_init_pkt_complete()` returns (both discarded today)
- parsed expected image length, hash, version, device type
- RPC DATA frames/bytes received, **queue allocation failures and silently consumed frames**
- scheduled DATA frames/bytes processed; first and last non-success from `dfu_data_pkt_handle()`
- flash submit/completion/error counts, and operations still pending at STOP
- DFU state and bank code at STOP, then after validate, then after activate
- validate return; activate return
- settings-save entered, submission return, callback reached, callback operation/result
- bank code loaded on the **next** boot; the app vector table's SP/reset-handler words if the bank
  reads valid but boot is still rejected

Two refinements worth building in: compute a CRC over the staged flash range **only on validation
failure**, so the success path's validate→activate timing is unchanged; and checksum each queued
payload at enqueue and again when processed — a mismatch there would directly prove alias/reuse
corruption from the pointer-based queue, which is the one structural suspect the tests above cannot
touch.

Keep it observational: no extra flash writes, no bypasses, volatile `.noinit` stores only in the
timing-sensitive callbacks. Then capture the same record from a **successful BLE run** as a control.
The first lifecycle field where the two diverge is the defect.

### Original candidate list, retained for context

1. `send_validate_firmware()` / `send_activate_firmware()` on the serial transport, versus what the
   BLE path sends. Activation is the step that must flip the application to valid, and it is the one
   part of the sequence not yet compared between the two transports.
2. `send_start_dfu()`'s size fields under `single_bank=True` — if the sizes the bootloader is told
   do not match what post-validation measures, `dfu_init_postvalidate()` fails exactly like this
   while every byte still arrives.
3. Whether 1.0.4's `dfu_transport_claim()` is handing the session to BLE, which is advertising
   throughout, leaving the serial writes consumed-and-dropped by design. This would fit all three
   attempts and is the only candidate specific to 1.0.4 rather than to the client.

Candidate 3 is the one that would change the design rather than the tooling, so it is worth
separating early: rebuild the bootloader without BLE advertising and retry the wired install on a
bench board. **Do not claim the wired path works until a signed wired install is verified end to end
by a probe that comes back advertising its application.**

### Original analysis, superseded by the above

**Not root-caused.** Candidates, with what each must explain:

- **USBD_IRQn at priority 2 against the SoftDevice under sustained writes** — the design document's
  own risk #1. Fits a device-side reset.
- **A watchdog unfed on the serial path specifically.** Any WDT theory has to explain the asymmetry:
  BLE transfers run 30–42 s on the same bootloader and are fine.
- **`dfu_transport_claim()` refusing the serial transport** because an earlier failed session left
  `m_dfu_state` non-idle. This fits attempt 1's silent no-ack — the arbiter is documented to consume
  and drop the packet so the host retries — but it does **not** fit the later port drops, and
  attempt 3 ran against a freshly re-enumerated bootloader.

### Where exactly it stops

Read out of the pinned client rather than guessed. `DfuTransportSerial.DFU_PACKET_MAX_SIZE = 512`,
and `update_progress()` in `nordicsemi/__main__.py` prints one `#` per frame. So:

- the image is **259 frames** of 512 B (132,672 B);
- it dies at **frame 13–14**, i.e. **~6,656–7,168 bytes, about 5%**.

That is **just past the first 4 KB flash page**, and this is the part worth carrying forward —
`send_firmware()` sleeps `FLASH_PAGE_WRITE_TIME` only when `count % 8 == 0`, because "after 8 frames
(4096 Bytes), nrf5x will erase and write to flash \[and\] nrf5x's CPU is blocked". Frames 1–8 fill
page 1 and get that settle. The failure lands in frames 9–16, filling page 2, **before** the next
settle.

This sharpens the leading hypothesis considerably. With the SoftDevice enabled, bootloader flash
writes go through the SoftDevice's asynchronous flash API, which timeslots around the radio — and in
1.0.4 the radio is **advertising continuously** during a wired transfer. A flash write that
previously completed inside the client's fixed sleep can now be deferred behind advertising events,
the CPU stalls longer than the client expects, and USB CDC is not serviced. The client's serial read
timeout is only `DEFAULT_SERIAL_PORT_TIMEOUT = 1.0 s`.

It also explains the asymmetry with BLE, which nothing else does: on the BLE path the host waits on
packet receipts with a deadline this project already raised to 6 s, so flash stalls are absorbed. On
the serial path the host has a fixed short sleep and a 1 s read timeout, and the port drops when the
device stalls past it.

**Next diagnostics**, before changing any code:

1. **Retry with BLE quiesced** — the single most valuable measurement, because it decides between
   "coexistence starves USB" and "USB is broken on its own", and the frame-13 evidence above now
   predicts a specific outcome: it should get materially further. Quiescing needs a bootloader build
   that does not advertise, so this is a build-and-SWD-install on a bench board, not a field test.
2. Confirm the stop is at a fixed FRAME rather than a fixed elapsed time, by retrying with the
   client's inter-frame sleep raised. If a longer sleep moves the stopping point, the flash-stall
   story is confirmed and the fix may be as small as a client-side timing change — which would mean
   no bootloader change at all.
3. Capture the bootloader's own serial output during the failure, if the CDC path can carry it.

## Status against the acceptance criteria

| Criterion | State |
|---|---|
| Valid application exposes its USB CDC diagnostics | Met (COM9, `PID_80DA`) |
| Wired firmware update completes without SWD | **NOT MET — the open defect above** |
| Application-side BLE DFU handoff still works | Met |
| Erased/absent application enters persistent BLE recovery automatically | Met, observed twice |
| Signed-init, device type `0x3356`, monotonic floor still enforced | Met |
| Equality-version rescue still possible | Not retested on 1.0.4 |
| A rejected lower-version package leaves a recovery path | **Not tested** |
| Tooling reports which path appears, rather than inferring from COM enumeration | Met — `PID_00DA` vs `0x80DA` is the discriminator |

Also untested: the deliberate two-host arbiter race, and the negative control of an erased
application reaching BLE recovery with **no cable attached**.

## For the rest of the fleet

Nothing here licenses another board. `mbr_params_page_verified` is per-probe and
`ble_ota_update.py --bootloader` refuses any probe without it — verified fail-closed against probe 6
during this session. Each board needs its own `uicr` read first. **Probe 6 goes last**: it is the
only conformally coated one, so it is the only board where a bad outcome cannot be undone.

Given the open defect, there is no reason to move any further probe to 1.0.4 yet. The wired path is
the entire point of the release, and it does not work.

## Tooling added this session

- `build_msv3_release.ps1 -BootloaderOta` — signed `0x3356` SD+BL+APP packages.
- `msv3_fleet.py`: `read_package(allow_bootloader=)`, `Package.contains_bootloader`,
  `Probe.mbr_params_page_verified`. Bootloader-bearing packages are still refused by default.
- `ble_ota_update.py --bootloader`, gated on `mbr_params_page_verified`, plus the two-phase transfer.
  nrfutil emits SD+BL and the application as two independently signed components, and `dfu_lib`'s
  `parse_zip()` is an if/elif chain that would have sent the bootloader and silently skipped the
  application.
- `uicr` command in `MSV3_Probe_Bringup_Test` — UICR boot-start/MBR-params words and the bootloader
  settings page, including the anti-rollback floor.
