import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
};

type SnapshotRow = {
  receiver_id: string;
  snapshot: any;
  config_version: number | null;
  plant_count: number;
  received_at: string;
  household_id?: string;
  household_name?: string;
};

function response(body: string, contentType: string, status = 200): Response {
  return new Response(body, {
    status,
    headers: {
      ...corsHeaders,
      "Cache-Control": "no-store",
      "Content-Type": contentType,
    },
  });
}

function jsonResponse(body: unknown, status = 200): Response {
  return response(JSON.stringify(body), "application/json", status);
}

function requireEnv(name: string, fallbackName = ""): string {
  const value = Deno.env.get(name) || (fallbackName ? Deno.env.get(fallbackName) : "");
  if (!value) {
    throw new Error(fallbackName ? `${name} or ${fallbackName} is not configured` : `${name} is not configured`);
  }
  return value;
}

function bearerToken(request: Request): string {
  const header = request.headers.get("authorization") ?? "";
  const prefix = "Bearer ";
  return header.startsWith(prefix) ? header.slice(prefix.length).trim() : "";
}

// A malformed body previously escaped to the top-level catch, which returns an HTML
// error page - breaking every JSON-consuming client and contradicting this function's
// own "API-only JSON" contract (see the root route's note). Each POST handler below
// uses this instead of a bare `await request.json()` so bad input gets a clean 400.
class BadJsonError extends Error {}

async function parseJsonBody(request: Request): Promise<any> {
  try {
    return await request.json();
  } catch {
    throw new BadJsonError("invalid JSON body");
  }
}

function serviceClient() {
  return createClient(
    requireEnv("SUPABASE_URL", "URL"),
    requireEnv("SUPABASE_SERVICE_ROLE_KEY", "SERVICE_ROLE_KEY"),
    { auth: { persistSession: false } },
  );
}

Deno.serve(async (request) => {
  if (request.method === "OPTIONS") {
    return new Response(null, { status: 204, headers: corsHeaders });
  }

  try {
    const url = new URL(request.url);
    const path = url.pathname;

    if (path.endsWith("/manifest.webmanifest")) return jsonResponse(webManifest());
    if (path.endsWith("/app-icon.svg")) return response(appIconSvg(), "image/svg+xml");
    if (path.endsWith("/api/share-claim")) return await shareClaimApi(request);
    if (path.endsWith("/api/share-revoke")) return await shareRevokeApi(request);
    if (path.endsWith("/api/share-snapshots")) return await shareSnapshotsApi(request);
    if (path.endsWith("/api/share-config")) return await shareConfigApi(request);
    if (path.endsWith("/api/share-history")) return await shareHistoryApi(request);
    if (path.endsWith("/api/share-history-clear")) return await shareHistoryClearApi(request);
    if (path.endsWith("/api/snapshots")) return await userSnapshotsApi(request);

    return jsonResponse({
      ok: true,
      role: "MSV2 plant-dashboard API",
      note: "Supabase Edge Functions serve browser HTML as text/plain in this project. Host web/plant-dashboard/index.html as the static app and keep this function for APIs.",
      endpoints: {
        shareClaim: "/functions/v1/plant-dashboard/api/share-claim",
        shareRevoke: "/functions/v1/plant-dashboard/api/share-revoke",
        shareSnapshots: "/functions/v1/plant-dashboard/api/share-snapshots",
        shareConfig: "/functions/v1/plant-dashboard/api/share-config",
        shareHistory: "/functions/v1/plant-dashboard/api/share-history",
        shareHistoryClear: "/functions/v1/plant-dashboard/api/share-history-clear",
      },
    });
  } catch (error) {
    if (error instanceof BadJsonError) {
      return jsonResponse({ ok: false, error: error.message }, 400);
    }
    const message = error instanceof Error ? error.message : String(error);
    return response(`<!doctype html><title>MSV2 Error</title><pre>${escapeHtml(message)}</pre>`, "text/html; charset=utf-8", 500);
  }
});

async function shareClaimApi(request: Request): Promise<Response> {
  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "method not allowed" }, 405);
  }

  const body = await parseJsonBody(request);
  const receiverId = String(body.receiverId ?? "").trim();
  const claimCode = String(body.claimCode ?? "").trim();
  const householdName = String(body.householdName ?? "My Plants").trim() || "My Plants";
  if (!receiverId || !claimCode) {
    return jsonResponse({ ok: false, error: "receiverId and claimCode required" }, 400);
  }

  const { data, error } = await serviceClient().rpc("msv2_issue_household_share", {
    p_receiver_id: receiverId,
    p_claim_code: claimCode,
    p_household_name: householdName,
  });
  if (error) {
    return jsonResponse({ ok: false, error: error.message }, 400);
  }

  const share = Array.isArray(data) ? data[0] : data;
  return jsonResponse({ ok: true, share });
}

async function shareRevokeApi(request: Request): Promise<Response> {
  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "method not allowed" }, 405);
  }

  const shareToken = bearerToken(request);
  if (!shareToken) {
    return jsonResponse({ ok: false, error: "share token required" }, 401);
  }

  const { error } = await serviceClient().rpc("msv2_revoke_household_share", {
    p_share_token: shareToken,
  });
  if (error) {
    return jsonResponse({ ok: false, error: error.message }, 400);
  }

  return jsonResponse({ ok: true });
}

async function shareSnapshotsApi(request: Request): Promise<Response> {
  const shareToken = bearerToken(request);
  if (!shareToken) {
    return jsonResponse({ ok: false, error: "share token required" }, 401);
  }

  const { data, error } = await serviceClient().rpc("msv2_read_share_latest_snapshots", {
    p_share_token: shareToken,
  });
  if (error) {
    return jsonResponse({ ok: false, error: error.message }, 401);
  }

  return jsonResponse({ ok: true, snapshots: (data ?? []) as SnapshotRow[] });
}

async function shareConfigApi(request: Request): Promise<Response> {
  const shareToken = bearerToken(request);
  if (!shareToken) {
    return jsonResponse({ ok: false, error: "share token required" }, 401);
  }

  if (request.method === "GET") {
    const { data, error } = await serviceClient().rpc("msv2_read_share_receiver_configs", {
      p_share_token: shareToken,
    });
    if (error) {
      return jsonResponse({ ok: false, error: error.message }, 401);
    }
    return jsonResponse({ ok: true, configs: data ?? [] });
  }

  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "method not allowed" }, 405);
  }

  const body = await parseJsonBody(request);
  const receiverId = String(body.receiverId ?? "").trim();
  const config = body.config;
  if (!receiverId || !config || typeof config !== "object") {
    return jsonResponse({ ok: false, error: "receiverId and config required" }, 400);
  }

  const { data, error } = await serviceClient().rpc("msv2_upsert_share_receiver_config", {
    p_share_token: shareToken,
    p_receiver_id: receiverId,
    p_config: config,
  });
  if (error) {
    return jsonResponse({ ok: false, error: error.message }, 400);
  }

  return jsonResponse({ ok: true, config: Array.isArray(data) ? data[0] : data });
}

async function shareHistoryApi(request: Request): Promise<Response> {
  const shareToken = bearerToken(request);
  if (!shareToken) {
    return jsonResponse({ ok: false, error: "share token required" }, 401);
  }

  const url = new URL(request.url);
  const receiverId = String(url.searchParams.get("receiverId") ?? "").trim();
  if (!receiverId) {
    return jsonResponse({ ok: false, error: "receiverId required" }, 400);
  }

  const sensorId = Number(url.searchParams.get("sensorId") ?? "0");
  if (!Number.isFinite(sensorId) || sensorId <= 0) {
    return jsonResponse({ ok: false, error: "sensorId required" }, 400);
  }

  // Clamped before the Date math below: an unbounded sinceHours (e.g. a huge or
  // negative value) can push Date.now() - sinceHours*3600_000 outside the valid Date
  // range, throwing an uncaught RangeError that surfaced as a raw 500 instead of a
  // clean 400. 2160h (90 days) comfortably covers the widest UI range (7 days).
  const rawSinceHours = Number(url.searchParams.get("sinceHours") ?? "24");
  const sinceHours = Number.isFinite(rawSinceHours) ? Math.min(Math.max(rawSinceHours, 1), 2160) : 24;
  const since = new Date(Date.now() - sinceHours * 3600_000).toISOString();
  const rawBeforeId = Number(url.searchParams.get("beforeId") ?? "0");
  const beforeId = Number.isFinite(rawBeforeId) ? Math.max(rawBeforeId, 0) : 0;
  const rawLimit = Number(url.searchParams.get("limit") ?? "500");
  const limit = Number.isFinite(rawLimit) ? Math.min(Math.max(rawLimit, 1), 2000) : 500;

  const { data, error } = await serviceClient().rpc("msv2_read_share_sensor_history", {
    p_share_token: shareToken,
    p_receiver_id: receiverId,
    p_sensor_id: sensorId,
    p_since: since,
    p_before_id: beforeId,
    p_limit: limit,
  });
  if (error) {
    return jsonResponse({ ok: false, error: error.message }, 401);
  }

  return jsonResponse({ ok: true, readings: data ?? [] });
}

async function shareHistoryClearApi(request: Request): Promise<Response> {
  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "method not allowed" }, 405);
  }

  const shareToken = bearerToken(request);
  if (!shareToken) {
    return jsonResponse({ ok: false, error: "share token required" }, 401);
  }

  const body = await parseJsonBody(request);
  const receiverId = String(body.receiverId ?? "").trim();
  if (!receiverId) {
    return jsonResponse({ ok: false, error: "receiverId required" }, 400);
  }

  const { data, error } = await serviceClient().rpc("msv2_clear_share_sensor_history", {
    p_share_token: shareToken,
    p_receiver_id: receiverId,
  });
  if (error) {
    return jsonResponse({ ok: false, error: error.message }, 401);
  }

  return jsonResponse({ ok: true, deleted: data ?? 0 });
}

async function userSnapshotsApi(request: Request): Promise<Response> {
  const userToken = bearerToken(request);
  if (!userToken) {
    return jsonResponse({ ok: false, error: "user token required" }, 401);
  }

  const supabase = serviceClient();
  const { data: authData, error: authError } = await supabase.auth.getUser(userToken);
  if (authError || !authData.user) {
    return jsonResponse({ ok: false, error: "invalid user token" }, 401);
  }

  const { data, error } = await supabase.rpc("msv2_read_user_latest_snapshots", {
    p_user_id: authData.user.id,
  });
  if (error) {
    return jsonResponse({ ok: false, error: error.message }, 500);
  }

  return jsonResponse({ ok: true, snapshots: (data ?? []) as SnapshotRow[] });
}

function escapeHtml(value: unknown): string {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function webManifest(): Record<string, unknown> {
  return {
    name: "MSV2 Plants",
    short_name: "MSV2",
    start_url: "/functions/v1/plant-dashboard",
    scope: "/functions/v1/plant-dashboard",
    display: "standalone",
    background_color: "#f7f8f5",
    theme_color: "#287a55",
    icons: [{ src: "/functions/v1/plant-dashboard/app-icon.svg", sizes: "any", type: "image/svg+xml", purpose: "any maskable" }],
  };
}

function appIconSvg(): string {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">
  <rect width="128" height="128" rx="28" fill="#287a55"/>
  <path d="M64 98V63" stroke="#f7f8f5" stroke-width="10" stroke-linecap="round"/>
  <path d="M62 65c-24-2-36-14-38-36 23 1 38 13 38 36z" fill="#b7e4c7"/>
  <path d="M66 66c3-25 17-37 39-39-2 24-15 38-39 39z" fill="#d8f3dc"/>
  <path d="M40 100h48" stroke="#f7f8f5" stroke-width="10" stroke-linecap="round"/>
</svg>`;
}
