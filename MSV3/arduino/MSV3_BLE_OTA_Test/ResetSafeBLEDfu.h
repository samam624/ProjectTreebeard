#pragma once

#include <bluefruit.h>

// Nordic legacy buttonless DFU service with a full-reset handoff. Adafruit's
// stock BLEDfu uses an application-to-bootloader soft jump (GPREGRET 0xB1).
// Bootloader 0.11.0 can retain nRF52840 ACL state across that jump and stall at
// its first flash operation. GPREGRET 0xA8 plus a system reset is the reset-safe
// OTA entry supported by the same bootloader.
// Diagnostic record of the last authorize request, captured inside the BLE event callback and
// printed later from the application loop. Serial work inside the callback delays the ATT
// response and can itself change the behaviour being measured, so nothing is printed there.
struct ResetSafeDfuDiag
{
  volatile uint8_t checkpoint;   // highest point reached; see kDfuCp* in the .cpp
  volatile uint16_t connHandle;
  volatile uint16_t gattStatus;  // status handed to sd_ble_gatts_rw_authorize_reply
  volatile uint32_t replyRc;     // its return code
  volatile uint8_t op;
  volatile bool valid;
};

class ResetSafeBLEDfu : public BLEService
{
public:
  ResetSafeBLEDfu();
  err_t begin() override;
  void setDfuAllowed(bool allowed);

  // 2026-09-03: the reset is no longer performed inside the authorize callback. A successful
  // sd_ble_gatts_rw_authorize_reply() only means the SoftDevice ACCEPTED the ATT response, not
  // that it has been transmitted -- resetting immediately can cancel it in flight, leaving the
  // client waiting on a write that is never answered. The callback now authorizes and returns;
  // the application loop calls completeDfuReset() a short time later, once the response has
  // actually gone out. Doing a system reset from inside a BLE event handler was wrong anyway.
  bool dfuResetPending() const;
  void completeDfuReset();

  // Bench diagnostics only -- safe to call at any time; returns false when nothing was recorded.
  bool takeDiag(ResetSafeDfuDiag& out);

private:
  BLECharacteristic control_;
};
