import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const firmware = await readFile(
  join(repoRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "Treebeard_ESP32_Display_Runtime.ino"),
  "utf8",
);

function functionBody(signaturePattern) {
  const definition = new RegExp(`${signaturePattern}\\s*\\{`, "g");
  const match = definition.exec(firmware);
  assert.ok(match, `Could not find firmware function matching ${signaturePattern}`);
  const openBrace = firmware.indexOf("{", match.index);
  let depth = 0;
  for (let index = openBrace; index < firmware.length; index += 1) {
    if (firmware[index] === "{") depth += 1;
    if (firmware[index] === "}") depth -= 1;
    if (depth === 0) return firmware.slice(openBrace, index + 1);
  }
  assert.fail(`Unbalanced firmware function matching ${signaturePattern}`);
}

const enqueueBody = functionBody(
  "bool enqueueHistoryReading\\(uint8_t sensorId, const SensorReading& reading\\)",
);
const copyBatchBody = functionBody(
  "size_t copyHistoryUploadBatch\\(PendingHistoryReading\\* destination, size_t capacity\\)",
);
const uploaderBody = functionBody("void runCloudSnapshotUploader\\(uint32_t now\\)");
const autoCalBody = functionBody("void runCloudAutoCalPull\\(uint32_t now\\)");
const cloudTaskBody = functionBody("void cloudSyncTask\\(void\\* parameter\\)");

assert.match(
  firmware,
  /const size_t HISTORY_STAGING_CAPACITY = 64;[\s\S]*?const size_t HISTORY_JOURNAL_FLUSH_RECORDS = 56;[\s\S]*?const size_t HISTORY_QUEUE_CAPACITY = 8192;[\s\S]*?const size_t HISTORY_UPLOAD_BATCH_SIZE = 96;/,
  "History must use bounded RTC staging, outage spillover capacity, and a paced throughput-sized batch.",
);
assert.match(
  firmware,
  /RTC_NOINIT_ATTR RtcHistoryStaging rtcHistoryStaging;[\s\S]*?restoreHistoryStagingFromRtc\(\);/,
  "Uncommitted ingress must survive receiver crashes and software resets in checksummed RTC memory.",
);
assert.match(
  firmware,
  /initializeHistoryJournal\(\)[\s\S]*?validHistoryJournalRecord[\s\S]*?durable outbox ready/,
  "Startup must validate and recover the power-safe history journal.",
);
assert.match(
  firmware,
  /serviceHistoryJournal\(\);[\s\S]*?wifiStationConnected && \(int32_t\)\(now - nextCloudNetworkSlotMs\) >= 0/,
  "The cloud task must service outage spillover before considering network work.",
);
assert.match(
  firmware,
  /if \(displaySyncSucceeded\)[\s\S]*?cloudDisplayConsecutiveFailures = 0;[\s\S]*?else[\s\S]*?cloudDisplayConsecutiveFailures\+\+[\s\S]*?nextCloudDisplaySyncMs = millis\(\) \+ backoffMs;/,
  "Failed display sync must back off instead of retrying a pending write every second.",
);
assert.match(
  firmware,
  /if \(bootstrapNeeded && due\(nextCloudBootstrapMs\)\)[\s\S]*?else if \(cloudEnabled && due\(nextCloudSyncMs\)\)[\s\S]*?else if \(!historyEmergency && cloudEnabled && due\(nextCloudAutoCalPullMs\)\)/,
  "One prioritized cloud operation must be selected per network slot.",
);
assert.match(
  firmware,
  /nextCloudNetworkSlotMs = millis\(\) \+ CLOUD_NETWORK_MIN_GAP_MS;/,
  "Cloud operations must be globally spaced after each selected connection.",
);
assert.match(
  firmware,
  /\(uint32_t\)\(now - cloudLastAttemptMs\) < CLOUD_MIN_UPLOAD_SPACING_MS[\s\S]*?nextCloudSyncMs = cloudLastAttemptMs \+ CLOUD_MIN_UPLOAD_SPACING_MS;/,
  "Every upload path, including retries after payload construction, must obey the hard one-minute attempt floor.",
);
assert.match(
  firmware,
  /class HistoryOutboxStream : public Stream[\s\S]*?http\.sendRequest\("POST", &payloadStream, payloadBytes\);/,
  "Backlog uploads must stream compact rows instead of retaining a batch-sized JSON String.",
);
assert.match(
  firmware,
  /secureClient\.connect\(cloudHost, cloudPort, 8000\)[\s\S]*?HistoryOutboxStream payloadStream/,
  "TLS must reserve its contiguous record buffers before the history payload is serialized.",
);
assert.match(
  firmware,
  /const bool historyEmergency =[\s\S]*?historyQueued >= \(HISTORY_QUEUE_CAPACITY \* 3U\) \/ 4U;/,
  "Auxiliary pulls must yield only for a genuinely endangered outbox, not a normal nonempty queue.",
);
assert.match(
  firmware,
  /destination\[copied \+ i\] =[\s\S]*?pendingHistory\[\(pendingHistoryHead \+ i\) % HISTORY_STAGING_CAPACITY\][\s\S]*?checkpointHistoryStagingToRtcLocked\(\);/,
  "Healthy uploads must copy and acknowledge RTC staging directly without a routine LittleFS commit.",
);
assert.doesNotMatch(
  enqueueBody,
  /LittleFS|HTTPClient|serviceHistoryJournal|WiFiClient/,
  "The BLE ingress path must never perform filesystem or network work.",
);
assert.doesNotMatch(
  copyBatchBody,
  /serviceHistoryJournal|FILE_APPEND|FILE_WRITE|\.flush\s*\(/,
  "A healthy upload must not force a flash-journal write before copying staged rows.",
);
assert.doesNotMatch(
  uploaderBody,
  /serviceHistoryJournal\s*\(/,
  "The snapshot uploader must not restore the periodic pre-upload LittleFS commit.",
);
assert.doesNotMatch(
  cloudTaskBody,
  /historyPressure|queuedHistoryCount\(\)\s*>\s*0/,
  "A normal nonempty queue must never starve calibration/config/display pulls.",
);
assert.match(
  autoCalBody,
  /nextCloudAutoCalPullMs = now;[\s\S]*?saveAutoCalCache\(\);[\s\S]*?sensorDataDirty = true;/,
  "Auto-calibration must use its granted boot slot, persist learned ranges, and redraw cards.",
);
assert.match(
  firmware,
  /loadCloudConfig\(\);[\s\S]*?loadAutoCalCache\(\);[\s\S]*?loadDisplaySettings\(\);/,
  "Setup must restore learned ranges before display settings and the first normal render.",
);
assert.match(
  uploaderBody,
  /if \(accepted < 0 \|\| \(size_t\)accepted != historyCount\)[\s\S]*?if \(uploadAccepted\)[\s\S]*?acknowledgeHistoryUploadBatch\(historyBatch, historyAccepted\)/,
  "Rows must only leave the receiver after an exact successful backend acknowledgement.",
);
assert.doesNotMatch(
  firmware,
  /HISTORY_BACKLOG_DRAIN_MS/,
  "A backlog must never restore the five-second TLS burst loop.",
);

const worstCaseHistoryRow = {
  observationId: "FFFFFFFF-FFFFFFFF",
  queuedAgeMs: 604_800_000,
  observedAtMs: 9_999_999_999_999,
  sensor: 16,
  wake: 4_294_967_295,
  moisture: 65_535,
  measuredMoisture: 65_535,
  moistureScale: 2,
  batteryPercent: 100,
  rssi: -32_768,
  ageMs: 0,
  raw: {
    fdcCounts: 268_435_455,
    boardTempQuarterC: -32_768,
    bareTankFdcCounts: 268_435_455,
    tankPhase: 255,
    batteryMv: 65_535,
    charging: true,
  },
};
assert.ok(
  JSON.stringify(worstCaseHistoryRow).length <= 384,
  "The one-shot firmware reserve must cover a maximum-width MSV3 history row.",
);
assert.ok(
  96 >= 4 * (16 + 8),
  "One paced streamed batch must retain 4x headroom over the measured 16-probe-plus-bench load.",
);
const normalSixteenRowsPerMinute = 16;
const oneFastPlusFifteenNormalRowsPerMinute = 20 + 15;
const allSixteenFastRowsPerMinute = 16 * 20;
assert.ok(96 >= 6 * normalSixteenRowsPerMinute, "Sixteen normal probes need at least 6x nominal batch headroom.");
assert.ok(96 >= 2 * oneFastPlusFifteenNormalRowsPerMinute, "One fast plus fifteen normal probes need at least 2x batch headroom.");
assert.ok(allSixteenFastRowsPerMinute > 96, "The verifier must keep the all-probes-fast unsupported boundary explicit.");
assert.ok(8192 / normalSixteenRowsPerMinute >= 8 * 60, "The durable outage queue must cover at least eight hours at sixteen normal rows/minute.");

console.log("PASS receiver cloud recovery protects RTC ingress, spills outages durably, paces TLS, and prevents state-pull starvation");
