# The water-now line ratcheted up with each watering (2026-10-21)

The maintainer, 2026-10-21 ~23:35 PDT: the probes have them watering "earlier and earlier every
single week until the plants are getting overwatered"; a probe's card "wants me to water it but
just 14 days ago it still would have needed a couple days to get back down to dry level"; they
water "when the display tells me to". The History chart they sent is sensor 7 (probe 7): its
latest raw 44,688 and its four steps match sensor 7's stored record and no other.

Changed in `supabase/functions/receiver-snapshot/region.ts` (rule 7), migration 043,
`index.ts` and `scripts/water_now_replay.py`. Deployed 2026-10-22 ~00:05 PDT at the maintainer's
word: migration 043 applied, receiver-snapshot v34. No calibration reset was made for probe 7.

## Measured: probe 7, replayed with the rule as deployed (receiver-snapshot v33)

Replayed from 2026-10-11 00:00 PDT, as after a calibration reset before its first watering in
this pot. Hourly medians from `msv2_read_sensor_hourly_moisture`, pulled 10-21 23:39 PDT.

| Watered (PDT) | Reading before | Settled (12-24 h) | Days since the last | Line in force before it | Line after it settled |
|---|---:|---:|---:|---:|---:|
| 10-11 15:00 | 42,530 | 43,769 | | (none) | 42,623 |
| 10-14 23:00 | 42,801 | 43,788 | 3.33 | 42,623 | 42,761 |
| 10-17 13:00 | 43,026 | 44,822 | 2.58 | 42,761 | 43,472 |
| 10-19 15:00 | 43,331 | 46,902 | 2.08 | 43,472 (water now from 12:00) | 44,903 |
| water now again from 10-21 18:00 | 44,895 | | 2.13 | 44,903 | |

The 10-14 and 10-17 waterings came while the reading was still above the line in force: those
were not "water now" prompts (the maintainer goes by the percent too). The 10-19 one and tonight's
prompt were. What was live on 10-14 and 10-17 (the rule before 10-18, with a range from the
Begonia pot) was a different line and is not reconstructed here.

Live, without the reset, the deployed rule gives 44,808 / overdue 44,367 / 100 % 46,902 at 10-21
23:00. The reading is 44,694, so the card shows -5 %, "WATER NOW". The pot reads 1,400 counts
wetter than at the previous watering and 2,160 wetter than at the first.

**Mechanism.** The line is a fixed share of each watering's settled level (rule 4), and since
2026-10-18 a higher settled level is the new scale at once (500a6b9). A watering before the pot
has dried back (here each one was, 42,530 -> 42,801 -> 43,026 -> 43,331) leaves it settling
wetter. The line follows the settled level up, so the display asks for the next watering sooner
and wetter, and that watering leaves it wetter again. Whatever starts it, the loop then keeps
it going.

**Why the 10-11 analysis missed it.** Its closed-loop simulation
(`scripts/moisture_percent_closedloop.py:177`) sets a full watering to `w = 1.0` whatever the
water already there: the pot always returns to the same level, as one watered to runoff does.
In that world a share of the settled level is stable, and the doc found it so. This pot does not
return to the same level: its settled level tracks what was left in it.

## The rule now (rule 7)

- While the drop is taught (the first `TEACH_CYCLES` = 2 waterings since a reset), nothing
  changes.
- At the watering that completes it, the region is fixed in raw readings: water now, overdue,
  100 %. It is stored (migration 043, `msv2_set_sensor_region_line_if_lower`) with the hour of
  that watering.
- After that, a higher settled level moves nothing. A lower one still lowers the region as
  before: whole within 10 % of the scale, half the fall past it. The lowered region is stored
  with its watering's hour, and a stored region is never re-applied to waterings at or before it.
- A calibration reset clears it (the 041 trigger, extended). That is the only way up, and the
  way a probe moved to another pot starts again.

**What it costs.** A fertilized watering reads wetter for the same water and no longer raises the
line, so "water now" comes later until the ions wash out. Sensor 3's one fertilizing (09-22)
shows the size: the line stays at 864 where it was rescaled to 976.

## Replays with the rule

| | Rule as deployed | Rule 7 |
|---|---|---|
| Sensor 3 (Alocasia GP fixture), waterings 2, 4, 5 (on time) | water now 18, 50, 25 h | identical |
| Sensor 3, watering 6 (fertilized, "a little early") | not yet | not yet |
| Sensor 3, watering 7 ("a little early") | water now for 24 h, line 976 | **not yet**, line 864 |
| Probe 7, reset 10-11 00:00 PDT | 44,903 / 44,482 / 46,902: -10 % | **42,761 / 42,545 / 43,788**: reading 44,694 is above 100 % |
| Probe 7, no reset (region from the Begonia pot and the 10-07 sun step) | 44,808 / 44,367 / 46,902 | 42,389 / 42,196 / 43,307 |
| Sensor 10 (MSV4, Alocasia) | 15,127 / 14,003 / 20,457: 72 % | identical, and no longer raised by a wetter watering |
| Sensors 4, 5, 11 | | identical |
| Sensors 3, 9 (out of soil since 09-30) | | lower (stored drop applied to an older settled level) |

Probe 7's 42,761 sits just above where it was watered on 10-11 (42,530) after four days at
42,500-42,800 (10-07 to 10-11). That is the level the maintainer calls "dry level".

The Python reference (`water_now_replay.py`, sensor 3, top 0.38) gives the same verdicts:
waterings 1-5 water now for 21, 21, 26, 34 and 15 h; 6 and 7 not yet (shares 0.40 and 0.62).

## The Alocasia pot (sensor 10) is a different case

The scale says the display did not move its waterings earlier. Watered 10-10 at 4674 g and
10-19 at 4605 g (pot and dish), the second 69 g drier than the first, 8.2 days apart. Sensor 10's
line moved 14,640 -> 15,127 at the second watering. That is the averaged drop, with no ratchet
in it, and rule 7 holds it there.

10-21 ~23:38 PDT: 4812 g, 207 g above the 10-19 pre-watering weight. It has lost 156 g since
the dish was emptied at 10-19 00:45 (2.2 g/h), and 28 g in the last 23.4 h (1.2 g/h). At those
rates it is back at 4605 g in 3.9 to 7.2 days.

## 2026-10-22: probe 7 set back; every calibration on the fixed region

The maintainer: "set calibration back to earlier on the curve before it ratcheted up, make this
logic fix for all probe calibrations but do not reset the other ones yet".

- **Probe 7: calibration reset backdated to 2026-10-11 00:00 PDT** (07:00 UTC), at their word.
  Done 00:11 PDT by updating `reset_at` on its `msv2_sensor_calibration_reset` row; the 041/043
  trigger cleared the stored drop. The region relearns from the 10-11 and 10-14 waterings and
  fixes at the 10-14 one: water now 42,761, overdue below 42,545, 100 % at 43,788 (drop 0.3376).
  That is the region in force before the 10-17 watering raised it. Whether the probe had been in
  this Begonia continuously since before 10-11 was not stated. Undo: set `reset_at` back to
  2026-09-17 00:51:24.42+00, `region_drop` 0.35352309743152843, `region_mode` 'share',
  `region_set_at` 2026-10-12 02:23:23.616246+00, and clear the region_top/bottom/full/line_hour
  columns.
- **No other sensor was reset.** Rule 7 already covers every region-calibrated sensor; sensor 11
  is still on its first watering and is fixed by its second.
- **The older range is retired** (receiver-snapshot v35): every `autocal=1` request now gets the
  region, with or without `region=1`. `autocal.ts` learned 0 % from where waterings happened and
  moved up with each early one. No receiver build still asked for it, so nothing that runs
  changes; it can no longer be served.
- **Nothing else learns a range.** The receiver, its local page and the dashboard all show the
  bounds the receiver pulls from the cloud every 10 minutes (`runCloudAutoCalPull`). The
  receiver's own smoothing (`smoothMoistureForDisplay`) is symmetric and display-only. Cards
  without a ready region use the range set by hand on the card.

Tests: `node --test supabase/functions/receiver-snapshot/region.test.ts`, 15 pass, including the
probe 7 record from 10-11 as a fixture (`fixtures/region-probe7-pot2-hourly.json`).

## 2026-11-10: the fixed line no longer moves down either

Maintainer, 2026-11-10: "changing the water now level based on the watering step is not a good
idea, if the probe is working properly then it should come back to the same level every time
watering is needed".

What prompted it (sensor 10, Alocasia pot, measured): the 10-30 watering was a small one, +192 g
on the scale against 461 and 463 g for the 2-cup waterings of 10-10 and 10-19. It settled at
17,518 against the 20,137 of 10-19, and rule 7 still lowered a fixed line by half of a fall, so the
line went from 15,127 to 13,657 (stored with line hour 10-30 23:00 UTC). On 11-10 ~00:00 PST the pot
weighed 4,424 g, 96 g under the driest earlier watering (4,520 g before 10-30), while the card read
3 %.

Change: `region.ts` rule 7 now keeps a fixed region whatever a watering settles at, up or down;
`lineToStore` is offered only when nothing is stored yet; `water_now_replay.py` follows it.
Tests 15/15. The sensor 3 fixture's waterings after its 08-28 knock now read overdue under a line
that does not move, so the on-time checks stop at the knock; a calibration reset is the fix for a
knocked probe.

Stored lines lowered after they were fixed, with what the new rule gives (computed 11-10 from the
live hourly medians, stored drop kept):

| sensor | stored (top / bottom / full) | as fixed (top / bottom / full) | fixed at |
|---|---|---|---|
| 10 | 13,657 / 12,534 / 18,988 | 15,127 / 14,003 / 20,457 | 10-19 08:00 UTC watering |
| 11 | 13,169 / 11,162 / 22,687 | 13,789 / 11,782 / 23,307 | 10-27 20:00 UTC watering |

Sensors 3 and 4 also hold lines set after they were fixed; both have been out of soil since 09-30
and were left as they are. Sensor 10 restored to the fixed line reads about -23 % (overdue) at
13,875; it crossed 15,127 around 11-04/05, at roughly 4,560 g on the scale.

**Done 2026-11-10 ~00:13 PST:** receiver-snapshot deployed (with the latest-plant merge fix,
2bcb8c8). Sensor 10's stored line written back to 15,127 / 14,003 / 20,457, line hour 497888
(10-19 08:00 UTC watering) by a direct UPDATE of its `msv2_sensor_calibration_reset` row
(`reset_at` untouched). Undo: 13,657 / 12,534 / 18,988, line hour 498167. Live check after: sensor
10 reads -23 % (overdue). Sensor 11 NOT changed (still 13,169 / 11,162 / 22,687): awaiting the
maintainer's word.
