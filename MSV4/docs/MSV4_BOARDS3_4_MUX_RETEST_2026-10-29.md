# MSV4 boards 3 and 4: mux retest after U3 resolder (2026-10-29)

Dated measurement record. Current physical state lives in `MSV3/fleet.json` under
`msv4_boards`.

The maintainer reported that the two probes had been resoldered and requested a mux retest. In
the immediate context these were board 3 (`MSV4-BU-5667`) and board 4 (`MSV4-BU-D7D2`), whose
2026-10-26 bring-up tests had both failed at U3. The exact resolder method and blade condition
were not stated.

Both boards were discovered over BLE. Each was tested with `drive 16`, `chop on`, and `iq 16`
after selecting `swap open`, `swap a`, and `swap b`. Each session ended with `safe`, which returned
`OK all outputs safe`.

| Board | Open SENSE | A SENSE | B SENSE | A vs B | Result |
|---|---:|---:|---:|---:|---|
| 3 / `5667` | 786.90 mV | 763.05 mV | 764.27 mV | 0.16% | PASS |
| 4 / `D7D2` | 779.46 mV | 756.45 mV | 755.55 mV | 0.12% | PASS |

On board 3, A and B are respectively 3.03% and 2.88% below open. On board 4, they are 2.95%
and 3.07% below open. The two electrode paths therefore agree well within the prior approximately
1% pass criterion, and both show the expected few-percent electrode loading in air.

Measured conclusion: both U3 mux repairs succeeded. Board 3's former A-open and B-to-ground
symptoms are gone, and board 4's former A-open symptom is gone.

Remaining work is unrelated to this mux pass: board 3 still needs R17 fitted and `temp` rerun;
board 4 needs R17 population checked and its prior `temp` failure rerun/diagnosed. Then run
`spectrum ab` and the MSV4 Lab hand test before assigning sensor IDs and installing production
firmware.
