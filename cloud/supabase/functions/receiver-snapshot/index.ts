import { createClient } from "https://esm.sh/@supabase/supabase-js@2";
import { type AutoCalRawSensor, deriveAutoCal } from "./autocal.ts";
import {
  isValidInt32,
  isValidRawFdcCount,
  mergeLatestPlants,
  observedAtIso,
  selectHistoryRows,
  validateHistoryRowIdentity,
} from "./history-batch.ts";
import { parseMsv3Diagnostics } from "./msv3-diagnostics.ts";
import { parseTemperatureCompensation } from "./temperature-compensation.ts";

// Permissive on purpose - not a full RFC 791/4291 validator, just enough to keep obviously
// non-address content (a hostname, an empty segment, stray proxy annotations) from ever
// reaching a Postgres `inet` cast.
function looksLikeIpAddress(value: string): boolean {
  const ipv4Match = value.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/);
  if (ipv4Match) {
    return ipv4Match.slice(1).every((octet) => Number(octet) <= 255);
  }
  return value.includes(":") && /^[0-9a-fA-F:]+(%[\w.-]+)?$/.test(value);
}

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
};

type PlantReading = {
  sensor?: number;
  wake?: number;
  // Receiver-smoothed presentation value. Older receivers only send this field, so
  // ingestion falls back to it for both history series during a rolling deployment.
  moisture?: number;
  // Latest unsmoothed receiver-derived measurement. This remains the source for the
  // original history column and therefore for auto-calibration.
  measuredMoisture?: number;
  moistureScale?: number;
  raw?: {
    moistureKHz?: number;
    fdcCounts?: number;
    batteryMv?: number;
    charging?: boolean;
    // MSV3 software-only board-temperature/bare-tank-reference diagnostics (2026-08-25) - two
    // independent raw observables, not a computed/compensated moisture value. See
    // msv3-diagnostics.ts's parseMsv3Diagnostics() for validation and MSV3_Moisture_BLE_Sleep.ino
    // for where these originate.
    boardTempQuarterC?: number;
    bareTankFdcCounts?: number;
    caplessTank?: boolean;
    tankPhase?: number;
  };
  batteryPercent?: number;
  rssi?: number;
  ageMs?: number;
  observationId?: string;
  queuedAgeMs?: number;
  // Server-time-anchored timestamp from durable receiver outboxes. Unlike queuedAgeMs,
  // this remains meaningful after the ESP32 reboots while rows are waiting on flash.
  observedAtMs?: number;
  temperatureCompensatedLogRatioPpm?: number;
  temperatureCompensationModelVersion?: number;
  temperatureCompensation?: {
    logRatioPpm?: number;
    modelVersion?: number;
  };
};

type Snapshot = {
  receiverId?: string;
  claimCode?: string;
  build?: string;
  sentAtMs?: number;
  historyOnly?: boolean;
  config?: {
    version?: number;
  };
  plants?: PlantReading[];
  latestPlants?: PlantReading[];
  historyBatch?: PlantReading[];
};

type DisplaySettingsRow = {
  receiver_id: string;
  brightness_percent: number;
  screen_on: boolean;
  settings_version: number;
  updated_at: string;
  updated_by: string;
};

function displaySettingsJson(row: DisplaySettingsRow | null | undefined) {
  if (!row) return null;
  return {
    receiverId: row.receiver_id,
    brightnessPercent: row.brightness_percent,
    screenOn: row.screen_on,
    settingsVersion: row.settings_version,
    updatedAt: row.updated_at,
    updatedBy: row.updated_by,
  };
}

async function verifyReceiverToken(
  supabase: any,
  receiverId: string,
  providedToken: string,
): Promise<boolean> {
  if (!providedToken || !receiverId) return false;
  const { data: verified, error: verifyError } = await supabase.rpc("msv2_verify_receiver_api_token", {
    p_receiver_id: receiverId,
    p_api_token: providedToken,
  });
  return !verifyError && verified === true;
}

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...corsHeaders,
      "Content-Type": "application/json",
    },
  });
}

function bearerToken(request: Request): string {
  const header = request.headers.get("authorization") ?? "";
  const prefix = "Bearer ";
  return header.startsWith(prefix) ? header.slice(prefix.length).trim() : "";
}

function requireEnv(name: string, fallbackName = ""): string {
  const value = Deno.env.get(name) || (fallbackName ? Deno.env.get(fallbackName) : "");
  if (!value) {
    throw new Error(fallbackName ? `${name} or ${fallbackName} is not configured` : `${name} is not configured`);
  }
  return value;
}

Deno.serve(async (request) => {
  if (request.method === "OPTIONS") {
    return new Response(null, {
      status: 204,
      headers: corsHeaders,
    });
  }

  try {
    const supabaseUrl = requireEnv("SUPABASE_URL", "URL");
    const serviceRoleKey = requireEnv("SUPABASE_SERVICE_ROLE_KEY", "SERVICE_ROLE_KEY");
    const supabase = createClient(supabaseUrl, serviceRoleKey, {
      auth: {
        persistSession: false,
      },
    });

    const providedToken = bearerToken(request);
    const requestUrl = new URL(request.url);

    if (request.method === "GET") {
      const url = requestUrl;
      const receiverId = url.searchParams.get("receiverId");
      const wantsConfig = url.searchParams.get("config") === "1";
      const wantsDisplay = url.searchParams.get("display") === "1";
      const wantsHistory = url.searchParams.get("history") === "1";
      const wantsAutoCal = url.searchParams.get("autocal") === "1";

      const cleanReceiverId = String(receiverId ?? "").trim();
      if (!cleanReceiverId) {
        return jsonResponse({ ok: false, error: "receiverId required" }, 400);
      }
      if (!(await verifyReceiverToken(supabase, cleanReceiverId, providedToken))) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      if (wantsDisplay) {
        const currentVersion = Number(url.searchParams.get("currentVersion") ?? "0");
        const { data, error } = await supabase.rpc("msv2_read_receiver_pending_display_settings", {
          p_receiver_id: cleanReceiverId,
          p_current_version: Number.isFinite(currentVersion) ? currentVersion : 0,
        });
        if (error) {
          return jsonResponse({ ok: false, error: error.message }, 500);
        }
        const row = (Array.isArray(data) ? data[0] : data) as DisplaySettingsRow | null;
        return jsonResponse({ ok: true, displaySettingsApi: 1, settings: displaySettingsJson(row) });
      }

      if (wantsHistory) {
        const sensorId = Number(url.searchParams.get("sensorId") ?? "0");
        if (!Number.isFinite(sensorId) || sensorId <= 0) {
          return jsonResponse({ ok: false, error: "sensorId required" }, 400);
        }
        // Clamped before the Date math below - an unbounded sinceHours can push
        // Date.now() - sinceHours*3600_000 outside the valid Date range, throwing an
        // uncaught RangeError. 2160h (90 days) comfortably covers this endpoint's
        // widest real caller (MobileAppPage.h's History tab, 7-day range).
        const rawSinceHours = Number(url.searchParams.get("sinceHours") ?? "24");
        const sinceHours = Number.isFinite(rawSinceHours) ? Math.min(Math.max(rawSinceHours, 1), 2160) : 24;
        const since = new Date(Date.now() - sinceHours * 3600_000).toISOString();
        const rawLimit = Number(url.searchParams.get("limit") ?? "150");
        const limit = Number.isFinite(rawLimit) ? Math.min(Math.max(rawLimit, 1), 2000) : 150;
        const rawAfterId = Number(url.searchParams.get("afterId") ?? "0");
        const afterId = Number.isFinite(rawAfterId) ? Math.max(rawAfterId, 0) : 0;

        const { data, error } = await supabase.rpc("msv2_read_sensor_history", {
          p_receiver_id: cleanReceiverId,
          p_sensor_id: sensorId,
          p_since: since,
          p_after_id: afterId,
          p_limit: limit,
        });
        if (error) {
          return jsonResponse({ ok: false, error: error.message }, 500);
        }

        return jsonResponse({ ok: true, readings: data ?? [] });
      }

      if (wantsAutoCal) {
        const { data, error } = await supabase.rpc("msv2_read_sensor_auto_calibration", {
          p_receiver_id: cleanReceiverId,
        });
        if (error) {
          return jsonResponse({ ok: false, error: error.message }, 500);
        }

        const nowMs = Date.now();
        const rawSensors = (data ?? []) as AutoCalRawSensor[];
        const sensors = rawSensors.map((sensor) => deriveAutoCal(sensor, nowMs));
        return jsonResponse({ ok: true, sensors });
      }

      if (wantsConfig) {
        const currentVersion = Number(url.searchParams.get("currentVersion") ?? "0");
        const { data, error } = await supabase.rpc("msv2_read_receiver_pending_config", {
          p_receiver_id: cleanReceiverId,
          p_current_version: Number.isFinite(currentVersion) ? currentVersion : 0,
        });
        if (error) {
          return jsonResponse({ ok: false, error: error.message }, 500);
        }
        const config = Array.isArray(data) ? data[0] : data;
        return jsonResponse({ ok: true, config: config ?? null });
      }

      const { data, error } = await supabase.rpc("msv2_read_latest_snapshots", {
        p_receiver_id: cleanReceiverId,
        p_limit: 1,
      });
      if (error) {
        return jsonResponse({ ok: false, error: error.message }, 500);
      }

      return jsonResponse({
        ok: true,
        snapshots: data ?? [],
      });
    }

    if (request.method !== "POST") {
      return jsonResponse({ ok: false, error: "method not allowed" }, 405);
    }

    const snapshot = await request.json() as Snapshot & {
      brightnessPercent?: number;
      screenOn?: boolean;
      baseVersion?: number;
    };
    const receiverId = String(snapshot.receiverId ?? "").trim();
    if (!receiverId) {
      return jsonResponse({ ok: false, error: "receiverId required" }, 400);
    }

    if (!(await verifyReceiverToken(supabase, receiverId, providedToken))) {
      return jsonResponse({ ok: false, error: "unauthorized" }, 401);
    }

    if (requestUrl.searchParams.get("display") === "1") {
      const brightnessPercent = Number(snapshot.brightnessPercent);
      if (!Number.isInteger(brightnessPercent) || brightnessPercent < 1 || brightnessPercent > 100 ||
          typeof snapshot.screenOn !== "boolean") {
        return jsonResponse({ ok: false, error: "brightnessPercent 1-100 and screenOn are required" }, 400);
      }
      const { data, error } = await supabase.rpc("msv2_upsert_receiver_display_settings", {
        p_receiver_id: receiverId,
        p_brightness_percent: brightnessPercent,
        p_screen_on: snapshot.screenOn,
        p_updated_by: "receiver-touch",
      });
      if (error) {
        return jsonResponse({ ok: false, error: error.message }, 400);
      }
      const row = (Array.isArray(data) ? data[0] : data) as DisplaySettingsRow | null;
      return jsonResponse({ ok: true, displaySettingsApi: 1, settings: displaySettingsJson(row) });
    }

    const forwardedFor = request.headers.get("x-forwarded-for") ?? "";
    const forwardedForCandidate = forwardedFor.split(",")[0]?.trim() || null;
    // x-forwarded-for is attacker/proxy-controlled and goes straight into msv2_touch_receiver's
    // `p_last_ip inet` parameter - a value that doesn't parse as an address throws there and
    // 500s the WHOLE snapshot POST (moisture/history included), not just the IP-logging update.
    // Drop anything that doesn't look like an address instead of passing it through unchecked.
    const lastIp = forwardedForCandidate && looksLikeIpAddress(forwardedForCandidate) ? forwardedForCandidate : null;
    let plantCount = Array.isArray(snapshot.plants) ? snapshot.plants.length : 0;
    let configVersion = typeof snapshot.config?.version === "number" ? snapshot.config.version : null;

    const { error: receiverError } = await supabase.rpc("msv2_touch_receiver", {
      p_receiver_id: receiverId,
      p_claim_code: snapshot.claimCode ?? null,
      p_build: snapshot.build ?? null,
      p_last_ip: lastIp,
    });
    if (receiverError) {
      return jsonResponse({ ok: false, error: receiverError.message }, 500);
    }

    // A compact history-only upload avoids the ESP32's 18KB full-snapshot JSON document
    // while a backlog is draining. Merge its tiny latestPlants cache into the previous
    // latest snapshot so the dashboard remains current without overwriting config/display
    // metadata. The read-before-write is safe here: one authenticated receiver is the only
    // writer for its own receiver id, and history idempotency is handled separately below.
    let snapshotForLatest: Record<string, unknown>;
    if (snapshot.historyOnly === true) {
      const { data: latestRows, error: latestError } = await supabase.rpc("msv2_read_latest_snapshots", {
        p_receiver_id: receiverId,
        p_limit: 1,
      });
      if (latestError) {
        return jsonResponse({ ok: false, error: latestError.message }, 500);
      }
      const latestRow = Array.isArray(latestRows) ? latestRows[0] : null;
      const existingSnapshot = latestRow?.snapshot && typeof latestRow.snapshot === "object"
        ? latestRow.snapshot as Record<string, unknown>
        : {};
      const mergedPlants = mergeLatestPlants(
        existingSnapshot.plants as PlantReading[] | undefined,
        snapshot.latestPlants,
      );
      snapshotForLatest = {
        ...existingSnapshot,
        receiverId,
        build: snapshot.build ?? existingSnapshot.build ?? null,
        sentAtMs: snapshot.sentAtMs ?? existingSnapshot.sentAtMs ?? null,
        plants: mergedPlants,
      };
      configVersion = Number.isInteger(Number(latestRow?.config_version))
        ? Number(latestRow.config_version)
        : typeof (existingSnapshot.config as { version?: unknown } | undefined)?.version === "number"
        ? Number((existingSnapshot.config as { version: number }).version)
        : null;
      plantCount = mergedPlants.length;
    } else {
      snapshotForLatest = { ...snapshot };
    }
    // Queue/transport fields are not current receiver state and must never bloat the
    // upsert-only latest snapshot.
    delete snapshotForLatest.historyBatch;
    delete snapshotForLatest.historyOnly;
    delete snapshotForLatest.latestPlants;
    const { error: snapshotError } = await supabase.rpc("msv2_store_latest_snapshot", {
      p_receiver_id: receiverId,
      p_snapshot: snapshotForLatest,
      p_config_version: configVersion,
      p_plant_count: plantCount,
    });
    if (snapshotError) {
      return jsonResponse({ ok: false, error: snapshotError.message }, 500);
    }

    // Batch-capable receivers send every unique BLE wake accumulated since the last
    // HTTPS sync. An explicit empty historyBatch is meaningful: do not fall back to the
    // latest plants and create duplicate/stale history rows. Receivers without the field
    // retain the old one-row-per-latest-plant behavior during rolling deployment.
    const historySelection = selectHistoryRows(snapshot);
    const historyServerNowMs = Date.now();
    let historyAccepted = 0;
    if (historySelection.usesBatch && historySelection.rows.length > 0) {
      // A malformed row must not block every OTHER row in the same batch. The receiver's
      // ack model (acknowledgeHistoryUploadBatch()) removes a contiguous PREFIX of its
      // queue sized by the historyAccepted count we return - so as long as we account for
      // every row in the original batch (either stored, or deliberately dropped here as
      // unrecoverably malformed), it's safe to report the full original row count and let
      // the receiver advance past all of them. Retrying a malformed row would never
      // succeed anyway (it's a wire-format problem, not a transient one), so dropping it
      // is strictly better than the previous behavior of rejecting the whole batch with a
      // 400 and retrying the same poisoned prefix forever.
      let malformedDropped = 0;
      const readings = [];
      for (const plant of historySelection.rows) {
        const identity = validateHistoryRowIdentity(plant);
        if (!identity) {
          malformedDropped++;
          console.error(
            `dropping malformed history row for receiver ${receiverId}: ` +
              `sensor=${plant?.sensor} observationId=${plant?.observationId}`,
          );
          continue;
        }
        const { sensorId, observationId } = identity;

        const displayMoisture = Number(plant?.moisture);
        const measuredMoisture = Number(plant?.measuredMoisture);
        const moistureScale = Number(plant?.moistureScale);
        // Every field below except rawCharging/observationId/receivedAt maps to an `integer`
        // or `bigint` Postgres column that msv2_store_sensor_reading_batch casts with no
        // per-row exception handler - a value that fails isValidInt32/isValidRawFdcCount would
        // throw there and roll back the ENTIRE batch, not just this row (see history-batch.ts's
        // comment on isValidInt32). Null out anything that doesn't validate instead of ever
        // handing it to the RPC - this row is still stored, just without that one field, same
        // as the existing degrade-to-null behavior these checks already had.
        readings.push({
          sensorId,
          wake: isValidInt32(Number(plant?.wake)) ? Number(plant.wake) : null,
          moisture: isValidInt32(measuredMoisture)
            ? measuredMoisture
            : isValidInt32(displayMoisture) ? displayMoisture : null,
          displayMoisture: isValidInt32(displayMoisture) ? displayMoisture : null,
          moistureScale: isValidInt32(moistureScale) && moistureScale > 0 ? moistureScale : 1,
          rawMoistureKHz: isValidInt32(Number(plant?.raw?.moistureKHz))
            ? Number(plant.raw.moistureKHz)
            : null,
          rawFdcCounts: isValidRawFdcCount(Number(plant?.raw?.fdcCounts))
            ? Number(plant.raw.fdcCounts)
            : null,
          batteryPercent: isValidInt32(Number(plant?.batteryPercent)) ? Number(plant.batteryPercent) : null,
          // Raw battery voltage/charging alongside the receiver's own smoothed percent, so
          // the History chart can re-derive/re-tune battery presentation from the original
          // signal instead of only ever seeing what the ingest-time filter already baked in.
          rawBatteryMv: isValidInt32(Number(plant?.raw?.batteryMv)) ? Number(plant.raw.batteryMv) : null,
          rawCharging: typeof plant?.raw?.charging === "boolean" ? plant.raw.charging : null,
          rssi: isValidInt32(Number(plant?.rssi)) ? Number(plant.rssi) : null,
          ageMs: isValidInt32(Number(plant?.ageMs)) ? Number(plant.ageMs) : null,
          observationId,
          receivedAt: observedAtIso(historyServerNowMs, plant?.queuedAgeMs, plant?.observedAtMs),
          ...parseMsv3Diagnostics(plant?.raw),
          ...parseTemperatureCompensation(plant),
        });
      }

      let stored = 0;
      if (readings.length > 0) {
        const { data, error: historyError } = await supabase.rpc("msv2_store_sensor_reading_batch", {
          p_receiver_id: receiverId,
          p_readings: readings,
        });
        if (historyError) {
          console.error(`sensor history batch insert failed: ${historyError.message}`);
          // A DB/RPC-level failure is a different, likely-transient problem from a
          // malformed row - fail the whole request so the receiver retries the valid
          // rows too, rather than reporting a partial success we can't actually stand
          // behind. malformedDropped rows already logged above are not recoverable by a
          // retry either way.
          return jsonResponse({ ok: false, error: historyError.message, historyAccepted }, 500);
        }
        stored = Number(data);
        if (!Number.isInteger(stored) || stored !== readings.length) {
          return jsonResponse({ ok: false, error: "history batch acknowledgement mismatch", historyAccepted }, 500);
        }
      }
      // Every row in the original batch is now accounted for: stored, or dropped as
      // unrecoverably malformed. Reporting the full prefix count lets the receiver
      // advance past the malformed rows too instead of retrying them forever.
      historyAccepted = stored + malformedDropped;
    } else if (!historySelection.usesBatch && historySelection.rows.length > 0) {
      // THIS BRANCH MUST RETURN A TRUE PREFIX COUNT, exactly like the batch branch above.
      //
      // acknowledgeHistoryUploadBatch() on the receiver removes a contiguous PREFIX of its queue
      // sized by historyAccepted -- it walks the journal from the head, matching bootId/sequence
      // as it goes. The batch branch reasons about that explicitly and counts malformedDropped so
      // its total still describes a prefix. This branch used to `continue` past a skipped row
      // while NOT incrementing, which makes the returned number describe a non-prefix success set:
      // skip row 3 of 10, return 9, and the receiver drops rows 1-9 -- discarding the row that
      // actually failed and re-sending a tail row that had already succeeded.
      //
      // So: an unrecoverable row is COUNTED (retrying a wire-format problem never succeeds, and
      // the batch branch drops those too), while a transient RPC failure STOPS the count where it
      // is, leaving that row and everything after it queued for the next attempt.
      let fellBackToRetry = false;
      for (const plant of historySelection.rows) {
        const sensorId = Number(plant?.sensor);
        if (!Number.isFinite(sensorId) || sensorId <= 0) {
          console.error(
            `dropping malformed history row for receiver ${receiverId}: sensor=${plant?.sensor}`,
          );
          historyAccepted++;
          continue;
        }

        const displayMoisture = Number(plant?.moisture);
        const measuredMoisture = Number(plant?.measuredMoisture);
        const msv3Diagnostics = parseMsv3Diagnostics(plant?.raw);
        const temperatureCompensation = parseTemperatureCompensation(plant);

        const { error: historyError } = await supabase.rpc("msv2_store_sensor_reading", {
          p_receiver_id: receiverId,
          p_sensor_id: sensorId,
          p_wake: Number.isFinite(Number(plant?.wake)) ? Number(plant.wake) : null,
          // Keep `moisture` scientifically honest/backward compatible: it is the real
          // unsmoothed derived reading and remains the auto-calibration input. Old
          // receiver firmware has no measuredMoisture, so fall back without data loss.
          p_moisture: Number.isFinite(measuredMoisture)
            ? measuredMoisture
            : Number.isFinite(displayMoisture) ? displayMoisture : null,
          p_display_moisture: Number.isFinite(displayMoisture) ? displayMoisture : null,
          p_moisture_scale: Number.isInteger(Number(plant?.moistureScale)) && Number(plant?.moistureScale) > 0
            ? Number(plant.moistureScale)
            : 1,
          p_raw_moisture_khz: Number.isFinite(Number(plant?.raw?.moistureKHz))
            ? Number(plant.raw.moistureKHz)
            : null,
          p_raw_fdc_counts: Number.isFinite(Number(plant?.raw?.fdcCounts))
            ? Number(plant.raw.fdcCounts)
            : null,
          p_battery_percent: Number.isFinite(Number(plant?.batteryPercent)) ? Number(plant.batteryPercent) : null,
          p_rssi: Number.isFinite(Number(plant?.rssi)) ? Number(plant.rssi) : null,
          p_age_ms: Number.isFinite(Number(plant?.ageMs)) ? Number(plant.ageMs) : null,
          p_observation_id: null,
          p_received_at: null,
          p_raw_battery_mv: Number.isFinite(Number(plant?.raw?.batteryMv)) ? Number(plant.raw.batteryMv) : null,
          p_raw_charging: typeof plant?.raw?.charging === "boolean" ? plant.raw.charging : null,
          p_board_temp_quarter_c: msv3Diagnostics.boardTempQuarterC,
          p_bare_tank_fdc_counts: msv3Diagnostics.bareTankFdcCounts,
          p_tank_phase: msv3Diagnostics.tankPhase,
          p_temperature_compensated_log_ratio_ppm:
            temperatureCompensation.temperatureCompensatedLogRatioPpm,
          p_temperature_compensation_model_version:
            temperatureCompensation.temperatureCompensationModelVersion,
        });
        if (historyError) {
          console.error(`sensor history insert failed for sensor ${sensorId}: ${historyError.message}`);
          // Stop, do not skip. Everything from here on stays queued and is retried, which is the
          // only behaviour that keeps historyAccepted a prefix the receiver can safely act on.
          fellBackToRetry = true;
          break;
        }
        historyAccepted++;
      }
      if (fellBackToRetry) {
        console.error(
          `history fallback stopped early for receiver ${receiverId}: accepted ${historyAccepted} ` +
            `of ${historySelection.rows.length}; the remainder stays queued for retry`,
        );
      }
    }

    return jsonResponse({
      ok: true,
      acceptedConfigVersion: configVersion,
      plantCount,
      historyAccepted,
      serverTimeMs: Date.now(),
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return jsonResponse({ ok: false, error: message }, 500);
  }
});
