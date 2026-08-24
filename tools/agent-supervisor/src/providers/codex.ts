import { Codex, type ThreadOptions } from "@openai/codex-sdk";
import { assertSubscriptionOnlyBilling, subscriptionOnlyEnvironment } from "../billing-safety.js";
import type { AgentProvider, ProviderRequest, ProviderResponse } from "./types.js";

export type CodexProviderOptions = {
  cwd: string;
  model?: string;
};

export class CodexProvider implements AgentProvider {
  readonly name = "codex" as const;
  private readonly codex: Codex;
  private readonly cwd: string;
  private readonly model: string | undefined;
  private threadId: string | null = null;

  constructor(options: CodexProviderOptions) {
    // See the identical guard in ClaudeProvider's constructor - previously only enforced
    // once at the CLI entrypoint, so anything constructing a CodexProvider directly got no
    // preemptive billing protection.
    assertSubscriptionOnlyBilling();
    this.cwd = options.cwd;
    this.model = options.model;

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
