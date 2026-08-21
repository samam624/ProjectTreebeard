import { Codex, type ThreadOptions } from "@openai/codex-sdk";
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
    this.cwd = options.cwd;
    this.model = options.model;

    const apiKey = process.env.CODEX_API_KEY ?? process.env.OPENAI_API_KEY;
    this.codex = new Codex({
      ...(apiKey ? { apiKey } : {}),
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
