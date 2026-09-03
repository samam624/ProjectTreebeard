#!/usr/bin/env python3
"""Tiny local Treebeard cloud mock.

This is intentionally just a test harness for the draft API. It stores the
latest snapshot in memory and prints received receiver IDs.
"""

from __future__ import annotations

import argparse
import html
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from time import time
from typing import Any
from urllib.parse import urlparse


LATEST_SNAPSHOTS: dict[str, dict[str, Any]] = {}


class Handler(BaseHTTPRequestHandler):
    server_version = "MSV2MockCloud/0.1"

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path in ("/", "/app"):
            self.send_html(render_dashboard())
            return
        if parsed.path == "/manifest.webmanifest":
            self.send_json(web_manifest())
            return
        if parsed.path == "/sw.js":
            self.send_text(service_worker(), "application/javascript")
            return
        if parsed.path == "/app-icon.svg":
            self.send_text(app_icon_svg(), "image/svg+xml")
            return
        if parsed.path == "/health":
            self.send_json({"ok": True, "service": "msv2-mock-cloud"})
            return
        if parsed.path == "/api/v1/snapshots":
            self.send_json({"ok": True, "snapshots": LATEST_SNAPSHOTS})
            return
        prefix = "/api/v1/receivers/"
        suffix = "/snapshot"
        if parsed.path.startswith(prefix) and parsed.path.endswith(suffix):
            receiver_id = parsed.path[len(prefix):-len(suffix)]
            snapshot = LATEST_SNAPSHOTS.get(receiver_id)
            if snapshot is None:
                self.send_json({"ok": False, "error": "snapshot not found"}, status=404)
                return
            self.send_json({"ok": True, "snapshots": [snapshot]})
            return
        self.send_error(404, "not found")

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        body = self.read_json()
        if body is None:
            return

        if parsed.path == "/api/v1/claim":
            receiver_id = str(body.get("receiverId", ""))
            if not receiver_id or not body.get("claimCode"):
                self.send_json({"ok": False, "error": "receiverId and claimCode required"}, status=400)
                return
            print(f"claim receiver={receiver_id}")
            self.send_json({
                "ok": True,
                "claim": {
                    "household_id": "household_local",
                    "receiver_id": receiver_id,
                },
            })
            return

        prefix = "/api/v1/receivers/"
        suffix = "/snapshot"
        if parsed.path.startswith(prefix) and parsed.path.endswith(suffix):
            receiver_id = parsed.path[len(prefix):-len(suffix)]
            auth_header = self.headers.get("Authorization", "")
            token = auth_header[len("Bearer "):].strip() if auth_header.startswith("Bearer ") else ""
            if not token:
                self.send_json({"ok": False, "error": "unauthorized"}, status=401)
                return
            if body.get("receiverId") != receiver_id:
                self.send_json({"ok": False, "error": "receiverId path/body mismatch"}, status=400)
                return
            body["_mockReceivedAtMs"] = int(time() * 1000)
            LATEST_SNAPSHOTS[receiver_id] = body
            plant_count = len(body.get("plants", []))
            config_version = body.get("config", {}).get("version")
            print(f"snapshot receiver={receiver_id} plants={plant_count} config=v{config_version}")
            self.send_json({
                "ok": True,
                "serverTimeMs": int(time() * 1000),
                "acceptedConfigVersion": config_version,
                "plantCount": plant_count,
            })
            return

        self.send_error(404, "not found")

    def read_json(self) -> dict[str, Any] | None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            return json.loads(raw.decode("utf-8") or "{}")
        except Exception as exc:
            self.send_json({"ok": False, "error": f"invalid json: {exc}"}, status=400)
            return None

    def send_json(self, payload: dict[str, Any], status: int = 200) -> None:
        raw = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def send_html(self, body: str, status: int = 200) -> None:
        raw = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def send_text(self, body: str, content_type: str, status: int = 200) -> None:
        raw = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, format: str, *args: Any) -> None:
        return


def render_dashboard() -> str:
    receivers = sorted(LATEST_SNAPSHOTS.items())
    summary = dashboard_summary(receivers)
    if not receivers:
        cards = """
          <section class="empty">
            <h2>No snapshots yet</h2>
            <p>Start the receiver uploader or run <code>relay_snapshot_once.py</code>, then refresh this page.</p>
          </section>
        """
    else:
        cards = "\n".join(render_receiver_card(receiver_id, snapshot) for receiver_id, snapshot in receivers)

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#287a55">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-title" content="MSV2 Plants">
  <meta name="apple-mobile-web-app-status-bar-style" content="default">
  <link rel="manifest" href="/manifest.webmanifest">
  <link rel="icon" href="/app-icon.svg" type="image/svg+xml">
  <link rel="apple-touch-icon" href="/app-icon.svg">
  <title>MSV2 Mock Cloud</title>
  <style>
    :root {{
      color-scheme: light;
      --bg: #f6f7f4;
      --ink: #18211b;
      --muted: #687368;
      --panel: #ffffff;
      --line: #d9ded6;
      --accent: #287a55;
      --warn: #a26300;
      --danger: #b3261e;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: var(--bg);
      color: var(--ink);
    }}
    header {{
      padding: 20px clamp(16px, 4vw, 40px) 12px;
      border-bottom: 1px solid var(--line);
      background: var(--panel);
    }}
    h1 {{ margin: 0 0 4px; font-size: clamp(24px, 4vw, 36px); }}
    h2 {{ margin: 0; font-size: 18px; }}
    p {{ margin: 6px 0 0; color: var(--muted); }}
    .summary {{
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 8px;
      margin-bottom: 16px;
    }}
    .summary-cell {{
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
      background: var(--panel);
    }}
    .summary-label {{
      display: block;
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 4px;
    }}
    .summary-value {{
      font-size: 22px;
      font-weight: 700;
      line-height: 1;
    }}
    main {{
      width: min(1120px, 100%);
      margin: 0 auto;
      padding: 20px clamp(16px, 4vw, 40px) 40px;
    }}
    .receiver {{
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 16px;
      margin-bottom: 16px;
    }}
    .meta {{
      display: flex;
      flex-wrap: wrap;
      gap: 8px 16px;
      margin-top: 8px;
      color: var(--muted);
      font-size: 14px;
    }}
    .plants {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 10px;
      margin-top: 14px;
    }}
    .plant {{
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
      background: #fbfcfa;
    }}
    .plant-head {{
      display: flex;
      justify-content: space-between;
      gap: 10px;
      align-items: baseline;
      margin-bottom: 10px;
    }}
    .plant strong {{ display: block; font-size: 16px; }}
    .badge {{
      border-radius: 999px;
      border: 1px solid var(--line);
      padding: 2px 8px;
      font-size: 12px;
      white-space: nowrap;
      background: #ffffff;
    }}
    .metric {{
      display: flex;
      justify-content: space-between;
      gap: 10px;
      font-size: 14px;
      padding: 3px 0;
    }}
    .bar {{
      height: 9px;
      border-radius: 999px;
      background: #e4e9e1;
      overflow: hidden;
      margin: 6px 0 8px;
    }}
    .bar span {{
      display: block;
      height: 100%;
      width: var(--pct);
      background: var(--accent);
    }}
    .bar.warn span {{ background: var(--warn); }}
    .bar.danger span {{ background: var(--danger); }}
    .metric span:first-child {{ color: var(--muted); }}
    .ok {{ color: var(--accent); }}
    .warn {{ color: var(--warn); }}
    .danger {{ color: var(--danger); }}
    .empty {{
      border: 1px dashed var(--line);
      border-radius: 8px;
      padding: 24px;
      background: var(--panel);
    }}
    code {{
      background: #eef1eb;
      border: 1px solid var(--line);
      border-radius: 4px;
      padding: 1px 4px;
    }}
    @media (max-width: 620px) {{
      .summary {{ grid-template-columns: repeat(2, minmax(0, 1fr)); }}
      header {{ padding-top: max(20px, env(safe-area-inset-top)); }}
      main {{ padding-bottom: max(40px, env(safe-area-inset-bottom)); }}
    }}
  </style>
</head>
<body>
  <header>
    <h1>MSV2 Mock Cloud</h1>
    <p>Local prototype of the future cloud-backed app view. Auto-refreshes every 10 seconds.</p>
  </header>
  <main>
    {summary}
    {cards}
  </main>
  <script>
    if ("serviceWorker" in navigator) {{
      navigator.serviceWorker.register("/sw.js").catch(() => {{}});
    }}
    window.setTimeout(() => window.location.reload(), 10000);
  </script>
</body>
</html>"""


def web_manifest() -> dict[str, Any]:
    return {
        "name": "MSV2 Plants",
        "short_name": "MSV2",
        "start_url": "/app",
        "scope": "/",
        "display": "standalone",
        "background_color": "#f6f7f4",
        "theme_color": "#287a55",
        "icons": [
            {
                "src": "/app-icon.svg",
                "sizes": "any",
                "type": "image/svg+xml",
                "purpose": "any maskable",
            }
        ],
    }


def service_worker() -> str:
    return """const TREEBEARD_MOCK_CACHE = "msv2-mock-cloud-v1";
self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(TREEBEARD_MOCK_CACHE).then((cache) => cache.addAll(["/app", "/manifest.webmanifest", "/app-icon.svg"])));
  self.skipWaiting();
});
self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});
self.addEventListener("fetch", (event) => {
  if (event.request.method !== "GET") return;
  event.respondWith(fetch(event.request).catch(() => caches.match(event.request)));
});
"""


def app_icon_svg() -> str:
    return """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">
  <rect width="128" height="128" rx="28" fill="#287a55"/>
  <path d="M64 98V63" stroke="#f6f7f4" stroke-width="10" stroke-linecap="round"/>
  <path d="M62 65c-24-2-36-14-38-36 23 1 38 13 38 36z" fill="#b7e4c7"/>
  <path d="M66 66c3-25 17-37 39-39-2 24-15 38-39 39z" fill="#d8f3dc"/>
  <path d="M40 100h48" stroke="#f6f7f4" stroke-width="10" stroke-linecap="round"/>
</svg>
"""


def dashboard_summary(receivers: list[tuple[str, dict[str, Any]]]) -> str:
    receiver_count = len(receivers)
    plant_count = 0
    dry_count = 0
    low_battery_count = 0
    for _, snapshot in receivers:
        config_cards = config_cards_by_sensor(snapshot.get("config", {}))
        for plant in snapshot.get("plants", []):
            plant_count += 1
            config_card = config_cards.get(plant.get("sensor"), {})
            moisture_config = config_card.get("moisture", {})
            moisture_pct = percent(plant.get("moisture"), moisture_config.get("dry", 350), moisture_config.get("wet", 800))
            if moisture_pct == 0:
                dry_count += 1
            battery = plant.get("batteryPercent")
            if isinstance(battery, (int, float)) and battery < 20:
                low_battery_count += 1

    return f"""
      <section class="summary" aria-label="Garden summary">
        {summary_cell("Receivers", receiver_count)}
        {summary_cell("Plants", plant_count)}
        {summary_cell("Dry", dry_count, "danger" if dry_count else "ok")}
        {summary_cell("Low Battery", low_battery_count, "danger" if low_battery_count else "ok")}
      </section>
    """


def summary_cell(label: str, value: Any, class_name: str = "") -> str:
    class_attr = f' {class_name}' if class_name else ""
    return f"""
      <div class="summary-cell">
        <span class="summary-label">{escape(label)}</span>
        <span class="summary-value{class_attr}">{escape(value)}</span>
      </div>
    """


def render_receiver_card(receiver_id: str, snapshot: dict[str, Any]) -> str:
    network = snapshot.get("network", {})
    config = snapshot.get("config", {})
    plants = snapshot.get("plants", [])
    title = config.get("title") or "MSV2 Plants"
    card_by_sensor = config_cards_by_sensor(config)
    received_at = snapshot.get("_mockReceivedAtMs", 0)
    plant_cards = "\n".join(render_plant_card(plant, card_by_sensor.get(plant.get("sensor"), {})) for plant in plants) or "<p>No plant readings in this snapshot.</p>"
    return f"""
      <section class="receiver">
        <h2>{escape(title)}</h2>
        <div class="meta">
          <span>Receiver: <code>{escape(receiver_id)}</code></span>
          <span>Claim: <code>{escape(snapshot.get("claimCode", "--"))}</code></span>
          <span>Config v{escape(config.get("version", "--"))}</span>
          <span>{escape(network.get("mode", "unknown"))} / {escape(network.get("ip", "--"))}</span>
          <span>Received: {format_ms(received_at)}</span>
        </div>
        {render_config_notice(config)}
        <div class="plants">{plant_cards}</div>
      </section>
    """


def render_config_notice(config: dict[str, Any]) -> str:
    if config.get("cards"):
        return ""
    return '<p>Waiting for plant setup details from the next receiver firmware snapshot.</p>'


def render_plant_card(plant: dict[str, Any], config_card: dict[str, Any]) -> str:
    name = config_card.get("plant") or f'Probe {plant.get("sensor", "--")}'
    moisture_config = config_card.get("moisture", {})
    moisture_pct = percent(plant.get("moisture"), moisture_config.get("dry", 350), moisture_config.get("wet", 800))
    moisture_class = health_class(moisture_pct)
    moisture_label = "--" if moisture_pct is None else f"{moisture_pct}%"
    battery = plant.get("batteryPercent")
    battery_class = "danger" if isinstance(battery, (int, float)) and battery < 20 else "warn" if isinstance(battery, (int, float)) and battery < 50 else "ok"
    packets = f'{plant.get("received", "--")} rx / {plant.get("missedTotal", "--")} missed'
    return f"""
      <article class="plant">
        <div class="plant-head">
          <strong>{escape(name)}</strong>
          <span class="badge">Probe {escape(plant.get("sensor", "--"))}</span>
        </div>
        {metric("Moisture", f'{plant.get("moisture", "--")} raw / {moisture_label}', moisture_class)}
        {bar(moisture_pct, moisture_class)}
        {metric("Battery", "--" if battery is None else str(battery) + "%", battery_class)}
        {metric("RSSI", str(plant.get("rssi", "--")) + " dBm")}
        {metric("Age", str(plant.get("ageMs", "--")) + " ms")}
        {metric("Packets", packets)}
      </article>
    """


def config_cards_by_sensor(config: dict[str, Any]) -> dict[Any, dict[str, Any]]:
    cards = {}
    for card in config.get("cards", []):
        if isinstance(card, dict):
            cards[card.get("sensor")] = card
    return cards


def percent(value: Any, dry: Any, wet: Any) -> int | None:
    try:
        reading = float(value)
        dry_value = float(dry)
        wet_value = float(wet)
    except (TypeError, ValueError):
        return None
    if wet_value == dry_value:
        return None
    pct = round((reading - dry_value) * 100 / (wet_value - dry_value))
    return max(0, min(100, pct))


def health_class(pct: int | None) -> str:
    if pct is None:
        return ""
    if pct == 0:
        return "danger"
    if pct <= 10:
        return "warn"
    return "ok"


def bar(pct: int | None, class_name: str) -> str:
    value = 0 if pct is None else pct
    return f'<div class="bar {escape(class_name)}" style="--pct:{value}%"><span></span></div>'


def metric(label: str, value: Any, class_name: str = "") -> str:
    class_attr = f' class="{class_name}"' if class_name else ""
    return f'<div class="metric"><span>{escape(label)}</span><span{class_attr}>{escape(value)}</span></div>'


def escape(value: Any) -> str:
    return html.escape(str(value), quote=True)


def format_ms(ms: Any) -> str:
    try:
        value = int(ms)
    except (TypeError, ValueError):
        return "--"
    if value <= 0:
        return "--"
    return f"{value // 1000}s server time"


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a local MSV2 mock cloud API.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8088)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"MSV2 mock cloud listening on http://{args.host}:{args.port}")
    print("dashboard: GET /")
    print("health: GET /health")
    print("snapshots: GET /api/v1/snapshots")
    server.serve_forever()


if __name__ == "__main__":
    main()
