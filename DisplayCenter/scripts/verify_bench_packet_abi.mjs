// The MSV3 bench advertisement is an ABI shared by three decoders that live in three languages
// and three subsystems:
//
//   1. MSV3/arduino/MSV3_Probe_Bringup_Test/MSV3_Probe_Bringup_Test.ino   bleStartAdv()  (writer)
//   2. DisplayCenter/.../Treebeard_ESP32_Display_Runtime.ino  parseBenchAdvertisement()  (reader)
//   3. scripts/ble_periodic_reader.py                        parse_msv3_manufacturer_data()
//
// CLAUDE.md records what happens when a layout like this is changed on one side only: during the
// wired-DFU work three scan-response bytes were reused across builds without updating the reader,
// and four readings were mislabelled - one of which produced a confident, wrong sentence about a
// field that did not exist in the build being read. This check exists so that cannot recur
// silently here.
//
// It is a SOURCE-TEXT check, like its sibling verifiers. It cannot prove the decoders agree at
// runtime - the synthetic self-test in the bench decoder's own docs does that - but it does prove
// the three files still agree about field offsets, flag bits and the format version, which is
// where drift actually starts.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const displayCenterRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = resolve(displayCenterRoot, "..");

const probe = await readFile(join(
  repoRoot, "MSV3", "arduino", "MSV3_Probe_Bringup_Test", "MSV3_Probe_Bringup_Test.ino",
), "utf8");
const receiver = await readFile(join(
  displayCenterRoot, "arduino", "Treebeard_ESP32_Display_Runtime",
  "Treebeard_ESP32_Display_Runtime.ino",
), "utf8");
const reader = await readFile(join(repoRoot, "scripts", "ble_periodic_reader.py"), "utf8");
const app = await readFile(join(
  displayCenterRoot, "arduino", "Treebeard_ESP32_Display_Runtime", "MobileAppPage.h",
), "utf8");

// ---- The bench magic must stay distinct from production's ----
//
// This is the safety property, not a naming preference: a bring-up probe sitting in air next to a
// running receiver must be dropped before the receiver ever looks at its sensor id. If these two
// ever become the same string, bench firmware can write real plant history.
assert.match(probe, /const uint8_t BLE_MAGIC\[4\] = \{'M', 'S', 'V', '3'\};/,
  "Bring-up firmware must advertise the MSV3 bench magic, never the production MSV2 one.");
assert.match(receiver, /const uint8_t MSV3_BENCH_MAGIC\[\] = \{ 'M', 'S', 'V', '3' \};/);
assert.match(receiver, /const uint8_t MSV2_MAGIC\[\] = \{ 'M', 'S', 'V', '2' \};/);

// ---- The bench parse path must stay isolated from the production one ----
//
// parseBenchAdvertisement() may not touch the arrays and flags that reach history or the cloud.
// Checked by absence within the function body, which is crude but catches the change that
// matters: someone "helpfully" making bench probes show up on the dashboard.
const benchFn = receiver.match(
  /void parseBenchAdvertisement\(const uint8_t\* buffer, size_t len, int packetRssi\)\s*\{[\s\S]*?\n\}/,
);
assert.ok(benchFn, "parseBenchAdvertisement() must exist with its documented signature.");
// Comments are stripped first. The function deliberately NAMES these symbols in prose to explain
// why it does not use them, and a check that cannot tell an explanation from a call would punish
// the documentation it depends on.
const stripComments = (source) => source
  .replace(/\/\*[\s\S]*?\*\//g, " ")
  .replace(/\/\/[^\n]*/g, " ");
const benchBody = stripComments(benchFn[0]);
for (const forbidden of ["sensors[", "sensorDataDirty", "pendingHistory", "enqueueHistory", "durableHistory"]) {
  assert.ok(
    !benchBody.includes(forbidden),
    `parseBenchAdvertisement() must never reference ${forbidden} - a bench probe's numbers must ` +
    "never reach plant history or the cloud. See MSV3_BENCH_MAGIC's comment.",
  );
}

// ---- Packet geometry ----
assert.match(probe, /uint8_t mfg\[23\];/, "The bench packet is 23 bytes.");
assert.match(probe, /"MSV3 bench advertising payload exceeds the 31-byte legacy PDU limit"/,
  "The bench packet must keep its compile-time 31-byte PDU budget assertion.");
assert.match(receiver, /const size_t MSV3_BENCH_PACKET_MIN_LEN = 17;/);
assert.match(receiver, /const size_t MSV3_BENCH_PACKET_V1_LEN = 23;/);
// bleak strips the 2-byte company ID, so the Python side's indices are the firmware's minus 2.
// 23 - 2 = 21. If this ever disagrees the Python decoder silently stops reading the v1 fields.
assert.match(reader, /if len\(payload\) >= 21:/,
  "The Python decoder's wide-packet gate must equal the firmware's 23 bytes minus bleak's " +
  "2-byte company-ID strip.");

// ---- Flag bits, all three sides ----
const flagBits = [
  ["CHARGING", 0],
  ["BOARD_TEMP_OK", 2],
  ["OTA_POWER_OK", 4],
  ["READING_INVALID", 5],
  ["LIVE_MODE", 7],
];
for (const [name, bit] of flagBits) {
  const cDefine = new RegExp(`#define BENCH_ADV_FLAG_${name}\\s+\\(1U << ${bit}\\)`);
  assert.match(probe, cDefine, `Probe firmware must define BENCH_ADV_FLAG_${name} as bit ${bit}.`);
  assert.match(receiver, cDefine, `Receiver must define BENCH_ADV_FLAG_${name} as bit ${bit}.`);
  const pyDefine = new RegExp(`MSV3_BENCH_FLAG_${name} = 1 << ${bit}`);
  assert.match(reader, pyDefine, `ble_periodic_reader.py must define ${name} as bit ${bit}.`);
}

// The charging bit must be MASKED, not truthiness-tested. Before the OTA power gate existed a
// bare `payload[14] != 0` was correct; afterwards it reported charging for any probe whose gate
// was merely open.
assert.match(reader, /"charging": bool\(flags & MSV3_BENCH_FLAG_CHARGING\)/,
  "The Python decoder must mask the charging bit out of the flags byte, not test the whole byte.");

// ---- Format version ----
//
// Byte [19] distinguishes a versioned bench packet from production's zeroed legacy field. Every
// v1-only field must be gated on it, not on length alone - length says the bytes are present,
// version says what they mean.
assert.match(probe, /const uint8_t BENCH_ADV_FORMAT_VERSION = 1;/);
assert.match(receiver, /const uint8_t MSV3_BENCH_FORMAT_VERSION_1 = 1;/);
assert.match(reader, /MSV3_BENCH_FORMAT_VERSION_1 = 1/);
assert.match(receiver, /if \(buffer\[19\] >= MSV3_BENCH_FORMAT_VERSION_1\)[\s\S]{0,220}muxCode = buffer\[20\][\s\S]{0,120}fdcStatus = buffer\[21\]/,
  "The receiver must read mux code and FDC status only behind the format-version gate.");
assert.match(reader, /if payload\[17\] >= MSV3_BENCH_FORMAT_VERSION_1:[\s\S]{0,260}payload\[18\] & 0x03[\s\S]{0,120}struct\.unpack_from\("<H", payload, 19\)/,
  "The Python decoder must read mux code and FDC status only behind the format-version gate.");

// ---- Board temperature shares production's field, so it shares production's range gate ----
assert.match(receiver, /tempField >= -160 && tempField <= 340/);
assert.match(reader, /if -160 <= temp_q4 <= 340:/);
assert.match(probe, /const int32_t BOARD_TEMP_MIN_Q4C = -160;/);
assert.match(probe, /const int32_t BOARD_TEMP_MAX_Q4C = 340;/);

// ---- Command beacon ABI: receiver writes it, bring-up firmware reads it ----
//
// A third magic, distinct from both sensor magics, so a command can never be parsed as sensor
// data nor sensor data as a command.
assert.match(receiver, /const uint8_t BENCH_CMD_MAGIC\[\] = \{ 'T', 'B', 'C', 'M' \};/);
assert.match(probe, /const uint8_t BENCH_CMD_MAGIC\[4\] = \{'T', 'B', 'C', 'M'\};/);
for (const [name, value] of [["TARGET_ALL", "0xFF"], ["OP_IDENTIFY", "0x01"]]) {
  assert.match(receiver, new RegExp(`const uint8_t BENCH_CMD_${name} = ${value};`),
    `Receiver must define BENCH_CMD_${name} as ${value}.`);
  assert.match(probe, new RegExp(`const uint8_t BENCH_CMD_${name} = ${value};`),
    `Probe must define BENCH_CMD_${name} as ${value}.`);
}
assert.match(receiver, /const size_t BENCH_CMD_PACKET_LEN = 14;/);
assert.match(probe, /const uint8_t BENCH_CMD_PACKET_LEN = 14;/);
// The session id is what makes the counter meaningful. Without it the receiver's
// counter restarts at 1 each boot and collides with the last value a probe acted on,
// which measurably dropped the first identify after a receiver reflash.
assert.match(receiver, /payload\[12\] = \(uint8_t\)\(benchCommandSessionId & 0xFF\);/);
assert.match(probe, /const uint16_t session = \(uint16_t\)\(buf\[12\] \| \(buf\[13\] << 8\)\);/);
assert.match(probe, /session == g_benchCmdLastSession/,
  "The probe must compare the command counter only within one receiver session.");
assert.ok(!probe.includes("BENCH_CMD_RESTART_GAP"),
  "The backwards-jump reboot heuristic is replaced by the session id; it must not return.");

// The counter routinely contains zero bytes. String(ptr) would truncate the payload mid-field and
// the probe would drop it as short - a failure that looks exactly like a radio problem.
assert.match(receiver, /setManufacturerData\(String\(\(const char\*\)payload, sizeof\(payload\)\)\)/,
  "The command beacon must build its manufacturer data with the length-taking String " +
  "constructor; the counter contains zero bytes.");
// The receiver runs no GATT server, so its beacon must not be connectable - and the constant is
// backend-dependent, because setAdvertisementType() forwards to adv_type under Bluedroid and to
// conn_mode under NimBLE. Passing the wrong one compiles and silently misconfigures the radio.
assert.match(receiver, /#if defined\(CONFIG_NIMBLE_ENABLED\)\s*\r?\n\s*advertising->setAdvertisementType\(BLE_GAP_CONN_MODE_NON\);\s*\r?\n#elif defined\(CONFIG_BLUEDROID_ENABLED\)\s*\r?\n\s*advertising->setAdvertisementType\(ADV_TYPE_NONCONN_IND\);/,
  "The command beacon must select its non-connectable constant per BLE backend: " +
  "BLE_GAP_CONN_MODE_NON for NimBLE, ADV_TYPE_NONCONN_IND for Bluedroid.");

// BLEScanner only auto-resumes on the branch where no rx callback is installed, so a callback
// that returns without resuming stops scanning permanently after the first report it hears.
const scanCb = probe.match(/void benchCommandScanCallback\(ble_gap_evt_adv_report_t\* report\)\s*\{[\s\S]*?\n\}/);
assert.ok(scanCb, "benchCommandScanCallback() must exist.");
assert.match(scanCb[0], /Bluefruit\.Scanner\.resume\(\);\s*\r?\n\}/,
  "benchCommandScanCallback() must resume the scanner on its way out, unconditionally - see " +
  "BLEScanner.cpp's invoke_cb branch.");
// Scanning is a central-role activity; without the central slot Bluefruit.Scanner never starts.
assert.match(probe, /Bluefruit\.begin\(1, 1\)/,
  "The bring-up image must enable the central slot so BLEScanner can run.");
// The scanner must stay off outside bench mode: `sleepbench` and `wakeprofile` MEASURE current.
assert.match(probe, /if \(Bluefruit\.connected\(\) \|\| !g_benchLiveActive\) benchScannerStop\(\);/,
  "The command scanner must be off whenever bench live mode is off or a connection is up - a " +
  "scanning radio would invalidate this sketch's own power measurements and compete with DFU.");

// ---- The Bench view is a WEB page, and must not be reachable from the display ----
//
// Explicit user requirement (2026-09-21): the panel shows plant data only. This check exists
// because the first implementation DID put it on the display, and a later change could
// plausibly try to "restore" that.
for (const symbol of ["drawBenchPage", "benchPageOpen", "BenchTouchTarget", "drawBenchChip", "openBenchPage"]) {
  assert.ok(
    !receiver.includes(symbol),
    `${symbol} must not exist: the Bench view is served at /bench, not drawn on the panel.`,
  );
}
assert.ok(
  !/OverlayTouchTarget::Bench/.test(receiver),
  "The brightness overlay must not carry a Bench entry point.",
);

// The data routes that back the app's Bench tab.
assert.match(receiver, /server\.on\("\/bench-data", HTTP_GET/);
assert.match(receiver, /server\.on\("\/bench-identify", HTTP_POST/);
// The view is the app's Bench TAB, not a second page. One implementation only: a separate
// /bench page existed briefly and would have drifted from the tab.
assert.ok(!receiver.includes("TREEBEARD_BENCH_PAGE_HTML"),
  "There must be no standalone bench page; the Bench tab in MobileAppPage.h is the one view.");
for (const needle of ['<button id="benchTab"', 'id="benchView"', 'id="benchList"', 'id="benchStatus"',
                      "startBenchPolling()", "stopBenchPolling()", "/bench-data", "/bench-identify"]) {
  assert.ok(app.includes(needle), `MobileAppPage.h must contain ${needle} for the Bench tab.`);
}
// Polling must be scoped to the visible tab: the receiver serves this from the same
// single-threaded loop that drives the display and the cloud uploader.
assert.match(app, /if \(name === "bench"\) startBenchPolling\(\);[\s\S]{0,20}else stopBenchPolling\(\);/,
  "setTab() must start bench polling only for the bench tab and stop it otherwise.");

// Identify must be addressed to one probe. Broadcasting to every listener is not identification.
assert.match(receiver, /benchSendCommand\(\(uint8_t\)sensorId, BENCH_CMD_OP_IDENTIFY\)/,
  "/bench-identify must address the requested sensor id, never BENCH_CMD_TARGET_ALL.");
assert.ok(
  !/benchSendCommand\([^)]*BENCH_CMD_TARGET_ALL/.test(receiver),
  "Nothing may send a bench command to BENCH_CMD_TARGET_ALL.",
);

// A placeholder advertisement must omit the count, not send 0. Sending 0 would let the page
// render a fabricated reading even by accident.
assert.match(
  receiver,
  /if \(snapshot\[i\]\.readingValid\)[\s\S]{0,40}probe\["fdcCounts"\] = snapshot\[i\]\.fdcCounts;/,
  "benchDataJson() must omit fdcCounts when the reading is not valid.",
);
// v1-only fields are gated on the format version, not on the presence of bytes.
assert.match(
  receiver,
  /if \(snapshot\[i\]\.formatVersion >= MSV3_BENCH_FORMAT_VERSION_1\)[\s\S]{0,40}probe\["muxCode"\]/,
  "benchDataJson() must gate muxCode/fdcStatus on the bench format version.",
);

// The command's broadcast window has to close on its own, from loop(), now that no page owns it.
assert.match(receiver, /serviceBuzzer\(now\);[\s\S]{0,320}serviceBenchCommandBroadcast\(\);/,
  "serviceBenchCommandBroadcast() must be called unconditionally from loop().");

console.log(
  "PASS MSV3 bench packet ABI agrees across probe/receiver/python, stays isolated from plant " +
  "history, and the Bench view is web-only",
);
