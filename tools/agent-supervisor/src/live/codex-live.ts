import { Codex, type ThreadOptions, type ThreadItem } from "@openai/codex-sdk";
import { assertSubscriptionOnlyBilling, subscriptionOnlyEnvironment } from "../billing-safety.js";
import type { LiveEvent } from "./claude-live.js";

export class CodexLiveSession {
  private threadId: string | null;
  private readonly codex: Codex;
  private readonly cwd: string;
  private readonly model: string | undefined;

  constructor(options: { cwd: string; model?: string; resumeThreadId?: string }) {
    assertSubscriptionOnlyBilling();
    this.cwd = options.cwd;
    this.model = options.model;
    this.threadId = options.resumeThreadId ?? null;
    this.codex = new Codex({
      env: subscriptionOnlyEnvironment(),
      config: { service_tier: "default", features: { fast_mode: false } },
    });
  }

  async *send(prompt: string, signal: AbortSignal): AsyncGenerator<LiveEvent> {
    yield { kind: "turn_start" };

    const threadOptions: ThreadOptions = {
      workingDirectory: this.cwd,
      sandboxMode: "read-only",
      approvalPolicy: "never",
      networkAccessEnabled: false,
      webSearchMode: "disabled",
      ...(this.model ? { model: this.model } : {}),
    };

    const thread = this.threadId
      ? this.codex.resumeThread(this.threadId, threadOptions)
      : this.codex.startThread(threadOptions);

    try {
      const streamed = await thread.runStreamed(prompt, { signal });
      for await (const event of streamed.events) {
        if (event.type === "thread.started") {
          this.threadId = event.thread_id;
        } else if (event.type === "item.completed") {
          const described = describeItem(event.item);
          if (described) yield described;
        } else if (event.type === "turn.completed") {
          this.threadId = thread.id ?? this.threadId;
          yield { kind: "turn_end", costUsd: null };
        } else if (event.type === "turn.failed") {
          yield { kind: "error", message: event.error.message };
          yield { kind: "turn_end", costUsd: null };
        } else if (event.type === "error") {
          yield { kind: "error", message: (event as { message?: string }).message ?? "Codex stream error." };
        }
      }
    } catch (error) {
      yield { kind: "error", message: error instanceof Error ? error.message : String(error) };
      yield { kind: "turn_end", costUsd: null };
    }
  }
}

function describeItem(item: ThreadItem): LiveEvent | null {
  switch (item.type) {
    case "agent_message":
      return item.text ? { kind: "text", text: item.text } : null;
    case "reasoning":
      return item.text ? { kind: "text", text: item.text } : null;
    case "command_execution":
      return { kind: "tool", label: `$ ${item.command}` };
    case "file_change":
      return { kind: "tool", label: `Patch: ${item.changes.map((change) => `${change.kind} ${change.path}`).join(", ")}` };
    case "mcp_tool_call":
      return { kind: "tool", label: `${item.server}.${item.tool}` };
    case "web_search":
      return { kind: "tool", label: `Search: ${item.query}` };
    case "todo_list":
      return null;
    case "error":
      return { kind: "error", message: item.message };
    default:
      return null;
  }
}
