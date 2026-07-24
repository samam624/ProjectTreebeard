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
    .fill.danger { background: var(--danger); }
    .fill.muted { background: var(--muted); }
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
    .hidden { display: none; }
    @media (min-width: 700px) {
      main { max-width: 960px; }
      .grid { grid-template-columns: repeat(4, minmax(0, 1fr)); }
      .diag-list { grid-template-columns: repeat(2, minmax(0, 1fr)); }
      .network-row { grid-template-columns: 1fr 1fr; }
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
            </select>
          </label>
        </div>
        <div class="notice" id="historyNotice">Pulled from Supabase - requires cloud sync enabled on this receiver.</div>
      </section>

      <section class="network-card chart-card">
        <div class="chart-head">
          <div class="chart-title">Moisture (RC fall time, &micro;s)</div>
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
    </section>
  </main>

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
      "cactus_cluster": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAACyUlEQVR4nO2cv0rDUBTGb0XExdEplC6iQsHBJU5uLkJxExRfwDdwc/UN+gKi4Cq4uDmZxUEoVNFBxMnRpXRQ0SE9Obn526+am36/KeY2f/w458s5N7dtfJkvQ3DMAM9FKCgeRigYCgqGgoKhoGAoKBgKCoaCgpk1/4zfWkxt1YKX94ZxCEYoGAoKhoLWwUN94Zve5UFs/PXqY/TZbtRjq+6pjFAwFBQMBa1bHWpjeNMLt+dNeX+Wfqvr3Ul5MSMUDAWtQ8oHMhU78dZz7nAn3B6oMd+kt6qyDJPn1uWZvi7KAhihYCgoGAo6Da3n89FtuD232Y4O9p5M3rZ12F6y7v+hOSFPZYSCoaCupLyf0LHkIZbmgoFI47fOaWxcpnlS9/V7bHdkHWXuMQlGKBgKCoaCgqGgVXwo2V4Fewk9tR7LerDoB9R8kTpUPIj0ebyTjej/wDq0mjDlwTTKrrEv8uZyqGpAmX7N7YXYsdIGPsVUnj6XTH/bvcjz6HvUVsM6tKIw5auS8llP+UFCC6ixpe04x+aFL+kcgSkPhoJWpVNClU2e6lh0SaP9NKtTSiqVbB3ZJGCEgqGgYCioK3Wol+JfWd4mfVPXmvJY/SYzbYZJt7hsPR2BKQ+GglbRQ4u+RvbFmK1vl96na1jpqXl7fn2c7Z5QMELBUFBXUj6trDrc2wq37+4fTRpB7yXyN8umKYMpD4aC1u17Svu7ndi+s4vLcNtvtyJjgWhV9aJZvapOTg1y+s5RmPJgKGjdPNTG+tpyrrGuZepP1rhyXO7/HTu/NpOAEQqGgoKhoHXw0NXVldKf7fcfEn1RI8fj16SHOgFTfhrKpnGsQ1pCEWtBwQgFQ0HBUNC6eWhfeF6ZcdR1UDBCwVBQMBTURQ8N9MqR49Fr5az2Ma31LIKeruPKEUdgyru4cmScXwlH8Ve/jMsIBUNBwVBQg+UbKsoXwiO2I3sAAAAASUVORK5CYII=",
      "color_bonsai_bowl": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAADgElEQVR4nO2aTWgTQRTHN61e/KCCFCw1h6JIBQW/ECSCVDzm4kXFmxAQT9aDihAQQQg2l3iSSkFPoiJ48SiKYBCCHwWFirQoqLQggsEPBAkRN/ug+9zJzJK3m5nk/7sMu1l2w+PH/83ObKbpNT0gx4DgvQAKKg8MFQYFFQYFFQYFFQYFFQYFFQYFFQYFFQYFFQYFFQYFFQYFFQYFFQYFFQYFFQYFFQYFFWaF10Xyp3b4G1q5veP+8YUTtzP/xsLUAf/8zLkn/rFLwFBhMknueuYDAx9cm81End8wNhS6ful93VtubLX2NvQ73ad041hzudE2AUNdMlRn5KbhEX9c+LLomaC63qashaEudPlC0KWJ3PB4pFl0bGqq6fXdzFgY6lKGFgJTefc2zUyOykx+/252fxhqs6EFlp0cMol3e2mo66vmwUkCQ13I0FLQZelNJ66Rpl1fl6n03DTnqTDUZkPzLLN0pnLDOjVTd30a3R+G2mgombgQGKPr5irDBu9/98fG4bUd/Z9uzkthqM0ZWmKmmkLGfrj+zh+3Zlf548P1P0LXxTWeoC7P39ySmJ/CUBfmoQVmwujsgFE2qq6nbCVz3+wZ7Gheys9LmgpDbV4PLQUZulQLG9EYa9/FyZRDX9e0TmRbw5yhmYSpmWRyEsBQF+ahKrY9b7Q17T9TGdT1ybhO96SSeMeHoS50+bxil1PX1SkrVWZSN+b351mpM5f/LtntYahLq015ZpLKHMpWFZN3n2ZMnsd3DHSmYh7qAKl8OVJSrEbxzKoc2d+MYyhx80zr/vd+t98hSPIdnkCGumgooduFPLlvVzOq26sMJTO/ff4UOj/38Zc/Tj97iS9HXCdVQ3WoDF03utGLMtEGIznI0F76xt4UmzJSBwzt5QzVZanpvLSbwNB+yNDpIBsr2eg3J5uBof2QoeXTO/0/NV9rRaUqS/+M/PTHs1dfWZOpMLQXDS0HRhIrF1dHzjtVptrU/WFoLxlaZmZyyFTKSt0x0c1MhaEuGlqcORp6yNDr1ld2SVPfviV0fLlwB18wu0aihhYDM9My0tTYJE1Fhrr4Ll9nWdbLwFAXu/zE8c2xHpI7uNvouuqjF7H+x+Nb8+jyrmHlemg1pnkqJs9f9MfKlUteWiBD+8FQF80kYKjLq00TMbu9FGl0dwKG9sJ6aJGtPiVFGqtLHBjqyfIXwnXW3VHWM+gAAAAASUVORK5CYII=",
      "color_grass_planter": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAE3klEQVR4nO1dv2vbQBQ+BdOlhf4ThWRMacniQkinFDx0S+lm6NKpUwZBxlANmTJ5KWQr6dbB0E4thnoxLc2YQv+J0GYxBpen6DmvL3fSyZbe6ZT7gmP5dKeLPr3fJzlqrvz4eXuyN3f9N9j8rCmPkJy8mMMLt1UD4RWhPiACMW0KEiJ1cf80ws+wrdtPx9HPLtEoQhFcnZGs3uvNtH04OIts+rtAp86DJwaJKtpX1J4H15IabKivhCY5XhlVmUrZeHJe6vjQv+yYVklorFFNGhb5ClFCkxyyiogsQzTty6W/bgQb6pOXj1msyG0cjy+X9dAwDiWRZ1JdtaHUQLWDUNNJcwDRPLbUobu1cbUxuBnML/ZpLqQSRFB5XyW0CCC9NlKqk7pU8idX27bHaA2hPU36CNtF3pjm9viZ7udELo6XEd1KlR+uID02qWrVcy6DYEN9U/lkSckqEyXo5kGvn2zJlvZEbWgCJ12TTauyYtVoQmNNcdgEU72zqL8OrgolwYb6SmjcP42o+nHpKpJKnYSDncQXhXRBxGkcOsyIoye9IGtin3raznMrVT7OJBelTSeNWHTW2cY8x8NrrHXn9o0gtE3oNHHZmKejuM1jS5MnT/sN/j+uVNWpdkJ7GSlwkkUxYVGow51aXvBuqrHWHZfWvozchQJvAfAkdQ7LZhzOhW26+qpUXBpsqE+Exkuq1/ad5bXS9arpmnRAT2EKg0ZTPSerOJe8kKxKBJX3ldAxk0SeLpZVeZQ26oxsHJLXXr4IplQSVH6/YKxppZTGoC7gjNC4fxodvXk4z5NQfMc66qoFaiWAYEPbROhoOtd6dGyj+/NsLjcFLkOnRtwsdpSpvumhBK6u9LYb7AfHgJfr2qjYHcxdloLCvmH/OtWkTghJod6eempo3z/+GUEOj20oyUONrZSsjQYb6vPdd8qyP/ZFqUPpxfbxZK5m7HirpKteh01Hi1Bp3UgGr40CqG2E/jN2wWabV8fDu/JcrSvVqvKJ5nEXOHE8eQRIIo9JO2e/UuKQbPiM2RD0p2TCWNiPfaANx4ZbcTyH6K04FFTCQIp0WRO3oXn2Uif1dGzZmygaXw+Ns21UTRug2uJFAVJcrrk3ImzqZARyIkx5vAkggUgwbOvsM32EkdpfyYsQ4lD/MqX1dLsLvwYqzXBMEopt2MdUmUJJzJNyfgwpb+90TSkhuTgnoAz4WO60JIN+sTWl8eS8lC3LI7hobYlWsGBO0xpVHQg21FdCh4OzSEL1uF2VzpZEc/kRFIwxwD6+GcxTh5Wn8rpcn85BEwWb9akqEVTe9+eUtknAzVEmEzLdq8SPLV0cacSjiT0NiWm2o0imM73eh2ZjsfZExuMYVxAndERJmNr1LQt6ASTtJyDYUF+/t2nn5YN0orv37ylJXF78Td+/vv8dtVLlL7MTbCvEVP6P1ESO5w421Ofvvnuc2VFpfBeyn06+TPDg3V464ecvP2qdZ/fpo/T98NWH2xHY72Yn3DYEG+ozoQeZurd5zrU2k+li7qDyFSMQWjFaS+jzJ8/SlzRaS6grBELbWLGvGqDqH799Ui4gmnoeOAqdJNNPUZU/FM6rXcwZbKjvNnScVZm6pDiS10bbdW02fSUhXr7bEa6JSq0lOf/nKjs1EytNJCLY0IoRCFXV4h8c46+6WKoQqgAAAABJRU5ErkJggg==",
      "color_hanging_clump": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAE40lEQVR4nO2cMU8UQRTH9wzxA1hIaWLDiYmhIiJGW4ozelpIwyXEisqWM2oIGs/WioqQYEMDp5HC1kSUUBETkWtMKLHwAxjNmdnjT2YfM7OzO7N7e3vvTwjszezsMvzemzdvZrfSDbpBP9T+shxeuH7jeYWWNV5PhmXri3uVpOf2W+f6fQNl00i/LnzYOQh/ttZmQ9qa8xuxpIJMnGsj0b6q7azEhJaF0OrYeGBLG0i90rkcWxfE0+uUokNb5I+TTQ8DCq0jm/hpR45GO9LGhNGReQ9cbPKeVckybGorBhEdXaDRROiP459nzvt99CdyvL2yrx3caPtZiAkdJB9aP/FfKlJrCxPhZxcunQ90RIG+5mKUarkc59dv1XrtBr12QSqlM2sxoUX2oQ0NDfBZoFIIZIFCmVT6memYRgAY3Vffbp1pN0vfCTGhgzDKNwipMlEqIuU6smzqQNOT1cgxIgJxjmrkz0pMaJEJfbn0zbmx3eNGcH10XVsmpCpHGUj9d/Qk9rynS9cqA2HyMxMvwkZv3rsbHn9+9z7AMX6HaJ2Rqc3g79cH2jK5Y+S2UBedhzZUZdSV+Bys2OSLSCgdhH5t3EndlkyoqY6Qqp5NmWoy4YtUJrRoU09VMH9x9oMzqSaBPhuaVeLkyDARavPfnjkZ9W11+/H3yPGnN1cDF33cf9ab+k5tZp4oYR86SAlmCEkRW3+H0ZhOJ0FqklFe0Inr0/ayWA1lQouYYNatNK6epNAgQUtSf6ryrTt79n5TdV82ewL61qFyZ1Y1HSlne2CS+MzUwXQwsjF1KmHup9dfiZZhJcGn2OQ9y5lQlbmsLmxF1nVsTDOtK3AJibJYs2dCByFsail2gwjt7B3GUityqrr8Jf1czmciNEL78I+yP4+7trhv1x0nTGiR1+VrCKCDaACdhOpdA8W1hZ45mTLtdA+AaAv3RSmmEnQm2SqpEhNaRELl9XZToEzryWTCdwk/qxPIUpGGaaWJMN35MtWuQT4TWpYNt23MUjq9Y1ffRWUijZI63Unu8713qGwmGIR2DOZKBwj6R6ADVG6BCubdnlvuIrxJcr58P6oNZi5ik/es1ITCRMVgQjdmNWKeM/KV4fHtJnyICS3aoOSbkm3Jz4J4Hem2T3ignbjA3oeY0H4Tqkt8qB42aFj4UhvRduB/s0gQu4oJ7TehdF1G0EJjv/WMtl5TUm1SbDYPLaji2tw61HTBlsWiF26+NRlfl7qQrCUgcX10nE2+iHP5R3P3vS3P1uRVSg8uRD5XNy3FfYr75nxoGQmVp6FCuv+yKeRylY+gXc7Yp7Uy9qFFJFT2QYFl3axkmkyYMv4Y2dO8hkMWE+pZXju0OjZuTFjk+TKVtBKUugT3TGiR15TqhveI5C3XxExaa8pkka7paNpZ7NvM6zps8oO4xz6JREhDp7I2BNFQSPVSGEjnknyQyoSWZaNDkqlsEoG+9fn4FVdKqo/35DGhw0AolGT6R59BshGlURDqOvIzoWUf5YV0CQoVNdQPYn9Vni9ukcWEDoMPrRte8VZ0FbJDoSRZnzTbGbMQm7xncYd6FnfoMPlQGxVtFYAJHYbA3kU01MqbYCbUswbeh/pM/fkQE+pZpe3Qw85B+J33CmzpTL5J9t/nnQcoLaF9U7fkX6/WHnbzvB4TGvjVf80TmKiQIsx7AAAAAElFTkSuQmCC",
      "color_hanging_vine": "data:image/png;base64,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",
      "color_round_pot": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAACzUlEQVR4nO2aMUvDQBTHE3Wr4JcQdCwILpnqnF1xE1zc3EQoiCAENzcXwU1076xTlpZCRwW/hKCjVGh9YF5zXtq+u+Qd/98S0kuT8vj13bt3icfROAJyrAjeCyCg8sBQYRBQYRBQYRBQYRBQYRBQYRBQYRBQYRBQYRBQYRBQYRBQYRBQYRBQYRBQYdaihpHdHxS2EM6PHuNIETBUmLgpe0oZMzPvv06Oye62KlNhaOg5NP8103TedGBoqDmUSE/apT+odzuKNVQLMDR0Q23m2kzlhlOV4MtUGKp1lk8tudE0vuz9CeRQpXgzNPnNZfPWlbbvWXPrbeQV5FDtK6XEYhyflQkykNeXSTS9Ptudfl73mh+GhlKHpmxW5jmQ50bbLN6UrhQMDaXb1LMYOZMz+/NVCzzXElgpKaM2QzNuUL983NS5n6k/Wb3Jv+cL5FDthqY0WzMjyaQ0Kh+vOvvz3EymzttPXRQYqr0OzVhuNMFzHzfNVpf6MpIDQ0NZKWWGPR9TTqzaL63LTAKGhrqnlFrMXPTcNzBUu6HpkntHhGkFxHOx7y4UDG26oZmhy2NbsZBR7Z2tSs8ZDd8K52Ti1/f433F0m5ThbC2fV9zd5P3Omfs8DyfHZG+ncN7aWC993tfHZ+Fzus7XW3zIoaH1Q3PWtzSZRmYSfNwEXffy8I5ZXiPO6tDU0qe0dZ2qGsg5PbuYHG+uL0vHXZuKHKolh/ZqWkuTmTZTXQFDta/lu3f7hQe2VuN/Z/dFIUMJMpXq2avjJyf/IBiq1dAuM9NUX0pDpg4GvdJxaVNhaCgrpdyxmQSf5SmHugKGhmJowrpHvp7nGhiqvQ7tHG42YpvV1ZoehgqDgAqDgIby5kinplyKfqgyan+3qePJVOwpKaV2Q3k3yrQPv+i5q76nCczyoRpq65vOi28zCRgauqHLmlqXmQQMDd3QLnIoiP7wA1kKftSrAT86AAAAAElFTkSuQmCC",
      "color_vine_pot": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAADj0lEQVR4nO1bPWgUQRSeiz9YnFwriIUoaRQJWOZQFJsjWwgWgvU0ByFYpboy1VZBhDRbCxYHFkoaMRiSUpCgCMEgKIKt5IqghJO5yztmJzN7e7h3783M+yAwbGbv57vvm/czs7W+6AsfIdO7ow+erb6vCSKYw/4AoWHOd3VSw1mKxNy8WBdP22/I2Dh4hVJGDSMoyTGWVUFmfWNpMEdXqu0+SgEJhVDpINNFjCIWSKUa2XWw5X1VqLQo89e339a5rzc+jtQH1lf483k4f/X5Dkl1oq2hSXuh8E0vXW3kIv6Vv8PxwT59Qtnyvis0KVCnrkzA/PdjcW2+kVPo9nHPuUREndibZNjWWWXvdLk5uA7Ebn/pWX8kCsSy5UMISi648szus2Gkf7RSnOSb92GAFRoDodkYlbnU6cpro7W8C2B5wOZRz/kDYNf7JBXqM9DTJhtMlbVEPZeHZo5KiULjmQyhsoAMIBLy0LL3YkR8tnxoCpUlms1QKZUBdh7qRZQHALF6t0m1997u/RQmsMpQtnwMCpXaMqD6obbGMjSeberEVCs6oevaZpwrKYfEHqL9+RuN3OadqyWIYXu2vO9RXhoqzNrDqDxQ6mG51zAPQVDogwJYoaHloQmsf3v2LRBXpaTvhn467MWXh0pHwNEDik6ojRgITj/O0T1Rwpb33fKZppxx+/OnFHh08horefXBa+pKVWMMlc6M0KzEl7PNmaQlN67RPAuw5UOqlBLN8mYuSSnQeFV6uqAT2rqQ79jz2aaIQFahOsyjOPqBB2rwgtCiEyTUwFE+tFp+EkBQogxWaIwKTS2bc1ThRVDqGlsglIlly1cMJjTGNfRgzNkmSmCFxhiUfKqUvLB8CofF9gV5sOVjVCjAh6BEfg3tGg8sAKiuo2z5GBSaLjf78IDsnTP1Uw/L6tfUmFIpygoNVaFJe6H/4NZlAYdobWOA6/9qjH0SD53Q5GQrWZECp5FdY0CZuXzGPhCgK7STPZ7KB1iTL2vREXrvyfWpvvnWi698xt53cNoUguU7lnVz992H0Xjx/m3rdR2uOfp1jPWUFRpqt2nRoqyi65POmRWCU+jDZmvwh4XgCMUGE1oxmNBQg1IVUGvnq51NgQnU0rMzpToes55ny1cMJjQkQtemaEms9h0rNOQov1uyQTJJYyS6jn3VzWaMprIOtnyohG5VoCxsdZKy/P/YnwKR5BQqAsE/eRzU+7rmCVIAAAAASUVORK5CYII=",
      "color_zigzag_pot": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAC70lEQVR4nO2cv0scQRTH98S/QjsVcknlDxDMVWtrmpAiYiEIgqQxlaAQUoTABbbSJgQCQopgCrlGW8XiEhCSWOkd5K7L/RFpLrC5V+zkxp1b3s68mft+mmN37xfDh+/Me3N7lX7UjwAfE4zvBTCg/MBQZjCgzGBAmcGAMoMBZQYDygwGlJnJyBMaX9+kJV2rfZs5X33wKH18+vh1JRIADB03Q+vH6/+aDe3h11VjXQNDmalI7TY1BplZ1ERX2QpDfc/Qhma2Ptg6qZhkpimushWG+pqhdTLPMmS+LWCoLxna0GRl6MBQ6YbWmWZpHc3rVua4tlw1ep4tYKjUWb5oZdNUTDp7fzN0Vj49Wkvfv9vppcd7hz9FdJdUYKjUDM0zsjkwkQxMXi6kxm09nE7PP9s999pMAoaG1m1KBqbOzE5lTLVlJq1KuLpTMDQ0Q3WmErpsLbtyK2osDA3VUG5TuXoJo5oKQ0M3lMtU7v6raV8VhkoztOxfdCRMmUrr2uO73/d2qXSVnSkwVIqhttZ1qqmEWlnpjFShyou4+tPPGFvUTAKGujKUe4/ooOBupC5T87DVrYKhrvqhlH2u9tdHRc1KW31UGCplln/yYr5f5npu1Ox0ZaQKDPWllk+YKhzd+pOQttcEQ33rNiU5hunMNc1MKWYSMDSUfmiiMZDM881MAoaG1rFPDGtz6WYSMDQ0Q037l9LNJGBoqAPa7fT+q8d9RMyAhoKYDDVdn0rPUhg6bvfLE2QsGSzVVBgaiqE7K4tDw3tOOVZre3rUvZ748O0HOvYhYG2W38kxqmxsGYsM9c3QeGNO1EL38vMv/HLEJ6xlaOzY1LLNJJChvtfyrz4+Tz+wefE9c762upQ5Vq+r5D2frr/d/oL/HPEZZxlaMzRMx6iGF83QzXfL6ff+tH+Nu0BcgFmeGczyvs/yseX1qK31JwFDQ9lTiks21baZBAwNbdczZjaV20y6Pwv3y4+roWqNXxTbNbsOZGjEy189D5atpwf8JwAAAABJRU5ErkJggg==",
      "cup_plant": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAACgUlEQVR4nO2dMUscQRiG9yQcIlbHpYmIASUQjjRp1ibaiRg0taCFpfEPhPsBIU3K6A8wkDqGiNipjdukCVpIBOUQBA8tkiKk0HDl7s7kxvPd3VnzPN0us7N7D9/sezuznpWb4CYAHX3CvgCheqhQMQgV8yDwmHDkoTQxo9OLSpAxVKgYhIpBqBiE+hRKvYZGZAgHU18Dq0uBkvB1/BxZhBQVKgahYhBaxD3Udq8c+rLQ00nDWe0Xdp+gQsUgVAxCxSBUDELvw/TdkOHbQWvzZ2rfr++XTv31rX12asf0XQlhyItBqJiKaV0++WT0uzHW8wlqKy+c7o1/dg9S+6oTDadzmI410X/wI7bNbFMJYMiLQagYhPr8xb5qCJHLD3tO7UwMPqt17es2XC+/im2Ha+lpxLsGFRUqBqFiECoGoUWEUvIJw8a1IWxG3427XclMut1Vy+1Q15DLAypUDELFIFQMQvMIJdenBeUbxmezH7sulbgtiBS7JEKFikGoGISKQWjZ1uWP3+w7PdnU+wezvhTW5csIQ14MQsUg1KdXwmuJIJk/PEq1eR/oeHR8btzfToTcyuRTY7twR/dnQDaoUDEIFYNQMQjN4+0711D6uv02tr04F9/uMB8Op/Z9ihwXixz6MvW3vtE0tns51fzn9XeoP3me2hc+nnYOKipUDELFIFQMQn0KpehkK7bdPvoW+IApWGzh0u0z2Y4jlHKCIS8GoWIQ6vOaUt0SBr5ielK6K1SoGISKQagYhP5Pvx+aNcnpPBusKRUIQ14MQsUgtIjpO9dpvcgw9VUEtmk6fhWnhDDkxSBUDEJ9CqWs/zNCr+QRPjaoUDEIFYNQMQgNtPwFtTabbBbLQVUAAAAASUVORK5CYII=",
      "monstera": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAFTUlEQVR4nO1dO2gUQRiek0NsBMVi4TAocoFUxsYgNikjBLlGfARLK5WQSouTSIIBS4kPEKxjY3NEi3TahBARjJWQIyQIwhViwE6Lk9ncv/ffZJ67c7Ozy3wQsne7t4/vvv85s3uVLumSPBDNXJUeuLOyWiEFRCUPQjfaWwMHbcw/MiZW9YW0Fp+QS/Vx51/KEdcHLDusK3R5+65yh0uPfxxSk0ypWKWsMuGzLPC+XLqPap5EshePyeGRi8lUfQl5IZi8jwrVUacOsNqwApMgNu+vMq2bvG00OG6ALvtKJCCYfJEIra2dHPhLA6xI39VJERTqUx7KC0Y1pMSfU7+10iadvFIHrILzKF+tBCUZiYPb8AlV5Z94O9V6nf0ME8HkfVIoKFOkShmGoSY2j43IQWXl0vSDQn1QqKoyqnFSpFjF6+4SdLr/xApWSPEqJVFgqvXep/+bC24DBhynQ1aJKwST9zEo3Vv/lqREzYWRgfWg1jgH7Zl8lhyU3Uam9peXzzs3+VSJ/bsXzfhDp69cl17Y8YmI/NnsJK9FnSJVB4m3XidLwF/u7OgrJ5E+mLwPCoUoj0tJrKKWxrAE/pwqSMkULFMqmLwoTx6GalMRKhtxbBmUkaptdLflHZOSCUTKSmPbpAaT9zUPNS0lGxZzUbyvJBCtEWlezLovW0o1Nnlq7rL0poF8qIw0uHDRkLIKvH3jml1UzVFCD9I8MbLU/sHkLcNYoXQEkpdHmkTwJsoP2UxBFslFo6IiRWGV4s6YTqM77XSeqnFknx88oC6aCyP9iyJ9n0bJUM1TktbpEvMEv0gLEZ0WI68KM+0DBJP3IcqrTJAXoDa7rwkGjqqgsvrEHW2lmgSOa/eXKqqWo8jdgPXAdahcgNKHyiZnqfxmE/lKDFmKUn92QCr0ANjjmRApciU8V2VShcnOIZi8ZUgViiM6r1ZPM/0wyyRaE3VSpeNOl0n3Srf44J1PVRXRdWtu3s4jAUH0fRXpaQDugqI990aYPWCRwDJPKDy/iomng4DsdQeTdxnldQfT0pRqG70piib7pwqcnT56aP3yh7+JKk3OQeW+eK1I1fkGheadh8Zq6Y3RiKqIyJJP5PlGVpUUiWrnOOea8Zx0y2whobibhCWfxy0qAIjWNK8FIp9Wo2Q9lLI0eRfluEK3BAN4M4Sb0QB0s5xg8sPOQ9lukmnAiTRMy2TMCQMrFAcn3hCH7aEN3Xw6KNRlUMo6a62l6OzztpeplKoTlIn7m2lm/5lCl4tDhMbBZybb7XUtg1FK07yPnUIpmrsvC1DDRDB5FyafxdRbGacq8vK+2++fx8uxuaPRTADP5NPedeLdjV8Ny1MV6f6gr4onnwHoa16Ud+FXeQgm77tCOykH3WSA4CK6jYc3kSEvhQ7liQ5RxqEHdkok7iIBqSri6Ho6lkQcI5h8URXaQtGfXZap9eaXbemxPv47lixv7XytlPL27k4v7WI7VzywHR14bUIkYPzchW7epAaTL9IDCDorq5WNxcFHCsmAS8+30ze0VElx4lS/N0p2SLmf2zR5cSo+wPfRwbEgkSsY2+534tPg0+e1YPJlQvChRSV0TMOUs5q7DwgKLfJjhsaQAmXTXYoMZ4TWz55Jltu7ewPklsHUAcHky0bow1sPSJmQO6FlQyDUMkpF6P4v/oxllygVoT4gF0LrKIUqG4JCLSMQWlRC27t73GVbwciHgEQRFFrk5kjbsjJ9ROEVuu+JqZeGUN/g7WPXi6ZM579WM9kb/RwmkXlPcqAIJl/k31OazKhSnjp9UGXuP1BlSm4RiAQEkyd28R8QuybvygGrGAAAAABJRU5ErkJggg==",
      "spindly": "data:image/png;base64,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",
      "sprout_pot": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAAB6klEQVR4nO2cPUsDQRQAN9HCQhAEERSxtrBOI+gfCNZCShvx94hNyoC1CPaCTWoLawkWQbDRwsbEyur2Qpab3G10ptxi993w9l7249KahmkQjjbYlyiUxwyFUSiMQmEUCqNQmFWik87+VqUfs8OXt1aT4xMx/GKGwigURqEwCm2yKJW9/HfvepWC6HSL/ZYViVgMVcdPjWEWZiiMQmEUCqNQGIXCtMqOQBZVTefltTuItucQw6zqb4bCKBRGoTAKbXLpObr/iLa3r28rBTG5OC32mRBD1fFTY5iFGQqjUBiFwii0yZVSnYdsk0iRKCtAxAEb9bxmKIxCYRQKo1AYhda19EypnMRVmCp4FecP45SHUSiMQnO8irO5tl5oO+vsFdpuhqNov+9fnylhzDV+agyxZ/MqTgY45WEUCqNQGIXWtcGcUuUvjw/CsnP18Fxos8pngFMeRqEwCs3x08TYC/2/YobCKBRGoTAKhVFojlV+sH0Ulp3e+BHpxwyFUSiMQmEU2uR+aMo+6fn3YciR/spTtN1/xckUpzyMQmEUCqPQHJeeMU52NkKO9MeL7d8MhVEojEJhFJrj0nPe5eig5n3T2B4ntcQswwyFUSiMQmEUCqPQZanyOXwTuuiKHsMMhVEojEJhFBpYfgCCm37vJRUk6wAAAABJRU5ErkJggg==",
      "wide_planter": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFQAAABUCAYAAAAcaxDBAAACk0lEQVR4nO2bsW4TQRCG90IoQDRBUIJDSZFIFODa0CAKkNKkoaEML8Az8AKkpKGhQYIC0ZjUFp1TUIKhRIIGkQKB6ay9sW9u1/59F+Pvq3y69XrzaWZubu9SjMM4gI4N4VyAUD1EqBiEikGoGISKQagYhIpBqBiEikGoGISKQagYhIpBqBiEikGoGISKQagYhIpBqBiEikGoGISKQaiYzXDK6HYuV77KMhh9K8IphwgVg1AxCF2VGtp1aqHH4PO76jm355uzyfpLhIpBqBiEiikWeYPZq5Pnnz1Knufo3v7k8/jjm8pxxfX7pePe25fJv/Hr8fNG6isRKgahbaa8TfG4xfHS7yhK6VnsHb+YfH618zBpXN3YnDV1t+/KSgARKgahTaa8l+L2inz708lcV2579bZp7aV4zrw2/d9fOzdz3KIlgAgVg1AxCF2Vtmnryu/k9mZZbVM8/sfXs6VztE0rAikvBqFt9qHeDpJ3e7nn9Ja2FubsNuXMm3Nbanem6ENbhJQXg9Bl1tC6J5V/Dx6Ujj88OZDstG8l9qy2Znq9Zc6abj49LJ3bOHxd+b26ekqEikGomOJW59Ikze90dksn+6Ohm/IXdi4mtU29modpP4+/z5zTG1c3NmdNdl6b8rEX68RChIpBqBiELvNlsd2rZ0on+yP/y3Ht6YXqOmlrVOqci45VrSn2UueECBWD0CbfD51qo0w7EbdROSnUFKlr8tqkXIhQMQgVg9Ama6hto0Lwa+qqYmvm9N+dDhEqBqHLTPnhlz9u6E8f3wjrwNB48SBCxSBUDELFbMYPnbrBPqSb/xbsfyLepechXcOQ8mIQ2u6t53rSr9mljyFCxSC0zfdD15UB/6fUHqS8GIQGLf8AMjnoelutA+kAAAAASUVORK5CYII=",
    };
    // MSV2_ART_CATALOG_END

    const presets = {
      custom: { label: "Custom", dry: null, wet: null },
      herb: { label: "Herbs", dry: 350, wet: 800 },
      tropical: { label: "Tropical", dry: 380, wet: 860 },
      succulent: { label: "Succulent", dry: 260, wet: 650 },
      fern: { label: "Fern", dry: 450, wet: 900 },
      dryHouseplant: { label: "Dry houseplant", dry: 300, wet: 720 }
    };
    let config = null;
    let readings = {};
    let wifiInfo = null;
    let claimInfo = null;
    let cloudInfo = null;
    const MAX_PLANTS = 16;

    const $ = (id) => document.getElementById(id);
    const pct = (value, dry, wet) => {
      if (value == null || wet === dry) return null;
      return Math.max(0, Math.min(100, Math.round((value - dry) * 100 / (wet - dry))));
    };
    const fillClass = (p) => p == null ? "muted" : p < 25 ? "danger" : p < 50 ? "warn" : "";
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
      const response = await fetch("/data", { cache: "no-store" });
      const data = await response.json();
      readings = {};
      (data.sensors || []).forEach((sensor) => readings[sensor.sensor] = sensor);
      $("statusText").textContent = "Updated " + new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit", second: "2-digit" });
      renderStatus();
      renderDiagnostics();
    }

    async function loadWifi() {
      const response = await fetch("/wifi", { cache: "no-store" });
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
      $("claimQrImage").src = "https://api.qrserver.com/v1/create-qr-code/?size=220x220&margin=10&data=" + encodeURIComponent(url);
    }

    async function loadCloud() {
      const response = await fetch("/cloud", { cache: "no-store" });
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
        $("wifiNotice").textContent = await response.text();
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
        $("wifiNotice").textContent = await response.text();
        return;
      }
      $("wifiPasswordInput").value = "";
      $("wifiNotice").textContent = "Forgotten. After reboot, connect your phone to MSV2-Display to set up WiFi again.";
    }

    function renderStatus() {
      if (!config) return;
      $("statusGrid").innerHTML = config.cards.map((card) => {
        const reading = readings[card.sensor] || {};
        const dry = Number(card.moisture?.dry ?? card.dry ?? 350);
        const wet = Number(card.moisture?.wet ?? card.wet ?? 800);
        const moistPct = pct(reading.moisture, dry, wet);
        const battPct = reading.batteryPercent == null ? null : Number(reading.batteryPercent);
        return `
          <article class="card">
            <div class="card-head">
              <div>
                <div class="name">${escapeHtml(card.plant || card.label || ("Probe " + card.sensor))}</div>
                <div class="meta">Probe ${escapeHtml(card.sensor ?? "-")}${reading.rssi ? " / " + reading.rssi + " dBm" : ""}</div>
              </div>
              <div class="art">${artPreview(card.art || "sprout_pot")}<span>${escapeHtml(labelFor(artOptions, card.art || "sprout_pot"))}</span></div>
            </div>
            ${metric("Moisture", reading.moisture == null ? "--" : reading.moisture, moistPct)}
            ${metric("Battery", battPct == null ? "--%" : battPct + "%", battPct)}
          </article>
        `;
      }).join("");
    }

    function metric(name, value, percent) {
      return `
        <div class="metric">
          <div class="metric-row"><span>${name}</span><strong>${value}</strong></div>
          <div class="bar"><div class="fill ${fillClass(percent)}" style="width:${percent == null ? 0 : percent}%"></div></div>
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

      let vMin = Math.min(...points.map((p) => p.v));
      let vMax = Math.max(...points.map((p) => p.v));
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
        label.textContent = new Date(t).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
        svg.appendChild(label);
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
        tooltip.querySelector(".tt-value").textContent = formatHistoryValue(p.v, unit);
        tooltip.querySelector(".tt-time").textContent = new Date(p.t).toLocaleString();
      });
      hitRect.addEventListener("pointerleave", () => {
        crosshair.setAttribute("opacity", 0);
        hoverDot.setAttribute("opacity", 0);
        tooltip.style.opacity = 0;
      });

      latestEl.textContent = `latest ${formatHistoryValue(last.v, unit)} · ${new Date(last.t).toLocaleString()}`;
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

    // 150 rows/page (~22KB JSON) stays comfortably under a hard ~31.6KB ceiling hit
    // during testing - looked time-based at first (http.getString() timeout) but held
    // steady at the same byte count regardless of requested size, so it's a fixed
    // buffer limit somewhere in the ESP32's TLS/HTTPClient stack, not a timeout.
    const HISTORY_PAGE_SIZE = 150;
    const HISTORY_MAX_PAGES = 80; // safety cap: 80 * 150 = 12,000 rows max per load

    async function loadHistory() {
      populateHistorySensorSelect();
      const sensor = $("historySensorSelect").value;
      const hours = $("historyRangeSelect").value;
      if (!sensor) {
        $("historyNotice").textContent = "No probes seen yet.";
        return;
      }
      $("historyNotice").textContent = "Loading...";

      const rows = [];
      let afterId = 0;
      try {
        for (let page = 0; page < HISTORY_MAX_PAGES; page++) {
          const response = await fetch(
            `/history?sensor=${encodeURIComponent(sensor)}&hours=${encodeURIComponent(hours)}&afterId=${afterId}&limit=${HISTORY_PAGE_SIZE}`,
            { cache: "no-store" }
          );
          const body = await response.json();
          if (!response.ok || !body.ok) {
            $("historyNotice").textContent = body.error || ("HTTP " + response.status);
            renderHistoryChart("historyMoistureChart", "historyMoistureTooltip", "historyMoistureLatest", [], "#3987e5", "µs");
            renderHistoryChart("historyBatteryChart", "historyBatteryTooltip", "historyBatteryLatest", [], "#199e70", "%");
            return;
          }

          const page_rows = body.readings || [];
          rows.push(...page_rows);
          if (page_rows.length < HISTORY_PAGE_SIZE) break;

          afterId = page_rows[page_rows.length - 1].id;
          $("historyNotice").textContent = `Loading... ${rows.length} readings so far`;
        }

        $("historyNotice").textContent = `${rows.length} readings`;
        const moisturePoints = rows.filter((r) => r.moisture != null).map((r) => ({ t: new Date(r.received_at).getTime(), v: r.moisture }));
        const batteryPoints = rows.filter((r) => r.battery_percent != null).map((r) => ({ t: new Date(r.received_at).getTime(), v: r.battery_percent }));
        renderHistoryChart("historyMoistureChart", "historyMoistureTooltip", "historyMoistureLatest", moisturePoints, "#3987e5", "µs");
        renderHistoryChart("historyBatteryChart", "historyBatteryTooltip", "historyBatteryLatest", batteryPoints, "#199e70", "%");
      } catch (err) {
        $("historyNotice").textContent = "Fetch failed: " + err.message;
      }
    }

    function renderEditors() {
      const editorList = $("editorList");
      editorList.innerHTML = "";
      config.cards.forEach((card, index) => {
        const dry = Number(card.moisture?.dry ?? card.dry ?? 350);
        const wet = Number(card.moisture?.wet ?? card.wet ?? 800);
        const presetName = presetFor(dry, wet);
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
            <label>Preset<select data-field="preset" data-index="${index}">${presetOptions(presetName)}</select></label>
            <label>Dry<input type="number" min="0" max="4095" data-field="dry" data-index="${index}" value="${dry}"></label>
            <label>Wet<input type="number" min="0" max="4095" data-field="wet" data-index="${index}" value="${wet}"></label>
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
        moisture: { dry: 350, wet: 800 }
      });
      syncLayoutWithCards(config);
      renderEditors();
      renderStatus();
      renderDiagnostics();
      $("setupNotice").textContent = "Plant added. Save to update the display.";
    }

    async function saveConfig(event) {
      event.preventDefault();
      config = collectConfig();
      if (config.cards.length > MAX_PLANTS) {
        $("setupNotice").textContent = "Delete extra plants before saving. The display supports up to 16 plants.";
        return;
      }
      $("saveState").textContent = "Saving...";
      const response = await fetch("/ui", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(config)
      });
      if (!response.ok) throw new Error(await response.text());
      await loadConfig();
      $("saveState").textContent = "Saved";
      $("setupNotice").textContent = "Saved to display.";
      renderStatus();
      setTimeout(() => $("saveState").textContent = "", 1600);
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
    $("historyRangeSelect").addEventListener("change", loadHistory);
    $("wifiRefreshButton").addEventListener("click", loadWifi);
    $("wifiSaveButton").addEventListener("click", saveWifi);
    $("wifiForgetButton").addEventListener("click", forgetWifi);
    $("wifiLocalOnlyInput").addEventListener("change", updateWifiControls);
    $("claimRefreshButton").addEventListener("click", loadClaim);
    $("copyReceiverIdButton").addEventListener("click", () => copyText(claimInfo?.receiverId, "Receiver ID copied."));
    $("copyClaimCodeButton").addEventListener("click", () => copyText(claimInfo?.claimCode, "Claim code copied."));
    $("copyClaimUrlButton").addEventListener("click", () => copyText(claimInfo?.claimUrl, "Hosted app link copied."));
    $("openClaimUrlButton").addEventListener("click", () => {
      if (claimInfo?.claimUrl) window.location.href = claimInfo.claimUrl;
    });
    $("cloudRefreshButton").addEventListener("click", loadCloud);
    $("reloadButton").addEventListener("click", async () => { await loadConfig(); await loadReadings(); await loadWifi(); await loadClaim(); await loadCloud(); });
    $("addPlantButton").addEventListener("click", addPlantCard);
    $("configForm").addEventListener("submit", saveConfig);
    $("editorList").addEventListener("change", (event) => {
      const input = event.target;
      if (input.dataset.field === "art") {
        const preview = input.closest("label").querySelector(".editor-art-preview");
        if (preview) preview.src = artSrc(input.value);
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
        setInterval(loadReadings, 3000);
        setInterval(() => {
          if (!$("historyView").classList.contains("hidden")) loadHistory();
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
