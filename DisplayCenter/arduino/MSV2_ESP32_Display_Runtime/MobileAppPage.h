#pragma once
#include <Arduino.h>

const char MSV2_MOBILE_APP_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <meta name="theme-color" content="#0e141b">
  <meta name="mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-title" content="MSV2 Plants">
  <link rel="manifest" href="/manifest.webmanifest">
  <link rel="icon" href="/app-icon.svg" type="image/svg+xml">
  <link rel="apple-touch-icon" href="/app-icon.svg">
  <title>MSV2 Plants</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #0e141b;
      --panel: #17212b;
      --panel-2: #1d2a35;
      --text: #f5f7fa;
      --muted: #8fa3ad;
      --accent: #20c997;
      --warn: #ffb020;
      --danger: #ff5a5f;
      --line: rgba(255,255,255,.11);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      background: var(--bg);
      color: var(--text);
      font: 14px/1.35 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    header {
      position: sticky;
      top: 0;
      z-index: 3;
      background: rgba(14,20,27,.94);
      border-bottom: 1px solid var(--line);
      padding: 14px 14px 10px;
      backdrop-filter: blur(12px);
    }
    h1 {
      margin: 0;
      font-size: 21px;
      letter-spacing: 0;
    }
    .subhead {
      display: flex;
      gap: 8px;
      align-items: center;
      justify-content: space-between;
      color: var(--muted);
      margin-top: 4px;
      font-size: 12px;
    }
    main { padding: 14px; max-width: 880px; margin: 0 auto; }
    .tabs {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 8px;
      margin: 0 0 14px;
    }
    button, select, input {
      font: inherit;
      color: var(--text);
      background: var(--panel-2);
      border: 1px solid var(--line);
      border-radius: 8px;
    }
    button {
      min-height: 40px;
      padding: 8px 12px;
      font-weight: 650;
    }
    button.primary { background: var(--accent); color: #06231a; border-color: transparent; }
    button.ghost { background: transparent; color: var(--muted); }
    button.active { border-color: var(--accent); color: var(--accent); }
    .grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 10px;
    }
    .card {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 10px;
      min-width: 0;
    }
    .card-head {
      display: flex;
      gap: 8px;
      align-items: flex-start;
      justify-content: space-between;
      margin-bottom: 9px;
    }
    .name {
      font-size: 15px;
      font-weight: 750;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .meta { color: var(--muted); font-size: 12px; margin-top: 1px; }
    .art {
      flex: 0 0 auto;
      width: 58px;
      text-align: right;
      color: var(--accent);
      font-size: 12px;
      font-weight: 700;
    }
    .art img {
      display: block;
      width: 48px;
      height: 48px;
      object-fit: contain;
      image-rendering: pixelated;
      margin-left: auto;
    }
    .art span {
      display: block;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      margin-top: 2px;
    }
    .metric { margin-top: 8px; }
    .metric-row {
      display: flex;
      justify-content: space-between;
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 4px;
    }
    .bar {
      position: relative;
      height: 9px;
      border-radius: 999px;
      background: #0a1015;
      overflow: hidden;
      border: 1px solid rgba(255,255,255,.08);
    }
    .fill {
      height: 100%;
      width: 0%;
      background: var(--accent);
      border-radius: inherit;
    }
    .fill.warn { background: var(--warn); }
    /* Matches the physical display's applyLowMoistureBlink() cadence exactly (500ms/500ms,
       full color <-> muted) so the "actually overdue, not just low" alert reads the same way
       on every surface. */
    .fill.danger { background: var(--danger); animation: moisture-flash 1s step-end infinite; }
    .fill.muted { background: var(--muted); }
    @keyframes moisture-flash {
      0%, 49% { background: var(--danger); }
      50%, 100% { background: var(--muted); }
    }
    /* Watering-threshold marker: a plant is "watered" toward the wet (100%/right) end of the
       bar, not at any fixed percent - the dotted line is the actual alert boundary. Below it,
       the fill itself renders danger-red (see moistureFillClass) - watering is overdue, not
       just "getting low". overflow:hidden on .bar clips this to stay inside the pill shape. */
    .water-line {
      position: absolute;
      top: -1px;
      bottom: -1px;
      width: 0;
      border-left: 2px dotted rgba(255,255,255,.55);
      z-index: 1;
    }
    form {
      display: grid;
      gap: 12px;
    }
    .editor-card {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
    }
    .editor-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
      margin-bottom: 10px;
    }
    .editor-title { font-weight: 800; }
    .fields {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 9px;
    }
    label {
      display: grid;
      gap: 5px;
      color: var(--muted);
      font-size: 12px;
    }
    label.wide { grid-column: 1 / -1; }
    .fields .reset-row { grid-column: 1 / -1; display: flex; align-items: center; gap: 10px; }
    .fields .reset-row .editor-reset-status { color: var(--muted); font-size: 12px; }
    input, select {
      width: 100%;
      min-height: 38px;
      padding: 8px 9px;
    }
    .editor-art-preview {
      width: 54px;
      height: 54px;
      object-fit: contain;
      image-rendering: pixelated;
      justify-self: start;
      background: #0a1015;
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 3px;
    }
    .actions {
      position: sticky;
      bottom: 0;
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
      padding: 10px 0 env(safe-area-inset-bottom);
      background: rgba(14,20,27,.94);
      backdrop-filter: blur(12px);
    }
    .actions .primary { grid-column: 1 / -1; }
    .notice {
      min-height: 18px;
      color: var(--muted);
      font-size: 12px;
      margin: 8px 0 0;
    }
    .notice.warn { color: var(--warn); }
    .network-card {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
      margin-bottom: 12px;
    }
    .network-row {
      display: grid;
      grid-template-columns: 1fr;
      gap: 9px;
    }
    .check-row {
      display: flex;
      align-items: flex-start;
      gap: 9px;
      margin: 10px 0 2px;
      color: var(--text);
      font-weight: 650;
    }
    .check-row input {
      width: 18px;
      height: 18px;
      margin: 1px 0 0;
      flex: 0 0 auto;
      accent-color: var(--accent);
    }
    .check-row span {
      display: block;
      color: var(--muted);
      font-size: 12px;
      font-weight: 500;
      margin-top: 2px;
    }
    .network-actions {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
      margin-top: 10px;
    }
    .claim-qr {
      display: grid;
      justify-items: center;
      gap: 6px;
      margin-top: 12px;
      padding: 12px;
      background: #0a1015;
      border: 1px solid var(--line);
      border-radius: 8px;
    }
    .claim-qr img {
      width: min(220px, 100%);
      aspect-ratio: 1;
      background: #fff;
      border-radius: 6px;
      padding: 8px;
    }
    .claim-qr .meta {
      max-width: 100%;
      overflow-wrap: anywhere;
      text-align: center;
      font-size: 11px;
    }
    .diag-list {
      display: grid;
      gap: 10px;
    }
    .diag-card {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
    }
    .diag-head {
      display: flex;
      justify-content: space-between;
      gap: 10px;
      align-items: baseline;
      margin-bottom: 10px;
    }
    .diag-title {
      font-size: 15px;
      font-weight: 800;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .pill {
      flex: 0 0 auto;
      border: 1px solid var(--line);
      border-radius: 999px;
      padding: 3px 8px;
      color: var(--muted);
      font-size: 12px;
      font-weight: 700;
    }
    .pill.good { color: var(--accent); border-color: rgba(32,201,151,.45); }
    .pill.warn { color: var(--warn); border-color: rgba(255,176,32,.45); }
    .pill.bad { color: var(--danger); border-color: rgba(255,90,95,.45); }
    .diag-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px;
    }
    .diag-cell {
      background: #0a1015;
      border: 1px solid rgba(255,255,255,.07);
      border-radius: 8px;
      padding: 8px;
      min-width: 0;
    }
    .diag-label {
      color: var(--muted);
      font-size: 11px;
      margin-bottom: 2px;
    }
    .diag-value {
      font-size: 15px;
      font-weight: 800;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .chart-card { margin-bottom: 12px; }
    .chart-head { display: flex; align-items: baseline; justify-content: space-between; margin-bottom: 8px; }
    .chart-title { font-size: 13px; font-weight: 800; }
    .chart-latest { font-size: 12px; color: var(--muted); }
    .chart-wrap { position: relative; }
    svg.chart { width: 100%; height: auto; display: block; overflow: visible; }
    .chart-grid { stroke: var(--line); stroke-width: 1; }
    .chart-axis { fill: var(--muted); font-size: 10px; }
    .chart-crosshair { stroke: var(--muted); stroke-width: 1; opacity: 0; pointer-events: none; }
    .chart-tooltip {
      position: absolute;
      pointer-events: none;
      background: #000;
      color: #fff;
      font-size: 11px;
      padding: 5px 8px;
      border-radius: 6px;
      opacity: 0;
      transform: translate(-50%, -110%);
      white-space: nowrap;
      z-index: 5;
    }
    .chart-tooltip .tt-value { font-weight: 700; }
    .chart-tooltip .tt-time { color: #aaa; font-size: 10px; }
    .smoothing-panel { margin-top: 10px; border-top: 1px solid var(--line); padding-top: 10px; }
    .smoothing-panel summary { cursor: pointer; font-weight: 700; font-size: 13px; list-style: none; }
    .smoothing-panel summary::-webkit-details-marker { display: none; }
    .smoothing-panel summary::before { content: "\25b8  "; color: var(--muted); }
    .smoothing-panel[open] summary::before { content: "\25be  "; }
    .smoothing-sub { font-size: 11px; font-weight: 600; color: var(--muted); text-transform: uppercase; letter-spacing: .04em; margin: 12px 0 6px; }
    .smoothing-sub:first-of-type { margin-top: 10px; }
    .smoothing-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 9px; }
    .smoothing-grid label { font-size: 11px; }
    .smoothing-grid input { min-height: 34px; padding: 6px 8px; }
    .smoothing-actions { display: flex; gap: 8px; margin-top: 10px; }
    .smoothing-actions button { flex: 1; }
    .hidden { display: none; }
    @media (min-width: 700px) {
      main { max-width: 960px; }
      .grid { grid-template-columns: repeat(4, minmax(0, 1fr)); }
      .diag-list { grid-template-columns: repeat(2, minmax(0, 1fr)); }
      .network-row { grid-template-columns: 1fr 1fr; }
      .smoothing-grid { grid-template-columns: repeat(4, minmax(0, 1fr)); }
      .fields { grid-template-columns: 1.3fr .7fr 1fr 1fr; }
      label.wide { grid-column: auto; }
      .actions { grid-template-columns: .8fr .8fr 1.2fr; }
      .actions .primary { grid-column: auto; }
    }
  </style>
</head>
<body>
  <header>
    <h1 id="appTitle">MSV2 Plants</h1>
    <div class="subhead">
      <span id="statusText">Loading...</span>
      <span id="saveState"></span>
    </div>
  </header>
  <main>
    <div class="tabs">
      <button id="statusTab" class="active" type="button">Status</button>
      <button id="setupTab" type="button">Setup</button>
      <button id="diagnosticsTab" type="button">Diagnostics</button>
      <button id="historyTab" type="button">History</button>
    </div>

    <section id="statusView">
      <div id="statusGrid" class="grid"></div>
    </section>

    <section id="setupView" class="hidden">
      <section class="network-card">
        <div class="editor-head">
          <div>
            <div class="editor-title">Receiver WiFi</div>
            <div class="meta" id="wifiStatusText">Loading network...</div>
          </div>
          <button type="button" class="ghost" id="wifiRefreshButton">Refresh</button>
        </div>
        <div class="notice" id="wifiHelpText">Choose home WiFi below, or use local-only mode to keep editing from MSV2-Display.</div>
        <div class="network-row">
          <label>Network name
            <input id="wifiSsidInput" maxlength="63" autocomplete="off">
          </label>
          <label>Password
            <input id="wifiPasswordInput" type="password" maxlength="95" autocomplete="new-password">
          </label>
        </div>
        <label class="check-row">
          <input id="wifiLocalOnlyInput" type="checkbox">
          <div>
            Use without home WiFi
            <span>The receiver will keep broadcasting MSV2-Display and show the plant dashboard locally.</span>
          </div>
        </label>
        <div class="network-actions">
          <button type="button" class="ghost" id="wifiForgetButton">Forget WiFi</button>
          <button type="button" class="primary" id="wifiSaveButton">Save & Reboot</button>
        </div>
        <div class="notice" id="wifiNotice"></div>
      </section>
      <section class="network-card">
        <div class="editor-head">
          <div>
            <div class="editor-title">Sound Alerts</div>
            <div class="meta">Startup chime and water-warning buzzer</div>
          </div>
        </div>
        <label class="check-row">
          <input id="soundEnabledInput" type="checkbox">
          <div>
            Play alert sounds
            <span>Startup chime, plus a short double-buzz that repeats hourly while a plant stays critically low on water.</span>
          </div>
        </label>
        <div class="notice" id="soundNotice"></div>
      </section>
      <section class="network-card">
        <div class="editor-head">
          <div>
            <div class="editor-title">Receiver Pairing</div>
            <div class="meta" id="claimStatusText">Loading receiver...</div>
          </div>
          <button type="button" class="ghost" id="claimRefreshButton">Refresh</button>
        </div>
        <div class="diag-grid">
          <div class="diag-cell">
            <div class="diag-label">Receiver ID</div>
            <div class="diag-value" id="receiverIdText">--</div>
          </div>
          <div class="diag-cell">
            <div class="diag-label">Claim Code</div>
            <div class="diag-value" id="claimCodeText">--</div>
          </div>
        </div>
        <div class="network-actions">
          <button type="button" class="ghost" id="copyReceiverIdButton">Copy ID</button>
          <button type="button" class="primary" id="copyClaimCodeButton">Copy Code</button>
        </div>
        <div class="network-actions">
          <button type="button" class="ghost" id="copyClaimUrlButton">Copy App Link</button>
          <button type="button" class="primary" id="openClaimUrlButton">Open App</button>
        </div>
        <div class="claim-qr hidden" id="claimQrBox">
          <img id="claimQrImage" alt="Receiver pairing QR code">
          <div class="meta" id="claimQrText"></div>
        </div>
        <div class="notice" id="claimNotice">Use the app link for one-scan household setup. The phone stores the household link after pairing.</div>
      </section>
      <section class="network-card">
        <div class="editor-head">
          <div>
            <div class="editor-title">Cloud Status</div>
            <div class="meta" id="cloudStatusText">Loading cloud status...</div>
          </div>
          <button type="button" class="ghost" id="cloudRefreshButton">Refresh</button>
        </div>
        <div class="notice" id="cloudNotice">Cloud setup now happens automatically after WiFi connects.</div>
      </section>
      <form id="configForm">
        <label class="wide">
          Dashboard title
          <input id="titleInput" name="title" maxlength="32">
        </label>
        <div class="notice" id="configVersionText">Config version --</div>
        <div id="editorList"></div>
        <div class="notice">Edits only save within about 15 minutes of the receiver's last reboot or edit (a security limit, not a bug). If "Save to Display" fails, power-cycle the receiver and save again - each successful save extends the window another 15 minutes.</div>
        <div class="actions">
          <button class="ghost" id="reloadButton" type="button">Reload</button>
          <button class="ghost" id="addPlantButton" type="button">Add Plant</button>
          <button class="primary" type="submit">Save to Display</button>
        </div>
      </form>
      <div class="notice" id="setupNotice"></div>
    </section>

    <section id="diagnosticsView" class="hidden">
      <div id="diagnosticsList" class="diag-list"></div>
      <div class="notice">RSSI below -90 dBm is edge-of-range. Missed packets are inferred from probe wake-counter gaps.</div>
    </section>

    <section id="historyView" class="hidden">
      <section class="network-card">
        <div class="network-row">
          <label>Probe
            <select id="historySensorSelect"></select>
          </label>
          <label>Range
            <select id="historyRangeSelect">
              <option value="6">Last 6 hours</option>
              <option value="24" selected>Last 24 hours</option>
              <option value="72">Last 3 days</option>
              <option value="168">Last 7 days</option>
              <option value="336">Last 14 days</option>
              <option value="720">Last 30 days</option>
              <option value="87600">All time</option>
              <option value="custom">Custom range...</option>
            </select>
          </label>
          <button type="button" class="ghost" id="historyClearButton">Clear All History</button>
        </div>
        <!-- Extra alongside the presets above, not a replacement - lets you pin an exact
             historical window (e.g. 2026-10-29 through 2026-10-31) instead of only "last N
             hours/days through right now". Hidden unless "Custom range..." is selected. -->
        <div class="network-row hidden" id="historyCustomRangeRow">
          <label>From
            <input type="date" id="historyCustomStart">
          </label>
          <label>To
            <input type="date" id="historyCustomEnd">
          </label>
          <button type="button" class="ghost" id="historyCustomApply">Apply</button>
        </div>
        <div class="notice" id="historyNotice">Pulled from Supabase - requires cloud sync enabled on this receiver.</div>

        <details class="smoothing-panel" id="smoothingPanel">
          <summary>Smoothing (recomputed from raw, on the fly)</summary>
          <div class="notice">Both charts are recomputed from the loaded raw signal in your browser; nothing is re-flashed or refetched. Defaults mirror the current firmware math and use this sensor's current learned calibration span when available. This is a window-local simulation: filter state before the selected range and receiver reboot boundaries are not present in history, so it does not claim to reconstruct the originally displayed line exactly.</div>

          <div class="smoothing-sub">Moisture</div>
          <div class="smoothing-grid">
            <label>Robust center window (odd packets)
              <input type="number" id="smCenterWindowPackets" min="3" max="31" step="2">
            </label>
            <label>Center tracking (EMA alpha)
              <input type="number" id="smMoistureAlpha" min="0.01" max="1" step="0.025">
            </label>
            <label>Watering trigger (x noise)
              <input type="number" id="smNoiseWateringMult" min="0" step="0.5">
            </label>
            <label>Watering trigger (% of span)
              <input type="number" id="smWateringSpanPct" min="0" step="1">
            </label>
            <label>Max tracking step (% of span)
              <input type="number" id="smMaxStepSpanPct" min="0.1" step="0.5">
            </label>
            <label>Watering confirm (packets)
              <input type="number" id="smConfirmPackets" min="1" step="1">
            </label>
          </div>

          <div class="smoothing-sub">Battery</div>
          <div class="smoothing-grid">
            <label>Smoothing (EMA alpha)
              <input type="number" id="smBatteryAlpha" min="0.01" max="1" step="0.01">
            </label>
          </div>

          <div class="smoothing-actions">
            <button type="button" class="ghost" id="smoothingResetButton">Reset to firmware defaults</button>
          </div>
        </details>
      </section>

      <section class="network-card chart-card">
        <div class="chart-head">
          <div class="chart-title">Moisture (oscillator, higher = wetter)</div>
          <div class="chart-latest" id="historyMoistureLatest"></div>
        </div>
        <div class="chart-wrap">
          <svg class="chart" id="historyMoistureChart"></svg>
          <div class="chart-tooltip" id="historyMoistureTooltip"><div class="tt-value"></div><div class="tt-time"></div></div>
        </div>
      </section>

      <section class="network-card chart-card">
        <div class="chart-head">
          <div class="chart-title">Battery</div>
          <div class="chart-latest" id="historyBatteryLatest"></div>
        </div>
        <div class="chart-wrap">
          <svg class="chart" id="historyBatteryChart"></svg>
          <div class="chart-tooltip" id="historyBatteryTooltip"><div class="tt-value"></div><div class="tt-time"></div></div>
        </div>
      </section>

      <!-- MSV3-only: board_temp_quarter_c (nRF52840 on-die temperature, quarter-degree C)
           is nullable and absent from every MSV2 probe and from MSV3 probes still on
           pre-diagnostics firmware, so this card starts hidden and only appears once
           renderHistoryFromCache() finds at least one non-null reading for the selected
           probe. Raw/uncompensated - see MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md. -->
      <section class="network-card chart-card hidden" id="historyTempSection">
        <div class="chart-head">
          <div class="chart-title">Board (die) temperature</div>
          <div class="chart-latest" id="historyTempLatest"></div>
        </div>
        <div class="chart-wrap">
          <svg class="chart" id="historyTempChart"></svg>
          <div class="chart-tooltip" id="historyTempTooltip"><div class="tt-value"></div><div class="tt-time"></div></div>
        </div>
      </section>

      <!-- MSV3-only, same nullable/hidden-until-seen convention as historyTempSection above.
           raw_fdc_counts (moisture-connected, present since migration 029/037) - see
           MSV3_MULTI_DAY_FDC_DRIFT_FOLLOWUP_2026-08-28.md. Own auto-scaled axis rather than
           overlaying on the bare-tank chart below: the two run ~4M counts apart (electrodes
           in parallel vs. disconnected), so a shared axis would flatten both lines' actual
           (much smaller) day-to-day drift into a nearly invisible sliver. Compare this
           chart's shape against the bare-tank and temperature charts by eye instead. -->
      <section class="network-card chart-card hidden" id="historyRawFdcSection">
        <div class="chart-head">
          <div class="chart-title">Raw FDC counts (moisture-connected)</div>
          <div class="chart-latest" id="historyRawFdcLatest"></div>
        </div>
        <div class="chart-wrap">
          <svg class="chart" id="historyRawFdcChart"></svg>
          <div class="chart-tooltip" id="historyRawFdcTooltip"><div class="tt-value"></div><div class="tt-time"></div></div>
        </div>
      </section>

      <!-- MSV3-only. bare_tank_fdc_counts (EC-check mux codes 2/3, electrodes disconnected,
           oscillating on L2/C17 alone) - present since migration 035, only taken on
           advertising wakes so this is naturally sparser than the raw-FDC chart above.
           See MSV3_SOFTWARE_TEMPERATURE_TANK_REFERENCE_2026-08-25.md for the reserved
           Cconnected/Cbare = (Dbare/Dconnected)^2 compensation formula this chart exists to
           let you eyeball before trusting. -->
      <section class="network-card chart-card hidden" id="historyBareTankSection">
        <div class="chart-head">
          <div class="chart-title">Bare-tank FDC reference (disconnected mux)</div>
          <div class="chart-latest" id="historyBareTankLatest"></div>
        </div>
        <div class="chart-wrap">
          <svg class="chart" id="historyBareTankChart"></svg>
          <div class="chart-tooltip" id="historyBareTankTooltip"><div class="tt-value"></div><div class="tt-time"></div></div>
        </div>
      </section>

      <!-- MSV3-only experimental derived observable. This is calculated from the connected
           and bare counts carried by the SAME history row; never construct it by dividing
           independently averaged/interpolated chart series. The squared ratio follows from
           the FDC resonant-tank transfer function and cancels multiplicative reference-clock
           and inductor drift to first order. It is not volumetric water content and does not
           remove soil's own temperature/conductivity response, so the production moisture,
           alerts, smoothing and calibration remain deliberately unchanged. -->
      <section class="network-card chart-card hidden" id="historyFdcReferenceSection">
        <div class="chart-head">
          <div class="chart-title">Bare-reference normalized FDC (experimental, higher = wetter)</div>
          <div class="chart-latest" id="historyFdcReferenceLatest"></div>
        </div>
        <div class="chart-wrap">
          <svg class="chart" id="historyFdcReferenceChart"></svg>
          <div class="chart-tooltip" id="historyFdcReferenceTooltip"><div class="tt-value"></div><div class="tt-time"></div></div>
        </div>
      </section>
    </section>
  </main>

  <script>
/**
 * qrcode-generator (kazuhikoarase/qrcode-generator), MIT License.
 * Copyright (c) 2009 Kazuhiko Arase. http://www.d-project.com/
 * https://github.com/kazuhikoarase/qrcode-generator
 * Vendored (minified, v1.4.4) so the receiver's local pairing QR code renders
 * fully client-side - no network call, works even in local-only AP mode where
 * the phone has no route to the internet, and never sends the pairing
 * receiverId/claimCode to a third party.
 */
var qrcode=function(){var t=function(t,r){var e=t,n=g[r],o=null,i=0,a=null,u=[],f={},c=function(t,r){o=function(t){for(var r=new Array(t),e=0;e<t;e+=1){r[e]=new Array(t);for(var n=0;n<t;n+=1)r[e][n]=null}return r}(i=4*e+17),l(0,0),l(i-7,0),l(0,i-7),s(),h(),d(t,r),e>=7&&v(t),null==a&&(a=p(e,n,u)),w(a,r)},l=function(t,r){for(var e=-1;e<=7;e+=1)if(!(t+e<=-1||i<=t+e))for(var n=-1;n<=7;n+=1)r+n<=-1||i<=r+n||(o[t+e][r+n]=0<=e&&e<=6&&(0==n||6==n)||0<=n&&n<=6&&(0==e||6==e)||2<=e&&e<=4&&2<=n&&n<=4)},h=function(){for(var t=8;t<i-8;t+=1)null==o[t][6]&&(o[t][6]=t%2==0);for(var r=8;r<i-8;r+=1)null==o[6][r]&&(o[6][r]=r%2==0)},s=function(){for(var t=B.getPatternPosition(e),r=0;r<t.length;r+=1)for(var n=0;n<t.length;n+=1){var i=t[r],a=t[n];if(null==o[i][a])for(var u=-2;u<=2;u+=1)for(var f=-2;f<=2;f+=1)o[i+u][a+f]=-2==u||2==u||-2==f||2==f||0==u&&0==f}},v=function(t){for(var r=B.getBCHTypeNumber(e),n=0;n<18;n+=1){var a=!t&&1==(r>>n&1);o[Math.floor(n/3)][n%3+i-8-3]=a}for(n=0;n<18;n+=1){a=!t&&1==(r>>n&1);o[n%3+i-8-3][Math.floor(n/3)]=a}},d=function(t,r){for(var e=n<<3|r,a=B.getBCHTypeInfo(e),u=0;u<15;u+=1){var f=!t&&1==(a>>u&1);u<6?o[u][8]=f:u<8?o[u+1][8]=f:o[i-15+u][8]=f}for(u=0;u<15;u+=1){f=!t&&1==(a>>u&1);u<8?o[8][i-u-1]=f:u<9?o[8][15-u-1+1]=f:o[8][15-u-1]=f}o[i-8][8]=!t},w=function(t,r){for(var e=-1,n=i-1,a=7,u=0,f=B.getMaskFunction(r),c=i-1;c>0;c-=2)for(6==c&&(c-=1);;){for(var g=0;g<2;g+=1)if(null==o[n][c-g]){var l=!1;u<t.length&&(l=1==(t[u]>>>a&1)),f(n,c-g)&&(l=!l),o[n][c-g]=l,-1==(a-=1)&&(u+=1,a=7)}if((n+=e)<0||i<=n){n-=e,e=-e;break}}},p=function(t,r,e){for(var n=A.getRSBlocks(t,r),o=b(),i=0;i<e.length;i+=1){var a=e[i];o.put(a.getMode(),4),o.put(a.getLength(),B.getLengthInBits(a.getMode(),t)),a.write(o)}var u=0;for(i=0;i<n.length;i+=1)u+=n[i].dataCount;if(o.getLengthInBits()>8*u)throw"code length overflow. ("+o.getLengthInBits()+">"+8*u+")";for(o.getLengthInBits()+4<=8*u&&o.put(0,4);o.getLengthInBits()%8!=0;)o.putBit(!1);for(;!(o.getLengthInBits()>=8*u||(o.put(236,8),o.getLengthInBits()>=8*u));)o.put(17,8);return function(t,r){for(var e=0,n=0,o=0,i=new Array(r.length),a=new Array(r.length),u=0;u<r.length;u+=1){var f=r[u].dataCount,c=r[u].totalCount-f;n=Math.max(n,f),o=Math.max(o,c),i[u]=new Array(f);for(var g=0;g<i[u].length;g+=1)i[u][g]=255&t.getBuffer()[g+e];e+=f;var l=B.getErrorCorrectPolynomial(c),h=k(i[u],l.getLength()-1).mod(l);for(a[u]=new Array(l.getLength()-1),g=0;g<a[u].length;g+=1){var s=g+h.getLength()-a[u].length;a[u][g]=s>=0?h.getAt(s):0}}var v=0;for(g=0;g<r.length;g+=1)v+=r[g].totalCount;var d=new Array(v),w=0;for(g=0;g<n;g+=1)for(u=0;u<r.length;u+=1)g<i[u].length&&(d[w]=i[u][g],w+=1);for(g=0;g<o;g+=1)for(u=0;u<r.length;u+=1)g<a[u].length&&(d[w]=a[u][g],w+=1);return d}(o,n)};f.addData=function(t,r){var e=null;switch(r=r||"Byte"){case"Numeric":e=M(t);break;case"Alphanumeric":e=x(t);break;case"Byte":e=m(t);break;case"Kanji":e=L(t);break;default:throw"mode:"+r}u.push(e),a=null},f.isDark=function(t,r){if(t<0||i<=t||r<0||i<=r)throw t+","+r;return o[t][r]},f.getModuleCount=function(){return i},f.make=function(){if(e<1){for(var t=1;t<40;t++){for(var r=A.getRSBlocks(t,n),o=b(),i=0;i<u.length;i++){var a=u[i];o.put(a.getMode(),4),o.put(a.getLength(),B.getLengthInBits(a.getMode(),t)),a.write(o)}var g=0;for(i=0;i<r.length;i++)g+=r[i].dataCount;if(o.getLengthInBits()<=8*g)break}e=t}c(!1,function(){for(var t=0,r=0,e=0;e<8;e+=1){c(!0,e);var n=B.getLostPoint(f);(0==e||t>n)&&(t=n,r=e)}return r}())},f.createTableTag=function(t,r){t=t||2;var e="";e+='<table style="',e+=" border-width: 0px; border-style: none;",e+=" border-collapse: collapse;",e+=" padding: 0px; margin: "+(r=void 0===r?4*t:r)+"px;",e+='">',e+="<tbody>";for(var n=0;n<f.getModuleCount();n+=1){e+="<tr>";for(var o=0;o<f.getModuleCount();o+=1)e+='<td style="',e+=" border-width: 0px; border-style: none;",e+=" border-collapse: collapse;",e+=" padding: 0px; margin: 0px;",e+=" width: "+t+"px;",e+=" height: "+t+"px;",e+=" background-color: ",e+=f.isDark(n,o)?"#000000":"#ffffff",e+=";",e+='"/>';e+="</tr>"}return e+="</tbody>",e+="</table>"},f.createSvgTag=function(t,r,e,n){var o={};"object"==typeof arguments[0]&&(t=(o=arguments[0]).cellSize,r=o.margin,e=o.alt,n=o.title),t=t||2,r=void 0===r?4*t:r,(e="string"==typeof e?{text:e}:e||{}).text=e.text||null,e.id=e.text?e.id||"qrcode-description":null,(n="string"==typeof n?{text:n}:n||{}).text=n.text||null,n.id=n.text?n.id||"qrcode-title":null;var i,a,u,c,g=f.getModuleCount()*t+2*r,l="";for(c="l"+t+",0 0,"+t+" -"+t+",0 0,-"+t+"z ",l+='<svg version="1.1" xmlns="http://www.w3.org/2000/svg"',l+=o.scalable?"":' width="'+g+'px" height="'+g+'px"',l+=' viewBox="0 0 '+g+" "+g+'" ',l+=' preserveAspectRatio="xMinYMin meet"',l+=n.text||e.text?' role="img" aria-labelledby="'+y([n.id,e.id].join(" ").trim())+'"':"",l+=">",l+=n.text?'<title id="'+y(n.id)+'">'+y(n.text)+"</title>":"",l+=e.text?'<description id="'+y(e.id)+'">'+y(e.text)+"</description>":"",l+='<rect width="100%" height="100%" fill="white" cx="0" cy="0"/>',l+='<path d="',a=0;a<f.getModuleCount();a+=1)for(u=a*t+r,i=0;i<f.getModuleCount();i+=1)f.isDark(a,i)&&(l+="M"+(i*t+r)+","+u+c);return l+='" stroke="transparent" fill="black"/>',l+="</svg>"},f.createDataURL=function(t,r){t=t||2,r=void 0===r?4*t:r;var e=f.getModuleCount()*t+2*r,n=r,o=e-r;return I(e,e,(function(r,e){if(n<=r&&r<o&&n<=e&&e<o){var i=Math.floor((r-n)/t),a=Math.floor((e-n)/t);return f.isDark(a,i)?0:1}return 1}))},f.createImgTag=function(t,r,e){t=t||2,r=void 0===r?4*t:r;var n=f.getModuleCount()*t+2*r,o="";return o+="<img",o+=' src="',o+=f.createDataURL(t,r),o+='"',o+=' width="',o+=n,o+='"',o+=' height="',o+=n,o+='"',e&&(o+=' alt="',o+=y(e),o+='"'),o+="/>"};var y=function(t){for(var r="",e=0;e<t.length;e+=1){var n=t.charAt(e);switch(n){case"<":r+="&lt;";break;case">":r+="&gt;";break;case"&":r+="&amp;";break;case'"':r+="&quot;";break;default:r+=n}}return r};return f.createASCII=function(t,r){if((t=t||1)<2)return function(t){t=void 0===t?2:t;var r,e,n,o,i,a=1*f.getModuleCount()+2*t,u=t,c=a-t,g={"██":"█","█ ":"▀"," █":"▄","  ":" "},l={"██":"▀","█ ":"▀"," █":" ","  ":" "},h="";for(r=0;r<a;r+=2){for(n=Math.floor((r-u)/1),o=Math.floor((r+1-u)/1),e=0;e<a;e+=1)i="█",u<=e&&e<c&&u<=r&&r<c&&f.isDark(n,Math.floor((e-u)/1))&&(i=" "),u<=e&&e<c&&u<=r+1&&r+1<c&&f.isDark(o,Math.floor((e-u)/1))?i+=" ":i+="█",h+=t<1&&r+1>=c?l[i]:g[i];h+="\n"}return a%2&&t>0?h.substring(0,h.length-a-1)+Array(a+1).join("▀"):h.substring(0,h.length-1)}(r);t-=1,r=void 0===r?2*t:r;var e,n,o,i,a=f.getModuleCount()*t+2*r,u=r,c=a-r,g=Array(t+1).join("██"),l=Array(t+1).join("  "),h="",s="";for(e=0;e<a;e+=1){for(o=Math.floor((e-u)/t),s="",n=0;n<a;n+=1)i=1,u<=n&&n<c&&u<=e&&e<c&&f.isDark(o,Math.floor((n-u)/t))&&(i=0),s+=i?g:l;for(o=0;o<t;o+=1)h+=s+"\n"}return h.substring(0,h.length-1)},f.renderTo2dContext=function(t,r){r=r||2;for(var e=f.getModuleCount(),n=0;n<e;n++)for(var o=0;o<e;o++)t.fillStyle=f.isDark(n,o)?"black":"white",t.fillRect(n*r,o*r,r,r)},f};t.stringToBytes=(t.stringToBytesFuncs={default:function(t){for(var r=[],e=0;e<t.length;e+=1){var n=t.charCodeAt(e);r.push(255&n)}return r}}).default,t.createStringToBytes=function(t,r){var e=function(){for(var e=S(t),n=function(){var t=e.read();if(-1==t)throw"eof";return t},o=0,i={};;){var a=e.read();if(-1==a)break;var u=n(),f=n()<<8|n();i[String.fromCharCode(a<<8|u)]=f,o+=1}if(o!=r)throw o+" != "+r;return i}(),n="?".charCodeAt(0);return function(t){for(var r=[],o=0;o<t.length;o+=1){var i=t.charCodeAt(o);if(i<128)r.push(i);else{var a=e[t.charAt(o)];"number"==typeof a?(255&a)==a?r.push(a):(r.push(a>>>8),r.push(255&a)):r.push(n)}}return r}};var r,e,n,o,i,a=1,u=2,f=4,c=8,g={L:1,M:0,Q:3,H:2},l=0,h=1,s=2,v=3,d=4,w=5,p=6,y=7,B=(r=[[],[6,18],[6,22],[6,26],[6,30],[6,34],[6,22,38],[6,24,42],[6,26,46],[6,28,50],[6,30,54],[6,32,58],[6,34,62],[6,26,46,66],[6,26,48,70],[6,26,50,74],[6,30,54,78],[6,30,56,82],[6,30,58,86],[6,34,62,90],[6,28,50,72,94],[6,26,50,74,98],[6,30,54,78,102],[6,28,54,80,106],[6,32,58,84,110],[6,30,58,86,114],[6,34,62,90,118],[6,26,50,74,98,122],[6,30,54,78,102,126],[6,26,52,78,104,130],[6,30,56,82,108,134],[6,34,60,86,112,138],[6,30,58,86,114,142],[6,34,62,90,118,146],[6,30,54,78,102,126,150],[6,24,50,76,102,128,154],[6,28,54,80,106,132,158],[6,32,58,84,110,136,162],[6,26,54,82,110,138,166],[6,30,58,86,114,142,170]],e=1335,n=7973,i=function(t){for(var r=0;0!=t;)r+=1,t>>>=1;return r},(o={}).getBCHTypeInfo=function(t){for(var r=t<<10;i(r)-i(e)>=0;)r^=e<<i(r)-i(e);return 21522^(t<<10|r)},o.getBCHTypeNumber=function(t){for(var r=t<<12;i(r)-i(n)>=0;)r^=n<<i(r)-i(n);return t<<12|r},o.getPatternPosition=function(t){return r[t-1]},o.getMaskFunction=function(t){switch(t){case l:return function(t,r){return(t+r)%2==0};case h:return function(t,r){return t%2==0};case s:return function(t,r){return r%3==0};case v:return function(t,r){return(t+r)%3==0};case d:return function(t,r){return(Math.floor(t/2)+Math.floor(r/3))%2==0};case w:return function(t,r){return t*r%2+t*r%3==0};case p:return function(t,r){return(t*r%2+t*r%3)%2==0};case y:return function(t,r){return(t*r%3+(t+r)%2)%2==0};default:throw"bad maskPattern:"+t}},o.getErrorCorrectPolynomial=function(t){for(var r=k([1],0),e=0;e<t;e+=1)r=r.multiply(k([1,C.gexp(e)],0));return r},o.getLengthInBits=function(t,r){if(1<=r&&r<10)switch(t){case a:return 10;case u:return 9;case f:case c:return 8;default:throw"mode:"+t}else if(r<27)switch(t){case a:return 12;case u:return 11;case f:return 16;case c:return 10;default:throw"mode:"+t}else{if(!(r<41))throw"type:"+r;switch(t){case a:return 14;case u:return 13;case f:return 16;case c:return 12;default:throw"mode:"+t}}},o.getLostPoint=function(t){for(var r=t.getModuleCount(),e=0,n=0;n<r;n+=1)for(var o=0;o<r;o+=1){for(var i=0,a=t.isDark(n,o),u=-1;u<=1;u+=1)if(!(n+u<0||r<=n+u))for(var f=-1;f<=1;f+=1)o+f<0||r<=o+f||0==u&&0==f||a==t.isDark(n+u,o+f)&&(i+=1);i>5&&(e+=3+i-5)}for(n=0;n<r-1;n+=1)for(o=0;o<r-1;o+=1){var c=0;t.isDark(n,o)&&(c+=1),t.isDark(n+1,o)&&(c+=1),t.isDark(n,o+1)&&(c+=1),t.isDark(n+1,o+1)&&(c+=1),0!=c&&4!=c||(e+=3)}for(n=0;n<r;n+=1)for(o=0;o<r-6;o+=1)t.isDark(n,o)&&!t.isDark(n,o+1)&&t.isDark(n,o+2)&&t.isDark(n,o+3)&&t.isDark(n,o+4)&&!t.isDark(n,o+5)&&t.isDark(n,o+6)&&(e+=40);for(o=0;o<r;o+=1)for(n=0;n<r-6;n+=1)t.isDark(n,o)&&!t.isDark(n+1,o)&&t.isDark(n+2,o)&&t.isDark(n+3,o)&&t.isDark(n+4,o)&&!t.isDark(n+5,o)&&t.isDark(n+6,o)&&(e+=40);var g=0;for(o=0;o<r;o+=1)for(n=0;n<r;n+=1)t.isDark(n,o)&&(g+=1);return e+=Math.abs(100*g/r/r-50)/5*10},o),C=function(){for(var t=new Array(256),r=new Array(256),e=0;e<8;e+=1)t[e]=1<<e;for(e=8;e<256;e+=1)t[e]=t[e-4]^t[e-5]^t[e-6]^t[e-8];for(e=0;e<255;e+=1)r[t[e]]=e;var n={glog:function(t){if(t<1)throw"glog("+t+")";return r[t]},gexp:function(r){for(;r<0;)r+=255;for(;r>=256;)r-=255;return t[r]}};return n}();function k(t,r){if(void 0===t.length)throw t.length+"/"+r;var e=function(){for(var e=0;e<t.length&&0==t[e];)e+=1;for(var n=new Array(t.length-e+r),o=0;o<t.length-e;o+=1)n[o]=t[o+e];return n}(),n={getAt:function(t){return e[t]},getLength:function(){return e.length},multiply:function(t){for(var r=new Array(n.getLength()+t.getLength()-1),e=0;e<n.getLength();e+=1)for(var o=0;o<t.getLength();o+=1)r[e+o]^=C.gexp(C.glog(n.getAt(e))+C.glog(t.getAt(o)));return k(r,0)},mod:function(t){if(n.getLength()-t.getLength()<0)return n;for(var r=C.glog(n.getAt(0))-C.glog(t.getAt(0)),e=new Array(n.getLength()),o=0;o<n.getLength();o+=1)e[o]=n.getAt(o);for(o=0;o<t.getLength();o+=1)e[o]^=C.gexp(C.glog(t.getAt(o))+r);return k(e,0).mod(t)}};return n}var A=function(){var t=[[1,26,19],[1,26,16],[1,26,13],[1,26,9],[1,44,34],[1,44,28],[1,44,22],[1,44,16],[1,70,55],[1,70,44],[2,35,17],[2,35,13],[1,100,80],[2,50,32],[2,50,24],[4,25,9],[1,134,108],[2,67,43],[2,33,15,2,34,16],[2,33,11,2,34,12],[2,86,68],[4,43,27],[4,43,19],[4,43,15],[2,98,78],[4,49,31],[2,32,14,4,33,15],[4,39,13,1,40,14],[2,121,97],[2,60,38,2,61,39],[4,40,18,2,41,19],[4,40,14,2,41,15],[2,146,116],[3,58,36,2,59,37],[4,36,16,4,37,17],[4,36,12,4,37,13],[2,86,68,2,87,69],[4,69,43,1,70,44],[6,43,19,2,44,20],[6,43,15,2,44,16],[4,101,81],[1,80,50,4,81,51],[4,50,22,4,51,23],[3,36,12,8,37,13],[2,116,92,2,117,93],[6,58,36,2,59,37],[4,46,20,6,47,21],[7,42,14,4,43,15],[4,133,107],[8,59,37,1,60,38],[8,44,20,4,45,21],[12,33,11,4,34,12],[3,145,115,1,146,116],[4,64,40,5,65,41],[11,36,16,5,37,17],[11,36,12,5,37,13],[5,109,87,1,110,88],[5,65,41,5,66,42],[5,54,24,7,55,25],[11,36,12,7,37,13],[5,122,98,1,123,99],[7,73,45,3,74,46],[15,43,19,2,44,20],[3,45,15,13,46,16],[1,135,107,5,136,108],[10,74,46,1,75,47],[1,50,22,15,51,23],[2,42,14,17,43,15],[5,150,120,1,151,121],[9,69,43,4,70,44],[17,50,22,1,51,23],[2,42,14,19,43,15],[3,141,113,4,142,114],[3,70,44,11,71,45],[17,47,21,4,48,22],[9,39,13,16,40,14],[3,135,107,5,136,108],[3,67,41,13,68,42],[15,54,24,5,55,25],[15,43,15,10,44,16],[4,144,116,4,145,117],[17,68,42],[17,50,22,6,51,23],[19,46,16,6,47,17],[2,139,111,7,140,112],[17,74,46],[7,54,24,16,55,25],[34,37,13],[4,151,121,5,152,122],[4,75,47,14,76,48],[11,54,24,14,55,25],[16,45,15,14,46,16],[6,147,117,4,148,118],[6,73,45,14,74,46],[11,54,24,16,55,25],[30,46,16,2,47,17],[8,132,106,4,133,107],[8,75,47,13,76,48],[7,54,24,22,55,25],[22,45,15,13,46,16],[10,142,114,2,143,115],[19,74,46,4,75,47],[28,50,22,6,51,23],[33,46,16,4,47,17],[8,152,122,4,153,123],[22,73,45,3,74,46],[8,53,23,26,54,24],[12,45,15,28,46,16],[3,147,117,10,148,118],[3,73,45,23,74,46],[4,54,24,31,55,25],[11,45,15,31,46,16],[7,146,116,7,147,117],[21,73,45,7,74,46],[1,53,23,37,54,24],[19,45,15,26,46,16],[5,145,115,10,146,116],[19,75,47,10,76,48],[15,54,24,25,55,25],[23,45,15,25,46,16],[13,145,115,3,146,116],[2,74,46,29,75,47],[42,54,24,1,55,25],[23,45,15,28,46,16],[17,145,115],[10,74,46,23,75,47],[10,54,24,35,55,25],[19,45,15,35,46,16],[17,145,115,1,146,116],[14,74,46,21,75,47],[29,54,24,19,55,25],[11,45,15,46,46,16],[13,145,115,6,146,116],[14,74,46,23,75,47],[44,54,24,7,55,25],[59,46,16,1,47,17],[12,151,121,7,152,122],[12,75,47,26,76,48],[39,54,24,14,55,25],[22,45,15,41,46,16],[6,151,121,14,152,122],[6,75,47,34,76,48],[46,54,24,10,55,25],[2,45,15,64,46,16],[17,152,122,4,153,123],[29,74,46,14,75,47],[49,54,24,10,55,25],[24,45,15,46,46,16],[4,152,122,18,153,123],[13,74,46,32,75,47],[48,54,24,14,55,25],[42,45,15,32,46,16],[20,147,117,4,148,118],[40,75,47,7,76,48],[43,54,24,22,55,25],[10,45,15,67,46,16],[19,148,118,6,149,119],[18,75,47,31,76,48],[34,54,24,34,55,25],[20,45,15,61,46,16]],r=function(t,r){var e={};return e.totalCount=t,e.dataCount=r,e},e={};return e.getRSBlocks=function(e,n){var o=function(r,e){switch(e){case g.L:return t[4*(r-1)+0];case g.M:return t[4*(r-1)+1];case g.Q:return t[4*(r-1)+2];case g.H:return t[4*(r-1)+3];default:return}}(e,n);if(void 0===o)throw"bad rs block @ typeNumber:"+e+"/errorCorrectionLevel:"+n;for(var i=o.length/3,a=[],u=0;u<i;u+=1)for(var f=o[3*u+0],c=o[3*u+1],l=o[3*u+2],h=0;h<f;h+=1)a.push(r(c,l));return a},e}(),b=function(){var t=[],r=0,e={getBuffer:function(){return t},getAt:function(r){var e=Math.floor(r/8);return 1==(t[e]>>>7-r%8&1)},put:function(t,r){for(var n=0;n<r;n+=1)e.putBit(1==(t>>>r-n-1&1))},getLengthInBits:function(){return r},putBit:function(e){var n=Math.floor(r/8);t.length<=n&&t.push(0),e&&(t[n]|=128>>>r%8),r+=1}};return e},M=function(t){var r=a,e=t,n={getMode:function(){return r},getLength:function(t){return e.length},write:function(t){for(var r=e,n=0;n+2<r.length;)t.put(o(r.substring(n,n+3)),10),n+=3;n<r.length&&(r.length-n==1?t.put(o(r.substring(n,n+1)),4):r.length-n==2&&t.put(o(r.substring(n,n+2)),7))}},o=function(t){for(var r=0,e=0;e<t.length;e+=1)r=10*r+i(t.charAt(e));return r},i=function(t){if("0"<=t&&t<="9")return t.charCodeAt(0)-"0".charCodeAt(0);throw"illegal char :"+t};return n},x=function(t){var r=u,e=t,n={getMode:function(){return r},getLength:function(t){return e.length},write:function(t){for(var r=e,n=0;n+1<r.length;)t.put(45*o(r.charAt(n))+o(r.charAt(n+1)),11),n+=2;n<r.length&&t.put(o(r.charAt(n)),6)}},o=function(t){if("0"<=t&&t<="9")return t.charCodeAt(0)-"0".charCodeAt(0);if("A"<=t&&t<="Z")return t.charCodeAt(0)-"A".charCodeAt(0)+10;switch(t){case" ":return 36;case"$":return 37;case"%":return 38;case"*":return 39;case"+":return 40;case"-":return 41;case".":return 42;case"/":return 43;case":":return 44;default:throw"illegal char :"+t}};return n},m=function(r){var e=f,n=t.stringToBytes(r),o={getMode:function(){return e},getLength:function(t){return n.length},write:function(t){for(var r=0;r<n.length;r+=1)t.put(n[r],8)}};return o},L=function(r){var e=c,n=t.stringToBytesFuncs.SJIS;if(!n)throw"sjis not supported.";!function(){var t=n("友");if(2!=t.length||38726!=(t[0]<<8|t[1]))throw"sjis not supported."}();var o=n(r),i={getMode:function(){return e},getLength:function(t){return~~(o.length/2)},write:function(t){for(var r=o,e=0;e+1<r.length;){var n=(255&r[e])<<8|255&r[e+1];if(33088<=n&&n<=40956)n-=33088;else{if(!(57408<=n&&n<=60351))throw"illegal char at "+(e+1)+"/"+n;n-=49472}n=192*(n>>>8&255)+(255&n),t.put(n,13),e+=2}if(e<r.length)throw"illegal char at "+(e+1)}};return i},D=function(){var t=[],r={writeByte:function(r){t.push(255&r)},writeShort:function(t){r.writeByte(t),r.writeByte(t>>>8)},writeBytes:function(t,e,n){e=e||0,n=n||t.length;for(var o=0;o<n;o+=1)r.writeByte(t[o+e])},writeString:function(t){for(var e=0;e<t.length;e+=1)r.writeByte(t.charCodeAt(e))},toByteArray:function(){return t},toString:function(){var r="";r+="[";for(var e=0;e<t.length;e+=1)e>0&&(r+=","),r+=t[e];return r+="]"}};return r},S=function(t){var r=t,e=0,n=0,o=0,i={read:function(){for(;o<8;){if(e>=r.length){if(0==o)return-1;throw"unexpected end of file./"+o}var t=r.charAt(e);if(e+=1,"="==t)return o=0,-1;t.match(/^\s$/)||(n=n<<6|a(t.charCodeAt(0)),o+=6)}var i=n>>>o-8&255;return o-=8,i}},a=function(t){if(65<=t&&t<=90)return t-65;if(97<=t&&t<=122)return t-97+26;if(48<=t&&t<=57)return t-48+52;if(43==t)return 62;if(47==t)return 63;throw"c:"+t};return i},I=function(t,r,e){for(var n=function(t,r){var e=t,n=r,o=new Array(t*r),i={setPixel:function(t,r,n){o[r*e+t]=n},write:function(t){t.writeString("GIF87a"),t.writeShort(e),t.writeShort(n),t.writeByte(128),t.writeByte(0),t.writeByte(0),t.writeByte(0),t.writeByte(0),t.writeByte(0),t.writeByte(255),t.writeByte(255),t.writeByte(255),t.writeString(","),t.writeShort(0),t.writeShort(0),t.writeShort(e),t.writeShort(n),t.writeByte(0);var r=a(2);t.writeByte(2);for(var o=0;r.length-o>255;)t.writeByte(255),t.writeBytes(r,o,255),o+=255;t.writeByte(r.length-o),t.writeBytes(r,o,r.length-o),t.writeByte(0),t.writeString(";")}},a=function(t){for(var r=1<<t,e=1+(1<<t),n=t+1,i=u(),a=0;a<r;a+=1)i.add(String.fromCharCode(a));i.add(String.fromCharCode(r)),i.add(String.fromCharCode(e));var f,c,g,l=D(),h=(f=l,c=0,g=0,{write:function(t,r){if(t>>>r!=0)throw"length over";for(;c+r>=8;)f.writeByte(255&(t<<c|g)),r-=8-c,t>>>=8-c,g=0,c=0;g|=t<<c,c+=r},flush:function(){c>0&&f.writeByte(g)}});h.write(r,n);var s=0,v=String.fromCharCode(o[s]);for(s+=1;s<o.length;){var d=String.fromCharCode(o[s]);s+=1,i.contains(v+d)?v+=d:(h.write(i.indexOf(v),n),i.size()<4095&&(i.size()==1<<n&&(n+=1),i.add(v+d)),v=d)}return h.write(i.indexOf(v),n),h.write(e,n),h.flush(),l.toByteArray()},u=function(){var t={},r=0,e={add:function(n){if(e.contains(n))throw"dup key:"+n;t[n]=r,r+=1},size:function(){return r},indexOf:function(r){return t[r]},contains:function(r){return void 0!==t[r]}};return e};return i}(t,r),o=0;o<r;o+=1)for(var i=0;i<t;i+=1)n.setPixel(i,o,e(i,o));var a=D();n.write(a);for(var u=function(){var t=0,r=0,e=0,n="",o={},i=function(t){n+=String.fromCharCode(a(63&t))},a=function(t){if(t<0);else{if(t<26)return 65+t;if(t<52)return t-26+97;if(t<62)return t-52+48;if(62==t)return 43;if(63==t)return 47}throw"n:"+t};return o.writeByte=function(n){for(t=t<<8|255&n,r+=8,e+=1;r>=6;)i(t>>>r-6),r-=6},o.flush=function(){if(r>0&&(i(t<<6-r),t=0,r=0),e%3!=0)for(var o=3-e%3,a=0;a<o;a+=1)n+="="},o.toString=function(){return n},o}(),f=a.toByteArray(),c=0;c<f.length;c+=1)u.writeByte(f[c]);return u.flush(),"data:image/gif;base64,"+u};return t}();qrcode.stringToBytesFuncs["UTF-8"]=function(t){return function(t){for(var r=[],e=0;e<t.length;e++){var n=t.charCodeAt(e);n<128?r.push(n):n<2048?r.push(192|n>>6,128|63&n):n<55296||n>=57344?r.push(224|n>>12,128|n>>6&63,128|63&n):(e++,n=65536+((1023&n)<<10|1023&t.charCodeAt(e)),r.push(240|n>>18,128|n>>12&63,128|n>>6&63,128|63&n))}return r}(t)},function(t){"function"==typeof define&&define.amd?define([],t):"object"==typeof exports&&(module.exports=t())}((function(){return qrcode}));
  </script>

  <script>
    // MSV2_ART_CATALOG_BEGIN
    const artOptions = [
      ["cactus_cluster", "Cactus Cluster"],
      ["color_bonsai_bowl", "Color Bonsai Bowl"],
      ["color_grass_planter", "Color Grass Planter"],
      ["color_hanging_clump", "Color Hanging Clump"],
      ["color_hanging_vine", "Color Hanging Vine"],
      ["color_round_pot", "Color Round Pot"],
      ["color_vine_pot", "Color Vine Pot"],
      ["color_zigzag_pot", "Color Zigzag Pot"],
      ["cup_plant", "Cup Plant"],
      ["monstera", "Monstera"],
      ["spindly", "Spindly"],
      ["sprout_pot", "Sprout Pot"],
      ["wide_planter", "Wide Planter"],
    ];
    const artImages = {
      "cactus_cluster": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAC0UlEQVR4nO2dMUscQRzF/xeC2KS0ksNGVDiwsLlU6dIEDrtAxC/gN0hn6zfYLyAKtkKadKlyTYrAgQlJIcHK0kYs1DI3b//szO6+3R2S9+vGGWeHx/Dmv4+5u9GTPdlQTDfWKh8+v74d9bUWFi+GXsC/hgQlI0HJvOzzYeiZ65eHQf+fT3fh+KLssbn7qnYoGQlKRoKS6dVDYzx8WQTt1QZzoE+j53q1L9OXtUPJSFAyEpRMrx5a8rNZ6GcrR/vB+HtnjqlVv/9jbYvPwH4c09ZPtUPJSFAyEpSMBCWTVTjy++PXoL3yZlKeZPEraMYClofJZmW/mdl4aQ48xMzqHVTaoWQkKBkJSobmobFQIgXXM4F78MSb2WnQRs9EMIAxM7sp/vqyCvvMkKBkJCiZxh4aqym9eg7HxPzP89TVunUoeKY35/rJ68p1qw4dEAlKRoKSGaVeFqt7ScGr99C/xu9eBW301EcInL15Y56Kc3oB8/IY1aGZIUHJSFAyyR6KoKfiO3YK6H+MOZqgiw4ZI0HJSFAyEpRMcjjSRWG/HEqYlYtw75BqW8hjPxvtUDISlIwEJUMr7FO8KjYGPdMr2tsGyhjI4DoUjmSGBCUjQck09lAk5aJDLFBBv/Nq2baBiufLCkcyRoKSkaBkaB6KeJ9YO/rwNmh/+/6zco754rr0N9Wh/xkSlIwEJSNByQz68e6D97OgfXZxGbSnk43S/8whUBlj4FJAAB0Jsdloh5KRoGQkKJmsviJjb3er9pgCPBFfHmL9ZmbF+efUJUbRDiUjQclIUDK9eujOznbr8VdXP4K254lV/f4a5KHZIkHJSFAyWdWhTUBPRI+t69tt0Q4lI0HJSFAyEpTMoIcSHiB1+7t4Zlu0Q8lIUDISlExnHurevjsOb5PEgo2UcKQuXpis23cZI0HJdHZZLIXYz/90Qdc/faEdSkaCkpGgZCQomWdF7hfA1f0ktAAAAABJRU5ErkJggg==",
      "color_bonsai_bowl": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAADoklEQVR4nO2aS2jVQBSG/2t144MKUrDUuyiKKCj4QpAKorh040bFnVAQV9aFiiCIIIh1U1eiFHQlKoIbl6IIFuHio6BQKRUFlRZEsPhAkBIX4VByesdJvP9kcsv5Npck0yScfvxnMkktQQKDx4LYNzDfsIKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSsYKSWRjz4vuObUoAoG/7OgDAmSO3awDQP7grAYDhU09q0W7uPzFDydRCvvUUAx9cHa0127+ytzMzfur9NIBZY0cabzPH5TwXbxxKgFmjq4QZSiaooYLLyNVd3QCAd18mc53HNb5KWWuGkgnS5aVLC31daSZqs2Q7r6l5x8fMWDOUTNAMFVN1986bmRqXmfr8Mbu/GUqGaqjOTo2YpLs9G+n6rnlwSMxQMkEyVLqsPOkUNTJv1/dlqly3zHmqGUqGaqjOLJ+p2rBWzfSNL6P7m6FkKIaKiWKMr5u7DOu4/x0AMLN/WUv3E3NeaoaSoWaoNjUvYuyH6+MAgPX1xQCAhyt+ZMYVNV6QLq+f3ELMT81QMkHmodqEntH0/+bLRtd4yVYx9822jqZ/n3deqvczTTVDyVDXQyVDpxpZI2Z60+OuLi6m7P26NN1RT3/Gcpop5DVTTA6BGUomyDzUxYbnMwDcps0xVSFdX4xr9Z1UiGd8M5RMkC7vesup0V1dslIjZko31ufXWekzVx9ndnszlEzQ1SZtksscyVYXA3efNjVHX0+/MfCZavPQNqCUL0dcq1HajKEDO5vejMtQ4eaJ9Pz3fv/7DUHIZ3jBDCVTiqGC7y3k0R1bEmBut3cZKmZ++/wps3/s4y8AwLVnL+3LkXanVEN9uAxd3rMKwFwThZhGasxQMlG/sc9LlTLShxlKplIZKriyVPDNS2NihpKpZIZKNg7Vmz85VRkzlEwlM/Ty8c0JAEw00qh0Zemf7p8AgJNXXlUmU81QMpUwVIwUFk0uyRyXeafLVKEK3d8MJRPVUG2mRkyVrPRtCzEz1QwlU4qhZ4cPZi7S+Xo8+DUBYHrj2sz2hf479gVzuxHUUDGzLCN9iLEhTTVDyZTyLK+zbD5jhpIppcvvPrym0EX69mzNNW7k0YtC9/H41oR1+XajkuuhRc1zMXD6HABg6NJ5yvnyYIaSqaShrRLDTMEMJVPqalPRbs+ijO4umKFkoqyH6tWnUJSxuqQxQ8n8BcJ11t30J97TAAAAAElFTkSuQmCC",
      "color_grass_planter": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAFN0lEQVR4nO1dPWsbSRh+Nhg3F8ifCFjlmhxqZBBOdQYV7myuE6RR5cqFIGWICleu1ATUHUqXQnBX3SE4NeIOq5Th/sRxSSMEk8J+V69Gs5+aeUfSzWOMV7vzsfvs+z27MhT24+fj4Er5PociPy+wR+gNrlVvcK1o2/f5mLBXhO4DIoXdudFc6rrtYUSfu+1hZDrO+/HPPrFThBJ0dSayWp1YAcCoP4uKtPeBI5eDp0lU3rG8/VnwLanBhlqGGKFZXplUmdBtD6PJdF5q/Ml0jrJ9XMCbhJpUk4dF+wpRQrPIyiOyDNG8rS79rhFsqGU49fJ6rKjbOD2+rOqhu+1hRJKoZ1IN1IB+pdOvBKeEcvCL1jGZzjdiSxMa9drTRn8zmE+OYfNGbnHapRFU3jLEJDQPrU6sikgpsCl1rU6sMH3aLjqGK4gTakofR/1Zqjkg8NyePvPjOpHJeFMLJ10Coiq/jfQUSVVtz1kFwYZahnOVrypZOrKiBNM85PV7ddnSnqgN7Q2ulSubZrNitQ2cE2oqDqchrd6Z194EX4WSYEMtQ4zQbnsYcfXTpStPKk0S3qjXkl8O6YIIh3gcSsTxi07IyrGvPPUsOo80dkLlSXJJ2kzSSEVnk23Mcjx6jdV1br8ThB4SvObyacvGwHo6Stt6bJnmyRv1VcmOxpWqOjknlEhp1Gu5MWFeqKM7tazgPa3G6joudb6M3EAttx1dpMlhFelHc9E+U31VKi4NNtQynBJaVb2ax9W10veqqXMJ1QN6jrQwaLwwc7KNc8kKyWwiqLxliBGqS6KeLnIUUXmSNu6MijikvfbyeUhLJccLhducvmkrpTwG9QFvhHbbw+ju5tRoz0hC6S/VUbctUFftWwbBhlqGV0LHC2X06LSPH8+yuRyT6dxr6LQTD4uR6qe9lKCrK3/shtrd3Zyqu5tT5bs2KvYEs56C9gbXatRepZrcCREp3NtzT908jnB7/xD16qvxSZJHBlspWRsNNtQyRJ++K9qe2pLUkfTS/slUYamNt026ahPiYdMqVDpJ9ulk6LVRYN0pNY8jLLF+w5bx03j0VJ6vdSWnKm963WUZnyQXTxgvFPSY9Gj2iOZxlJB9NHtMsqHxQq2ReXdzqo5mj0kbAEnf8CjOnkP0URwOLmGj/syYNek2lGCylyap533LPkRRFWL1UNom1SwCUlu6Ka1OrHyuuReBc5UnAnUi0vL4NCzjk4TgZXxitM/8FUZufyVvQrChliGQKT3ZtgYA9IHb+4cIMEso7aM2BL0yRZKYJeX6GFLe3uuaEs/FdQLKQO+rOy3JoF9sTWkynZeyZVkE560t8QpWqxOrtDUqFwg21DLECB31Z5GE6ul2VTpbEs3lxwuFMQXY95vBPHdYWSpvyvX5HDxRKLI+ZRNB5S1D/D0lHnDrKJMJpT2rpI8tXRzZiVcTTSS2OrFqgmU6i9UxMhvJ2hPrT318QZzQNRIWxdqWBb8BkvYTCDbUOsS+t+n859cKAH549VJkPsK3f78CAP745R8RWyCu8nSBhwoxlf9PaiLPcwcbahmi333347MdlcZfQvYT8PBlgu8/XSkA+O33v53O89PbNwCAD+8+/z8Ce7rgQ0OwoZYhSiip+yHPKUaoDzJ9zB1U3jICoZZxsIRenl3g8uxCfN6DJdQXAqGWsRMVe9u4PLvAlz9/9TK3aOrpK3SSTD9FVV46r/YxZ7ChliFuQyfPVaYGK45k7eP7TfuKtJWEePnuXLgmKrWWRPD2z1VcEytNJCHYUMsIhFrGdxzjr7rnj5H5AAAAAElFTkSuQmCC",
      "color_hanging_clump": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAFLklEQVR4nO2cv28cRRTHv4ei/AEUpERK48NIyJVFfoi0KQ6BoeCaRLKorqKNEY6sEOXSUrmKTjKNmyQgUrhFwhC5OkXC3DVILpMif0AEGorjnWffzczO7rzZ25zex03WOzu7nnzmzc/djoHBMnj6+z0DAFtX73b4udsPNw0AHNw5WThXdu2yeWfZD7BqXFjWjSfTUwDAcNQ3ALCzfVhqKplJ18YwHPWNK+9cqKHCLM3Q7to6gDjbyNQPppdL05Lx/D5NkbVA+R9nVz1qUHga4LyKzwvyUrEgY6owFWTTDZdWeWE6ObtNrkbEZxfZCPgN/evl3wvXvT57Uzh+tj/2Nm48/xyoocJkjaEUv1ym9gYbBgDeff/iwnVkFNm3c6dotX2ert/6pDfLF7N8yVRuZ27UUGFEY6jPBopZZCVwbiZZaJvKfxc65j0Aat0f/fhkId+csZNQQ4XJ0spzU22jXEbaaWxi0hDXNruFY+oRvD5742z5c6GGCiNq6P29F8mZPX95Gx9fOvCeA+A8T+fI1H/Pvi297ru9j8TNzVLlb258bwDg+uefAQB+++ln0DH9m+BpLlx5jH/++NJ7DjgvGDsvSkuFR3m4zvFQItlYaZUXRsRQ3gi9Ovy0dl62oaE0AJzpYs65BhOAjKlqqDDJQ09XZ/69/i8A0kwNQfbF2OxCJ0feIpINjfnfplY/lhvf/Fk4/vWHDys+VZGj8e5s6HvlcfaJEjVUmKwTzARNisTGO2qN+XCSTK3Syh+Ndzt0f55fjtVQNVQYkQlm30ojTaERR+PdTtV4akOx9fikPC3FTddzxewJqEtygdqF6StIe7aHqiT9LlTAvDGKqeqc3mDDzO+/XzxHKwmSaJUXJtlQV3V5NHhSWNcJQVWzbihI6RLlWLNXQ4XJ0m1y7QYBgOOTSam19/deGN/8Jf+9PZ9JXSPKn+KjHc/L7j0c9U3qjhM1VBjRdfl5BxrdsqQLkNXPAxb3BrPqFJpp53sAnu2P5x17bjGnu7ZeaaukCzVUGBFD7fV2wN9R5umAczMpdh2fTLz3IbNcptGwMmSY73rb6tROvhoqzNI23M5HKdPZcWrs4oRM46Zem1aP+T5qF6hdTagRClVXoNhA8D+CCsAVFjhUvZ/eumeoe1Plevt5gMUNZilolRemtqFURbtr6wsbs8reMwJkZnikw4QEaqgwyY2StCV2nCXjfabHvuFB+ZR17CVQQ4WpbKhv4gNYfNkgJpbGwPOh+JtjgjgVNVSYyobydZmd7cMO7/vl2pnBTY2ZYot5acHVr61L5QIN3TBm0YsefrhZnpaHkNxMpqfJr45rlRdGZCz/9a0vAMgszxZWKf8nJYTY1/qGpfScw1Hf6HxoyxAx1B6G2secUJcrFYlOuz1jX7eWqaHCiBhqx6DYtLkIDSZCM/7ArGWv8xkOGzVUGNEC7a6tBycsmvyYSl22rt7tpHTu1VBhRNeUQt8RaZrUiZm6tSnLIl1q1c6xb7Op+2iVF6aRPfZV6A02DB/KxhjEu0Kuj8IQvpAkYaoaKszSNjqE4EPZKpB9B9vlK67cVInv5KmhwrTSUKLK8I+/gxQDt3E46pvUll8NFaZ1rTzg/06oyxoeB2l/VZMfbrFRQ4VpZQwNfeKt7bSyQIkqsz51tjPmQKu8MFqgwmiBCtPqGBpD21YB1FBhWtmxT4F3tZo2WA0V5q2PoZyUqT8J1FBhVrZAJ9NTTKanja/ArlyV5/vvm54HWFlDl4ZZ8Z8Ho69Mk/dTQ4X5D80TmKjN9filAAAAAElFTkSuQmCC",
      "color_hanging_vine": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAGqklEQVR4nO1cMWgbSRR9exwuDleCkHTigrHWAhM1J4MWjuNCiiNqnOZUxAaTSm0quzFExUlVWldGYKdIJTWCK0LCcSBBVBkCOimEO9z5MLgyV7iZK8xfz45m1rvSzOzK2adGuzs7kp7e/P/n/5kFQ/KvzuAVE89ttX6YOtcZvGKytml6fYMUYDwZoTtosO6gwfjz260y226VGQB0Bw02nowwnoyU/Yj3J4FUEHqX8G3SX4AQprztVpmtTR5Kr5Eqx5MRMDHz3eLAukK7gwZrtmuBobm389YR2x3tDv1zmz9W/XZiW94MyPqxDYfBrtkhMt1CEQCwWdkPkEA282h36ND7tfsP8de/fwf6uTi9AgD0Dk6k91Mf2n/ALbBOKHBDKnCtqmq9xHL5pal2ImlE1sXpFXL5JRztDp1qvcSoDU8mkAyhmVPSDCsKlSmHlAUAufySr0ZSKqmQf792/8YxuYUiun/2fBWKKk9CnYDFIc+TKiOLv0ZQnffKLoAbUvk2ok21DSuE/rz/4NYPefL9T4Hjd//8ETj+7vwB/rt3NtVe1g6A3/ZD48wqwZkN1QxrQ/7e42UGAOveMj71L7HuLQMAPvUv8fJ5NdC2Pxzj4+cvgTbr3rKvvo+fvwAAXj6voj8c++eobwDYWF1BEjbVOKFkO39/p54J8YS+ftPDxuqKT5oM/PWN1RUAmGovEgrYIdXo1JN3RL88uQ7kRWKJzNdvetCNLA69AzCqUJVCyJ4CamWKZgBQD28e5+8vr+PS1VIiqbxEFErEqCASJjotFc7fXzrVeolV6yXmlV2ISRgbMJ6+C6TXAN8rk5J4tfLoD8d+AA8EPboM1B/BLRQxnox8Um1loowSSll2AAEy+NnMxuoKegcnjoxY3hzIPL+o9Gq9xPy+D4KfbwuZU9IMowrlc518mk3WloasP009hx/Yy44/NM6c2xzPZmXfsV1nsjZTEh0EmQAVwYHhKzkWz3UHDXZ43PGvyfpttmvMLRSnkto6YWXI86m629Bs15iMPBWoWjqejCD+AWJbclQmkdlQzTAeNpFSxLBFVNBtMWPv4MQR7TAfVqna8ybBhsdPRRl5ngA8rOLJ/wnexFW20wkjhPLBvAdXGpCTcqr1EvNwozQq2sn6JUV2txpss7LvhLWd+hxEt8vzwAihpBq3UMThcQe5/FKgPCy2j6uaWYfudqvMTGegMqekGUYJnVVJZPv4xWI8aJGEDLn8EnL5JWWoJutPJ7QTKnMw/DCb5QfRPbKlOHFA30P1R+mAdhvKB8+805jXdvH2TzXTCSPJK7vobjXYUSWzoQsF7YRSOMMPzbA4MywRfLQ7dHR6ZVrYq6s/GYwH9l7ZhVsoxkr0inP5qKTy7USntLfz1mm2a8ZnS1ZmSuPJaMoz6yhPxEmi8CCVmsg6GbehpEjV+vj+cBxQbVzvq/LY/LRT/C4mkTklzUiM0Hljynk+12T/VgiN8iPmDbbj3LtZ2XdMZe2tKTSqIslT66qpN9u1qU0SJpHZUM1IZNMCEF4FpZUfBJWyxWXlQDAWjbJ9RzesK5QvwoXFkGEZJR5e2YVXdnFxejU1Adjbeeu4haLfl42hb43QuLaMJgNhxPIqlsWdAHB43MHhcccaqZkN1QwrU0/ZRi9VW75KSvc12zUm2j+6FraAjIfJxQ08rBA6rzOIak9TgaQ26j+tP2JP64+Y7Jg/r3r91v6VxbkvSp86XonYUFPlB3JMMpOiclq6kTklzbC+cmS7VWbidsRZ8poqu8wvoEhim6I1hYrJj7DyxizDM2p8ufAlEEJYeQKYDq1ki8Nk4DfSRgFfV1rIjP3XhlQQqsPWiRVNr+wqhzeVY0wMf+tOqdmuMQynz5OTiTvXppnSi61nUzUr2fCOWt6eFYko9MXWM+U1fsoZB5uVfYe3o7cVB/k2OpHYpoWo9XlAbhIo/IpqLmyFUamwoXcJ1m1o1G0tYYqKM3WNEnrphHWF2t4qSDC9LpRgjVDKvs/jCPjZFv94oaizKhukZjZUM6wRGmdxQbNdY2GhFT1IK+lnNMmQOoXSfkyZraWEyixk8svBNX1VKVKx8UvEeDKKbWtV608D6bxd84pObKGDCt3B9aYu2bWwECjKxMGGmUjdkF90pI7QWXOUUcvJppE6QueB6UxSFKTSKcmgSvuJdjXp5zDfKYWmAQtBKA1fWmk3K2w8HGthhjyQ/HCOgoVQ6CIhI1QzFoZQ1XCPY1fv5JLwWaAiIqlYMwwLQahOLORTcWwibZ7/q1OoaWSEasb/d0Q56jqG5tQAAAAASUVORK5CYII=",
      "color_round_pot": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAC5klEQVR4nO2aMYsUQRCFa9XsBP+EoOHBwSUbaXy5YiaYmJmJIIggHJeZXSKYieYXa2Syx8GFCv4JwQtlDZYnTO20PTP7uqtH3pfN9MzOUHxbXV09i7WtTfC4Fv0C/xsKKBkFlIwCSkYBJaOAklFAySigZBRQMgooGQWUjAJKRgElo4CSUUDJKKBkFFAyN6JfwHP8/mFnC+HF44+LqHeZggwls2hlT8mb+XX1zczMlod3zWw+pspQMs3lUJiZOm4dGUqmmRwKjp7u977Q2enlqBwaVS3IUDLNGZoC5uZM9YajSgClTZWhZKrN8rncmBrf9feBcuhMqWYoctnYujJ3Xza3no563M7IUDLVV0o54/ysDGCgry+Xtrn++HBzPnrNL0PJhNWhflb2OdDnxtws3kpXSoaSCes25YzcypmrzXVDqwWfa4FWSjMjzNAtg1b946nO/Vb96epNf18tZCiZ6ob+na2dkTDpyPrH/f2p2d/nZpg6tp86FRlKpnod6nNjCp/7vGm5urSWkR4ZSiZspZTa80nlxKH90igzgQwl08yeUs7Mqce1kaFkqhu6694RSK2AfC6u3YWSoWTohqa6PLkVC4zaP7gz6DmXF987xzDx6vf6n+PqNs2MYmv5obubvt+59TufL8zMbHn/oHO8d+tm7/Oufv7qnMd1tb7ik6Fkwvuhvm+ZMg1mAj+eAtd9+fBDs/wcKVaH5vqUua7TUAM9z56/MjOztyeve8dLmypDyRTLoVFraZiZM7UUMpRM9bX8y3cPOg/cu74ROTW7TwWGApiKevbNk09F/kEylEw1Q72ZwNeXbGDq+flZ7zjbVBlKJmylVNpM4Gd55NBSyFAyYYb67lGt55VGhpKpXofee3S7iW3WUmt6GUpGASWjgJIJ+3IkKpeqHzozwr9tqmWq9pRmSrihAN2o1D781ONSfc8UMpRMM4aCVN90LLXNBDKUTHOGgqmmRpkJZCiZ5gxVDhUd/gBZCn7UHV0aDwAAAABJRU5ErkJggg==",
      "color_vine_pot": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAADzElEQVR4nO2bP2gUQRTG3xkViw1phWAhShqPELD0SFDSHJfiwEKwvuYgBKurLK2uOiRwzdWChWChpAlKwlkKcihCUAQlYCuxCEpYi/VtZjczd7vwbt6by/tVy2b2T7773ryZN7OVGGIIkVZ3LX3xQWevwvkuJhe4X2DWCFJQ053SuMj5cJcw1fkIHrVfiwnjMgTpUMlUOJLSpJAddPYqvX4jBoCMU23XSUpIAAyCusR0CdPrN2IUVWpmN9GQJ8abQ23O/Pntl7Xtq/6H1H0Y+gAAfz4l7TvbQ5HuBGDqQzfaK2MfevX6QnpcnY/g2t/k+OuBfEE15Inx7tBx7jSdiSx9P4EbS8l5dOj+ye9MG7OL4IZ1YA+QFcPWz3a2h5XuZi0GgFTY/c9ZQfFHkiCshjwxLEnJhWuc+eJpkunvb40f5Oev40AdSoxIQSe5zOVO17jWJ6JC3gWGPLJznE1K5g/APd8X6dCQYR822ci7rA4RAJyOQweOmZKEwrMYQceJgULiOLTotRwZX0OeGHaHFik240ypCNzj0CCyPILCmtWmXr8R744Oz7TlmoZqyBMj0qFmN1Cdj6yFZSw829yZx6db2QU1F+Ncg3Ic2GO2v3xrIbN45yoJcoS9hjwx3rN83oWDdpKVe/1GDEfF7pHfBCGhDoqoQ4lhH4em/d/IvgQCYJ8pmauhH49OiyXnZhzqSjhmQjEFtQmDyenHJbk7SjTkifEe8qZzJq3PI6kDj//fYyvrPryn6dRWdy3mcKk3QYv8c7Y2ZUpykwrNPtCQJ4Z1pmSGfH4sKSnRlIF96unCFLR+JVux171N5wixDjXJb8UxNzxIIwhBEdsOEmloyBPDPpcvAyYlyahDiQnCobbFOakEkZTySyCShdWQJ0YFJSaIPnTS3iZJqEOJCSIpISHMlIII+XSz2AHzixRAQ56YIByKhJCUxPeh+Q8WEKn9qIY8MSId2t2sxfiB7OpcdOZjWfPc6lwkaiqqDiVGjEM32ivx+vIiACSbaG3HiOvvu6ND9p147ILiUvL68mK6G9l1jBRpq3vsZwR2hz4ePJjKCzxpPWdxKKugdx/enOrD3z77onvsQ0cFJYYl5G395rs379PjO/duW8+buNqY5xGf/ak6lBgx1Sabs8adL9vGFzPn0GatDs1ane35MycoNyooMSooMWKSEgXNWh1eDndY34F16jmteTzCMZ/XkCdGBSWGVdBphiRX+U4dSoyoLF+0QFKmMOIb9oo9QlVs5igqm2jIEyNGUApncbsTQFDIm5QNfwlCImIcOiv8A3kc1PsQbKbOAAAAAElFTkSuQmCC",
      "color_zigzag_pot": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAADCklEQVR4nO2cPWsVQRiFz5X8Cu00YLTyAwIxlbbaiIViIQQEsdFKUBALERSstBFBCFiIFmKjrWIRhYAflSZg0pkfYbMW4Sh3cof9yNmZ2ct5msve3QvL8HBm3nd276hCBaNjT+4bmDY8oGI8oGI8oGI8oGI8oGI8oGI8oGI8oGJmct9AU958ulsBwNr6j7Hv5w4eBgCcPXFnlP6udmJDxRRv6P3lC9vNhvXJ50Njc2NDxYxK7TYxM0lbE3Nlqw0VkzxDY7P1raWXI6A+M5uSK1ttqJhkGfrPvMTQ/FTYUDG9ZWgsK6cdGypGbqhqlo6xsro2drw4P9foulTYUDGyWb5rZROa9PbJ94mz8uvHpysA2NzYAgDcePStiO5SiA0VI8vQOiNpIg18eP1oBQBLh/YBAM5dezdoM4kNFZO920RT9x/YC+C/qanM5KpE1Z2yoWKyG0pCU0ksW9vStnLraqwNFVOMoURlqqqX0NZUGyqmOEPJbk1V91+b9lVtqJhdG9r3Ex2qTOW6dvnnbwDxLhUJK7um2FAxnQ1Nta4jNJWElVUIjQxh5UU+/tm+jMZ2NZPYUDGNDVXvEXXdjYxlah2pulU2VEzjfiizL9f+elvCrEzVR7WhYjrP8meuHqmA/tZzIXXZmcvIEBsqprdaXlXhxNafpLS9JhsqpvduU51hJDS3aWaWYiaxoWKy9UNjBtK8oZlJbKiY7B37prV56WYSGyomu6Gkrn9ZupnEhoopZkA3N7Z21ONDpJgBnRaKyVBStz4tPUttqJji35cnNJYGl2qqDRWTzdArC8cmhvdscBzW9vyM/Z48/fzVHftpINksX2dU36Qy1oaK6d3Qkxdni1rofnjxy0+ODIlkGZrb1L7NJDZUTPJa/vaz8xUArLz/Mvb94qnjY8fh+ZC663n+3uVX/s+RIZMtQ5saFqOt4V0z9NKD+QoAnt9c9VsgOfAsL8aGikk+y6c2NZWZxIaKyban1Lepqc0kNlRM9l1PtalqM/l+lt+Xz0R2Qwlr/K6krtlj2FAxfwE9D5atbizV+AAAAABJRU5ErkJggg==",
      "cup_plant": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAChklEQVR4nO3csUsbYRjH8YtIkeIU4lIpLShCCV1crkvtJtISnQUdHG3+AfEPKF06Vv+AFjq3YpFutotZupQ4BAVFBEGJQzuIQ+LgIj4PuTfn93Kx/D7jw+W9y4+XPLn3vaTQjtqRcAbyvoD/jQKFKVCYAoUN5n0BoeInI1j3rB2eFqixbtMMhSlQmAKFKVAY0pTu0jC8BuGN93BtKe0pjPitHZ9qVJqhMAUKU6Cwrj9Dvc+30Y2F1BcQV7gv7P1AMxSmQGEKFKZAYQoUlvvynfcN4ej7X1P796eZONbA+tegc2r57h5RoDAFCit02pf37oouyuOpT1asvjQ177Px8mfd1B5MlRPH917nGarvmZpWm/qUAoUpUJgChWX2xd5rIs2Pv4KO8ww/LyaOFaq1PGdq8TqzLaIZClOgMAUKU6CwrpuSd5fhaTnNZuz9i7CTvLbHnR8lvyy0wWVJMxSmQGEKFKZAYR2bUuidAvl0cRRF0XHls6nd3ipJ3hC51uttEc1QmAKFKVCYAoX1dF9+f2XH1Ly7m9LQcKbXoX35e0SBwhQoTIHCkEfCi04Tmd9tmNqHbk/WwaP9E1M7cxpc9dUzU4u32Z8B3aQZClOgMAUKU6Cwjk/febymtPnjnTlucdbW5uPHpvalFrBZ5Agd69O3VVN7M21r3nsoTUyaWvx0xtRuNirNUJgChSlQmAKFIU2pdrBljjtr/E5/VaDQxuLx3peaUo8pUJgChSlQWGZ7Sl4z6GfenVIamqEwBQpToDAFCsv9Dwj6hbek59GeUo8pUJgChSlQWNfLd57QJb08JC230TRDYQoUpkBhChSGNCUP/dultLJsQB7NUJgChSlQmAKFXQGoCJtsjC94IQAAAABJRU5ErkJggg==",
      "monstera": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAFnElEQVR4nO2dPWhTURTHT0TERbA4BIqiSAQn62IRF8cKpXQRP4qjU5XipENKpMWCo8QPEJzj0kWiQzZdpCiCdRISRBGEDKLgVofnEM/LeS/nfr133s1ruL/pNe/l3eT0f77uvUkqEUTgm+rSgnbQfqtd8fVapKn4NuhWbzsx4GJjVXmtyrCmf8iL9XtwtjYzln/KnnEMOsmIKrTZXTbebOPu98TfL9bvAYBaqWmVUnXic9PQe/kOH3slbpLFkAi+eWoczricIXXhYlwElxcmt0Jt1GkDVRtVYJzEGsNzZVQmIuLy0nBhoMxuTgkuL0xhCp3uTKUe4ZOSjsXG6q5RJhIUKkzmOpRLRlSVP+Z+Jc6pyiaKqq60Ia3gcbWvuV1eZ8TkNbxBTfVn+jrVNbb3KZrg8sJkVigqU6VKHUWoKV3HVmHQWfl2/aBQYZwVauqMRsul/yp+Ozj20enQcgtahQ41gkgdqkpM+Ph0Zwrqa4PHfCUMHKcPbS/jIcHlhcmdlG68/QRYEtXXjiTOo1o37n6PXV6FbQ1qm9Aenzs1OPDs8s6F/eajegQAcPjCpfgx7o0dmK3Cn3f9+G9VHWlbX3KzUbrn0X/uyokn3jJ9cHlhnBWKWZ62klRFNssS9HmmJKVTsE6p6PKqOrko1TobVLfiKNVGulzLjfn43KnYkLrWuAijBpcXRqQOdW0lJWtReq84EXWG57m6GMHwJalUJ5evLi1EuvLGdkIY37hqSdkEd2/as6u6uenO1P8yT03e3j+4vDBOCt3qbUdcHemSwWl9mK4UdJlctSqqUhRVKZ0Zs5nopmO4bumxiqFxZm+MDmhDfe3I8E3BMKb1W+2KaZ8SR9yna9wT4+Lmo3pkO8XI5QLXuYDg8sI4Z3mTCyI0Qb2LnibuQbMqqqw2e91aqS6J4+LNjYppylGXSGkitnF/bQxNu6OpPKJxkMZKiq5EqT0YGBXnAFgXtDSmKpRwocqlCzONH1xeGKVCaUbnenWb7YecSrJuonVx89qD6xGd6aKYZq8QU/Ohej0jMZRmdNuem7u5ykDVpYXIZPQsYLgAAOjdeqasHqhI8Jh7zGT4KiTfBxJcXhhllrddTMvSquEWRZf7owJX5vclzjdf7QDAQJUur8EUvmw3AqcJChXGqQ7tt9oVXKNRdRBSMRGhsRFBVQIQxd5KXiMVp9Mx1KTShEFpEUtdYlwfUQEY1qRY1zZf7cD9vdX4PLayze5ypKpxlWEJF/CWIA5BpsbFlKiDywuTqEPTs0muCcfGtVzWnChUoTQxcUsc0ksbLvV0UKgwyqSUd+baNLPPXa9TKSailfl9ifnNLLv/XHGxRcKgZ2szFQzQWXFZpaRwS9DcPeiOFG5jGoA+QRVNcHlhRlw+j6vn3arILa9ce/kQAEi92Uk+h3N5lXJ9IPqxGumtiouN1Ti7czumf8z9YrO8j7iqIri8MKIKzbropgOTi+pjPNxGhnEqVPwbHfIuPVAOzFZHZpGa3eXIZLjpzhRcvLkRsvwk4EWh6Zlw27nGKx+62rFe/90fH29/+ViKL34R//Asll3pmSsO1dSYiyGRmeOnozIYNbi8MIV9vLvfale21ret4wltPZ/PXx45z6kSAODgoeHcKHxxfJEFUOj3Np0/MxcBAHw+kVwHUoWCk90dyMOb953g8pNGMKgwXgxq48p53b0sBIUK4+1rhqgCuSJ/UvBi0Nqxo/Fx7+u3hHEnxdWR4PLCjNWgd67eHufwhRAUKkwwqDATY9DfP/kdy76ZGIOWBe8GpSXUJBIUKkwwqDBeDNr7+o09luD3z35pEhJAUKg43iZHpJVZVna1Qsvk6siuNmgZKeXXrpsoozIRL79Wg6ufEqiMWYZNDgDB5cXx9ntKeVXKKbMsqqR4/4EqADfj7hZDIsHlhfkH4OIm31JRWG8AAAAASUVORK5CYII=",
      "spindly": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAH0klEQVR4nO1bXUwUVxT+rjYtBpYKlUIQaWAbFw2FpErTQAI+mJaihPjQxBhDGtIQG0PSN5pGnzSmPtOmlRrSbIxp4oPZUAg2PqDJmthVEy1VIV2JFDYLVFFWIi/t6cNwZ+/cnVkWnJ+7dL+NcXbu3Znh23POd849dxmBsBHAgrNEnaXM6+d4zesHWA/6x+d1K+gOlDAAqH7zcwCDnj0TxyavH2CjIesslAVnKVyUb3ivgqtzZJWF9o/PU7g1H42T29GY50Njng/h1nxDCAA0ksXPyONOIussFACwaxF4UKgdVyVPcyIpsBMsOEHUWcp4jHULLNtUngVnKdyadPmmkSVQZynzh9qpr+EuAODA1Qi8CgNZ5fLZgKwjlDpL2djCS/B/Wrqkoa18irWVTzHxHAvOkhhTnUbWubwZ/KF26q0Z0N+ffdiFaMcgA4wC5UYYyCpR4uQM7W9AW/mUTs6j5+dRW7QF4nsAGI5VEgnzXHlGNyy0f3ye7FJbFpwl0aV7awZQW7QFjXk+XflvVM1gbOEl3FZ4IAtjqPKgLHydG58j8Tg8lyC6WUi0CKJFUHguQeIcN18bQpSsclMvnsVRl+ciIpd+mZaDw7FKyzniNeRUysva3hGVl/M+URzExQ29VDQhwB9qp7409xCv6Q+1U5/vrkH5vUJOlOyG00G6OnSQuHjoArIIopuFFJ5LWApIdeggDcV2rCosmc5z6+W4hUY7Bpk/1G6MhQ8KtRWjDQhHVN6sRcFJffT8PLgiN40sAUjGUBacpaH9DQCAnkg9AKCvwTw28utZjXsF20XJakWd19b+UDuNLWh1d7LiGdTmBXaCXZ2QxlIhLtWpRCaQEyX7YWdANoiOIDznxudoKLZDFw8uVNWhg8SPCQQE4wZx4ePyZ1USIfnlzGoTb0/sWgSWtUMeExGrpGj5FOsfn6deaK4/HKuktvIpJuejsqBxV1fNzUXYLkqrlYH+UDuJwmQ2R4RIqKpxU4RrKi+OpVtuM1NvVRXdDDlRshmOxNCMFnbF5H45eVpMr7ASW/lYT6QebTY/q+1wWvVk5ebneNkZnksQgnGDkvOXrOiqKzzBKZWXIG+Xoc5SJsZZra9er71fWbJTPVZawXFCrdRbDgsGVwc0YnmqJaAnUg85FKiEnCjZDOXayCmWKo2Znc8EPMR0B0qYePwqz2oG5QjlkNVeHzAJA+nAuwJil4ACzrVIsqJJZ2aVmVRNvIgAoBcSvIgA/mcWKoJbK8e63J7nvcvG08M2C1xOlGyGK4SuZQexuFtuvTuP+ee7AyWsaWQJTSNLuFE1gxvLCTSNLKE7UMK6AyVsOFZJ04nb67mFJVxxebNYZbY3nq/aa8cTaxYPsx3MvKUytqARR52ljPf7eyL1iHbYG0ddi6FiqsL/cB4LeYykzlLGghMEaC2Q4djdNcW3ZG9KI9MqPvK1Wb5Lz054qvKiuMjCw8fNluxW6ynx8enEbVT49lje/8DVCAB7943mRMlmuOby8rYbsQVy9mGX6Wf0yijDBRNxIVpz6y4AmnvzHc4Vvj36dZzwTdcIFd1KbjWni2WGcjNNziiHAb5uyr9InuA3jUQcDXKuJ/b94/MULso3tEDCrTPr3uWcrmXSWzOgl5z8fvxewEarlExW7P2hdjITp5S6XqjnRasUyQWA6YTJ/aRKyW7kRMlmeJI2pWs1y7mpDNkKAeM+KMAoXvpqk7CfyskNuZ64vNwCEZP41ZJteU20J1KfQqRYhYlZhfjeKSizfMdVmlvbWiyUE8lLSicS9ozhZYcQwTiJXVF5H5PZS/XOZ06UbIZnaZPZD1ozTeJVhmeEyhvIrGKmiGzYfadEC+RVupnpIP5K2a3ffSpBKGDRN1px+0x235lte4x6YMk5UbIZylioDMPS3Sq9eHGlCfB2X5SyhHL0WJApu7gqQqU8oYCxKgLUskgZShNqpv6qp045UbIZShOabk+TqlCaUCuo6u6AIjGU7lwxr5L+Sj31SfxH6/krYO9/7Bnhrq2HpiOB7l8DABy/dNlw/teud1PmfjTwp+k1vvv0kH7MdreYznGD6Kx0eZXhioXSnSvErdAKxy9dRm3VVhT78gAAJ/YW4PStFzixtwAdFyb1eaGjVTh96wWu34sDAJrryhCdfoyZp28YrNQMbHeL41bqOKGczMi1syljP83t1I9rq7ZiW0ECo7//AwDY8vpm+Lf7UOzLw4m9BQCARx2/4OLJfYhOP4a/4h0AwJFTo/j2yw9R7MvTSQaAz96eSLlfQ0uv46Q6Tui/F74mMab99n2bTmRzXRkA4Pq9OJrrynDk1CiOddQA0Agem3xmmMfnAsC+9zYDAP5+4cPY5DP8EHqof7a5rkyfx4n94IthAFq83nT0THYTarihhWD8fPkbXL8XR23VVgDAtoIEDp/+A8c6anRCRWs8cmoUAHDx5D79C+E4fOirlOuLIcdJQnOiZDPccfkyPygeTf8gFpa7HlgJIH8OJy3Uve2MZX7jieIK/ZDuX7MkYd33290CPJ229ZqZQIlKyWCdb23X/n8ykzyWIY49mXH24daIXAy1GTlCbYbjhG46eoatJkhuwWlBAnIWajtcIZRbqcFSnVZg6fpuWCfgosrzP4ZXTqzMn/yjhRTKFqxcV/wC3SATyLm87fBsw61c4zsBt6xSxH9/GISHZDIbTAAAAABJRU5ErkJggg==",
      "sprout_pot": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAB9UlEQVR4nO3boUsDURzA8W0aDIIgiKCI2WBeEfQfGGZh0SL+PWJZHJhFsAuWZYNZhmEIFg0Wncl0vzf2uO/tjvn9xAvvnl8ee77brT1tTVvidOqewLIxKMygMIPCDAozKMygsFVikO7+Vql/Zkcvb+0670/M4Y8rFGZQmEFhBoVlbUqpD//du36pSXR7xXFTm0Q0h7L3z53DLK5QmEFhBoUZFGZQWDv1FUhVu+m8XnvD8HoT5jBr93eFwgwKMyjMoLCso+f4/iO83rm+LTWJn4vT4pgZcyh7/9w5zOIKhRkUZlCYQWFZJ6WUKr5kizaJVivegIgv2Ki/1xUKMyjMoDCDwgwKSx49c3ZO4lWYMnwVZ4kZFGZQmEFhyKs4m2vrhWtn3b3CtZvROBz3/eszZxpz3T93DtHf5qs4DWBQmEFhBoUZFJZ8wBxJ7fKXxwfYhOpy9fBcuOYu3wAGhRkUZlAY8tPE6AP9v3KFwgwKMyjMoDCDwpBdfrh9RAxTq/7kERnHFQozKMygMIPCsp6HpkTPSc+/D0uPW4XBylN43VdxGsqgMIPCDAozKAw5ekZOdjaqGrqUwaTa8V2hMIPCDAozKAw5ekai4+iin5tGzzipI2aKKxRmUJhBYQaFGRRW2S4fWfRvQqve0SOuUJhBYQaFGRT2C4Kbfu9A836YAAAAAElFTkSuQmCC",
      "wide_planter": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAACrklEQVR4nO2aMU8UQRTHZxELjI1GSzksLSCxwKvBhlhIQmNjY4lfwM/gF5DSxsbGBAtjA9SEDgpLPC1JsDFYEDy7zcy727c7t/+7E/j9qt3M3DD88ubl7cwU/dAPoGNm2hO4aiBUDELFIFQMQsUgVAxCxSBUDELFIFQMQsUgVAxCxSBUDELFIFQMQsUgVAxCxSBUDELFIFQMQsUgVAxCxcxOewKWbud+5VWW/d5JMcm5jAIRKgahYhAqZmw51MuFHvvfv1aPuTDamCFMLv8SoWIQKgahYoo2N5i9PHnr3avG4+w9e1E+9799ruxXPHqevK98+dj4b5y9fl/ZpsyvRKgYhIrJWvJ2iccljrf84iU9jI2jD+Xzp8WXjfrV9c2ZU3dhLWlrkwKIUDEIFYNQMW4O9XJmCGmJs3r8J2lrWgqFkJZDNk/G2JyZM67Np7sP54b2C6FdTiVCxSBUzNjKpjsPzsvnuvJmXGVT3P/Xz5tJG2XTJQGhYhAqJqts8naQvM9LrxQKIc2FObtNOeNavM9SuzNF2TRFECoGoWKSHFp3Uvl3cz15P3izWT632WlvWrPanOnVljlzWn67lbTNbG1X/q4unxKhYhAqpnjSuVcu86edpaRxp3eYvNslf3vxbvnslU11h2m/j06Hjun1q+ubMyc7rl3ysRfrxEKEikGoGISKSS6LLc3fSBp3ev6P49yzEqrzpM1RTcds21c1p9hLnRMiVAxCxSBUjHvhdqAuNfVZXJfm5KRJ0XROXt2ZCxEqBqFi3CVvy6gQ/BRwWbFLfPD/bg4RKgahYhAqJsmhhz8ukkabSwbfH49pWv8X1osHESoGoWJm40OnbrCHdKN/MVwl4l16DukmDELFIFRM5qfn9aRulz6GCBWDUDEIFZN14fa6woXbKYJQMf8A/hPoev536O8AAAAASUVORK5CYII=",
    };
    // MSV2_ART_CATALOG_END

    // Dry/wet thresholds are in the oscillator sensor's raw units. The probe firmware
    // now inverts its raw frequency reading (higher = wetter, matching the field name -
    // see MOISTURE_INVERT_CEILING in MSV2_Moisture_BLE_Sleep.ino), so dry < wet again,
    // same ordering as the original pre-oscillator scale, just different absolute
    // numbers. Provisional until per-species soil calibration data replaces these.
    const presets = {
      custom: { label: "Custom", dry: null, wet: null },
      herb: { label: "Herbs", dry: 120, wet: 175 },
      tropical: { label: "Tropical", dry: 125, wet: 180 },
      succulent: { label: "Succulent", dry: 110, wet: 155 },
      fern: { label: "Fern", dry: 135, wet: 190 },
      dryHouseplant: { label: "Dry houseplant", dry: 115, wet: 170 }
    };
    let config = null;
    let readings = {};
    let wifiInfo = null;
    let claimInfo = null;
    let cloudInfo = null;
    let soundInfo = null;
    let shareToken = null;
    const MAX_PLANTS = 16;
    // History reads go straight from this browser to Supabase (see loadHistory below)
    // using a self-minted household share token, the same model web/plant-dashboard
    // already uses - not the receiver's own private cloudApiToken, which never leaves
    // the device.
    const SHARE_API_BASE = "https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/plant-dashboard";

    const $ = (id) => document.getElementById(id);
    // 0-100 is exactly the historical dry-to-wet range, same as it has always been - 0 at
    // the driest ever learned, 100 at the wettest. Unlike the old version, this does NOT
    // clamp below zero: a reading drier than anything on record keeps counting down as a
    // negative number instead of pinning at 0 forever, so "how much drier than ever before"
    // stays visible. Still clamped at 100 on top - "more than fully watered" isn't
    // meaningful. Mirrors moisturePercent() in MSV2_ESP32_Display_Runtime.ino exactly.
    const pct = (value, dry, wet) => {
      if (value == null || wet === dry) return null;
      return Math.min(100, Math.round((value - dry) * 100 / (wet - dry)));
    };
    const batteryFillClass = (p) => p == null ? "muted" : p < 25 ? "danger" : p < 50 ? "warn" : "";
    // 0 is the watering-line inflection point - literally the driest this sensor has ever
    // recorded (pct()'s own zero), not a padded-above-dry cue. 0-100 (the entire learned
    // dry-to-wet range) is normal, unchanged from how it has always displayed. Crossing
    // below 0 means "drier than ever observed", which is why color/alerts only escalate
    // there: MOISTURE_DANGER_PCT is the overdue boundary - red and actually flashing (see
    // .fill.danger's animation), not just a warning. MOISTURE_BAR_NEGATIVE_WIDTH_PCT is a
    // display-layout constant, not an alert threshold - see barPercent() below. Mirrors the
    // physical display's moistureHealthColor()/slotNeedsBlink()/drawBar() exactly - see
    // scripts/verify_moisture_alert_thresholds.mjs.
    const MOISTURE_WATER_LINE_PCT = 0;
    const MOISTURE_DANGER_PCT = -10;
    const MOISTURE_BAR_NEGATIVE_WIDTH_PCT = 30;
    const moistureFillClass = (p) => p == null ? "muted" : p <= MOISTURE_DANGER_PCT ? "danger" : p < MOISTURE_WATER_LINE_PCT ? "warn" : "";
    // The bar can't render negative width, so 0-100 (pct()'s unchanged dry-to-wet range)
    // occupies the right (100 - MOISTURE_BAR_NEGATIVE_WIDTH_PCT)% of the bar's width; the
    // left MOISTURE_BAR_NEGATIVE_WIDTH_PCT% is a fixed, finite strip for the open-ended
    // negative range to render into before the bar goes fully empty ("goes off the end of
    // the bar and can't be displayed anymore") - the number itself (pct()) stays unclamped.
    // Mirrors drawBar()'s barPct calculation in MSV2_ESP32_Display_Runtime.ino exactly.
    const barPercent = (p) => {
      if (p == null) return 0;
      const positiveSpan = 100 - MOISTURE_BAR_NEGATIVE_WIDTH_PCT;
      return Math.max(0, Math.min(100, MOISTURE_BAR_NEGATIVE_WIDTH_PCT + (p * positiveSpan) / 100));
    };
    const labelFor = (pairs, value) => (pairs.find((p) => p[0] === value) || [value, value])[1];
    const artSrc = (value) => artImages[value] || artImages.sprout_pot;
    const artPreview = (value, className = "") => `<img class="${className}" src="${artSrc(value)}" alt="">`;
    const bootTimeLabel = (ms) => ms ? `${Math.round(Number(ms) / 1000)}s after boot` : "never";

    function setTab(name) {
      $("statusView").classList.toggle("hidden", name !== "status");
      $("setupView").classList.toggle("hidden", name !== "setup");
      $("diagnosticsView").classList.toggle("hidden", name !== "diagnostics");
      $("historyView").classList.toggle("hidden", name !== "history");
      $("statusTab").classList.toggle("active", name === "status");
      $("setupTab").classList.toggle("active", name === "setup");
      $("diagnosticsTab").classList.toggle("active", name === "diagnostics");
      $("historyTab").classList.toggle("active", name === "history");
    }

    async function loadConfig() {
      const response = await fetch("/ui", { cache: "no-store" });
      config = await response.json();
      config.cards = Array.isArray(config.cards) ? config.cards : [];
      $("appTitle").textContent = config.title || "MSV2 Plants";
      $("titleInput").value = config.title || "";
      renderConfigMeta();
      renderEditors();
      renderStatus();
      renderDiagnostics();
    }

    async function loadReadings() {
      // Self-contained try/catch: this runs on a 3s setInterval with no outer catch, so
      // an unhandled rejection here (WiFi hiccup, receiver reboot) would otherwise leave
      // statusText frozen on the last successful "Updated HH:MM:SS" forever, looking
      // live when it silently isn't.
      try {
        const response = await fetch("/data", { cache: "no-store" });
        if (!response.ok) throw new Error("HTTP " + response.status);
        const data = await response.json();
        readings = {};
        (data.sensors || []).forEach((sensor) => readings[sensor.sensor] = sensor);
        $("statusText").textContent = "Updated " + new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit", second: "2-digit" });
        renderStatus();
        renderDiagnostics();
      } catch (error) {
        $("statusText").textContent = "Connection lost - retrying...";
      }
    }

    async function loadWifi() {
      const response = await fetch("/wifi", { cache: "no-store" });
      if (!response.ok) throw new Error("HTTP " + response.status);
      wifiInfo = await response.json();
      renderWifi();
    }

    function renderWifi() {
      if (!wifiInfo) return;
      const mode = wifiInfo.networkMode || "unknown";
      const ip = wifiInfo.ip || "--";
      const configured = wifiInfo.configuredSsid || "";
      const connected = wifiInfo.connectedSsid || "";
      const localOnly = Boolean(wifiInfo.localOnly);
      $("wifiSsidInput").value = configured || connected || "";
      $("wifiLocalOnlyInput").checked = localOnly;
      $("wifiStatusText").textContent = `${mode === "station" ? "home WiFi" : mode === "local-only" ? "local-only" : "setup WiFi"} / ${ip}${connected ? " / " + connected : ""}`;
      $("wifiHelpText").textContent = localOnly
        ? "Local-only mode is on. Your phone must join MSV2-Display to edit this receiver. Uncheck it and enter home WiFi anytime."
        : mode === "station"
          ? `Connected to home WiFi${connected ? ": " + connected : ""}. Leave this alone unless you want to move the receiver to a different network.`
          : "Choose your home WiFi below, or check local-only mode to use MSV2-Display without connecting to home WiFi.";
      updateWifiControls();
    }

    function updateWifiControls() {
      const localOnly = $("wifiLocalOnlyInput").checked;
      $("wifiSsidInput").disabled = localOnly;
      $("wifiPasswordInput").disabled = localOnly;
      $("wifiSaveButton").textContent = localOnly ? "Use Local Only" : "Save & Reboot";
      if (localOnly) {
        $("wifiPasswordInput").value = "";
      }
    }

    async function loadClaim() {
      const response = await fetch("/claim", { cache: "no-store" });
      if (!response.ok) throw new Error("HTTP " + response.status);
      claimInfo = await response.json();
      renderClaim();
    }

    function renderClaim() {
      if (!claimInfo) return;
      $("receiverIdText").textContent = claimInfo.receiverId || "--";
      $("claimCodeText").textContent = claimInfo.claimCode || "--";
      $("claimStatusText").textContent = `${claimInfo.networkMode || "local"} / ${claimInfo.ip || "--"}`;
      renderClaimQr();
      $("claimNotice").textContent = claimInfo.claimUrl
        ? "Scan the QR code with your phone camera to link this receiver."
        : "Receiver ID and claim code are ready.";
    }

    function renderClaimQr() {
      const url = claimInfo?.claimUrl || "";
      $("claimQrBox").classList.toggle("hidden", !url);
      if (!url) return;
      $("claimQrText").textContent = url;
      // Rendered fully client-side with the vendored qrcode-generator library (see the
      // <script> block above this one) instead of a third-party image service - the
      // pairing receiverId/claimCode never leaves the phone, and this still works when
      // the phone has no internet route at all (e.g. viewing this page over the
      // receiver's own local-only MSV2-Display AP).
      try {
        const qr = qrcode(0, "M");
        qr.addData(url);
        qr.make();
        $("claimQrImage").src = qr.createDataURL(6, 8);
      } catch (error) {
        console.error("Could not render pairing QR code", error);
      }
    }

    async function loadCloud() {
      const response = await fetch("/cloud", { cache: "no-store" });
      if (!response.ok) throw new Error("HTTP " + response.status);
      cloudInfo = await response.json();
      renderCloud();
    }

    function renderCloud() {
      if (!cloudInfo) return;
      const tokenText = cloudInfo.hasApiToken ? "token saved" : "no token";
      const upload = cloudInfo.upload || {};
      const configPull = cloudInfo.configPull || {};
      const bootstrap = cloudInfo.bootstrap || {};
      const httpText = upload.lastHttpStatus ? `HTTP ${upload.lastHttpStatus}` : "no upload yet";
      const pullText = configPull.lastHttpStatus ? `config HTTP ${configPull.lastHttpStatus}` : "no config pull yet";
      $("cloudStatusText").textContent = `${cloudInfo.enabled ? "auto sync on" : "auto sync pending"} / ${tokenText} / ${httpText} / ${pullText}`;
      const lastSuccess = `Last success: ${bootTimeLabel(upload.lastSuccessMs)}`;
      const lastAttempt = `last attempt: ${bootTimeLabel(upload.lastAttemptMs)}`;
      const error = upload.lastError ? ` / ${upload.lastError}` : "";
      const bootstrapText = bootstrap.lastHttpStatus ? `Bootstrap HTTP ${bootstrap.lastHttpStatus}` : "Bootstrap waiting for WiFi";
      const pullError = configPull.lastError ? ` / config: ${configPull.lastError}` : "";
      $("cloudNotice").textContent = cloudInfo.enabled
        ? `${lastSuccess}; ${lastAttempt}${error}${pullError}`
        : `${bootstrapText}. The receiver bootstraps cloud upload and config pull automatically after WiFi setup.`;
    }

    async function loadSound() {
      const response = await fetch("/sound", { cache: "no-store" });
      if (!response.ok) throw new Error("HTTP " + response.status);
      soundInfo = await response.json();
      renderSound();
    }

    function renderSound() {
      if (!soundInfo) return;
      $("soundEnabledInput").checked = Boolean(soundInfo.enabled);
    }

    // Saves immediately on toggle, same pattern as wifiLocalOnlyInput's checkbox - no
    // separate save button for a single on/off setting.
    async function saveSound() {
      const enabled = $("soundEnabledInput").checked;
      const response = await fetch("/sound", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled })
      });
      if (!response.ok) {
        $("soundEnabledInput").checked = !enabled;
        $("soundNotice").textContent = "Save failed: " + (await parseErrorText(response));
        return;
      }
      $("soundNotice").textContent = enabled ? "Alert sounds on." : "Alert sounds muted.";
    }

    function renderConfigMeta() {
      const meta = config?._meta || {};
      const version = meta.version == null ? "--" : meta.version;
      const source = meta.updatedBy || "unknown";
      $("configVersionText").textContent = `Config v${version} / ${source}`;
    }

    async function copyText(value, notice) {
      if (!value) return;
      try {
        if (navigator.clipboard && window.isSecureContext) {
          await navigator.clipboard.writeText(value);
        } else {
          const input = document.createElement("textarea");
          input.value = value;
          input.setAttribute("readonly", "");
          input.style.position = "fixed";
          input.style.left = "-9999px";
          input.style.top = "0";
          document.body.appendChild(input);
          input.focus();
          input.select();
          input.setSelectionRange(0, input.value.length);
          const copied = document.execCommand("copy");
          document.body.removeChild(input);
          if (!copied) throw new Error("copy command failed");
        }
        $("claimNotice").textContent = notice;
      } catch (error) {
        $("claimNotice").textContent = "Copy failed. Press and hold this value: " + value;
      }
    }

    // Shared by every save/mutate action's error path (saveWifi/forgetWifi/saveConfig)
    // so a non-OK response (most commonly the admin session's 401 JSON body, e.g.
    // {"error":"admin session expired - power-cycle to reconfigure"}) always renders as
    // its readable "error" field instead of the raw JSON text landing in the UI verbatim.
    async function parseErrorText(response) {
      const bodyText = await response.text();
      try {
        return JSON.parse(bodyText).error || bodyText;
      } catch (parseErr) {
        return bodyText; // not JSON - fall back to the raw response text
      }
    }

    async function saveWifi() {
      const ssid = $("wifiSsidInput").value.trim();
      const password = $("wifiPasswordInput").value;
      const localOnly = $("wifiLocalOnlyInput").checked;
      if (!localOnly && !ssid) {
        $("wifiNotice").textContent = "Network name is required.";
        return;
      }
      $("wifiNotice").textContent = localOnly ? "Saving local-only mode. Receiver will reboot..." : "Saving WiFi. Receiver will reboot...";
      const response = await fetch("/wifi", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ ssid, password, localOnly })
      });
      if (!response.ok) {
        $("wifiNotice").textContent = "Save failed: " + (await parseErrorText(response));
        return;
      }
      $("wifiPasswordInput").value = "";
      $("wifiNotice").textContent = localOnly
        ? "Saved. The receiver is rebooting and will keep using MSV2-Display for local editing."
        : "Saved. The receiver is rebooting. Reconnect your phone to home WiFi and watch the receiver screen for the plant dashboard.";
    }

    async function forgetWifi() {
      $("wifiNotice").textContent = "Forgetting WiFi. Receiver will reboot into setup mode...";
      const response = await fetch("/wifi", { method: "DELETE" });
      if (!response.ok) {
        $("wifiNotice").textContent = "Forget failed: " + (await parseErrorText(response));
        return;
      }
      $("wifiPasswordInput").value = "";
      $("wifiNotice").textContent = "Forgotten. After reboot, connect your phone to MSV2-Display to set up WiFi again.";
    }

    // Calendar-day phrasing for autoCal.lastWateredAtMs - informational only (see
    // receiver-snapshot's deriveAutoCal). The "overdue"/predicted-interval alerting this
    // used to feed was removed 2026-08-17 (see docs/STATUS_HANDOFF.md): the product's
    // whole point is moisture-based alerting, not a calendar prediction, so this is now
    // just a nice-to-have stat, never a color/danger signal.
    //
    // Deliberately compares calendar dates, not "was this within the last 24h" - watering
    // at 11pm and checking at 9am the next morning is only ~10 hours ago but is clearly
    // "yesterday", not "today". A rolling-hours calculation (what this replaced) gets that
    // wrong right up to the 24h mark regardless of where midnight actually fell. This can
    // only be done client-side: the server has no idea what timezone the viewer is in.
    function wateredAgoText(lastWateredAtMs) {
      if (lastWateredAtMs == null || lastWateredAtMs < 0) return null;
      const startOfDay = (ms) => {
        const d = new Date(ms);
        d.setHours(0, 0, 0, 0);
        return d.getTime();
      };
      const dayDiff = Math.round((startOfDay(Date.now()) - startOfDay(lastWateredAtMs)) / 86400000);
      if (dayDiff <= 0) return "watered today";
      if (dayDiff === 1) return "watered yesterday";
      return "watered " + dayDiff + "d ago";
    }

    function renderStatus() {
      if (!config) return;
      $("statusGrid").innerHTML = config.cards.map((card) => {
        const reading = readings[card.sensor] || {};
        const autoCal = reading.autoCal || null;
        const autoEnabled = Boolean(card.moisture?.auto);
        const battPct = reading.batteryPercent == null ? null : Number(reading.batteryPercent);

        // Not enough history yet (see msv2_read_sensor_auto_calibration's ready gate)?
        // Fall back to the manually-configured/preset range as a working estimate -
        // unlike the old stale-scale bug, this range is on the CURRENT sensing scale
        // (set by the user, or a preset, after today's calibration fixes), so it's a
        // reasonable number to show rather than nothing. Once ready, the learned range
        // takes over automatically.
        const autoReady = Boolean(autoEnabled && autoCal && autoCal.ready);
        const learning = autoEnabled && !autoReady;
        const dry = autoReady ? autoCal.dryBound : Number(card.moisture?.dry ?? card.dry ?? 120);
        const wet = autoReady ? autoCal.wetBound : Number(card.moisture?.wet ?? card.wet ?? 175);
        // Once calibrated, show percent instead of the raw sensor value as a visual
        // "calibration done" signal (mirrors the physical display - see
        // docs/STATUS_HANDOFF.md's redesign item 5). moistPct is already signed (pct() is
        // unclamped below zero) - metric()/barPercent() handle turning it into bar width.
        const moistPct = pct(reading.moisture, dry, wet);
        const moistValueText = reading.moisture == null ? "--" : (autoReady && moistPct != null ? moistPct + "%" : String(reading.moisture));
        const moistureMetric = metric(learning ? "Moisture (learning)" : "Moisture", moistValueText, moistPct, false, true);
        const wateredNote = autoReady ? wateredAgoText(autoCal.lastWateredAtMs) : null;
        const trendNote = autoReady && autoCal.dryBoundTrendingLower;

        return `
          <article class="card">
            <div class="card-head">
              <div>
                <div class="name">${escapeHtml(card.plant || card.label || ("Probe " + card.sensor))}</div>
                <div class="meta">Probe ${escapeHtml(card.sensor ?? "-")}${reading.rssi ? " / " + escapeHtml(reading.rssi) + " dBm" : ""}${wateredNote ? " · " + escapeHtml(wateredNote) : ""}</div>
              </div>
              <div class="art">${artPreview(card.art || "sprout_pot")}<span>${escapeHtml(labelFor(artOptions, card.art || "sprout_pot"))}</span></div>
            </div>
            ${moistureMetric}
            ${trendNote ? '<div class="notice warn">Dry range has been trending lower - may be worth a manual check.</div>' : ""}
            ${metric("Battery", battPct == null ? "--%" : battPct + "%", battPct)}
          </article>
        `;
      }).join("");
    }

    function metric(name, value, percent, forceDanger, isMoisture = false) {
      const fill = forceDanger ? "danger" : (isMoisture ? moistureFillClass(percent) : batteryFillClass(percent));
      // Moisture's percent is signed/unclamped-below-zero (see pct()'s comment) - the bar
      // itself can't render negative width, so route it through barPercent() first. Battery
      // has no such extension, so its already-0-100 percent is used directly.
      const width = percent == null ? 0 : (isMoisture ? barPercent(percent) : percent);
      // The marker sits at a fixed visual position (MOISTURE_BAR_NEGATIVE_WIDTH_PCT of the
      // bar's width) - the same position barPercent(0) computes - not at MOISTURE_WATER_LINE_PCT
      // itself, which is now the alert-threshold VALUE (0), not a bar position.
      const waterLine = isMoisture ? `<div class="water-line" style="left:${MOISTURE_BAR_NEGATIVE_WIDTH_PCT}%"></div>` : "";
      return `
        <div class="metric">
          <div class="metric-row"><span>${name}</span><strong>${escapeHtml(value)}</strong></div>
          <div class="bar"><div class="fill ${fill}" style="width:${width}%"></div>${waterLine}</div>
        </div>
      `;
    }

    function renderDiagnostics() {
      if (!config) return;
      const seen = new Set();
      const rows = [];
      config.cards.forEach((card) => {
        if (card.sensor) {
          seen.add(Number(card.sensor));
          rows.push(diagnosticCard(card, readings[card.sensor] || {}));
        }
      });
      Object.values(readings).forEach((reading) => {
        if (!seen.has(Number(reading.sensor))) {
          rows.push(diagnosticCard({ sensor: reading.sensor, plant: "Unassigned" }, reading));
        }
      });
      $("diagnosticsList").innerHTML = rows.length ? rows.join("") : `<div class="notice">No probe readings yet.</div>`;
    }

    function diagnosticCard(card, reading) {
      const rssi = Number(reading.rssi);
      const quality = signalQuality(rssi);
      const received = Number(reading.received ?? 0);
      const missed = Number(reading.missedTotal ?? 0);
      const expected = received + missed;
      const loss = expected > 0 ? (missed * 100 / expected).toFixed(2) + "%" : "--";
      return `
        <article class="diag-card">
          <div class="diag-head">
            <div class="diag-title">${escapeHtml(card.plant || ("Probe " + card.sensor))}</div>
            <span class="pill ${quality.className}">${quality.label}</span>
          </div>
          <div class="diag-grid">
            ${diagCell("Probe", card.sensor || "--")}
            ${diagCell("RSSI", Number.isFinite(rssi) ? rssi + " dBm" : "--")}
            ${diagCell("Age", reading.ageMs == null ? "--" : formatAge(reading.ageMs))}
            ${diagCell("Wake", reading.wake ?? "--")}
            ${diagCell("Received", reading.received ?? "--")}
            ${diagCell("Missed", reading.missedTotal ?? "--")}
            ${diagCell("Loss", loss)}
            ${diagCell("Battery", reading.batteryPercent == null ? "--" : reading.batteryPercent + "%")}
          </div>
        </article>
      `;
    }

    function diagCell(label, value) {
      return `<div class="diag-cell"><div class="diag-label">${label}</div><div class="diag-value">${escapeHtml(value)}</div></div>`;
    }

    function signalQuality(rssi) {
      if (!Number.isFinite(rssi)) return { label: "No Data", className: "" };
      if (rssi >= -75) return { label: "Strong", className: "good" };
      if (rssi >= -90) return { label: "Usable", className: "warn" };
      return { label: "Weak", className: "bad" };
    }

    function formatAge(ms) {
      const seconds = Math.max(0, Math.round(Number(ms) / 1000));
      if (seconds < 60) return seconds + "s";
      const minutes = Math.floor(seconds / 60);
      const rem = seconds % 60;
      return minutes + "m " + rem + "s";
    }

    function niceHistoryStep(rough) {
      const mag = Math.pow(10, Math.floor(Math.log10(rough)));
      const norm = rough / mag;
      const step = norm < 1.5 ? 1 : norm < 3 ? 2 : norm < 7 ? 5 : 10;
      return step * mag;
    }

    function formatHistoryValue(v, unit) {
      if (v == null) return "--";
      const rounded = Math.round(v * 10) / 10;
      return unit === "%" ? rounded.toFixed(0) + "%" : rounded.toLocaleString() + " " + unit;
    }

    const ONE_DAY_MS = 24 * 60 * 60 * 1000;

    // Time-only x-axis labels ("6:11 PM") are fine for the 6h/24h ranges but meaningless
    // once a range spans multiple days - the same time-of-day appears many times with no
    // way to tell which day it was. Add date context once the visible window is wide enough
    // that it matters, and drop to date-only once time-of-day precision isn't useful anymore.
    function formatHistoryTick(t, spanMs) {
      const date = new Date(t);
      if (spanMs > 10 * ONE_DAY_MS) {
        return date.toLocaleDateString([], { month: "numeric", day: "numeric" });
      }
      if (spanMs > 1.5 * ONE_DAY_MS) {
        return (
          date.toLocaleDateString([], { weekday: "short" }) +
          " " +
          date.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })
        );
      }
      return date.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
    }

    // Client-side, retroactive presentation smoothing (2026-08-23) ---------------------------
    // Ports the receiver firmware's robust centered moisture filter (smoothMoistureForDisplay(),
    // MSV2_ESP32_Display_Runtime.ino) and battery EMA
    // (calibrateRawBattery()) so the History chart recomputes "displayed" from the stored RAW
    // signal every time it renders, instead of only ever showing whatever the receiver's
    // filter baked in at ingest time. A parameter change therefore applies to the ENTIRE
    // already-stored history the moment it's changed - not just to new packets going forward -
    // with no refetch and no reflash. Defaults mirror the firmware constants; the chart remains
    // a window-local simulation because history does not contain pre-window/reboot filter state.
    const SMOOTHING_DEFAULTS = {
      centerWindowPackets: 5,
      moistureAlpha: 0.125,
      noiseWateringMult: 4,
      wateringSpanPct: 12,
      maxStepSpanPct: 5,
      confirmPackets: 2,
      batteryAlpha: 0.2,
    };
    const SMOOTHING_STORAGE_KEY = "msv2.historySmoothingParams.v2";
    const SMOOTHING_FIELD_IDS = {
      centerWindowPackets: "smCenterWindowPackets",
      moistureAlpha: "smMoistureAlpha",
      noiseWateringMult: "smNoiseWateringMult",
      wateringSpanPct: "smWateringSpanPct",
      maxStepSpanPct: "smMaxStepSpanPct",
      confirmPackets: "smConfirmPackets",
      batteryAlpha: "smBatteryAlpha",
    };

    function loadSmoothingParams() {
      try {
        const stored = JSON.parse(localStorage.getItem(SMOOTHING_STORAGE_KEY) || "{}");
        return Object.assign({}, SMOOTHING_DEFAULTS, stored);
      } catch (err) {
        return Object.assign({}, SMOOTHING_DEFAULTS);
      }
    }

    function saveSmoothingParams(params) {
      try { localStorage.setItem(SMOOTHING_STORAGE_KEY, JSON.stringify(params)); } catch (err) { /* private-browsing etc - fine to skip persisting */ }
    }

    function writeSmoothingFormValues(params) {
      for (const key of Object.keys(SMOOTHING_FIELD_IDS)) {
        $(SMOOTHING_FIELD_IDS[key]).value = params[key];
      }
    }

    function readSmoothingFormValues() {
      const bounds = {
        centerWindowPackets: [3, 31, true], moistureAlpha: [0.01, 1, false],
        noiseWateringMult: [0, 100, false],
        wateringSpanPct: [0, 100, false], maxStepSpanPct: [0.1, 100, false],
        confirmPackets: [1, 255, true], batteryAlpha: [0.01, 1, false],
      };
      const params = {};
      for (const key of Object.keys(SMOOTHING_FIELD_IDS)) {
        // An emptied field (Number("") === 0, which IS finite) must fall back to the
        // documented default, not silently become 0 and then get clamped to whatever
        // this field's minimum happens to be - that's not the same value and there's no
        // indication to the user that a blank box became the minimum instead of the default.
        const text = $(SMOOTHING_FIELD_IDS[key]).value.trim();
        const raw = text === "" ? Number.NaN : Number(text);
        const [minimum, maximum, integer] = bounds[key];
        let value = Number.isFinite(raw) ? raw : SMOOTHING_DEFAULTS[key];
        value = Math.min(maximum, Math.max(minimum, value));
        params[key] = integer ? Math.round(value) : value;
        if (key === "centerWindowPackets" && params[key] % 2 === 0) {
          params[key] += params[key] < maximum ? 1 : -1;
        }
      }
      return params;
    }

    function moisturePresentationSpan(measured, learnedSpan) {
      if (Number.isFinite(learnedSpan) && learnedSpan > 0) {
        return Math.min(65535, Math.floor(learnedSpan));
      }
      return Math.max(20, Math.floor(measured / 5));
    }

    function medianOfValues(values) {
      const sorted = values.slice().sort((a, b) => a - b);
      return sorted[Math.floor(sorted.length / 2)];
    }

    // Browser port of smoothMoistureForDisplay() in MSV2_ESP32_Display_Runtime.ino.
    // The robust median center rejects isolated spikes before a symmetric low-pass tracks
    // it in both directions. The old drying ratchet accepted low excursions while ignoring
    // equal high excursions, which inevitably pulled a stationary signal below its centroid
    // and later misclassified the accumulated gap as watering.
    function smoothMoistureSeries(rawPoints, p) {
      const out = [];
      const centerWindow = [];
      const windowPackets = Math.max(3, Math.round(p.centerWindowPackets) | 1);
      let displayed = null, filteredQ8 = null, wateringStreak = 0;

      for (const point of rawPoints) {
        const measured = point.raw;
        if (measured == null) continue;

        centerWindow.push(measured);
        if (centerWindow.length > windowPackets) centerWindow.shift();

        if (displayed === null) {
          displayed = measured;
          filteredQ8 = measured * 256;
          out.push({ t: point.t, v: displayed, raw: measured });
          continue;
        }

        // Do not let a two-sample pseudo-center move the startup value. The first full
        // window is the earliest point with the promised two-outlier rejection.
        if (centerWindow.length < windowPackets) {
          out.push({ t: point.t, v: displayed, raw: measured });
          continue;
        }

        const center = medianOfValues(centerWindow);
        const robustNoise = Math.max(1, medianOfValues(centerWindow.map((value) => Math.abs(value - center))));
        const span = moisturePresentationSpan(center, p.learnedSpan);
        let wateringThreshold = Math.max(4, Math.ceil((span * p.wateringSpanPct) / 100));
        wateringThreshold = Math.max(wateringThreshold, Math.floor(displayed / 50));
        wateringThreshold = Math.max(wateringThreshold, robustNoise * p.noiseWateringMult);

        const wateringRise = center > displayed && center - displayed >= wateringThreshold;

        if (wateringRise) {
          wateringStreak++;
          if (wateringStreak >= Math.max(1, p.confirmPackets)) {
            displayed = center;
            filteredQ8 = center * 256;
            wateringStreak = 0;
          }
        } else {
          wateringStreak = 0;
          const targetQ8 = center * 256;
          let stepQ8 = Math.trunc((targetQ8 - filteredQ8) * p.moistureAlpha);
          const maxStepQ8 = Math.max(1, Math.floor((span * p.maxStepSpanPct) / 100)) * 256;
          stepQ8 = Math.max(-maxStepQ8, Math.min(maxStepQ8, stepQ8));
          filteredQ8 += stepQ8;
          displayed = Math.floor((filteredQ8 + 128) / 256);
        }

        out.push({ t: point.t, v: displayed, raw: measured });
      }

      return out;
    }

    // Battery voltage -> percent curve, ported verbatim from BATTERY_CURVE/batteryMvToPercent()
    // in MSV2_ESP32_Display_Runtime.ino. Percentage uses measured voltage directly; charging
    // remains visible as a diagnostic but does not apply a cross-probe voltage offset.
    const BATTERY_CURVE = [
      { mv: 4200, pct: 100 }, { mv: 4150, pct: 95 }, { mv: 4110, pct: 90 }, { mv: 4080, pct: 85 }, { mv: 4020, pct: 80 },
      { mv: 3980, pct: 75 }, { mv: 3950, pct: 70 }, { mv: 3910, pct: 65 }, { mv: 3870, pct: 60 }, { mv: 3850, pct: 55 },
      { mv: 3840, pct: 50 }, { mv: 3820, pct: 45 }, { mv: 3800, pct: 40 }, { mv: 3790, pct: 35 }, { mv: 3770, pct: 30 },
      { mv: 3750, pct: 25 }, { mv: 3730, pct: 20 }, { mv: 3710, pct: 15 }, { mv: 3690, pct: 10 }, { mv: 3610, pct: 5 },
      { mv: 3270, pct: 0 },
    ];
    function interpolateBatteryPercent(mv) {
      if (mv >= BATTERY_CURVE[0].mv) return BATTERY_CURVE[0].pct;
      const last = BATTERY_CURVE[BATTERY_CURVE.length - 1];
      if (mv <= last.mv) return last.pct;
      for (let i = 0; i < BATTERY_CURVE.length - 1; i++) {
        const a = BATTERY_CURVE[i], b = BATTERY_CURVE[i + 1];
        if (mv <= a.mv && mv >= b.mv) {
          const frac = (mv - b.mv) / (a.mv - b.mv);
          return b.pct + frac * (a.pct - b.pct);
        }
      }
      return 0;
    }

    function batteryMvToPercent(mv) {
      // Firmware's version returns uint8_t - it rounds to a whole percent HERE, before
      // that value ever reaches the EMA in smoothBatterySeries(). Rounding only at final
      // display time instead would blend a continuous float trajectory the receiver never
      // actually computed.
      return Math.round(interpolateBatteryPercent(mv));
    }

    // Faithful port of calibrateRawBattery() in MSV2_ESP32_Display_Runtime.ino. Rows that predate raw-battery history (before
    // migration 032) or come from a probe still on the legacy no-raw packet format have no
    // rawBatteryMv/rawCharging - those pass straight through as whatever the receiver's own
    // ingest-time filter already computed, with no raw overlay possible, and reset the filter
    // state so the next raw-carrying row cold-starts instead of blending across the gap.
    function smoothBatterySeries(rows, p) {
      const out = [];
      let smoothed = null;

      for (const row of rows) {
        if (row.rawBatteryMv == null || row.rawCharging == null) {
          if (row.batteryPercent != null) {
            out.push({ t: row.t, v: row.batteryPercent });
          }
          smoothed = null;
          continue;
        }

        const rawPct = batteryMvToPercent(row.rawBatteryMv);
        if (smoothed === null) {
          smoothed = rawPct;
        } else {
          smoothed += p.batteryAlpha * (rawPct - smoothed);
        }

        out.push({
          t: row.t,
          v: Math.round(smoothed),
          raw: rawPct,
        });
      }

      return out;
    }

    // Rows from the most recent loadHistory() fetch, cached so a Smoothing-panel parameter
    // change can recompute both charts instantly with no refetch.
    let lastHistoryRows = [];

    // FDC2214 DATA is proportional to 1/sqrt(L*C)/fref. Connected and bare readings in one
    // packet use the same FDC, reference clock, inductor and close-in-time thermal state, so
    // (Dbare/Dconnected)^2 estimates Cconnected/Cbare. Subtracting one reports the connected
    // electrode/soil contribution relative to the bare tank. Keeping this dimensionless avoids
    // a window-dependent baseline and preserves the exact sign: larger means more capacitance
    // and therefore wetter. This is reference normalization, not calibrated soil-water percent.
    function msv3ReferenceExcessPercent(row) {
      if (!row) return null;
      const connected = Number(row.raw_fdc_counts);
      const bare = Number(row.bare_tank_fdc_counts);
      const maxFdcCount = 0x0FFFFFFF;
      if (!Number.isInteger(connected) || connected <= 0 || connected > maxFdcCount) return null;
      if (!Number.isInteger(bare) || bare <= 0 || bare > maxFdcCount) return null;
      const ratio = bare / connected;
      const excessPercent = (ratio * ratio - 1) * 100;
      return Number.isFinite(excessPercent) ? excessPercent : null;
    }

    function renderHistoryFromCache() {
      // Deliberately does NOT call writeSmoothingFormValues() here - this runs on every
      // "input" event (every keystroke), and writing the clamped value back into the field
      // mid-edit fights the user typing a decimal (e.g. "0.5": the "0." keystroke parses as
      // 0, clamps, and would get rewritten to "0.01" before "5" can ever be typed). The
      // field snaps to its clamped, canonical value on blur instead - see the "change"
      // listener below.
      const params = readSmoothingFormValues();
      saveSmoothingParams(params);

      const selectedReading = readings[$("historySensorSelect").value] || {};
      const learned = selectedReading.autoCal;
      const learnedSpan = learned && learned.ready && Number(learned.wetBound) > Number(learned.dryBound)
        ? Number(learned.wetBound) - Number(learned.dryBound)
        : null;
      const moistureParams = Object.assign({}, params, { learnedSpan });

      const moistureRaw = lastHistoryRows
        .map((r) => ({ t: new Date(r.received_at).getTime(), raw: r.moisture }))
        .filter((point) => point.raw != null);
      const moisturePoints = smoothMoistureSeries(moistureRaw, moistureParams);

      const batteryRows = lastHistoryRows.map((r) => ({
        t: new Date(r.received_at).getTime(),
        rawBatteryMv: r.raw_battery_mv,
        rawCharging: r.raw_charging,
        batteryPercent: r.battery_percent,
      }));
      const batteryPoints = smoothBatterySeries(batteryRows, params);

      renderHistoryChart("historyMoistureChart", "historyMoistureTooltip", "historyMoistureLatest", moisturePoints, "#3987e5", "");
      renderHistoryChart("historyBatteryChart", "historyBatteryTooltip", "historyBatteryLatest", batteryPoints, "#199e70", "%");

      // board_temp_quarter_c is nullable (MSV3-only, quarter-degree C) - only show the card
      // once the selected probe actually has at least one reading, so every MSV2 probe and
      // any MSV3 probe still on pre-diagnostics firmware doesn't get a permanent empty chart.
      const tempPoints = lastHistoryRows
        .filter((r) => r.board_temp_quarter_c != null)
        .map((r) => ({ t: new Date(r.received_at).getTime(), v: r.board_temp_quarter_c / 4 }));
      $("historyTempSection").classList.toggle("hidden", tempPoints.length === 0);
      if (tempPoints.length) {
        renderHistoryChart("historyTempChart", "historyTempTooltip", "historyTempLatest", tempPoints, "#e5723f", "°C");
      }

      const rawFdcPoints = lastHistoryRows
        .filter((r) => r.raw_fdc_counts != null)
        .map((r) => ({ t: new Date(r.received_at).getTime(), v: r.raw_fdc_counts }));
      $("historyRawFdcSection").classList.toggle("hidden", rawFdcPoints.length === 0);
      if (rawFdcPoints.length) {
        renderHistoryChart("historyRawFdcChart", "historyRawFdcTooltip", "historyRawFdcLatest", rawFdcPoints, "#9b6bd6", "");
      }

      const bareTankPoints = lastHistoryRows
        .filter((r) => r.bare_tank_fdc_counts != null)
        .map((r) => ({ t: new Date(r.received_at).getTime(), v: r.bare_tank_fdc_counts }));
      $("historyBareTankSection").classList.toggle("hidden", bareTankPoints.length === 0);
      if (bareTankPoints.length) {
        renderHistoryChart("historyBareTankChart", "historyBareTankTooltip", "historyBareTankLatest", bareTankPoints, "#3fb8c9", "");
      }

      // Compute before any chart smoothing/downsampling from the two fields in each retained
      // history row. The server's sampled-history path keeps whole rows, so pairing survives.
      const fdcReferencePoints = lastHistoryRows
        .map((r) => ({
          t: new Date(r.received_at).getTime(),
          v: msv3ReferenceExcessPercent(r),
        }))
        .filter((point) => point.v != null);
      $("historyFdcReferenceSection").classList.toggle("hidden", fdcReferencePoints.length === 0);
      if (fdcReferencePoints.length) {
        renderHistoryChart(
          "historyFdcReferenceChart",
          "historyFdcReferenceTooltip",
          "historyFdcReferenceLatest",
          fdcReferencePoints,
          "#d5a63c",
          "% capacitance above bare",
        );
      }
    }

    function renderHistoryChart(svgId, tooltipId, latestId, points, color, unit) {
      const svg = $(svgId);
      const tooltip = $(tooltipId);
      const latestEl = $(latestId);
      svg.innerHTML = "";

      if (!points.length) {
        svg.setAttribute("viewBox", "0 0 800 60");
        const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
        t.setAttribute("x", 12);
        t.setAttribute("y", 32);
        t.setAttribute("class", "chart-axis");
        t.textContent = "No data yet.";
        svg.appendChild(t);
        latestEl.textContent = "";
        return;
      }

      const width = 800, height = 200, padL = 44, padR = 10, padT = 10, padB = 26;
      const plotW = width - padL - padR, plotH = height - padT - padB;
      const tMin = points[0].t, tMax = points[points.length - 1].t, tSpan = Math.max(1, tMax - tMin);

      // Diagnostic raw overlay (2026-08-23): points optionally carry `.raw`, the unsmoothed
      // measured value alongside the displayed one - same timestamps, built from the same
      // rows, so it always lines up point-for-point with the primary series with no separate
      // lookup needed. Undefined/null on every point (e.g. the battery chart) is a no-op.
      const hasRaw = points.some((p) => p.raw != null);
      const allValues = hasRaw ? points.flatMap((p) => [p.v, p.raw]).filter((v) => v != null) : points.map((p) => p.v);
      let vMin = Math.min(...allValues);
      let vMax = Math.max(...allValues);
      if (vMin === vMax) { vMin -= 1; vMax += 1; }
      const pad = (vMax - vMin) * 0.1;
      vMin -= pad;
      vMax += pad;
      if (unit === "%") { vMin = Math.max(0, vMin); vMax = Math.min(100, Math.max(vMax, vMin + 1)); }

      const x = (t) => padL + ((t - tMin) / tSpan) * plotW;
      const y = (v) => padT + (1 - (v - vMin) / (vMax - vMin)) * plotH;

      svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
      const ns = "http://www.w3.org/2000/svg";

      const yStep = niceHistoryStep((vMax - vMin) / 4);
      for (let v = Math.ceil(vMin / yStep) * yStep; v <= vMax; v += yStep) {
        const gy = y(v);
        const line = document.createElementNS(ns, "line");
        line.setAttribute("x1", padL);
        line.setAttribute("x2", width - padR);
        line.setAttribute("y1", gy);
        line.setAttribute("y2", gy);
        line.setAttribute("class", "chart-grid");
        svg.appendChild(line);

        const label = document.createElementNS(ns, "text");
        label.setAttribute("x", padL - 6);
        label.setAttribute("y", gy + 3);
        label.setAttribute("text-anchor", "end");
        label.setAttribute("class", "chart-axis");
        label.textContent = Math.round(v).toLocaleString();
        svg.appendChild(label);
      }

      const tickCount = 4;
      for (let i = 0; i < tickCount; i++) {
        const t = tMin + (tSpan * i) / (tickCount - 1);
        const gx = x(t);
        const label = document.createElementNS(ns, "text");
        label.setAttribute("x", gx);
        label.setAttribute("y", height - 6);
        label.setAttribute("text-anchor", i === 0 ? "start" : i === tickCount - 1 ? "end" : "middle");
        label.setAttribute("class", "chart-axis");
        label.textContent = formatHistoryTick(t, tSpan);
        svg.appendChild(label);
      }

      const RAW_COLOR = "#f5a742";
      if (hasRaw) {
        // Drawn before the primary path so the (usually noisier) raw line sits visually
        // behind the smoothed one where they overlap, rather than obscuring it.
        const rawPoints = points.filter((p) => p.raw != null);
        const rawPath = document.createElementNS(ns, "path");
        rawPath.setAttribute("d", rawPoints.map((p, i) => `${i === 0 ? "M" : "L"}${x(p.t).toFixed(1)},${y(p.raw).toFixed(1)}`).join(" "));
        rawPath.setAttribute("fill", "none");
        rawPath.setAttribute("stroke", RAW_COLOR);
        rawPath.setAttribute("stroke-width", "1.5");
        rawPath.setAttribute("stroke-dasharray", "3,3");
        rawPath.setAttribute("stroke-linejoin", "round");
        rawPath.setAttribute("stroke-linecap", "round");
        rawPath.setAttribute("opacity", "0.85");
        svg.appendChild(rawPath);

        const legend = document.createElementNS(ns, "text");
        legend.setAttribute("x", width - padR);
        legend.setAttribute("y", padT + 10);
        legend.setAttribute("text-anchor", "end");
        legend.setAttribute("class", "chart-axis");
        legend.textContent = "— displayed   ┄ raw (unsmoothed)";
        svg.appendChild(legend);
      }

      const path = document.createElementNS(ns, "path");
      path.setAttribute("d", points.map((p, i) => `${i === 0 ? "M" : "L"}${x(p.t).toFixed(1)},${y(p.v).toFixed(1)}`).join(" "));
      path.setAttribute("fill", "none");
      path.setAttribute("stroke", color);
      path.setAttribute("stroke-width", "2");
      path.setAttribute("stroke-linejoin", "round");
      path.setAttribute("stroke-linecap", "round");
      svg.appendChild(path);

      const last = points[points.length - 1];
      const endDot = document.createElementNS(ns, "circle");
      endDot.setAttribute("cx", x(last.t));
      endDot.setAttribute("cy", y(last.v));
      endDot.setAttribute("r", 5);
      endDot.setAttribute("fill", color);
      endDot.setAttribute("stroke", "#17212b");
      endDot.setAttribute("stroke-width", 2);
      svg.appendChild(endDot);

      if (hasRaw && last.raw != null) {
        const rawEndDot = document.createElementNS(ns, "circle");
        rawEndDot.setAttribute("cx", x(last.t));
        rawEndDot.setAttribute("cy", y(last.raw));
        rawEndDot.setAttribute("r", 3.5);
        rawEndDot.setAttribute("fill", RAW_COLOR);
        rawEndDot.setAttribute("stroke", "#17212b");
        rawEndDot.setAttribute("stroke-width", 1.5);
        svg.appendChild(rawEndDot);
      }

      const crosshair = document.createElementNS(ns, "line");
      crosshair.setAttribute("y1", padT);
      crosshair.setAttribute("y2", height - padB);
      crosshair.setAttribute("class", "chart-crosshair");
      svg.appendChild(crosshair);

      const hoverDot = document.createElementNS(ns, "circle");
      hoverDot.setAttribute("r", 4);
      hoverDot.setAttribute("fill", color);
      hoverDot.setAttribute("stroke", "#17212b");
      hoverDot.setAttribute("stroke-width", 2);
      hoverDot.setAttribute("opacity", 0);
      svg.appendChild(hoverDot);

      const hitRect = document.createElementNS(ns, "rect");
      hitRect.setAttribute("x", padL);
      hitRect.setAttribute("y", padT);
      hitRect.setAttribute("width", plotW);
      hitRect.setAttribute("height", plotH);
      hitRect.setAttribute("fill", "transparent");
      svg.appendChild(hitRect);

      function nearest(clientX) {
        const rect = svg.getBoundingClientRect();
        const svgX = ((clientX - rect.left) / rect.width) * width;
        const t = tMin + ((svgX - padL) / plotW) * tSpan;
        let lo = 0, hi = points.length - 1;
        while (lo < hi) {
          const mid = (lo + hi) >> 1;
          if (points[mid].t < t) lo = mid + 1; else hi = mid;
        }
        if (lo > 0 && Math.abs(points[lo - 1].t - t) < Math.abs(points[lo].t - t)) lo -= 1;
        return points[lo];
      }

      hitRect.addEventListener("pointermove", (evt) => {
        const p = nearest(evt.clientX);
        crosshair.setAttribute("x1", x(p.t));
        crosshair.setAttribute("x2", x(p.t));
        crosshair.setAttribute("opacity", 1);
        hoverDot.setAttribute("cx", x(p.t));
        hoverDot.setAttribute("cy", y(p.v));
        hoverDot.setAttribute("opacity", 1);
        const wrapRect = svg.parentElement.getBoundingClientRect();
        tooltip.style.left = (evt.clientX - wrapRect.left) + "px";
        tooltip.style.top = (evt.clientY - wrapRect.top) + "px";
        tooltip.style.opacity = 1;
        tooltip.querySelector(".tt-value").textContent = hasRaw && p.raw != null
          ? `${formatHistoryValue(p.v, unit)} (raw ${formatHistoryValue(p.raw, unit)})`
          : formatHistoryValue(p.v, unit);
        tooltip.querySelector(".tt-time").textContent = new Date(p.t).toLocaleString();
      });
      hitRect.addEventListener("pointerleave", () => {
        crosshair.setAttribute("opacity", 0);
        hoverDot.setAttribute("opacity", 0);
        tooltip.style.opacity = 0;
      });

      const latestValueText = hasRaw && last.raw != null
        ? `${formatHistoryValue(last.v, unit)} (raw ${formatHistoryValue(last.raw, unit)})`
        : formatHistoryValue(last.v, unit);
      latestEl.textContent = `latest ${latestValueText} · ${new Date(last.t).toLocaleString()}`;
    }

    function populateHistorySensorSelect() {
      const select = $("historySensorSelect");
      const seen = new Set();
      const options = [];
      (config?.cards || []).forEach((card) => {
        if (card.sensor && !seen.has(Number(card.sensor))) {
          seen.add(Number(card.sensor));
          options.push([card.sensor, card.plant || ("Probe " + card.sensor)]);
        }
      });
      Object.values(readings).forEach((r) => {
        if (r.sensor && !seen.has(Number(r.sensor))) {
          seen.add(Number(r.sensor));
          options.push([r.sensor, "Probe " + r.sensor]);
        }
      });
      const previous = select.value;
      select.innerHTML = options.map(([id, label]) => `<option value="${id}">${escapeHtml(label)}</option>`).join("");
      if (previous && options.some(([id]) => String(id) === previous)) {
        select.value = previous;
      }
    }

    // The edge function probes the selected window's row count before downloading its
    // payload and returns at most 2400 evenly spaced readings. Sparse windows keep every
    // row; dense windows (such as Alocasia GP's >100k-row week) return one row out of every
    // N. 2400 is three points per horizontal pixel on this 800px chart, so transmitting
    // more would add load time without visible chart detail. Keep in sync with
    // plant-dashboard/index.ts's HISTORY_POINT_BUDGET.
    const HISTORY_BATCH_ROWS = 2400;
    // Circuit breaker on browser round trips, not a "how much data should exist"
    // assumption - the real stop condition is the server's `hasMore` flag below, which
    // reflects whether its own internal pagination actually ran out of rows (a previous
    // version inferred this from page size instead and broke as soon as the platform's
    // per-call row cap didn't match this constant - see git history on this comment).
    const HISTORY_MAX_BATCHES = 10;

    function shareTokenStorageKey() {
      return "msv2.shareToken." + ((claimInfo && claimInfo.receiverId) || "");
    }

    // Self-mints a household share token from this receiver's own claim info (already
    // fetched locally via /claim, no cloud round-trip needed for that part) and caches
    // it, mirroring the same share-token model web/plant-dashboard uses. Pass
    // forceRefresh after a 401 to discard a revoked/stale cached token and mint a fresh one.
    async function getOrMintShareToken(forceRefresh = false) {
      if (!forceRefresh && shareToken) return shareToken;

      const storageKey = shareTokenStorageKey();
      if (!forceRefresh) {
        const cached = localStorage.getItem(storageKey);
        if (cached) {
          shareToken = cached;
          return shareToken;
        }
      }

      const response = await fetch(SHARE_API_BASE + "/api/share-claim", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ receiverId: claimInfo.receiverId, claimCode: claimInfo.claimCode }),
      });
      const body = await response.json();
      if (!response.ok || !body.ok || !body.share?.share_token) {
        throw new Error(body.error || ("HTTP " + response.status));
      }

      shareToken = body.share.share_token;
      localStorage.setItem(storageKey, shareToken);
      return shareToken;
    }

    async function fetchHistoryPage(token, sensor, hours, beforeId, untilMs) {
      let query = `receiverId=${encodeURIComponent(claimInfo.receiverId)}&sensorId=${encodeURIComponent(sensor)}` +
        `&sinceHours=${encodeURIComponent(hours)}&beforeId=${beforeId}&limit=${HISTORY_BATCH_ROWS}`;
      // Extra, optional upper bound alongside sinceHours - lets a custom range pin an exact
      // historical window (e.g. 2026-10-29 through 2026-10-31) instead of always reading
      // through whatever's newest right now. Omitted entirely for the preset-range case.
      if (untilMs) query += `&untilMs=${encodeURIComponent(untilMs)}`;
      return fetch(`${SHARE_API_BASE}/api/share-history?${query}`, {
        cache: "no-store",
        headers: { "Authorization": "Bearer " + token },
      });
    }

    // Fetches straight from this browser to Supabase - no longer proxied through the
    // receiver's own blocking HTTPClient, which shared the ESP32's single loop with
    // display rendering and froze the physical display for the length of the whole
    // paginated fetch (a real, documented incident - see docs/STATUS_HANDOFF.md).
    //
    // historyRequestId guards against a stale response clobbering a fresh one: the 30s
    // auto-refresh (see init() below) and a tab-switch-triggered call can overlap, and
    // pagination time varies, so an older call can resolve AFTER a newer one and would
    // otherwise blindly overwrite the chart it just rendered with out-of-date rows. Each
    // call captures its own id and checks it's still current before touching the DOM;
    // a superseded call just quietly stops.
    let historyRequestId = 0;
    // Set for the duration of any in-flight loadHistory() call (any caller - tab open,
    // sensor/range change, or the 30s auto-refresh below), cleared only by whichever call
    // is still current when it finishes. A wide range (14-30 days) can take many sequential
    // Supabase pages to walk and may run past 30s; without this flag the auto-refresh timer
    // would fire mid-load, bump historyRequestId, and restart pagination from scratch every
    // tick - never letting a wide-range load finish (it looked like the view kept resetting
    // and reloading forever).
    let historyLoadInFlight = false;

    async function loadHistory() {
      const requestId = ++historyRequestId;
      const isCurrent = () => requestId === historyRequestId;
      historyLoadInFlight = true;
      try {
        await loadHistoryInner(isCurrent);
      } finally {
        if (isCurrent()) historyLoadInFlight = false;
      }
    }

    async function loadHistoryInner(isCurrent) {
      populateHistorySensorSelect();
      const sensor = $("historySensorSelect").value;
      if (!sensor) {
        if (isCurrent()) $("historyNotice").textContent = "No probes seen yet.";
        return;
      }

      // Custom range is extra, alongside the hour presets, not a replacement - the API still
      // only understands "sinceHours back from now" plus an optional untilMs upper bound, so
      // a custom start date gets converted into however many hours back from *now* it is.
      let hours, untilMs;
      if ($("historyRangeSelect").value === "custom") {
        const startVal = $("historyCustomStart").value;
        const endVal = $("historyCustomEnd").value;
        if (!startVal || !endVal) {
          if (isCurrent()) $("historyNotice").textContent = "Pick both a start and end date, then Apply.";
          return;
        }
        const startMs = new Date(startVal + "T00:00:00").getTime();
        // End date is inclusive through the end of that day, not just midnight at its start.
        const endMs = new Date(endVal + "T23:59:59.999").getTime();
        if (!Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs < startMs) {
          if (isCurrent()) $("historyNotice").textContent = "End date must be on or after the start date.";
          return;
        }
        hours = Math.max(1, Math.ceil((Date.now() - startMs) / 3600_000));
        untilMs = endMs;
      } else {
        hours = $("historyRangeSelect").value;
        untilMs = null;
      }
      if (isCurrent()) $("historyNotice").textContent = "Loading...";

      // Batches walk backward from the most recent row (beforeId=0 means "start at the
      // newest"), not forward from the start of the window - a dense window can hold
      // far more rows than one batch, and paginating forward from the oldest end meant
      // the chart silently stalled short of "now" instead of ever reaching it, worse the
      // wider the selected range. Each batch comes back newest-first; the whole assembled
      // list is reversed once at the end for chronological rendering.
      const pages = [];
      let beforeId = 0;
      let truncated = false;
      let totalReadings = null;
      let sampleEvery = 1;
      try {
        let token = await getOrMintShareToken();

        for (let page = 0; page < HISTORY_MAX_BATCHES; page++) {
          let response = await fetchHistoryPage(token, sensor, hours, beforeId, untilMs);
          if (response.status === 401) {
            token = await getOrMintShareToken(true);
            response = await fetchHistoryPage(token, sensor, hours, beforeId, untilMs);
          }

          const body = await response.json();
          if (!isCurrent()) return;
          if (!response.ok || !body.ok) {
            $("historyNotice").textContent = body.error || ("HTTP " + response.status);
            renderHistoryChart("historyMoistureChart", "historyMoistureTooltip", "historyMoistureLatest", [], "#3987e5", "");
            renderHistoryChart("historyBatteryChart", "historyBatteryTooltip", "historyBatteryLatest", [], "#199e70", "%");
            $("historyTempSection").classList.add("hidden");
            $("historyRawFdcSection").classList.add("hidden");
            $("historyBareTankSection").classList.add("hidden");
            $("historyFdcReferenceSection").classList.add("hidden");
            return;
          }

          const page_rows = body.readings || [];
          pages.push(...page_rows);
          const bodyTotalReadings = Number(body.totalReadings);
          const bodySampleEvery = Number(body.sampleEvery);
          if (Number.isFinite(bodyTotalReadings) && bodyTotalReadings >= 0) {
            totalReadings = Math.floor(bodyTotalReadings);
          }
          if (Number.isFinite(bodySampleEvery) && bodySampleEvery >= 1) {
            sampleEvery = Math.floor(bodySampleEvery);
          }
          if (!body.hasMore) break;

          // A hasMore response without a cursor row cannot make forward progress. Treat it
          // as a partial result instead of throwing while dereferencing the empty array or
          // spinning until the circuit breaker.
          if (!page_rows.length) {
            truncated = true;
            break;
          }

          beforeId = page_rows[page_rows.length - 1].id;
          $("historyNotice").textContent = sampleEvery > 1 && totalReadings != null
            ? `Loading... ${pages.length.toLocaleString()} plotted from ${totalReadings.toLocaleString()} readings (every ${sampleEvery})`
            : `Loading... ${pages.length.toLocaleString()} readings so far`;
          // Ran out of batches, not out of data: the server said hasMore, so the
          // requested window almost certainly has more rows before beforeId that this
          // load never fetched. Say so below rather than reporting a plain count that
          // looks complete.
          if (page === HISTORY_MAX_BATCHES - 1) truncated = true;
        }

        if (!isCurrent()) return;
        const rows = pages.reverse();
        let historySummary = sampleEvery > 1 && totalReadings != null
          ? `${rows.length.toLocaleString()} plotted from ${totalReadings.toLocaleString()} readings (sampled every ${sampleEvery})`
          : `${rows.length.toLocaleString()} readings`;
        if (truncated) {
          historySummary += " (hit the safety limit - older data in this range wasn't loaded)";
        }
        $("historyNotice").textContent = historySummary;
        // `moisture` is always the raw, unsmoothed receiver-derived measurement (and
        // raw_battery_mv/raw_charging alongside it from migration 032 onward) - both charts
        // are recomputed from that raw signal by renderHistoryFromCache()/the Smoothing panel
        // rather than read straight from the receiver's own baked-in display_moisture/
        // battery_percent, so a filter change applies retroactively to everything just loaded.
        lastHistoryRows = rows;
        renderHistoryFromCache();
      } catch (err) {
        if (isCurrent()) $("historyNotice").textContent = "Fetch failed: " + err.message;
      }
    }

    // Deletes all stored history for this receiver (every probe, every sensor) - not
    // range- or probe-scoped, since data logged before a sensing-firmware change is
    // uniformly meaningless, not just the currently-viewed slice.
    async function clearAllHistory() {
      if (!confirm("Delete all stored history for every probe on this receiver? This can't be undone.")) {
        return;
      }

      $("historyNotice").textContent = "Clearing...";
      try {
        const token = await getOrMintShareToken();
        const response = await fetch(SHARE_API_BASE + "/api/share-history-clear", {
          method: "POST",
          headers: { "Content-Type": "application/json", "Authorization": "Bearer " + token },
          body: JSON.stringify({ receiverId: claimInfo.receiverId }),
        });
        const body = await response.json();
        if (!response.ok || !body.ok) {
          $("historyNotice").textContent = body.error || ("HTTP " + response.status);
          return;
        }

        await loadHistory();
      } catch (err) {
        $("historyNotice").textContent = "Clear failed: " + err.message;
      }
    }

    // Forces one sensor's learned range back into "learning" mode from fresh data (see
    // docs/STATUS_HANDOFF.md's redesign item 3) - for cases like moving a probe to a new
    // plant/pot, where the old learned range no longer means anything. Server-side, this
    // just moves the calibration window's lower bound forward (msv2_reset_sensor_calibration);
    // History-tab data is untouched. Same share-token pattern as loadHistory/clearAllHistory.
    async function resetSensorCalibration(button) {
      const index = button.dataset.index;
      const sensorId = Number(button.dataset.sensor);
      if (!confirm("Reset the learned moisture range for this plant? It will show as \"learning\" again until enough new data comes in.")) {
        return;
      }
      const status = document.querySelector(`[data-reset-status="${index}"]`);
      button.disabled = true;
      if (status) status.textContent = "Resetting...";
      try {
        let token = await getOrMintShareToken();
        const post = (t) => fetch(SHARE_API_BASE + "/api/share-autocal-reset", {
          method: "POST",
          headers: { "Content-Type": "application/json", "Authorization": "Bearer " + t },
          body: JSON.stringify({ receiverId: claimInfo.receiverId, sensorId }),
        });
        let response = await post(token);
        if (response.status === 401) {
          token = await getOrMintShareToken(true);
          response = await post(token);
        }
        const body = await response.json();
        if (!response.ok || !body.ok) {
          throw new Error(body.error || ("HTTP " + response.status));
        }
        // The reset above went straight to Supabase - this receiver has no way to know
        // it happened otherwise, and would keep showing the stale pre-reset range for up
        // to ~10 minutes (its normal autocal poll cadence). Nudge it to refresh in ~10s
        // instead. Best-effort: the reset itself already succeeded either way, so a
        // failure here just means the usual ~10-minute wait instead of ~10 seconds.
        try {
          await fetch("/autocal-refresh", { method: "POST" });
        } catch (refreshErr) {
          // ignored - see comment above
        }
        if (status) status.textContent = "Reset. Refreshing calibration - this plant will show as \"learning\" within about 10 seconds, then relearn from new data.";
        await loadReadings();
        renderStatus();
      } catch (err) {
        if (status) status.textContent = "Reset failed: " + err.message;
      } finally {
        button.disabled = false;
      }
    }

    function renderEditors() {
      const editorList = $("editorList");
      editorList.innerHTML = "";
      config.cards.forEach((card, index) => {
        const dry = Number(card.moisture?.dry ?? card.dry ?? 120);
        const wet = Number(card.moisture?.wet ?? card.wet ?? 175);
        const presetName = presetFor(dry, wet);
        const autoEnabled = Boolean(card.moisture?.auto);
        const item = document.createElement("section");
        item.className = "editor-card";
        item.innerHTML = `
          <div class="editor-head">
            <div class="editor-title">Plant ${index + 1}</div>
            <button type="button" class="ghost" data-action="delete" data-index="${index}">Delete</button>
          </div>
          <div class="fields">
            <label class="wide">Name<input data-field="plant" data-index="${index}" value="${escapeAttr(card.plant || "")}" maxlength="24"></label>
            <label>Probe ID<input type="number" min="1" max="16" data-field="sensor" data-index="${index}" value="${card.sensor || index + 1}"></label>
            <label>Art${artPreview(card.art || "sprout_pot", "editor-art-preview")}<select data-field="art" data-index="${index}">${options(artOptions, card.art || "sprout_pot")}</select></label>
            <label class="wide">Auto-calibrate (learn moisture range from history)<input type="checkbox" data-field="auto" data-index="${index}" ${autoEnabled ? "checked" : ""}></label>
            <label>Preset<select data-field="preset" data-index="${index}" ${autoEnabled ? "disabled" : ""}>${presetOptions(presetName)}</select></label>
            <label>Dry<input type="number" min="0" max="4095" data-field="dry" data-index="${index}" value="${dry}" ${autoEnabled ? "disabled" : ""}></label>
            <label>Wet<input type="number" min="0" max="4095" data-field="wet" data-index="${index}" value="${wet}" ${autoEnabled ? "disabled" : ""}></label>
            ${autoEnabled ? `
            <div class="reset-row">
              <button type="button" class="ghost" data-action="reset-cal" data-index="${index}" data-sensor="${card.sensor || index + 1}">Reset calibration</button>
              <span class="editor-reset-status" data-reset-status="${index}">Forgets the learned range and starts relearning from now. History charts are unaffected.</span>
            </div>` : ""}
          </div>
        `;
        editorList.appendChild(item);
      });
    }

    function options(pairs, selected) {
      return pairs.map(([value, label]) => `<option value="${value}" ${value === selected ? "selected" : ""}>${label}</option>`).join("");
    }

    function presetOptions(selected) {
      return Object.entries(presets).map(([value, preset]) => `<option value="${value}" ${value === selected ? "selected" : ""}>${preset.label}</option>`).join("");
    }

    function presetFor(dry, wet) {
      for (const [name, preset] of Object.entries(presets)) {
        if (preset.dry === dry && preset.wet === wet) return name;
      }
      return "custom";
    }

    function collectConfig() {
      const next = JSON.parse(JSON.stringify(config));
      next.title = $("titleInput").value.trim() || "MSV2 Plants";
      next.cards = Array.isArray(next.cards) ? next.cards : [];
      document.querySelectorAll("[data-field]").forEach((input) => {
        const index = Number(input.dataset.index);
        const field = input.dataset.field;
        const card = next.cards[index];
        if (!card) return;
        if (!card.moisture) card.moisture = {};
        if (field === "plant") card.plant = input.value.trim() || ("Probe " + (card.sensor || index + 1));
        if (field === "sensor") card.sensor = Number(input.value || index + 1);
        if (field === "art") card.art = input.value;
        if (field === "auto") card.moisture.auto = input.checked;
        if (field === "dry") card.moisture.dry = Number(input.value || 0);
        if (field === "wet") card.moisture.wet = Number(input.value || 0);
      });
      syncLayoutWithCards(next);
      return next;
    }

    function layoutForCardCount(count) {
      const total = Math.max(1, Math.min(MAX_PLANTS, Number(count) || 1));
      if (total === 1) return { columns: 1, rows: 1 };
      if (total === 2) return { columns: 2, rows: 1 };
      if (total === 3) return { columns: 3, rows: 1 };
      if (total === 4) return { columns: 2, rows: 2 };
      if (total <= 6) return { columns: 3, rows: 2 };
      if (total <= 8) return { columns: 4, rows: 2 };
      if (total === 9) return { columns: 3, rows: 3 };
      if (total <= 12) return { columns: 4, rows: 3 };
      return { columns: 4, rows: 4 };
    }

    function syncLayoutWithCards(next) {
      const layout = layoutForCardCount(next.cards.length);
      next.layout = {
        type: next.layout?.type || "grid",
        orientation: next.layout?.orientation || "landscape",
        columns: layout.columns,
        rows: layout.rows,
        gap: Number(next.layout?.gap ?? 6),
        padding: Number(next.layout?.padding ?? 6)
      };
    }

    function nextSensorId(cards) {
      const used = new Set((cards || []).map((card) => Number(card.sensor || 0)).filter(Boolean));
      for (let sensor = 1; sensor <= MAX_PLANTS; sensor += 1) {
        if (!used.has(sensor)) return sensor;
      }
      return Math.min(MAX_PLANTS, (cards || []).length + 1);
    }

    function addPlantCard() {
      if (!config) return;
      config = collectConfig();
      if (!Array.isArray(config.cards)) config.cards = [];
      if (config.cards.length >= MAX_PLANTS) {
        $("setupNotice").textContent = "Limit reached: the display supports up to 16 plants.";
        return;
      }
      const sensor = nextSensorId(config.cards);
      config.cards.push({
        sensor,
        plant: "Plant " + sensor,
        art: "sprout_pot",
        moisture: { dry: 120, wet: 175 }
      });
      syncLayoutWithCards(config);
      renderEditors();
      renderStatus();
      renderDiagnostics();
      $("setupNotice").textContent = "Plant added. Save to update the display.";
    }

    async function saveConfig(event) {
      event.preventDefault();
      // Collect into a local candidate, not the shared `config` - mutating the shared
      // one before the save is confirmed made a failed/rejected save (e.g. an expired
      // admin session) look like it took effect: the Status tab reads `config` too, so
      // it would show the new range even though the receiver's file never changed.
      const candidate = collectConfig();
      if (candidate.cards.length > MAX_PLANTS) {
        $("setupNotice").textContent = "Delete extra plants before saving. The display supports up to 16 plants.";
        return;
      }
      const sensorIds = candidate.cards.map((card) => Number(card.sensor || 0));
      const duplicateSensorId = sensorIds.find((id, index) => sensorIds.indexOf(id) !== index);
      if (duplicateSensorId) {
        $("setupNotice").textContent = `Probe ID ${duplicateSensorId} is used by more than one card - give each plant a unique Probe ID before saving.`;
        return;
      }
      $("saveState").textContent = "Saving...";
      try {
        const response = await fetch("/ui", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(candidate)
        });
        if (!response.ok) {
          throw new Error(await parseErrorText(response));
        }
        config = candidate;
        await loadConfig();
        $("saveState").textContent = "Saved";
        $("setupNotice").textContent = "Saved to display.";
        renderStatus();
        setTimeout(() => $("saveState").textContent = "", 1600);
      } catch (err) {
        $("saveState").textContent = "";
        $("setupNotice").textContent = "Save failed: " + err.message;
      }
    }

    function escapeHtml(value) {
      return String(value).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
    }

    function escapeAttr(value) {
      return escapeHtml(value).replace(/`/g, "&#96;");
    }

    $("statusTab").addEventListener("click", () => setTab("status"));
    $("setupTab").addEventListener("click", () => setTab("setup"));
    $("diagnosticsTab").addEventListener("click", () => setTab("diagnostics"));
    $("historyTab").addEventListener("click", () => { setTab("history"); loadHistory(); });
    $("historySensorSelect").addEventListener("change", loadHistory);
    $("historyRangeSelect").addEventListener("change", () => {
      $("historyCustomRangeRow").classList.toggle("hidden", $("historyRangeSelect").value !== "custom");
      loadHistory();
    });
    $("historyCustomApply").addEventListener("click", loadHistory);
    $("historyClearButton").addEventListener("click", clearAllHistory);

    // Smoothing panel: initialize from whatever was last saved (or firmware defaults), then
    // recompute both charts straight from the already-cached raw rows on every edit - no
    // refetch, matching the "on the fly" point of the whole panel.
    writeSmoothingFormValues(loadSmoothingParams());
    for (const fieldId of Object.values(SMOOTHING_FIELD_IDS)) {
      $(fieldId).addEventListener("input", renderHistoryFromCache);
      // On commit (blur/Enter), snap the field to its actual clamped value - e.g. typing
      // "9999" into a field bounded to 100 should visibly settle on "100" once the user is
      // done, not silently chart against 100 while still displaying "9999".
      $(fieldId).addEventListener("change", () => writeSmoothingFormValues(readSmoothingFormValues()));
    }
    $("smoothingResetButton").addEventListener("click", () => {
      writeSmoothingFormValues(SMOOTHING_DEFAULTS);
      renderHistoryFromCache();
    });
    $("wifiRefreshButton").addEventListener("click", loadWifi);
    $("wifiSaveButton").addEventListener("click", saveWifi);
    $("wifiForgetButton").addEventListener("click", forgetWifi);
    $("wifiLocalOnlyInput").addEventListener("change", updateWifiControls);
    $("soundEnabledInput").addEventListener("change", saveSound);
    $("claimRefreshButton").addEventListener("click", loadClaim);
    $("copyReceiverIdButton").addEventListener("click", () => copyText(claimInfo?.receiverId, "Receiver ID copied."));
    $("copyClaimCodeButton").addEventListener("click", () => copyText(claimInfo?.claimCode, "Claim code copied."));
    $("copyClaimUrlButton").addEventListener("click", () => copyText(claimInfo?.claimUrl, "Hosted app link copied."));
    $("openClaimUrlButton").addEventListener("click", () => {
      if (claimInfo?.claimUrl) window.location.href = claimInfo.claimUrl;
    });
    $("cloudRefreshButton").addEventListener("click", loadCloud);
    $("reloadButton").addEventListener("click", async () => {
      try {
        await loadConfig();
        await loadReadings();
        await loadWifi();
        await loadClaim();
        await loadCloud();
        await loadSound();
      } catch (error) {
        $("setupNotice").textContent = "Reload failed: " + (error.message || String(error));
      }
    });
    $("addPlantButton").addEventListener("click", addPlantCard);
    $("configForm").addEventListener("submit", saveConfig);
    $("editorList").addEventListener("change", (event) => {
      const input = event.target;
      if (input.dataset.field === "art") {
        const preview = input.closest("label").querySelector(".editor-art-preview");
        if (preview) preview.src = artSrc(input.value);
        return;
      }
      if (input.dataset.field === "auto") {
        config = collectConfig();
        renderEditors();
        return;
      }
      if (input.dataset.field !== "preset") return;
      const preset = presets[input.value];
      if (!preset || preset.dry == null) return;
      const index = input.dataset.index;
      document.querySelector(`[data-field="dry"][data-index="${index}"]`).value = preset.dry;
      document.querySelector(`[data-field="wet"][data-index="${index}"]`).value = preset.wet;
    });
    $("editorList").addEventListener("click", (event) => {
      const resetButton = event.target.closest("button[data-action='reset-cal']");
      if (resetButton) {
        resetSensorCalibration(resetButton);
        return;
      }
      const button = event.target.closest("button[data-action='delete']");
      if (!button) return;
      const index = Number(button.dataset.index);
      config = collectConfig();
      config.cards.splice(index, 1);
      syncLayoutWithCards(config);
      renderEditors();
      renderStatus();
      renderDiagnostics();
      $("setupNotice").textContent = "Plant deleted. Save to update the display.";
    });

    if ("serviceWorker" in navigator) {
      navigator.serviceWorker.register("/sw.js").catch(() => {});
    }

    (async function init() {
      try {
        await loadConfig();
        await loadReadings();
        await loadWifi();
        await loadClaim();
        await loadCloud();
        await loadSound();
        setInterval(loadReadings, 3000);
        setInterval(() => {
          if (!$("historyView").classList.contains("hidden") && !historyLoadInFlight) loadHistory();
        }, 30000);
      } catch (error) {
        $("statusText").textContent = "Could not load board";
        $("setupNotice").textContent = error.message || String(error);
      }
    })();
  </script>
</body>
</html>
)rawliteral";

const char MSV2_WEB_MANIFEST[] PROGMEM = R"rawliteral(
{
  "name": "MSV2 Plant Monitor",
  "short_name": "MSV2 Plants",
  "description": "Local plant status and setup for the MSV2 display hub.",
  "start_url": "/app",
  "scope": "/",
  "display": "standalone",
  "background_color": "#0e141b",
  "theme_color": "#0e141b",
  "icons": [
    {
      "src": "/app-icon.svg",
      "sizes": "any",
      "type": "image/svg+xml",
      "purpose": "any maskable"
    }
  ]
}
)rawliteral";

const char MSV2_SERVICE_WORKER_JS[] PROGMEM = R"rawliteral(
const MSV2_CACHE = "msv2-app-v2";
const MSV2_SHELL = ["/app", "/manifest.webmanifest", "/app-icon.svg"];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(MSV2_CACHE).then((cache) => cache.addAll(MSV2_SHELL)));
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) => Promise.all(keys.filter((key) => key !== MSV2_CACHE).map((key) => caches.delete(key))))
  );
  self.clients.claim();
});

self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  if (url.pathname === "/data" || url.pathname === "/ui") {
    event.respondWith(fetch(event.request));
    return;
  }
  event.respondWith(fetch(event.request).catch(() => caches.match(event.request)));
});
)rawliteral";

const char MSV2_APP_ICON_SVG[] PROGMEM = R"rawliteral(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <rect width="256" height="256" rx="56" fill="#0e141b"/>
  <rect x="56" y="154" width="144" height="42" rx="12" fill="#d96b8a"/>
  <rect x="76" y="186" width="104" height="18" rx="8" fill="#7f3f2d"/>
  <path d="M128 158c0-36 0-64 0-96" stroke="#f5f7fa" stroke-width="12" stroke-linecap="round"/>
  <path d="M125 89c-31-8-48-24-51-48 32 1 52 17 60 45z" fill="#20c997"/>
  <path d="M134 110c34-2 56-18 66-48-35-4-59 10-73 41z" fill="#82e6c5"/>
  <path d="M122 139c-34 1-58-14-71-43 35-5 62 8 78 37z" fill="#25b786"/>
  <circle cx="166" cy="78" r="10" fill="#ffb020"/>
</svg>
)rawliteral";
