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

- The normal display runtime has not yet been modified to play these sounds.
- Keep sound scheduling non-blocking in the display runtime; the standalone test
  uses `delay()` because it does not service BLE, Wi-Fi, HTTP, touch, or drawing.
- Trigger the startup sound only when boot/setup reaches the desired ready state,
  not on every internal recovery reset unless that behavior is intentional.
- Trigger the water sound on a dry-state transition or through an explicit alert
  scheduler. Avoid replaying it on every sensor update.
- Pair the sound with the existing visual dry-plant indication; sound alone must
  not be the only status signal.
- Preserve a user-facing mute/disable option when the sound is integrated.

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
