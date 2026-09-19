# Probe 7 zener-clamp charging reassessment (2026-09-16)

## Bottom line

Probe 7 is online and healthy, but there is not yet a daylight telemetry window long enough to say
whether its battery is gaining charge. The temporary `1N4728A` is a poor clamp for this source. Its
nominal 3.3 V is specified at **76 mA**, while this panel can supply at most about 30 mA in direct sun
and much less at the deployment window. The measured steady `SP+ < 2.2 V` is therefore not surprising.

More importantly, 2.2 V is almost exactly the BQ25504's expected **post-sample operating point**, not
necessarily the zener voltage:

```text
as-built MPPT ratio = 15.6 M / (4.42 M + 15.6 M) = 0.779
inferred sampled clamp voltage = 2.20 V / 0.779 = 2.82 V
```

Unless the DMM happened to catch the 256 ms open-circuit sample that occurs once every 16 s, it saw
the normal regulated input. A 1N4728A clamping near 2.8 V below its 76 mA test current is completely
plausible. The zener probably did not short the panel into the ground. It made the MPPT controller
operate the panel near 2.2 V, which throws away roughly half of the old panel power.

For MSV3, the durable fix is a lower-voltage panel with the same area/power. If the 6 V panel must be
kept, use a characterized low-current precision shunt near 2.8-2.85 V and change the MPPT ratio only
as a matched pair with that clamp. Do not fit another ordinary "3.3 V" zener by label alone.

## Live evidence from Probe 7

At approximately 18:54 PDT on 2026-09-16, the repository's guarded fleet scanner observed:

- registered address `F7:B1:E6:50:EC:B6`
- 32 packets, mean RSSI -73 dBm
- two distinct wakes, advancing by one to wake 104
- 4011 mV battery telemetry
- valid capless production reading, flags `0x56`
- die temperature 22.75 C and open OTA gate

The linked Supabase history contains 56 rows from 17:51 through 18:55 PDT: 4022 mV first, 4018 mV
last, 4000-4022 mV range, 4013.2 mV mean. That is only one noisy hour around sunset. It proves cloud
ingest and continued operation, but it is not a charging test. `raw_charging=false` is also not
evidence against solar charging: that bit comes from the separate USB BQ25170 `STAT` path.

The current local export is `data/sensor_logs/probe7_zener_trial_raw.csv` (intentionally ignored by
Git).

## What the 1N4728A data sheet actually guarantees

The onsemi 1N4728A table specifies:

| parameter | data-sheet value | relevance here |
|---|---:|---|
| zener voltage | 3.135-3.465 V | only at 76 mA |
| test current | 76 mA | panel cannot reach it |
| dynamic impedance at test current | 10 ohm max | not applicable at window current |
| knee impedance | 400 ohm max at 1 mA | very soft knee |
| reverse leakage | 100 uA max at 1 V | already material beside a sub-mA window source |

Source: [onsemi 1N4728A-1N4758A data sheet](https://www.onsemi.com/download/data-sheet/pdf/1n4736at-d.pdf).

The part number therefore does **not** mean "holds 3.3 V" at 0.1, 1, 10, or 30 mA. It is a 1 W
through-hole zener characterized for a much stiffer source. Its low-current I-V curve is the wrong
shape for an energy harvester.

The immediate measurement should be an I-V sweep of the actual part, not another voltage spot check:

| zener current | record voltage |
|---:|---:|
| 50 uA | |
| 100 uA | |
| 300 uA | |
| 1 mA | |
| 3 mA | |
| 10 mA | |
| 30 mA | |

Use a current-limited bench supply. If using a resistor from a 6 V supply, 1 kohm only reaches a few
mA; it cannot perform the 30 mA point. Use approximately 100 ohm, rated at least 0.25 W, for the high
current point and set the supply current limit first.

## Why charging now looks almost flat

Probe 4's measured baseline estimates 8-9 mAh/day gross harvest from the old panel and 3.03 mAh/day
probe consumption. The unclamped panel operated near 4.68 V. If current is approximately flat below
the panel MPP, operating it at 2.2 V retains only:

```text
2.20 / 4.68 = 47% of the old input power
gross harvest approximately 3.8-4.2 mAh/day
net after the 3.03 mAh/day load approximately 0.7-1.2 mAh/day
```

This is an estimate, not a measurement; the still-missing deployment-window `I_SC` dominates its
uncertainty. It does explain the observed symptom: the temporary clamp can turn a healthy 5-6
mAh/day net gain into a nearly flat battery without being a literal short.

Changing only the MPPT ratio can recover roughly 10-17%, not the missing factor of two. A lower-
voltage panel preserves the silicon area and power by trading excess series voltage for current; it
is the only simple fix that restores the lost panel power.

## Correction to the earlier body-diode conclusion

The 6 V panel remains a definite design violation:

- BQ25504 recommended `VIN_DC` maximum: 3.0 V
- absolute maximum on `VIN_DC`: 5.5 V
- measured panel `V_OC`: about 6 V

However, the earlier claim that `VBAT_OV` is structurally unable to stop body-diode charging omitted
an explicit BQ25504 protection behavior. TI states that when `VIN_DC` is above `VSTOR` and `VSTOR`
is above `VBAT_OV`, the IC pulls `VIN_DC` to ground through a small resistance to stop further
charging; TI requires the source impedance to exceed 20 ohm in this case. A PV panel is a current-
limited source, not a low-impedance bench supply.

Source: [TI BQ25504 data sheet, sections 8.3.3 and 8.4.2](https://www.ti.com/lit/ds/symlink/bq25504.pdf).

Therefore:

- The panel still must be replaced or clamped because 6 V exceeds the IC absolute maximum.
- A healthy BQ25504 is designed to suppress the otherwise possible high-side path at overvoltage.
- Probe 4 stopping near its programmed 4.26 V threshold is consistent with that protection working.
- **Probe 6 is direct evidence that the protection was not operating on that board.** The 2026-09-14
  rework record says the coated Probe 6 received the 3.887 V nominal divider. On 2026-09-16 the
  maintainer measured 5.01 V at loaded `SP+` outdoors while the battery was still about 4.30 V.
  Therefore `VIN_DC > VSTOR > VBAT_OV` was satisfied, yet `VIN_DC` was not held low. The only benign
  measurement loophole is a single DMM update landing in the 256 ms open-circuit sample that occurs
  every 16 s. A level that persists for seconds, or is the dominant level on a 20 s scope capture,
  proves the internal pull-down is ineffective. Plausible causes are damage from repeated absolute-
  maximum excursions or a fault in the OV-divider/comparator path. The 30 mA panel is not a low-
  impedance source: even 20 ohms would develop only 0.60 V at its measured full-sun short-circuit
  current, nowhere near 5.01 V.
- The old expression `VBAT = 0.78 x V_OC - 0.3 V` is a useful fit to some observations, but it is
  not a valid universal ceiling while ignoring the documented input pull-down.

The zener remains useful as **input absolute-maximum protection**, even if `VBAT_OV` is healthy.

## Corrected clamp behavior and thermal case

While the BQ25504 is charging normally, a shunt clamp sets the sampled open-circuit voltage and is
mostly off while the boost converter regulates below it. With a healthy IC at `VBAT_OV`, the BQ's
own input pull-down should also keep the external shunt off. Probe 6 shows that this protection may
not be available on the actual fleet hardware, so an external clamp must be sized as the primary
continuous protection, not merely as an absolute-maximum backstop.

Do not nevertheless size the shunt from the 1.6% MPPT-sample duty cycle alone. In the fault case the
external clamp exists to cover -- a damaged or ineffective internal pull-down -- it can conduct for
the entire illuminated interval. The safe thermal design point is continuous:

```text
2.85 V x 30 mA = 86 mW continuous worst-case panel dissipation
```

That is still easy for SOT-23, SOD-123, or the present 1 W axial part, but the distinction matters.

## Recommended MSV3 choices

### 1. Best: replace the panel

Use a panel whose **maximum** open-circuit voltage, including cold-temperature rise, is at most
2.7 V. Preserve area/power by using fewer cells in series and more current. This lets the BQ25504
track the real panel MPP and removes the zener's operating-point penalty.

The current small-batch plan -- two 1.5 V / 60 mA panels in parallel -- is electrically sound if the
actual panels pass `V_OC`/`I_SC` tests. The BQ25504 operates after cold start down to 130 mV and
accepts at most 100 mA average input, so the parallel array is a natural match. See
`MSV4/docs/MSV4_SOLAR_PANEL_SELECTION_2026-09-16.md`.

### 2. If keeping the 6 V panel: use a precision shunt, not another power zener

The strongest candidate found is **TI ATL431B** in SOT-23:

- 35 uA maximum minimum cathode current for regulation
- 0.2 uA maximum specified off-state current
- 100 mA maximum cathode current
- 0.5% B-grade reference option
- 0.3 ohm maximum dynamic impedance over the characterized range

Source: [TI ATL431 data sheet](https://www.ti.com/lit/ds/symlink/atl431.pdf).

Set approximately 2.85 V with `Rtop = 140 kohm` (cathode to REF) and `Rbottom = 1.00 Mohm` (REF to
GND). The typical equation gives about 2.85 V before the small reference-current term. Even with
reference, resistor, and temperature error, this has useful margin below 3.0 V. Use the B grade and
verify the assembled threshold.

This is a candidate, not a drop-in release:

- `SP+` already has approximately 14.7 uF. ATL431 has load-capacitance stability boundaries, so
  scope the cathode for oscillation over 50 uA-30 mA and cold/room/warm conditions.
- A small 1-4.7 ohm resistor between `SP+` and the shunt can isolate the large input capacitance;
  include its current-dependent voltage rise in the 3.0 V ceiling.
- Verify startup and the BQ's 256 ms sample on a scope over at least a 20 s acquisition.
- The divider itself draws about 2.5 uA. Below regulation, characterize total shunt current rather
  than assuming the data-sheet off-state number applies to the feedback configuration.

`TLVH431B` is workable but inferior here: its minimum cathode current is about 80-100 uA. `TLA431`
is guaranteed stable with every capacitive load but needs 200 uA. Both taxes are material when the
deployment-window source may itself be only a few hundred microamps.

### 3. If one diode is mandatory

A small-signal zener characterized at 5 mA is preferable to the 1N4728A characterized at 76 mA, but
it is still a soft-knee compromise. For example, Nexperia's BZX84-A3V0 is 2.97-3.03 V at 5 mA,
with 600 ohm maximum differential resistance at 1 mA and 95 ohm at 5 mA. Those impedance numbers
show why even this part must be swept at the real currents.

Source: [Nexperia BZX84 series data sheet](https://assets.nexperia.com/documents/data-sheet/BZX84_SER.pdf).

Do not select `MMSZ5226B` merely because it is 3.3 V: it is specified at 20 mA and has 1600 ohm knee
impedance at 0.25 mA, another poor fit for window light.

## MPPT divider: safe matched rework

The current network is `R9 = 4.42 M`, `R10 = 10 M`, `R13 = 5.6 M`, giving 77.9% and the required
20.02 M total. A higher ratio recovers some input voltage after a clamp, but it must be designed from
the **measured clamp voltage**, not from the printed zener value. The BQ25504 input regulation itself
has +/-10% tolerance.

For a verified 2.80-2.85 V precision clamp, a 90% network is reasonable:

```text
R9 top = 2.00 M
R10 + R13 bottom = 10.0 M + 8.06 M = 18.06 M
ratio = 18.06 / 20.06 = 90.0%
nominal operating SP+ = 2.52-2.57 V
+10% regulation edge = 2.77-2.82 V
```

This remains below the 3.0 V recommended input maximum and below the approximately 3.04 V `VBAT_UV`
level at which the main charger connects the battery.

**Do not apply 90% to an uncharacterized 3.3 V clamp.** Nominal input would be 2.97 V and the +10%
regulation edge would be 3.27 V. That erases both margins and can put a boost-only converter back in
the input-above-output regime near a low battery.

A lower-risk one-resistor experiment is `R9 = 2.43 M`, leaving `R10/R13` unchanged. Total resistance
is 18.03 M (just inside TI's 18-22 M recommendation), ratio is 86.5%, and a 2.82 V sampled clamp gives
about 2.44 V nominal -- roughly 11% more input power than 2.20 V. This is still a modest recovery,
not a substitute for the panel fix.

## Decision test before changing Probe 7

1. Leave the current clamp fitted; do not expose the unclamped 6 V panel to sun.
2. Run the actual 1N4728A I-V sweep above.
3. With the board in direct sun, scope `SP+` for at least 20 s. Record steady regulation, the 256 ms
   sample peak, sample period, and any oscillation.
4. At the deployment window, disconnect the panel briefly and measure `I_SC` at morning, solar noon,
   and late afternoon. Do not short it while it remains connected to the board.
5. Record simultaneous `SP+`, `VBAT`, and telemetry `raw_battery_mv`. This separates the input loop,
   battery threshold, and ADC offset.
6. Collect at least three complete window days. Compare daylight and 21:00-05:00 battery slopes;
   do not infer charge from the BQ25170 `raw_charging` flag.
7. If the existing panel is retained, bench the ATL431 candidate and the matched MPPT network on an
   accessible board before touching the deployed probe.

Until those measurements exist, the defensible conclusion is: **Probe 7 is alive; the temporary
1N4728A likely halved the harvest and erased most recharge margin, but tonight's data cannot yet
prove a negative daily energy balance.**
