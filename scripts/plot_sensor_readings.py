#!/usr/bin/env python
"""Render a sensor-readings CSV (from log_sensor_readings.py) into a self-contained
HTML chart: moisture and battery over time, each as its own single-series line
chart (no dual-axis - see MSV2 dataviz conventions), with hover tooltips and a
table-view toggle. Safe to re-run against a growing CSV at any point mid-run.

Example:
  python scripts/plot_sensor_readings.py --csv data/sensor_logs/alocasia_overnight.csv --title "Alocasia overnight drydown"
"""

from __future__ import annotations

import argparse
import csv
import json
from datetime import datetime
from html import escape
from pathlib import Path


def load_rows(csv_path: Path, sensor: int | None) -> list[dict]:
    rows = []
    with csv_path.open("r", newline="", encoding="utf-8") as handle:
        for raw in csv.DictReader(handle):
            if sensor is not None and raw.get("sensor") and int(raw["sensor"]) != sensor:
                continue
            try:
                t = datetime.fromisoformat(raw["timestamp_iso"]).timestamp() * 1000
            except (KeyError, ValueError):
                continue

            def num(key: str) -> float | None:
                value = raw.get(key, "")
                if value in ("", None):
                    return None
                try:
                    return float(value)
                except ValueError:
                    return None

            rows.append(
                {
                    "t": t,
                    "moisture": num("moisture"),
                    "battery": num("battery_percent"),
                    "wake": raw.get("wake", ""),
                }
            )
    rows.sort(key=lambda r: r["t"])
    return rows


PAGE_TEMPLATE = """<!doctype html>
<meta charset="utf-8" />
<title>{title}</title>
<style>
  :root {{ color-scheme: light; }}
  * {{ box-sizing: border-box; }}
  body {{
    margin: 0;
    font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
    background: #f9f9f7;
    color: #0b0b0b;
    padding: 24px;
  }}
  @media (prefers-color-scheme: dark) {{
    :root:where(:not([data-theme="light"])) {{ color-scheme: dark; }}
    body {{ background: #0d0d0d; color: #ffffff; }}
  }}
  :root[data-theme="dark"] body {{ background: #0d0d0d; color: #ffffff; }}

  h1 {{ font-size: 20px; margin: 0 0 4px; }}
  .subtitle {{ color: #52514e; font-size: 13px; margin: 0 0 24px; }}
  @media (prefers-color-scheme: dark) {{
    :root:where(:not([data-theme="light"])) .subtitle {{ color: #c3c2b7; }}
  }}
  :root[data-theme="dark"] .subtitle {{ color: #c3c2b7; }}

  .card {{
    background: #fcfcfb;
    border: 1px solid rgba(11,11,11,0.10);
    border-radius: 12px;
    padding: 20px;
    margin-bottom: 20px;
  }}
  @media (prefers-color-scheme: dark) {{
    :root:where(:not([data-theme="light"])) .card {{
      background: #1a1a19;
      border-color: rgba(255,255,255,0.10);
    }}
  }}
  :root[data-theme="dark"] .card {{
    background: #1a1a19;
    border-color: rgba(255,255,255,0.10);
  }}

  .card-head {{ display: flex; align-items: baseline; justify-content: space-between; margin-bottom: 8px; }}
  .card-title {{ font-size: 14px; font-weight: 600; }}
  .card-latest {{ font-size: 13px; color: #52514e; }}
  @media (prefers-color-scheme: dark) {{
    :root:where(:not([data-theme="light"])) .card-latest {{ color: #c3c2b7; }}
  }}
  :root[data-theme="dark"] .card-latest {{ color: #c3c2b7; }}

  svg {{ width: 100%; height: auto; display: block; overflow: visible; }}
  .gridline {{ stroke: #e1e0d9; stroke-width: 1; }}
  @media (prefers-color-scheme: dark) {{
    :root:where(:not([data-theme="light"])) .gridline {{ stroke: #2c2c2a; }}
  }}
  :root[data-theme="dark"] .gridline {{ stroke: #2c2c2a; }}

  .axis-text {{ fill: #898781; font-size: 11px; }}
  .crosshair {{ stroke: #898781; stroke-width: 1; opacity: 0; pointer-events: none; }}

  .tooltip {{
    position: absolute;
    pointer-events: none;
    background: #0b0b0b;
    color: #ffffff;
    font-size: 12px;
    padding: 6px 10px;
    border-radius: 6px;
    opacity: 0;
    transform: translate(-50%, -110%);
    white-space: nowrap;
    z-index: 10;
  }}
  .tooltip .tt-value {{ font-weight: 600; }}
  .tooltip .tt-time {{ color: #c3c2b7; font-size: 11px; }}
  .chart-wrap {{ position: relative; }}

  .table-toggle {{
    font: inherit;
    font-size: 12px;
    background: none;
    border: 1px solid rgba(11,11,11,0.20);
    border-radius: 6px;
    padding: 4px 10px;
    cursor: pointer;
    color: inherit;
  }}
  @media (prefers-color-scheme: dark) {{
    :root:where(:not([data-theme="light"])) .table-toggle {{ border-color: rgba(255,255,255,0.20); }}
  }}
  :root[data-theme="dark"] .table-toggle {{ border-color: rgba(255,255,255,0.20); }}

  table {{ width: 100%; border-collapse: collapse; font-size: 12px; margin-top: 12px; display: none; }}
  table.shown {{ display: table; }}
  .table-scroll {{ max-height: 320px; overflow-y: auto; }}
  th, td {{ text-align: left; padding: 4px 8px; border-bottom: 1px solid #e1e0d9; font-variant-numeric: tabular-nums; }}
  @media (prefers-color-scheme: dark) {{
    :root:where(:not([data-theme="light"])) th, :root:where(:not([data-theme="light"])) td {{ border-color: #2c2c2a; }}
  }}
  :root[data-theme="dark"] th, :root[data-theme="dark"] td {{ border-color: #2c2c2a; }}
  th {{ color: #898781; font-weight: 500; position: sticky; top: 0; background: inherit; }}
</style>

<h1>{title}</h1>
<p class="subtitle">{subtitle}</p>

<div class="card">
  <div class="card-head">
    <div class="card-title">Moisture (RC fall time, &micro;s)</div>
    <div class="card-latest" id="moisture-latest"></div>
  </div>
  <div class="chart-wrap">
    <svg id="moisture-chart"></svg>
    <div class="tooltip" id="moisture-tooltip"><div class="tt-value"></div><div class="tt-time"></div></div>
  </div>
</div>

<div class="card">
  <div class="card-head">
    <div class="card-title">Battery</div>
    <div class="card-latest" id="battery-latest"></div>
  </div>
  <div class="chart-wrap">
    <svg id="battery-chart"></svg>
    <div class="tooltip" id="battery-tooltip"><div class="tt-value"></div><div class="tt-time"></div></div>
  </div>
</div>

<div class="card">
  <div class="card-head">
    <div class="card-title">Raw data ({row_count} rows)</div>
    <button class="table-toggle" id="table-toggle" type="button">Show table</button>
  </div>
  <div class="table-scroll">
    <table id="data-table">
      <thead><tr><th>Time</th><th>Wake</th><th>Moisture</th><th>Battery %</th></tr></thead>
      <tbody></tbody>
    </table>
  </div>
</div>

<script>
const DATA = {data_json};
const SERIES = [
  {{ key: "moisture", light: "#2a78d6", dark: "#3987e5", unit: "\\u00b5s", chartId: "moisture-chart", tooltipId: "moisture-tooltip", latestId: "moisture-latest" }},
  {{ key: "battery", light: "#1baf7a", dark: "#199e70", unit: "%", chartId: "battery-chart", tooltipId: "battery-tooltip", latestId: "battery-latest" }},
];

function isDark() {{
  const stamped = document.documentElement.getAttribute("data-theme");
  if (stamped === "dark") return true;
  if (stamped === "light") return false;
  return window.matchMedia("(prefers-color-scheme: dark)").matches;
}}

function niceStep(roughStep) {{
  const mag = Math.pow(10, Math.floor(Math.log10(roughStep)));
  const norm = roughStep / mag;
  let step;
  if (norm < 1.5) step = 1;
  else if (norm < 3) step = 2;
  else if (norm < 7) step = 5;
  else step = 10;
  return step * mag;
}}

function formatTimeTick(ms, spanMs) {{
  const d = new Date(ms);
  const timeOpts = {{ hour: "numeric", minute: "2-digit" }};
  if (spanMs > 36 * 3600 * 1000) {{
    return d.toLocaleDateString(undefined, {{ month: "short", day: "numeric" }}) + " " + d.toLocaleTimeString(undefined, timeOpts);
  }}
  if (spanMs > 20 * 3600 * 1000) {{
    return d.toLocaleDateString(undefined, {{ weekday: "short" }}) + " " + d.toLocaleTimeString(undefined, timeOpts);
  }}
  return d.toLocaleTimeString(undefined, timeOpts);
}}

function formatValue(v, unit) {{
  if (v == null) return "--";
  const rounded = Math.round(v * 10) / 10;
  return (unit === "%" ? rounded.toFixed(0) : rounded.toLocaleString()) + (unit === "%" ? "%" : " " + unit);
}}

function renderChart(series) {{
  const points = DATA.filter((d) => d[series.key] != null).map((d) => ({{ t: d.t, v: d[series.key], wake: d.wake }}));
  const svg = document.getElementById(series.chartId);
  const tooltip = document.getElementById(series.tooltipId);
  const latestEl = document.getElementById(series.latestId);
  svg.innerHTML = "";

  if (points.length === 0) {{
    svg.setAttribute("viewBox", "0 0 800 80");
    const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
    t.setAttribute("x", 20);
    t.setAttribute("y", 40);
    t.setAttribute("class", "axis-text");
    t.textContent = "No data yet.";
    svg.appendChild(t);
    return;
  }}

  const color = isDark() ? series.dark : series.light;
  const width = 800;
  const height = 220;
  const padL = 46;
  const padR = 12;
  const padT = 12;
  const padB = 28;
  const plotW = width - padL - padR;
  const plotH = height - padT - padB;

  const tMin = points[0].t;
  const tMax = points[points.length - 1].t;
  const tSpan = Math.max(1, tMax - tMin);

  let vMin = Math.min(...points.map((p) => p.v));
  let vMax = Math.max(...points.map((p) => p.v));
  if (vMin === vMax) {{ vMin -= 1; vMax += 1; }}
  const pad = (vMax - vMin) * 0.1;
  vMin -= pad;
  vMax += pad;
  if (series.key === "battery") {{ vMin = Math.max(0, vMin); vMax = Math.min(100, Math.max(vMax, vMin + 1)); }}

  const x = (t) => padL + ((t - tMin) / tSpan) * plotW;
  const y = (v) => padT + (1 - (v - vMin) / (vMax - vMin)) * plotH;

  svg.setAttribute("viewBox", `0 0 ${{width}} ${{height}}`);
  const ns = "http://www.w3.org/2000/svg";

  const yStep = niceStep((vMax - vMin) / 4);
  const yTicks = [];
  for (let v = Math.ceil(vMin / yStep) * yStep; v <= vMax; v += yStep) yTicks.push(v);

  for (const v of yTicks) {{
    const gy = y(v);
    const line = document.createElementNS(ns, "line");
    line.setAttribute("x1", padL);
    line.setAttribute("x2", width - padR);
    line.setAttribute("y1", gy);
    line.setAttribute("y2", gy);
    line.setAttribute("class", "gridline");
    svg.appendChild(line);

    const label = document.createElementNS(ns, "text");
    label.setAttribute("x", padL - 8);
    label.setAttribute("y", gy + 3);
    label.setAttribute("text-anchor", "end");
    label.setAttribute("class", "axis-text");
    label.textContent = Math.round(v).toLocaleString();
    svg.appendChild(label);
  }}

  const tickCount = 5;
  for (let i = 0; i < tickCount; i++) {{
    const t = tMin + (tSpan * i) / (tickCount - 1);
    const gx = x(t);
    const label = document.createElementNS(ns, "text");
    label.setAttribute("x", gx);
    label.setAttribute("y", height - 6);
    label.setAttribute("text-anchor", i === 0 ? "start" : i === tickCount - 1 ? "end" : "middle");
    label.setAttribute("class", "axis-text");
    label.textContent = formatTimeTick(t, tSpan);
    svg.appendChild(label);
  }}

  const path = document.createElementNS(ns, "path");
  const d = points.map((p, i) => `${{i === 0 ? "M" : "L"}}${{x(p.t).toFixed(2)}},${{y(p.v).toFixed(2)}}`).join(" ");
  path.setAttribute("d", d);
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
  endDot.setAttribute("stroke", isDark() ? "#1a1a19" : "#fcfcfb");
  endDot.setAttribute("stroke-width", 2);
  svg.appendChild(endDot);

  const crosshair = document.createElementNS(ns, "line");
  crosshair.setAttribute("y1", padT);
  crosshair.setAttribute("y2", height - padB);
  crosshair.setAttribute("class", "crosshair");
  svg.appendChild(crosshair);

  const hoverDot = document.createElementNS(ns, "circle");
  hoverDot.setAttribute("r", 4);
  hoverDot.setAttribute("fill", color);
  hoverDot.setAttribute("stroke", isDark() ? "#1a1a19" : "#fcfcfb");
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

  function nearestPoint(clientX) {{
    const rect = svg.getBoundingClientRect();
    const svgX = ((clientX - rect.left) / rect.width) * width;
    const t = tMin + ((svgX - padL) / plotW) * tSpan;
    let lo = 0, hi = points.length - 1;
    while (lo < hi) {{
      const mid = (lo + hi) >> 1;
      if (points[mid].t < t) lo = mid + 1; else hi = mid;
    }}
    if (lo > 0 && Math.abs(points[lo - 1].t - t) < Math.abs(points[lo].t - t)) lo -= 1;
    return points[lo];
  }}

  function showTooltip(clientX, clientY, point) {{
    crosshair.setAttribute("x1", x(point.t));
    crosshair.setAttribute("x2", x(point.t));
    crosshair.setAttribute("opacity", 1);
    hoverDot.setAttribute("cx", x(point.t));
    hoverDot.setAttribute("cy", y(point.v));
    hoverDot.setAttribute("opacity", 1);

    const wrapRect = svg.parentElement.getBoundingClientRect();
    tooltip.style.left = (clientX - wrapRect.left) + "px";
    tooltip.style.top = (clientY - wrapRect.top) + "px";
    tooltip.style.opacity = 1;
    tooltip.querySelector(".tt-value").textContent = formatValue(point.v, series.unit);
    tooltip.querySelector(".tt-time").textContent = new Date(point.t).toLocaleString();
  }}

  function hideTooltip() {{
    crosshair.setAttribute("opacity", 0);
    hoverDot.setAttribute("opacity", 0);
    tooltip.style.opacity = 0;
  }}

  hitRect.addEventListener("pointermove", (evt) => {{
    const point = nearestPoint(evt.clientX);
    showTooltip(evt.clientX, evt.clientY, point);
  }});
  hitRect.addEventListener("pointerleave", hideTooltip);

  latestEl.textContent = `latest ${{formatValue(last.v, series.unit)}} · ${{new Date(last.t).toLocaleString()}}`;
}}

function renderAll() {{
  for (const series of SERIES) renderChart(series);
}}

function renderTable() {{
  const tbody = document.querySelector("#data-table tbody");
  tbody.innerHTML = "";
  for (const row of DATA) {{
    const tr = document.createElement("tr");
    const cells = [
      new Date(row.t).toLocaleString(),
      row.wake ?? "",
      row.moisture ?? "",
      row.battery ?? "",
    ];
    for (const value of cells) {{
      const td = document.createElement("td");
      td.textContent = value;
      tr.appendChild(td);
    }}
    tbody.appendChild(tr);
  }}
}}

document.getElementById("table-toggle").addEventListener("click", () => {{
  const table = document.getElementById("data-table");
  const showing = table.classList.toggle("shown");
  document.getElementById("table-toggle").textContent = showing ? "Hide table" : "Show table";
  if (showing && !table.dataset.filled) {{
    renderTable();
    table.dataset.filled = "1";
  }}
}});

renderAll();
window.addEventListener("resize", renderAll);
window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", renderAll);
</script>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Render a sensor-log CSV into an HTML chart.")
    parser.add_argument("--csv", type=Path, required=True, help="CSV produced by log_sensor_readings.py.")
    parser.add_argument("--sensor", type=int, default=None, help="Filter to this sensor ID (default: all rows in the CSV).")
    parser.add_argument("--title", default="MSV2 sensor readings", help="Chart page title.")
    parser.add_argument("--out", type=Path, default=None, help="Output HTML path (default: <csv>.html).")
    args = parser.parse_args()

    rows = load_rows(args.csv, args.sensor)
    out_path = args.out or args.csv.with_suffix(".html")

    if rows:
        span_hours = (rows[-1]["t"] - rows[0]["t"]) / 3_600_000
        subtitle = f"{len(rows)} readings over {span_hours:.1f}h, from {args.csv.name}"
    else:
        subtitle = f"No readings yet in {args.csv.name}"

    html = PAGE_TEMPLATE.format(
        title=escape(args.title),
        subtitle=escape(subtitle),
        row_count=len(rows),
        data_json=json.dumps(rows),
    )
    out_path.write_text(html, encoding="utf-8")
    print(f"Wrote {out_path} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
