import assert from "node:assert/strict";
import test from "node:test";
import { z } from "zod";
import { jsonSchemaFor, parseJsonText } from "../src/domain.js";

test("jsonSchemaFor targets the draft supported by Claude Agent SDK", () => {
  const schema = jsonSchemaFor(z.object({ result: z.string() }));

  assert.equal(schema.$schema, "http://json-schema.org/draft-07/schema#");
  assert.equal(schema.type, "object");
});

test("parseJsonText accepts fenced JSON", () => {
  assert.deepEqual(parseJsonText('```json\n{"approved":true}\n```'), { approved: true });
});

test("parseJsonText extracts an embedded object", () => {
  assert.deepEqual(parseJsonText('Result:\n{"approved":false}\nDone'), { approved: false });
});
