// Minimal blink test for D2 (LED, 470ohm to GND) on P1.10 of the bare
// E73-2G4M08S1C module (MSV3 hardware) -- exists to confirm the UF2
// bootloader just installed via SWD actually accepts and boots a new
// application, not just that it enumerates.
//
// Uses direct nRF52840 port register access (NRF_P1->DIRSET/OUTSET/OUTCLR)
// rather than Arduino digitalWrite(), matching this repo's existing pattern
// in the moisture probe sketches -- this hardware has no board-specific
// Arduino variant of its own, so there's no meaningful "Arduino pin number"
// for P1.10 to map to; the port/bit is the only thing that's actually fixed.
//
// Build through scripts/arduino_msv3_probe.ps1 -Sketch
// arduino/MSV3_E73_LED_Test. It uses the pca10056 FQBN while enforcing LFRC;
// the stock variant's LFXO setting is unsafe because this module has no LF
// crystal.
// Board FQBN remains adafruit:nrf52:pca10056 (Nordic nRF52840 DK profile)
// to match the exact bootloader/SoftDevice (0.11.0, S140 6.1.1) flashed onto
// this chip -- a mismatched board profile could link the app at a flash
// address this bootloader doesn't expect.

#include <Arduino.h>

#if !defined(USE_LFRC) || defined(USE_LFXO)
#error "MSV3 requires USE_LFRC and no USE_LFXO. Build with scripts/arduino_msv3_probe.ps1; see docs/KNOWN_ISSUES.md."
#endif

#define LED_BIT (1UL << 10) // P1.10

void setup() {
  NRF_P1->DIRSET = LED_BIT;
}

void loop() {
  NRF_P1->OUTSET = LED_BIT;
  delay(500);
  NRF_P1->OUTCLR = LED_BIT;
  delay(500);
}
