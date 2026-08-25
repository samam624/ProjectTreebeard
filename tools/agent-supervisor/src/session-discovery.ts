import { createReadStream, existsSync } from "node:fs";
import { readdir, readFile, stat } from "node:fs/promises";
import { createInterface } from "node:readline";
import os from "node:os";
import path from "node:path";

// Finds an existing, real conversation to resume instead of starting a provider blank --
// "real" meaning one the user (or the CLI/editor extension they were using) actually drove,
// not a session the supervisor itself already created on a previous coop run. A session ID
// that has already appeared in this repo's own .agent-runs/ logs is excluded so "auto" keeps
// pointing at your own conversation rather than re-resuming the supervisor's own prior output
// -- see the two exported find* functions below for the concrete rule each provider uses.

async function excludedSessionIds(targetRoot: string, provider: "claude" | "codex"): Promise<Set<string>> {
  const excluded = new Set<string>();
  const runsDir = path.join(targetRoot, ".agent-runs");
  if (!existsSync(runsDir)) return excluded;

  let runDirs: string[];
  try {
    runDirs = await readdir(runsDir);
  } catch {
    return excluded;
  }

  for (const runDir of runDirs) {
    const eventsPath = path.join(runsDir, runDir, "events.jsonl");
    if (!existsSync(eventsPath)) continue;
    let contents: string;
    try {
      contents = await readFile(eventsPath, "utf8");
    } catch {
      continue;
    }
    for (const line of contents.split("\n")) {
      if (!line.trim()) continue;
      try {
        const event = JSON.parse(line) as { actor?: string; kind?: string; data?: { sessionId?: string } };
        if (event.actor === provider && event.kind === "provider.response" && event.data?.sessionId) {
          excluded.add(event.data.sessionId);
        }
      } catch {
        // Skip a malformed line rather than aborting discovery over one bad record.
      }
    }
  }
  return excluded;
}

function normalizePath(value: string): string {
  return path.resolve(value).replace(/\\/g, "/").toLowerCase();
}

async function readFirstLine(filePath: string): Promise<string | null> {
  const stream = createReadStream(filePath, { encoding: "utf8" });
  const rl = createInterface({ input: stream });
  try {
    for await (const line of rl) {
      return line;
    }
    return null;
  } finally {
    rl.close();
    stream.destroy();
  }
}

// Claude Code names a project's session directory after its cwd with path separators and
// colons replaced by `-` (e.g. `c:\Users\me\repo` -> `c--Users-me-repo`).
function claudeProjectDirName(targetRoot: string): string {
  return path.resolve(targetRoot).replace(/[\\/:]/g, "-");
}

export async function findMostRecentClaudeSession(
  targetRoot: string,
  claudeHomeDir: string = path.join(os.homedir(), ".claude"),
): Promise<string | null> {
  const projectDir = path.join(claudeHomeDir, "projects", claudeProjectDirName(targetRoot));
  if (!existsSync(projectDir)) return null;

  const excluded = await excludedSessionIds(targetRoot, "claude");
  let entries: string[];
  try {
    entries = await readdir(projectDir);
  } catch {
    return null;
  }

  let best: { id: string; mtimeMs: number } | null = null;
  for (const entry of entries) {
    if (!entry.endsWith(".jsonl")) continue;
    const id = entry.slice(0, -".jsonl".length);
    if (excluded.has(id)) continue;
    try {
      const info = await stat(path.join(projectDir, entry));
      if (!best || info.mtimeMs > best.mtimeMs) best = { id, mtimeMs: info.mtimeMs };
    } catch {
      // A file that disappears mid-scan just doesn't compete for "most recent."
    }
  }
  return best?.id ?? null;
}

async function walkJsonlFiles(root: string): Promise<string[]> {
  const results: string[] = [];
  let entries: Array<{ name: string; isDirectory(): boolean; isFile(): boolean }>;
  try {
    entries = await readdir(root, { withFileTypes: true });
  } catch {
    return results;
  }
  for (const entry of entries) {
    const full = path.join(root, entry.name);
    if (entry.isDirectory()) results.push(...(await walkJsonlFiles(full)));
    else if (entry.isFile() && entry.name.endsWith(".jsonl")) results.push(full);
  }
  return results;
}

type CodexSessionMeta = {
  type?: string;
  payload?: {
    session_id?: string;
    id?: string;
    cwd?: string;
    originator?: string;
    thread_source?: string;
  };
};

export async function findMostRecentCodexThread(
  targetRoot: string,
  codexSessionsDir: string = path.join(os.homedir(), ".codex", "sessions"),
): Promise<string | null> {
  const sessionsDir = codexSessionsDir;
  if (!existsSync(sessionsDir)) return null;

  const excluded = await excludedSessionIds(targetRoot, "codex");
  const targetPath = normalizePath(targetRoot);
  const files = await walkJsonlFiles(sessionsDir);

  let best: { id: string; mtimeMs: number } | null = null;
  for (const file of files) {
    let info;
    try {
      info = await stat(file);
    } catch {
      continue;
    }
    if (best && info.mtimeMs <= best.mtimeMs) continue; // cheap prune before the read below

    const firstLine = await readFirstLine(file);
    if (!firstLine) continue;
    let meta: CodexSessionMeta;
    try {
      meta = JSON.parse(firstLine) as CodexSessionMeta;
    } catch {
      continue;
    }
    const payload = meta.payload;
    if (meta.type !== "session_meta" || !payload) continue;
    // codex_sdk_ts is this supervisor's own originator string (see providers/codex.ts) --
    // exclude it the same way excludedSessionIds() would eventually exclude it by ID, but
    // without needing a prior .agent-runs log entry to know that. "subagent" threads (e.g.
    // Codex's own internal reviewer subagents) are children of a real conversation, not one.
    if (payload.originator === "codex_sdk_ts") continue;
    if (payload.thread_source && payload.thread_source !== "user") continue;
    if (!payload.cwd || normalizePath(payload.cwd) !== targetPath) continue;
    const id = payload.session_id ?? payload.id;
    if (!id || excluded.has(id)) continue;

    best = { id, mtimeMs: info.mtimeMs };
  }
  return best?.id ?? null;
}
