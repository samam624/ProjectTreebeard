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
  rises: { at: string; delta: number }[];
};

const AUTOCAL_MIN_SAMPLES_READY = 50;
const AUTOCAL_MIN_DAYS_READY = 3;
const AUTOCAL_CLUSTER_GAP_MS = 12 * 3600_000; // merge rises within 12h into one watering event
const AUTOCAL_MAX_EVENTS_FOR_INTERVAL = 7; // up to 6 gaps from the last 7 events

// Clusters raw candidate "rise" rows (from msv2_read_sensor_auto_calibration) into
// discrete watering events, then derives a median watering interval and whether this
// sensor is overdue - done here in TypeScript rather than in SQL since clustering a
// sorted list is much more naturally expressed as a simple loop than window-function
// chains.
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

  let daysSinceLastWatering: number | null = null;
  if (events.length > 0) {
    daysSinceLastWatering = (nowMs - events[events.length - 1]) / (24 * 3600_000);
  }

  let predictedIntervalDays: number | null = null;
  if (events.length >= 3) {
    const recentEvents = events.slice(-AUTOCAL_MAX_EVENTS_FOR_INTERVAL);
    const gaps: number[] = [];
    for (let i = 1; i < recentEvents.length; i++) {
      gaps.push((recentEvents[i] - recentEvents[i - 1]) / (24 * 3600_000));
    }
    gaps.sort((a, b) => a - b);
    predictedIntervalDays = gaps[Math.floor(gaps.length / 2)];
  }

  const overdue = predictedIntervalDays !== null && daysSinceLastWatering !== null &&
    daysSinceLastWatering > predictedIntervalDays;

  return {
    sensorId: sensor.sensorId,
    ready,
    sampleCount: sensor.sampleCount,
    dryBound: sensor.dryBound,
    wetBound: sensor.wetBound,
    daysSinceLastWatering,
    predictedIntervalDays,
    overdue,
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
        const sinceHours = Number(url.searchParams.get("sinceHours") ?? "24");
        const since = new Date(Date.now() - (Number.isFinite(sinceHours) ? sinceHours : 24) * 3600_000).toISOString();
        const limit = Number(url.searchParams.get("limit") ?? "150");
        const afterId = Number(url.searchParams.get("afterId") ?? "0");

        const { data, error } = await supabase.rpc("msv2_read_sensor_history", {
          p_receiver_id: cleanReceiverId,
          p_sensor_id: sensorId,
          p_since: since,
          p_after_id: Number.isFinite(afterId) ? afterId : 0,
          p_limit: Number.isFinite(limit) ? limit : 300,
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

    if (Array.isArray(snapshot.plants)) {
      for (const plant of snapshot.plants) {
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
