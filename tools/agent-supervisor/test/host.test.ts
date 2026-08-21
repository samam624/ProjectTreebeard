import assert from "node:assert/strict";
import test from "node:test";
import { runTestCommand } from "../src/host.js";

test("host test commands do not inherit provider API keys", async () => {
  const previousAnthropic = process.env.ANTHROPIC_API_KEY;
  const previousCodex = process.env.CODEX_API_KEY;
  process.env.ANTHROPIC_API_KEY = "test-only-anthropic-value";
  process.env.CODEX_API_KEY = "test-only-codex-value";
  try {
    const result = await runTestCommand(
      'node -e "console.log(process.env.ANTHROPIC_API_KEY || process.env.CODEX_API_KEY || \'clear\')"',
      process.cwd(),
      10_000,
      10_000,
    );
    assert.equal(result.exitCode, 0);
    assert.equal(result.output.trim(), "clear");
  } finally {
    if (previousAnthropic === undefined) delete process.env.ANTHROPIC_API_KEY;
    else process.env.ANTHROPIC_API_KEY = previousAnthropic;
    if (previousCodex === undefined) delete process.env.CODEX_API_KEY;
    else process.env.CODEX_API_KEY = previousCodex;
  }
});
