/*
  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.
  Copyright (c) 2018, Adafruit Industries (adafruit.com)

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

// Project Treebeard MSV4 probe (E73-2G4M08S1C on the MSV4 PCB).
//
// Derived from Adafruit nRF52 core 1.7.0 variants/pca10056. This file is TRACKED in the repo and
// selected with `--build-property build.variant.path=<this folder>` by
// MSV4/scripts/arduino_msv4_probe.ps1. It deliberately does not touch the installed core's
// pca10056 variant, which MSV3/scripts/arduino_msv3_probe.ps1 rewrites to USE_LFRC before every
// MSV3 build. The two generations need opposite LFCLK sources and must never share a variant file.
//
// Differences from pca10056, each for a board reason:
// - USE_LFXO: Y1 (32.768 kHz, SC-32S) is populated on P0.00/XL1 and P0.01/XL2 on MSV4. MSV3 has no
//   crystal; building an MSV3 sketch against this variant would reproduce the RTC1 hang in
//   MSV3/docs/MSV3_LFCLK_RTC1_HANG_2026-08-21.md, which is why MSV3 sketches #error on USE_LFXO.
// - LED is P1.10, active HIGH (R10 470R -> D1). The dev-kit LEDs on P0.13-P0.16 are gone: on
//   MSV4, P0.13 is SWAP_CTRL and P0.15 is SWAP_nOE, and pca10056's initVariant() drove them.
// - No QSPI flash, buttons or Serial2 pin assignments. On this PCB those pins are READ_CNTL (P0.17),
//   READ_nOE (P0.20), TEMP_READ (P0.12) and VBAT_DIV_EN (P0.06).

#ifndef _VARIANT_MSV4_E73_
#define _VARIANT_MSV4_E73_

// Sketches assert on this so a build that silently fell back to the installed pca10056 variant
// fails at compile time instead of on the bench.
#define MSV4_VARIANT 1

/** Master clock frequency */
#define VARIANT_MCK       (64000000ul)

#define USE_LFXO    // Board uses 32khz crystal for LF (Y1, populated on MSV4)
// define USE_LFRC  // NEVER on MSV4 -- see header comment

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT           (48)
#define NUM_DIGITAL_PINS     (48)
#define NUM_ANALOG_INPUTS    (6)
#define NUM_ANALOG_OUTPUTS   (0)

// LEDs -- one real LED, P1.10 (Arduino pin 42), active high.
#define PIN_LED1             (42)
#define PIN_LED2             (42)

#define LED_BUILTIN          PIN_LED1
#define LED_CONN             PIN_LED2

#define LED_RED              PIN_LED1
#define LED_BLUE             PIN_LED2

#define LED_STATE_ON         1         // State when LED is litted

/*
 * Analog pins -- identical numbering to pca10056 so the core's analogRead() tables still compile.
 * MSV4 sketches use their own SAADC routine on AIN0/1/6/7 (P0.02/P0.03/P0.30/P0.31) instead.
 */
#define PIN_A0               (3)
#define PIN_A1               (4)
#define PIN_A2               (28)
#define PIN_A3               (29)
#define PIN_A4               (30)
#define PIN_A5               (31)
#define PIN_A6               (0xff)
#define PIN_A7               (0xff)

static const uint8_t A0  = PIN_A0 ;
static const uint8_t A1  = PIN_A1 ;
static const uint8_t A2  = PIN_A2 ;
static const uint8_t A3  = PIN_A3 ;
static const uint8_t A4  = PIN_A4 ;
static const uint8_t A5  = PIN_A5 ;
static const uint8_t A6  = PIN_A6 ;
static const uint8_t A7  = PIN_A7 ;
#define ADC_RESOLUTION    14

// Other pins
#define PIN_AREF           (2)
#define PIN_NFC1           (9)
#define PIN_NFC2           (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces. P1.01/P1.02 are unconnected on MSV4; kept so the core's Serial1 compiles.
 */
#define PIN_SERIAL1_RX      (33) // P1.01
#define PIN_SERIAL1_TX      (34) // P1.02

/*
 * SPI / Wire -- unconnected P1 pins on MSV4, kept so the core libraries compile.
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO         (46)
#define PIN_SPI_MOSI         (45)
#define PIN_SPI_SCK          (47)

static const uint8_t SS   = 44 ;
static const uint8_t MOSI = PIN_SPI_MOSI ;
static const uint8_t MISO = PIN_SPI_MISO ;
static const uint8_t SCK  = PIN_SPI_SCK ;

#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA         (26)
#define PIN_WIRE_SCL         (27)

#ifdef __cplusplus
}
#endif

#endif
