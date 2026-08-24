export type AccessMode = "read" | "write";
export type ProviderPhase =
  | "proposal"
  | "critique"
  | "synthesis"
  | "implementation"
  | "fix"
  | "review";

export type ProviderRequest = {
  phase: ProviderPhase;
  prompt: string;
  access: AccessMode;
  outputSchema: Record<string, unknown>;
  signal: AbortSignal;
};

export type ProviderResponse = {
  text: string;
  structured: unknown | null;
  sessionId: string | null;
  costUsd: number | null;
  metadata: Record<string, unknown>;
};

export interface AgentProvider {
  readonly name: "claude" | "codex";
  invoke(request: ProviderRequest): Promise<ProviderResponse>;
}
