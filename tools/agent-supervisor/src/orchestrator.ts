import { z } from "zod";
import { isUsageOrBillingLimit } from "./billing-safety.js";
import {
  CritiqueSchema,
  ImplementationReportSchema,
  PlanSchema,
  ProposalSchema,
  ReviewSchema,
  jsonSchemaFor,
  parseJsonText,
  type CollaborationPlan,
  type Critique,
  type ImplementationReport,
  type Proposal,
  type Review,
  type RunStatus,
} from "./domain.js";
import type { SupervisorConfig } from "./config.js";
import {
  ensureGitRepository,
  gitDiff,
  gitPatch,
  gitStatus,
  runTestCommand,
  type TestResult,
} from "./host.js";
import {
  critiquePrompt,
  fixPrompt,
  implementationPrompt,
  proposalPrompt,
  reviewPrompt,
  soloPlanPrompt,
  synthesisPrompt,
} from "./prompts.js";
import { RunLog } from "./run-log.js";
import type { AgentProvider, AccessMode, ProviderPhase, ProviderResponse } from "./providers/types.js";

export type OrchestratorOptions = {
  task: string;
  config: SupervisorConfig;
  claude: AgentProvider;
  codex: AgentProvider;
  allowWrite: boolean;
  allowDirty: boolean;
  autoApprove: boolean;
  confirm: (question: string) => Promise<boolean>;
  onProgress?: (update: ProgressUpdate) => void;
  progressHeartbeatMs?: number;
};

export type ProgressUpdate = {
  timestamp: string;
  phase: string;
  actor: "supervisor" | "claude" | "codex" | "user";
  state: "started" | "working" | "completed" | "failed" | "info";
  message: string;
  elapsedSeconds?: number;
};

export type OrchestratorResult = {
  status: RunStatus;
  runDirectory: string;
  plan: CollaborationPlan;
  implementation: ImplementationReport | null;
  reviews: Review[];
  tests: TestResult[];
};

type ProviderName = "claude" | "codex";

// Thrown by call() instead of a bare Error so callers can tell a genuine billing/session-limit
// stop (fatal -- must abort the whole run, matches the documented billing-safety contract) apart
// from an ordinary hiccup (turn limit, timeout, a malformed response) that should only take that
// one provider out of *this* run, not discard the other provider's independent, still-good work.
class ProviderCallError extends Error {
  readonly provider: ProviderName;
  readonly phase: ProviderPhase;
  readonly fatal: boolean;

  constructor(provider: ProviderName, phase: ProviderPhase, fatal: boolean, cause: Error) {
    super(cause.message, { cause });
    this.name = "ProviderCallError";
    this.provider = provider;
    this.phase = phase;
    this.fatal = fatal;
  }
}

function errorMessage(reason: unknown): string {
  return reason instanceof Error ? reason.message : String(reason);
}

export class Orchestrator {
  private readonly options: OrchestratorOptions;
  private readonly providers: Record<ProviderName, AgentProvider>;
  private calls = 0;
  private log!: RunLog;
  private readonly abortController = new AbortController();
  // Providers that have failed at least one non-fatal call this run. Consulted whenever a
  // phase needs to pick a leader/writer/reviewer, so a provider already struggling this run
  // isn't handed the next role by default -- see resolveProvider().
  private readonly unavailable = new Set<ProviderName>();

  constructor(options: OrchestratorOptions) {
    this.options = options;
    this.providers = { claude: options.claude, codex: options.codex };
  }

  async run(): Promise<OrchestratorResult> {
    const { config, task } = this.options;
    this.progress("startup", "supervisor", "started", "Checking the target repository.");
    const requiredCallLimit = 5 + 2 * config.maxReviewRounds;
    if (config.maxProviderCalls < requiredCallLimit) {
      throw new Error(
        `maxProviderCalls must be at least ${requiredCallLimit} for ` +
          `${config.maxReviewRounds} review round(s).`,
      );
    }
    await ensureGitRepository(config.targetRoot);
    const initialStatus = await gitStatus(config.targetRoot);
    if (this.options.allowWrite && initialStatus && !this.options.allowDirty) {
      throw new Error(
        "The target worktree has existing changes. Commit/stash them or rerun with --allow-dirty " +
          "after reviewing the risk of overlapping edits.",
      );
    }

    this.log = await RunLog.create(config.targetRoot);
    this.progress(
      "startup",
      "supervisor",
      "info",
      `Run started. Live event log: ${this.log.eventsPath}`,
    );
    await this.log.event({
      phase: "startup",
      actor: "supervisor",
      kind: "run.started",
      data: {
        task,
        config,
        allowWrite: this.options.allowWrite,
        allowDirty: this.options.allowDirty,
        initialGitStatus: initialStatus,
      },
    });

    try {
      return await this.runPhases();
    } catch (error) {
      // Every prior failure mode in this file threw straight out of run(), so a failed run
      // left nothing under .agent-runs/ beyond the raw events.jsonl -- "fails with no result"
      // from the outside even though real provider spend already happened. Always leave a
      // human-readable artifact, and point to it from the thrown message itself so it's
      // visible even through index.ts's generic top-level catch.
      const message = errorMessage(error);
      await this.log.artifact(
        "failure.md",
        `# Agent supervisor run failed\n\n- Task: ${task}\n- Error: ${message}\n`,
      );
      this.progress("completion", "supervisor", "failed", `Run failed: ${message}`);
      throw new Error(`${message}\nRun artifacts: ${this.log.directory}`, { cause: error });
    }
  }

  private async runPhases(): Promise<OrchestratorResult> {
    const { config, task } = this.options;

    const proposals = await this.runPaired(
      "proposal",
      () => this.call(this.options.claude, "proposal", "read", proposalPrompt(task, "Claude"), ProposalSchema),
      () => this.call(this.options.codex, "proposal", "read", proposalPrompt(task, "Codex"), ProposalSchema),
    );

    let plan: CollaborationPlan;
    let claudeProposal: Proposal | null = proposals.claude;
    let codexProposal: Proposal | null = proposals.codex;
    let claudeCritique: Critique | null = null;
    let codexCritique: Critique | null = null;

    if (claudeProposal && codexProposal) {
      const critiques = await this.runPaired(
        "critique",
        () => this.call(this.options.claude, "critique", "read", critiquePrompt(task, "Codex", codexProposal!), CritiqueSchema),
        () => this.call(this.options.codex, "critique", "read", critiquePrompt(task, "Claude", claudeProposal!), CritiqueSchema),
      );
      claudeCritique = critiques.claude;
      codexCritique = critiques.codex;

      const leaderName = this.resolveProvider(config.leader);
      const leader = this.providers[leaderName];
      plan = await this.call(
        leader,
        "synthesis",
        "read",
        synthesisPrompt(task, claudeProposal, codexProposal, claudeCritique, codexCritique, this.resolveProvider(config.writer)),
        PlanSchema,
      );
      const planPath = await this.log.artifact(
        "plan.md",
        renderPlan(task, plan, claudeProposal, codexProposal, claudeCritique, codexCritique),
      );
      this.progress("synthesis", leaderName, "completed", `Collaboration plan ready: ${planPath}`);
    } else {
      // Exactly one proposal came back (runPaired() already threw if both failed). There is
      // nothing for a peer to critique, so skip straight to a solo plan from whichever
      // provider actually produced a proposal, regardless of the configured leader/writer --
      // handing either role to a provider that just failed its proposal call would just
      // reproduce the same failure one phase later.
      const soloName: ProviderName = claudeProposal ? "claude" : "codex";
      const soloProposal = (claudeProposal ?? codexProposal)!;
      const solo = this.providers[soloName];
      plan = await this.call(
        solo,
        "synthesis",
        "read",
        soloPlanPrompt(task, soloProposal, soloName),
        PlanSchema,
      );
      const planPath = await this.log.artifact(
        "plan.md",
        renderSoloPlan(task, plan, soloName, soloProposal),
      );
      this.progress("synthesis", soloName, "completed", `Collaboration plan ready (solo -- only ${soloName} was available): ${planPath}`);
    }

    if (!this.options.allowWrite) {
      this.progress(
        "approval",
        "supervisor",
        "info",
        "Planning complete. Write access was not enabled, so no files will be changed.",
      );
      await this.log.event({
        phase: "approval",
        actor: "supervisor",
        kind: "run.stopped_read_only",
        data: { reason: "Write access was not enabled." },
      });
      await this.writeSummary("planned", plan, null, [], []);
      return {
        status: "planned",
        runDirectory: this.log.directory,
        plan,
        implementation: null,
        reviews: [],
        tests: [],
      };
    }

    if (!this.options.autoApprove) {
      this.progress("approval", "supervisor", "working", "Waiting for plan approval.");
    }
    const writerName = this.resolveProvider(config.writer);
    const approved = this.options.autoApprove
      ? true
      : await this.options.confirm(`Approve this plan and grant ${writerName} workspace write access?`);
    if (!approved) {
      this.progress("approval", "user", "info", "Plan declined; no files were changed.");
      await this.log.event({
        phase: "approval",
        actor: "user",
        kind: "plan.declined",
        data: {},
      });
      await this.writeSummary("planned", plan, null, [], []);
      return {
        status: "planned",
        runDirectory: this.log.directory,
        plan,
        implementation: null,
        reviews: [],
        tests: [],
      };
    }

    await this.log.event({
      phase: "approval",
      actor: "user",
      kind: "plan.approved",
      data: { automatic: this.options.autoApprove, writer: writerName },
    });
    this.progress(
      "approval",
      "user",
      "completed",
      `Plan approved. ${capitalize(writerName)} has bounded workspace write access.`,
    );

    const writer = this.providers[writerName];
    const reviewerName: ProviderName = writerName === "claude" ? "codex" : "claude";
    const reviewer = this.providers[reviewerName];
    let implementation = await this.call(
      writer,
      "implementation",
      "write",
      implementationPrompt(task, plan),
      ImplementationReportSchema,
    );
    let tests = await this.runTests();
    const reviews: Review[] = [];

    for (let round = 1; round <= config.maxReviewRounds; round += 1) {
      this.progress(
        "review",
        "supervisor",
        "info",
        `Starting review round ${round}/${config.maxReviewRounds}.`,
      );
      const repositoryEvidence = {
        status: await gitStatus(config.targetRoot),
        diffStat: await gitDiff(config.targetRoot),
        patch: await gitPatch(config.targetRoot),
      };

      let review: Review;
      try {
        review = await this.call(
          reviewer,
          "review",
          "read",
          reviewPrompt(task, plan, implementation, tests, repositoryEvidence, round),
          ReviewSchema,
        );
      } catch (error) {
        if (error instanceof ProviderCallError && !error.fatal) {
          // The independent reviewer flaked (not a billing stop) -- report the gap honestly
          // instead of throwing away a completed implementation and every dollar already
          // spent producing it. The caller sees needs_attention and a clear reason why no
          // independent review exists, rather than "no result" with real spend behind it.
          review = {
            approved: false,
            summary: `Independent review by ${reviewerName} could not be completed: ${error.message}`,
            findings: [
              {
                severity: "high",
                file: "",
                issue: `${capitalize(reviewerName)}'s review call failed: ${error.message}`,
                recommendation: "Rerun review manually, or rerun the supervisor once the reviewer provider is available again.",
              },
            ],
            testsSuggested: [],
          };
          reviews.push(review);
          await this.writeSummary("needs_attention", plan, implementation, reviews, tests);
          return {
            status: "needs_attention",
            runDirectory: this.log.directory,
            plan,
            implementation,
            reviews,
            tests,
          };
        }
        throw error;
      }

      const failedTests = tests.filter((test) => test.timedOut || test.exitCode !== 0);
      if (failedTests.length > 0) {
        review = {
          ...review,
          approved: false,
          summary: review.approved
            ? `Supervisor overrode reviewer approval because ${failedTests.length} required test command(s) failed or timed out. ${review.summary}`
            : review.summary,
          findings: [
            ...review.findings,
            {
              severity: "critical",
              file: "",
              issue: `${failedTests.length} required test command(s) failed or timed out: ${failedTests.map((test) => test.command).join(", ")}`,
              recommendation: "Fix the implementation or test environment and rerun every required test successfully before approval.",
            },
          ],
        };
        this.progress(
          "review",
          "supervisor",
          "failed",
          `Approval blocked: ${failedTests.length} required test command(s) failed or timed out.`,
        );
      }
      reviews.push(review);

      if (review.approved) {
        this.progress("review", reviewerName, "completed", `Review round ${round} approved the changes.`);
        await this.writeSummary("approved", plan, implementation, reviews, tests);
        return {
          status: "approved",
          runDirectory: this.log.directory,
          plan,
          implementation,
          reviews,
          tests,
        };
      }

      if (round < config.maxReviewRounds) {
        this.progress(
          "fix",
          "supervisor",
          "info",
          `Review round ${round} requested corrections; returning them to ${capitalize(writerName)}.`,
        );
        implementation = await this.call(
          writer,
          "fix",
          "write",
          fixPrompt(task, plan, review, round),
          ImplementationReportSchema,
        );
        tests = await this.runTests();
      }
    }

    await this.writeSummary("needs_attention", plan, implementation, reviews, tests);
    return {
      status: "needs_attention",
      runDirectory: this.log.directory,
      plan,
      implementation,
      reviews,
      tests,
    };
  }

  // Runs a claude+codex call pair concurrently and never lets one provider's ordinary failure
  // erase the other's completed work. Only rethrows (aborting the run) when the shared
  // abortController is already tripped, which call() only does for a genuine billing/session
  // stop -- see the ProviderCallError.fatal check there.
  private async runPaired<T>(
    label: string,
    claudeCall: () => Promise<T>,
    codexCall: () => Promise<T>,
  ): Promise<{ claude: T | null; codex: T | null }> {
    const [claudeResult, codexResult] = await Promise.allSettled([claudeCall(), codexCall()]);

    if (this.abortController.signal.aborted) {
      const reason = this.abortController.signal.reason;
      throw reason instanceof Error ? reason : new Error(errorMessage(reason));
    }

    if (claudeResult.status === "rejected") this.unavailable.add("claude");
    if (codexResult.status === "rejected") this.unavailable.add("codex");

    if (claudeResult.status === "rejected" && codexResult.status === "rejected") {
      throw new Error(
        `Both Claude and Codex failed during ${label}: ` +
          `Claude: ${errorMessage(claudeResult.reason)}; Codex: ${errorMessage(codexResult.reason)}`,
      );
    }

    if (claudeResult.status === "rejected") {
      this.progress(
        label,
        "supervisor",
        "info",
        `Claude was unavailable during ${label} (${errorMessage(claudeResult.reason)}); continuing with Codex alone.`,
      );
    } else if (codexResult.status === "rejected") {
      this.progress(
        label,
        "supervisor",
        "info",
        `Codex was unavailable during ${label} (${errorMessage(codexResult.reason)}); continuing with Claude alone.`,
      );
    }

    return {
      claude: claudeResult.status === "fulfilled" ? claudeResult.value : null,
      codex: codexResult.status === "fulfilled" ? codexResult.value : null,
    };
  }

  // Falls back to the other provider when the configured one has already failed a call this
  // run, so a struggling provider doesn't keep getting handed leader/writer/reviewer roles it
  // just demonstrated it can't fill. Only meaningful once both providers have been attempted
  // at least once (runPhases()'s solo-mode branch handles the "only one ever worked" case
  // itself, before this is consulted).
  private resolveProvider(preferred: ProviderName): ProviderName {
    if (!this.unavailable.has(preferred)) return preferred;
    const fallback: ProviderName = preferred === "claude" ? "codex" : "claude";
    if (this.unavailable.has(fallback)) return preferred; // both struggling; keep the configured choice
    this.progress(
      "supervisor",
      "supervisor",
      "info",
      `${capitalize(preferred)} was unavailable earlier this run; using ${fallback} instead.`,
    );
    return fallback;
  }

  private async call<T>(
    provider: AgentProvider,
    phase: ProviderPhase,
    access: AccessMode,
    prompt: string,
    schema: z.ZodType<T>,
  ): Promise<T> {
    this.abortController.signal.throwIfAborted();
    this.calls += 1;
    if (this.calls > this.options.config.maxProviderCalls) {
      throw new Error(`Provider call limit exceeded (${this.options.config.maxProviderCalls}).`);
    }

    await this.log.event({
      phase,
      actor: provider.name,
      kind: "provider.request",
      data: { access, prompt },
    });

    const startedAt = Date.now();
    const action = phaseAction(phase);
    const accessLabel = access === "read" ? "read-only" : "bounded workspace write";
    this.progress(
      phase,
      provider.name,
      "started",
      `${capitalize(provider.name)} started ${action} (${accessLabel} access).`,
    );
    const heartbeat = setInterval(() => {
      const elapsedSeconds = Math.max(1, Math.round((Date.now() - startedAt) / 1000));
      this.progress(
        phase,
        provider.name,
        "working",
        `${capitalize(provider.name)} is still ${action}.`,
        elapsedSeconds,
      );
    }, this.options.progressHeartbeatMs ?? 15_000);
    heartbeat.unref();

    // Per-call signal = shared run-level abort OR this call's own wall-clock timeout. Neither
    // provider SDK previously had a wall-clock ceiling of its own (Claude's maxTurns/maxBudgetUsd
    // bound cost, not time; Codex had no bound at all), so a stalled call could otherwise sit
    // indefinitely against the subscription's shared session window.
    const perCallController = new AbortController();
    const relayAbort = (): void => perCallController.abort(this.abortController.signal.reason);
    if (this.abortController.signal.aborted) relayAbort();
    else this.abortController.signal.addEventListener("abort", relayAbort, { once: true });
    const timeoutSeconds = this.options.config.providerCallTimeoutSeconds;
    const timeoutHandle = setTimeout(() => {
      perCallController.abort(new Error(`Provider call timed out after ${timeoutSeconds}s.`));
    }, timeoutSeconds * 1000);
    timeoutHandle.unref?.();

    try {
      const response: ProviderResponse = await provider.invoke({
        phase,
        prompt,
        access,
        outputSchema: jsonSchemaFor(schema),
        signal: perCallController.signal,
      });

      const payload = response.structured ?? parseJsonText(response.text);
      const parsed = schema.parse(payload);
      await this.log.event({
        phase,
        actor: provider.name,
        kind: "provider.response",
        data: { ...response, structured: parsed },
      });
      this.progress(
        phase,
        provider.name,
        "completed",
        `${capitalize(provider.name)} finished ${action}.`,
        Math.max(0, Math.round((Date.now() - startedAt) / 1000)),
      );
      return parsed;
    } catch (error) {
      const message = errorMessage(error);
      // Only a genuine billing/session-limit stop aborts the *whole run* (the documented
      // billing-safety contract: no further phases, no further spend anywhere). An ordinary
      // failure -- a turn limit, a malformed response, this call's own timeout -- only takes
      // this one provider out of this one call; runPaired()/resolveProvider() decide from there
      // whether the run can still proceed with the other provider.
      const fatal = isUsageOrBillingLimit(message);
      if (fatal && !this.abortController.signal.aborted) {
        this.abortController.abort(error);
      }
      await this.log.event({
        phase,
        actor: provider.name,
        kind: "provider.error",
        data: { message, fatal },
      });
      this.progress(
        phase,
        provider.name,
        "failed",
        `${capitalize(provider.name)} failed while ${action}: ${message}`,
        Math.max(0, Math.round((Date.now() - startedAt) / 1000)),
      );
      if (fatal) {
        this.progress(
          phase,
          "supervisor",
          "failed",
          "Usage or billing limit detected. All active provider work was cancelled; no further phases will run.",
        );
      }
      throw new ProviderCallError(provider.name, phase, fatal, error instanceof Error ? error : new Error(message));
    } finally {
      clearInterval(heartbeat);
      clearTimeout(timeoutHandle);
      this.abortController.signal.removeEventListener("abort", relayAbort);
    }
  }

  private async runTests(): Promise<TestResult[]> {
    const { config } = this.options;
    const results: TestResult[] = [];
    if (config.testCommands.length === 0) {
      this.progress("verification", "supervisor", "info", "No host test commands are configured.");
    }
    for (const command of config.testCommands) {
      const startedAt = Date.now();
      this.progress("verification", "supervisor", "started", `Running test: ${command}`);
      const result = await runTestCommand(
        command,
        config.targetRoot,
        config.testTimeoutSeconds * 1000,
        config.maxTestOutputChars,
      );
      results.push(result);
      await this.log.event({
        phase: "verification",
        actor: "supervisor",
        kind: "test.completed",
        data: result,
      });
      this.progress(
        "verification",
        "supervisor",
        result.exitCode === 0 && !result.timedOut ? "completed" : "failed",
        `Test finished with exit code ${result.exitCode}${result.timedOut ? " (timed out)" : ""}: ${command}`,
        Math.max(0, Math.round((Date.now() - startedAt) / 1000)),
      );
    }
    return results;
  }

  private async writeSummary(
    status: RunStatus,
    plan: CollaborationPlan,
    implementation: ImplementationReport | null,
    reviews: Review[],
    tests: TestResult[],
  ): Promise<void> {
    const finalStatus = await gitStatus(this.options.config.targetRoot);
    const diff = await gitDiff(this.options.config.targetRoot);
    await this.log.event({
      phase: "completion",
      actor: "supervisor",
      kind: "run.completed",
      data: { status, finalGitStatus: finalStatus, diffStat: diff },
    });
    await this.log.artifact(
      "summary.md",
      renderSummary(this.options.task, status, plan, implementation, reviews, tests, finalStatus, diff),
    );
    this.progress(
      "completion",
      "supervisor",
      "completed",
      `Run finished with status ${status}. Artifacts: ${this.log.directory}`,
    );
  }

  private progress(
    phase: string,
    actor: ProgressUpdate["actor"],
    state: ProgressUpdate["state"],
    message: string,
    elapsedSeconds?: number,
  ): void {
    try {
      this.options.onProgress?.({
        timestamp: new Date().toISOString(),
        phase,
        actor,
        state,
        message,
        ...(elapsedSeconds === undefined ? {} : { elapsedSeconds }),
      });
    } catch {
      // Status rendering must never interrupt the supervised work.
    }
  }
}

function phaseAction(phase: ProviderPhase): string {
  switch (phase) {
    case "proposal":
      return "analyzing the repository and drafting a proposal";
    case "critique":
      return "reviewing the other agent's proposal";
    case "synthesis":
      return "combining both analyses into one plan";
    case "implementation":
      return "implementing the approved plan";
    case "fix":
      return "applying the requested corrections";
    case "review":
      return "reviewing the implementation";
  }
}

function capitalize(value: string): string {
  return `${value.charAt(0).toUpperCase()}${value.slice(1)}`;
}

function renderPlan(
  task: string,
  plan: CollaborationPlan,
  claudeProposal: Proposal,
  codexProposal: Proposal,
  claudeCritique: Critique | null,
  codexCritique: Critique | null,
): string {
  return `# Collaboration plan\n\n## Task\n\n${task}\n\n## Final plan\n\n\`\`\`json\n${JSON.stringify(plan, null, 2)}\n\`\`\`\n\n## Claude proposal\n\n\`\`\`json\n${JSON.stringify(claudeProposal, null, 2)}\n\`\`\`\n\n## Codex proposal\n\n\`\`\`json\n${JSON.stringify(codexProposal, null, 2)}\n\`\`\`\n\n## Cross-critiques\n\n### Claude on Codex\n\n${claudeCritique ? `\`\`\`json\n${JSON.stringify(claudeCritique, null, 2)}\n\`\`\`` : "_Not available this run._"}\n\n### Codex on Claude\n\n${codexCritique ? `\`\`\`json\n${JSON.stringify(codexCritique, null, 2)}\n\`\`\`` : "_Not available this run._"}\n`;
}

function renderSoloPlan(task: string, plan: CollaborationPlan, soloProvider: ProviderName, proposal: Proposal): string {
  return `# Collaboration plan (solo)\n\n## Task\n\n${task}\n\nOnly ${soloProvider} produced a usable proposal this run; the plan below comes from ${soloProvider} alone, with no peer critique.\n\n## Final plan\n\n\`\`\`json\n${JSON.stringify(plan, null, 2)}\n\`\`\`\n\n## ${capitalize(soloProvider)} proposal\n\n\`\`\`json\n${JSON.stringify(proposal, null, 2)}\n\`\`\`\n`;
}

function renderSummary(
  task: string,
  status: RunStatus,
  plan: CollaborationPlan,
  implementation: ImplementationReport | null,
  reviews: Review[],
  tests: TestResult[],
  gitStatusOutput: string,
  diff: string,
): string {
  return `# Agent supervisor result\n\n- Status: **${status}**\n- Task: ${task}\n\n## Plan\n\n${plan.chosenApproach}\n\n## Implementation\n\n${implementation?.summary ?? "No write phase was run."}\n\n## Review\n\n${reviews.at(-1)?.summary ?? "No review phase was run."}\n\n## Tests\n\n${tests.length ? tests.map((test) => `- \`${test.command}\`: exit ${test.exitCode}${test.timedOut ? " (timed out)" : ""}`).join("\n") : "No test commands were configured."}\n\n## Git status\n\n\`\`\`text\n${gitStatusOutput || "clean"}\n\`\`\`\n\n## Diff stat\n\n\`\`\`text\n${diff || "No tracked diff."}\n\`\`\`\n`;
}
