const API_BASE = "https://lyemomfpzkawqecpfufm.supabase.co/functions/v1/plant-dashboard";
const SHARE_TOKEN_KEY = "msv2.householdShareToken";
const SHARE_TOKEN_COOKIE = "msv2_share_token";
const QR_DECODER_URL = "assets/jsQR-1.4.0.min.js";
// Not applied as the loaded <script>'s integrity/crossOrigin attributes anymore -
// those were meaningless (and a future-drift footgun) for a same-origin file under
// this app's script-src 'self' CSP, which already fully trusts same-origin script.
// Kept as a source-level sentinel: scripts/verify_plant_dashboard_qr.mjs asserts this
// exact line matches a freshly computed hash of the vendored file, catching the same
// kind of hash/file drift that broke the decoder once before (2026-08-11).
const QR_DECODER_INTEGRITY = "sha384-hStSInNIZ8ljtOVrmrgf7zdHMapaLBWoSnPTtF0nzsybp4+LuhDz6sHuEVpWIX8o";
const params = new URLSearchParams(window.location.search);
const state = {
  shareToken: loadShareToken(),
  snapshots: [],
  scanStream: null,
  scanFrame: 0,
  scanSession: 0,
  scanLastAt: 0,
  qrDecoderReady: false,
  qrDecoderPromise: null,
};

const byId = (id) => document.getElementById(id);
const setNotice = (text, type = "") => {
  const node = byId("connectNotice");
  node.textContent = text;
  node.className = "notice" + (type ? " " + type : "");
};
const escapeHtml = (value) => String(value ?? "")
  .replaceAll("&", "&amp;")
  .replaceAll("<", "&lt;")
  .replaceAll(">", "&gt;")
  .replaceAll('"', "&quot;");

function readCookie(name) {
  const prefix = encodeURIComponent(name) + "=";
  for (const part of document.cookie.split(";")) {
    const value = part.trim();
    if (value.startsWith(prefix)) return decodeURIComponent(value.slice(prefix.length));
  }
  return "";
}

function writeCookie(name, value) {
  document.cookie = encodeURIComponent(name) + "=" + encodeURIComponent(value) + "; Max-Age=31536000; Path=/; SameSite=Lax; Secure";
}

function clearCookie(name) {
  document.cookie = encodeURIComponent(name) + "=; Max-Age=0; Path=/; SameSite=Lax; Secure";
}

function loadShareToken() {
  const token = localStorage.getItem(SHARE_TOKEN_KEY) || readCookie(SHARE_TOKEN_COOKIE) || "";
  if (token) {
    localStorage.setItem(SHARE_TOKEN_KEY, token);
    writeCookie(SHARE_TOKEN_COOKIE, token);
  }
  return token;
}

function saveShareToken(token) {
  state.shareToken = token;
  localStorage.setItem(SHARE_TOKEN_KEY, token);
  writeCookie(SHARE_TOKEN_COOKIE, token);
}

function clearShareToken() {
  state.shareToken = "";
  localStorage.removeItem(SHARE_TOKEN_KEY);
  clearCookie(SHARE_TOKEN_COOKIE);
}

function applyPairingParams() {
  const receiverId = params.get("receiverId") || params.get("rid") || "";
  const claimCode = params.get("claimCode") || params.get("code") || "";
  if (receiverId) byId("receiverIdInput").value = receiverId;
  if (claimCode) byId("claimCodeInput").value = claimCode;
  byId("claimPill").hidden = !(receiverId && claimCode);
}

function applyPairingLink(value, autoConnect = false) {
  const text = String(value || "").trim();
  if (!text) return false;
  let parsed = null;
  try {
    parsed = new URL(text, window.location.href);
  } catch (error) {
    const query = text.includes("?") ? text.slice(text.indexOf("?")) : "?" + text.replace(/^[?#]/, "");
    parsed = new URL(query, window.location.href);
  }
  const linkParams = parsed.searchParams;
  const receiverId = linkParams.get("receiverId") || linkParams.get("rid") || "";
  const claimCode = linkParams.get("claimCode") || linkParams.get("code") || "";
  if (!receiverId || !claimCode) {
    setNotice("That link does not include receiver pairing details.", "error");
    return false;
  }
  byId("receiverIdInput").value = receiverId;
  byId("claimCodeInput").value = claimCode;
  byId("claimUrlInput").value = text;
  byId("claimPill").hidden = false;
  setNotice("Pairing details loaded in this app.", "ok");
  if (autoConnect) connectHousehold().catch(() => setNotice("Could not connect from that link. Tap Connect to try again.", "error"));
  return true;
}

function hasPairingParams() {
  return Boolean(
    (params.get("receiverId") || params.get("rid")) &&
    (params.get("claimCode") || params.get("code"))
  );
}

function shouldAutoConnect() {
  const value = (params.get("autoConnect") || params.get("connect") || "").toLowerCase();
  return hasPairingParams() && (value === "1" || value === "true" || value === "yes");
}

function renderLinkState() {
  const connected = Boolean(state.shareToken);
  byId("linkPill").textContent = connected ? "Household connected" : "No household link";
  byId("forgetButton").disabled = !state.shareToken;
  byId("connectPanel").hidden = connected;
  document.body.classList.toggle("connected", connected);
}

async function connectHousehold() {
  const receiverId = byId("receiverIdInput").value.trim();
  const claimCode = byId("claimCodeInput").value.trim();
  const householdName = byId("householdInput").value.trim() || "My Plants";
  setNotice("Connecting household...");
  const response = await fetch(API_BASE + "/api/share-claim", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ receiverId, claimCode, householdName }),
  });
  const body = await response.json();
  if (!response.ok || !body.ok || !body.share?.share_token) {
    setNotice(body.error || "Could not connect household.", "error");
    return;
  }
  saveShareToken(body.share.share_token);
  setNotice("Household connected on this phone.", "ok");
  renderLinkState();
  await loadSnapshots();
}

async function loadSnapshots() {
  renderLinkState();
  if (!state.shareToken) {
    byId("summary").innerHTML = "";
    byId("receivers").className = "empty";
    byId("receivers").innerHTML = "<h2>No household connected</h2><p>Connect with the receiver QR code to show cloud plant data here.</p>";
    return;
  }
  const response = await fetch(API_BASE + "/api/share-snapshots", {
    cache: "no-store",
    headers: { "Authorization": "Bearer " + state.shareToken },
  });
  const body = await response.json();
  if (!response.ok || !body.ok) {
    clearShareToken();
    renderLinkState();
    byId("receivers").className = "empty";
    byId("receivers").innerHTML = "<h2>Could not load plants</h2><p>" + escapeHtml(body.error || "Unknown error") + "</p>";
    setNotice("Household link expired or could not be loaded. Connect again with the receiver QR code.", "error");
    return;
  }
  state.snapshots = body.snapshots || [];
  renderDashboard();
}

async function forgetHousehold() {
  const token = state.shareToken;
  state.snapshots = [];
  clearShareToken();
  setNotice("Household link removed from this phone.");
  loadSnapshots();
  if (token) {
    try {
      await fetch(API_BASE + "/api/share-revoke", {
        method: "POST",
        headers: { "Authorization": "Bearer " + token },
      });
    } catch (error) {
      // Best-effort: the token is already forgotten locally either way.
    }
  }
}

async function startQrScan() {
  stopQrScan();
  const scanSession = state.scanSession;
  if (!navigator.mediaDevices?.getUserMedia) {
    setNotice("Live camera scanning is not supported here. Use Scan Photo or paste the app link instead.", "error");
    return;
  }

  let stream;
  try {
    stream = await navigator.mediaDevices.getUserMedia({
      audio: false,
      video: { facingMode: { ideal: "environment" } },
    });
  } catch (error) {
    stopQrScan();
    console.error("Could not start QR camera", error);
    setNotice(cameraErrorMessage(error), "error");
    return;
  }

  // A newer startQrScan()/stopQrScan() call can supersede this one while
  // getUserMedia() was pending (e.g. a fast double-tap on Scan QR). Stop the stream
  // this call just obtained directly, rather than assigning it to state.scanStream
  // and calling stopQrScan() - that would only stop whatever state.scanStream
  // CURRENTLY holds, which by now may belong to the newer, still-active call,
  // leaking this call's camera hardware (and its "on" indicator) indefinitely
  // instead of the stream that's actually still in use.
  if (scanSession !== state.scanSession) {
    for (const track of stream.getTracks()) track.stop();
    return;
  }
  state.scanStream = stream;
  const video = byId("scanVideo");
  video.srcObject = state.scanStream;
  byId("scanBox").hidden = false;
  setNotice("Camera ready. Starting the QR reader...");

  try {
    await video.play();
  } catch (error) {
    stopQrScan();
    console.error("Could not play QR camera preview", error);
    setNotice("The camera opened, but its preview could not start. Close other camera apps and try again.", "error");
    return;
  }

  let detector = null;
  try {
    detector = await createBarcodeDetector();
    if (!detector) await loadQrDecoder();
  } catch (error) {
    stopQrScan();
    console.error("Could not load QR decoder", error);
    setNotice("The camera works, but the QR reader could not load. Reload the app or use Scan Photo.", "error");
    return;
  }

  if (scanSession !== state.scanSession) return;
  setNotice("Point the camera at the receiver pairing QR code.");
  const scan = async (timestamp) => {
    if (scanSession !== state.scanSession) return;
    if (timestamp - state.scanLastAt < 100) {
      state.scanFrame = requestAnimationFrame(scan);
      return;
    }
    state.scanLastAt = timestamp;
    try {
      const value = detector ? await scanWithBarcodeDetector(detector, video) : scanWithJsQr(video);
      if (value && applyPairingLink(value, true)) {
        stopQrScan();
        return;
      }
    } catch (error) {
      stopQrScan();
      console.error("QR reader stopped", error);
      setNotice("The QR reader stopped unexpectedly. Try again or use Scan Photo.", "error");
      return;
    }
    if (scanSession === state.scanSession) state.scanFrame = requestAnimationFrame(scan);
  };
  state.scanFrame = requestAnimationFrame(scan);
}

function cameraErrorMessage(error) {
  switch (error?.name) {
    case "NotAllowedError":
    case "SecurityError":
      return "Camera access is blocked. In Safari Website Settings, set Camera to Allow, then reopen this app.";
    case "NotFoundError":
      return "No camera was found. Use Scan Photo or paste the app link instead.";
    case "NotReadableError":
    case "AbortError":
      return "The camera is busy or unavailable. Close other camera apps, reopen this app, and try again.";
    default:
      return "The camera could not start. Use Scan Photo or paste the app link instead.";
  }
}

async function createBarcodeDetector() {
  if (!("BarcodeDetector" in window)) return null;
  try {
    if (typeof BarcodeDetector.getSupportedFormats === "function") {
      const formats = await BarcodeDetector.getSupportedFormats();
      if (!formats.includes("qr_code")) return null;
    }
    return new BarcodeDetector({ formats: ["qr_code"] });
  } catch (error) {
    return null;
  }
}

async function scanWithBarcodeDetector(detector, video) {
  const codes = await detector.detect(video);
  return codes[0]?.rawValue || "";
}

async function loadQrDecoder() {
  if (typeof window.jsQR === "function") {
    state.qrDecoderReady = true;
    return;
  }
  if (state.qrDecoderPromise) return state.qrDecoderPromise;
  state.qrDecoderPromise = new Promise((resolve, reject) => {
    const script = document.createElement("script");
    script.src = QR_DECODER_URL;
    script.async = true;
    script.onload = () => {
      if (typeof window.jsQR !== "function") {
        reject(new Error("QR decoder loaded without exposing jsQR."));
        return;
      }
      state.qrDecoderReady = true;
      resolve();
    };
    script.onerror = () => {
      script.remove();
      reject(new Error("QR decoder script failed to load."));
    };
    document.head.appendChild(script);
  });
  try {
    await state.qrDecoderPromise;
  } catch (error) {
    state.qrDecoderPromise = null;
    state.qrDecoderReady = false;
    throw error;
  }
}

function scanWithJsQr(video) {
  if (!window.jsQR || !video.videoWidth || !video.videoHeight) return "";
  const canvas = scanWithJsQr.canvas || (scanWithJsQr.canvas = document.createElement("canvas"));
  const context = canvas.getContext("2d", { willReadFrequently: true });
  const sourceSize = Math.min(video.videoWidth, video.videoHeight);
  const targetSize = Math.min(sourceSize, 900);
  const sx = Math.max(0, Math.floor((video.videoWidth - sourceSize) / 2));
  const sy = Math.max(0, Math.floor((video.videoHeight - sourceSize) / 2));
  canvas.width = targetSize;
  canvas.height = targetSize;
  context.drawImage(video, sx, sy, sourceSize, sourceSize, 0, 0, targetSize, targetSize);
  const image = context.getImageData(0, 0, targetSize, targetSize);
  const code = window.jsQR(image.data, image.width, image.height, { inversionAttempts: "dontInvert" });
  return code?.data || "";
}

async function scanQrPhoto(event) {
  const input = event.currentTarget;
  const file = input.files?.[0] || null;
  input.value = "";
  if (!file) return;
  stopQrScan();
  setNotice("Reading QR code from photo...");
  try {
    await loadQrDecoder();
    const image = await loadQrImage(file);
    const value = scanImageWithJsQr(image);
    if (!value) {
      setNotice("No QR code was found in that photo. Try a closer, sharper picture.", "error");
      return;
    }
    if (!applyPairingLink(value, true)) {
      setNotice("That photo contains a QR code, but it is not an MSV2 pairing link.", "error");
    }
  } catch (error) {
    console.error("Could not read QR photo", error);
    setNotice("The QR photo could not be read. Try another photo or paste the app link.", "error");
  }
}

function loadQrImage(file) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    const url = URL.createObjectURL(file);
    image.onload = () => {
      URL.revokeObjectURL(url);
      resolve(image);
    };
    image.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error("Selected QR image could not be decoded."));
    };
    image.src = url;
  });
}

function scanImageWithJsQr(image) {
  const width = image.naturalWidth || image.width;
  const height = image.naturalHeight || image.height;
  if (!width || !height) return "";
  const maxDimension = 2048;
  const scale = Math.min(1, maxDimension / Math.max(width, height));
  const targetWidth = Math.max(1, Math.round(width * scale));
  const targetHeight = Math.max(1, Math.round(height * scale));
  const canvas = scanImageWithJsQr.canvas || (scanImageWithJsQr.canvas = document.createElement("canvas"));
  const context = canvas.getContext("2d", { willReadFrequently: true });
  canvas.width = targetWidth;
  canvas.height = targetHeight;
  context.drawImage(image, 0, 0, targetWidth, targetHeight);
  const pixels = context.getImageData(0, 0, targetWidth, targetHeight);
  return window.jsQR(pixels.data, pixels.width, pixels.height, { inversionAttempts: "attemptBoth" })?.data || "";
}

function stopQrScan() {
  state.scanSession += 1;
  if (state.scanFrame) cancelAnimationFrame(state.scanFrame);
  state.scanFrame = 0;
  state.scanLastAt = 0;
  if (state.scanStream) {
    for (const track of state.scanStream.getTracks()) track.stop();
  }
  state.scanStream = null;
  byId("scanVideo").srcObject = null;
  byId("scanBox").hidden = true;
}

function renderDashboard() {
  const rows = state.snapshots;
  byId("summary").innerHTML = renderSummary(rows);
  byId("receivers").className = rows.length ? "" : "empty";
  byId("receivers").innerHTML = rows.length
    ? rows.map(renderReceiver).join("")
    : "<h2>No snapshots yet</h2><p>The receiver is connected. Waiting for its next cloud upload.</p>";
}

function renderSummary(rows) {
  let plantCount = 0;
  let dryCount = 0;
  let lowBatteryCount = 0;
  for (const row of rows) {
    const plants = displayPlants(row.snapshot || {});
    plantCount += plants.length;
    for (const plant of plants) {
      if (!plant.reading) continue;
      const { healthCls } = resolveMoisture(plant.card, plant.reading);
      if (healthCls === "danger") dryCount += 1;
      if (typeof plant.reading.batteryPercent === "number" && plant.reading.batteryPercent < 20) lowBatteryCount += 1;
    }
  }
  return '<section class="summary" aria-label="Garden summary">' +
    summaryCell("Receivers", rows.length) +
    summaryCell("Plants", plantCount) +
    summaryCell("Dry", dryCount, dryCount ? "danger" : "ok") +
    summaryCell("Low Battery", lowBatteryCount, lowBatteryCount ? "danger" : "ok") +
  '</section>';
}

function summaryCell(label, value, className = "") {
  return '<div class="summary-cell"><span class="summary-label">' + escapeHtml(label) + '</span><span class="summary-value ' + escapeHtml(className) + '">' + escapeHtml(value) + '</span></div>';
}

function renderReceiver(row) {
  const snapshot = row.snapshot || {};
  const config = snapshot.config || {};
  const title = config.title || row.household_name || "MSV2 Plants";
  const plants = displayPlants(snapshot);
  const plantCards = plants.length ? plants.map(renderPlant).join("") : "<p>No plant setup in this snapshot.</p>";
  return '<section class="receiver"><h2>' + escapeHtml(title) + '</h2><div class="meta">' +
    '<span>Household: ' + escapeHtml(row.household_name || "My Plants") + '</span>' +
    '<span>Receiver: <code>' + escapeHtml(row.receiver_id) + '</code></span>' +
    '<span>Config v' + escapeHtml(config.version ?? row.config_version ?? "--") + '</span>' +
    '<span>Cloud: ' + escapeHtml(formatDate(row.received_at)) + '</span>' +
    '</div><div class="display-board">' + plantCards + '</div></section>';
}

function renderPlant(plant) {
  const card = plant.card || {};
  const reading = plant.reading || {};
  const name = card.plant || "Probe " + (plant.sensor ?? "--");
  const { pct, healthCls: moistureClass } = resolveMoisture(card, reading);
  const battery = reading.batteryPercent;
  const batteryClass = typeof battery === "number" && battery < 20 ? "danger" : typeof battery === "number" && battery < 50 ? "warn" : "ok";
  const art = card.art || "sprout_pot";
  return '<article class="plant"><div class="plant-head"><div class="plant-title"><h3 class="plant-name">' + escapeHtml(name) + '</h3><div class="meta">Probe ' + escapeHtml(plant.sensor ?? "--") + '</div></div><img class="plant-art" src="' + escapeHtml(artSrc(art)) + '" alt=""></div>' +
    metric("Moisture", String(reading.moisture ?? "--") + " raw / " + String(pct ?? "--") + "%", moistureClass) +
    bar(pct, moistureClass) +
    metric("Battery", battery == null ? "--" : String(battery) + "%", batteryClass) +
    metric("RSSI", String(reading.rssi ?? "--") + " dBm") +
    metric("Age", formatAge(reading.ageMs)) +
    metric("Packets", String(reading.received ?? "--") + " rx / " + String(reading.missedTotal ?? "--") + " missed") +
    '</article>';
}

function displayPlants(snapshot) {
  const cards = Array.isArray(snapshot.config?.cards) ? snapshot.config.cards : [];
  const readings = readingsBySensor(snapshot.plants || []);
  if (cards.length) {
    const usedSensors = new Set();
    const plants = cards.map((card, index) => {
      const sensor = Number(card.sensor || index + 1);
      usedSensors.add(sensor);
      return { sensor, card, reading: readings.get(sensor) || null };
    });
    for (const reading of readings.values()) {
      const sensor = Number(reading.sensor || 0);
      if (sensor && !usedSensors.has(sensor)) {
        plants.push({ sensor, card: { sensor, plant: "Unassigned", art: "sprout_pot" }, reading });
      }
    }
    return plants;
  }
  return Array.from(readings.values()).map((reading) => ({
    sensor: Number(reading.sensor || 0),
    card: { sensor: reading.sensor, plant: "Probe " + (reading.sensor ?? "--"), art: "sprout_pot" },
    reading,
  }));
}

function cardsBySensor(cards) {
  const result = new Map();
  for (const card of cards) if (typeof card.sensor === "number") result.set(card.sensor, card);
  return result;
}

function readingsBySensor(readings) {
  const result = new Map();
  for (const reading of readings) {
    const sensor = Number(reading?.sensor || 0);
    if (sensor) result.set(sensor, reading);
  }
  return result;
}

function artSrc(key) {
  const safe = String(key || "sprout_pot").replace(/[^a-z0-9_]/g, "");
  const known = [
    "cactus_cluster", "cup_plant", "monstera", "spindly", "sprout_pot", "wide_planter",
    "color_bonsai_bowl", "color_grass_planter", "color_hanging_clump", "color_hanging_vine",
    "color_round_pot", "color_vine_pot", "color_zigzag_pot",
  ];
  return "assets/plant_gifs/" + (known.includes(safe) ? safe : "sprout_pot") + ".gif";
}

function percent(value, dry, wet) {
  if (typeof value !== "number" || wet === dry) return null;
  return Math.max(0, Math.min(100, Math.round(((value - dry) * 100) / (wet - dry))));
}

function healthClass(pct) {
  if (pct == null) return "";
  if (pct < 25) return "danger";
  if (pct < 50) return "warn";
  return "ok";
}

// Mirrors the firmware's refreshSlotMoistureBounds() (see MSV2_ESP32_Display_Runtime.ino)
// and the local app's renderStatus() (MobileAppPage.h) so this dashboard's colors never
// diverge from the physical display's: once a sensor's auto-calibration is ready, the
// learned dry/wet bounds replace the static manual/preset ones. (This used to also force
// red on a calendar-based "overdue" watering-schedule prediction - removed 2026-08-17,
// see docs/STATUS_HANDOFF.md - so color now comes from the live reading alone.)
function resolveMoisture(card, reading) {
  const autoEnabled = Boolean(card?.moisture?.auto);
  const autoCal = reading?.autoCal || null;
  const autoReady = Boolean(autoEnabled && autoCal && autoCal.ready);
  const dry = autoReady ? autoCal.dryBound : Number(card?.moisture?.dry ?? 120);
  const wet = autoReady ? autoCal.wetBound : Number(card?.moisture?.wet ?? 175);
  const pct = percent(reading?.moisture, dry, wet);
  return { pct, healthCls: healthClass(pct) };
}

function bar(pct, className) {
  return '<div class="bar ' + escapeHtml(className) + '" style="--pct:' + String(pct ?? 0) + '%"><span></span></div>';
}

function metric(label, value, className = "") {
  return '<div class="metric"><span>' + escapeHtml(label) + '</span><span class="' + escapeHtml(className) + '">' + escapeHtml(value) + '</span></div>';
}

function formatAge(value) {
  if (typeof value !== "number") return "--";
  const seconds = Math.max(0, Math.round(value / 1000));
  if (seconds < 60) return seconds + "s";
  const minutes = Math.floor(seconds / 60);
  return minutes + "m " + String(seconds % 60) + "s";
}

function formatDate(value) {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? "--" : date.toLocaleString();
}

byId("connectButton").addEventListener("click", () => {
  // Disabled for the duration of the request so a slow/failed attempt plus an
  // impatient re-click can't fire two share-claim POSTs.
  const button = byId("connectButton");
  button.disabled = true;
  connectHousehold()
    .catch((error) => setNotice("Could not connect household: " + error.message, "error"))
    .finally(() => { button.disabled = false; });
});
byId("useClaimUrlButton").addEventListener("click", () => applyPairingLink(byId("claimUrlInput").value, false));
byId("scanQrButton").addEventListener("click", startQrScan);
byId("scanPhotoButton").addEventListener("click", () => byId("scanPhotoInput").click());
byId("scanPhotoInput").addEventListener("change", scanQrPhoto);
byId("stopScanButton").addEventListener("click", stopQrScan);
byId("refreshButton").addEventListener("click", () => {
  loadSnapshots().catch((error) => setNotice("Could not refresh plants: " + error.message, "error"));
});
byId("forgetButton").addEventListener("click", forgetHousehold);
window.addEventListener("pagehide", stopQrScan);
document.addEventListener("visibilitychange", () => {
  if (document.hidden) stopQrScan();
});

applyPairingParams();
renderLinkState();
if (!state.shareToken && shouldAutoConnect()) {
  connectHousehold().catch(() => {
    setNotice("Could not auto-connect from the QR link. Tap Connect to try again.", "error");
  });
} else {
  loadSnapshots().catch((error) => setNotice("Could not refresh plants: " + error.message, "error"));
}
if ("serviceWorker" in navigator && window.isSecureContext) {
  navigator.serviceWorker.register("sw.js").then((registration) => registration.update()).catch(() => {});
}
window.setInterval(() => loadSnapshots().catch((error) => console.warn("Automatic refresh failed", error)), 30000);
