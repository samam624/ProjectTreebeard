import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

type ClaimRequest = {
  receiverId?: string;
  claimCode?: string;
  householdName?: string;
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
    return new Response(null, { status: 204, headers: corsHeaders });
  }

  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "method not allowed" }, 405);
  }

  try {
    const supabaseUrl = requireEnv("SUPABASE_URL", "URL");
    const serviceRoleKey = requireEnv("SUPABASE_SERVICE_ROLE_KEY", "SERVICE_ROLE_KEY");
    const userToken = bearerToken(request);
    if (!userToken) {
      return jsonResponse({ ok: false, error: "user token required" }, 401);
    }

    const supabase = createClient(supabaseUrl, serviceRoleKey, {
      auth: { persistSession: false },
    });

    const { data: authData, error: authError } = await supabase.auth.getUser(userToken);
    if (authError || !authData.user) {
      return jsonResponse({ ok: false, error: "invalid user token" }, 401);
    }

    const body = await request.json() as ClaimRequest;
    const receiverId = String(body.receiverId ?? "").trim();
    const claimCode = String(body.claimCode ?? "").trim();
    if (!receiverId || !claimCode) {
      return jsonResponse({ ok: false, error: "receiverId and claimCode required" }, 400);
    }

    const { data, error } = await supabase.rpc("msv2_claim_receiver", {
      p_user_id: authData.user.id,
      p_receiver_id: receiverId,
      p_claim_code: claimCode,
      p_household_name: body.householdName ?? "My Plants",
    });

    if (error) {
      return jsonResponse({ ok: false, error: error.message }, 400);
    }

    return jsonResponse({
      ok: true,
      claim: Array.isArray(data) ? data[0] : data,
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return jsonResponse({ ok: false, error: message }, 500);
  }
});
