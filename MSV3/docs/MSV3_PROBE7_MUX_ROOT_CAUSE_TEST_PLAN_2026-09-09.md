# Probe 7 TMUX1109 root-cause test plan

**Date:** 2026-09-09  
**Status:** Diagnostic plan recorded; do not reflow or remove the mux until the electrical evidence
below has been captured.

## Existing fault signature

Probe 7's production telemetry is consistent with the TMUX1109 address input `A1` being stuck low:

- phase 0 connected/bare counts were `19,502,047 / 19,502,915`;
- phase 1 connected/bare counts were `20,784,705 / 20,784,074`;
- requested codes 0 and 2 therefore behave alike, as do requested codes 1 and 3; and
- codes 0 and 1 differ by about 6.37%, proving that `A0` and at least part of both analog banks work.

The fault is localized to the `A1` path or the mux's response to it. On this PCB, `A1` is
E73 `P0.31` / module pin 9 -> `MUX1` -> TMUX1109PWR pin 16. TMUX pin 15, immediately adjacent,
is GND. Leading possibilities are a pin-16-to-pin-15 solder bridge, an open pin-16 or E73-pin-9
joint/trace, a damaged E73 `P0.31` output, or damage to the TMUX1109's internal A1 input/decoder.

## Preserve evidence before applying heat

Disconnect USB, battery, and solar before resistance or continuity measurements.

1. Inspect TMUX pins 16/15 and E73 module pin 9 under magnification. Look for a bridge, dry or
   cracked joint, lifted lead, lifted pad, and damaged trace.
2. Measure E73 module pin 9 to TMUX pin 16. It should be essentially zero ohms. Measure directly
   on the exposed metal at both packages so that both solder joints and the PCB trace are included.
3. Measure TMUX pin 16 to pin 15/GND. It must not be shorted. Compare both measurements with a
   healthy board such as Probe 8 if the meter result is ambiguous.
4. Use the working `A0` path as the same-board control: E73 module pin 10 to TMUX pin 1.
5. Verify the mux support pins: pin 14/VDD and pin 2/EN connect to 3V3; pins 3/VSS and 15/GND
   connect to ground.

Do not use continuity mode on a powered board. Do not inject 3.3 V into `A1` while the MCU may be
driving it low.

## Decisive powered logic test

The installed production application already requests codes 2/3 briefly for each advertised
bare-tank reading. With the board powered, use a high-impedance oscilloscope or logic analyzer on
TMUX pin 16. Connect the instrument ground only to PCB GND, never to `TANK_REF`. Trigger on a rising
edge near 1.5 V and allow at least one advertising interval. Expect a clean approximately 0 V to
3.3 V pulse. At 3.3 V supply, the TMUX1109 specification guarantees low at no more than 0.8 V and
high at no less than 1.35 V.

Probe the same event at the E73 `P0.31` castellated connection when possible:

| Observation | Interpretation |
|---|---|
| E73 side goes high but TMUX pin 16 does not | Open trace or bad TMUX pin-16 joint |
| Both stay low and pin 16 is shorted to GND | Likely bridge to adjacent pin 15 |
| Both stay low without a GND short | E73 pin-9 joint or damaged `P0.31` GPIO |
| TMUX pin 16 reaches 3.3 V but selection remains in codes 0/1 | Internal A1/decoder damage, or less likely analog-pin solder/short fault |
| A1 switches and the analog nodes obey all four codes | TMUX is functional; investigate an external electrode-to-tank short or acquisition fault |

A multimeter will probably miss the production application's brief A1 pulse. If a scope is not
available, use a temporary **signed diagnostic build derived from the production sketch** that
retains `ResetSafeBLEDfu` and holds each mux code for several seconds. Do not OTA-install the plain
`MSV3_Probe_Bringup_Test` sketch on Probe 7: it has no reset-safe DFU jump and can be a one-way
installation with this bootloader unless SWD/reset access is maintained.

## Static four-code truth-table check

Perform this out of soil and keep codes 2/3 selected only long enough to measure:

| Code | A1 A0 | Expected Node A | Expected Node B | Expected FDC region |
|---:|:---:|---|---|---|
| 0 | 00 | `TANK_HOT` | `TANK_REF` | Electrode-connected |
| 1 | 01 | `TANK_REF` | `TANK_HOT` | Electrode-connected, swapped |
| 2 | 10 | 3V3 | `VDIV` | Bare tank, normally about 22.6M |
| 3 | 11 | `VDIV` | 3V3 | Bare tank, swapped |

`VDIV` is nominally pulled toward ground, but use its measured voltage rather than assuming zero
because a separate VDIV assembly fault has existed on an earlier board. Codes 0/1 carry an active
LC waveform; use an oscilloscope/AC observation or the FDC count rather than treating `TANK_REF` as
DC ground.

If one Node bank switches and the other does not, check the affected common/source pins and joints.
If `A1` reaches pin 16 at a valid logic high, all package-lead-to-net continuity checks pass, and
both banks still behave as codes 0/1, the evidence supports damaged TMUX1109 silicon.

## Reflow/replacement decision

After recording the measurements, flux and reflow the localized A1 path first: TMUX pin 16 and the
E73 module pin-9 joint. Recovery after that is strong evidence of an assembly joint. If the valid
A1 waveform already reaches pin 16 and localized reflow does not restore codes 2/3, replace/remove
the mux rather than repeatedly heating the board.

Never install the permanent `MS_NODE_A` -> `TANK_HOT` and `MS_NODE_B` -> `TANK_REF` bypass while
the TMUX1109 remains populated. A selected internal route can oppose the fixed wiring. Remove the
mux first, then install the bypass and use firmware that fixes one phase and marks the bare
reference invalid.
