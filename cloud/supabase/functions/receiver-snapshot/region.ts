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
//   5. While the drop is being taught, every watering recalibrates: a settled level at or above
//      the one in force is the new scale; a settled level BELOW it may be a small watering, so
//      only LEAK of the fall is taken. (Until 2026-10-18 a higher level was first held as a shift
//      on the old scale and judged against two earlier first days. Probe 7 never had two, so its
//      10-11, 10-14 and 10-17 waterings stayed on a range from before them -- a range from the
//      Begonia pot it had left. A calibration reset at the move was the fix that case needed.)
//   6. A fall of STEP_DOWN of the scale within an hour, no sooner than DRYDOWN_HOURS after a
//      watering, is a disturbed probe: no region until the next watering, and that cycle teaches
//      nothing. Deliberately almost never true: the maintainer (2026-10-18) wants calibration
//      kept between waterings above all, and the user can see a knocked or pulled probe.
//   7. Once TEACH_CYCLES waterings have settled, the region is FIXED IN RAW READINGS and NEVER
//      RAISED: a higher settled level moves nothing. A lower one still lowers it by LEAK of the
//      fall (a small watering, ions leaving, the 08-28 knock on sensor 3, whose on-time waterings
//      a region fixed both ways would call overdue). Stored when fixed and when lowered
//      (msv2_set_sensor_region_line_if_lower); only a calibration reset clears it, and a reset is
//      how a probe moved to another pot starts again. Measured 2026-10-21 on probe 7: with the line
//      riding on each settled level, the user waters at the line, the pot has not dried, it
//      settles wetter (43,769 -> 43,788 -> 44,822 -> 46,902), the line follows it up (42,700 ->
//      44,808 in eight days) and the next watering comes sooner (3.3, 2.6, 2.1 days). The 10-11
//      simulation that chose "a share of each settled level" assumed a full watering always
//      settles the pot at the same level, as one watered to runoff does; this pot does not. A
//      line that cannot follow its own waterings cannot ratchet. The cost: a fertilized watering
//      reads wetter for the same water, so "water now" comes later until the ions wash out.
//
// Everything is causal: the result at `nowMs` uses only hours at or before it.

export type RegionRawSensor = {
  sensorId: number;
  scale?: number | null;                       // msv2_sensor_history.moisture_scale of the latest rows
  regionDrop?: number | null;                  // the stored anchor, if any
  regionMode?: string | null;                  // "share" | "span": the units regionDrop was stored in
  regionBase?: number | null;                  // per-sensor base for share mode (set by hand)
  regionTop?: number | null;                   // the stored fixed region (rule 7), raw readings
  regionBottom?: number | null;
  regionFull?: number | null;
  regionLineHour?: number | null;              // the watering (hours since 1970 UTC) that last set or lowered it
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
  fixed: boolean;                              // rule 7: the region no longer moves with waterings
  lineStored: boolean;
  lineToStore: { top: number; bottom: number; full: number; hour: number } | null;
  scaleSettled: number | null;                 // the latest scale, which a fixed region no longer follows
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
  const finite = (value: unknown): value is number => value !== null && value !== undefined && Number.isFinite(Number(value));
  const storedLine = finite(sensor.regionTop) && finite(sensor.regionBottom) && finite(sensor.regionFull) &&
      Number(sensor.regionBottom) < Number(sensor.regionTop) && Number(sensor.regionTop) < Number(sensor.regionFull)
    ? { top: Number(sensor.regionTop), bottom: Number(sensor.regionBottom), full: Number(sensor.regionFull),
        hour: finite(sensor.regionLineHour) ? Number(sensor.regionLineHour) : -Infinity } : null;

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
  let cyclesSettled = 0;                       // settled waterings not after a disturbance
  let fixedLine = storedLine;                  // rule 7
  let latestSettled: number | null = null;
  const taughtDrops: number[] = [];
  const waterings: RegionWatering[] = [];

  // The region a scale and a drop give: water now (top), overdue (bottom) and 100 % (full).
  const windowFor = (scale: number, drop: number) => {
    const top = mode === "share" ? base! + (1 - REGION.TOP * drop) * (scale - base!) : scale - REGION.TOP * drop;
    const bottom = mode === "share" ? base! + (1 - REGION.BOTTOM * drop) * (scale - base!) : scale - REGION.BOTTOM * drop;
    return { top, bottom, full: scale, hour: lastWateredHour ?? -Infinity };
  };
  const fits = (w: { top: number; bottom: number; full: number }) => w.bottom < w.top && w.top < w.full && w.top >= 1 && w.full <= 65535;

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
        latestSettled = neu;
        const afterDisturbance = scaleVoid;
        const drop = anchor();
        if (storedLine !== null && lastWateredHour !== null && lastWateredHour <= storedLine.hour) {
          // Already in the stored region: applying it again would lower it on every pull.
          watering.verdict += "; in the stored region";
          settled = fixedLine!.full;
        } else if (fixedLine !== null && cyclesSettled >= REGION.TEACH_CYCLES && drop !== null) {
          // Rule 7: the region is fixed. A lower settled level still lowers it by half of the
          // fall, as before (a small watering, ions leaving, a knocked probe reading low); a
          // higher one never raises it.
          const full = fixedLine.full;
          const change = (neu - full) / (mode === "share" ? full - base! : drop);
          // As before the region was fixed: within TOLERANCE the new level is taken whole, past it half the fall.
          const lowered = windowFor(change < -REGION.TOLERANCE ? full - REGION.LEAK * (full - neu) : neu, drop);
          if (change < 0 && fits(lowered) && lowered.top < fixedLine.top) {
            fixedLine = lowered;
            watering.verdict = (change < -REGION.TOLERANCE ? `lower by ${Math.round(-100 * change)} % of scale: region lowered by half of the fall`
              : "same scale, a little lower: region lowered to it") + watering.verdict;
          } else {
            watering.verdict = (change > REGION.TOLERANCE ? `higher by ${Math.round(100 * change)} % of scale: region fixed, not raised`
              : change < -REGION.TOLERANCE ? `lower by ${Math.round(-100 * change)} % of scale: region kept` : "same scale") + watering.verdict;
          }
          settled = fixedLine.full;
        } else if (settled === null || scaleVoid) {
          settled = neu;
          watering.verdict = (scaleVoid ? "new scale after a disturbance" : "first scale") + watering.verdict;
        } else {
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
        if (!afterDisturbance) cyclesSettled++;
        const anchorComplete = storedDrop !== null || taughtDrops.length >= REGION.TEACH_CYCLES;
        if (cyclesSettled === REGION.TEACH_CYCLES && anchorComplete && anchor() !== null) {
          // The watering that completes the teaching fixes the region (or meets the stored one).
          const candidate = windowFor(settled, anchor()!);
          if (fixedLine === null && fits(candidate)) fixedLine = candidate;
          if (fixedLine !== null) {
            settled = fixedLine.full;
            if (storedLine === null) watering.verdict += "; region fixed here";
          }
        }
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
    settled: fixedLine !== null ? Math.round(fixedLine.full) : settled === null ? null : Math.round(settled),
    drop,
    taught: storedDrop !== null ? REGION.TEACH_CYCLES : Math.min(taughtDrops.length, REGION.TEACH_CYCLES),
    anchorStored: storedDrop !== null,
    anchorToStore: storedDrop === null && taughtDrops.length >= REGION.TEACH_CYCLES ? drop : null,
    fixed: fixedLine !== null,
    lineStored: storedLine !== null,
    // Stored when first fixed and whenever lowered, so neither is lost when its watering leaves
    // the 60-day window (msv2_set_sensor_region_line_if_lower).
    lineToStore: fixedLine !== null && Number.isFinite(fixedLine.hour) &&
        (storedLine === null || Math.round(fixedLine.top) < Math.round(storedLine.top))
      ? { top: Math.round(fixedLine.top), bottom: Math.max(0, Math.round(fixedLine.bottom)), full: Math.round(fixedLine.full), hour: fixedLine.hour } : null,
    scaleSettled: latestSettled === null ? null : Math.round(latestSettled),
    disturbed,
    lastWateredAtMs: lastWateredHour === null ? null : lastWateredHour * 3600_000,
    daysSinceLastWatering: lastWateredHour === null ? null : (nowHour - lastWateredHour) / 24,
    waterings,
  };
  if (disturbed) result.reason = "the probe was disturbed; waiting for the next watering";
  else if (fixedLine !== null) {
    result.ready = true;
    result.top = Math.round(fixedLine.top);
    result.bottom = Math.max(0, Math.round(fixedLine.bottom));
  } else if (settled === null) result.reason = "no watering has settled yet";
  else if (drop === null) result.reason = "no watering to learn the watering line from yet";
  else {
    const computed = windowFor(settled, drop);
    if (!fits(computed)) result.reason = "the region does not fit the reading's range";
    else {
      result.ready = true;
      result.top = Math.round(computed.top);
      result.bottom = Math.max(0, Math.round(computed.bottom));
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
    regionFixed: region.fixed,
    regionLineStored: region.lineStored,
    regionScaleSettled: region.scaleSettled,
    regionDisturbed: region.disturbed,
    waterings: region.waterings,
  };
}
