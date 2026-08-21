import { z } from "zod";
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

export class Orchestrator {
  private readonly options: OrchestratorOptions;
  private readonly providers: Record<"claude" | "codex", AgentProvider>;
  private calls = 0;
  private log!: RunLog;

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

    const [claudeProposal, codexProposal] = await Promise.all([
      this.call(this.options.claude, "proposal", "read", proposalPrompt(task, "Claude"), ProposalSchema),
      this.call(this.options.codex, "proposal", "read", proposalPrompt(task, "Codex"), ProposalSchema),
    ]);

    const [claudeCritique, codexCritique] = await Promise.all([
      this.call(
        this.options.claude,
        "critique",
        "read",
        critiquePrompt(task, "Codex", codexProposal),
        CritiqueSchema,
      ),
      this.call(
        this.options.codex,
        "critique",
        "read",
        critiquePrompt(task, "Claude", claudeProposal),
        CritiqueSchema,
      ),
    ]);

    const leader = this.providers[config.leader];
    const plan = await this.call(
      leader,
      "synthesis",
      "read",
      synthesisPrompt(
        task,
        claudeProposal,
        codexProposal,
        claudeCritique,
        codexCritique,
        config.writer,
      ),
      PlanSchema,
    );
    const planPath = await this.log.artifact(
      "plan.md",
      renderPlan(task, plan, claudeProposal, codexProposal, claudeCritique, codexCritique),
    );
    this.progress("synthesis", config.leader, "completed", `Collaboration plan ready: ${planPath}`);

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
    const approved = this.options.autoApprove
      ? true
      : await this.options.confirm(`Approve this plan and grant ${config.writer} workspace write access?`);
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
      data: { automatic: this.options.autoApprove, writer: config.writer },
    });
    this.progress(
      "approval",
      "user",
      "completed",
      `Plan approved. ${capitalize(config.writer)} has bounded workspace write access.`,
    );

    const writer = this.providers[config.writer];
    const reviewer = this.providers[config.writer === "claude" ? "codex" : "claude"];
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
      const review = await this.call(
        reviewer,
        "review",
        "read",
        reviewPrompt(task, plan, implementation, tests, repositoryEvidence, round),
        ReviewSchema,
      );
      reviews.push(review);

      if (review.approved) {
        this.progress("review", reviewer.name, "completed", `Review round ${round} approved the changes.`);
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
          `Review round ${round} requested corrections; returning them to ${capitalize(writer.name)}.`,
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

  private async call<T>(
    provider: AgentProvider,
    phase: ProviderPhase,
    access: AccessMode,
    prompt: string,
    schema: z.ZodType<T>,
  ): Promise<T> {
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

    try {
      const response: ProviderResponse = await provider.invoke({
        phase,
        prompt,
        access,
        outputSchema: jsonSchemaFor(schema),
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
      const message = error instanceof Error ? error.message : String(error);
      await this.log.event({
        phase,
        actor: provider.name,
        kind: "provider.error",
        data: { message },
      });
      this.progress(
        phase,
        provider.name,
        "failed",
        `${capitalize(provider.name)} failed while ${action}: ${message}`,
        Math.max(0, Math.round((Date.now() - startedAt) / 1000)),
      );
      throw error;
    } finally {
      clearInterval(heartbeat);
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
  claudeCritique: Critique,
  codexCritique: Critique,
): string {
  return `# Collaboration plan\n\n## Task\n\n${task}\n\n## Final plan\n\n\`\`\`json\n${JSON.stringify(plan, null, 2)}\n\`\`\`\n\n## Claude proposal\n\n\`\`\`json\n${JSON.stringify(claudeProposal, null, 2)}\n\`\`\`\n\n## Codex proposal\n\n\`\`\`json\n${JSON.stringify(codexProposal, null, 2)}\n\`\`\`\n\n## Cross-critiques\n\n### Claude on Codex\n\n\`\`\`json\n${JSON.stringify(claudeCritique, null, 2)}\n\`\`\`\n\n### Codex on Claude\n\n\`\`\`json\n${JSON.stringify(codexCritique, null, 2)}\n\`\`\`\n`;
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
