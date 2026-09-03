/* MSV3 E73-2G4M08S1C custom-PCB definition for Adafruit_nRF52_Bootloader 0.11.0. */
#ifndef MSV3_E73_H
#define MSV3_E73_H

/* The only board LED is D2 on P1.10 through 470 ohms, active high. */
#define LEDS_NUMBER       1
#define LED_PRIMARY_PIN   PINNUM(1, 10)
#define LED_STATE_ON      1

/* There are no DFU buttons. Recovery is requested by GPREGRET or an invalid app. */

#define BLEDIS_MANUFACTURER "Project Treebeard"
#define BLEDIS_MODEL        "MSV3 E73"

/* Native USB is wired on MSV3. Retain the known-working PCA10056 VID/PID pair. */
#define USB_DESC_VID          0x239A
#define USB_DESC_UF2_PID      0x00DA
#define USB_DESC_CDC_ONLY_PID 0x00DA

#define UF2_PRODUCT_NAME "Project Treebeard MSV3"
#define UF2_VOLUME_LABEL "MSV3BOOT"
#define UF2_BOARD_ID     "project-treebeard-msv3-e73"
#define UF2_INDEX_URL    "https://github.com/adafruit/Adafruit_nRF52_Bootloader"

#endif
