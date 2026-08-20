import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
};

type PlantReading = {
  sensor?: number;
  wake?: number;
  moisture?: number;
  batteryPercent?: number;
  rssi?: number;
  ageMs?: number;
};

type Snapshot = {
  receiverId?: string;
  claimCode?: string;
  build?: string;
  config?: {
    version?: number;
  };
  plants?: PlantReading[];
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

type AutoCalRawSensor = {
  sensorId: number;
  sampleCount: number;
  oldestReadingAt: string | null;
  dryBound: number | null;
  wetBound: number | null;
  anchorDryBound: number | null;
  anchorWetBound: number | null;
  rises: { at: string; delta: number }[];
};

const AUTOCAL_MIN_SAMPLES_READY = 50;
const AUTOCAL_MIN_DAYS_READY = 3;
const AUTOCAL_CLUSTER_GAP_MS = 12 * 3600_000; // merge rises within 12h into one watering event
// Must match msv2_read_sensor_auto_calibration's p_lookback_days default (023) - the
// window the anchor/floor-clamp gate below waits to fill before engaging at all. Deliberately
// later than AUTOCAL_MIN_DAYS_READY's 3-day "trust the learned range for display" gate: the
// first full lookback window is meant to run completely unclamped so several real watering
// cycles get observed before anything about this sensor's range gets locked in.
const AUTOCAL_LOOKBACK_DAYS = 21;
// The learned dry bound is allowed to drop this fraction of the sensor's own *anchored*
// (wet - dry) range below its anchor before being held in place. A percentage of each
// sensor's own range, not a flat raw-unit number, since different probes/plants/soils sit
// on very different raw scales - a fixed margin that's reasonable for one probe would be
// meaningless (too tight or too loose) for another. Chronic under-watering otherwise drags
// the rolling 5th-percentile floor down indefinitely, silently reporting a plant as
// healthier than it is at the exact moisture level that used to read "critical". No
// absolute ground-truth dry value exists to clamp against instead (differs per
// plant/pot/soil), so each sensor is anchored to its own first full-window calibration.
// Provisional - no real multi-week drift data exists yet to tune this against.
const AUTOCAL_DRY_FLOOR_MARGIN_PCT = 0.10;

// Clusters raw candidate "rise" rows (from msv2_read_sensor_auto_calibration) into
// discrete watering events and derives days-since-last-watering - done here in
// TypeScript rather than in SQL since clustering a sorted list is much more naturally
// expressed as a simple loop than window-function chains. This is informational only
// (see docs/STATUS_HANDOFF.md's 2026-08-16 redesign note) - it no longer predicts an
// interval or forces any alert; that "overdue" behavior was removed this session because
// it cut against the product's actual value proposition (moisture-based alerting, not a
// calendar prediction).
function deriveAutoCal(sensor: AutoCalRawSensor, nowMs: number) {
  const events: number[] = [];
  let clusterStart: number | null = null;
  let lastAt: number | null = null;
  for (const rise of sensor.rises) {
    const at = new Date(rise.at).getTime();
    if (lastAt === null || at - lastAt > AUTOCAL_CLUSTER_GAP_MS) {
      if (clusterStart !== null) events.push(clusterStart);
      clusterStart = at;
    }
    lastAt = at;
  }
  if (clusterStart !== null) events.push(clusterStart);

  const oldestReadingAtMs = sensor.oldestReadingAt ? new Date(sensor.oldestReadingAt).getTime() : null;
  const ready = sensor.sampleCount >= AUTOCAL_MIN_SAMPLES_READY &&
    oldestReadingAtMs !== null &&
    (nowMs - oldestReadingAtMs) >= AUTOCAL_MIN_DAYS_READY * 24 * 3600_000;

  // lastWateredAtMs is the actual event timestamp, not just the rolling days-since
  // float below - a rolling 24h window can't tell "watered 10 hours ago last night" from
  // "watered 10 hours ago this morning", and only the client (which knows the viewer's
  // real local timezone; this Deno function does not) can turn a timestamp into a
  // correct calendar-day label ("today"/"yesterday"/"Nd ago"). See MobileAppPage.h's
  // wateredAgoText().
  let daysSinceLastWatering: number | null = null;
  let lastWateredAtMs: number | null = null;
  if (events.length > 0) {
    lastWateredAtMs = events[events.length - 1];
    daysSinceLastWatering = (nowMs - lastWateredAtMs) / (24 * 3600_000);
  }

  // Floor clamp (redesign item 6, revised twice): the first full lookback window
  // (AUTOCAL_LOOKBACK_DAYS) runs completely unclamped - multiple real watering cycles
  // need to happen before there's anything meaningful to anchor to. Once that window is
  // full, the learned dry bound can never sit more than AUTOCAL_DRY_FLOOR_MARGIN_PCT of
  // the sensor's own anchored range below its anchor. dryBound below is the actual value
  // the firmware/app use for percent/color - this doesn't just flag the drift, it stops
  // it from destroying the interval. dryBoundTrendingLower now means "the floor is
  // actively being held up" rather than a raw trend observation - same field
  // name/meaning-to-consumers (a "this plant's care has drifted" notice), computed
  // differently. needsAnchor tells the caller to persist the current dry/wet reading as
  // this sensor's anchor (first full window, or the first one after a reset wiped the old
  // anchor) - can't be done here since this function has no DB access.
  const fullWindowObserved = oldestReadingAtMs !== null &&
    (nowMs - oldestReadingAtMs) >= AUTOCAL_LOOKBACK_DAYS * 24 * 3600_000;

  let dryBound = sensor.dryBound;
  let dryBoundTrendingLower = false;
  let needsAnchor = false;
  if (ready && fullWindowObserved && sensor.dryBound !== null && sensor.wetBound !== null) {
    if (sensor.anchorDryBound === null || sensor.anchorWetBound === null) {
      needsAnchor = true;
    } else {
      const margin = AUTOCAL_DRY_FLOOR_MARGIN_PCT * (sensor.anchorWetBound - sensor.anchorDryBound);
      const floor = sensor.anchorDryBound - margin;
      if (sensor.dryBound < floor) {
        dryBound = Math.round(floor);
        dryBoundTrendingLower = true;
      }
    }
  }

  return {
    sensorId: sensor.sensorId,
    ready,
    sampleCount: sensor.sampleCount,
    dryBound,
    wetBound: sensor.wetBound,
    daysSinceLastWatering,
    lastWateredAtMs,
    dryBoundTrendingLower,
    needsAnchor,
    rawDryBoundForAnchor: sensor.dryBound,
    rawWetBoundForAnchor: sensor.wetBound,
  };
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
        const derived = rawSensors.map((sensor) => deriveAutoCal(sensor, nowMs));

        // Persist an anchor the first time a sensor's calibration window is full (or the
        // first time again after a manual reset cleared the old one) - best-effort, a
        // failure here just means the anchor gets (re)tried on the next ~10-minute poll
        // rather than failing this response, since the anchor is bookkeeping, not the
        // data the caller actually asked for.
        await Promise.all(
          derived
            .filter((sensor) => sensor.needsAnchor && sensor.rawDryBoundForAnchor !== null && sensor.rawWetBoundForAnchor !== null)
            .map((sensor) =>
              supabase.rpc("msv2_set_sensor_calibration_anchor_if_unset", {
                p_receiver_id: cleanReceiverId,
                p_sensor_id: sensor.sensorId,
                p_anchor_dry_bound: sensor.rawDryBoundForAnchor,
                p_anchor_wet_bound: sensor.rawWetBoundForAnchor,
              })
            ),
        );

        const sensors = derived.map(({ needsAnchor, rawDryBoundForAnchor, rawWetBoundForAnchor, ...sensor }) => sensor);
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
    const lastIp = forwardedFor.split(",")[0]?.trim() || null;
    const plantCount = Array.isArray(snapshot.plants) ? snapshot.plants.length : 0;
    const configVersion = typeof snapshot.config?.version === "number" ? snapshot.config.version : null;

    const { error: receiverError } = await supabase.rpc("msv2_touch_receiver", {
      p_receiver_id: receiverId,
      p_claim_code: snapshot.claimCode ?? null,
      p_build: snapshot.build ?? null,
      p_last_ip: lastIp,
    });
    if (receiverError) {
      return jsonResponse({ ok: false, error: receiverError.message }, 500);
    }

    const { error: snapshotError } = await supabase.rpc("msv2_store_latest_snapshot", {
      p_receiver_id: receiverId,
      p_snapshot: snapshot,
      p_config_version: configVersion,
      p_plant_count: plantCount,
    });
    if (snapshotError) {
      return jsonResponse({ ok: false, error: snapshotError.message }, 500);
    }

    // Bounded to well above the firmware's real 16-card cap (MobileAppPage.h's
    // MAX_PLANTS): an untrusted/buggy sender otherwise controls how many sequential
    // per-plant RPC round-trips this request makes, with no cap.
    const MAX_HISTORY_PLANTS_PER_SNAPSHOT = 32;
    if (Array.isArray(snapshot.plants)) {
      for (const plant of snapshot.plants.slice(0, MAX_HISTORY_PLANTS_PER_SNAPSHOT)) {
        const sensorId = Number(plant?.sensor);
        if (!Number.isFinite(sensorId) || sensorId <= 0) continue;

        const { error: historyError } = await supabase.rpc("msv2_store_sensor_reading", {
          p_receiver_id: receiverId,
          p_sensor_id: sensorId,
          p_wake: Number.isFinite(Number(plant?.wake)) ? Number(plant.wake) : null,
          p_moisture: Number.isFinite(Number(plant?.moisture)) ? Number(plant.moisture) : null,
          p_battery_percent: Number.isFinite(Number(plant?.batteryPercent)) ? Number(plant.batteryPercent) : null,
          p_rssi: Number.isFinite(Number(plant?.rssi)) ? Number(plant.rssi) : null,
          p_age_ms: Number.isFinite(Number(plant?.ageMs)) ? Number(plant.ageMs) : null,
        });
        if (historyError) {
          console.error(`sensor history insert failed for sensor ${sensorId}: ${historyError.message}`);
        }
      }
    }

    return jsonResponse({
      ok: true,
      acceptedConfigVersion: configVersion,
      plantCount,
      serverTimeMs: Date.now(),
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return jsonResponse({ ok: false, error: message }, 500);
  }
});
