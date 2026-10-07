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

// MSV4 probes (moisture scale 4) reuse these same columns for different quantities: raw_fdc_counts
// is soil resistance in 0.1 ohm, board_temp_quarter_c is the blade-tip NTC, tank_phase is 1 on the
// wake the probe measured and 0 on the wakes that repeat it. They have no tank at all, so the
// bare-tank column carries their fit diagnostics as one word: bits 0-15 Cm in 0.1 pF, bits 16-23
// Cp in 0.1 pF (0 = not sent), bits 24-31 the fit RMS residual in 1e-4 of drive, bits 32-47 Cd,
// the direct plate capacitance, in 0.01 pF (0 = not sent). The column is bigint, so 48 bits fit.
// Which of Cp and Cd a row has depends on the probe's firmware: application 262148/262150 send
// Cp, 262152 onward sends Cd. See MSV4/docs/MSV4_PRODUCTION_FIRMWARE_2026-10-06.md.
export const MSV4_MOISTURE_SCALE = 4;

export type Msv4DiagnosticsInput = {
  cmDeciPf?: unknown;
  cpDeciPf?: unknown;
  fitRmsE4?: unknown;
  dieTempQuarterC?: unknown;
  cdCentiPf?: unknown;
};

// Number(null) is 0, which would turn an explicit null into a plausible reading.
function integerOrNaN(value: unknown): number {
  return typeof value === "number" && Number.isInteger(value) ? value : NaN;
}

// Cm and the residual come from every fit and are both required: a word with one of them silently
// zeroed would read as a real measurement. Cp and Cd are each optional, and for each a stored
// zero already means "not sent".
export function packMsv4Diagnostics(msv4: Msv4DiagnosticsInput | null | undefined): number | null {
  const cm = integerOrNaN(msv4?.cmDeciPf);
  const rms = integerOrNaN(msv4?.fitRmsE4);
  if (!(cm >= 0 && cm <= 0xFFFF)) return null;
  if (!(rms >= 0 && rms <= 0xFF)) return null;
  const cpRaw = integerOrNaN(msv4?.cpDeciPf);
  const cp = cpRaw >= 1 && cpRaw <= 0xFF ? cpRaw : 0;
  // Arithmetic, not bit operators: JavaScript's are 32-bit signed and would turn a residual of
  // 0x80 or more into a negative number.
  const cdRaw = integerOrNaN(msv4?.cdCentiPf);
  const cd = cdRaw >= 1 && cdRaw <= 0xFFFF ? cdRaw : 0;
  return cm + cp * 0x10000 + rms * 0x1000000 + cd * 0x100000000;
}

// MSV4 packets from application version 262150 also carry the nRF52840 die temperature, in the
// same quarter-degree units and range as MSV3's. On a scale-4 row board_temp_quarter_c is already
// the tip thermistor, so the die temperature is stored in raw_moisture_khz, a column those rows
// otherwise always leave null.
export function parseMsv4DieTempQuarterC(msv4: Msv4DiagnosticsInput | null | undefined): number | null {
  const quarterC = integerOrNaN(msv4?.dieTempQuarterC);
  return quarterC >= MSV3_BOARD_TEMP_MIN_Q4C && quarterC <= MSV3_BOARD_TEMP_MAX_Q4C ? quarterC : null;
}

export type Msv3DiagnosticsInput = {
  boardTempQuarterC?: unknown;
  bareTankFdcCounts?: unknown;
  tankPhase?: unknown;
  msv4?: Msv4DiagnosticsInput | null;
};

export type Msv3Diagnostics = {
  boardTempQuarterC: number | null;
  bareTankFdcCounts: number | null;
  tankPhase: number | null;
};

// Each field is validated independently and normalized to either a valid integer or null - an
// invalid/absent value must never silently become 0 (0 is itself a plausible real reading for
// either field), and a bad value in one field must never invalidate a valid sibling field.
export function parseMsv3Diagnostics(
  raw: Msv3DiagnosticsInput | null | undefined,
  moistureScale?: unknown,
): Msv3Diagnostics {
  const tempRaw = Number(raw?.boardTempQuarterC);
  const boardTempQuarterC =
    Number.isInteger(tempRaw) && tempRaw >= MSV3_BOARD_TEMP_MIN_Q4C && tempRaw <= MSV3_BOARD_TEMP_MAX_Q4C
      ? tempRaw
      : null;

  // The scale decides which meaning the column has. An MSV4 row never carries a tank count, and
  // an FDC row must never have MSV4's packed word accepted past the 28-bit FDC bound.
  const tankRaw = Number(raw?.bareTankFdcCounts);
  const bareTankFdcCounts = Number(moistureScale) === MSV4_MOISTURE_SCALE
    ? packMsv4Diagnostics(raw?.msv4)
    : Number.isInteger(tankRaw) && tankRaw >= 0 && tankRaw <= MSV3_FDC_COUNT_MAX ? tankRaw : null;

  const phaseRaw = Number(raw?.tankPhase);
  const tankPhase = phaseRaw === 0 || phaseRaw === 1 ? phaseRaw : null;

  return { boardTempQuarterC, bareTankFdcCounts, tankPhase };
}
