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
    if (path.endsWith("/api/share-autocal-reset")) return await shareAutocalResetApi(request);
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
        shareAutocalReset: "/functions/v1/plant-dashboard/api/share-autocal-reset",
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
  // clean 400. 87600h (10 years) matches the widest UI range - MobileAppPage.h's
  // "All time" option - with headroom to spare.
  const rawSinceHours = Number(url.searchParams.get("sinceHours") ?? "24");
  const sinceHours = Number.isFinite(rawSinceHours) ? Math.min(Math.max(rawSinceHours, 1), 87600) : 24;
  const since = new Date(Date.now() - sinceHours * 3600_000).toISOString();
  const rawBeforeId = Number(url.searchParams.get("beforeId") ?? "0");
  const beforeId = Number.isFinite(rawBeforeId) ? Math.max(rawBeforeId, 0) : 0;

  // Optional closed-range upper bound (e.g. "view 2026-10-29 through 2026-10-31") on top of
  // the existing sinceHours/beforeId pagination - additive, not a replacement. Absent/invalid
  // means "no upper bound", same as every existing caller before this parameter existed. Not
  // clamped against `since` here - an until earlier than since just yields zero rows, same as
  // any other empty-range query, rather than needing special-cased validation.
  const rawUntilMs = Number(url.searchParams.get("untilMs") ?? "");
  const until = Number.isFinite(rawUntilMs) && rawUntilMs > 0 ? new Date(rawUntilMs).toISOString() : null;

  // Probe the eligible row count before reading any history payload. A normally reporting
  // probe keeps every row; a dense probe is sampled at a stable one-in-N stride selected to
  // fit HISTORY_POINT_BUDGET. At 2400 points the embedded 800px-wide chart still has three
  // samples per horizontal pixel, while a 100k-row week shrinks from ~100 database RPCs and
  // five large browser downloads to three small internal RPC pages and one response.
  const HISTORY_POINT_BUDGET = 2400;

  const supabase = serviceClient();
  const { data: densityData, error: densityError } = await supabase.rpc(
    "msv2_read_share_sensor_history_density",
    {
      p_share_token: shareToken,
      p_receiver_id: receiverId,
      p_sensor_id: sensorId,
      p_since: since,
      p_until: until,
    },
  );
  if (densityError) {
    return jsonResponse({ ok: false, error: densityError.message }, 401);
  }

  const density = (Array.isArray(densityData) ? densityData[0] : densityData) as
    | { total_rows?: number | string; newest_id?: number | string; moisture_scale?: number | string | null }
    | null;
  const totalRows = Math.max(0, Math.floor(Number(density?.total_rows ?? 0)));
  const newestId = Math.max(0, Math.floor(Number(density?.newest_id ?? 0)));
  const moistureScale = Number(density?.moisture_scale);
  const sampleEvery = Math.max(1, Math.ceil(totalRows / HISTORY_POINT_BUDGET));
  const sampledTotal = Math.ceil(totalRows / sampleEvery);

  if (totalRows === 0 || newestId === 0 || !Number.isFinite(moistureScale)) {
    return jsonResponse({
      ok: true,
      readings: [],
      hasMore: false,
      totalReadings: totalRows,
      sampleEvery: 1,
      sampled: false,
      pointBudget: HISTORY_POINT_BUDGET,
    });
  }

  // PostgREST's own "Max Rows" API setting hard-caps every RPC response to 1000 rows
  // server-side no matter what p_limit asks for (confirmed live 2026-08-26). The edge
  // function still loops internally so those pages stay on Supabase's network, but it now
  // walks the bounded sample rather than exhaustively walking the underlying dense history.
  const RPC_PAGE_ROWS = 1000;
  // Wall-clock guard on the loop below, comfortably inside the edge function's own execution
  // ceiling. If it fires, `hasMore` is still reported honestly and the caller just makes
  // another round trip instead of this request hanging until it times out.
  const LOOP_TIME_BUDGET_MS = 20000;

  const rawLimit = Number(url.searchParams.get("limit") ?? "500");
  const requestedTotal = Number.isFinite(rawLimit)
    ? Math.min(Math.max(Math.floor(rawLimit), 1), HISTORY_POINT_BUDGET, sampledTotal)
    : Math.min(500, sampledTotal);

  // Fetch one row past what the caller asked for ("lookahead") instead of exactly
  // requestedTotal. Getting that extra row back proves more data exists past this page,
  // full stop - no need to separately reason about whether a page happened to come back
  // exactly full, or special-case the initial request against sampledTotal (the previous
  // approach did both, and still had a gap: a continuation page - beforeId != 0 - that
  // exactly exhausted the remaining sampled range wasn't covered by that special case, so
  // hasMore was wrongly reported true and the client burned one extra round trip that came
  // back empty). The lookahead row itself is trimmed off below before responding, so the
  // client's own next-page cursor (derived client-side from the last row it actually
  // receives - see MobileAppPage.h's loadHistoryInner) is always the last row we return,
  // never the discarded lookahead row.
  const overfetchTarget = requestedTotal + 1;
  const rows: Record<string, unknown>[] = [];
  let cursor = beforeId;
  let stoppedByTimeBudget = false;
  const loopStartedAt = Date.now();

  while (rows.length < overfetchTarget) {
    const want = Math.min(RPC_PAGE_ROWS, overfetchTarget - rows.length);
    const { data, error } = await supabase.rpc("msv2_read_share_sensor_history_sampled", {
      p_share_token: shareToken,
      p_receiver_id: receiverId,
      p_sensor_id: sensorId,
      p_since: since,
      p_at_or_before_id: newestId,
      p_before_id: cursor,
      p_moisture_scale: moistureScale,
      p_sample_every: sampleEvery,
      p_limit: want,
      p_until: until,
    });
    if (error) {
      return jsonResponse({ ok: false, error: error.message }, 401);
    }

    const page = (data ?? []) as Record<string, unknown>[];
    rows.push(...page);

    if (page.length < want) {
      // The underlying range is genuinely exhausted - fewer rows came back than asked for.
      break;
    }

    cursor = Number(page[page.length - 1].id);

    if (rows.length >= overfetchTarget) {
      break;
    }
    if (Date.now() - loopStartedAt > LOOP_TIME_BUDGET_MS) {
      stoppedByTimeBudget = true;
      break;
    }
  }

  // Stopping on the time budget means we bailed before proving exhaustion either way -
  // report hasMore honestly so the caller retries, same as before. Otherwise the lookahead
  // row (if present) is the whole signal: strictly more than requestedTotal rows came back
  // only when there truly was another row past this page.
  const hasMore = rows.length > 0 && (stoppedByTimeBudget || rows.length > requestedTotal);
  if (rows.length > requestedTotal) {
    rows.length = requestedTotal;
  }

  return jsonResponse({
    ok: true,
    readings: rows,
    hasMore,
    totalReadings: totalRows,
    sampleEvery,
    sampled: sampleEvery > 1,
    pointBudget: HISTORY_POINT_BUDGET,
  });
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

// Local app only (see MobileAppPage.h's Setup tab) - forces one sensor's learned range
// back into "learning" mode from fresh data, for cases like moving a probe to a new
// plant/pot. Not exposed on the read-only Netlify dashboard, matching the same
// local-app-owns-writes decision as 022's retired share-display RPCs.
async function shareAutocalResetApi(request: Request): Promise<Response> {
  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "method not allowed" }, 405);
  }

  const shareToken = bearerToken(request);
  if (!shareToken) {
    return jsonResponse({ ok: false, error: "share token required" }, 401);
  }

  const body = await parseJsonBody(request);
  const receiverId = String(body.receiverId ?? "").trim();
  const sensorId = Number(body.sensorId);
  if (!receiverId || !Number.isFinite(sensorId) || sensorId <= 0) {
    return jsonResponse({ ok: false, error: "receiverId and sensorId required" }, 400);
  }

  const { data, error } = await serviceClient().rpc("msv2_reset_sensor_calibration", {
    p_share_token: shareToken,
    p_receiver_id: receiverId,
    p_sensor_id: sensorId,
  });
  if (error) {
    return jsonResponse({ ok: false, error: error.message }, 401);
  }

  return jsonResponse({ ok: true, resetAt: data });
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
