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
      grid-template-columns: repeat(3, 1fr);
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
    .network-actions {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
      margin-top: 10px;
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
    .hidden { display: none; }
    @media (min-width: 700px) {
      .grid { grid-template-columns: repeat(4, minmax(0, 1fr)); }
      .diag-list { grid-template-columns: repeat(2, minmax(0, 1fr)); }
      .network-row { grid-template-columns: 1fr 1fr; }
      .fields { grid-template-columns: 1.3fr .7fr 1fr 1fr; }
      label.wide { grid-column: auto; }
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
        <div class="network-row">
          <label>Network name
            <input id="wifiSsidInput" maxlength="63" autocomplete="off">
          </label>
          <label>Password
            <input id="wifiPasswordInput" type="password" maxlength="95" autocomplete="new-password">
          </label>
        </div>
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
        <div class="notice" id="claimNotice">This code is local for now. It will become the app/cloud pairing code later.</div>
      </section>
      <section class="network-card">
        <div class="editor-head">
          <div>
            <div class="editor-title">Cloud Sync</div>
            <div class="meta" id="cloudStatusText">Loading cloud settings...</div>
          </div>
          <button type="button" class="ghost" id="cloudRefreshButton">Refresh</button>
        </div>
        <div class="network-row">
          <label>Mode
            <select id="cloudEnabledInput">
              <option value="false">Disabled</option>
              <option value="true">Enabled</option>
            </select>
          </label>
          <label>Sync interval seconds
            <input id="cloudIntervalInput" type="number" min="60" max="86400" step="60">
          </label>
        </div>
        <label class="wide">Upload URL
          <input id="cloudUrlInput" maxlength="159" placeholder="https://example.com/api/msv2/snapshot">
        </label>
        <label class="wide">API token
          <input id="cloudTokenInput" type="password" maxlength="79" autocomplete="new-password" placeholder="Leave blank to keep existing token">
        </label>
        <div class="network-actions">
          <button type="button" class="ghost" id="cloudResetButton">Reset Cloud</button>
          <button type="button" class="primary" id="cloudSaveButton">Save Cloud</button>
        </div>
        <div class="notice" id="cloudNotice">Cloud sync is disabled until you enable it and save an upload URL.</div>
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
          <button class="primary" type="submit">Save to Display</button>
        </div>
      </form>
      <div class="notice" id="setupNotice"></div>
    </section>

    <section id="diagnosticsView" class="hidden">
      <div id="diagnosticsList" class="diag-list"></div>
      <div class="notice">RSSI below -90 dBm is edge-of-range. Missed packets are inferred from probe wake-counter gaps.</div>
    </section>
  </main>

  <script>
    // MSV2_ART_CATALOG_BEGIN
    const artOptions = [
      ["cactus_cluster", "Cactus Cluster"],
      ["cup_plant", "Cup Plant"],
      ["monstera", "Monstera"],
      ["spindly", "Spindly"],
      ["sprout_pot", "Sprout Pot"],
      ["wide_planter", "Wide Planter"],
    ];
    const artImages = {
      "cactus_cluster": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFIAAAB8CAYAAAD6iufdAAADKklEQVR4nO2dMYsTQRiGN3KENHZnFY40ogcBCwvXys5GCHaC4h+4f2Bn6z/IHzgUbA9s7KxMYyEEomhxiJWlzXGFJ3ftvh/MMO/cbsLzlMNkk3uY2Tfft7u50UVz0UA5NwzHAET6YEWaQKQJRJpApAlEmkCkCUSaQKQJRJpApAlEmkCkib3mGmlnt5J7dqvTP6Nmi2BFmkCkCUSaQOTQw6YVwTI9eSnn/vrwt/v6pQ6moYYQK9IEIk0g0gQiTSByG0vEiPNP687YpKnzzUGlflS65nxDYEWaQKQJRJpA5NDDZqVO6gt9Uh8fPe2MnQXHbZv0nqYqSdVniEpXNTcKIFakCUSaQKQJRJpA5C41dn+++twZGz+a6wOvfzQlDePz+e2keZccJKb+JaxIE4g0gUgTiOwjbFL7e7mEwSI4E2Hxe3Es56pgSe2HXh132Q02SsTKsLVNINIEIk0gsmZqR1fVUhul0dycdI2SfFJaIoqEjt5r+uZhZ4wSsTJsbROINIFIE6MHs/2iG0Kj8mosTuAHT27KuSqE/okri9H7qQDKCbzo71VzKRErw9Y2gUgTiDQxUr9EFVU2qheYwyQIhVrHLYX7I3uArW0CkSYQaQKRJvZKnxnMKRGnor8XlWJRkuf0I1PLwahPmgMr0gQiTSDSBCL7KBFzTtTTjLkqWHJ6jNH9jakXunL6pPQjK8PWNoFIE4g0gciaqe240bQVc6OyTyVpVHqqNK/VGKax2wNsbROINIHIPsJGEZWTR88fd8a+fP2efNzV+lSOUyLuOGxtE4g0gUgTiNzG30Z78Wwhx9++P+mMtfOZnLsSzVb1gHr0LKG6kslVxAHB1jaBSBOI3KUf4rx/707R3GVwdVKVqWqumnc1993H5M/FijSBSBOINIFIE4gcemofHt6t8vrN5lty6irU3PizktrXDlvbBCJNIHKXSsRScoKpNAQjWJEmEGkCkSYQaQKR25jaG5GijrmlON6LFWkCkSYQaQKRQwmb8KmG190bUHP6hjllXynR1UKeaugBtrYJRJpA5FCeanA8AdE3jv83wYo0gUgTiDSBSBOIbDz8B6AgEObdEa2kAAAAAElFTkSuQmCC",
      "cup_plant": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAFgAAABdCAYAAAAhWKGHAAACkElEQVR4nO2dP0scQRiH50RExOo4G0UUFEHExmbTRDsRRa0FLSzVLyB+ALGxTPwAEVKrGEK6aOM1NqKFKCgiCIoWSRFSqNiJM6Oznr+92eN5ypfZ2duHl3nnzy5XeDAPBnTUCfsGBOshg8UgWEy9yQFJR4ukEpfPrwtGDBksBsFiECwGwWIQHOssotLKXvZUcFe/TV9njYJk3r7XR88syGAxCBaDYDEIrnaR8xWztq3pim6cjGuWv7FBBotBsBgEi0GwGATX6oZ7m2cWcvHjjxX7e3Ab3G/d2kZwWzbcawCGCDEIFoNgMYXnr065lsX/+rorvklx4XNw4fq/c2jFGgb7gu/lut5H4+GJFWM/OGcwRIhBsBgE52kl5ytGt192g9u6aO4vBvWZlvu5SSuWrLn3qd9b/MhgMQgWg2AxCBaD4GrPIlzLSR/3nplB18qn8F80are9uwi/PM3sJAvIYDEIFoNgMQjOssilWQ6qvvy5HF8POiANPwat7kEoGSwGwWIQLAbBYhBcC69OnS7uBS9pS43NJit4daoGYIgQg2AxCI71K6OioxhNHR07+1g1H0/r6ZUzfuMongtDvc62yW/N58DPIYPFIFgMgsUgWAyCs3wBO80sYvvXshWbmbBjT0wl7eYl38spjooD+/T1+21zydl2bHgp6LmeKPUMWLGkc+TN2QUZLAbBYhAsBsGxFrny2U8rdnO8b2Kk5ChQrxWp0OelyEUAQ4QYBItBsBgE5+lU2Vet84RvqfxeyGAxCBaDYDEIFpOLv3nIEtcesQ9OlSOAIUIMgsUgWAyCq73hnmYj3rUpHQMhG+MqyGAxCBaDYDEIjrXIZfn9cqVkUcx8kMFiECwGwWIQLAbBRssjvhqbfqdGFusAAAAASUVORK5CYII=",
      "monstera": "data:image/png;base64,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",
      "spindly": "data:image/png;base64,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",
      "sprout_pot": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEcAAABMCAYAAAA/d6rcAAAByElEQVR4nO2bsUoDQRCG92IKC0EQRVAktYWt1wj6AsFaSGkjPo+kSRmwFsFesEltYS3BIgg2WtiY+AA3+3OT3YRL+L5y2dxMPmZ3s7u5YhZmoS5lZ69+Z4PR+2eR8vnU+N4cWqnB1hnkCJAjQI6g7Zn4Dh97IYWyW31ubIK0ckiN782ByhEgR4AcAXIEyBEUp53dhawKdfnoDs32JuRA5QiQI0COADne7cP46dvs3Oo/hBSmN5fVZ0b6WjmkxvfmQOUIkCNAjgA53l/IyzwgnxoTZGzyTY0fyyEGlSNAjgA5AuQIkCNoe1aAMsN1bApcBzcIhpUAOQLk5LoO3tncqrRdlUeVtvvR2Az29fsTUrDie3OwvhvXwXPAsBIgR4AcAXIEhfVv0thqdXt+HFadu+e3Shur1RwwrATIESDHu33wTGbrDJUjQI4AOQLkCJCTa7Ua7p+FVac3eandl8oRIEeAHAFyvOc5MUrjnOf67yQ0kcHGq9nO28GZYFgJkCNAjgA5ubYPFhcH26GJDCbpz6ByBMgRIEeAnFzbh7pbimWf+1hnNDnek6ByBMgRIEeAHAFyFrlaNeEdiRwrkwWVI0COADkC5IQ4/9P1dbFwzXm+AAAAAElFTkSuQmCC",
      "wide_planter": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAH4AAABcCAYAAACoRwG1AAACz0lEQVR4nO2dIY/UUBRG22UQEAwEJDuLROwkCBi9YAgCkjEYDHL5A/wG/gArMRgMCQiCWdAb3K5AwoDcBMwGBIEh6LmPvLev0xk458imfW1zcpuvt69tO2tmjfBYW/YByHJQPBTFQ1E8lEHznzIeXshOrXvTw7aBYcVDUTwUxUNRPJTBvxbEctn7+Dp//xvd73/VQ6MVD0XxUBQPRfFQFA+lXdTz+JKkfvrx/ap9vb11d27Z7P3L7O3by7fD5VuvnlUd17cHT1b2DsCKh6J4KIqHongo1eEuFeKilmlJWIoCW4rJwdO5Zc8371VtXzpG7bmNN272GviseCiKh6J4KIqHUhTuoiCXeu4ddc6uf/jeW+ctFdhKQtys8hhSge/NpVNZ20eBr6vQZ8VDUTwUxUNRPBTFQ1mJlu3Ziz86b7n23bKdBGN8/XwyXNeWrSwNL/VQFA9F8VCqW7YlEyVrn7GniEJYF5MtJx20fWue3acma9qylWPjpR6K4qEoHorioYSpvuT1p1/bd8Ll7x5u9zaxYREt31TLNXVnUntuVx/tzC1b23mRPWZp0rfioSgeiuKhKB5Ke214fi7I3RiOwpV3p/vZ4e7M5rmqlm3JK0lHB1+y9l+yfekYtecWHUMq3EV+Ijd/w4qHongoioeieCiKhxJ+y3a0fiJceXeaP3CUUreauqReQu32pWP0eW6RnxI3f7DioSgeiuKhKB5K0Y8KwlZhoq0YtXK7CFyrylHluUXt2VTrvAuseCiKh6J4KIqHongoRak+buUmJm0UzBCVJkzwqdZ5F1jxUBQPRfFQFA8lDHf7n36GK0dhIxVARutXao9NEqT8lGDFQ1E8FMVDUTwUxUMZRC/Uj5vUhxEWNzFA8onek/PDCJKFl3ooioeieCgdPI+Xvil9XSrCioeieCiKh6J4KNU/KpD+8UcFcmy81ENRPBTFQ1F8w+Q3SxzoihyU2zwAAAAASUVORK5CYII=",
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
      $("statusTab").classList.toggle("active", name === "status");
      $("setupTab").classList.toggle("active", name === "setup");
      $("diagnosticsTab").classList.toggle("active", name === "diagnostics");
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
      $("wifiSsidInput").value = configured || connected || "";
      $("wifiStatusText").textContent = `${mode} / ${ip}${connected ? " / " + connected : ""}`;
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
    }

    async function loadCloud() {
      const response = await fetch("/cloud", { cache: "no-store" });
      cloudInfo = await response.json();
      renderCloud();
    }

    function renderCloud() {
      if (!cloudInfo) return;
      $("cloudEnabledInput").value = cloudInfo.enabled ? "true" : "false";
      $("cloudUrlInput").value = cloudInfo.uploadUrl || "";
      $("cloudIntervalInput").value = cloudInfo.syncIntervalSeconds || 900;
      $("cloudTokenInput").value = "";
      const tokenText = cloudInfo.hasApiToken ? "token saved" : "no token";
      const upload = cloudInfo.upload || {};
      const httpText = upload.lastHttpStatus ? `HTTP ${upload.lastHttpStatus}` : "no upload yet";
      $("cloudStatusText").textContent = `${cloudInfo.enabled ? "enabled" : "disabled"} / ${tokenText} / ${httpText}`;
      const lastSuccess = `Last success: ${bootTimeLabel(upload.lastSuccessMs)}`;
      const lastAttempt = `last attempt: ${bootTimeLabel(upload.lastAttemptMs)}`;
      const error = upload.lastError ? ` / ${upload.lastError}` : "";
      $("cloudNotice").textContent = cloudInfo.enabled ? `${lastSuccess}; ${lastAttempt}${error}` : (cloudInfo.note || "Cloud sync is disabled.");
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
        await navigator.clipboard.writeText(value);
        $("claimNotice").textContent = notice;
      } catch (error) {
        $("claimNotice").textContent = value;
      }
    }

    async function saveWifi() {
      const ssid = $("wifiSsidInput").value.trim();
      const password = $("wifiPasswordInput").value;
      if (!ssid) {
        $("wifiNotice").textContent = "Network name is required.";
        return;
      }
      $("wifiNotice").textContent = "Saving WiFi. Receiver will reboot...";
      const response = await fetch("/wifi", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ ssid, password })
      });
      if (!response.ok) {
        $("wifiNotice").textContent = await response.text();
        return;
      }
      $("wifiPasswordInput").value = "";
      $("wifiNotice").textContent = "Saved. Reconnect to the receiver after it reboots.";
    }

    async function forgetWifi() {
      $("wifiNotice").textContent = "Forgetting WiFi. Receiver will reboot into setup mode...";
      const response = await fetch("/wifi", { method: "DELETE" });
      if (!response.ok) {
        $("wifiNotice").textContent = await response.text();
        return;
      }
      $("wifiPasswordInput").value = "";
      $("wifiNotice").textContent = "Forgotten. Connect to MSV2-Display after reboot.";
    }

    async function saveCloud() {
      const enabled = $("cloudEnabledInput").value === "true";
      const uploadUrl = $("cloudUrlInput").value.trim();
      const syncIntervalSeconds = Math.max(60, Math.min(86400, Number($("cloudIntervalInput").value || 900)));
      const apiToken = $("cloudTokenInput").value;
      if (enabled && !uploadUrl) {
        $("cloudNotice").textContent = "Upload URL is required before enabling cloud sync.";
        return;
      }
      $("cloudNotice").textContent = "Saving cloud settings...";
      const body = { enabled, uploadUrl, syncIntervalSeconds };
      if (apiToken) body.apiToken = apiToken;
      const response = await fetch("/cloud", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body)
      });
      if (!response.ok) {
        $("cloudNotice").textContent = await response.text();
        return;
      }
      $("cloudTokenInput").value = "";
      $("cloudNotice").textContent = "Cloud settings saved locally.";
      await loadCloud();
    }

    async function resetCloud() {
      $("cloudNotice").textContent = "Resetting cloud settings...";
      const response = await fetch("/cloud", { method: "DELETE" });
      if (!response.ok) {
        $("cloudNotice").textContent = await response.text();
        return;
      }
      $("cloudTokenInput").value = "";
      $("cloudNotice").textContent = "Cloud settings reset.";
      await loadCloud();
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
                <div class="meta">Probe ${card.sensor || "-"}${reading.rssi ? " / " + reading.rssi + " dBm" : ""}</div>
              </div>
              <div class="art">${artPreview(card.art || "sprout_pot")}<span>${labelFor(artOptions, card.art || "sprout_pot")}</span></div>
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
            <button type="button" class="ghost" data-action="disable" data-index="${index}">Clear</button>
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
      document.querySelectorAll("[data-field]").forEach((input) => {
        const index = Number(input.dataset.index);
        const field = input.dataset.field;
        const card = next.cards[index];
        if (!card.moisture) card.moisture = {};
        if (field === "plant") card.plant = input.value.trim() || ("Probe " + (card.sensor || index + 1));
        if (field === "sensor") card.sensor = Number(input.value || index + 1);
        if (field === "art") card.art = input.value;
        if (field === "dry") card.moisture.dry = Number(input.value || 0);
        if (field === "wet") card.moisture.wet = Number(input.value || 0);
      });
      return next;
    }

    async function saveConfig(event) {
      event.preventDefault();
      config = collectConfig();
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
    $("wifiRefreshButton").addEventListener("click", loadWifi);
    $("wifiSaveButton").addEventListener("click", saveWifi);
    $("wifiForgetButton").addEventListener("click", forgetWifi);
    $("claimRefreshButton").addEventListener("click", loadClaim);
    $("copyReceiverIdButton").addEventListener("click", () => copyText(claimInfo?.receiverId, "Receiver ID copied."));
    $("copyClaimCodeButton").addEventListener("click", () => copyText(claimInfo?.claimCode, "Claim code copied."));
    $("cloudRefreshButton").addEventListener("click", loadCloud);
    $("cloudSaveButton").addEventListener("click", saveCloud);
    $("cloudResetButton").addEventListener("click", resetCloud);
    $("reloadButton").addEventListener("click", async () => { await loadConfig(); await loadReadings(); await loadWifi(); await loadClaim(); await loadCloud(); });
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
      const button = event.target.closest("button[data-action='disable']");
      if (!button) return;
      const index = Number(button.dataset.index);
      config.cards[index].plant = "";
      config.cards[index].sensor = index + 1;
      config.cards[index].art = "sprout_pot";
      config.cards[index].moisture = { dry: 350, wet: 800 };
      renderEditors();
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
const MSV2_CACHE = "msv2-app-v1";
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
