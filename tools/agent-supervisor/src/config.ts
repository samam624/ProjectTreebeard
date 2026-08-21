import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { z } from "zod";
import { ProviderNameSchema } from "./domain.js";

const sourceDirectory = path.dirname(fileURLToPath(import.meta.url));
export const DEFAULT_TARGET_ROOT = path.resolve(sourceDirectory, "../../..");

export const SupervisorConfigSchema = z.object({
  targetRoot: z.string().min(1).default(DEFAULT_TARGET_ROOT),
  leader: ProviderNameSchema.default("claude"),
  writer: ProviderNameSchema.default("claude"),
  maxReviewRounds: z.number().int().min(1).max(5).default(2),
  maxProviderCalls: z.number().int().min(4).max(30).default(10),
  claudeMaxTurns: z.number().int().min(1).max(50).default(12),
  claudeMaxBudgetUsdPerCall: z.number().positive().max(100).default(2),
  claudeMaxTotalBudgetUsd: z.number().positive().max(500).default(10),
  testCommands: z.array(z.string().min(1)).default([]),
  testTimeoutSeconds: z.number().int().min(1).max(3600).default(600),
  maxTestOutputChars: z.number().int().min(1000).max(5_000_000).default(200_000),
});
export type SupervisorConfig = z.infer<typeof SupervisorConfigSchema>;

export async function loadConfig(configPath?: string): Promise<SupervisorConfig> {
  if (!configPath) {
    return SupervisorConfigSchema.parse({});
  }

  const resolved = path.resolve(configPath);
  const raw = JSON.parse(await readFile(resolved, "utf8")) as unknown;
  const parsed = SupervisorConfigSchema.parse(raw);
  return {
    ...parsed,
    targetRoot: path.resolve(path.dirname(resolved), parsed.targetRoot),
  };
}
