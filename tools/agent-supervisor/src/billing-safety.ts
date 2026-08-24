const METERED_AUTH_VARIABLES = [
  "ANTHROPIC_API_KEY",
  "ANTHROPIC_AUTH_TOKEN",
  "CLAUDE_CODE_USE_BEDROCK",
  "CLAUDE_CODE_USE_VERTEX",
  "CLAUDE_CODE_USE_FOUNDRY",
  "CLAUDE_CODE_CUSTOM_OAUTH_URL",
  "ANTHROPIC_BASE_URL",
  "CODEX_API_KEY",
  "OPENAI_API_KEY",
  "OPENAI_BASE_URL",
] as const;
const METERED_AUTH_VARIABLE_SET = new Set<string>(METERED_AUTH_VARIABLES);

export function activeMeteredAuthVariables(
  environment: NodeJS.ProcessEnv = process.env,
): string[] {
  return METERED_AUTH_VARIABLES.filter((name) => Boolean(environment[name]?.trim()));
}

export function assertSubscriptionOnlyBilling(
  environment: NodeJS.ProcessEnv = process.env,
): void {
  const active = activeMeteredAuthVariables(environment);
  if (active.length > 0) {
    throw new Error(
      "Billing safety stopped this run because metered provider configuration is active: " +
        `${active.join(", ")}. Clear these variables and use Claude/Codex subscription login.`,
    );
  }
}

export function assertSubscriptionSafeModels(claudeModel?: string): void {
  if (claudeModel?.toLowerCase().includes("[1m]")) {
    throw new Error(
      "Billing safety stopped this run because Claude [1m] context can require usage credits. " +
        "Choose a standard-context model.",
    );
  }
}

export function subscriptionOnlyEnvironment(
  environment: NodeJS.ProcessEnv = process.env,
): Record<string, string> {
  const safe: Record<string, string> = {};
  for (const [name, value] of Object.entries(environment)) {
    if (value !== undefined && !METERED_AUTH_VARIABLE_SET.has(name)) {
      safe[name] = value;
    }
  }

  // These prevent enabling paid Claude usage from inside a supervised child process.
  safe.DISABLE_EXTRA_USAGE_COMMAND = "1";
  safe.CLAUDE_CODE_DISABLE_1M_CONTEXT = "1";
  return safe;
}

export function isUsageOrBillingLimit(message: string): boolean {
  return /(?:usage|session|weekly|rate|spend|budget) limit|quota|credit balance|usage credits|insufficient_quota|billing/i.test(
    message,
  );
}
