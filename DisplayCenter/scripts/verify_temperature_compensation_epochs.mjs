// Verifies the History page's compensation-epoch reconstruction against the receiver's own
// real-time rule, by running BOTH algorithms over the same synthetic traces.
//
// WHY THIS EXISTS. The receiver decides an epoch boundary by comparing each observation with the
// one immediately before it, using per-sample thresholds (3% of the connected count, 0.35% of the
// bare reference) plus a probe restart. The browser cannot see that decision -- the epoch is never
// transported -- so it reconstructs it from the history rows it holds.
//
// Those rows are not necessarily consecutive observations. The server samples long ranges
// (msv2_read_share_sensor_history_sampled returns every K-th row, and the page reports the K it
// was given), and even an unsampled window can be missing rows. Applying a PER-SAMPLE threshold
// across a K-sample gap manufactures boundaries that never happened -- and because the
// reconstruction keeps only the LAST boundary and slices from it, one spurious step near the end
// of a window collapses the entire compensated chart to a handful of points.
//
// The fix is to prove adjacency from the `wake` counter -- the same counter the receiver uses --
// rather than assuming it. These tests pin that: the client must agree with the receiver at
// K=1, must not invent boundaries at K>1, and must still catch a probe restart, which the old
// step-only reconstruction could never see because the counts either side of a restart are
// nearly identical.
//
// Run:  node DisplayCenter/scripts/verify_temperature_compensation_epochs.mjs

import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const displayCenterRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const appPath = join(
  displayCenterRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "MobileAppPage.h");
// Normalised: the firmware sources are stored with CRLF endings, and every offset below is
// computed against "\n    }\n" style anchors.
const app = (await readFile(appPath, "utf8")).replace(/\r\n/g, "\n");

// Pull the REAL functions out of the page rather than restating them here. A test that carries
// its own copy of the logic passes forever while the shipped page drifts away from it.
function extract(name, kind = "function") {
  const opener = kind === "function"
    ? `    function ${name}(`
    : `    const ${name} =`;
  const start = app.indexOf(opener);
  assert.ok(start >= 0, `Could not find ${kind} ${name} in MobileAppPage.h`);
  if (kind === "const") {
    const end = app.indexOf("\n", start);
    return app.slice(start, end + 1);
  }
  const end = app.indexOf("\n    }\n", start);
  assert.ok(end > start, `Could not find the end of ${name}`);
  return app.slice(start, end + "\n    }\n".length);
}

const source = [
  extract("TEMPERATURE_COMPENSATION_MAX_CONNECTED_STEP_FRACTION", "const"),
  extract("TEMPERATURE_COMPENSATION_MAX_BARE_STEP_FRACTION", "const"),
  extract("WAKE_COUNTER_MODULUS", "const"),
  extract("msv3TemperatureCompensatedExcessPercent"),
  extract("consecutiveWakes"),
  extract("probeRestarted"),
  extract("latestTemperatureCompensationEpochRows"),
  "({ latestTemperatureCompensationEpochRows, consecutiveWakes, probeRestarted });",
].join("\n");

const page = vm.runInNewContext(source);

// ---------------------------------------------------------------- the oracle ----

// The receiver's rule, from updateTemperatureCompensation() in
// Treebeard_ESP32_Display_Runtime.ino: every observation, in order, against the one before it.
function receiverBoundaryCount(rows) {
  let count = 0;
  for (let i = 1; i < rows.length; i++) {
    const previous = rows[i - 1];
    const current = rows[i];
    if (current.wake < previous.wake) { count++; continue; }
    const connectedStep = Math.abs(current.raw_fdc_counts / previous.raw_fdc_counts - 1);
    const bareStep = Math.abs(current.bare_tank_fdc_counts / previous.bare_tank_fdc_counts - 1);
    if (connectedStep > 0.03 || bareStep > 0.0035) count++;
  }
  return count;
}

function makeTrace({ wakes = 600, wateringFrom = 560, perWakeStep = 1.008, restartAt = null }) {
  const rows = [];
  let connected = 19_690_000;
  let bare = 22_480_000;
  let wake = 1;
  for (let i = 0; i < wakes; i++) {
    if (i >= wateringFrom) connected *= perWakeStep;
    bare *= 1 + Math.sin(i / 40) * 0.00002;
    if (restartAt !== null && i === restartAt) wake = 1;
    rows.push({
      wake: wake++,
      raw_fdc_counts: connected,
      bare_tank_fdc_counts: bare,
      temperature_compensated_log_ratio_ppm: 120_000,
      temperature_compensation_model_version: 2,
    });
  }
  return rows;
}

const sampleEvery = (rows, k) => rows.filter((_, i) => i % k === 0);

// ------------------------------------------------------------------- tests ----

// 1. A gradual watering below the receiver's per-sample threshold. The receiver sees ONE epoch;
//    the client must not manufacture a boundary at any sampling factor.
{
  const rows = makeTrace({});
  assert.equal(receiverBoundaryCount(rows), 0,
    "fixture is wrong: the receiver should see no boundary in this trace");

  for (const k of [1, 2, 5, 10, 20]) {
    const sampled = sampleEvery(rows, k);
    const kept = page.latestTemperatureCompensationEpochRows(sampled);
    assert.equal(kept.length, sampled.length,
      `sampleEvery=${k}: the chart collapsed to ${kept.length} of ${sampled.length} points; a ` +
      `per-sample threshold was applied across a ${k}-sample gap`);
  }
}

// 2. A REAL discontinuity between genuinely consecutive wakes must still split.
{
  const rows = makeTrace({ wateringFrom: 600 });
  rows[400].raw_fdc_counts *= 1.20;           // an insertion/removal-sized step, not a watering
  for (let i = 401; i < rows.length; i++) rows[i].raw_fdc_counts *= 1.20;
  const kept = page.latestTemperatureCompensationEpochRows(rows);
  assert.equal(kept.length, rows.length - 400,
    "a 20% step between consecutive wakes must start a new epoch");
}

// 3. A probe restart must split even though the counts either side are nearly identical -- an
//    OTA, a watchdog reset, or a boot-guard DFU escape re-seeds the receiver's model.
{
  const rows = makeTrace({ wateringFrom: 600, restartAt: 300 });
  const kept = page.latestTemperatureCompensationEpochRows(rows);
  assert.equal(kept.length, rows.length - 300,
    "a probe restart re-seeds the receiver's model and must start a new epoch");
  assert.ok(page.probeRestarted({ wake: 5000 }, { wake: 1 }), "a wake rollback is a restart");
}

// 4. The 24-bit wake counter wraps at 2^24, ~37 years out at the advertise cadence. The client
//    must MIRROR the receiver here rather than improve on it: the receiver's rule is a plain
//    `wake < previous.wake` with no wrap exclusion, so it resets the epoch at a wrap, and the
//    chart has to agree with the model that produced the numbers it is plotting.
{
  assert.ok(page.consecutiveWakes({ wake: 16777215 }, { wake: 0 }),
    "a wake-counter wrap is arithmetically an adjacent pair");
  assert.ok(page.probeRestarted({ wake: 16777215 }, { wake: 0 }),
    "the receiver resets its epoch at a wrap, so the client must too");
  assert.equal(
    page.probeRestarted({ wake: 16777215 }, { wake: 0 }),
    receiverBoundaryCount([
      { wake: 16777215, raw_fdc_counts: 1, bare_tank_fdc_counts: 1 },
      { wake: 0, raw_fdc_counts: 1, bare_tank_fdc_counts: 1 },
    ]) === 1,
    "client and receiver must agree about what a wrap means");
}

// 5. Adjacency must be PROVEN, not assumed. Rows missing a usable wake cannot support a
//    per-sample step test, and an unsampled window can still have gaps.
{
  assert.ok(!page.consecutiveWakes({ wake: 10 }, { wake: 12 }), "a one-wake gap is not adjacent");
  assert.ok(!page.consecutiveWakes({}, { wake: 2 }), "a missing wake cannot prove adjacency");
  assert.ok(!page.consecutiveWakes({ wake: null }, { wake: 2 }), "a null wake cannot prove it");
  assert.ok(page.consecutiveWakes({ wake: 11 }, { wake: 12 }), "successive wakes are adjacent");
}

// 6. A model-version change is a boundary regardless of adjacency -- it is carried per row and
//    does not depend on comparing counts at all.
{
  const rows = makeTrace({ wateringFrom: 600 });
  for (let i = 500; i < rows.length; i++) rows[i].temperature_compensation_model_version = 3;
  const kept = page.latestTemperatureCompensationEpochRows(sampleEvery(rows, 10));
  assert.equal(kept.length, 10, "a model-version change must start a new epoch even when sampled");
}

console.log("PASS compensation-epoch reconstruction agrees with the receiver and survives sampling");
