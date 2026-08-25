// Validates and normalizes the optional MSV3 board-temperature/bare-tank-reference fields a
// probe's raw payload may carry (see PlantReading.raw in index.ts, and
// MSV3_Moisture_BLE_Sleep.ino's startAdv()/wire-format comment for where these originate).
// These are two independent raw observables, never a computed/compensated value - see
// docs/MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md for the formula reserved for
// later receiver-side analysis, which this module deliberately does not apply.

export const MSV3_BOARD_TEMP_MIN_Q4C = -160;
export const MSV3_BOARD_TEMP_MAX_Q4C = 340;
// FDC2214 DATA_CH0 is a 28-bit register on this board (see MSV3_Moisture_BLE_Sleep.ino's
// fdcReadChannel0() and its 0x0FFFFFFF mask) - the same real bound the moisture-connected raw
// reading is subject to, applied here for its bare-tank sibling too.
export const MSV3_FDC_COUNT_MAX = 2 ** 28 - 1;

export type Msv3DiagnosticsInput = {
  boardTempQuarterC?: unknown;
  bareTankFdcCounts?: unknown;
  tankPhase?: unknown;
};

export type Msv3Diagnostics = {
  boardTempQuarterC: number | null;
  bareTankFdcCounts: number | null;
  tankPhase: number | null;
};

// Each field is validated independently and normalized to either a valid integer or null - an
// invalid/absent value must never silently become 0 (0 is itself a plausible real reading for
// either field), and a bad value in one field must never invalidate a valid sibling field.
export function parseMsv3Diagnostics(raw: Msv3DiagnosticsInput | null | undefined): Msv3Diagnostics {
  const tempRaw = Number(raw?.boardTempQuarterC);
  const boardTempQuarterC =
    Number.isInteger(tempRaw) && tempRaw >= MSV3_BOARD_TEMP_MIN_Q4C && tempRaw <= MSV3_BOARD_TEMP_MAX_Q4C
      ? tempRaw
      : null;

  const tankRaw = Number(raw?.bareTankFdcCounts);
  const bareTankFdcCounts =
    Number.isInteger(tankRaw) && tankRaw >= 0 && tankRaw <= MSV3_FDC_COUNT_MAX ? tankRaw : null;

  const phaseRaw = Number(raw?.tankPhase);
  const tankPhase = phaseRaw === 0 || phaseRaw === 1 ? phaseRaw : null;

  return { boardTempQuarterC, bareTankFdcCounts, tankPhase };
}
