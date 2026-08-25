// Live pairing server: a local-only HTTP+SSE server that lets Claude and Codex work on the same
// prompt concurrently, each in a real resumable session, streaming both live to a browser tab --
// built in response to the batch-oriented Orchestrator (orchestrator.ts) structurally not being
// able to give live per-token/per-tool-call visibility or an ongoing multi-turn conversation (it
// collapses each provider call down to one structured JSON result). This does not replace that
// tool -- it is for open-ended discussion/design collaboration, not the plan/implement/review
// pipeline. Read-only on both sides by design (see ClaudeLiveSession/CodexLiveSession): this is a
// place to think out loud together, not to edit files.
import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { assertSubscriptionOnlyBilling, assertSubscriptionSafeModels } from "./billing-safety.js";
import { findMostRecentClaudeSession, findMostRecentCodexThread } from "./session-discovery.js";
import { ClaudeLiveSession, type LiveEvent } from "./live/claude-live.js";
import { CodexLiveSession } from "./live/codex-live.js";
import { DEFAULT_TARGET_ROOT } from "./config.js";

const sourceDirectory = path.dirname(fileURLToPath(import.meta.url));
const PUBLIC_HTML_PATH = path.join(sourceDirectory, "..", "public", "live.html");

type AgentName = "claude" | "codex";

function parseFlags(argv: string[]): Map<string, string | true> {
  const flags = new Map<string, string | true>();
  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i];
    if (!token?.startsWith("--")) continue;
    const key = token.slice(2);
    const next = argv[i + 1];
    if (next && !next.startsWith("--")) {
      flags.set(key, next);
      i += 1;
    } else {
      flags.set(key, true);
    }
  }
  return flags;
}

async function resolveResume(
  flags: Map<string, string | true>,
  flag: string,
  label: string,
  targetRoot: string,
  findAuto: () => Promise<string | null>,
): Promise<string | undefined> {
  const value = flags.get(flag);
  if (value === undefined) return undefined;
  if (value !== true && value !== "auto") return value;
  const found = await findAuto();
  console.error(found ? `Resuming ${label} conversation ${found} (auto-detected).` : `No existing ${label} conversation found for this repo; starting blank.`);
  return found ?? undefined;
}

async function main(): Promise<void> {
  const flags = parseFlags(process.argv.slice(2));
  const port = Number(flags.get("port") ?? 4127);
  const targetRoot = flags.get("root") ? path.resolve(String(flags.get("root"))) : DEFAULT_TARGET_ROOT;
  const claudeModel = typeof flags.get("claude-model") === "string" ? (flags.get("claude-model") as string) : process.env.CLAUDE_MODEL;
  const codexModel = typeof flags.get("codex-model") === "string" ? (flags.get("codex-model") as string) : process.env.CODEX_MODEL;

  assertSubscriptionOnlyBilling();
  assertSubscriptionSafeModels(claudeModel);

  const resumeClaude = await resolveResume(flags, "resume-claude", "Claude", targetRoot, () => findMostRecentClaudeSession(targetRoot));
  const resumeCodex = await resolveResume(flags, "resume-codex", "Codex", targetRoot, () => findMostRecentCodexThread(targetRoot));

  const claude = new ClaudeLiveSession({ cwd: targetRoot, ...(claudeModel ? { model: claudeModel } : {}), ...(resumeClaude ? { resumeSessionId: resumeClaude } : {}) });
  const codex = new CodexLiveSession({ cwd: targetRoot, ...(codexModel ? { model: codexModel } : {}), ...(resumeCodex ? { resumeThreadId: resumeCodex } : {}) });

  const html = await readFile(PUBLIC_HTML_PATH, "utf8");

  const sseClients = new Set<ServerResponse>();
  const broadcast = (payload: unknown): void => {
    const line = `data: ${JSON.stringify(payload)}\n\n`;
    for (const client of sseClients) client.write(line);
  };

  // Each agent's last complete reply this session, shown to the *other* agent as context on the
  // next round -- without this, the two are answering the same prompt in parallel isolation, not
  // actually collaborating.
  const lastReply: Record<AgentName, string> = { claude: "", codex: "" };
  let roundId = 0;
  let roundInFlight = false;
  const abortController = new AbortController();

  async function runAgent(agent: AgentName, roundPrompt: string, thisRound: number): Promise<void> {
    const session = agent === "claude" ? claude : codex;
    let collectedText = "";
    for await (const event of session.send(roundPrompt, abortController.signal) as AsyncGenerator<LiveEvent>) {
      if (event.kind === "text") collectedText += (collectedText ? "\n\n" : "") + event.text;
      broadcast({ roundId: thisRound, agent, ...event });
    }
    if (collectedText) lastReply[agent] = collectedText;
  }

  async function runRound(userPrompt: string): Promise<void> {
    roundId += 1;
    const thisRound = roundId;
    roundInFlight = true;
    broadcast({ roundId: thisRound, agent: "system", kind: "round_start", prompt: userPrompt });

    const claudePrompt = lastReply.codex
      ? `Codex's last reply in this conversation, for your awareness (you don't have to agree with it):\n\n${lastReply.codex}\n\n---\n\nNow, the user says:\n\n${userPrompt}`
      : userPrompt;
    const codexPrompt = lastReply.claude
      ? `Claude's last reply in this conversation, for your awareness (you don't have to agree with it):\n\n${lastReply.claude}\n\n---\n\nNow, the user says:\n\n${userPrompt}`
      : userPrompt;

    try {
      await Promise.allSettled([runAgent("claude", claudePrompt, thisRound), runAgent("codex", codexPrompt, thisRound)]);
    } finally {
      roundInFlight = false;
      broadcast({ roundId: thisRound, agent: "system", kind: "round_end", claudeCostUsd: claude.costSoFarUsd });
    }
  }

  const server = createServer((req: IncomingMessage, res: ServerResponse) => {
    if (req.method === "GET" && (req.url === "/" || req.url === "/index.html")) {
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      res.end(html);
      return;
    }

    if (req.method === "GET" && req.url === "/events") {
      res.writeHead(200, {
        "Content-Type": "text/event-stream",
        "Cache-Control": "no-cache",
        Connection: "keep-alive",
      });
      res.write(": connected\n\n");
      sseClients.add(res);
      req.on("close", () => sseClients.delete(res));
      return;
    }

    if (req.method === "POST" && req.url === "/prompt") {
      if (roundInFlight) {
        res.writeHead(409, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ error: "A round is already in progress." }));
        return;
      }
      let body = "";
      req.on("data", (chunk) => (body += chunk));
      req.on("end", () => {
        let text: string;
        try {
          text = String((JSON.parse(body) as { text?: unknown }).text ?? "").trim();
        } catch {
          res.writeHead(400, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ error: "Invalid JSON body." }));
          return;
        }
        if (!text) {
          res.writeHead(400, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ error: "Empty prompt." }));
          return;
        }
        res.writeHead(202, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: true }));
        runRound(text).catch((error) => {
          broadcast({ agent: "system", kind: "error", message: error instanceof Error ? error.message : String(error) });
        });
      });
      return;
    }

    res.writeHead(404);
    res.end();
  });

  server.listen(port, () => {
    console.error(`Live pairing server at http://localhost:${port}  (repo: ${targetRoot})`);
    console.error("Read-only on both sides: this session can look at the repo but will not edit anything. Ctrl+C to stop.");
  });

  process.on("SIGINT", () => {
    abortController.abort(new Error("Server shutting down."));
    server.close(() => process.exit(0));
  });
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
});
