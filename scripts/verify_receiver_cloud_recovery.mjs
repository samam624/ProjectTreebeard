import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const firmware = await readFile(
  join(repoRoot, "arduino", "MSV2_ESP32_Display_Runtime", "MSV2_ESP32_Display_Runtime.ino"),
  "utf8",
);

assert.match(
  firmware,
  /const size_t HISTORY_UPLOAD_BATCH_SIZE = 16;/,
  "History batches must stay within the live-tested non-PSRAM payload ceiling.",
);
assert.match(
  firmware,
  /if \(displaySyncSucceeded\)[\s\S]*?cloudDisplayConsecutiveFailures = 0;[\s\S]*?else[\s\S]*?cloudDisplayConsecutiveFailures\+\+[\s\S]*?nextCloudDisplaySyncMs = millis\(\) \+ backoffMs;/,
  "Failed display sync must back off instead of retrying a pending write every second.",
);
assert.match(
  firmware,
  /runCloudSnapshotUploader\(now\);[\s\S]*?runCloudBootstrap\(now\);[\s\S]*?queuedHistoryCount\(\) < \(HISTORY_QUEUE_CAPACITY \* 3U\) \/ 4U[\s\S]*?runCloudDisplaySettings\(now\);/,
  "A critical history backlog must receive priority over recoverable cloud pulls.",
);

console.log("PASS receiver cloud recovery limits TLS payloads, backs off display failures, and prioritizes history");
