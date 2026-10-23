# Project Treebeard: engineering evidence map

This is the short path through Project Treebeard for hardware, validation and mixed-signal roles.
The full repository is an engineering archive; the links below lead directly to representative
design artifacts and measured results.

## PCB design and bring-up

- [MSV4 PCB package](../../MSV4/hardware/MSV4_Probe_PCB/README.md): schematic, layout, fabrication
  files, 3D views and photographs of assembled boards.
- [MSV4 board 1 bring-up](../../MSV4/docs/MSV4_BOARD1_BRINGUP_2026-10-01.md): rail checks,
  resistance-based fault isolation and rework of three hidden fine-pitch solder faults, including a
  0 Ω short under R15.
- [ESP32-S3 receiver PCB](../../DisplayCenter/hardware/MSV2_ESP32_DisplayCenter/pcb/README.md):
  USB-C power, TFT interface, buzzer circuitry and BLE-to-cloud receiver hardware.

## Validation and reliability

- [Four-board mux triage](../../MSV3/docs/MSV3_FOUR_BOARD_MUX_TRIAGE_2026-08-31.md): a repeatable
  switching test separated a misleading initial result from the final all-four-boards-healthy
  verdict.
- [MSV3 production measurements](../../MSV3/docs/MSV3_PRODUCTION_FIRMWARE_2026-08-22.md): measured
  sleep current fell from about 65-71 µA to 10-15 µA after unused divider/status GPIOs were
  disconnected during sleep.
- [Pot-mass reference experiment](../../MSV4/docs/MSV4_MOISTURE_SLOPE_AND_THERMAL_FIT_2026-10-17.md):
  one pot, eight days and one watering used to compare candidate moisture metrics and document
  where the estimators did not match the reference closely enough. It is not an accuracy
  specification.

## Analog and power

- [MSV4 lock-in measurement notes](../../MSV4/docs/MSV4_LOCKIN_MEASUREMENT_NOTES_2026-10-01.md):
  eight-frequency excitation, analog switching, synchronous demodulation and fitted circuit models.
- [Raw-sweep model comparison](../../MSV4/docs/MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md): captured
  sweeps compared across lumped, constant-phase-element, RC-line and two-branch models, with
  residuals and limitations recorded.
- [MSV3 overvoltage root cause](../../MSV3/docs/MSV3_VIN_DC_OVERVOLTAGE_ROOT_CAUSE_2026-09-15.md):
  a 6 V panel drove the cell to 4.49 V against a 3.89 V limit, motivating the MSV4 change to a 2 V
  panel within the charger's input range.

## System demonstration

- [Interactive dashboard demo](https://msv2-plants.netlify.app/?demo=1): the production dashboard
  UI running on clearly labeled synthetic data, so no household pairing secret is required.
- [System overview](../../README.md#how-it-works): BLE probe, ESP32-S3 receiver, local display,
  cloud history and phone dashboard.
- [Contribution boundary](../../CONTRIBUTIONS.md): hardware ownership, AI-assisted implementation
  and the evidence standard used throughout the repository.

The live physical-state registry is [`MSV3/fleet.json`](../../MSV3/fleet.json). Dated documents are
historical records and are not used to infer current board locations or test setups.
