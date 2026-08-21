import { execFile } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { promisify } from "node:util";
import path from "node:path";
import { fileURLToPath } from "node:url";
import type { SupervisorConfig } from "./config.js";
import { ensureGitRepository } from "./host.js";

const execFileAsync = promisify(execFile);
export type DoctorCheck = {
  name: string;
  ok: boolean;
  detail: string;
};

export async function doctor(config: SupervisorConfig): Promise<DoctorCheck[]> {
  const checks: DoctorCheck[] = [];
  checks.push({
    name: "Node.js",
    ok: Number(process.versions.node.split(".")[0]) >= 18,
    detail: process.version,
  });

  try {
    await ensureGitRepository(config.targetRoot);
    checks.push({ name: "Target Git repository", ok: true, detail: config.targetRoot });
  } catch (error) {
    checks.push({
      name: "Target Git repository",
      ok: false,
      detail: error instanceof Error ? error.message : String(error),
    });
  }

  checks.push(packageCheck("Codex SDK", "@openai/codex-sdk"));
  checks.push(packageCheck("Claude Agent SDK", "@anthropic-ai/claude-agent-sdk"));

  const codexKey = Boolean(process.env.CODEX_API_KEY ?? process.env.OPENAI_API_KEY);
  const codexLogin = await commandResult("codex", ["login", "status"]);
  checks.push({
    name: "Codex authentication",
    ok: codexKey || codexLogin.ok,
    detail: codexKey ? "API key environment variable is present." : codexLogin.detail,
  });

  const claudeAuth = Boolean(
    process.env.ANTHROPIC_API_KEY ??
      process.env.CLAUDE_CODE_USE_BEDROCK ??
      process.env.CLAUDE_CODE_USE_VERTEX ??
      process.env.CLAUDE_CODE_USE_FOUNDRY,
  );
  const claudeLogin = await firstWorkingCommand(claudeExecutableCandidates(), ["auth", "status", "--text"]);
  checks.push({
    name: "Claude authentication",
    ok: claudeAuth || claudeLogin.ok,
    detail: claudeAuth
      ? "A supported Claude provider environment variable is present."
      : claudeLogin.ok
        ? "Claude Code login is active."
        : "Run `claude auth login`, set ANTHROPIC_API_KEY, or configure Bedrock, Vertex, or Foundry.",
  });

  return checks;
}

function packageCheck(name: string, packageName: string): DoctorCheck {
  try {
    let directory = path.dirname(fileURLToPath(import.meta.resolve(packageName)));
    let manifestPath = "";
    for (let depth = 0; depth < 4; depth += 1) {
      const candidate = path.join(directory, "package.json");
      if (existsSync(candidate)) {
        manifestPath = candidate;
        break;
      }
      directory = path.dirname(directory);
    }
    if (!manifestPath) throw new Error(`Could not locate ${packageName}/package.json.`);
    const manifest = JSON.parse(readFileSync(manifestPath, "utf8")) as { version?: string };
    return { name, ok: true, detail: manifest.version ?? "installed" };
  } catch (error) {
    return { name, ok: false, detail: error instanceof Error ? error.message : String(error) };
  }
}

async function commandResult(command: string, args: string[]): Promise<{ ok: boolean; detail: string }> {
  try {
    const { stdout, stderr } = await execFileAsync(command, args, { windowsHide: true });
    const detail = `${stdout}${stderr}`.trim();
    return { ok: !/not logged in/i.test(detail), detail: detail || "available" };
  } catch (error) {
    const candidate = error as { stdout?: string; stderr?: string; message?: string };
    const detail = `${candidate.stdout ?? ""}${candidate.stderr ?? ""}`.trim();
    return { ok: false, detail: detail || candidate.message || `${command} is unavailable.` };
  }
}

async function firstWorkingCommand(
  commands: string[],
  args: string[],
): Promise<{ ok: boolean; detail: string }> {
  let last = { ok: false, detail: "Command is unavailable." };
  for (const command of commands) {
    last = await commandResult(command, args);
    if (last.ok) return last;
  }
  return last;
}

function claudeExecutableCandidates(): string[] {
  const candidates = [process.env.CLAUDE_CODE_PATH, "claude"];
  if (process.platform === "win32" && process.env.LOCALAPPDATA) {
    candidates.push(
      path.join(
        process.env.LOCALAPPDATA,
        "Microsoft",
        "WinGet",
        "Packages",
        "Anthropic.ClaudeCode_Microsoft.Winget.Source_8wekyb3d8bbwe",
        "claude.exe",
      ),
    );
  }
  return candidates.filter((candidate): candidate is string => Boolean(candidate));
}
