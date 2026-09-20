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

#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include "nrf.h"

const uint32_t g_ADigitalPinMap[] =
{
  // P0
  0 , 1 , 2 , 3 , 4 , 5 , 6 , 7 ,
  8 , 9 , 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23,
  24, 25, 26, 27, 28, 29, 30, 31,

  // P1
  32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47
};

// Runs from the core's init(), before setup(). Only pins whose floating state costs something are
// driven here; everything the board already biases with a resistor is left alone:
// - SWAP_nOE (P0.15) / READ_nOE (P0.20) have 100k pull-UPS -> switches isolated. Leave floating.
// - SWAP_CTRL (P0.13) / READ_CNTL (P0.17) have 100k pull-downs. Leave floating.
// - VBAT_DIV_EN (P0.06) has NO pull resistor and gates Q1's N-FET. A floating gate can half-enable
//   the high-side switch and leak the battery through the 200k divider, so drive it LOW.
// - DRV (P0.07) and TEMP_READ (P0.12) drive divider tops; drive LOW so neither sources current.
// - LED (P1.10) off.
void initVariant()
{
  digitalWrite(6, LOW);
  pinMode(6, OUTPUT);
  digitalWrite(7, LOW);
  pinMode(7, OUTPUT);
  digitalWrite(12, LOW);
  pinMode(12, OUTPUT);

  // wiring.c init() has just set every OUT bit high, so preload low before enabling the driver or
  // the LED flashes at each boot.
  digitalWrite(PIN_LED1, LOW);
  pinMode(PIN_LED1, OUTPUT);
}
