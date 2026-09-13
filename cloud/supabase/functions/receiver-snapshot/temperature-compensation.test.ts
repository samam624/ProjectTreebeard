import assert from "node:assert/strict";
import test from "node:test";

import {
  parseTemperatureCompensation,
  TEMPERATURE_COMPENSATED_LOG_RATIO_PPM_MAX,
} from "./temperature-compensation.ts";

test("accepts a valid queued flat temperature-compensation value", () => {
  assert.deepEqual(parseTemperatureCompensation({
    temperatureCompensatedLogRatioPpm: 237_420,
    temperatureCompensationModelVersion: 1,
  }), {
    temperatureCompensatedLogRatioPpm: 237_420,
    temperatureCompensationModelVersion: 1,
  });
});

test("accepts the nested latest-snapshot envelope", () => {
  assert.deepEqual(parseTemperatureCompensation({
    temperatureCompensation: { logRatioPpm: 240_000, modelVersion: 1 },
  }), {
    temperatureCompensatedLogRatioPpm: 240_000,
    temperatureCompensationModelVersion: 1,
  });
});

test("absent or invalid values degrade to null rather than zero", () => {
  assert.deepEqual(parseTemperatureCompensation(undefined), {
    temperatureCompensatedLogRatioPpm: null,
    temperatureCompensationModelVersion: null,
  });
  assert.equal(parseTemperatureCompensation({
    temperatureCompensatedLogRatioPpm: TEMPERATURE_COMPENSATED_LOG_RATIO_PPM_MAX + 1,
    temperatureCompensationModelVersion: 1,
  }).temperatureCompensatedLogRatioPpm, null);
  assert.equal(parseTemperatureCompensation({
    temperatureCompensatedLogRatioPpm: 1.5,
    temperatureCompensationModelVersion: 1,
  }).temperatureCompensatedLogRatioPpm, null);
});

test("a model version is never stored without a valid derived observation", () => {
  assert.deepEqual(parseTemperatureCompensation({
    temperatureCompensatedLogRatioPpm: "bad",
    temperatureCompensationModelVersion: 1,
  }), {
    temperatureCompensatedLogRatioPpm: null,
    temperatureCompensationModelVersion: null,
  });
});

test("flat immutable history fields win over a nested latest envelope", () => {
  assert.deepEqual(parseTemperatureCompensation({
    temperatureCompensatedLogRatioPpm: 230_000,
    temperatureCompensationModelVersion: 1,
    temperatureCompensation: { logRatioPpm: 250_000, modelVersion: 2 },
  }), {
    temperatureCompensatedLogRatioPpm: 230_000,
    temperatureCompensationModelVersion: 1,
  });
});

test("non-number JSON types are rejected rather than coerced to zero", () => {
  // Number(false), Number("") and Number([]) are all 0, which is an integer inside the accepted
  // range. Before 2026-09-09 each of these was therefore stored as a genuine reading of exactly
  // zero log-ratio with a valid model version beside it -- a fabricated measurement produced by
  // a malformed payload. Only a real JSON number is a measurement.
  for (const bogus of [false, true, "", [], {}, "500", null]) {
    assert.deepEqual(parseTemperatureCompensation({
      temperatureCompensatedLogRatioPpm: bogus,
      temperatureCompensationModelVersion: 2,
    }), {
      temperatureCompensatedLogRatioPpm: null,
      temperatureCompensationModelVersion: null,
    }, `expected ${JSON.stringify(bogus)} to be rejected`);
  }
});

test("non-finite numbers are rejected", () => {
  for (const bogus of [Number.NaN, Number.POSITIVE_INFINITY, Number.NEGATIVE_INFINITY]) {
    assert.equal(parseTemperatureCompensation({
      temperatureCompensatedLogRatioPpm: bogus,
      temperatureCompensationModelVersion: 2,
    }).temperatureCompensatedLogRatioPpm, null);
  }
});

test("the receiver's full encodable range is still accepted end to end", () => {
  // The receiver clamps correctedLogRatio to [0, 0.65534] and quantises to 10-ppm counts, so
  // these are the real extremes the wire can carry. Both must survive, or a legitimate reading
  // at the top of the range would silently vanish.
  for (const ppm of [0, 10, TEMPERATURE_COMPENSATED_LOG_RATIO_PPM_MAX]) {
    assert.deepEqual(parseTemperatureCompensation({
      temperatureCompensatedLogRatioPpm: ppm,
      temperatureCompensationModelVersion: 2,
    }), {
      temperatureCompensatedLogRatioPpm: ppm,
      temperatureCompensationModelVersion: 2,
    });
  }
});
