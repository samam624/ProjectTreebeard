# MSV4 Lab

MSV4 Lab is a local browser page for the `MSV4_Probe_Bringup_BLE` firmware. It
uses the existing Nordic UART BLE console to send commands, shows replies and
numeric trends, and records a portable troubleshooting session. The web server
listens only on `127.0.0.1`; it does not send readings to the cloud.

## Start it

From the repository root on either Windows computer:

```powershell
powershell -ExecutionPolicy Bypass -File MSV4\scripts\Start-MSV4-Lab.ps1
```

The script opens `http://127.0.0.1:8765/`. Keep the PowerShell window running
while using the page. Click **Scan**, select the nearby `MSV4-BU-XXXX` board,
and **Connect**. If only one board is present, Connect can auto-select it. You
can enter a board name or BLE address manually. A Windows Python 3.10+
installation needs `bleak`; the project's portable Python already has it. The page and saved
sessions can be reviewed without BLE or a connected board.

The board must be running the MSV4 bring-up image. No MSV4 hardware session has
yet validated this path. The current sketch has been compiled with the tracked
MSV4 variant; the older signed ZIP in `.arduino-build` predates later source
edits and should be rebuilt before first flashing. Follow
[`MSV4_BLE_BRINGUP_FIRMWARE_2026-09-18.md`](MSV4_BLE_BRINGUP_FIRMWARE_2026-09-18.md)
for initial power, SWD, and signed BLE installation.

## Use it for a test

- The command buttons and command box send the firmware's exact commands.
  `read` returns `SENSE_OUT` and `DRV_OUT` in mV; `temp`, `vbat`, and `vdd`
  report their ADC readings. `iq 16` reports four-phase I/Q at 1 MHz.
  `spectrum open` measures the floating-electrode reference; `spectrum ab`
  sweeps both orientations and fits soil resistance and interface capacitance.
  `moisture` is the one-line logging form of the same sweep: temperature-corrected
  `Rsoil` plus the contact-quality channel.
- **Repeated readings** run up to three selected measurement commands in
  sequence on a timer. Capture runs in the local server, so it continues if
  the browser tab closes. It stops on disconnection or when you click Stop.
  Commands and replies appear in the transcript and are written to the active
  session as they arrive.
- The cards show the most recent replies. The trend chart shows one numeric
  field from the session. Add a note to mark a physical change, scope result,
  watering event, or electrode position. A spectrum fit's `Rsoil` is the
  moisture proxy; plot `Rsoil25` from `moisture` for a trend. Watch `Cm`
  alongside it — `Rsoil` rising with `Cm` steady means the pot dried out,
  `Rsoil` rising with `Cm` collapsing means the blade lost soil contact, and
  those need opposite responses. `Cs` was removed from the fit on 2026-09-23;
  see [`MSV4_SPECTRUM_FIT_CONDUCTANCE_ONLY_2026-09-23.md`](MSV4_SPECTRUM_FIT_CONDUCTANCE_ONLY_2026-09-23.md).
- `safe` releases the front-end outputs; `stop` cancels a long sweep. A BLE
  disconnect also sends the firmware to its safe state.

## Take a session to the other computer

Sessions save automatically under `MSV4/.lab-sessions/` by default. This folder
is ignored by Git. Click **Export JSON**, move the file to the other computer,
then click **Import JSON** there. The imported session can be opened, read, and
continued after connecting that computer to a nearby board. Export includes
timestamps, commands, all reply lines, parsed numeric readings, and notes.

For automatic file synchronization, start both installations with the same
user-managed sync folder path, for example:

```powershell
powershell -ExecutionPolicy Bypass -File MSV4\scripts\Start-MSV4-Lab.ps1 -DataDir 'C:\Users\YourName\OneDrive\MSV4 Lab Sessions'
```

Use one computer at a time for a given session and let the folder finish
syncing before switching machines. The Export/Import route avoids sync
conflicts and works offline.

## Boundaries

The UI talks to the local Python server, which starts
[`msv4_ble_console.py`](../scripts/msv4_ble_console.py) as the BLE transport.
It does not make a browser Web Bluetooth connection, so the same page works
with the project's Windows BLE handling. It is a bring-up tool, not production
moisture firmware or a calibrated watering display. Live BLE behavior remains
to be proven on the first assembled MSV4 board.
