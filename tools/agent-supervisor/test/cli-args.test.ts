import assert from "node:assert/strict";
import test from "node:test";
import { parseArgs } from "../src/cli-args.js";

test("parseArgs preserves an unquoted multiword task", () => {
  const args = parseArgs([
    "run",
    "--task",
    "inspect",
    "the",
    "10.0.0.35",
    "webpage",
    "--allow-write",
  ]);

  assert.deepEqual(args.values.get("task"), ["inspect the 10.0.0.35 webpage"]);
  assert.equal(args.flags.has("allow-write"), true);
});

test("parseArgs keeps repeated multiword values separate", () => {
  const args = parseArgs(["run", "--test", "npm", "run", "check", "--test", "npm", "test"]);

  assert.deepEqual(args.values.get("test"), ["npm run check", "npm test"]);
});
