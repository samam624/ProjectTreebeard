#pragma once

#include <bluefruit.h>

// Nordic legacy buttonless DFU service with a full-reset handoff. Adafruit's
// stock BLEDfu uses an application-to-bootloader soft jump (GPREGRET 0xB1).
// Bootloader 0.11.0 can retain nRF52840 ACL state across that jump and stall at
// its first flash operation. GPREGRET 0xA8 plus a system reset is the reset-safe
// OTA entry supported by the same bootloader.
class ResetSafeBLEDfu : public BLEService
{
public:
  ResetSafeBLEDfu();
  err_t begin() override;

private:
  BLECharacteristic control_;
};
