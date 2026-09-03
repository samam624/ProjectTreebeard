// One-off diagnostic: dump the contents of /device.json (and /cloud.json, for reference)
// from this board's LittleFS over Serial. Treebeard_ESP32_Display_Runtime.ino never exposes
// receiverSecret over HTTP or serial by design (see deviceInfoJson()), so pulling it off a
// board that's being physically retired/replaced requires a one-shot read like this instead.
//
// Read-only: never writes, formats, or removes anything. Uses LittleFS.begin(false) -
// deliberately NOT matching the runtime firmware's formatOnFail=true mount call, because
// this tool's whole purpose is rescuing /device.json/receiverSecret off a board that's
// failing, and formatOnFail would destroy that exact data on the one mount outcome (a
// failed mount) this tool actually exists to handle. A healthy filesystem mounts either
// way; only the failure case differs, and that's precisely the case a rescue tool must
// never write to.
#include <FS.h>
#include <LittleFS.h>
using fs::File;

void dumpFile(const char* path) {
  Serial.printf("--- %s ---\n", path);
  if (!LittleFS.exists(path)) {
    Serial.println("(missing)");
    return;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("(open failed)");
    return;
  }
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  uint32_t startMs = millis();
  while (!Serial && millis() - startMs < 5000) {
    delay(50);
  }
  delay(500);

  Serial.println();
  Serial.println("[identity dump] mounting LittleFS");
  if (!LittleFS.begin(true)) {
    Serial.println("[identity dump] LittleFS mount FAILED");
    return;
  }
  Serial.println("[identity dump] LittleFS mounted");
}

void loop() {
  Serial.println();
  Serial.println("=== MSV2 identity dump ===");
  dumpFile("/device.json");
  dumpFile("/cloud.json");
  Serial.println("=== end dump (repeats every 5s) ===");
  delay(5000);
}
