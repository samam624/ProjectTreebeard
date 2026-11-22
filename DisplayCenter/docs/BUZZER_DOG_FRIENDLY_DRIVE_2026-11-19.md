# Buzzer drive: making the alerts less harsh for a dog (2026-11-19)

**Question (maintainer):** can the buzzer sound less harsh so it does not frighten the dog?

## What drives it now

From `Treebeard_ESP32_Display_Runtime.ino` and `DISPLAY_CENTER_BUZZER_SOUND_DESIGN.md`:

- GPIO38 drives Q3 (2N7002, low side; R15 22 ohm gate, R16 100k pull-down). The PCB was laid
  out for the TDK PS1440P02BT piezo. Build 1 carries an unidentified two-pin "passive/resistive"
  buzzer instead. No flyback diode is listed on the board.
- The tone is a 0/1 square wave at **4 % duty**. Volume is set by narrowing the pulse, chosen by
  ear on 2026-08-14 because 8 % was louder and harsher.
- Water alert: 2400 Hz, a 48 ms then a 58 ms pulse, played three times 350 ms apart, then again
  every hour while a card is overdue. Startup: C6-E6-G6 (1047/1319/1568 Hz).
- Envelope: an **8 ms** linear duty ramp in and out, added to remove switching clicks.

## Computed: the 4 % duty puts the drive into the dog's band

These are the Fourier levels of the drive voltage, a 0/1 pulse train. They are NOT measured sound:
the buzzer's own response is unknown and unmeasured.

| | 4 % duty (now) | 50 % duty |
|---|---|---|
| fundamental (2.4 kHz), vs 50 % duty | -18 dB | 0 dB |
| harmonics 4.8 / 7.2 / 9.6 / 12 / 24 kHz, vs its own fundamental | 0 / 0 / 0 / -1 / -2 dB | none / -10 / none / -14 / none |
| share of AC drive power at 4-45 kHz | **80 %** | 17 % |

A narrow pulse has a flat spectrum up to about 1/duty x the tone (60 kHz here). So turning the
volume down with duty lowered the 2.4 kHz tone by 18 dB and left its 4.8-24 kHz harmonics nearly as
strong as the tone. That is the band dogs hear best: roughly 67 Hz-45 kHz overall, best around
4-16 kHz by most methods, and lowest thresholds at 20 kHz in one 2024 behavioural study. How much
of it the diaphragm actually radiates is unknown. A phone or laptop spectrum app (48 kHz sampling,
so it shows up to 24 kHz) held at the display would show it.

The 8 ms onset is in the startle range. The acoustic startle reflex is driven by onset, not
duration: in rats, a stimulus reaching its level within about 15 ms. In odontocetes, startle fell
as rise time went from 2 to 100 ms, and rise times of 141-220 ms were modelled to remove it.

## Strategies

Firmware only (no rework):

1. **Slow onset.** Ramp in over ~100-200 ms with a raised cosine, and let each note decay like a
   chime instead of stopping. This acts directly on the startle reflex.
2. **Lower, fewer, longer notes.** For example one descending two-note chime at ~700-1000 Hz,
   ~300 ms each, once per alert instead of three double-buzzes at 2400 Hz. This moves the
   fundamental below the dog's best band and drops the alarm-like repetition.
3. **Do not turn the volume down with duty.** Each step of duty below
   50 % trades fundamental for harmonics. With no other volume control, prefer 50 % duty and
   shorter or fewer notes.
4. **A high carrier, modulated as a sine.** Switch Q3 at >=100 kHz (above any dog's hearing) and
   vary the duty sinusoidally at the tone frequency. LEDC reaches ~312 kHz at 8 bits, and the
   ESP32-S3 SDM peripheral gives a pulse-density stream whose density a timer callback updates
   (ESP-IDF's `sdm_dac` example makes a 100 Hz sine this way). A coil's inductance smooths the
   current into close to a sine at an adjustable amplitude: a soft, adjustable tone with almost
   no harmonics. This NEEDS item 6 (a freewheel path) to work on a coil. It has not been tried.
5. **Quiet hours or visual-only alerts.** A mute toggle already exists. Quiet hours need a time
   of day, which the receiver does not have (no clock; the cloud pull could supply one).

Hardware:

6. **Flyback diode across the buzzer** (e.g. a 1N4148 or BAT54, cathode to +V). PUI Audio's
   reference drive circuit shows it. Without one, each turn-off of a coil buzzer kicks the drain,
   which rings and adds clicks and HF content. Cheap rework; required for item 4.
7. **Series resistor** (tens to ~150 ohm, by ear) to set the volume, so the drive can stay at 50 %
   duty (PUI: 50 % gives the most SPL; moving away from it changes the tone). Item 3 plus this is
   the simplest real fix.
8. **A small speaker and an I2S class-D amp** (e.g. MAX98357A): real sine chimes or samples at any
   level. A footprint change, so a Display Center #2 board decision, not a rework.

Before any of this, measure the part: its DC resistance tells a coil buzzer (tens of ohms) from
a piezo (open circuit), and that decides whether items 4 and 6 apply.

## Sources

- Dog hearing range and band: [LSU, How well do dogs and other animals hear?](https://www.lsu.edu/deafness/HearingRange.html);
  [Guerineau et al. 2024, Veterinary Sciences](https://mdpi.com/2306-7381/11/2/67).
- Startle and rise time: [Götz and Janik](https://scor-int.org/wp-content/uploads/2026/05/Gotz_and_Janik.pdf);
  [Psychonomic Society paper on rise time](https://link.springer.com/article/10.3758/BF03205275).
- Drive: [PUI Audio, Overview and Drive Circuits for Electromechanical Transducers](https://puiaudio.com/wp-content/uploads/2022/08/OverviewDriveCircuitsforElectromechanicalTransducers.pdf).
- Pulse-train spectrum: [Wikipedia, Pulse wave](https://en.wikipedia.org/wiki/Pulse_wave).
- ESP32-S3 SDM: [ESP-IDF Sigma-Delta Modulation](https://docs.espressif.com/projects/esp-idf/en/v5.3.4/esp32s3/api-reference/peripherals/sdm.html).

## Implemented 2026-11-19 (items 1-4, no rework): receiver build `soft-chime-v1`

- Q3 switches at a fixed 156.25 kHz carrier (LEDC, 9-bit). A 40 kHz timer interrupt
  (`buzzerSynthIsr`) sets the carrier duty to peak x envelope x (1 + sin)/2, using the LEDC
  duty's 4 fraction bits, so a note is a smooth sine at its own pitch.
- Envelope: 120 ms raised-cosine fade in, 220 ms out.
- Volume 1-100 % (`/sound.json`, default 35). Peak duty = 50 % x (volume/100)^2. The phone app's
  Setup > Sound Alerts has a slider (plays the chime on release) and a "Play test chime" button.
  `POST /sound {"test":true}` plays it even while muted.
- Water alert: one A5 -> E5 chime (420 + 620 ms), hourly while overdue (was a 2400 Hz double
  buzz x3). Startup: C5-E5-G5.
- Flashed COM16 2026-11-19; `/snapshot` reports the build; `/sound` read `enabled: false`,
  `volume: 35`, so the startup chime did not play at that boot.

NOT YET HEARD. Unknown until it is: how loud each volume is (the coil may follow the carrier
duty linearly or roughly as its square), whether 523-880 Hz carries through a buzzer probably
resonant in the kHz, and whether Q3 turning off a coil with no diode at 156 kHz is audible.

## Heard, and the builds after (2026-11-19, late)

- `soft-chime-v1/v2` were SILENT for a firmware reason: this core's LEDC runs from the 40 MHz
  crystal, so 156.25 kHz at 9 bits does not exist and `ledcAttach()` failed (`/sound` showed
  `ready: false`). `soft-chime-v3` uses 8 bits (40 MHz / 256).
- Maintainer, on v3: tone mode is heard and "this is better". The 156 kHz carrier ("smooth")
  is SILENT at every volume, as the coil physics predicted (no flyback diode), so it was
  removed from the app. It is worth retrying only with a diode fitted (item 6).
- `sound-choice-v1`: startup sounds Rising chime, Sunrise, Two-note hello, Sprout, Single bell,
  Off; water sounds Ding-dong, Droplets, Falling four, Low bell, Ding-dong twice. All are
  523-1047 Hz with the same envelope. Picked in the phone app (choosing one plays it) and stored
  by key in `/sound.json`. Flashed with the script's FQBN and build path but outside the script,
  because its MSV4 probe check was failing on an uncommitted probe edit in progress (Codex). Every
  receiver check passed, and the MSV4 check passed against the committed probe.
