# Moisture insertion settling investigation and V4 temperature plan

**Recorded:** 2026-08-24  
**Status:** Investigation plan retained; 2026-08-30 coating evidence strongly advanced one branch,
but the controlled soil re-entry test is still pending.

> **2026-08-30 update:** Water pockets were physically observed beneath the nail polish. Once the
> coating was removed, the probe's long drift reversed during drying and settled to a flat air
> baseline. This strongly supports the water-uptake/retention hypothesis below and explains why the
> abnormal behavior appeared in soil, but it does not yet eliminate soil-contact settling. See
> [MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md](MSV3_SOIL_INGRESS_AND_COATING_REMEDIATION_2026-08-30.md)
> for the repair procedure, acceptance test, and corrected V4 edge-sealing recommendation.

## Problem statement

Every tested architecture has shown the same broad behavior after a newly prepared probe is inserted
into soil: the reported moisture rises for roughly one to several days without watering, reaches a
resting point, and then behaves normally. This has appeared on the Amazon Capacitive Moisture Sensor
V1.2, MSV2, switched-electrode MSV2, and the FDC2214-based MSV3. MSV3 has risen by almost 10,000
display points in one installation.

The slow baseline rise is distinct from the smaller time-of-day-shaped modulation superimposed on it.
The slow common-mode behavior across unrelated electronics makes soil/probe physics more likely than
one shared firmware bug. Temperature can still explain part of the daily variation and must be logged
before it can be ruled in or out quantitatively.

## Evidence captured on 2026-08-24

- In the inspected MSV3 installation window, derived raw moisture rose from about 42,669 to 49,379
  while the underlying FDC2214 count fell from 19,733,075 to 19,062,059. A lower tank frequency/count
  means higher effective capacitance, so the movement has the correct sign for a real dielectric or
  parasitic-capacitance increase.
- Since tank capacitance is proportional to `1 / frequency^2`, that count change represents about a
  **7.2% increase in total effective tank capacitance**. With the present nominal tank, this is roughly
  366.6 pF to 392.9 pF, or about +26 pF. A full +10,000 displayed-point rise at this operating point
  would be about a +10.8% effective-capacitance change.
- The queried MSV3 rows that carried battery telemetry all reported `charging=false` (1,061 rows), so
  charging cannot explain that entire run.
- MSV3 uses the deterministic mapping `(24,000,000 - raw_fdc) / 100`; the display is not inventing the
  direction of the drift. See
  `arduino/Treebeard_ESP32_Display_Runtime/Treebeard_ESP32_Display_Runtime.ino` near line 6019.
- The AFE test firmware runs the FDC continuously, whereas production MSV3 wakes it on schedule. Both
  architectures exhibit the broad phenomenon, which makes one particular wake implementation a less
  likely universal cause.
- The current MSV3 blade is protected with nail polish. FR-4, solder mask, and organic coatings can
  absorb moisture in an initially fast and then slower process, changing parasitic capacitance.

## Ranked working hypotheses

1. **Soil-to-probe contact equilibration.** Insertion leaves air gaps and disturbed soil. Wetting,
   capillary redistribution, swelling, and mechanical settling progressively improve contact around
   the blade. Published capacitive-soil work has observed installation settling on the order of days.
2. **Water uptake by the blade stack-up/coating.** FR-4, solder mask, edges, vias, and nail polish are
   not perfect moisture barriers. Uptake close to the electrodes raises effective capacitance and can
   naturally produce a fast initial rise followed by a slow asymptote.
3. **Temperature.** Soil permittivity, conductivity, the inductor, capacitors, PCB, and oscillator all
   have temperature dependence. This is a strong candidate for the daily wiggle or changing slope,
   but ordinary daily cycling alone is a weak explanation for a monotonic multi-day asymptote.
4. **Ionic/conductive effects and electrode polarization.** Architecture and electrode switching can
   reduce these effects, but cannot eliminate dielectric uptake or contact settling. Cadence-dependent
   testing will determine whether cumulative excitation matters.
5. **Electronics, wake timing, supply, or charging.** These remain testable, but rank below the physical
   causes because the behavior spans different sensor front ends, power arrangements, and wake modes.

## Tests to run

Keep each vessel covered, do not move the probe, record pot mass, and hold temperature as constant as
practical. Change one variable per test.

### 1. Unpowered-in-soil test -- highest information value

1. Prepare a repeatable soil jar and insert a new/dry probe at a marked depth.
2. Take one immediate raw reading, then disconnect all power for 48-72 hours without touching it.
3. Reconnect and take a raw reading immediately, preserving position.

Interpretation:

- If it powers up near the eventual high/resting value, the shift occurred without excitation. Contact
  settling and/or material water uptake become the leading explanation.
- If it powers up near the original value and starts a fresh ramp only after measurements resume,
  excitation, polarization, self-heating, wake sequencing, or electronics move up the list.

### 2. Fixed capacitor electronics control

Replace the soil electrode with a stable C0G/NP0 dummy load near the expected sensor capacitance
(start around 68-82 pF, while keeping the installed 270 pF tank capacitor) and run the production wake
cycle for at least 72 hours.

- Stable dummy: FDC, reference clock, supply, wake sequence, and display pipeline are unlikely to cause
  the multi-day ramp.
- Similar ramp: investigate FDC settle/data-ready handling, tank-component temperature, supply, PCB
  leakage/contamination, and raw-to-display conversion before changing the blade.

### 3. Fixed-depth water/coating test

Clamp only the passive blade at a fixed depth in a covered, constant-temperature water vessel while
keeping the electronics and connector dry. Do not let the water line move.

- Continued multi-day capacitance rise in a geometrically fixed medium strongly implicates coating,
  board-edge, solder-mask, or FR-4 water uptake.
- Stability here but drift in soil favors soil contact/settling.

Run a bare/new blade against a blade that has already been soaked to equilibrium if possible.

### 4. Preconditioned versus dry blade

Place matched dry and pre-soaked blades in matched sealed soil jars. Swap electronics between blades
after settling if the connector allows it.

- Dry blade drifts, preconditioned blade does not: coating/PCB uptake.
- Both drift similarly: contact/soil redistribution.
- Drift follows electronics when swapped: electronics or firmware.

### 5. Measurement-cadence test

Run matched probes at, for example, one reading every 10 seconds and one every 10 minutes.

- Same curve versus wall-clock time: passive diffusion/contact/temperature mechanism.
- Same curve versus number of measurements: excitation/polarization/wake mechanism.
- Different curve versus both: multiple mechanisms are likely present.

### 6. Temperature-correlation test

Log actual soil temperature at the blade and electronics/tank temperature separately. First remove the
slow settling trend, then regress the remaining moisture residual against each temperature, including
possible time lag. A raw correlation with time of day is not enough because temperature, light,
evaporation, and charging opportunity co-vary.

### 7. FDC acquisition diagnostic

For a diagnostic build, log the electrode phase, raw FDC count, FDC status/error flags, battery voltage,
charge/power-good state, and precise wake-to-read timing. Compare the present fixed delay with polling
data-ready. Also compare both electrode phases explicitly; the production history should not silently
sample mostly one phase.

## Minimum data record for every test

- UTC timestamp and local timestamp
- sensor serial number, blade/coating build, insertion time, depth, and vessel ID
- untouched raw sensor value (FDC count or oscillator period/frequency)
- displayed/scaled moisture value
- electrode phase and measurement cadence
- blade/soil temperature and electronics/LC-tank temperature
- battery voltage, charging, and power-good state
- FDC status/error flags and wake-to-sample delay
- room temperature, room RH, and light if available
- vessel/pot mass and every watering or handling event

Do not fit a temperature-compensation coefficient until the insertion transient has been isolated. A
temperature model trained on the first few days could accidentally treat coating uptake or soil
settling as temperature response.

## V4 temperature sensor options

### Recommended architecture

Use **two temperature channels** if board area permits:

1. A **soil/blade sensor** on a small non-sensing tab at the same depth as the active electrode region.
2. A **board/tank sensor** close to the FDC2214, L2, and C17.

The first supports soil-moisture compensation; the second distinguishes LC/front-end drift and thermal
lag from real soil behavior. A sensor beside the radio or charger alone does not measure soil
temperature. The nRF52840's internal sensor is die temperature only and is specified at +/-5 degrees C,
so it is useful as a free diagnostic but not as the primary compensation sensor.

| Option | Best use | Interface / V4 fit | Important specifications | Main tradeoff |
|---|---|---|---|---|
| **TMP117** | Preferred integrated blade sensor; also excellent tank reference | I2C on the existing 3.3 V SDA/SCL bus; four selectable addresses | +/-0.1 C max from -20 to +50 C; 16-bit, 0.0078 C/LSB; about 15.5 ms conversion; 150 nA typical shutdown | Premium part and 2 x 2 mm WSON assembly; I2C is less attractive over a long detachable cable |
| **TMP112A** | Lower-cost, very-low-power local sensor | Same I2C bus; choose the A variant for its 3.3 V accuracy specification | +/-0.5 C max from 0 to +65 C at 3.3 V; 12-bit; 7.5 uA max active and 0.35 uA max shutdown | Less accurate, but probably adequate after per-unit offset calibration |
| **TMP1826 (WSON)** | Preferred remote/cabled blade sensor | 1-Wire-compatible; bus-powered mode can use data plus ground; has CRC and strong data-pin ESD rating | +/-0.2 C max from +10 to +45 C; 16-bit; 94 uA measuring, 1.3 uA shutdown | Needs a GPIO and new protocol; more complex firmware than sharing I2C |
| **10 kOhm 1% NTC** | Lowest-cost integrated sensor | GPIO-switched divider into a dedicated SAADC input | Accuracy depends on thermistor, reference resistor, placement, and calibration; essentially zero sleep current when switched off | Consumes an analog pin and needs calibration/linearization. V4's P0.03/AIN1 is already needed to repair the EC-divider routing, so allocate another AIN or an analog mux |
| **DS18B20** | Fastest external prototype and independent validation instrument | 1-Wire, normally three conductors; commonly available in metal probe assemblies | +/-0.5 C from -10 to +85 C; 9-12 bit; up to 750 ms at 12 bit | Bulky/slow and less accurate than TMP117/TMP1826; better for testing than the final compact PCB |
| **SHT45** | Optional enclosure ambient temperature + RH diagnostic | I2C, 1.08-3.6 V; can share the bus | Typical +/-0.1 C and +/-1% RH; 0.4 uA average at 1 Hz | The humidity port must remain exposed and clean; it cannot be conformally coated or buried in wet soil, so it does not replace the blade sensor |
| **nRF52840 TEMP peripheral** | Zero-BOM board/die diagnostic | Internal one-shot peripheral | 0.25 C resolution but only +/-5 C specified accuracy | Measures MCU die, not soil or the LC tank |

MCP9808 is another workable I2C fallback (2.7-5.5 V, +/-0.5 C maximum from -20 to +100 C,
0.1 uA typical shutdown), but TMP112A is smaller/lower-active-current and TMP117 is substantially more
accurate, so there is no clear reason to prefer MCP9808 unless availability or assembly drives it.

## Proposed V4 implementation

1. **Default choice:** place one TMP117 on a narrow soil-exposed but electrically non-sensing side tab,
   and optionally a second TMP117 beside L2/C17. Strap them to different addresses. The existing MSV3
   3.3 V I2C bus already has 4.7 kOhm pullups and the FDC2214 occupies 0x2A, so no extra MCU pins are
   required.
2. Keep the blade temperature IC and all digital traces outside the active capacitive field. Do not run
   SDA/SCL/VDD/GND between or directly behind the sensing electrodes. A fixed parasitic can be
   calibrated; temperature-dependent parasitic and digital edge injection are harder to remove.
3. Trigger the temperature conversion while the FDC is shut down. Finish the I2C transaction, put the
   temperature sensor into shutdown, return SDA/SCL to a quiet state, then wake and sample the FDC.
4. Waterproof the blade sensor and board edges with a characterized low-water-uptake encapsulant. A
   thin encapsulant adds thermal lag but readings are minutes apart, so stability matters more than
   sub-second response. Do not assume nail polish is a permanent moisture barrier.
5. If the sensing blade is detachable or connected by a meaningful cable length, prefer TMP1826 at the
   blade over extending I2C. Run data with a ground return, add connector ESD protection as needed, and
   keep the digital cable away from the LC sensor conductors.
6. Preserve raw values in telemetry. Add signed `soil_temp_centi_c` and `tank_temp_centi_c`, plus sensor
   validity bits. Do not transmit only a temperature-corrected moisture number.
7. During V4 validation, install an external DS18B20 or calibrated thermometer beside the blade as an
   independent reference. Characterize offsets in a stirred water bath at roughly 10, 20, 30, and 40 C.

### Current recommendation in one line

**TMP117 at the blade + TMP117 (or TMP112A) beside the LC tank; use TMP1826 instead of blade I2C when
the blade is remote/cabled; add SHT45 only if enclosure RH is worth measuring.**

## Sources

- Soil-sensor installation/contact settling example: <https://pmc.ncbi.nlm.nih.gov/articles/PMC5017457/>
- METER Group installation guidance on air gaps and soil contact:
  <https://metergroup.com/expertise-library/how-to-install-soil-moisture-sensors-faster-better-and-for-higher-accuracy/>
- PCB moisture absorption measured through capacitance:
  <https://imapsjmep.org/article/40197-a-technique-for-detecting-moisture-absorption-in-printed-circuit-boards/attachment/103968.pdf>
- TI FDC2214 datasheet: <https://www.ti.com/lit/ds/symlink/fdc2214.pdf>
- TI TMP117 datasheet: <https://www.ti.com/lit/ds/symlink/tmp117.pdf>
- TI TMP112 datasheet: <https://www.ti.com/lit/ds/symlink/tmp112.pdf>
- TI TMP1826 datasheet: <https://www.ti.com/lit/ds/symlink/tmp1826.pdf>
- Analog Devices DS18B20 product page: <https://www.analog.com/en/products/ds18b20.html>
- Sensirion SHT45 product page: <https://sensirion.com/products/catalog/SHT45>
- Microchip MCP9808 product page: <https://www.microchip.com/en-us/product/mcp9808>
- Local nRF52840 specification copy: `hardware/MSV3_Probe_PCB/reference/datasheets/nRF52840_PS_v1.1.pdf`
