import {
  query,
  type Options,
  type SDKResultMessage,
} from "@anthropic-ai/claude-agent-sdk";
import type { AgentProvider, ProviderRequest, ProviderResponse } from "./types.js";

const READ_TOOLS = ["Read", "Glob", "Grep"];
const WRITE_TOOLS = [...READ_TOOLS, "Edit", "Write"];

const SYSTEM_PROMPT = `You are one participant in a bounded two-agent engineering workflow.
Follow the requested phase exactly. Never commit, push, deploy, install dependencies, use the
network, access paths outside the working directory, or ask another agent to work. In read mode,
inspect and reason only. In write mode, edit only files necessary for the approved plan. The host
process runs tests separately. Return only the requested structured output.`;

export type ClaudeProviderOptions = {
  cwd: string;
  maxTurns: number;
  maxBudgetUsdPerCall: number;
  maxTotalBudgetUsd: number;
  model?: string;
};

export class ClaudeProvider implements AgentProvider {
  readonly name = "claude" as const;
  private readonly options: ClaudeProviderOptions;
  private sessionId: string | null = null;
  private spentUsd = 0;

  constructor(options: ClaudeProviderOptions) {
    this.options = options;
  }

  async invoke(request: ProviderRequest): Promise<ProviderResponse> {
    const remainingBudget = this.options.maxTotalBudgetUsd - this.spentUsd;
    if (remainingBudget <= 0) {
      throw new Error(
        `Claude budget exhausted at $${this.spentUsd.toFixed(2)} ` +
          `(limit $${this.options.maxTotalBudgetUsd.toFixed(2)}).`,
      );
    }

    const tools = request.access === "write" ? WRITE_TOOLS : READ_TOOLS;
    const sdkOptions: Options = {
      cwd: this.options.cwd,
      maxTurns: this.options.maxTurns,
      maxBudgetUsd: Math.min(this.options.maxBudgetUsdPerCall, remainingBudget),
      permissionMode: "dontAsk",
      tools,
      allowedTools: tools,
      disallowedTools: ["Bash", "WebFetch", "WebSearch", "Agent"],
      settingSources: [],
      skills: [],
      systemPrompt: SYSTEM_PROMPT,
      outputFormat: {
        type: "json_schema",
        schema: request.outputSchema,
      },
      ...(this.options.model ? { model: this.options.model } : {}),
      ...(this.sessionId ? { resume: this.sessionId } : {}),
    };

    let result: SDKResultMessage | null = null;
    for await (const message of query({ prompt: request.prompt, options: sdkOptions })) {
      if (message.type === "result") {
        result = message;
      }
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
