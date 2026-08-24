import { readFile } from "node:fs/promises";
import path from "node:path";
import { createInterface } from "node:readline/promises";
import { stdin as input, stdout as output } from "node:process";
import { parseArgs, type ParsedArgs } from "./cli-args.js";
import { assertSubscriptionOnlyBilling, assertSubscriptionSafeModels } from "./billing-safety.js";
import { loadConfig, SupervisorConfigSchema, type SupervisorConfig } from "./config.js";
import { doctor } from "./doctor.js";
import { ProviderNameSchema } from "./domain.js";
import { Orchestrator, type ProgressUpdate } from "./orchestrator.js";
import { ClaudeProvider } from "./providers/claude.js";
import { CodexProvider } from "./providers/codex.js";
import { MockProvider } from "./providers/mock.js";

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));
  if (args.command === "help") {
    printHelp();
    return;
  }

  const baseConfig = await loadConfig(lastValue(args, "config"));
  const config = applyOverrides(baseConfig, args);

  if (args.command === "doctor") {
    const checks = await doctor(config);
    for (const check of checks) {
      console.log(`${check.ok ? "PASS" : "FAIL"}  ${check.name}: ${check.detail}`);
    }
    if (checks.some((check) => !check.ok)) {
      process.exitCode = 1;
    }
    return;
  }

  const task = await resolveTask(args);
  if (!task) {
    throw new Error("Provide --task \"...\" or --task-file <path>.");
  }

  const mock = args.flags.has("mock");
  if (!mock) {
    assertSubscriptionOnlyBilling();
    assertSubscriptionSafeModels(process.env.CLAUDE_MODEL);
  }
  const claude = mock
    ? new MockProvider("claude")
    : new ClaudeProvider({
        cwd: config.targetRoot,
        maxTurns: config.claudeMaxTurns,
        maxBudgetUsdPerCall: config.claudeMaxBudgetUsdPerCall,
        maxTotalBudgetUsd: config.claudeMaxTotalBudgetUsd,
        ...(process.env.CLAUDE_MODEL ? { model: process.env.CLAUDE_MODEL } : {}),
      });
  const codex = mock
    ? new MockProvider("codex")
    : new CodexProvider({
        cwd: config.targetRoot,
        ...(process.env.CODEX_MODEL ? { model: process.env.CODEX_MODEL } : {}),
      });

  const result = await new Orchestrator({
    task,
    config,
    claude,
    codex,
    allowWrite: args.flags.has("allow-write"),
    allowDirty: args.flags.has("allow-dirty"),
    autoApprove: args.flags.has("yes"),
    confirm,
    ...(args.flags.has("quiet") ? {} : { onProgress: printProgress }),
  }).run();

  console.log(`Status: ${result.status}`);
  console.log(`Run artifacts: ${result.runDirectory}`);
  if (result.status === "needs_attention") {
    process.exitCode = 2;
  }
}

function applyOverrides(base: SupervisorConfig, args: ParsedArgs): SupervisorConfig {
  const testValues = args.values.get("test");
  return SupervisorConfigSchema.parse({
    ...base,
    ...(lastValue(args, "root") ? { targetRoot: path.resolve(lastValue(args, "root")!) } : {}),
    ...(lastValue(args, "leader") ? { leader: ProviderNameSchema.parse(lastValue(args, "leader")) } : {}),
    ...(lastValue(args, "writer") ? { writer: ProviderNameSchema.parse(lastValue(args, "writer")) } : {}),
    ...(lastValue(args, "max-review-rounds")
      ? { maxReviewRounds: Number(lastValue(args, "max-review-rounds")) }
      : {}),
    ...(testValues ? { testCommands: testValues } : {}),
  });
}

async function resolveTask(args: ParsedArgs): Promise<string> {
  const direct = lastValue(args, "task");
  if (direct) return direct.trim();
  const taskFile = lastValue(args, "task-file");
  if (!taskFile) return "";
  return (await readFile(path.resolve(taskFile), "utf8")).trim();
}

function lastValue(args: ParsedArgs, key: string): string | undefined {
  return args.values.get(key)?.at(-1);
}

async function confirm(question: string): Promise<boolean> {
  if (!input.isTTY || !output.isTTY) return false;
  const terminal = createInterface({ input, output });
  try {
    const answer = await terminal.question(`${question} [y/N] `);
    return /^(y|yes)$/i.test(answer.trim());
  } finally {
    terminal.close();
  }
}

function printHelp(): void {
  console.log(`Usage:
  npm run doctor -- [--root <repo>] [--config <json>]
  npm run start -- --task "..." [options]

Options:
  --task <text>                 Task for both agents
  --task-file <path>            Read the task from a file
  --root <repo>                 Target Git repository
  --leader claude|codex         Plan synthesizer (default: claude)
  --writer claude|codex         Only provider allowed to edit (default: claude)
  --test <command>              Trusted host-run test command; repeatable
  --max-review-rounds <1-5>     Reviewer/correction cycles (default: 2)
  --allow-write                 Enable the gated implementation phase
  --allow-dirty                 Permit writes in an already-dirty worktree
  --yes                         Approve the synthesized plan non-interactively
  --mock                        Use deterministic local providers; no API calls
  --quiet                       Hide live phase and heartbeat status updates
  --config <json>               Load supervisor settings from JSON

Write access requires --allow-write. Non-interactive write runs also require --yes.`);
}

function printProgress(update: ProgressUpdate): void {
  const time = update.timestamp.slice(11, 19);
  const label = update.state.toUpperCase().padEnd(9);
  const elapsed = update.elapsedSeconds === undefined ? "" : ` (${update.elapsedSeconds}s)`;
  console.error(`[${time}] ${label} ${update.message}${elapsed}`);
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
});
