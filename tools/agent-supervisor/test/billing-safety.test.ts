import assert from "node:assert/strict";
import test from "node:test";
import {
  assertSubscriptionOnlyBilling,
  assertSubscriptionSafeModels,
  isUsageOrBillingLimit,
  subscriptionOnlyEnvironment,
} from "../src/billing-safety.js";

test("subscription-only mode rejects metered authentication without exposing values", () => {
  const secret = "test-secret-value";

  assert.throws(
    () => assertSubscriptionOnlyBilling({ PATH: "test-path", OPENAI_API_KEY: secret }),
    (error: unknown) => {
      assert.match(String(error), /OPENAI_API_KEY/);
      assert.doesNotMatch(String(error), new RegExp(secret));
      return true;
    },
  );
});

test("provider child environments remove metered routes and disable paid Claude features", () => {
  const environment = subscriptionOnlyEnvironment({
    PATH: "test-path",
    ANTHROPIC_API_KEY: "anthropic-secret",
    CODEX_API_KEY: "codex-secret",
    CLAUDE_CODE_USE_BEDROCK: "1",
  });

  assert.equal(environment.PATH, "test-path");
  assert.equal(environment.ANTHROPIC_API_KEY, undefined);
  assert.equal(environment.CODEX_API_KEY, undefined);
  assert.equal(environment.CLAUDE_CODE_USE_BEDROCK, undefined);
  assert.equal(environment.DISABLE_EXTRA_USAGE_COMMAND, "1");
  assert.equal(environment.CLAUDE_CODE_DISABLE_1M_CONTEXT, "1");
});

test("billing safety rejects paid-context model aliases and recognizes quota failures", () => {
  assert.throws(() => assertSubscriptionSafeModels("sonnet[1m]"), /usage credits/i);
  assert.equal(isUsageOrBillingLimit("You've hit your weekly limit"), true);
  assert.equal(isUsageOrBillingLimit("Credit balance is too low"), true);
});
