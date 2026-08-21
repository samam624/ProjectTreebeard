import { execFile, spawn } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

export type TestResult = {
  command: string;
  exitCode: number | null;
  timedOut: boolean;
  durationMs: number;
  output: string;
};

export async function gitStatus(root: string): Promise<string> {
  const { stdout } = await execFileAsync("git", ["status", "--short"], {
    cwd: root,
    windowsHide: true,
  });
  return stdout.trimEnd();
}

export async function gitDiff(root: string): Promise<string> {
  const { stdout } = await execFileAsync(
    "git",
    ["diff", "--no-ext-diff", "--stat", "--", "."],
    { cwd: root, windowsHide: true, maxBuffer: 4 * 1024 * 1024 },
  );
  return stdout.trimEnd();
}

export async function gitPatch(root: string, maxChars = 500_000): Promise<string> {
  const { stdout } = await execFileAsync(
    "git",
    ["diff", "--no-ext-diff", "--unified=3", "--", "."],
    { cwd: root, windowsHide: true, maxBuffer: 8 * 1024 * 1024 },
  );
  if (stdout.length <= maxChars) return stdout.trimEnd();
  return `${stdout.slice(0, maxChars)}\n\n[diff truncated by supervisor]`;
}

export async function ensureGitRepository(root: string): Promise<void> {
  try {
    await execFileAsync("git", ["rev-parse", "--show-toplevel"], {
      cwd: root,
      windowsHide: true,
    });
  } catch {
    throw new Error(`Target is not a Git repository: ${root}`);
  }
}

export async function runTestCommand(
  command: string,
  cwd: string,
  timeoutMs: number,
  maxOutputChars: number,
): Promise<TestResult> {
  const started = Date.now();
  return await new Promise<TestResult>((resolve) => {
    const testEnvironment = sanitizedTestEnvironment();
    const child = spawn(command, {
      cwd,
      shell: true,
      windowsHide: true,
      env: testEnvironment,
    });
    let output = "";
    let timedOut = false;

    const collect = (chunk: Buffer | string): void => {
      if (output.length < maxOutputChars) {
        output += chunk.toString().slice(0, maxOutputChars - output.length);
      }
    };
    child.stdout?.on("data", collect);
    child.stderr?.on("data", collect);

    const timer = setTimeout(() => {
      timedOut = true;
      child.kill();
    }, timeoutMs);

    child.on("close", (exitCode) => {
      clearTimeout(timer);
      resolve({
        command,
        exitCode,
        timedOut,
        durationMs: Date.now() - started,
        output,
      });
    });

    child.on("error", (error) => {
      clearTimeout(timer);
      resolve({
        command,
        exitCode: null,
        timedOut,
        durationMs: Date.now() - started,
        output: `${output}\n${error.message}`.trim(),
      });
    });
  });
}

function sanitizedTestEnvironment(): NodeJS.ProcessEnv {
  const environment = { ...process.env };
  const alwaysRemove = [
    "ANTHROPIC_API_KEY",
    "ANTHROPIC_AUTH_TOKEN",
    "CLAUDE_CODE_OAUTH_TOKEN",
    "CODEX_API_KEY",
    "OPENAI_API_KEY",
  ];
  for (const name of alwaysRemove) delete environment[name];

  if (process.env.CLAUDE_CODE_USE_BEDROCK) {
    for (const name of ["AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY", "AWS_SESSION_TOKEN"]) {
      delete environment[name];
    }
  }
  if (process.env.CLAUDE_CODE_USE_VERTEX) {
    delete environment.GOOGLE_APPLICATION_CREDENTIALS;
  }
  if (process.env.CLAUDE_CODE_USE_FOUNDRY) {
    for (const name of Object.keys(environment)) {
      if (name.startsWith("AZURE_") || name.startsWith("ANTHROPIC_FOUNDRY_")) {
        delete environment[name];
      }
    }
  }
  return environment;
}
