import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const PROJECT_URL = "https://lyemomfpzkawqecpfufm.supabase.co";
const PUBLISHABLE_KEY = "sb_publishable_Ycl5Be9F1qOxcU0pjQd1Cw_EzW4husY";

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

type DisplaySettingsRow = {
  receiver_id: string;
  brightness_percent: number;
  screen_on: boolean;
  settings_version: number;
  updated_at: string;
  updated_by: string;
};

function displaySettingsJson(row: DisplaySettingsRow) {
  return {
    receiverId: row.receiver_id,
    brightnessPercent: row.brightness_percent,
    screenOn: row.screen_on,
    settingsVersion: row.settings_version,
    updatedAt: row.updated_at,
    updatedBy: row.updated_by,
  };
}

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
    if (path.endsWith("/api/share-display")) return await shareDisplayApi(request);
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
        shareDisplay: "/functions/v1/plant-dashboard/api/share-display",
        shareHistory: "/functions/v1/plant-dashboard/api/share-history",
        shareHistoryClear: "/functions/v1/plant-dashboard/api/share-history-clear",
      },
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return response(`<!doctype html><title>MSV2 Error</title><pre>${escapeHtml(message)}</pre>`, "text/html; charset=utf-8", 500);
  }
});

async function shareClaimApi(request: Request): Promise<Response> {
  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "method not allowed" }, 405);
  }

  const body = await request.json();
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

  const body = await request.json();
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

async function shareDisplayApi(request: Request): Promise<Response> {
  const shareToken = bearerToken(request);
  if (!shareToken) {
    return jsonResponse({ ok: false, error: "share token required" }, 401);
  }

  if (request.method === "GET") {
    const { data, error } = await serviceClient().rpc("msv2_read_share_receiver_display_settings", {
      p_share_token: shareToken,
    });
    if (error) {
      return jsonResponse({ ok: false, error: error.message }, 401);
    }
    return jsonResponse({
      ok: true,
      settings: ((data ?? []) as DisplaySettingsRow[]).map(displaySettingsJson),
    });
  }

  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "method not allowed" }, 405);
  }

  const body = await request.json();
  const receiverId = String(body.receiverId ?? "").trim();
  const brightnessPercent = Number(body.brightnessPercent);
  const screenOn = body.screenOn;
  if (!receiverId || !Number.isInteger(brightnessPercent) || brightnessPercent < 1 || brightnessPercent > 100 ||
      typeof screenOn !== "boolean") {
    return jsonResponse({ ok: false, error: "receiverId, brightnessPercent 1-100, and screenOn are required" }, 400);
  }

  const { data, error } = await serviceClient().rpc("msv2_upsert_share_receiver_display_settings", {
    p_share_token: shareToken,
    p_receiver_id: receiverId,
    p_brightness_percent: brightnessPercent,
    p_screen_on: screenOn,
  });
  if (error) {
    return jsonResponse({ ok: false, error: error.message }, 400);
  }
  const row = (Array.isArray(data) ? data[0] : data) as DisplaySettingsRow;
  return jsonResponse({ ok: true, settings: displaySettingsJson(row) });
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

  const sinceHours = Number(url.searchParams.get("sinceHours") ?? "24");
  const since = new Date(Date.now() - (Number.isFinite(sinceHours) ? sinceHours : 24) * 3600_000).toISOString();
  const beforeId = Number(url.searchParams.get("beforeId") ?? "0");
  const limit = Number(url.searchParams.get("limit") ?? "500");

  const { data, error } = await serviceClient().rpc("msv2_read_share_sensor_history", {
    p_share_token: shareToken,
    p_receiver_id: receiverId,
    p_sensor_id: sensorId,
    p_since: since,
    p_before_id: Number.isFinite(beforeId) ? beforeId : 0,
    p_limit: Number.isFinite(limit) ? limit : 500,
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

  const body = await request.json();
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

function renderApp(): string {
  const config = JSON.stringify({
    supabaseUrl: Deno.env.get("SUPABASE_URL") || Deno.env.get("URL") || PROJECT_URL,
    publishableKey: Deno.env.get("SUPABASE_PUBLISHABLE_KEY") || Deno.env.get("PUBLISHABLE_KEY") || PUBLISHABLE_KEY,
    shareClaimUrl: "/functions/v1/plant-dashboard/api/share-claim",
    shareSnapshotsUrl: "/functions/v1/plant-dashboard/api/share-snapshots",
  });

  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#287a55">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-title" content="MSV2 Plants">
  <meta name="apple-mobile-web-app-status-bar-style" content="default">
  <link rel="manifest" href="/functions/v1/plant-dashboard/manifest.webmanifest">
  <link rel="icon" href="/functions/v1/plant-dashboard/app-icon.svg" type="image/svg+xml">
  <link rel="apple-touch-icon" href="/functions/v1/plant-dashboard/app-icon.svg">
  <title>MSV2 Plants</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f7f8f5;
      --ink: #18211b;
      --muted: #647064;
      --panel: #ffffff;
      --line: #d9dfd5;
      --field: #fbfcfa;
      --accent: #287a55;
      --accent-dark: #1f6044;
      --warn: #a26300;
      --danger: #b3261e;
    }
    * { box-sizing: border-box; }
    body { margin: 0; font: 14px/1.35 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: var(--bg); color: var(--ink); }
    header { display: flex; justify-content: space-between; gap: 16px; align-items: center; padding: 18px clamp(16px, 4vw, 40px); background: var(--panel); border-bottom: 1px solid var(--line); }
    h1 { margin: 0; font-size: 24px; line-height: 1; }
    h2 { margin: 0 0 10px; font-size: 18px; }
    h3 { margin: 0; font-size: 16px; }
    p { margin: 6px 0 0; color: var(--muted); }
    main { width: min(1080px, 100%); margin: 0 auto; padding: 18px clamp(16px, 4vw, 40px) 42px; }
    .topline { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; color: var(--muted); }
    .pill { display: inline-flex; min-height: 28px; align-items: center; border: 1px solid var(--line); border-radius: 999px; padding: 3px 10px; background: #f3f6f1; color: var(--muted); white-space: nowrap; }
    .toolbar { display: flex; gap: 8px; align-items: center; }
    .grid { display: grid; grid-template-columns: 320px minmax(0, 1fr); gap: 14px; align-items: start; }
    .panel, .receiver, .summary-cell { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; }
    .panel { padding: 14px; }
    label { display: block; margin: 10px 0 4px; color: var(--muted); font-size: 13px; }
    input { width: 100%; min-height: 40px; border: 1px solid var(--line); border-radius: 6px; padding: 8px 10px; background: var(--field); color: var(--ink); font: inherit; }
    button { min-height: 38px; border: 1px solid var(--accent); border-radius: 6px; padding: 8px 12px; background: var(--accent); color: #fff; font: inherit; font-weight: 700; cursor: pointer; }
    button.secondary { background: #fff; color: var(--accent-dark); border-color: var(--line); }
    button:disabled { opacity: .55; cursor: not-allowed; }
    .row { display: flex; gap: 8px; margin-top: 12px; }
    .row button { flex: 1; }
    .notice { margin-top: 12px; border: 1px solid var(--line); border-radius: 8px; padding: 10px; background: #f3f6f1; color: var(--muted); }
    .notice.error { border-color: #e1aaa5; background: #fff5f4; color: var(--danger); }
    .notice.ok { border-color: #a9d8bf; background: #f1fbf5; color: var(--accent-dark); }
    .summary { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 8px; margin-bottom: 14px; }
    .summary-cell { padding: 12px; }
    .summary-label { display: block; color: var(--muted); font-size: 12px; margin-bottom: 4px; }
    .summary-value { font-size: 22px; font-weight: 800; line-height: 1; }
    .receiver { padding: 14px; margin-bottom: 14px; }
    .meta { display: flex; flex-wrap: wrap; gap: 8px 14px; margin-top: 8px; color: var(--muted); font-size: 13px; }
    .plants { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 10px; margin-top: 14px; }
    .plant { border: 1px solid var(--line); border-radius: 8px; padding: 12px; background: #fbfcfa; }
    .plant-head { display: flex; justify-content: space-between; gap: 10px; align-items: baseline; margin-bottom: 10px; }
    .badge { border: 1px solid var(--line); border-radius: 999px; padding: 2px 8px; color: var(--muted); font-size: 12px; white-space: nowrap; }
    .metric { display: flex; justify-content: space-between; gap: 10px; padding: 3px 0; }
    .metric span:first-child { color: var(--muted); }
    .bar { height: 9px; border-radius: 999px; background: #e4e9e1; overflow: hidden; margin: 6px 0 8px; }
    .bar span { display: block; height: 100%; width: var(--pct); background: var(--accent); }
    .bar.warn span { background: var(--warn); }
    .bar.danger span { background: var(--danger); }
    .ok { color: var(--accent); }
    .warn { color: var(--warn); }
    .danger { color: var(--danger); }
    .empty { border: 1px dashed var(--line); border-radius: 8px; padding: 22px; background: var(--panel); }
    code { background: #eef1eb; border: 1px solid var(--line); border-radius: 4px; padding: 1px 4px; }
    @media (max-width: 760px) {
      header { align-items: flex-start; flex-direction: column; padding-top: max(18px, env(safe-area-inset-top)); }
      .toolbar { width: 100%; }
      .toolbar button { flex: 1; }
      .grid { grid-template-columns: 1fr; }
      .summary { grid-template-columns: repeat(2, minmax(0, 1fr)); }
      main { padding-bottom: max(42px, env(safe-area-inset-bottom)); }
    }
  </style>
</head>
<body>
  <header>
    <div>
      <h1>MSV2 Plants</h1>
      <div class="topline">
        <span id="linkPill" class="pill">Checking household link</span>
        <span id="claimPill" class="pill" hidden>Pairing details ready</span>
      </div>
    </div>
    <div class="toolbar">
      <button type="button" class="secondary" id="refreshButton">Refresh</button>
      <button type="button" class="secondary" id="forgetButton">Forget</button>
    </div>
  </header>
  <main>
    <div class="grid">
      <section class="panel">
        <h2>Connect Household</h2>
        <p>Scan the receiver QR code or enter the pairing details from the ESP32 app. This phone will stay connected.</p>
        <label for="receiverIdInput">Receiver ID</label>
        <input id="receiverIdInput" autocomplete="off" placeholder="MSV2...">
        <label for="claimCodeInput">Claim code</label>
        <input id="claimCodeInput" autocomplete="off" placeholder="ABCD-1234">
        <label for="householdInput">Household name</label>
        <input id="householdInput" autocomplete="organization" value="My Plants">
        <div class="row">
          <button type="button" id="connectButton">Connect</button>
        </div>
        <div class="notice" id="connectNotice">No account needed. The private household key is stored in this browser.</div>
      </section>

      <section>
        <div id="summary"></div>
        <div id="receivers" class="empty">
          <h2>No household connected</h2>
          <p>Connect with the receiver QR code to show cloud plant data here.</p>
        </div>
      </section>
    </div>
  </main>

  <script type="module">
    const CONFIG = ${config};
    const SHARE_TOKEN_KEY = "msv2.householdShareToken";
    const params = new URLSearchParams(window.location.search);
    const state = { shareToken: localStorage.getItem(SHARE_TOKEN_KEY) || "", snapshots: [] };

    const byId = (id) => document.getElementById(id);
    const setNotice = (text, type = "") => {
      const node = byId("connectNotice");
      node.textContent = text;
      node.className = "notice" + (type ? " " + type : "");
    };
    const escapeHtml = (value) => String(value ?? "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;");

    function applyPairingParams() {
      const receiverId = params.get("receiverId") || params.get("rid") || "";
      const claimCode = params.get("claimCode") || params.get("code") || "";
      if (receiverId) byId("receiverIdInput").value = receiverId;
      if (claimCode) byId("claimCodeInput").value = claimCode;
      byId("claimPill").hidden = !(receiverId && claimCode);
    }

    function renderLinkState() {
      byId("linkPill").textContent = state.shareToken ? "Household connected" : "No household link";
      byId("forgetButton").disabled = !state.shareToken;
    }

    async function connectHousehold() {
      const receiverId = byId("receiverIdInput").value.trim();
      const claimCode = byId("claimCodeInput").value.trim();
      const householdName = byId("householdInput").value.trim() || "My Plants";
      setNotice("Connecting household...");
      const response = await fetch(CONFIG.shareClaimUrl, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ receiverId, claimCode, householdName }),
      });
      const body = await response.json();
      if (!response.ok || !body.ok || !body.share?.share_token) {
        setNotice(body.error || "Could not connect household.", "error");
        return;
      }
      state.shareToken = body.share.share_token;
      localStorage.setItem(SHARE_TOKEN_KEY, state.shareToken);
      setNotice("Household connected on this phone.", "ok");
      renderLinkState();
      await loadSnapshots();
    }

    async function loadSnapshots() {
      renderLinkState();
      if (!state.shareToken) {
        byId("summary").innerHTML = "";
        byId("receivers").className = "empty";
        byId("receivers").innerHTML = "<h2>No household connected</h2><p>Connect with the receiver QR code to show cloud plant data here.</p>";
        return;
      }

      const response = await fetch(CONFIG.shareSnapshotsUrl, {
        headers: { "Authorization": "Bearer " + state.shareToken },
      });
      const body = await response.json();
      if (!response.ok || !body.ok) {
        byId("receivers").className = "empty";
        byId("receivers").innerHTML = "<h2>Could not load plants</h2><p>" + escapeHtml(body.error || "Unknown error") + "</p>";
        return;
      }
      state.snapshots = body.snapshots || [];
      renderDashboard();
    }

    function forgetHousehold() {
      state.shareToken = "";
      state.snapshots = [];
      localStorage.removeItem(SHARE_TOKEN_KEY);
      setNotice("Household link removed from this phone.");
      loadSnapshots();
    }

    function renderDashboard() {
      const rows = state.snapshots;
      byId("summary").innerHTML = renderSummary(rows);
      byId("receivers").className = rows.length ? "" : "empty";
      byId("receivers").innerHTML = rows.length
        ? rows.map(renderReceiver).join("")
        : "<h2>No snapshots yet</h2><p>The receiver is connected. Waiting for its next cloud upload.</p>";
    }

    function renderSummary(rows) {
      let plantCount = 0;
      let dryCount = 0;
      let lowBatteryCount = 0;
      for (const row of rows) {
        const cards = cardsBySensor(row.snapshot?.config?.cards || []);
        for (const plant of row.snapshot?.plants || []) {
          plantCount += 1;
          const card = cards.get(plant.sensor || 0);
          const pct = percent(plant.moisture, card?.moisture?.dry ?? 350, card?.moisture?.wet ?? 800);
          if (pct != null && pct < 25) dryCount += 1;
          if (typeof plant.batteryPercent === "number" && plant.batteryPercent < 20) lowBatteryCount += 1;
        }
      }
      return '<section class="summary" aria-label="Garden summary">' +
        summaryCell("Receivers", rows.length) +
        summaryCell("Plants", plantCount) +
        summaryCell("Dry", dryCount, dryCount ? "danger" : "ok") +
        summaryCell("Low Battery", lowBatteryCount, lowBatteryCount ? "danger" : "ok") +
      '</section>';
    }

    function summaryCell(label, value, className = "") {
      return '<div class="summary-cell"><span class="summary-label">' + escapeHtml(label) + '</span><span class="summary-value ' + escapeHtml(className) + '">' + escapeHtml(value) + '</span></div>';
    }

    function renderReceiver(row) {
      const snapshot = row.snapshot || {};
      const config = snapshot.config || {};
      const title = config.title || row.household_name || "MSV2 Plants";
      const readings = snapshot.plants || [];
      const cards = cardsBySensor(config.cards || []);
      const plants = readings.length ? readings.map((plant) => renderPlant(plant, cards.get(plant.sensor || 0))).join("") : "<p>No plant readings in this snapshot.</p>";
      return '<section class="receiver"><h2>' + escapeHtml(title) + '</h2><div class="meta">' +
        '<span>Household: ' + escapeHtml(row.household_name || "My Plants") + '</span>' +
        '<span>Receiver: <code>' + escapeHtml(row.receiver_id) + '</code></span>' +
        '<span>Config v' + escapeHtml(config.version ?? row.config_version ?? "--") + '</span>' +
        '<span>Cloud: ' + escapeHtml(formatDate(row.received_at)) + '</span>' +
        '</div><div class="plants">' + plants + '</div></section>';
    }

    function renderPlant(plant, config) {
      const name = config?.plant || "Probe " + (plant.sensor ?? "--");
      const pct = percent(plant.moisture, config?.moisture?.dry ?? 350, config?.moisture?.wet ?? 800);
      const moistureClass = healthClass(pct);
      const battery = plant.batteryPercent;
      const batteryClass = typeof battery === "number" && battery < 20 ? "danger" : typeof battery === "number" && battery < 50 ? "warn" : "ok";
      return '<article class="plant"><div class="plant-head"><h3>' + escapeHtml(name) + '</h3><span class="badge">Probe ' + escapeHtml(plant.sensor ?? "--") + '</span></div>' +
        metric("Moisture", String(plant.moisture ?? "--") + " raw / " + String(pct ?? "--") + "%", moistureClass) +
        bar(pct, moistureClass) +
        metric("Battery", battery == null ? "--" : String(battery) + "%", batteryClass) +
        metric("RSSI", String(plant.rssi ?? "--") + " dBm") +
        metric("Age", String(plant.ageMs ?? "--") + " ms") +
        metric("Packets", String(plant.received ?? "--") + " rx / " + String(plant.missedTotal ?? "--") + " missed") +
        '</article>';
    }

    function cardsBySensor(cards) {
      const result = new Map();
      for (const card of cards) if (typeof card.sensor === "number") result.set(card.sensor, card);
      return result;
    }

    function percent(value, dry, wet) {
      if (typeof value !== "number" || wet === dry) return null;
      return Math.max(0, Math.min(100, Math.round(((value - dry) * 100) / (wet - dry))));
    }

    function healthClass(pct) {
      if (pct == null) return "";
      if (pct < 25) return "danger";
      if (pct < 50) return "warn";
      return "ok";
    }

    function bar(pct, className) {
      return '<div class="bar ' + escapeHtml(className) + '" style="--pct:' + String(pct ?? 0) + '%"><span></span></div>';
    }

    function metric(label, value, className = "") {
      return '<div class="metric"><span>' + escapeHtml(label) + '</span><span class="' + escapeHtml(className) + '">' + escapeHtml(value) + '</span></div>';
    }

    function formatDate(value) {
      const date = new Date(value);
      return Number.isNaN(date.getTime()) ? "--" : date.toLocaleString();
    }

    byId("connectButton").addEventListener("click", connectHousehold);
    byId("refreshButton").addEventListener("click", loadSnapshots);
    byId("forgetButton").addEventListener("click", forgetHousehold);

    applyPairingParams();
    renderLinkState();
    await loadSnapshots();
    window.setInterval(loadSnapshots, 30000);
  </script>
</body>
</html>`;
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
