import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const firmwarePath = path.join(
  repoRoot,
  "arduino",
  "Treebeard_ESP32_Display_Runtime",
  "Treebeard_ESP32_Display_Runtime.ino",
);
const source = fs.readFileSync(firmwarePath, "utf8");

function check(condition, message) {
  if (!condition) throw new Error(message);
}

check(source.includes("formatBatteryPercent(batteryPct, out, outSize);"), "compact battery header formatter is missing");
check(!source.includes('snprintf(out, outSize, "Batt: %s", percent);'), 'legacy "Batt:" header label is still present');
check(source.includes("const int CARD_INFO_COLUMN_WIDTH = 24;"), "compact battery/temperature column width is missing");
check(!/drawBar\([^\n]*batteryPct/.test(source), "a battery progress bar is still rendered");
check(!/drawTextLeft\("Moist"/.test(source), 'legacy "Moist" label is still rendered');
check(source.match(/drawTextLeft\("Moisture"/g)?.length >= 4, 'the full "Moisture" label is not used in every card path');
check(source.includes("max(24, h - 57)"), "expanded art-region geometry is missing");

function gridForCardCount(count) {
  if (count === 1) return [1, 1];
  if (count === 2) return [2, 1];
  if (count === 3) return [3, 1];
  if (count === 4) return [2, 2];
  if (count <= 6) return [3, 2];
  if (count <= 8) return [4, 2];
  if (count === 9) return [3, 3];
  if (count <= 12) return [4, 3];
  return [4, 4];
}

const panelWidth = 480;
const panelHeight = 320;
const padding = 6;
const gap = 6;
const top = 34;
for (let count = 1; count <= 16; count += 1) {
  const [columns, rows] = gridForCardCount(count);
  const cardWidth = Math.trunc((panelWidth - padding * 2 - gap * (columns - 1)) / columns);
  const cardHeight = Math.trunc((panelHeight - top - padding - gap * (rows - 1)) / rows);
  const compact = cardHeight < 76;
  const inset = compact ? 5 : 7;
  const innerWidth = cardWidth - inset * 2;
  check(innerWidth >= 24, `${count} cards: 100% does not fit the compact header column`);

  if (compact) {
    let moistureY = Math.max(18, cardHeight - 25);
    let barY = moistureY + 13;
    if (barY + 5 > cardHeight - 4) {
      const shift = barY + 5 - (cardHeight - 4);
      moistureY -= shift;
      barY -= shift;
    }
    moistureY = Math.max(17, moistureY);
    barY = Math.max(moistureY + 11, barY);
    check(moistureY >= 17, `${count} cards: compact moisture label overlaps the header`);
    check(barY + 5 <= cardHeight - 4, `${count} cards: compact moisture bar leaves the card`);
    continue;
  }

  const artTop = 24;
  const artHeight = Math.max(24, cardHeight - 57);
  const artBottom = artTop + artHeight;
  const moistureLabelY = cardHeight - 29;
  const moistureBarY = cardHeight - 14;
  check(artBottom <= moistureLabelY - 4, `${count} cards: sprite and moisture label overlap`);
  check(moistureBarY + 8 <= cardHeight - 6, `${count} cards: moisture bar leaves the card`);
  check(innerWidth >= 82, `${count} cards: Moisture plus a five-digit value cannot fit`);
}

console.log("Display card layout contract OK for 1-16 cards on the 480x320 panel.");
