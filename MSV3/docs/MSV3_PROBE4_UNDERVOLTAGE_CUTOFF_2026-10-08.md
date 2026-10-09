# MSV3 probe 4: run down to the BQ25504 undervoltage cut-off, and back in light — 2026-10-07/08

Dated record. Current physical state lives in `MSV3/fleet.json`, not here.

**Stated by the maintainer, 2026-10-08 about 13:20 PDT:** "MSV3 shows proof of VBAT OV being
triggerend and hysteresis being needed to revive the probe after and it just came back right now
after the light hit it well enough."

The probe was not named. Sensor 4 is the only MSV3 probe whose stored history has a dropout and
a return that fit; sensor 7 reported all night without a gap. Where probe 4 sits was not stated.

**What the history shows is the undervoltage end of the BQ25504 (`VBAT_UV`), not `VBAT_OV`.**
Nothing in this event is within 0.3 V of the 3.887 V overvoltage limit.

## Measured

Sensor 4's stored cloud history, 2026-10-06 15:00 to 2026-10-08 13:25 PDT
(`cloud/scripts/fetch_supabase_history.py --receiver MSV2088A3104A7AC --sensor 4 --raw`, slopes
from `MSV4/scripts/msv4_battery_trend.py`). Battery values are the probe's own ADC reading.

| When (PDT) | Battery | Note |
|---|---:|---|
| 10-06 15:00–21:00 | 3712 → 3695 mV | −2.7 mV/h |
| 10-06 21:00 – 10-07 09:00 | 3696 → 3562 mV | −9 to −11 mV/h |
| 10-07 09:00–15:00 | 3559 → 3348 mV | −35 mV/h |
| 10-07 16:53–17:34 | 3289 → 3441 mV | **+152 mV in 41 minutes.** Cause not stated |
| 10-07 21:00–23:39 | 3315 → 3122 mV | −76 mV/h |
| 10-07 23:16–23:39 | 3141 → 3075 mV | −2.8 mV/min over the last 21 packets |
| **10-07 23:39:28** | **3075 mV** | last packet, counter 23797, 23.5 °C |
| | | silent 13 h 36 min |
| **10-08 13:15:23** | **3220 mV** | first packet stored, **counter 2**, 26.75 °C, −53 dBm |
| 10-08 13:16:33 / 13:17:43 / 13:18:52 | 3286 / 3350 / 3400 mV | counters 3, 4, 5 |
| 10-08 13:25:53 | 3584 mV | counter 11: +364 mV in 10.5 minutes |

- The wake counter had run without a restart since the v65567 install on 2026-09-18 (23797
  wakes at 70 s is 19.3 days). It restarted across the gap, so the board lost power.
- Counter 1, 70 s before the first stored packet, is not in the history.

### The charge after the return, to 14:39

| Time (PDT) | Battery (5-minute median) | Rate |
|---|---:|---|
| 13:15 → 13:25 | 3220 → 3595 mV | +37 mV/min |
| 13:25 → 13:40 | 3595 → 3724 mV | +8.6 mV/min |
| 13:40 → 13:50 | 3724 → 3744 mV | +2 mV/min |
| 13:50 → 14:30 | 3744 → 3788 mV | +1.1 mV/min (66 mV/h) |
| 14:35 | 3779 mV | |

Board temperature 26.5 → 28.75 °C over the same time, −52 to −56 dBm throughout.

The curve bends at about 3.72–3.74 V, 150 mV under this board's 3.887 V limit. The BQ25504 does
not reduce its current as the cell nears `VBAT_OV`; it runs until the limit and then switches
off. The bend is the cell's own curve: little of its charge sits below about 3.7 V, so the same
current moves the voltage quickly there and slowly on the plateau above it. The maintainer saw
this on the History chart at about 14:40 and asked whether it was a taper towards the limit.

**The rest of the day (added 22:25).** Hourly medians: 3778 mV at 14:00, 3800 at 15:00, 3797 at
16:00, a peak of 3806 mV at 17:00 (highest single reading 3816), then down 3 to 5 mV an hour to
3787 mV at 22:00. The light ran out about 80 mV under the 3.887 V limit, so the reworked limit
on this board is still unseen.

## Thresholds on this board

| Threshold | Value | Basis |
|---|---:|---|
| `VBAT_UV`, switch opens (VSTOR falling) | 3.04 V nominal; 3.048 V with this board's VBIAS of 1.2532 V; 2.94–3.09 V over VBIAS 1.21–1.27 V | R12 5.90 MΩ / R15 4.12 MΩ, `MSV3/hardware/MSV3_Probe_PCB/reference/COMPONENT_INDEX.md` |
| Switch recloses (VSTOR rising) | `VBAT_UV` + 40 / 80 / 125 mV: 3.09 / 3.13 / 3.17 V on this board | datasheet `VBAT_UV_HYST`, section 8.3.2 |
| `VBAT_OK` | not used: pin floating, `OK_PROG` and `OK_HYST` grounded | same reference |
| `VBAT_OV` after the 2026-09-14 rework | 3.887 V nominal, restart 18 / 35 / 89 mV below | `MSV3_VBAT_OV_STORAGE_REWORK_2026-09-14.md` |

Modelled from resistor values and the datasheet; none of them measured on this board.

## Reading

- **The dropout is the undervoltage switch (inferred).** The last reading, 3075 mV, is 27 mV
  above this board's nominal 3.048 V and inside the 2.94–3.09 V band. The reading is taken once
  per wake while the cell was falling 3.3 mV per wake, and a nearly empty cell sags further
  under each wake's radio burst. The production sketch has no low-battery shutdown of its own,
  and `VBAT_OK` is not fitted, so `VBAT_UV` is the first and only thing that acts on a falling
  cell.
- **The return fits the datasheet's hysteresis (inferred).** The datasheet closes the switch
  once VSTOR is above `VBAT_UV` plus 40–125 mV and holds it closed until VSTOR falls below
  `VBAT_UV`. The first stored reading is 3220 mV with the voltage climbing 50–66 mV per wake,
  which puts the wake before it near 3.15–3.16 V (extrapolated, not a reading), inside the
  3.09–3.17 V reclose band. The 145 mV between the last packet and the first one back is
  therefore an upper bound on the hysteresis seen here.
- **Why it took strong light (inferred).** With the switch open the cell is disconnected and
  the board has only VSTOR's capacitors behind it, so the harvester alone has to lift VSTOR
  past the reclose level. MSV4 board 1's cell began taking charge at 09:26 that morning and had
  full sun on its panel from 11:48–12:07; probe 4 came back at 13:15. Whether the two sit in
  the same window was not stated.
- **Probe 4's harvester works (measured).** +152 mV in 41 minutes on 10-07 and +364 mV in the
  first 10.5 minutes back on 10-08, both from a cell at the steep bottom of its curve. The
  2026-09-19 note in `fleet.json` left open that a dead harvester would give the same telemetry
  as one held off by `VBAT_OV`; a dead harvester does not do this. What is still unseen on this
  board is the reworked 3.887 V limit itself, which needs the cell to get there.
- **MSV4 is built differently here.** It uses `VBAT_OK` to switch its load off at 3.37 V and
  back on at 3.73 V (nominal), so its cell is never taken to `VBAT_UV` by the board. The
  datasheet calls `VBAT_UV` a fail-safe and says the load should be removed on `VBAT_OK`; MSV3
  has only the fail-safe.

## The other nodes over the same night and morning

| Node | Battery | Temperature | Note |
|---|---|---|---|
| Sensor 7 (MSV3 probe 7, as-built 4.26 V limit) | 4220.5 → 4188.7 mV from 19:00 to 07:00, **−2.65 ± 0.05 mV/h**; same slope until 12:09; then **+28.9 ± 1.6 mV/h**, 4177 → 4213 mV by 13:26 | 19.25–23.5 °C overnight; 24.75 → 30 °C from 12:10 | No gap, no restart. Still 50 mV under its limit, so nothing to see of the limit yet |
| Sensor 10 (MSV4 board 1) | see `MSV4/docs/MSV4_OUTDOOR_SOLAR_CHARGE_TEST_2026-10-06.md` | 21–24 °C overnight, 28.75 °C at 13:27 | In the dark the night before, without the burn: −0.17 mV/h |
| Sensor 1 (MSV2, outdoors) | 4046–4052 mV from 02:40 to 08:50; steps to 4110–4130 mV at 09:10–09:30 and back down at 01:40–02:40, on both days; single readings up to 4.40 V while high | none | Not explained |
| Sensors 2, 3, 5, 6, 8, 9 | no rows | | Sensor 2 has 61 rows inside 78 seconds at 14:54–14:56 on 2026-10-06 and nothing since; the others have nothing in 48 h |

Probe 7 lost 2.65 mV/h in the dark at 4.2 V; MSV4 board 1 lost 0.17 mV/h at 3.82 V. Different
cells at different points on their curves, so that is not a ratio of currents.

One event from the day before, not explained: probe 7's stored rows stop at 12:25:48 on
2026-10-07 (4002 mV, 48.75 °C, rising in full sun) and resume at 13:29:53 with the wake counter
restarted at 1, at 4189 mV and 57 °C.
