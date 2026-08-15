# Display Center Buzzer Sound Design

This document records the sound choices bench-tested on the MSV2 Display
Center PCB on 2026-08-14. The standalone audition firmware is in:

```text
arduino/MSV2_ESP32_Buzzer_Audition/MSV2_ESP32_Buzzer_Audition.ino
```

## Installed Hardware

- Buzzer output: ESP32-S3 GPIO38 through PCB transistor Q3.
- The original TDK PS1440P02BT piezo did not work in the assembled unit.
- It was replaced with a simple two-pin passive/resistive buzzer from another
  project. Its exact part number, voltage rating, resonant frequency, impedance,
  and calibrated SPL are unknown.
- Do not use the original TDK part's 4 kHz/75 dB specifications to predict the
  replacement buzzer's behavior.
- Drive the replacement with LEDC PWM. Do not leave GPIO38 continuously high.

## Final Volume

Use **4% PWM duty** with 8-bit LEDC resolution. This was selected by listening
on the assembled Display Center and was quieter and cleaner than 8%.

The raw 8-bit duty is calculated rather than hard-coded:

```cpp
constexpr uint8_t BUZZER_DUTY_PERCENT = 4;
constexpr uint8_t BUZZER_PWM_RESOLUTION_BITS = 8;
constexpr uint16_t BUZZER_PWM_MAX =
    (1U << BUZZER_PWM_RESOLUTION_BITS) - 1U;

uint16_t buzzerDutyRaw() {
  return (BUZZER_PWM_MAX * BUZZER_DUTY_PERCENT + 50U) / 100U;
}
```

The percentage is electrical PWM duty, not calibrated SPL or perceived-volume
percentage. On this unipolar square-wave drive, 50% is the useful maximum tone
duty. Values above 50% add DC/heating without producing a stronger fundamental.

## Clean Tone Envelope

Each short tone uses an approximately **8 ms linear PWM-duty attack and 8 ms
release**. Ramping from zero to the 4% target and back suppresses the hard clicks
heard when PWM is switched instantly.

The standalone sketch's `playCleanTone()` is the tested reference
implementation. It performs no USB serial writes while a pattern is playing;
USB CDC logging previously blocked after upload and badly stretched musical
timing.

## Selected Startup Sound

The selected startup sound is audition candidate 1, a soft ascending major
triad. Play it once after normal runtime initialization succeeds.

| Step | Frequency | Tone | Silence after |
| --- | ---: | ---: | ---: |
| C6 | 1047 Hz | 55 ms | 22 ms |
| E6 | 1319 Hz | 60 ms | 22 ms |
| G6 | 1568 Hz | 115 ms | 0 ms |

All steps use the 4% maximum duty and the clean attack/release envelope.

## Selected Plant-Needs-Water Warning

The selected water warning is audition candidate 4, a minimal double buzz:

| Step | Frequency | Tone | Silence after |
| --- | ---: | ---: | ---: |
| Pulse 1 | 2400 Hz | 48 ms | 72 ms |
| Pulse 2 | 2400 Hz | 58 ms | 0 ms |

One water-alert event repeats that complete double buzz **three times**, with
**350 ms** between repetitions. During the standalone bench emulation, the
three-repeat event ran once every **60 seconds**.

The one-minute schedule was used to demonstrate and evaluate the alert. Before
shipping it in the normal display runtime, decide whether an acknowledged/snooze
state and quiet hours are needed so a persistently dry plant does not beep every
minute indefinitely.

## Runtime Integration Notes

- **Done (2026-08-14, commit `42a36b0`)**: the normal display runtime now plays both
  sounds. `MSV2_ESP32_Display_Runtime.ino` has a non-blocking LEDC tone sequencer
  (`startBuzzerPattern()`/`serviceBuzzer()`) driven off `millis()` deadlines only - no
  `delay()` - so it can't stall BLE/WiFi/touch/render the way the standalone bench
  sketch's blocking player would.
- Startup sound plays once at the end of `setup()`, on every real boot/reset
  (power-cycle, WiFi-retry-exhausted reboot, config-change reboot). The WiFi self-heal
  radio-reset path does not call `ESP.restart()` and does not replay it.
- The water sound is driven by `serviceWaterAlert()`, which reuses the exact same
  overdue/critical condition (`anyLowMoistureCardVisible()`/`slotNeedsBlink()`) that
  already drives the visual blink alert, so sound and blink can never disagree about
  which plants count. It fires once on the false-to-true transition, then again every
  `BUZZER_WATER_ALERT_REPEAT_MS` (currently 1 hour) while still overdue/critical, and
  goes silent again as soon as the plant is watered - the bench sketch's unthrottled
  60s loop was rejected as too naggy to ship.
- A mute toggle is persisted locally to `/sound.json` and exposed via `GET`/`POST
  /sound` (admin-gated like other config writes). It's user-reachable from both the
  touchscreen double-tap brightness overlay (new "Sound on/off" button) and the phone
  app's Setup tab ("Sound Alerts" card).

## Standalone Test Commands

With the audition firmware loaded and a 115200-baud serial terminal open:

| Command | Action |
| --- | --- |
| `b` | Play the selected startup sound |
| `w` | Play the complete three-repeat water warning |
| `1` through `6` | Play an individual audition candidate |
| `a` | Play all six audition candidates |
| `m` | Play the timing-corrected test melody |
| `d` | Replay the volume and frequency capability tours |
| `?` | Print all commands |

## Restoring the Normal Display Runtime

From the repository root, with the current display port substituted as needed:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\arduino_display.ps1 upload -Port COM10
```

The standalone sketch remains a bench tool and should not be mistaken for the
normal display firmware.
