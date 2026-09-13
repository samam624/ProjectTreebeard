#!/usr/bin/env node

// Fit and validate a simple causal MSV3 thermal compensation model against full-resolution
// history read through the already-linked Supabase CLI. No cloud or receiver state is modified.

import { execSync, spawnSync } from "node:child_process";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const MAX_FDC_COUNT = 0x0fffffff;
const TAU_CANDIDATES_H = [0, 0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4, 6, 8, 12];
const CLOUD_DIR = fileURLToPath(new URL("../../cloud/", import.meta.url));

function argument(name, fallback) {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length ? process.argv[index + 1] : fallback;
}

function mean(values) {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function median(values) {
  return percentile(values, 0.5);
}

function percentile(values, fraction) {
  const ordered = [...values].sort((left, right) => left - right);
  const location = (ordered.length - 1) * fraction;
  const lower = Math.floor(location);
  const upper = Math.ceil(location);
  if (lower === upper) return ordered[lower];
  const weight = location - lower;
  return ordered[lower] * (1 - weight) + ordered[upper] * weight;
}

function solveLinear(matrix, vector) {
  const size = vector.length;
  const augmented = matrix.map((row, index) => [...row, vector[index]]);
  for (let column = 0; column < size; column += 1) {
    let pivot = column;
    for (let row = column + 1; row < size; row += 1) {
      if (Math.abs(augmented[row][column]) > Math.abs(augmented[pivot][column])) pivot = row;
    }
    if (Math.abs(augmented[pivot][column]) < 1e-14) throw new Error("singular regression matrix");
    [augmented[column], augmented[pivot]] = [augmented[pivot], augmented[column]];
    const scale = augmented[column][column];
    augmented[column] = augmented[column].map((value) => value / scale);
    for (let row = 0; row < size; row += 1) {
      if (row === column) continue;
      const factor = augmented[row][column];
      for (let item = 0; item <= size; item += 1) {
        augmented[row][item] -= factor * augmented[column][item];
      }
    }
  }
  return augmented.map((row) => row[size]);
}

function regress(features, targets) {
  const width = features[0].length;
  const xtx = Array.from({ length: width }, () => Array(width).fill(0));
  const xty = Array(width).fill(0);
  features.forEach((row, index) => {
    for (let left = 0; left < width; left += 1) {
      xty[left] += row[left] * targets[index];
      for (let right = 0; right < width; right += 1) xtx[left][right] += row[left] * row[right];
    }
  });
  const coefficients = solveLinear(xtx, xty);
  const residuals = targets.map(
    (target, index) => target - coefficients.reduce((sum, coefficient, item) => sum + coefficient * features[index][item], 0),
  );
  const targetMean = mean(targets);
  const total = targets.reduce((sum, target) => sum + (target - targetMean) ** 2, 0);
  const unexplained = residuals.reduce((sum, residual) => sum + residual ** 2, 0);
  return { coefficients, residuals, rSquared: total ? 1 - unexplained / total : 1 };
}

function emaTemperature(timesH, temperatures, tauH) {
  if (tauH <= 0) return [...temperatures];
  const filtered = [temperatures[0]];
  for (let index = 1; index < temperatures.length; index += 1) {
    const deltaH = Math.max(0, timesH[index] - timesH[index - 1]);
    const alpha = 1 - Math.exp(-deltaH / tauH);
    filtered.push(filtered.at(-1) + alpha * (temperatures[index] - filtered.at(-1)));
  }
  return filtered;
}

function harmonicMetrics(timesH, values, phases = null) {
  const trendFeatures = timesH.map((timeH, index) => {
    const row = [1, timeH / 24];
    if (phases) row.push(phases[index]);
    return row;
  });
  const detrended = regress(trendFeatures, values).residuals;
  const harmonicFeatures = timesH.map((timeH) => [
    1,
    Math.sin((2 * Math.PI * timeH) / 24),
    Math.cos((2 * Math.PI * timeH) / 24),
  ]);
  const harmonic = regress(harmonicFeatures, detrended);
  const amplitude = Math.hypot(harmonic.coefficients[1], harmonic.coefficients[2]);
  return {
    daily_harmonic_peak_to_peak_percent: 200 * amplitude,
    detrended_p95_minus_p05_percent: 100 * (percentile(detrended, 0.95) - percentile(detrended, 0.05)),
    detrended_rms_percent: 100 * Math.sqrt(mean(detrended.map((value) => value ** 2))),
    nonharmonic_rms_percent: 100 * Math.sqrt(mean(harmonic.residuals.map((value) => value ** 2))),
  };
}

function correlation(left, right) {
  const leftMean = mean(left);
  const rightMean = mean(right);
  const numerator = left.reduce((sum, value, index) => sum + (value - leftMean) * (right[index] - rightMean), 0);
  const denominator = Math.sqrt(
    left.reduce((sum, value) => sum + (value - leftMean) ** 2, 0)
      * right.reduce((sum, value) => sum + (value - rightMean) ** 2, 0),
  );
  return denominator ? numerator / denominator : null;
}

function endpointChange(timesH, values) {
  const first = values.filter((_, index) => timesH[index] <= timesH[0] + 3);
  const last = values.filter((_, index) => timesH[index] >= timesH.at(-1) - 3);
  return 100 * (median(last) - median(first));
}

function select(values, indices) {
  return indices.map((index) => values[index]);
}

function fetchRows(receiverId, sensorId, hours) {
  if (!/^[A-Za-z0-9_-]+$/.test(receiverId)) throw new Error("receiver ID contains unexpected characters");
  const sql = [
    "select received_at, raw_fdc_counts, bare_tank_fdc_counts, board_temp_quarter_c, tank_phase",
    "from public.msv2_sensor_history",
    `where receiver_id = '${receiverId}'`,
    `and sensor_id = ${sensorId}`,
    // Keep the shell command free of a bare `>` token: Windows cmd treats that as output
    // redirection even though it originated inside the SQL argument.
    `and received_at between now() - interval '${hours} hours' and now()`,
    "and raw_fdc_counts is not null",
    "and bare_tank_fdc_counts is not null",
    "and board_temp_quarter_c is not null",
    "and tank_phase in (0, 1)",
    "order by received_at asc;",
  ].join(" ");
  if (process.platform === "win32") {
    // receiverId/numeric arguments are validated above, and the generated SQL contains no double
    // quotes, so invoking the .cmd wrapper through cmd.exe cannot gain a second command segment.
    const stdout = execSync(`supabase.cmd db query --linked "${sql}"`, {
      encoding: "utf8",
      maxBuffer: 16 * 1024 * 1024,
      cwd: CLOUD_DIR,
    });
    return JSON.parse(stdout).rows ?? [];
  }
  const result = spawnSync("supabase", ["db", "query", "--linked", sql], {
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
    cwd: CLOUD_DIR,
  });
  if (result.status !== 0) throw new Error(result.error?.message || result.stderr || "Supabase query failed");
  return JSON.parse(result.stdout).rows ?? [];
}

function parseRows(rows) {
  const parsed = rows
    .map((row) => ({
      timestamp: new Date(String(row.received_at).replace(" ", "T").replace(/([+-]\d{2})$/, "$1:00")),
      connected: Number(row.raw_fdc_counts),
      bare: Number(row.bare_tank_fdc_counts),
      temperature: Number(row.board_temp_quarter_c) / 4,
      phase: Number(row.tank_phase),
    }))
    .filter((row) => Number.isFinite(row.timestamp.getTime())
      && Number.isInteger(row.connected) && row.connected > 0 && row.connected <= MAX_FDC_COUNT
      && Number.isInteger(row.bare) && row.bare > 0 && row.bare <= MAX_FDC_COUNT
      && Number.isFinite(row.temperature) && (row.phase === 0 || row.phase === 1))
    .sort((left, right) => left.timestamp - right.timestamp);
  if (parsed.length < 100) throw new Error(`only ${parsed.length} complete valid rows were returned`);
  const startMs = parsed[0].timestamp.getTime();
  return {
    timestamp: parsed.map((row) => row.timestamp),
    timesH: parsed.map((row) => (row.timestamp.getTime() - startMs) / 3_600_000),
    connected: parsed.map((row) => row.connected),
    bare: parsed.map((row) => row.bare),
    temperatures: parsed.map((row) => row.temperature),
    phases: parsed.map((row) => row.phase),
  };
}

const receiverId = String(argument("--receiver", "MSV2088A3104A7AC"));
const sensorId = Number(argument("--sensor", "4"));
const hours = Number(argument("--hours", "72"));
const holdoutHours = Number(argument("--holdout-hours", "24"));
if (!Number.isInteger(sensorId) || sensorId <= 0 || !Number.isFinite(hours) || hours <= holdoutHours) {
  throw new Error("invalid sensor/hours arguments");
}

const data = parseRows(fetchRows(receiverId, sensorId, hours));
const rawConnectedLogCap = data.connected.map((count) => -2 * Math.log(count));
const ratioLogCap = data.connected.map((connected, index) => 2 * (Math.log(data.bare[index]) - Math.log(connected)));
const holdoutStartH = data.timesH.at(-1) - holdoutHours;
const trainingIndices = data.timesH.map((_, index) => index).filter((index) => data.timesH[index] < holdoutStartH);
const holdoutIndices = data.timesH.map((_, index) => index).filter((index) => data.timesH[index] >= holdoutStartH);
if (trainingIndices.length < 100 || holdoutIndices.length < 100) throw new Error("training or holdout window is too short");

const candidates = TAU_CANDIDATES_H.map((tauH) => {
  const effectiveTemperature = emaTemperature(data.timesH, data.temperatures, tauH);
  const features = trainingIndices.map((index) => [
    1,
    data.timesH[index] / 24,
    effectiveTemperature[index],
    data.phases[index],
  ]);
  const fit = regress(features, select(ratioLogCap, trainingIndices));
  return {
    tauH,
    effectiveTemperature,
    ...fit,
    trainingRms: Math.sqrt(mean(fit.residuals.map((value) => value ** 2))),
  };
});
const best = candidates.reduce((winner, candidate) => candidate.trainingRms < winner.trainingRms ? candidate : winner);
const [, trendPerDay, temperatureCoefficient, phaseOffset] = best.coefficients;
const referenceTemperature = mean(select(best.effectiveTemperature, trainingIndices));
const corrected = ratioLogCap.map((value, index) => value
  - temperatureCoefficient * (best.effectiveTemperature[index] - referenceTemperature)
  - phaseOffset * data.phases[index]);

// Experimental second model: allow thermal gain to change smoothly while the pot dries. Time is
// only a short-window proxy here; a production learner should tie this to the slow moisture state.
const trainingMidDay = mean(select(data.timesH, trainingIndices)) / 24;
const changingGainCandidates = TAU_CANDIDATES_H.map((tauH) => {
  const effectiveTemperature = emaTemperature(data.timesH, data.temperatures, tauH);
  const temperatureReference = mean(select(effectiveTemperature, trainingIndices));
  const features = trainingIndices.map((index) => {
    const centeredDay = data.timesH[index] / 24 - trainingMidDay;
    const centeredTemperature = effectiveTemperature[index] - temperatureReference;
    return [1, centeredDay, centeredTemperature, centeredDay * centeredTemperature, data.phases[index]];
  });
  const fit = regress(features, select(ratioLogCap, trainingIndices));
  return {
    tauH,
    effectiveTemperature,
    temperatureReference,
    ...fit,
    trainingRms: Math.sqrt(mean(fit.residuals.map((value) => value ** 2))),
  };
});
const bestChangingGain = changingGainCandidates.reduce(
  (winner, candidate) => candidate.trainingRms < winner.trainingRms ? candidate : winner,
);
const [, changingTrend, changingGainAtMidpoint, gainChangePerDay, changingPhaseOffset] = bestChangingGain.coefficients;
const changingGainCorrected = ratioLogCap.map((value, index) => {
  const centeredDay = data.timesH[index] / 24 - trainingMidDay;
  const centeredTemperature = bestChangingGain.effectiveTemperature[index] - bestChangingGain.temperatureReference;
  return value
    - (changingGainAtMidpoint + gainChangePerDay * centeredDay) * centeredTemperature
    - changingPhaseOffset * data.phases[index];
});

// A display-friendly adaptive model: use a slow causal estimate of capacitance above bare as the
// gain state, matching the measured fact that wetter soil has the larger temperature response.
const MOISTURE_STATE_TAU_CANDIDATES_H = [12, 24, 36, 48];
const moistureScaledCandidates = [];
for (const temperatureTauH of TAU_CANDIDATES_H) {
  const effectiveTemperature = emaTemperature(data.timesH, data.temperatures, temperatureTauH);
  const temperatureReference = mean(select(effectiveTemperature, trainingIndices));
  for (const moistureTauH of MOISTURE_STATE_TAU_CANDIDATES_H) {
    const slowLogCap = emaTemperature(data.timesH, ratioLogCap, moistureTauH);
    const slowExcess = slowLogCap.map((value) => Math.exp(value) - 1);
    const thermalPredictor = effectiveTemperature.map(
      (temperature, index) => slowExcess[index] * (temperature - temperatureReference),
    );
    const fit = regress(
      trainingIndices.map((index) => [1, data.timesH[index] / 24, thermalPredictor[index], data.phases[index]]),
      select(ratioLogCap, trainingIndices),
    );
    moistureScaledCandidates.push({
      temperatureTauH,
      moistureTauH,
      effectiveTemperature,
      temperatureReference,
      slowExcess,
      thermalPredictor,
      ...fit,
      trainingRms: Math.sqrt(mean(fit.residuals.map((value) => value ** 2))),
    });
  }
}
const bestMoistureScaled = moistureScaledCandidates.reduce(
  (winner, candidate) => candidate.trainingRms < winner.trainingRms ? candidate : winner,
);
const [, moistureScaledTrend, gainPerExcess, moistureScaledPhaseOffset] = bestMoistureScaled.coefficients;
const moistureScaledCorrected = ratioLogCap.map((value, index) => value
  - gainPerExcess * bestMoistureScaled.thermalPredictor[index]
  - moistureScaledPhaseOffset * data.phases[index]);

const holdoutTimes = select(data.timesH, holdoutIndices);
const holdoutPhases = select(data.phases, holdoutIndices);
const rawMetrics = harmonicMetrics(holdoutTimes, select(rawConnectedLogCap, holdoutIndices), holdoutPhases);
const ratioMetrics = harmonicMetrics(holdoutTimes, select(ratioLogCap, holdoutIndices), holdoutPhases);
const correctedMetrics = harmonicMetrics(holdoutTimes, select(corrected, holdoutIndices));
const changingGainCorrectedMetrics = harmonicMetrics(
  holdoutTimes,
  select(changingGainCorrected, holdoutIndices),
);
const moistureScaledCorrectedMetrics = harmonicMetrics(
  holdoutTimes,
  select(moistureScaledCorrected, holdoutIndices),
);

const perBlock = [];
for (let block = 0; block < 3; block += 1) {
  const blockStart = data.timesH[0] + 24 * block;
  const indices = data.timesH.map((_, index) => index)
    .filter((index) => data.timesH[index] >= blockStart && data.timesH[index] < blockStart + 24);
  if (indices.length < 100) continue;
  const fit = regress(
    indices.map((index) => [1, (data.timesH[index] - blockStart) / 24, best.effectiveTemperature[index], data.phases[index]]),
    select(ratioLogCap, indices),
  );
  perBlock.push({
    block: block + 1,
    rows: indices.length,
    mean_capacitance_above_bare_percent: 100 * (Math.exp(mean(select(ratioLogCap, indices))) - 1),
    mean_temperature_corrected_capacitance_above_bare_percent: 100 * (Math.exp(mean(select(moistureScaledCorrected, indices))) - 1),
    mean_board_temperature_c: mean(select(data.temperatures, indices)),
    temperature_coefficient_log_cap_percent_per_c: 100 * fit.coefficients[2],
    slow_trend_log_cap_percent_per_day: 100 * fit.coefficients[1],
    r_squared: fit.rSquared,
  });
}

const output = {
  window: {
    receiver_id: receiverId,
    sensor_id: sensorId,
    rows: data.timesH.length,
    first_at: data.timestamp[0].toISOString(),
    last_at: data.timestamp.at(-1).toISOString(),
    duration_hours: data.timesH.at(-1),
    median_sample_interval_seconds: median(data.timesH.slice(1).map((timeH, index) => (timeH - data.timesH[index]) * 3600)),
    temperature_min_c: Math.min(...data.temperatures),
    temperature_max_c: Math.max(...data.temperatures),
    connected_min: Math.min(...data.connected),
    connected_max: Math.max(...data.connected),
    bare_min: Math.min(...data.bare),
    bare_max: Math.max(...data.bare),
    mean_capacitance_above_bare_percent: 100 * (Math.exp(mean(ratioLogCap)) - 1),
  },
  learned_model: {
    training_rows: trainingIndices.length,
    holdout_rows: holdoutIndices.length,
    causal_temperature_time_constant_hours: best.tauH,
    temperature_coefficient_log_cap_percent_per_c: 100 * temperatureCoefficient,
    tank_phase_offset_log_cap_percent: 100 * phaseOffset,
    training_slow_trend_log_cap_percent_per_day: 100 * trendPerDay,
    training_r_squared: best.rSquared,
  },
  rejected_elapsed_time_gain_model: {
    causal_temperature_time_constant_hours: bestChangingGain.tauH,
    temperature_coefficient_at_training_midpoint_log_cap_percent_per_c: 100 * changingGainAtMidpoint,
    temperature_coefficient_change_log_cap_percent_per_c_per_day: 100 * gainChangePerDay,
    training_slow_trend_log_cap_percent_per_day: 100 * changingTrend,
    tank_phase_offset_log_cap_percent: 100 * changingPhaseOffset,
    training_r_squared: bestChangingGain.rSquared,
  },
  moisture_scaled_gain_model: {
    causal_temperature_time_constant_hours: bestMoistureScaled.temperatureTauH,
    causal_moisture_state_time_constant_hours: bestMoistureScaled.moistureTauH,
    temperature_reference_c: bestMoistureScaled.temperatureReference,
    gain_per_capacitance_excess_per_c: gainPerExcess,
    training_slow_trend_log_cap_percent_per_day: 100 * moistureScaledTrend,
    tank_phase_offset_log_cap_percent: 100 * moistureScaledPhaseOffset,
    training_r_squared: bestMoistureScaled.rSquared,
  },
  held_out_window: {
    hours: holdoutHours,
    raw_connected_log_cap: rawMetrics,
    bare_normalized_log_cap: ratioMetrics,
    temperature_corrected_log_cap: correctedMetrics,
    rejected_elapsed_time_gain_corrected_log_cap: changingGainCorrectedMetrics,
    moisture_scaled_temperature_corrected_log_cap: moistureScaledCorrectedMetrics,
    daily_harmonic_reduction_from_bare_reference_percent: 100 * (1 - ratioMetrics.daily_harmonic_peak_to_peak_percent / rawMetrics.daily_harmonic_peak_to_peak_percent),
    daily_harmonic_reduction_from_temperature_model_percent: 100 * (1 - correctedMetrics.daily_harmonic_peak_to_peak_percent / ratioMetrics.daily_harmonic_peak_to_peak_percent),
    daily_harmonic_reduction_from_rejected_elapsed_time_gain_model_percent: 100 * (1 - changingGainCorrectedMetrics.daily_harmonic_peak_to_peak_percent / ratioMetrics.daily_harmonic_peak_to_peak_percent),
    daily_harmonic_reduction_from_moisture_scaled_model_percent: 100 * (1 - moistureScaledCorrectedMetrics.daily_harmonic_peak_to_peak_percent / ratioMetrics.daily_harmonic_peak_to_peak_percent),
  },
  full_window: {
    ratio_metrics: harmonicMetrics(data.timesH, ratioLogCap, data.phases),
    corrected_metrics: harmonicMetrics(data.timesH, corrected),
    moisture_scaled_corrected_metrics: harmonicMetrics(data.timesH, moistureScaledCorrected),
    ratio_temperature_correlation: correlation(ratioLogCap, best.effectiveTemperature),
    corrected_temperature_correlation: correlation(corrected, best.effectiveTemperature),
    bare_normalized_endpoint_change_percent: endpointChange(data.timesH, ratioLogCap),
    corrected_endpoint_change_percent: endpointChange(data.timesH, corrected),
    moisture_scaled_corrected_endpoint_change_percent: endpointChange(data.timesH, moistureScaledCorrected),
  },
  per_24_hour_block: perBlock,
  tau_candidates: candidates.map((candidate) => ({
    tau_h: candidate.tauH,
    training_rms_percent: 100 * candidate.trainingRms,
    training_r_squared: candidate.rSquared,
  })),
};

function detrendedSeries(timesH, values, phases = null) {
  const features = timesH.map((timeH, index) => {
    const row = [1, timeH / 24];
    if (phases) row.push(phases[index]);
    return row;
  });
  return regress(features, values).residuals.map((value) => 100 * value);
}

function renderHtmlArtifact(targetPath) {
  const timestampMs = data.timestamp.map((timestamp) => timestamp.getTime());
  const referenceExcess = ratioLogCap.map((value) => 100 * (Math.exp(value) - 1));
  const correctedExcess = moistureScaledCorrected.map((value) => 100 * (Math.exp(value) - 1));
  const holdoutTimestampMs = select(timestampMs, holdoutIndices);
  const holdoutRawResidual = detrendedSeries(
    holdoutTimes,
    select(rawConnectedLogCap, holdoutIndices),
    holdoutPhases,
  );
  const holdoutRatioResidual = detrendedSeries(
    holdoutTimes,
    select(ratioLogCap, holdoutIndices),
    holdoutPhases,
  );
  const holdoutCorrectedResidual = detrendedSeries(
    holdoutTimes,
    select(moistureScaledCorrected, holdoutIndices),
  );
  const totalDailyReduction = 100 * (
    1 - moistureScaledCorrectedMetrics.daily_harmonic_peak_to_peak_percent
      / rawMetrics.daily_harmonic_peak_to_peak_percent
  );
  const chartPayload = {
    holdoutStartMs: timestampMs[holdoutIndices[0]],
    full: timestampMs.map((time, index) => ({
      time,
      temperature: data.temperatures[index],
      connected: data.connected[index],
      bare: data.bare[index],
      referenceExcess: referenceExcess[index],
      correctedExcess: correctedExcess[index],
    })),
    holdout: holdoutTimestampMs.map((time, index) => ({
      time,
      raw: holdoutRawResidual[index],
      reference: holdoutRatioResidual[index],
      corrected: holdoutCorrectedResidual[index],
    })),
  };
  const escapedPayload = JSON.stringify(chartPayload).replaceAll("<", "\\u003c");
  const generatedAt = new Date().toISOString();
  const html = `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MSV3 real-pot thermal compensation</title>
  <style>
    :root { color-scheme: dark; --bg:#0b1119; --card:#141e2a; --line:#2a394a; --text:#edf4fb; --muted:#9eb2c7; }
    * { box-sizing:border-box; }
    body { margin:0; background:linear-gradient(180deg,#091019,#101925); color:var(--text); font-family:Inter,Segoe UI,Arial,sans-serif; }
    main { width:min(1180px,calc(100% - 28px)); margin:28px auto 60px; }
    h1 { margin:0 0 8px; font-size:clamp(25px,4vw,42px); letter-spacing:-.03em; }
    .subtitle { color:var(--muted); line-height:1.55; max-width:900px; }
    .metrics { display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); gap:12px; margin:24px 0; }
    .metric,.card { background:rgba(20,30,42,.96); border:1px solid #273646; border-radius:14px; box-shadow:0 12px 35px rgba(0,0,0,.18); }
    .metric { padding:16px 18px; }
    .metric strong { display:block; font-size:28px; color:#78dcad; margin-bottom:3px; }
    .metric span,.note { color:var(--muted); font-size:13px; line-height:1.45; }
    .card { padding:18px 18px 12px; margin:14px 0; }
    .card h2 { font-size:18px; margin:0 0 3px; }
    .card p { color:var(--muted); margin:0 0 10px; font-size:13px; }
    .chart-wrap { position:relative; min-height:250px; }
    svg { width:100%; height:auto; display:block; overflow:visible; }
    .grid { stroke:var(--line); stroke-width:1; }
    .axis { fill:#91a8bd; font-size:12px; }
    .holdout { fill:#50c993; opacity:.055; }
    .split { stroke:#78dcad; stroke-width:1.5; stroke-dasharray:6 6; opacity:.9; }
    .series { fill:none; stroke-width:2.2; stroke-linejoin:round; stroke-linecap:round; }
    .legend { display:flex; flex-wrap:wrap; gap:14px; margin:6px 0 2px 62px; color:#b8c9d8; font-size:12px; }
    .legend i { width:18px; height:3px; display:inline-block; border-radius:3px; margin-right:6px; vertical-align:middle; }
    .tooltip { position:absolute; display:none; pointer-events:none; z-index:3; background:#071019f2; border:1px solid #40546a; border-radius:8px; padding:8px 10px; font-size:12px; line-height:1.5; box-shadow:0 8px 24px #0008; white-space:nowrap; }
    .crosshair { stroke:#b6c8d8; stroke-width:1; opacity:.55; }
    .callout { padding:14px 16px; border-left:4px solid #78dcad; background:#111d28; border-radius:4px 10px 10px 4px; color:#c9d7e4; line-height:1.55; margin:20px 0; }
    code { color:#ffd58a; }
    footer { color:#71869a; font-size:12px; margin-top:24px; }
    @media (max-width:650px) { main{width:min(100% - 14px,1180px);margin-top:16px}.card{padding:13px 8px 8px}.legend{margin-left:50px}.series{stroke-width:2.8} }
  </style>
</head>
<body>
<main>
  <h1>MSV3 real-pot thermal compensation</h1>
  <div class="subtitle">Probe 4 · ${data.timesH.length.toLocaleString("en-US")} full-resolution readings · ${data.timesH.at(-1).toFixed(1)} hours. The first 48 hours train the causal model; the green-shaded final 24 hours are held out.</div>
  <section class="metrics">
    <div class="metric"><strong>${totalDailyReduction.toFixed(1)}%</strong><span>held-out daily-cycle reduction versus raw FDC</span></div>
    <div class="metric"><strong>${moistureScaledCorrectedMetrics.daily_harmonic_peak_to_peak_percent.toFixed(3)}%</strong><span>corrected final-day 24-hour peak-to-peak component</span></div>
    <div class="metric"><strong>${bestMoistureScaled.temperatureTauH} h</strong><span>learned causal temperature lag</span></div>
    <div class="metric"><strong>${output.per_24_hour_block[0].mean_temperature_corrected_capacitance_above_bare_percent.toFixed(1)} → ${output.per_24_hour_block[2].mean_temperature_corrected_capacitance_above_bare_percent.toFixed(1)}%</strong><span>real corrected dry-down retained across daily blocks</span></div>
  </section>

  <div class="callout"><strong>How to read this:</strong> higher moisture-signal values mean wetter. The correction uses temperature only to remove the repeating thermal load; it retains the slower three-day fall caused by the real pot drying. Hover any plot for exact values. The dashed line marks the train/holdout boundary.</div>

  <section class="card"><h2>Board (die) temperature</h2><p>The measured thermal driver; orange is an input to the correction, not the output.</p><div class="chart-wrap"><svg id="temperature"></svg><div class="tooltip"></div></div></section>
  <section class="card"><h2>Raw FDC counts — moisture connected</h2><p>Original connected tank. Higher count means lower capacitance and therefore drier.</p><div class="chart-wrap"><svg id="connected"></svg><div class="tooltip"></div></div></section>
  <section class="card"><h2>Raw FDC counts — bare tank</h2><p>Electrodes disconnected. This captures common electronics/tank thermal movement.</p><div class="chart-wrap"><svg id="bare"></svg><div class="tooltip"></div></div></section>
  <section class="card"><h2>Moisture-bearing signal before and after temperature correction</h2><p>Same physical units and time axis. The corrected green trace should retain dry-down while losing the repeated daily bulges.</p><div class="legend"><span><i style="background:#e1ad4d"></i>Reference-normalized</span><span><i style="background:#63d59b"></i>Temperature-corrected</span></div><div class="chart-wrap"><svg id="moisture"></svg><div class="tooltip"></div></div></section>
  <section class="card"><h2>Held-out final day: thermal-cycle residual</h2><p>A straight slow trend is removed from each series only for this diagnostic view. This makes the remaining daily wave directly comparable; these final-day rows were not used to fit the model.</p><div class="legend"><span><i style="background:#a879e5"></i>Raw connected</span><span><i style="background:#e1ad4d"></i>Reference-normalized</span><span><i style="background:#63d59b"></i>Temperature-corrected</span></div><div class="chart-wrap"><svg id="residual"></svg><div class="tooltip"></div></div></section>

  <div class="note">Experimental result, not yet used for watering alerts. This window validates thermal-cycle removal and dry-down preservation; the next labeled watering event is still required to validate watering-step magnitude and recovery timing.</div>
  <footer>Generated ${generatedAt} by MSV3/scripts/analyze_msv3_thermal_compensation.mjs. Embedded data are read-only history from receiver ${receiverId}, sensor ${sensorId}.</footer>
</main>
<script>
const DATA=${escapedPayload};
const NS='http://www.w3.org/2000/svg';
const timeFormat=new Intl.DateTimeFormat('en-US',{timeZone:'America/Los_Angeles',weekday:'short',hour:'numeric',minute:'2-digit'});
const numberFormat=new Intl.NumberFormat('en-US',{maximumFractionDigits:0});
function element(name,attributes,text){const node=document.createElementNS(NS,name);Object.entries(attributes||{}).forEach(([key,value])=>node.setAttribute(key,value));if(text!=null)node.textContent=text;return node;}
function nearest(points,target){let lo=0,hi=points.length-1;while(lo<hi){const mid=Math.floor((lo+hi)/2);if(points[mid].x<target)lo=mid+1;else hi=mid;}if(lo>0&&Math.abs(points[lo-1].x-target)<Math.abs(points[lo].x-target))return lo-1;return lo;}
function drawChart(id,series,options){
  const svg=document.getElementById(id),wrap=svg.parentElement,tooltip=wrap.querySelector('.tooltip');
  const W=1000,H=250,L=72,R=18,T=18,B=38,PW=W-L-R,PH=H-T-B;
  svg.setAttribute('viewBox','0 0 '+W+' '+H);
  const all=series.flatMap(item=>item.points).filter(point=>Number.isFinite(point.y));
  const xMin=Math.min(...all.map(point=>point.x)),xMax=Math.max(...all.map(point=>point.x));
  let yMin=Math.min(...all.map(point=>point.y)),yMax=Math.max(...all.map(point=>point.y));
  const margin=Math.max((yMax-yMin)*.08,options.minimumMargin||0.01);yMin-=margin;yMax+=margin;
  const xp=value=>L+(value-xMin)/(xMax-xMin)*PW,yp=value=>T+(yMax-value)/(yMax-yMin)*PH;
  if(options.shadeHoldout&&DATA.holdoutStartMs>xMin&&DATA.holdoutStartMs<xMax){svg.appendChild(element('rect',{x:xp(DATA.holdoutStartMs),y:T,width:xp(xMax)-xp(DATA.holdoutStartMs),height:PH,class:'holdout'}));}
  for(let tick=0;tick<=4;tick+=1){const y=T+PH*tick/4;svg.appendChild(element('line',{x1:L,y1:y,x2:W-R,y2:y,class:'grid'}));const value=yMax-(yMax-yMin)*tick/4;svg.appendChild(element('text',{x:L-10,y:y+4,'text-anchor':'end',class:'axis'},options.formatY(value)));}
  for(let tick=0;tick<=4;tick+=1){const x=L+PW*tick/4,value=xMin+(xMax-xMin)*tick/4;svg.appendChild(element('text',{x,y:H-12,'text-anchor':tick===0?'start':tick===4?'end':'middle',class:'axis'},timeFormat.format(new Date(value))));}
  series.forEach(item=>{const d=item.points.map((point,index)=>(index?'L':'M')+xp(point.x).toFixed(2)+','+yp(point.y).toFixed(2)).join(' ');svg.appendChild(element('path',{d,class:'series',stroke:item.color}));});
  if(options.shadeHoldout&&DATA.holdoutStartMs>xMin&&DATA.holdoutStartMs<xMax){svg.appendChild(element('line',{x1:xp(DATA.holdoutStartMs),y1:T,x2:xp(DATA.holdoutStartMs),y2:T+PH,class:'split'}));}
  const crosshair=element('line',{y1:T,y2:T+PH,class:'crosshair',visibility:'hidden'});svg.appendChild(crosshair);
  const hit=element('rect',{x:L,y:T,width:PW,height:PH,fill:'transparent'});svg.appendChild(hit);
  hit.addEventListener('mousemove',event=>{const rect=svg.getBoundingClientRect();const mouseX=(event.clientX-rect.left)/rect.width*W;const target=xMin+Math.max(0,Math.min(1,(mouseX-L)/PW))*(xMax-xMin);const index=nearest(series[0].points,target);const time=series[0].points[index].x;crosshair.setAttribute('x1',xp(time));crosshair.setAttribute('x2',xp(time));crosshair.setAttribute('visibility','visible');tooltip.innerHTML='<strong>'+timeFormat.format(new Date(time))+'</strong><br>'+series.map(item=>'<span style="color:'+item.color+'">'+item.name+': '+options.formatTooltip(item.points[index].y)+'</span>').join('<br>');tooltip.style.display='block';tooltip.style.left=Math.min(rect.width-185,Math.max(8,event.clientX-rect.left+12))+'px';tooltip.style.top=Math.max(8,event.clientY-rect.top-48)+'px';});
  hit.addEventListener('mouseleave',()=>{tooltip.style.display='none';crosshair.setAttribute('visibility','hidden');});
}
const full=(key)=>DATA.full.map(row=>({x:row.time,y:row[key]}));
const holdout=(key)=>DATA.holdout.map(row=>({x:row.time,y:row[key]}));
drawChart('temperature',[{name:'Temperature',color:'#e78245',points:full('temperature')}],{shadeHoldout:true,formatY:value=>value.toFixed(1)+'°',formatTooltip:value=>value.toFixed(2)+' °C'});
drawChart('connected',[{name:'Connected count',color:'#a879e5',points:full('connected')}],{shadeHoldout:true,formatY:value=>(value/1e6).toFixed(2)+'M',formatTooltip:value=>numberFormat.format(value)});
drawChart('bare',[{name:'Bare count',color:'#61c4d4',points:full('bare')}],{shadeHoldout:true,formatY:value=>(value/1e6).toFixed(3)+'M',formatTooltip:value=>numberFormat.format(value)});
drawChart('moisture',[{name:'Reference-normalized',color:'#e1ad4d',points:full('referenceExcess')},{name:'Corrected',color:'#63d59b',points:full('correctedExcess')}],{shadeHoldout:true,formatY:value=>value.toFixed(1)+'%',formatTooltip:value=>value.toFixed(3)+'% above bare'});
drawChart('residual',[{name:'Raw connected',color:'#a879e5',points:holdout('raw')},{name:'Reference-normalized',color:'#e1ad4d',points:holdout('reference')},{name:'Corrected',color:'#63d59b',points:holdout('corrected')}],{shadeHoldout:false,formatY:value=>value.toFixed(2)+'%',formatTooltip:value=>(value>=0?'+':'')+value.toFixed(3)+'%'});
</script>
</body>
</html>`;
  const absolutePath = resolve(targetPath);
  mkdirSync(dirname(absolutePath), { recursive: true });
  writeFileSync(absolutePath, html, "utf8");
  process.stderr.write(`Wrote ${absolutePath}\n`);
}

const htmlTarget = argument("--html", null);
if (htmlTarget) renderHtmlArtifact(htmlTarget);
console.log(JSON.stringify(output, null, 2));
