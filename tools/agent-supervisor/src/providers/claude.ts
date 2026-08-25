import {
  query,
  type Options,
  type SDKResultMessage,
} from "@anthropic-ai/claude-agent-sdk";
import {
  assertSubscriptionOnlyBilling,
  assertSubscriptionSafeModels,
  subscriptionOnlyEnvironment,
} from "../billing-safety.js";
import type { AgentProvider, ProviderRequest, ProviderResponse } from "./types.js";

const READ_TOOLS = ["Read", "Glob", "Grep"];
const WRITE_TOOLS = [...READ_TOOLS, "Edit", "Write"];

const SYSTEM_PROMPT = `You are one participant in a bounded two-agent engineering workflow.
Follow the requested phase exactly. Never commit, push, deploy, install dependencies, use the
network, access paths outside the working directory, or ask another agent to work. In read mode,
inspect and reason only. In write mode, edit only files necessary for the approved plan. The host
process runs tests separately. Return only the requested structured output.

This call is bounded by a real turn/time/spend budget shared with a live user session - do not
re-read files you have already read this call, do not glob the whole repository when a targeted
Grep for the relevant symbol/path will do, and do not re-derive project conventions already
covered by CLAUDE.md. Prefer the smallest amount of investigation that lets you answer the
current phase's question correctly.`;

export type ClaudeProviderOptions = {
  cwd: string;
  maxTurns: number;
  maxBudgetUsdPerCall: number;
  maxTotalBudgetUsd: number;
  model?: string;
  // Seeds the session this provider resumes on its very first call, instead of starting
  // blank. Meant for an existing real conversation (this interactive session, or another
  // Claude Code session on this repo) so the collaboration continues from context you've
  // already built up rather than rediscovering it -- see src/session-discovery.ts for how
  // --resume-claude auto finds one. A resumed session's already-processed prefix is eligible
  // for prompt caching same as any other resume, so this is not the same cost as reprocessing
  // the whole transcript at full price on every call, but it's still real extra input size
  // versus a blank session -- pick a specific id (not auto) if you want predictable reuse
  // across multiple supervisor runs, since a resumed session gets excluded from future "auto"
  // picks once it appears in this run's own .agent-runs log.
  resumeSessionId?: string;
};

export class ClaudeProvider implements AgentProvider {
  readonly name = "claude" as const;
  private readonly options: ClaudeProviderOptions;
  private sessionId: string | null;
  private spentUsd = 0;

  constructor(options: ClaudeProviderOptions) {
    // Previously only enforced once at the CLI entrypoint (index.ts) - anything that
    // constructs a ClaudeProvider directly (a test, a future script, another entrypoint)
    // got zero preemptive billing protection, just the passive env-scrubbing fallback in
    // subscriptionOnlyEnvironment() below. Enforcing it here means every real construction
    // path is covered regardless of caller.
    assertSubscriptionOnlyBilling();
    assertSubscriptionSafeModels(options.model);
    this.options = options;
    this.sessionId = options.resumeSessionId ?? null;
  }

  async invoke(request: ProviderRequest): Promise<ProviderResponse> {
    request.signal.throwIfAborted();
    const remainingBudget = this.options.maxTotalBudgetUsd - this.spentUsd;
    if (remainingBudget <= 0) {
      throw new Error(
        `Claude budget exhausted at $${this.spentUsd.toFixed(2)} ` +
          `(limit $${this.options.maxTotalBudgetUsd.toFixed(2)}).`,
      );
    }

    const tools = request.access === "write" ? WRITE_TOOLS : READ_TOOLS;
    const abortController = new AbortController();
    const relayAbort = (): void => abortController.abort(request.signal.reason);
    request.signal.addEventListener("abort", relayAbort, { once: true });
    const sdkOptions: Options = {
      abortController,
      cwd: this.options.cwd,
      maxTurns: this.options.maxTurns,
      maxBudgetUsd: Math.min(this.options.maxBudgetUsdPerCall, remainingBudget),
      permissionMode: "dontAsk",
      tools,
      allowedTools: tools,
      disallowedTools: ["Bash", "WebFetch", "WebSearch", "Agent"],
      // 2026-08-24: was `[]` ("SDK isolation mode"), which silently discards CLAUDE.md --
      // the SDK docs are explicit that 'project' is *required* to load it. That left every
      // call blind to this repo's own instructions (the MSV3 clock/build guardrails, the
      // "never raw arduino-cli" rule, etc.), forcing the model to rediscover repo
      // conventions from scratch via tool calls on every single phase instead of reading
      // them for free -- a real driver of both the turn/budget exhaustion seen in
      // .agent-runs/ and outright wrong recommendations that ignore project rules.
      settingSources: ["project"],
      skills: [],
      env: subscriptionOnlyEnvironment(),
      // Preset form (not a bare string) so Claude Code's own default system prompt still
      // loads -- a bare string fully replaces it, which also does NOT stop CLAUDE.md from
      // loading (that's settingSources' job above) but does throw away the model's normal
      // tool-use framing for no benefit here.
      systemPrompt: { type: "preset", preset: "claude_code", append: SYSTEM_PROMPT },
      outputFormat: {
        type: "json_schema",
        schema: request.outputSchema,
      },
      ...(this.options.model ? { model: this.options.model } : {}),
      ...(this.sessionId ? { resume: this.sessionId } : {}),
    };

    let result: SDKResultMessage | null = null;
    try {
      for await (const message of query({ prompt: request.prompt, options: sdkOptions })) {
        if (message.type === "result") {
          result = message;
        }
      }
    } finally {
      request.signal.removeEventListener("abort", relayAbort);
    }

    if (!result) {
      throw new Error("Claude ended without returning a result message.");
    }

    this.sessionId = result.session_id;
    this.spentUsd += result.total_cost_usd;

    if (result.subtype !== "success" || result.is_error) {
      const details = result.subtype === "success" ? result.result : result.errors.join("; ");
      throw new Error(`Claude ${result.subtype}: ${details || "unknown error"}`);
    }

    return {
      text: result.result,
      structured: result.structured_output ?? null,
      sessionId: this.sessionId,
      costUsd: result.total_cost_usd,
      metadata: {
        turns: result.num_turns,
        terminalReason: result.terminal_reason ?? null,
        permissionDenials: result.permission_denials,
      },
    };
  }
}
