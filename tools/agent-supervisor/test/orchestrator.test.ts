import assert from "node:assert/strict";
import test from "node:test";
import { mkdtemp, readFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { setTimeout } from "node:timers/promises";
import { SupervisorConfigSchema } from "../src/config.js";
import { Orchestrator } from "../src/orchestrator.js";
import { MockProvider } from "../src/providers/mock.js";
import type { AgentProvider, ProviderRequest, ProviderResponse } from "../src/providers/types.js";

class DelayedProvider implements AgentProvider {
  readonly name: "claude" | "codex";

  constructor(
    private readonly delegate: MockProvider,
    private readonly delayMs: number,
  ) {
    this.name = delegate.name;
  }

  async invoke(request: ProviderRequest): Promise<ProviderResponse> {
    await setTimeout(this.delayMs);
    return this.delegate.invoke(request);
  }
}

test("mock collaboration completes with bounded writer access", async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-"));
  execFileSync("git", ["init"], { cwd: root, stdio: "ignore" });

  const claude = new MockProvider("claude");
  const codex = new MockProvider("codex");
  const progress: string[] = [];
  const config = SupervisorConfigSchema.parse({
    targetRoot: root,
    testCommands: [],
  });
  const result = await new Orchestrator({
    task: "Add focused input validation.",
    config,
    claude,
    codex,
    allowWrite: true,
    allowDirty: true,
    autoApprove: true,
    confirm: async () => true,
    onProgress: (update) => progress.push(`${update.actor}:${update.state}:${update.phase}`),
  }).run();

  assert.equal(result.status, "approved");
  assert.equal(claude.calls.filter((call) => call.access === "write").length, 1);
  assert.equal(codex.calls.filter((call) => call.access === "write").length, 0);
  assert.equal(result.reviews.length, 1);
  assert.ok(progress.includes("claude:started:proposal"));
  assert.ok(progress.includes("codex:started:proposal"));
  assert.ok(progress.includes("supervisor:completed:completion"));
  assert.match(await readFile(path.join(result.runDirectory, "summary.md"), "utf8"), /Status: \*\*approved\*\*/);
});

test("read-only mode stops after planning", async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-plan-"));
  execFileSync("git", ["init"], { cwd: root, stdio: "ignore" });

  const claude = new MockProvider("claude");
  const codex = new MockProvider("codex");
  const result = await new Orchestrator({
    task: "Plan a focused change.",
    config: SupervisorConfigSchema.parse({ targetRoot: root }),
    claude,
    codex,
    allowWrite: false,
    allowDirty: false,
    autoApprove: false,
    confirm: async () => {
      throw new Error("Read-only mode must not request approval.");
    },
  }).run();

  assert.equal(result.status, "planned");
  assert.equal(claude.calls.some((call) => call.access === "write"), false);
  assert.equal(codex.calls.some((call) => call.access === "write"), false);
});

test("long provider calls emit working heartbeats", async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-progress-"));
  execFileSync("git", ["init"], { cwd: root, stdio: "ignore" });

  const updates: string[] = [];
  const result = await new Orchestrator({
    task: "Plan a focused change.",
    config: SupervisorConfigSchema.parse({ targetRoot: root }),
    claude: new DelayedProvider(new MockProvider("claude"), 15),
    codex: new DelayedProvider(new MockProvider("codex"), 15),
    allowWrite: false,
    allowDirty: false,
    autoApprove: false,
    confirm: async () => false,
    progressHeartbeatMs: 5,
    onProgress: (update) => updates.push(update.state),
  }).run();

  assert.equal(result.status, "planned");
  assert.ok(updates.includes("working"));
});
