# Identifying which physical MSV3 probe is on which Windows COM port

2026-09-11: needed to know which bench probe was on COM21 before working on it the next day.
There is no tool for this and none is planned -- this doc exists so the next person (or agent)
doing the same investigation starts from the dead ends already found, instead of re-walking
them.

## Why the obvious approaches don't work

**Windows device enumeration.** Every MSV3 E73/nRF52840 board reports the identical
`VID_239A&PID_80DA` (Adafruit-bootloader-range CDC) hardware ID, and none of them expose a real
per-device USB serial number -- the last segment of the PnP instance ID
(`USB\VID_239A&PID_80DA&MI_00\7&1C76C97E&0&0000`) is a Windows-generated hash, not anything the
board reported. Two different physical boards plugged into two different ports are
indistinguishable from `Get-CimInstance Win32_PnPEntity` output alone.

> **CORRECTED 2026-09-20: this is wrong, and it is the most useful thing on the page once fixed.**
> The instance ID quoted above is the **`MI_00` child** -- the CDC interface. That one is indeed a
> Windows-generated hash. The **composite parent** carries a real, chip-derived serial:
>
> ```
> USB\VID_239A&PID_80DA\1A2F0613682FD239     <- probe 8
> USB\VID_239A&PID_80DA\695715B3A7F8217E     <- probe 5
> ```
>
> Sixteen hex digits, no `&` separators, stable across replug: that is the board's own
> `iSerialNumber`, which the Adafruit nRF52 core derives from the chip's factory `FICR->DEVICEID`.
> It is a hardware-unique identity, so a probe-to-port match made this way is **deterministic**,
> not the convergent circumstantial evidence the rest of this document settles for.
>
> Pair the parent with its child by `DEVPKEY_Device_LastArrivalDate` -- both functions of one
> device share a timestamp to the second, which is how the two IDs above were bound to their COM
> ports. Note this identifies the *board*, not the sensor ID compiled into it; confirm that over
> BLE, or record the serial in `MSV3/fleet.json` once (probe 8's is recorded there now) and the
> mapping becomes a lookup.
>
> The convergent-evidence method below still works and is still the only option for a board that
> is not plugged in. It is no longer the best option for one that is.

**`MSV3/fleet.json`.** Deliberately has no COM-port field -- see its own `_comment`. Ports
aren't a stable identity: unplug and replug the same board and Windows may hand it a different
COM number, since there's no persistent serial number to recognize it by.

**Reading the boot-time serial banner.** `MSV3_Moisture_BLE_Sleep.ino` prints
`[DIAG] sensor id: N` (search for that string), but only once, gated behind
`while (!Serial) delay(10)` for `BOOT_HOST_WAIT_MS` immediately after boot. If the board has
been running for any length of time before you open a monitor, that window is long gone and
nothing will ever print again. Most builds also ship with `ENABLE_SERIAL_LOGS` off entirely
(it's a compile-time flag, not a runtime one), so even a fresh boot may print nothing at all.
Don't assume silence means "wrong board" -- it as often means "logging compiled out."

**1200-baud "touch" / DTR-RTS reset to force a fresh boot banner.** Tempting, since it would
solve the above -- but on Adafruit's nRF52 core this specifically resets the board into the UF2
bootloader, not just the application. Doing that to a board that's mid-experiment (a guard
watchdog test, an OTA session) throws it away for no good reason. Never do this just to get an
identifying string; if a reset is genuinely warranted, that's a decision to make deliberately,
not a side effect of trying to read a log line.

## What actually worked: BLE, with caveats

Only a probe currently on USB power reports `charging` in its BLE advertisement's flags byte
(`scripts/msv3_fleet_scan.py` decodes this). That narrows the candidate set to exactly the
probes plugged in right now -- if two ports are connected and the fleet scan shows exactly two
probes charging, matching is off to a good start. Cross-checking two more signals nails it down
fully:

- **Windows' COM-port last-arrival timestamp** (`Get-PnpDeviceProperty -InstanceId <id>
  -KeyName DEVPKEY_Device_LastArrivalDate`) -- when did this port last appear.
- **The advertisement's wake counter** -- this firmware wakes roughly once a minute, so a board
  that was just connected N minutes ago should be at wake~=N, while one that's been running for
  days will have a much higher count.

If both signals point to the same pairing, that's strong (not proof) evidence. Confirmed
2026-09-11: COM21 arrived ~8 minutes before a scan that showed a probe at wake=8, while COM9
had been connected continuously since two days prior and its counterpart probe was at wake=105.

### Where this can mislead you

- **A single scan can genuinely miss the window.** Probes advertise ~5s out of every 60s. A
  75-second scan should mathematically catch at least one window, but in practice (observed
  2026-09-11) a scan can still come back showing zero probes charging even though a board is
  sitting right there on USB power -- likely `bleak`/the Windows BLE stack missing the
  advertisement, not the board being silent. A "no charging probe seen" result is not proof
  nothing is connected; rerun before concluding that, ideally for 90s or more.
- **The wake-counter assumption only holds if plugging in USB actually reboots the MCU.**
  That's usually true for these boards but isn't a documented guarantee. If a board was already
  running (not freshly booted) when you plugged it in, its wake counter reflects however long
  it had already been running, not how long it's been on this port -- and the "recently arrived
  port = low wake count" pairing breaks.
- **Ports are not stable across a session.** A board that was on COM21 can vanish and a
  completely different device can take a nearby COM number later (observed: after COM21's board
  was unplugged, an unrelated ESP32-S3 on `VID_303A` showed up as COM16 -- a different product
  entirely, not an MSV3 probe). Always re-verify hardware IDs match `VID_239A&PID_80DA` before
  trusting a COM-port list; don't reuse a mapping from earlier in the same sitting without
  rechecking.
- **`charging` may not mean "USB present."** It's plausible this bit reflects actual
  charge-controller state (i.e. it could clear once a battery reaches full charge) rather than
  raw VBUS presence, which would make a fully-charged-but-still-plugged-in board
  indistinguishable from an unplugged one by this method alone. Not confirmed either way as of
  this writing -- flagged here so it isn't mistaken for settled.
- **None of this is proof.** It's convergent circumstantial evidence from independent passive
  signals, which is why it was trusted here, but it is not a hardware-level identity check.

## The only deterministic check

Ask whoever is at the bench to physically unplug the one port in question, then rescan. If the
board has a battery, its `charging` flag should drop (it may not go silent at all); if it's
USB-power-only, it should stop advertising entirely. Either way, whichever probe changes state
right after the unplug is the one that was on that port. This is the fallback when the
convergent-evidence method above comes back ambiguous or contradictory.
