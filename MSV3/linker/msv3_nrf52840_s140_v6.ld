/*
 * Project Treebeard MSV3 application linker script.
 *
 * This is the Adafruit nRF52 core 1.7.0 nrf52840_s140_v6.ld layout with one
 * deliberately shared ABI: the final eight bytes of nRF52840 RAM are excluded
 * from the ordinary application RAM arena and placed in a NOLOAD section for
 * the boot-failure guard.  Bootloader release 1.0.3 reserves the same address,
 * so neither image's startup, heap, nor stack may overwrite the counter while
 * passing through a watchdog reset.
 *
 * Do not silently fall back to the stock linker script.  The guarded build
 * wrapper installs this tracked file under the core's linker directory and the
 * assertions below make a misplaced or resized state fail at link time.
 */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x26000, LENGTH = 0xED000 - 0x26000

  /* Keep the last eight bytes out of the application's stack/heap arena. */
  RAM (rwx) : ORIGIN = 0x20006000, LENGTH = 0x2003FFF8 - 0x20006000
  MSV3_BOOT_GUARD_RETAINED (rwx) : ORIGIN = 0x2003FFF8, LENGTH = 0x08
}

SECTIONS
{
  . = ALIGN(4);
  .svc_data :
  {
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM

  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM

  .msv3_boot_guard_retained (NOLOAD) :
  {
    PROVIDE(__msv3_boot_guard_retained_start__ = .);
    KEEP(*(.msv3_boot_guard_retained))
    PROVIDE(__msv3_boot_guard_retained_end__ = .);
  } > MSV3_BOOT_GUARD_RETAINED
/*
 * Insert after .heap, not after .data.  nrf52_common.ld defines .bss after
 * .data; inserting this fixed high-RAM section between them advances ld's
 * location counter to 0x20040000 and strands .bss outside RAM.  .heap is the
 * final ordinary RAM output section, so placing the reservation after it
 * leaves the normal data/bss/heap layout intact.
 */
} INSERT AFTER .heap;

ASSERT(ORIGIN(RAM) + LENGTH(RAM) == ORIGIN(MSV3_BOOT_GUARD_RETAINED),
       "MSV3 boot-guard retention must immediately follow application RAM")
ASSERT(ORIGIN(MSV3_BOOT_GUARD_RETAINED) == 0x2003FFF8,
       "MSV3 boot-guard retention moved from its bootloader ABI address")
ASSERT(SIZEOF(.msv3_boot_guard_retained) == LENGTH(MSV3_BOOT_GUARD_RETAINED),
       "MSV3 boot-guard retained state must occupy exactly eight bytes")

INCLUDE "nrf52_common.ld"
