import { Codex, type ThreadOptions } from "@openai/codex-sdk";
import { assertSubscriptionOnlyBilling, subscriptionOnlyEnvironment } from "../billing-safety.js";
import type { AgentProvider, ProviderRequest, ProviderResponse } from "./types.js";

export type CodexProviderOptions = {
  cwd: string;
  model?: string;
  // Seeds the thread this provider resumes on its first call -- see the identical option on
  // ClaudeProviderOptions for the reasoning and the caching/cost caveat. src/session-discovery.ts's
  // --resume-codex auto finds a real codex_vscode/CLI thread for this repo, distinct from any
  // thread this supervisor itself created on a previous run (tagged originator "codex_sdk_ts").
  resumeThreadId?: string;
};

export class CodexProvider implements AgentProvider {
  readonly name = "codex" as const;
  private readonly codex: Codex;
  private readonly cwd: string;
  private readonly model: string | undefined;
  private threadId: string | null;

  constructor(options: CodexProviderOptions) {
    // See the identical guard in ClaudeProvider's constructor - previously only enforced
    // once at the CLI entrypoint, so anything constructing a CodexProvider directly got no
    // preemptive billing protection.
    assertSubscriptionOnlyBilling();
    this.cwd = options.cwd;
    this.model = options.model;
    this.threadId = options.resumeThreadId ?? null;

    this.codex = new Codex({
      env: subscriptionOnlyEnvironment(),
      config: {
        service_tier: "default",
        features: { fast_mode: false },
      },
    });
  }

  async invoke(request: ProviderRequest): Promise<ProviderResponse> {
    const threadOptions: ThreadOptions = {
      workingDirectory: this.cwd,
      sandboxMode: request.access === "write" ? "workspace-write" : "read-only",
      approvalPolicy: "never",
      networkAccessEnabled: false,
      webSearchMode: "disabled",
      ...(this.model ? { model: this.model } : {}),
    };

    const thread = this.threadId
      ? this.codex.resumeThread(this.threadId, threadOptions)
      : this.codex.startThread(threadOptions);
    const result = await thread.run(request.prompt, {
      outputSchema: request.outputSchema,
      signal: request.signal,
    });

    this.threadId = thread.id ?? this.threadId;

    return {
      text: result.finalResponse,
      structured: null,
      sessionId: this.threadId,
      costUsd: null,
      metadata: {
        usage: result.usage,
        itemCount: result.items.length,
      },
    };
  }
}
