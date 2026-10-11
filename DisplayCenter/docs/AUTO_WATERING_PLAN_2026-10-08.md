# Automatic watering from a reservoir — plan and parts checklist, 2026-10-08

Idea stage. Nothing here has been bought, built or tested. Written so the parts list can be
checked against what the school lab has.

**The idea (maintainer, 2026-10-08):** a bucket of water as a reservoir, a pump that waters the
plants on a signal from the DisplayCenter, and an MSV3 probe in the bucket as the level sensor.

Schematic: [PUMP_DRIVER_SCHEMATIC.png](PUMP_DRIVER_SCHEMATIC.png), drawn by
`DisplayCenter/scripts/pump_driver_schematic.py`.

## How it is meant to work

An ESP32 pin switches a small 12 V DC pump through a low-side N-channel MOSFET. The pump is a
brushed DC motor, which electrically is winding resistance, winding inductance and a back-EMF in
series, so it needs a flyback diode across it for the moment the MOSFET turns off.

## Parts to look for at school

| # | Part | What to look for | Notes |
|---|---|---|---|
| 1 | **N-channel MOSFET, logic-level** | One that turns on fully from a 3.3 V gate. Usually an "L" in the name: IRLZ44N, IRL540N, FQP30N06L, IRLB8721. Rated at least 30 V and a few amps | **Not** a plain IRF520 / IRF540 (they want about 10 V on the gate). **Not** a 2N7000 or BS170 (too small for the pump's start-up current) |
| 1b | Fallback if there is no logic-level MOSFET | An NPN Darlington such as TIP120 or TIP122, with a base resistor of about 1 kΩ | Works from 3.3 V but drops about a volt, so the pump runs a little slower |
| 2 | **Flyback diode** | 1 A or more, rated well above 12 V. A Schottky (1N5817, 1N5819) is the usual pick; a 1N4001–1N4007 also works | Stripe to +12 V, other end to the pump's negative terminal |
| 3 | **Pull-down resistor** | 10 kΩ | From the ESP32 pin to ground, so the pump stays off while the ESP32 boots or resets |
| 4 | **Gate resistor** | 100 Ω to 220 Ω | Between the ESP32 pin and the MOSFET gate |
| 5 | Ceramic capacitor | 100 nF | Across the pump terminals, to quiet brush noise |
| 6 | Electrolytic capacitor | 100 µF to 470 µF, 25 V or more | Across the 12 V supply near the driver |
| 7 | **12 V supply** | A wall adapter rated 1 A or more, with a barrel-jack-to-screw-terminal adapter | A 12 V battery also works; see below |
| 8 | Fuse and holder | 1 A to 2 A, inline | **Required with a battery**, at its positive terminal |
| 9 | 12 V → 5 V buck converter | Any small module | Only if the ESP32 is powered from the same 12 V. Otherwise power the ESP32 from USB and join the grounds |
| 10 | ESP32 dev board, breadboard, jumper wires, screw terminals | | A breadboard is fine at this current |
| 11 | Multimeter | | Measure the pump's winding resistance: start-up current is supply voltage divided by that |

Items 1, 3 and 4 can be replaced by a ready-made "MOSFET trigger switch drive module" (the dual
AOD4184 board), which takes a 3.3 V control signal directly. It still needs the diode.

## Parts that will have to be bought

| Part | What to look for |
|---|---|
| **Pump** | A 12 V **two-wire DC** peristaltic dosing pump (the Kamoer NKP series is the common one; listings give 37 mL/min at 0.25 A for the NKP-DC-S06B). Not a four-wire stepper version, which needs a different driver |
| Tubing | Silicone tube that matches the pump head, long enough to reach from the bucket to the pots |

Why peristaltic: it cannot siphon when stopped, it sits outside the bucket, its flow is steady
enough that run time means volume, and running dry does not hurt it.

## Rules the firmware should follow

- A watering command is "run for N seconds", never "on" followed by "off". The limit is enforced
  at the pump end.
- Hard limits: longest single run, most per day, shortest gap between runs.
- No watering on a stale or invalid moisture reading, or with the reservoir reading low.
- Water in short pulses and wait for the soil reading to respond before adding more.
- The pump is off at boot (the pull-down does this in hardware).

## Not decided

- **Wall adapter or battery.** The pump uses very little; an always-on ESP32 is what would drain
  a battery. A battery also makes the flow drift with its voltage.
- **Where the driver lives.** A spare pin on the DisplayCenter if the bucket is within wire
  reach (not GPIO 12–16, which the display uses, and not the boot-strapping pins 0, 3, 45, 46),
  or a separate small pump node that takes commands over Bluetooth.
- **The level sensor.** An MSV3 probe in the bucket needs new firmware and receiver support to
  report a level, should be powered from USB, and is better trusted as a low-level cut-off than
  as a gauge, because water film and scale on the blade read as "wet".
