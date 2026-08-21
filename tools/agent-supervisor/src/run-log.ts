import { mkdir, appendFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { randomUUID } from "node:crypto";

export type RunEvent = {
  phase: string;
  actor: string;
  kind: string;
  data: unknown;
};

export class RunLog {
  readonly runId: string;
  readonly directory: string;
  readonly eventsPath: string;

  private constructor(runId: string, directory: string) {
    this.runId = runId;
    this.directory = directory;
    this.eventsPath = path.join(directory, "events.jsonl");
  }

  static async create(root: string, now = new Date()): Promise<RunLog> {
    const stamp = now.toISOString().replace(/[:.]/g, "-");
    const runId = `${stamp}-${randomUUID().slice(0, 8)}`;
    const directory = path.join(root, ".agent-runs", runId);
    await mkdir(directory, { recursive: true });
    return new RunLog(runId, directory);
  }

  async event(event: RunEvent): Promise<void> {
    const record = {
      timestamp: new Date().toISOString(),
      runId: this.runId,
      ...event,
    };
    await appendFile(this.eventsPath, `${JSON.stringify(record)}\n`, "utf8");
  }

  async artifact(name: string, contents: string): Promise<string> {
    const target = path.join(this.directory, name);
    await writeFile(target, contents, "utf8");
    return target;
  }
}
