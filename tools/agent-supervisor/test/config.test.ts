import assert from "node:assert/strict";
import test from "node:test";
import { SupervisorConfigSchema } from "../src/config.js";

test("Claude receives the full default turn allowance", () => {
  const config = SupervisorConfigSchema.parse({});

  assert.equal(config.claudeMaxTurns, 50);
});
