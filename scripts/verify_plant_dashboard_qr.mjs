import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { createServer } from "node:http";
import { mkdtemp, readFile, rm, stat } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, extname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(scriptDir, "..");
const dashboardRoot = join(repoRoot, "web", "plant-dashboard");
const qrFixture = join(scriptDir, "fixtures", "msv2-pairing-qr.png");
const expectedPairing = {
  receiverId: "MSV2TEST123",
  claimCode: "TEST-1234",
};

const edgeCandidates = [
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
  "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
];

const contentTypes = new Map([
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".webmanifest", "application/manifest+json; charset=utf-8"],
  [".svg", "image/svg+xml"],
  [".gif", "image/gif"],
  [".png", "image/png"],
]);

const delay = (milliseconds) => new Promise((resolveDelay) => setTimeout(resolveDelay, milliseconds));

async function existingBrowser() {
  for (const candidate of edgeCandidates) {
    try {
      await stat(candidate);
      return candidate;
    } catch (error) {
      // Keep looking.
    }
  }
  throw new Error("Microsoft Edge or Google Chrome is required for this verification.");
}

async function freePort() {
  const probe = createServer();
  await new Promise((resolveListen, rejectListen) => {
    probe.once("error", rejectListen);
    probe.listen(0, "127.0.0.1", resolveListen);
  });
  const { port } = probe.address();
  await new Promise((resolveClose) => probe.close(resolveClose));
  return port;
}

function startDashboardServer() {
  const server = createServer(async (request, response) => {
    try {
      const url = new URL(request.url || "/", "http://127.0.0.1");
      const requestedPath = url.pathname === "/" ? "index.html" : decodeURIComponent(url.pathname.slice(1));
      const filePath = resolve(dashboardRoot, requestedPath);
      const relativePath = relative(dashboardRoot, filePath);
      if (relativePath.startsWith("..") || isAbsolute(relativePath)) {
        response.writeHead(403).end("Forbidden");
        return;
      }
      const body = await readFile(filePath);
      const headers = {
        "Content-Type": contentTypes.get(extname(filePath)) || "application/octet-stream",
        "Content-Security-Policy": "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self' https://lyemomfpzkawqecpfufm.supabase.co; base-uri 'none'; frame-ancestors 'none'; object-src 'none'",
        "Permissions-Policy": "camera=(self), microphone=()",
        "X-Content-Type-Options": "nosniff",
      };
      headers["Cache-Control"] = url.pathname.startsWith("/assets/")
        ? "public, max-age=31536000, immutable"
        : "no-store";
      response.writeHead(200, headers).end(body);
    } catch (error) {
      response.writeHead(error?.code === "ENOENT" ? 404 : 500).end("Not found");
    }
  });
  return server;
}

async function waitForDebugTarget(debugPort, pageUrl, timeout = 15000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    try {
      const targets = await fetch(`http://127.0.0.1:${debugPort}/json/list`).then((response) => response.json());
      const target = targets.find((candidate) => candidate.type === "page" && candidate.url.startsWith(pageUrl));
      if (target?.webSocketDebuggerUrl) return target;
    } catch (error) {
      // Browser startup is still in progress.
    }
    await delay(100);
  }
  throw new Error("Timed out waiting for the headless browser.");
}

async function connectCdp(webSocketUrl) {
  const socket = new WebSocket(webSocketUrl);
  await new Promise((resolveOpen, rejectOpen) => {
    socket.addEventListener("open", resolveOpen, { once: true });
    socket.addEventListener("error", rejectOpen, { once: true });
  });
  let nextId = 1;
  const pending = new Map();
  const exceptions = [];
  socket.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
    if (message.id && pending.has(message.id)) {
      const { resolveCommand, rejectCommand } = pending.get(message.id);
      pending.delete(message.id);
      if (message.error) rejectCommand(new Error(message.error.message));
      else resolveCommand(message.result);
      return;
    }
    if (message.method === "Runtime.exceptionThrown") {
      exceptions.push(message.params.exceptionDetails.text || "Uncaught browser exception");
    }
  });
  const command = (method, params = {}) => new Promise((resolveCommand, rejectCommand) => {
    const id = nextId++;
    pending.set(id, { resolveCommand, rejectCommand });
    socket.send(JSON.stringify({ id, method, params }));
  });
  return { socket, command, exceptions };
}

async function main() {
  const browserPath = await existingBrowser();
  await stat(qrFixture);
  const appSource = await readFile(join(dashboardRoot, "assets", "app.v12.js"), "utf8");
  const decoderBytes = await readFile(join(dashboardRoot, "assets", "jsQR-1.4.0.min.js"));
  const decoderLicense = await readFile(join(dashboardRoot, "assets", "jsQR-1.4.0.LICENSE.txt"), "utf8");
  const indexSource = await readFile(join(dashboardRoot, "index.html"), "utf8");
  const workerSource = await readFile(join(dashboardRoot, "sw.js"), "utf8");
  const headerSource = await readFile(join(dashboardRoot, "_headers"), "utf8");
  const localNetlifySource = await readFile(join(dashboardRoot, "netlify.toml"), "utf8");
  const rootNetlifySource = await readFile(join(repoRoot, "netlify.toml"), "utf8");
  const decoderIntegrity = `sha384-${createHash("sha384").update(decoderBytes).digest("base64")}`;
  const expectedIntegrity = "sha384-hStSInNIZ8ljtOVrmrgf7zdHMapaLBWoSnPTtF0nzsybp4+LuhDz6sHuEVpWIX8o";
  assert.equal(decoderIntegrity, expectedIntegrity, "Vendored decoder hash must remain pinned.");
  assert.ok(appSource.includes(`const QR_DECODER_INTEGRITY = "${expectedIntegrity}"`), "App SRI must match the vendored decoder.");
  assert.ok(indexSource.includes('src="assets/app.v12.js"'), "HTML must reference the versioned app.");
  assert.ok(workerSource.includes('"msv2-cloud-app-v12"'), "Service-worker cache must be v12.");
  assert.ok(workerSource.includes('"/assets/app.v12.js"') && workerSource.includes('"/assets/jsQR-1.4.0.min.js"'), "Service worker must precache both scanner assets.");
  assert.match(decoderLicense, /Apache License[\s\S]*Version 2\.0/, "Vendored decoder license must be present.");
  for (const configSource of [headerSource, localNetlifySource, rootNetlifySource]) {
    assert.ok(configSource.includes("camera=(self), microphone=()"), "Every Netlify header source must allow only same-origin camera use.");
    assert.ok(!configSource.includes("cdn.jsdelivr.net"), "Production CSP must not retain the decoder CDN.");
  }
  console.log("CHECK static scanner assets and deployment configs are consistent");
  const server = startDashboardServer();
  await new Promise((resolveListen, rejectListen) => {
    server.once("error", rejectListen);
    server.listen(0, "127.0.0.1", resolveListen);
  });
  const serverPort = server.address().port;
  const pageUrl = `http://127.0.0.1:${serverPort}/`;
  const debugPort = await freePort();
  const userDataDir = await mkdtemp(join(tmpdir(), "msv2-dashboard-test-"));
  const browser = spawn(browserPath, [
    "--headless=new",
    "--disable-gpu",
    "--no-first-run",
    "--no-default-browser-check",
    "--use-fake-ui-for-media-stream",
    "--use-fake-device-for-media-stream",
    "--autoplay-policy=no-user-gesture-required",
    `--remote-debugging-port=${debugPort}`,
    `--user-data-dir=${userDataDir}`,
    pageUrl,
  ], { stdio: ["ignore", "ignore", "pipe"], windowsHide: true });
  let browserStderr = "";
  browser.stderr.on("data", (chunk) => {
    browserStderr = (browserStderr + chunk.toString()).slice(-4000);
  });

  let cdp = null;
  try {
    let target = null;
    try {
      target = await waitForDebugTarget(debugPort, pageUrl);
    } catch (error) {
      throw new Error(`${error.message}${browserStderr ? `\nBrowser output:\n${browserStderr}` : ""}`);
    }
    cdp = await connectCdp(target.webSocketDebuggerUrl);
    const { command, exceptions } = cdp;
    await command("Runtime.enable");
    await command("DOM.enable");
    await command("Page.enable");
    await command("Emulation.setDeviceMetricsOverride", {
      width: 390,
      height: 844,
      deviceScaleFactor: 3,
      mobile: true,
    });
    await command("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
    await command("Page.reload", { ignoreCache: true });
    await delay(200);
    console.log("CHECK headless browser connected");

    const evaluate = async (expression, awaitPromise = false) => {
      const result = await command("Runtime.evaluate", {
        expression,
        awaitPromise,
        returnByValue: true,
      });
      if (result.exceptionDetails) throw new Error(result.exceptionDetails.text);
      return result.result.value;
    };

    const waitFor = async (expression, description, timeout = 10000) => {
      const deadline = Date.now() + timeout;
      while (Date.now() < deadline) {
        try {
          if (await evaluate(expression, true)) return;
        } catch (error) {
          // A reload can briefly destroy the current JavaScript execution context.
        }
        await delay(100);
      }
      throw new Error(`Timed out waiting for ${description}.`);
    };

    const click = async (selector) => {
      const result = await command("Runtime.evaluate", {
        expression: `document.querySelector(${JSON.stringify(selector)}).click()`,
        awaitPromise: true,
        returnByValue: true,
        userGesture: true,
      });
      if (result.exceptionDetails) throw new Error(result.exceptionDetails.text);
    };

    await waitFor("document.readyState === 'complete' && Boolean(document.getElementById('scanQrButton'))", "dashboard load");
    assert.equal(await evaluate("window.isSecureContext"), true, "Loopback test page must be a secure context.");
    assert.equal(await evaluate("typeof window.jsQR"), "undefined", "QR decoder should remain lazy before scanning.");
    assert.equal(
      await evaluate("document.documentElement.scrollWidth <= document.documentElement.clientWidth"),
      true,
      "Mobile layout must not overflow horizontally.",
    );
    console.log("CHECK dashboard loaded");

    await waitFor(
      "navigator.serviceWorker.getRegistration().then((value) => Boolean(value?.active))",
      "active service worker",
      15000,
    );
    const registration = await evaluate("navigator.serviceWorker.getRegistration().then((value) => ({ scope: value.scope }))", true);
    assert.equal(registration.scope, pageUrl, "Service worker scope should cover the dashboard.");
    await waitFor("navigator.serviceWorker.controller !== null", "service-worker control");
    const cachedAssets = await evaluate(
      "caches.open('msv2-cloud-app-v12').then(async (cache) => ({ app: Boolean(await cache.match('/assets/app.v12.js')), decoder: Boolean(await cache.match('/assets/jsQR-1.4.0.min.js')) }))",
      true,
    );
    assert.deepEqual(cachedAssets, { app: true, decoder: true }, "Versioned app and decoder should be precached.");
    console.log("CHECK service worker cache ready");

    await evaluate(`
      Object.defineProperty(window, "BarcodeDetector", { value: undefined, configurable: true });
      window.__scanOrder = [];
      window.__nativeGetUserMedia = navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
      navigator.mediaDevices.getUserMedia = (...args) => {
        window.__scanOrder.push("camera");
        return window.__nativeGetUserMedia(...args);
      };
      const nativeAppendChild = document.head.appendChild.bind(document.head);
      document.head.appendChild = (node) => {
        if (node?.src?.includes("jsQR-1.4.0.min.js")) window.__scanOrder.push("decoder");
        return nativeAppendChild(node);
      };
    `);
    await click("#scanQrButton");
    console.log("CHECK camera request dispatched");
    try {
      await waitFor(
        "typeof window.jsQR === 'function' && Boolean(document.getElementById('scanVideo').srcObject) && document.getElementById('scanBox').hidden === false",
        "camera and QR decoder startup",
        15000,
      );
    } catch (error) {
      const diagnostics = await evaluate(`({
        notice: document.getElementById('connectNotice').textContent,
        order: window.__scanOrder,
        decoderType: typeof window.jsQR,
        hasStream: Boolean(document.getElementById('scanVideo').srcObject),
        scanHidden: document.getElementById('scanBox').hidden,
      })`);
      throw new Error(`${error.message} ${JSON.stringify(diagnostics)}`);
    }
    assert.deepEqual(await evaluate("window.__scanOrder"), ["camera", "decoder"], "Camera must be requested before decoder loading.");
    assert.equal(
      await evaluate("performance.getEntriesByType('resource').some((entry) => new URL(entry.name).pathname === '/assets/jsQR-1.4.0.min.js')"),
      true,
      "QR decoder must load from the local origin.",
    );
    await click("#stopScanButton");
    await waitFor(
      "document.getElementById('scanVideo').srcObject === null && document.getElementById('scanBox').hidden === true",
      "camera shutdown",
    );
    console.log("CHECK camera scan started and stopped");

    await evaluate(`
      navigator.mediaDevices.getUserMedia = () => Promise.reject(new DOMException("Denied for local test", "NotAllowedError"));
    `);
    await click("#scanQrButton");
    await waitFor(
      "document.getElementById('connectNotice').textContent.includes('Camera access is blocked')",
      "permission-denied guidance",
    );
    console.log("CHECK denied-permission guidance shown");

    await evaluate(`
      window.__cancelledTrackStops = 0;
      navigator.mediaDevices.getUserMedia = () => new Promise((resolveCamera) => {
        window.__resolvePendingCamera = () => resolveCamera({
          getTracks: () => [{ stop: () => { window.__cancelledTrackStops += 1; } }],
        });
      });
    `);
    await click("#scanQrButton");
    await waitFor("typeof window.__resolvePendingCamera === 'function'", "pending camera request");
    await evaluate("window.dispatchEvent(new PageTransitionEvent('pagehide')); window.__resolvePendingCamera()");
    await waitFor("window.__cancelledTrackStops === 1", "late camera stream cleanup");
    console.log("CHECK camera stream resolved after page hide was released");

    await evaluate(`
      state.snapshots = [{
        receiver_id: "MSV2DISPLAY1",
        household_name: "Local Test",
        config_version: 3,
        received_at: new Date().toISOString(),
        snapshot: {
          config: { title: "Display Test", version: 3, cards: [] },
          display: { brightnessPercent: 50, screenOn: true, settingsVersion: 4 },
          plants: [],
        },
      }];
      state.displaySettings = new Map([["MSV2DISPLAY1", {
        receiverId: "MSV2DISPLAY1",
        brightnessPercent: 65,
        screenOn: true,
        settingsVersion: 5,
        updatedBy: "phone",
      }]]);
      renderDashboard();
    `);
    assert.equal(await evaluate("document.querySelector('[data-display-slider]').value"), "65", "Phone control should render desired brightness.");
    assert.match(await evaluate("document.querySelector('[data-display-state]').textContent"), /queued for receiver/, "A newer desired revision should render queued.");
    await evaluate(`
      window.__displayNativeFetch = window.fetch;
      window.__displayRequest = null;
      window.fetch = async (resource, options = {}) => {
        if (!String(resource).includes('/api/share-display')) return window.__displayNativeFetch(resource, options);
        window.__displayRequest = JSON.parse(options.body);
        return new Response(JSON.stringify({
          ok: true,
          settings: {
            receiverId: window.__displayRequest.receiverId,
            brightnessPercent: window.__displayRequest.brightnessPercent,
            screenOn: window.__displayRequest.screenOn,
            settingsVersion: 6,
            updatedBy: "phone",
          },
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });
      };
    `);
    await click('[data-display-preset="75"]');
    await waitFor("window.__displayRequest?.brightnessPercent === 75 && document.querySelector('[data-display-slider]').value === '75'", "remote brightness save");
    assert.deepEqual(
      await evaluate("window.__displayRequest"),
      { receiverId: "MSV2DISPLAY1", brightnessPercent: 75, screenOn: true },
      "Preset should send a scoped display-only write.",
    );
    await evaluate("window.fetch = window.__displayNativeFetch");
    console.log("CHECK remote brightness controls rendered queued state and sent a display-only write");

    await evaluate(`
      window.__nativeFetch = window.fetch.bind(window);
      window.fetch = (resource, options) => String(resource).includes('/api/share-claim')
        ? Promise.resolve(new Response(JSON.stringify({ ok: false, error: 'Local test stopped before claim' }), { status: 400, headers: { 'Content-Type': 'application/json' } }))
        : window.__nativeFetch(resource, options);
    `);
    const { root } = await command("DOM.getDocument");
    const { nodeId: inputNodeId } = await command("DOM.querySelector", {
      nodeId: root.nodeId,
      selector: "#scanPhotoInput",
    });
    await command("DOM.setFileInputFiles", { nodeId: inputNodeId, files: [qrFixture] });
    await evaluate("document.getElementById('scanPhotoInput').dispatchEvent(new Event('change', { bubbles: true }))");
    console.log("CHECK photo QR submitted");
    await waitFor(
      `document.getElementById('receiverIdInput').value === '${expectedPairing.receiverId}' && document.getElementById('claimCodeInput').value === '${expectedPairing.claimCode}'`,
      "photo QR decoding",
      15000,
    );
    assert.equal(await evaluate("document.getElementById('scanPhotoInput').value"), "", "Photo input should reset after use.");

    await command("Network.enable");
    await command("Network.emulateNetworkConditions", {
      offline: true,
      latency: 0,
      downloadThroughput: 0,
      uploadThroughput: 0,
    });
    await command("Page.reload", { ignoreCache: true });
    await delay(300);
    await waitFor("document.readyState === 'complete' && Boolean(document.getElementById('scanPhotoInput'))", "offline dashboard reload", 15000);
    assert.equal(await evaluate("typeof window.jsQR"), "undefined", "Offline reload should still lazy-load the decoder.");
    const { root: offlineRoot } = await command("DOM.getDocument");
    const { nodeId: offlineInputNodeId } = await command("DOM.querySelector", {
      nodeId: offlineRoot.nodeId,
      selector: "#scanPhotoInput",
    });
    await command("DOM.setFileInputFiles", { nodeId: offlineInputNodeId, files: [qrFixture] });
    await evaluate("document.getElementById('scanPhotoInput').dispatchEvent(new Event('change', { bubbles: true }))");
    await waitFor(
      `typeof window.jsQR === 'function' && document.getElementById('receiverIdInput').value === '${expectedPairing.receiverId}' && document.getElementById('claimCodeInput').value === '${expectedPairing.claimCode}'`,
      "offline photo QR decoding",
      15000,
    );
    console.log("CHECK offline app shell and QR decoder loaded from service-worker cache");
    assert.deepEqual(exceptions, [], "Dashboard should not throw uncaught browser exceptions.");

    console.log("PASS dashboard loaded under its production CSP and permissions policy");
    console.log("PASS service worker v12 precached the versioned app and local QR decoder");
    console.log("PASS live camera was requested before the integrity-checked QR decoder");
    console.log("PASS live scan stopped and released its MediaStream");
    console.log("PASS denied camera permission produced actionable Safari guidance");
    console.log("PASS late camera permission resolution could not leak a MediaStream");
    console.log("PASS remote brightness controls rendered and wrote only receiver display settings");
    console.log("PASS captured-photo fallback decoded an MSV2 pairing QR locally");
    console.log("PASS dashboard and photo QR fallback worked fully offline after precaching");
  } finally {
    if (cdp) {
      try {
        await cdp.command("Browser.close");
      } catch (error) {
        browser.kill();
      }
      cdp.socket.close();
    } else {
      browser.kill();
    }
    server.closeAllConnections();
    await new Promise((resolveClose) => server.close(resolveClose));
    if (!resolve(userDataDir).startsWith(resolve(tmpdir()) + sep)) {
      throw new Error("Refusing to remove browser data outside the temporary directory.");
    }
    for (let attempt = 0; attempt < 10; attempt += 1) {
      if (browser.exitCode === null) {
        if (attempt === 3) browser.kill();
        await delay(200);
      }
      try {
        await rm(userDataDir, { recursive: true, force: true });
        break;
      } catch (error) {
        if (attempt === 9) {
          console.warn(`Temporary browser profile could not be fully removed: ${error.message}`);
        } else {
          await delay(200);
        }
      }
    }
  }
}

await main();
