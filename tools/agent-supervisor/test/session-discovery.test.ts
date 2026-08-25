import assert from "node:assert/strict";
import test from "node:test";
import { mkdir, mkdtemp, writeFile, utimes } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { findMostRecentClaudeSession, findMostRecentCodexThread } from "../src/session-discovery.js";

async function touch(filePath: string, contents: string, mtime: Date): Promise<void> {
  await mkdir(path.dirname(filePath), { recursive: true });
  await writeFile(filePath, contents, "utf8");
  await utimes(filePath, mtime, mtime);
}

test("findMostRecentClaudeSession picks the newest session and skips ones the supervisor already used", async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-claude-repo-"));
  const claudeHome = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-claude-home-"));
  const projectDirName = path.resolve(root).replace(/[\\/:]/g, "-");
  const projectDir = path.join(claudeHome, "projects", projectDirName);

  await touch(path.join(projectDir, "older-session.jsonl"), "{}", new Date(Date.now() - 60_000));
  await touch(path.join(projectDir, "newer-session.jsonl"), "{}", new Date());

  const found = await findMostRecentClaudeSession(root, claudeHome);
  assert.equal(found, "newer-session");

  // A session already recorded as this supervisor's own past output should not be picked again.
  await touch(
    path.join(root, ".agent-runs", "past-run", "events.jsonl"),
    `${JSON.stringify({ actor: "claude", kind: "provider.response", data: { sessionId: "newer-session" } })}\n`,
    new Date(),
  );
  const foundAfterExclusion = await findMostRecentClaudeSession(root, claudeHome);
  assert.equal(foundAfterExclusion, "older-session");
});

test("findMostRecentClaudeSession returns null when no project directory exists", async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-claude-repo-empty-"));
  const claudeHome = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-claude-home-empty-"));
  assert.equal(await findMostRecentClaudeSession(root, claudeHome), null);
});

function codexMetaLine(payload: Record<string, unknown>): string {
  return `${JSON.stringify({ timestamp: new Date().toISOString(), type: "session_meta", payload })}\n{"type":"turn","payload":{}}\n`;
}

test("findMostRecentCodexThread only considers real user threads for this repo, not the supervisor's own", async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-codex-repo-"));
  const sessionsDir = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-codex-sessions-"));

  await touch(
    path.join(sessionsDir, "2026", "08", "24", "rollout-user-vscode.jsonl"),
    codexMetaLine({ session_id: "user-thread", cwd: root, originator: "codex_vscode", thread_source: "user" }),
    new Date(Date.now() - 30_000),
  );
  await touch(
    path.join(sessionsDir, "2026", "08", "24", "rollout-supervisor.jsonl"),
    codexMetaLine({ session_id: "supervisor-thread", cwd: root, originator: "codex_sdk_ts", thread_source: "user" }),
    new Date(), // newest by mtime, but must still be excluded by originator
  );
  await touch(
    path.join(sessionsDir, "2026", "08", "24", "rollout-subagent.jsonl"),
    codexMetaLine({ session_id: "subagent-thread", cwd: root, originator: "codex_vscode", thread_source: "subagent" }),
    new Date(),
  );
  await touch(
    path.join(sessionsDir, "2026", "08", "24", "rollout-other-repo.jsonl"),
    codexMetaLine({ session_id: "other-repo-thread", cwd: path.join(root, "..", "unrelated"), originator: "codex_vscode", thread_source: "user" }),
    new Date(),
  );

  const found = await findMostRecentCodexThread(root, sessionsDir);
  assert.equal(found, "user-thread");
});

test("findMostRecentCodexThread returns null when no sessions directory exists", async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), "agent-supervisor-codex-repo-empty-"));
  const missingSessionsDir = path.join(os.tmpdir(), `agent-supervisor-does-not-exist-${Date.now()}`);
  assert.equal(await findMostRecentCodexThread(root, missingSessionsDir), null);
});
