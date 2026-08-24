import type { AgentProvider, ProviderRequest, ProviderResponse } from "./types.js";

export class MockProvider implements AgentProvider {
  readonly name: "claude" | "codex";
  readonly calls: ProviderRequest[] = [];

  constructor(name: "claude" | "codex") {
    this.name = name;
  }

  async invoke(request: ProviderRequest): Promise<ProviderResponse> {
    request.signal.throwIfAborted();
    this.calls.push(request);
    const structured = this.responseFor(request);
    return {
      text: JSON.stringify(structured),
      structured,
      sessionId: `${this.name}-mock-session`,
      costUsd: 0,
      metadata: { mock: true },
    };
  }

  private responseFor(request: ProviderRequest): unknown {
    switch (request.phase) {
      case "proposal":
        return {
          summary: `${this.name} proposes a minimal, testable change.`,
          assumptions: ["The repository is the source of truth."],
          steps: [
            {
              id: "step-1",
              title: "Inspect and implement",
              details: "Identify the smallest relevant change and preserve existing behavior.",
              verification: "Run configured checks and inspect the diff.",
            },
          ],
          risks: ["Unconfigured project tests may leave a verification gap."],
          successCriteria: ["The requested behavior is implemented with focused changes."],
        };
      case "critique":
        return {
          overallAssessment: "The proposal is viable and appropriately scoped.",
          strengths: ["It favors a small change and explicit verification."],
          concerns: ["The exact test command must come from trusted configuration."],
          corrections: ["Record test output in the run transcript."],
          revisedRecommendation: "Proceed with the scoped plan and host-run tests.",
        };
      case "synthesis":
        const writer = request.prompt.match(/designated writer, (claude|codex)/i)?.[1]?.toLowerCase();
        return {
          objective: "Complete the requested task with bounded, reviewable changes.",
          chosenApproach: "Use one writer, one read-only reviewer, and host-run verification.",
          assumptions: ["Only the designated writer edits files."],
          steps: [
            {
              id: "step-1",
              title: "Implement the approved change",
              details: "Make only the files and edits necessary for the task.",
              owner: writer === "codex" ? "codex" : "claude",
              verification: "Run configured tests and request independent review.",
            },
          ],
          risks: ["A missing test command reduces automated confidence."],
          successCriteria: ["The reviewer approves and configured tests pass."],
        };
      case "implementation":
      case "fix":
        return {
          summary: `Mock ${this.name} completed the requested ${request.phase}.`,
          filesChanged: [],
          testsRecommended: ["Run the configured project checks."],
          remainingRisks: [],
        };
      case "review":
        return {
          approved: true,
          summary: `Mock ${this.name} approves the bounded change.`,
          findings: [],
          testsSuggested: [],
        };
    }
  }
}
