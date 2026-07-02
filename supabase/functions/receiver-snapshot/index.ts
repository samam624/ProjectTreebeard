import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
};

type Snapshot = {
  receiverId?: string;
  claimCode?: string;
  build?: string;
  config?: {
    version?: number;
  };
  plants?: unknown[];
};

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
    const receiverToken = requireEnv("MSV2_RECEIVER_TOKEN");
    const supabase = createClient(supabaseUrl, serviceRoleKey, {
      auth: {
        persistSession: false,
      },
    });

    const providedToken = bearerToken(request);

    if (request.method === "GET") {
      if (providedToken !== receiverToken) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }

      const url = new URL(request.url);
      const receiverId = url.searchParams.get("receiverId");
      const { data, error } = await supabase.rpc("msv2_read_latest_snapshots", {
        p_receiver_id: receiverId,
        p_limit: receiverId ? 1 : 20,
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

    const snapshot = await request.json() as Snapshot;
    const receiverId = String(snapshot.receiverId ?? "").trim();
    if (!receiverId) {
      return jsonResponse({ ok: false, error: "receiverId required" }, 400);
    }

    if (!providedToken) {
      return jsonResponse({ ok: false, error: "unauthorized" }, 401);
    }

    if (providedToken !== receiverToken) {
      const { data: verified, error: verifyError } = await supabase.rpc("msv2_verify_receiver_api_token", {
        p_receiver_id: receiverId,
        p_api_token: providedToken,
      });
      if (verifyError || verified !== true) {
        return jsonResponse({ ok: false, error: "unauthorized" }, 401);
      }
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
