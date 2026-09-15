# Capacitive Soil Moisture Sensor v1.2 AFE checkout

**Date:** 2026-09-14

**Purpose:** Drive an experimental probe trace with the inexpensive v1.2 analog front end and log
its live output overnight, independently of MSV3's FDC2214 firmware.

The serial-only ESP32-S3 sketch is
`MSV3/arduino/ESP32_Capacitive_v1_2_Serial_Checkout/ESP32_Capacitive_v1_2_Serial_Checkout.ino`.
It initializes no BLE or cloud identity and applies no duty cycle. Wiring is VCC to 3V3, GND to
GND, and AOUT to GPIO4/ADC1_CH3. The sketch prints the five-sample median, exponential smoothing,
session minimum/maximum, and span every 500 ms; lower millivolts correspond to greater capacitance.

The initial decay to zero was not an oversized probe capacitor or firmware filtering problem. It
was a missed physical connection. After repair, hand loading moved the output from roughly 2.6 V
to 1.8 V and the setup was left running for the overnight comparison. Use
`MSV3/scripts/monitor_capacitive_v1_2_serial.ps1` for a no-reset live COM-port monitor.

## Hybrid rig (sensor 9), added 2026-09-14

A second ESP32-S3 now carries the same v1.2 analog front end with an **MSV3 probe end soldered in
place of the module's own sense pad**. It runs `MSV2_Moisture_AFE_Test.ino` -- the same sketch as
the original bench rig -- and advertises as **sensor 9** so both rigs log the same days in the same
room. The original rig (sensor 3, `E0:72:A1:AE:BB:D5`) is deliberately left untouched as the
control, so the only difference between the two series is the probe end. The question is whether
the MSV3 electrode removes the temperature-shaped artefact that rung 1 of the duty-cycle ladder
did not.

Both rigs run the identical duty cycle, confirmed from the sensor 9 boot banner:

```
[MODE] MSV3-CYCLE -- measure every 10000 ms, advertise every 60000 ms for 5000 ms
[MODE] rung 2 off -- sensor module permanently powered from 3V3 (not rewired)
[MODE] rung 3 off -- CPU stays at full clock
```

Keep them in step. Changing the ladder rung on one rig and not the other reintroduces exactly the
confound the ladder exists to isolate.

Sensor identity is now a **build parameter**, not a hand-edited constant, for the same reason MSV3
made probe identity one (`ba76e29`): two boards running one sketch is precisely how both end up
advertising the same ID. Each ID also builds into its own directory so a stale `--input-dir` cannot
push one rig's image to the other.

```
powershell -ExecutionPolicy Bypass -File MSV2\scripts\arduino_moisture_afe_test.ps1 upload -Port COM13 -SensorId 9
```

Receiver: `MOISTURE_INVERT_CEILING_BY_SENSOR[9]` is 3300, the same ceiling as sensor 3. The two
rigs are being compared directly, so giving them different ceilings would bake a presentation
difference into an A/B whose whole point is that only the probe end differs. Without the entry it
would fall through to `DEFAULT_MOISTURE_INVERT_CEILING` (200) and every millivolt reading would
clamp.

First observation, 2026-09-14: sensor 9 advertising from `AC:A7:04:31:8A:09`, wake counter
advancing, alongside sensor 3 at wake 31. No probe was attached to the hybrid at flash time, so its
first readings were an unloaded-input artefact (53-270 mV alternating sample to sample, mean
absolute step 144 mV), not a moisture signal.

The probe end was attached at `2026-09-15 01:21 UTC` and the signal became immediately sane:
**2409-2411 mV**, holding within 2 mV across ten consecutive advertisements, against the roughly
2.2-3.0 V dry baseline this front end is expected to produce.

**Nine unloaded-input rows were deleted from cloud history** (`public.msv2_sensor_history`,
`sensor_id = 9`, ids 617379-617422, `01:09:21`-`01:19:58 UTC`). The cut point was not a judgement
call about which values looked wrong: the board's **wake counter reset to 1** when it was
power-cycled to attach the probe, so the bad and good series are separated by an independent
reboot boundary, and the delete was scoped by primary key rather than by value or timestamp. The
rows were backed up before deletion.

Nothing else needed cleaning. Cloud auto-calibration is a *computed* function over
`msv2_sensor_history` (`msv2_read_sensor_auto_calibration`, 5th/95th percentile), not a stored
table, so removing the rows removed them from the learned range too -- re-reading it afterwards
returned `dryBound 890 / wetBound 891`, `oldestReadingAt 01:20`, and no false watering rises. The
receiver's own "history" is an upload outbox that discards acknowledged rows, so it never held a
durable copy; its cached learned range is replaced wholesale by the next `runCloudAutoCalPull()`
within about ten minutes. Sensor 3's learned range was untouched (`850/1130`, 5124 samples).

## Serial monitoring pitfall: never open these boards with DTR/RTS asserted

On this ESP32-S3 (USB-Serial-JTAG), the serial control lines are boot straps: **RTS drives EN,
DTR drives GPIO0**. Opening the port with both asserted holds the chip in reset with GPIO0 low, so
releasing them on close boots it into **download mode** -- the board then stops running the sketch
and stops advertising, while still presenting a COM port. Measured 2026-09-14: a read with
`DtrEnable=$true; RtsEnable=$true` returned a correct banner and silently left the board in
download mode, which then read as "the firmware is not advertising" in a BLE scan and cost two
diagnostic cycles.

`MSV3/scripts/monitor_capacitive_v1_2_serial.ps1` already does the right thing
(`DtrEnable=$false; RtsEnable=$false`, leaving both straps alone) -- use it rather than opening a
raw `SerialPort`. To recover a board already sitting in download mode, drive an esptool-style hard
reset: GPIO0 high and pulse EN.

```powershell
$p.DtrEnable=$false; $p.RtsEnable=$true;  Start-Sleep -Milliseconds 150
$p.RtsEnable=$false
```
