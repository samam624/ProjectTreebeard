# MSV4 boards 3 and 4: full unattended bring-up (2026-10-29)

Dated measurement record. Current physical state lives in `MSV3/fleet.json` under
`msv4_boards`.

While the maintainer diagnosed board 5's 3V3 short, boards 3 (`MSV4-BU-5667`) and 4
(`MSV4-BU-D7D2`) received the full unattended BLE-console suite: `info`, initial `pins`, `vdd`,
`vbat`, `temp`, `cal`, 1 MHz drive/chopper setup, open/A/B `read` and `iq 16`, open `sweep 16`,
`spectrum open`, `spectrum ab`, `moisture`, then `safe` and final `pins`.

The blade condition and physical R17 population were not stated. A valid NTC-divider reading proves
the temperature electrical path works; it is not used to infer how the physical part was fitted.

| Check | Board 3 / 5667 | Board 4 / D7D2 | Result |
|---|---:|---:|---|
| LFCLK | XTAL, 360 ms start | XTAL, 395 ms start | PASS |
| VDD | 3317 mV | 3287 mV | PASS |
| VBAT | 4018 mV | 4038 mV | PASS, not meter-checked |
| Tip temperature | 22.66 C, 10939 ohm | 24.20 C, 10308 ohm | electrical path PASS |
| Die temperature | 23.00 C | 24.25 C | sane |
| Open/A/B SENSE at 1 MHz | 787.79 / 762.18 / 763.04 mV | 780.34 / 753.03 / 753.05 mV | mux PASS |
| Open Cp | 8.73 pF, rms 0.02067 | 8.79 pF, rms 0.02057 | PASS; boards agree 0.7% |
| A/B fit Cd | 14.03 pF | 14.96 pF | dry/open-like |
| A/B fit rms | 0.02311 | 0.02274 | acceptable for this air/open check |
| Final `safe` and pins | all safe | all safe | PASS |

Board 4's first connection attempt did not see it within eight seconds. A subsequent 15-second
scan found both boards, and a direct-address retry connected and completed the entire suite. This
is scanner timing, not a measured board failure.

Board 3's first `spectrum ab` fit landed on a nonsensical 2.3-ohm, zero-Cm local solution with
astronomical uncertainty. The immediately following `moisture` fit returned about 4e16 ohm, while
Cd remained 14.04 pF and rms remained about 0.023. This is the known dry/open model degeneracy,
not evidence of a physical 2.3-ohm electrode path. Board 4 returned 4.69e14 ohm directly.

Measured conclusion: both boards pass every unattended electrical bring-up check. Still requiring
the maintainer's physical interaction: MSV4 Lab live hand-on/hand-off response. After that, assign
sensor IDs and install signed production firmware.
