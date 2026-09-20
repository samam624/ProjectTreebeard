// Validates the receiver-derived MSV3 temperature-compensation shadow channel. The raw probe
// values remain independently stored; this parser only decides whether the optional derived
// value is safe to cast into Postgres columns.

export const TEMPERATURE_COMPENSATED_LOG_RATIO_PPM_MAX = 655_340;
export const TEMPERATURE_COMPENSATION_MODEL_VERSION_MAX = 32_767;

type TemperatureCompensationEnvelope = {
  logRatioPpm?: unknown;
  modelVersion?: unknown;
};

export type TemperatureCompensationInput = {
  temperatureCompensatedLogRatioPpm?: unknown;
  temperatureCompensationModelVersion?: unknown;
  temperatureCompensation?: TemperatureCompensationEnvelope | null;
};

export type TemperatureCompensationFields = {
  temperatureCompensatedLogRatioPpm: number | null;
  temperatureCompensationModelVersion: number | null;
};

export function parseTemperatureCompensation(
  input: TemperatureCompensationInput | null | undefined,
): TemperatureCompensationFields {
  // Queued history rows use the flat fields. Legacy/latest-only snapshots use the nested
  // envelope. Prefer the flat form if both are present because it belongs to that immutable
  // observation rather than the receiver's current model state.
  // Deliberately NOT Number(). Number(false), Number("") and Number([]) are all 0, which is an
  // integer inside the accepted range, so a malformed payload would be stored as a real reading
  // of exactly zero log-ratio rather than rejected as absent. Only an actual JSON number can be
  // a measurement; anything else is a wire-format problem and must degrade to null.
  const asNumber = (value: unknown): number =>
    typeof value === "number" && Number.isFinite(value) ? value : Number.NaN;

  const ppmRaw = asNumber(
    input?.temperatureCompensatedLogRatioPpm ?? input?.temperatureCompensation?.logRatioPpm,
  );
  const versionRaw = asNumber(
    input?.temperatureCompensationModelVersion ?? input?.temperatureCompensation?.modelVersion,
  );
  const ppm = Number.isInteger(ppmRaw) && ppmRaw >= 0 && ppmRaw <= TEMPERATURE_COMPENSATED_LOG_RATIO_PPM_MAX
    ? ppmRaw
    : null;
  const modelVersion = ppm !== null && Number.isInteger(versionRaw) && versionRaw >= 1 &&
      versionRaw <= TEMPERATURE_COMPENSATION_MODEL_VERSION_MAX
    ? versionRaw
    : null;
  return {
    temperatureCompensatedLogRatioPpm: ppm,
    temperatureCompensationModelVersion: modelVersion,
  };
}
