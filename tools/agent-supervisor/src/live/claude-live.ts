import {
  query,
  type Options,
  type SDKMessage,
} from "@anthropic-ai/claude-agent-sdk";
import { assertSubscriptionOnlyBilling, assertSubscriptionSafeModels, subscriptionOnlyEnvironment } from "../billing-safety.js";

export type LiveEvent =
  | { kind: "turn_start" }
  | { kind: "text"; text: string }
  | { kind: "tool"; label: string }
  | { kind: "turn_end"; costUsd: number | null }
  | { kind: "error"; message: string };

const SYSTEM_PROMPT = `You are one of two AI participants (the other is Codex) in a live, ongoing
collaborative conversation with a human in the loop for every round. Each round, you may be shown
the other participant's most recent reply for context - read it, but you are not required to agree
with it. Speak plainly and concisely; this is a live chat, not a batch report. You may read the
repository (Read/Glob/Grep) but never edit, commit, push, or run shell/network commands from here -
this is a discussion/design session, not an implementation one.`;

export class ClaudeLiveSession {
  private sessionId: string | null;
  private readonly cwd: string;
  private readonly model: string | undefined;
  private readonly maxTurns: number;
  private totalCostUsd = 0;

  constructor(options: { cwd: string; model?: string; maxTurns?: number; resumeSessionId?: string }) {
    assertSubscriptionOnlyBilling();
    assertSubscriptionSafeModels(options.model);
    this.cwd = options.cwd;
    this.model = options.model;
    this.maxTurns = options.maxTurns ?? 50;
    this.sessionId = options.resumeSessionId ?? null;
  }

  get costSoFarUsd(): number {
    return this.totalCostUsd;
  }

  async *send(prompt: string, signal: AbortSignal): AsyncGenerator<LiveEvent> {
    yield { kind: "turn_start" };

    const abortController = new AbortController();
    const relay = (): void => abortController.abort(signal.reason);
    if (signal.aborted) relay();
    else signal.addEventListener("abort", relay, { once: true });

    const sdkOptions: Options = {
      abortController,
      cwd: this.cwd,
      maxTurns: this.maxTurns,
      permissionMode: "dontAsk",
      tools: ["Read", "Glob", "Grep"],
      allowedTools: ["Read", "Glob", "Grep"],
      disallowedTools: ["Bash", "Edit", "Write", "WebFetch", "WebSearch", "Agent"],
      settingSources: ["project"],
      skills: [],
      env: subscriptionOnlyEnvironment(),
      systemPrompt: { type: "preset", preset: "claude_code", append: SYSTEM_PROMPT },
      ...(this.model ? { model: this.model } : {}),
      ...(this.sessionId ? { resume: this.sessionId } : {}),
    };

    try {
      for await (const message of query({ prompt, options: sdkOptions }) as AsyncGenerator<SDKMessage>) {
        if (message.type === "assistant") {
          for (const block of message.message.content) {
            if (block.type === "text" && block.text) {
              yield { kind: "text", text: block.text };
            } else if (block.type === "tool_use") {
              yield { kind: "tool", label: describeToolUse(block.name, block.input) };
            }
          }
        } else if (message.type === "result") {
          this.sessionId = message.session_id;
          this.totalCostUsd += message.total_cost_usd;
          if (message.subtype !== "success" || message.is_error) {
            const details = message.subtype === "success" ? message.result : message.errors.join("; ");
            yield { kind: "error", message: `Claude ${message.subtype}: ${details || "unknown error"}` };
          }
          yield { kind: "turn_end", costUsd: message.total_cost_usd };
        }
      }
    } catch (error) {
      yield { kind: "error", message: error instanceof Error ? error.message : String(error) };
      yield { kind: "turn_end", costUsd: null };
    } finally {
      signal.removeEventListener("abort", relay);
    }
  }
}

function describeToolUse(name: string, input: unknown): string {
  if (name === "Read" && isRecord(input) && typeof input.file_path === "string") return `Read ${input.file_path}`;
  if (name === "Grep" && isRecord(input) && typeof input.pattern === "string") return `Grep ${input.pattern}`;
  if (name === "Glob" && isRecord(input) && typeof input.pattern === "string") return `Glob ${input.pattern}`;
  return name;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}
