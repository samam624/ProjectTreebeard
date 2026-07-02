import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

type BootstrapRequest = {
  receiverId?: string;
  receiverSecret?: string;
  claimCode?: string;
  build?: string;
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

  if (request.method !== "POST") {
    return jsonResponse({ ok: false, error: "method not allowed" }, 405);
  }

  try {
    const supabaseUrl = requireEnv("SUPABASE_URL", "URL");
    const serviceRoleKey = requireEnv("SUPABASE_SERVICE_ROLE_KEY", "SERVICE_ROLE_KEY");
    const supabase = createClient(supabaseUrl, serviceRoleKey, {
      auth: {
        persistSession: false,
      },
    });

    const body = await request.json() as BootstrapRequest;
    const receiverId = String(body.receiverId ?? "").trim();
    const receiverSecret = String(body.receiverSecret ?? "").trim();
    const claimCode = String(body.claimCode ?? "").trim();
    const build = String(body.build ?? "").trim();

    if (!receiverId || !receiverSecret) {
      return jsonResponse({ ok: false, error: "receiverId and receiverSecret required" }, 400);
    }

    const { data, error } = await supabase.rpc("msv2_register_receiver", {
      p_receiver_id: receiverId,
      p_receiver_secret: receiverSecret,
      p_claim_code: claimCode || null,
      p_build: build || null,
    });
    if (error) {
      return jsonResponse({ ok: false, error: error.message }, 500);
    }

    const registration = data?.[0];
    if (!registration?.api_token) {
      return jsonResponse({ ok: false, error: "registration did not return an api token" }, 500);
    }

    return jsonResponse({
      ok: true,
      receiverId,
      uploadUrl: `${supabaseUrl}/functions/v1/receiver-snapshot`,
      apiToken: registration.api_token,
      apiTokenHint: registration.api_token_hint,
      claimCode: claimCode || null,
      syncIntervalSeconds: 300,
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return jsonResponse({ ok: false, error: message }, 500);
  }
});
