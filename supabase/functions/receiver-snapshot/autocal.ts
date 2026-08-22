export type AutoCalRise = {
  at: string;
  delta: number;
  // Added by migrations 024/025. Optional so edge-function and database deployment order
  // cannot break fallback percentile calibration during a rollout.
  dryBound?: number | null;
  wetBound?: number | null;
  preSampleCount?: number | null;
  postSampleCount?: number | null;
  laterSampleCount?: number | null;
  sustainedDelta?: number | null;
  retainedDelta?: number | null;
  persistenceValidated?: boolean | null;
};

export type AutoCalRawSensor = {
  sensorId: number;
  sampleCount: number;
  oldestReadingAt: string | null;
  dryBound: number | null;
  wetBound: number | null;
  anchorDryBound?: number | null;
  anchorWetBound?: number | null;
  rises: AutoCalRise[];
};

type WateringCycle = {
  atMs: number;
  dryBound: number | null;
  wetBound: number | null;
  supportWeight: number;
  persistenceValidated: boolean;
};

const AUTOCAL_MIN_SAMPLES_READY = 50;
const AUTOCAL_MIN_DAYS_READY = 3;
const AUTOCAL_CLUSTER_GAP_MS = 12 * 3600_000;
const AUTOCAL_EVENT_HALF_LIFE_DAYS = 7;
const AUTOCAL_SINGLE_CYCLE_PRIOR_WEIGHT = 0.20;
const AUTOCAL_MULTI_CYCLE_PRIOR_WEIGHT = 0.10;

function finiteBound(value: number | null | undefined): number | null {
  const numeric = Number(value);
  return value !== null && value !== undefined && Number.isFinite(numeric) ? numeric : null;
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.min(Math.max(value, minimum), maximum);
}

function evidenceSupport(rise: AutoCalRise): number {
  const preCount = finiteBound(rise.preSampleCount);
  const postCount = finiteBound(rise.postSampleCount);
  const laterCount = finiteBound(rise.laterSampleCount);
  // Older migration payloads have no counts and are treated as fully supported. Once 025
  // is live, sparse context windows contribute less instead of counting like full events.
  const preSupport = preCount === null ? 1 : clamp(preCount / 12, 0.25, 1);
  const postSupport = postCount === null ? 1 : clamp(postCount / 18, 0.25, 1);
  const laterSupport = laterCount === null ? 1 : clamp(laterCount / 24, 0.25, 1);
  // A new watering can begin improving the model as its first post-water buckets arrive,
  // but it remains deliberately weak until the later dry-down window confirms retention.
  const persistenceSupport = rise.persistenceValidated === false ? 0.25 : laterSupport;
  return preSupport * postSupport * persistenceSupport;
}

// Merge multiple large rises within 12 hours into one watering cycle. The first rise's
// pre-watering value is the dry landmark; the highest following wet peak is the wet
// landmark. This handles water soaking through the pot in more than one step without
// teaching the range that each partial rise was a separate watering.
function wateringCycles(rises: AutoCalRise[]): WateringCycle[] {
  const cycles: WateringCycle[] = [];
  let active: WateringCycle | null = null;
  let lastAtMs: number | null = null;

  const orderedRises = [...rises].sort((left, right) =>
    new Date(left.at).getTime() - new Date(right.at).getTime());
  for (const rise of orderedRises) {
    const atMs = new Date(rise.at).getTime();
    if (!Number.isFinite(atMs)) continue;

    const dryBound = finiteBound(rise.dryBound);
    const wetBound = finiteBound(rise.wetBound);
    if (dryBound === null || wetBound === null || wetBound <= dryBound) continue;
    const supportWeight = evidenceSupport(rise);
    if (lastAtMs === null || atMs - lastAtMs > AUTOCAL_CLUSTER_GAP_MS) {
      if (active !== null) cycles.push(active);
      active = {
        atMs,
        dryBound,
        wetBound,
        supportWeight,
        persistenceValidated: rise.persistenceValidated !== false,
      };
    } else if (active !== null) {
      // Several candidate steps in one 12-hour window are one multi-pour watering. Keep
      // the first supported dry point and the highest supported wet peak.
      active.wetBound = Math.max(active.wetBound!, wetBound);
      active.supportWeight = Math.max(active.supportWeight, supportWeight);
      active.persistenceValidated ||= rise.persistenceValidated !== false;
    }
    lastAtMs = atMs;
  }

  if (active !== null) cycles.push(active);
  return cycles;
}

type WeightedPoint = { value: number; weight: number };

function weightedMean(points: WeightedPoint[]): number {
  const totalWeight = points.reduce((sum, point) => sum + point.weight, 0);
  return totalWeight > 0
    ? points.reduce((sum, point) => sum + point.value * point.weight, 0) / totalWeight
    : Number.NaN;
}

function median(values: number[]): number {
  const ordered = [...values].sort((left, right) => left - right);
  const middle = Math.floor(ordered.length / 2);
  return ordered.length % 2 === 0
    ? (ordered[middle - 1] + ordered[middle]) / 2
    : ordered[middle];
}

// Once at least three waterings exist, winsorize endpoint outliers around the weighted
// median/MAD before averaging. With only one or two events there is not enough evidence to
// call either one an outlier, so both contribute normally according to recency and support.
function robustWeightedMean(points: WeightedPoint[], scaleFloor: number): number {
  if (points.length < 3) return weightedMean(points);
  // Estimate the robust center without recency weights so one brand-new bad event cannot
  // become the center solely because it is newer than all previous evidence. Recency still
  // controls the final mean after values have been constrained around that robust center.
  const center = median(points.map((point) => point.value));
  const mad = median(points.map((point) => Math.abs(point.value - center)));
  const limit = Math.max(scaleFloor, 2.5 * 1.4826 * mad);
  return weightedMean(points.map((point) => ({
    value: clamp(point.value, center - limit, center + limit),
    weight: point.weight,
  })));
}

function adaptiveCycleBounds(
  cycles: WateringCycle[],
  fallbackDry: number,
  fallbackWet: number,
) {
  const newestAtMs = cycles[cycles.length - 1].atMs;
  const fallbackSpan = Math.max(1, fallbackWet - fallbackDry);
  const weightedCycles = cycles.map((cycle) => {
    const ageDays = Math.max(0, newestAtMs - cycle.atMs) / (24 * 3600_000);
    const recencyWeight = Math.pow(0.5, ageDays / AUTOCAL_EVENT_HALF_LIFE_DAYS);
    return {
      dry: cycle.dryBound!,
      span: cycle.wetBound! - cycle.dryBound!,
      weight: cycle.supportWeight * recencyWeight,
    };
  });

  const dryPoints = weightedCycles.map((cycle) => ({ value: cycle.dry, weight: cycle.weight }));
  const spanPoints = weightedCycles.map((cycle) => ({ value: cycle.span, weight: cycle.weight }));
  const eventDry = robustWeightedMean(dryPoints, Math.max(1, fallbackSpan * 0.05));
  const eventSpan = robustWeightedMean(spanPoints, Math.max(1, fallbackSpan * 0.10));
  const evidenceWeight = weightedCycles.reduce((sum, cycle) => sum + cycle.weight, 0);

  // The time-weighted distribution remains a weak prior, so every new five-minute bucket
  // can refine the model even between waterings. Watering evidence dominates immediately
  // and increasingly so as more supported cycles accumulate.
  const priorWeight = cycles.length === 1
    ? AUTOCAL_SINGLE_CYCLE_PRIOR_WEIGHT
    : AUTOCAL_MULTI_CYCLE_PRIOR_WEIGHT;
  const learnedDry = (eventDry * evidenceWeight + fallbackDry * priorWeight) /
    (evidenceWeight + priorWeight);
  const learnedSpan = (eventSpan * evidenceWeight + fallbackSpan * priorWeight) /
    (evidenceWeight + priorWeight);

  return {
    dryBound: Math.round(learnedDry),
    wetBound: Math.round(learnedDry + Math.max(1, learnedSpan)),
    effectiveCycleWeight: evidenceWeight,
  };
}

export function deriveAutoCal(sensor: AutoCalRawSensor, nowMs: number) {
  const cycles = wateringCycles(sensor.rises ?? []);
  const oldestReadingAtMs = sensor.oldestReadingAt ? new Date(sensor.oldestReadingAt).getTime() : null;
  const ready = sensor.sampleCount >= AUTOCAL_MIN_SAMPLES_READY &&
    oldestReadingAtMs !== null &&
    Number.isFinite(oldestReadingAtMs) &&
    (nowMs - oldestReadingAtMs) >= AUTOCAL_MIN_DAYS_READY * 24 * 3600_000;

  let daysSinceLastWatering: number | null = null;
  let lastWateredAtMs: number | null = null;
  if (cycles.length > 0) {
    lastWateredAtMs = cycles[cycles.length - 1].atMs;
    daysSinceLastWatering = (nowMs - lastWateredAtMs) / (24 * 3600_000);
  }

  // Uniform five-minute percentiles are both the no-event fallback and a weak live prior.
  // Every sustained watering adds another supported dry+span observation; older cycles
  // decay gradually and endpoint outliers are constrained once enough cycles exist.
  const fallbackDry = finiteBound(sensor.dryBound);
  const fallbackWet = finiteBound(sensor.wetBound);
  let dryBound = fallbackDry;
  let wetBound = fallbackWet;
  let calibrationSource = "time-weighted-percentiles";
  let effectiveCycleWeight = 0;
  if (ready && cycles.length > 0 && fallbackDry !== null && fallbackWet !== null && fallbackWet > fallbackDry) {
    const learned = adaptiveCycleBounds(cycles, fallbackDry, fallbackWet);
    dryBound = learned.dryBound;
    wetBound = learned.wetBound;
    effectiveCycleWeight = learned.effectiveCycleWeight;
    calibrationSource = "adaptive-watering-history";
  }

  return {
    sensorId: sensor.sensorId,
    ready,
    sampleCount: sensor.sampleCount,
    dryBound,
    wetBound,
    daysSinceLastWatering,
    lastWateredAtMs,
    // Kept for receiver/dashboard response compatibility. Watering-cycle bounds no
    // longer need the old rolling-percentile anchor clamp or its drift warning.
    dryBoundTrendingLower: false,
    calibrationSource,
    wateringCycleCount: cycles.length,
    provisionalWateringCycleCount: cycles.filter((cycle) => !cycle.persistenceValidated).length,
    effectiveCycleWeight,
    // Informational/debug field; existing firmware ignores unknown JSON members.
    calibrationConfidence: Math.round(100 * (1 - Math.exp(-effectiveCycleWeight / 2))),
  };
}
