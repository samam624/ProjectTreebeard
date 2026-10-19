// The "water now" region (2026-10-11). See cloud/docs/MOISTURE_PERCENT_RANGE_AND_FERTILIZER_2026-10-11.md
// for why, and cloud/scripts/water_now_replay.py for the reference this is held equal to
// (region.test.ts).
//
// What it replaces: deriveAutoCal() learns 0 % from where the last waterings happened and 100 %
// from the pour's peak. Watering early then moves 0 % up, and one fertilized watering stretches
// the range. Here the scale comes from the pot and the watering line from a fixed habit:
//
//   1. A watering is a step up of STEP_UP of the scale between one hour's median and the three
//      before it.
//   2. The level the pot SETTLES at, the median 12-24 h after the step, is the scale: 100 %.
//   3. The region starts a fixed drop below the settled level. That drop is the user's own, taken
//      from where the first TEACH_CYCLES waterings since the last calibration reset happened,
//      then stored and never moved again (msv2_set_sensor_region_anchor_if_unset). 0 % is the top
//      of the region (TOP x drop below settled: "water now"); the bottom is BOTTOM x drop below.
//   4. Ions (a fertilizing) change the scale. On a linear reading the drop is a SHARE of
//      (settled - base) and scales with it; on MSV4's log-conductance number the drop is a fixed
//      SPAN and slides with it.
//   5. Every watering recalibrates: a settled level at or above the one in force is the new
//      scale. A settled level BELOW it may be a small watering, so only LEAK of the fall is taken.
//      (Until 2026-10-18 a higher level was first held as a shift on the old scale and judged
//      against two earlier first days. Probe 7 never had two, so its 10-11, 10-14 and 10-17
//      waterings stayed on a range from before them and it read overdue a day after watering.)
//   6. A fall of STEP_DOWN of the scale within an hour, no sooner than DRYDOWN_HOURS after a
//      watering, is a disturbed probe: no region until the next watering, and that cycle teaches
//      nothing. Deliberately almost never true: the maintainer (2026-10-18) wants calibration
//      kept between waterings above all, and the user can see a knocked or pulled probe.
//
// Everything is causal: the result at `nowMs` uses only hours at or before it.

export type RegionRawSensor = {
  sensorId: number;
  scale?: number | null;                       // msv2_sensor_history.moisture_scale of the latest rows
  regionDrop?: number | null;                  // the stored anchor, if any
  regionMode?: string | null;                  // "share" | "span": the units regionDrop was stored in
  regionBase?: number | null;                  // per-sensor base for share mode (set by hand)
  hours: [number, number][];                   // [hours since 1970-01-01 UTC, median moisture], ascending
};

export type RegionResult = {
  sensorId: number;
  ready: boolean;
  reason: string;                              // why not ready, or "ok"
  mode: "share" | "span";
  base: number | null;
  top: number | null;                          // raw reading at 0 %: "water now" at or below this
  bottom: number | null;                       // raw reading below which it is overdue
  settled: number | null;                      // raw reading at 100 %
  drop: number | null;                         // the anchor in force (share of scale, or raw span)
  taught: number;                              // waterings the anchor is built from
  anchorStored: boolean;
  anchorToStore: number | null;                // set once, when TEACH_CYCLES waterings are in
  disturbed: boolean;
  lastWateredAtMs: number | null;
  daysSinceLastWatering: number | null;
  waterings: RegionWatering[];
};

export type RegionWatering = {
  atMs: number;
  levelBefore: number;
  settled: number | null;
  verdict: string;
  firstDay: number | null;
  taughtDrop: number | null;
};

export const REGION = {
  STEP_UP: 0.15,
  // A fall of the whole scale within an hour. 0.20 until 2026-10-18, when probe 7's first noon
  // after a watering fell 22 % and lost its region; sensor 9's noon sun falls reach 29 %.
  STEP_DOWN: 1.0,
  TOLERANCE: 0.10,
  LEAK: 0.5,
  TOP: 0.925,
  BOTTOM: 1.12,
  TEACH_CYCLES: 2,
  SETTLE_HOURS: [12, 24] as const,
  FIRST_DAY_HOURS: [36, 48] as const,
  QUIET_AFTER_WATERING_HOURS: 6,
  QUIET_AFTER_INSERT_HOURS: 24,
  // No fall is a disturbance this soon after a watering: the water is still spreading through
  // the pot (maintainer). Probe 7's first noon after each watering fell 18-23 % of its scale in
  // an hour, 14-25 h after it; 36 h covers the first noon after a watering at any hour.
  DRYDOWN_HOURS: 36,
  PRE_WINDOW_HOURS: 9,
  // The reading the water-dependent part scales about, where it is the probe's own in-air value.
  // MSV3 capless (scale 3): sensor 8 read 40,511-40,567 in dry air on 2026-09-21.
  BASE_BY_SCALE: { 3: 40500 } as Record<number, number>,
  // MSV4 (scale 4) reports 10000 * log10(conductance): ions slide the range, they do not stretch it.
  SPAN_SCALES: [4],
};

// With the two multiples above, "overdue" always lands on this percent of the 0-100 scale the
// receiver draws: 100 * (TOP - BOTTOM) / TOP. The three display surfaces carry it as their
// danger threshold; DisplayCenter/scripts/verify_moisture_alert_thresholds.mjs holds them equal.
export const REGION_OVERDUE_PERCENT = Math.round(100 * (REGION.TOP - REGION.BOTTOM) / REGION.TOP);

function median(values: number[]): number {
  const ordered = [...values].sort((a, b) => a - b);
  const middle = Math.floor(ordered.length / 2);
  return ordered.length % 2 === 0 ? (ordered[middle - 1] + ordered[middle]) / 2 : ordered[middle];
}

function medianBetween(hours: [number, number][], upTo: number, fromHour: number, toHour: number, least: number): number | null {
  const values: number[] = [];
  for (let k = 0; k <= upTo; k++) {
    if (hours[k][0] >= fromHour && hours[k][0] < toHour) values.push(hours[k][1]);
  }
  return values.length >= least ? median(values) : null;
}

export function deriveRegion(sensor: RegionRawSensor, nowMs: number): RegionResult {
  const nowHour = nowMs / 3600_000;
  const hours = (sensor.hours ?? []).filter((row) => Number.isFinite(row[0]) && Number.isFinite(row[1]) && row[0] <= nowHour);
  const scaleVersion = Number(sensor.scale ?? 0);
  const storedDrop = Number.isFinite(Number(sensor.regionDrop)) && sensor.regionDrop !== null && sensor.regionDrop !== undefined
    ? Number(sensor.regionDrop) : null;
  const configuredBase = sensor.regionBase !== null && sensor.regionBase !== undefined && Number.isFinite(Number(sensor.regionBase))
    ? Number(sensor.regionBase) : (REGION.BASE_BY_SCALE[scaleVersion] ?? null);
  let mode: "share" | "span" = REGION.SPAN_SCALES.includes(scaleVersion) || configuredBase === null ? "span" : "share";
  if (storedDrop !== null && (sensor.regionMode === "share" || sensor.regionMode === "span")) {
    mode = sensor.regionMode === "share" && configuredBase !== null ? "share" : "span";
  }
  const base = mode === "share" ? configuredBase : null;

  let settled: number | null = null;           // the scale in force
  let lastWateredHour: number | null = null;
  let quietUntilHour = -Infinity;
  let waitingToSettle = false;
  let firstDayDue = false;
  let settledFromGap = false;
  let disturbed = false;
  let scaleVoid = false;                       // the last cycle was disturbed: the next settled level starts a new scale
  let scaleBeforeWatering: number | null = null;
  let teachThisWatering = false;
  const taughtDrops: number[] = [];
  const waterings: RegionWatering[] = [];

  const anchor = (): number | null => {
    if (storedDrop !== null) return storedDrop;
    if (taughtDrops.length === 0) return null;
    const used = taughtDrops.slice(0, REGION.TEACH_CYCLES);
    return used.reduce((sum, value) => sum + value, 0) / used.length;
  };
  // What a step is measured against: the scale itself where there is one, the anchor's span otherwise.
  const stepScale = (pre: number): number | null => {
    if (mode === "share") return settled !== null ? settled - base! : null;
    const drop = anchor();
    return drop !== null ? drop : (settled !== null ? Math.max(settled - pre, 0) || null : null);
  };

  for (let i = 0; i < hours.length; i++) {
    const [hour, current] = hours[i];
    const earlier: number[] = [];
    for (let k = i - 1; k >= 0 && earlier.length < 3; k--) {
      if (hour - hours[k][0] > REGION.PRE_WINDOW_HOURS) break;
      earlier.push(hours[k][1]);
    }
    const pre = earlier.length >= 2 ? median(earlier) : null;

    if (pre !== null && hour >= quietUntilHour) {
      const scale = stepScale(pre);
      const stepUp = scale !== null ? REGION.STEP_UP * scale
        : mode === "share" ? Math.max(0.5 * (pre - base!), 0.02 * Math.abs(pre)) : 0.15 * Math.abs(pre);
      if (pre <= 0 && current > 0) {
        // Out of "nothing conducting": the probe went into soil. Not a watering, and the soil
        // closes on the blade for a day afterwards.
        quietUntilHour = hour + REGION.QUIET_AFTER_INSERT_HOURS;
      } else if (stepUp > 0 && current - pre >= stepUp) {
        scaleBeforeWatering = settled;
        teachThisWatering = !disturbed;
        scaleVoid = disturbed;
        lastWateredHour = hour;
        quietUntilHour = hour + REGION.QUIET_AFTER_WATERING_HOURS;
        waitingToSettle = true;
        firstDayDue = true;
        settledFromGap = false;
        disturbed = false;
        waterings.push({ atMs: hour * 3600_000, levelBefore: pre, settled: null, verdict: "", firstDay: null, taughtDrop: null });
      } else if (scale !== null && pre - current >= REGION.STEP_DOWN * scale && !disturbed && lastWateredHour !== null &&
          hour >= lastWateredHour + REGION.DRYDOWN_HOURS) {
        disturbed = true;
        waterings[waterings.length - 1].verdict += " disturbed";
      }
    }

    if (waitingToSettle && lastWateredHour !== null && hour >= lastWateredHour + REGION.SETTLE_HOURS[1]) {
      let neu = medianBetween(hours, i, lastWateredHour + REGION.SETTLE_HOURS[0], lastWateredHour + REGION.SETTLE_HOURS[1], 4);
      if (neu === null && hour >= lastWateredHour + 30) {
        neu = medianBetween(hours, i, lastWateredHour + REGION.SETTLE_HOURS[0], hour + 1, 3);
        settledFromGap = neu !== null;
      }
      if (neu !== null) {
        waitingToSettle = false;
        const watering = waterings[waterings.length - 1];
        watering.settled = neu;
        // Where this watering happened, measured against the scale it ended. The first watering
        // on record has no scale before it; the one that follows stands in.
        const reference = scaleBeforeWatering ?? neu;
        if (teachThisWatering && storedDrop === null && taughtDrops.length < REGION.TEACH_CYCLES) {
          const drop = mode === "share"
            ? 1 - (watering.levelBefore - base!) / (reference - base!)
            : reference - watering.levelBefore;
          if (Number.isFinite(drop) && drop > 0 && (mode === "span" || drop < 1)) {
            taughtDrops.push(drop);
            watering.taughtDrop = drop;
          }
        }
        if (settled === null || scaleVoid) {
          settled = neu;
          watering.verdict = (scaleVoid ? "new scale after a disturbance" : "first scale") + watering.verdict;
        } else {
          const drop = anchor();
          const unit = mode === "share" ? settled - base! : (drop ?? Math.abs(settled));
          const change = (neu - settled) / unit;
          if (change < -REGION.TOLERANCE) {
            settled = settled - REGION.LEAK * (settled - neu);
            watering.verdict = `lower by ${Math.round(-100 * change)} % of scale: half of the fall taken` + watering.verdict;
          } else {
            settled = neu;
            watering.verdict = (change > REGION.TOLERANCE ? `higher by ${Math.round(100 * change)} % of scale: the new scale` : "same scale") +
              watering.verdict;
          }
        }
        scaleVoid = false;
      }
    }

    if (firstDayDue && !waitingToSettle && lastWateredHour !== null && hour >= lastWateredHour + REGION.FIRST_DAY_HOURS[1]) {
      const later = medianBetween(hours, i, lastWateredHour + REGION.FIRST_DAY_HOURS[0], lastWateredHour + REGION.FIRST_DAY_HOURS[1], 4);
      if (later !== null || hour >= lastWateredHour + 60) {
        firstDayDue = false;
        const watering = waterings[waterings.length - 1];
        // Recorded for the replay tools only: the first day's loss as a share of the scale.
        if (later !== null && !disturbed && !settledFromGap && mode === "share" && watering.settled !== null) {
          watering.firstDay = (watering.settled - later) / (watering.settled - base!);
        }
      }
    }
  }

  const drop = anchor();
  const result: RegionResult = {
    sensorId: sensor.sensorId,
    ready: false,
    reason: "ok",
    mode,
    base,
    top: null,
    bottom: null,
    settled: settled === null ? null : Math.round(settled),
    drop,
    taught: storedDrop !== null ? REGION.TEACH_CYCLES : Math.min(taughtDrops.length, REGION.TEACH_CYCLES),
    anchorStored: storedDrop !== null,
    anchorToStore: storedDrop === null && taughtDrops.length >= REGION.TEACH_CYCLES ? drop : null,
    disturbed,
    lastWateredAtMs: lastWateredHour === null ? null : lastWateredHour * 3600_000,
    daysSinceLastWatering: lastWateredHour === null ? null : (nowHour - lastWateredHour) / 24,
    waterings,
  };
  if (settled === null) result.reason = "no watering has settled yet";
  else if (drop === null) result.reason = "no watering to learn the watering line from yet";
  else if (disturbed) result.reason = "the probe was disturbed; waiting for the next watering";
  else {
    const top = mode === "share" ? base! + (1 - REGION.TOP * drop) * (settled - base!) : settled - REGION.TOP * drop;
    const bottom = mode === "share" ? base! + (1 - REGION.BOTTOM * drop) * (settled - base!) : settled - REGION.BOTTOM * drop;
    const full = settled;
    if (!(bottom < top && top < full) || top < 1 || full > 65535) result.reason = "the region does not fit the reading's range";
    else {
      result.ready = true;
      result.top = Math.round(top);
      result.bottom = Math.max(0, Math.round(bottom));
    }
  }
  return result;
}

// The row receiver-snapshot?autocal=1&region=1 returns for one sensor. It keeps the names the
// receiver has always read (ready, dryBound, wetBound): dryBound is the top of the region, where
// the display's 0 % and "water now" begin, and wetBound is the settled level, its 100 %. `region`
// tells the display those bounds mean that. Kept small: the receiver parses the whole response
// into a 4 kB document.
export function regionAutoCalRow(region: RegionResult, verbose = false) {
  const row = {
    sensorId: region.sensorId,
    ready: region.ready,
    dryBound: region.ready ? region.top : 0,
    wetBound: region.ready ? region.settled : 0,
    daysSinceLastWatering: region.daysSinceLastWatering,
    lastWateredAtMs: region.lastWateredAtMs,
    region: region.ready,
  };
  if (!verbose) return row;
  return {
    ...row,
    overdueBound: region.bottom,
    regionReason: region.reason,
    regionMode: region.mode,
    regionBase: region.base,
    regionDrop: region.drop,
    regionTaught: region.taught,
    regionAnchorStored: region.anchorStored,
    regionDisturbed: region.disturbed,
    waterings: region.waterings,
  };
}
