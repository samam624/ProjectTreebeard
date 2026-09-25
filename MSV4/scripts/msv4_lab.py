#!/usr/bin/env python3
"""Local MSV4 BLE bring-up lab: browser UI, live console, portable sessions.

Uses msv4_ble_console.py as the only BLE transport. No account, cloud service,
or external web asset is needed. The HTTP listener binds to loopback only.
"""

from __future__ import annotations

import argparse
import json
import re
import secrets
import subprocess
import sys
import threading
import time
import uuid
import webbrowser
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]
CONSOLE = Path(__file__).with_name("msv4_ble_console.py")
PAGE = ROOT / "ui" / "lab.html"
STYLE = ROOT / "ui" / "lab.css"
SCRIPT = ROOT / "ui" / "lab.js"
SESSION_ID = re.compile(r"^[0-9a-f]{32}$")
METRIC_PATTERNS = {
    "temp_c": re.compile(r"\bTEMP=([-+\d.]+) C"),
    "vbat_mv": re.compile(r"\bVBAT=([-+\d.]+) mV"),
    "vdd_mv": re.compile(r"\bVDD=([-+\d.]+) mV"),
    "sense_mv": re.compile(r"\bSENSE_OUT=([-+\d.]+) mV"),
    "drv_mv": re.compile(r"\bDRV_OUT=([-+\d.]+) mV"),
    "rsoil_ohm": re.compile(r"\bRsoil 1/Gs\s+([-+\d.]+) ohm"),
    "cm_pf": re.compile(r"\bCm interface\s+([-+\d.]+) pF"),
    "cp_pf": re.compile(r"\bCp = ([-+\d.]+) pF"),
}


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def parse_metrics(line: str) -> dict[str, float]:
    metrics = {}
    for key, pattern in METRIC_PATTERNS.items():
        found = pattern.search(line)
        if found:
            try:
                metrics[key] = float(found.group(1))
            except ValueError:
                pass
    return metrics


class SessionStore:
    """One JSON metadata file plus append-only JSONL per portable session."""

    def __init__(self, directory: Path):
        self.directory = directory.resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock()
        self.active: dict = {}
        self.events: list[dict] = []
        sessions = self.list_sessions()
        if sessions:
            self.activate(sessions[0]["id"])
        else:
            self.new_session("First bring-up")

    def _folder(self, session_id: str) -> Path:
        if not SESSION_ID.fullmatch(session_id):
            raise ValueError("Invalid session ID")
        return self.directory / session_id

    def _save_meta(self) -> None:
        folder = self._folder(self.active["id"])
        temp = folder / "session.json.tmp"
        temp.write_text(json.dumps(self.active, indent=2) + "\n", encoding="utf-8")
        temp.replace(folder / "session.json")

    def list_sessions(self) -> list[dict]:
        result = []
        for meta_path in self.directory.glob("*/session.json"):
            try:
                meta = json.loads(meta_path.read_text(encoding="utf-8"))
                if SESSION_ID.fullmatch(meta.get("id", "")):
                    result.append(meta)
            except (OSError, ValueError, TypeError):
                continue
        return sorted(result, key=lambda item: item.get("updated_at", ""), reverse=True)

    def new_session(self, title: str) -> dict:
        title = title.strip()[:100] or "Untitled session"
        with self.lock:
            session_id = uuid.uuid4().hex
            self._folder(session_id).mkdir()
            self.active = {"schema": 1, "id": session_id, "title": title,
                           "created_at": now_iso(), "updated_at": now_iso()}
            self.events = []
            self._save_meta()
            return dict(self.active)

    def activate(self, session_id: str) -> dict:
        with self.lock:
            folder = self._folder(session_id)
            self.active = json.loads((folder / "session.json").read_text(encoding="utf-8"))
            events_path = folder / "events.jsonl"
            self.events = []
            if events_path.exists():
                for line in events_path.read_text(encoding="utf-8").splitlines():
                    if line:
                        self.events.append(json.loads(line))
            return dict(self.active)

    def append(self, kind: str, text: str, metrics: dict | None = None) -> dict:
        with self.lock:
            event = {"seq": len(self.events) + 1, "time": now_iso(),
                     "kind": kind, "text": text[:1000]}
            if metrics:
                event["metrics"] = metrics
            path = self._folder(self.active["id"]) / "events.jsonl"
            with path.open("a", encoding="utf-8") as handle:
                handle.write(json.dumps(event, ensure_ascii=False) + "\n")
            self.events.append(event)
            self.active["updated_at"] = event["time"]
            self._save_meta()
            return event

    def snapshot(self, after: int = 0, limit: int = 500) -> dict:
        with self.lock:
            start = max(0, int(after))
            return {"session": dict(self.active), "events": self.events[start:start + limit],
                    "total": len(self.events)}

    def export(self, session_id: str) -> dict:
        with self.lock:
            if session_id == self.active["id"]:
                return {"schema": 1, "session": dict(self.active), "events": list(self.events)}
            folder = self._folder(session_id)
            meta = json.loads((folder / "session.json").read_text(encoding="utf-8"))
            path = folder / "events.jsonl"
            events = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()] if path.exists() else []
            return {"schema": 1, "session": meta, "events": events}

    def import_session(self, payload: dict) -> dict:
        if not isinstance(payload, dict) or payload.get("schema") != 1:
            raise ValueError("Unsupported session format")
        meta, events = payload.get("session"), payload.get("events")
        if not isinstance(meta, dict) or not isinstance(events, list) or len(events) > 50000:
            raise ValueError("Invalid session data")
        title = str(meta.get("title", "Imported session"))[:100]
        cleaned = []
        for index, event in enumerate(events, 1):
            if not isinstance(event, dict):
                raise ValueError("Invalid event")
            kind = str(event.get("kind", "note"))[:20]
            record = {"seq": index, "time": str(event.get("time", now_iso()))[:40],
                      "kind": kind, "text": str(event.get("text", ""))[:1000]}
            metrics = event.get("metrics")
            if isinstance(metrics, dict):
                record["metrics"] = {k: float(v) for k, v in metrics.items()
                                     if k in METRIC_PATTERNS and isinstance(v, (int, float))}
            cleaned.append(record)
        with self.lock:
            created = self.new_session(title)
            self.active["imported_from"] = str(meta.get("id", ""))[:64]
            self._save_meta()
            path = self._folder(created["id"]) / "events.jsonl"
            path.write_text("".join(json.dumps(e, ensure_ascii=False) + "\n" for e in cleaned), encoding="utf-8")
            self.events = cleaned
            return dict(self.active)


class BleBridge:
    """Wrap the existing console process, preserving its WinRT BLE setup."""

    def __init__(self, store: SessionStore):
        self.store = store
        self.lock = threading.RLock()
        self.process: subprocess.Popen | None = None
        self.state = "disconnected"
        self.device = ""
        self.busy = False
        self.capture_stop = threading.Event()
        self.capture_commands: list[str] = []
        self.capture_interval = 0

    def status(self) -> dict:
        with self.lock:
            return {"connection": self.state, "device": self.device, "busy": self.busy,
                    "capturing": not self.capture_stop.is_set() and bool(self.capture_commands),
                    "capture_interval": self.capture_interval}

    def scan(self) -> list[dict]:
        result = subprocess.run([sys.executable, str(CONSOLE), "--list", "--scan-timeout", "8"],
                                capture_output=True, text=True, timeout=18)
        if result.returncode:
            raise RuntimeError((result.stderr or result.stdout or "BLE scan failed")[-500:])
        return json.loads(result.stdout.strip().splitlines()[-1])

    def connect(self, device: str) -> None:
        if len(device) > 80 or "\n" in device or "\r" in device:
            raise ValueError("Invalid device")
        with self.lock:
            if self.state != "disconnected":
                raise ValueError("Already connected or connecting")
            command = [sys.executable, "-u", str(CONSOLE), "--machine", "--scan-timeout", "8"]
            if device:
                command += ["--device", device]
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                            text=True, encoding="utf-8", errors="replace", bufsize=1,
                                            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
            self.state = "connecting"
            self.device = device or "auto"
            self.store.append("status", f"Connecting to {self.device}")
            threading.Thread(target=self._read_output, args=(self.process,), daemon=True).start()

    def _read_output(self, process: subprocess.Popen) -> None:
        assert process.stdout is not None
        for raw in process.stdout:
            line = raw.rstrip("\r\n")
            with self.lock:
                if self.process is not process:
                    continue
                if line == "__MSV4_PROMPT__":
                    self.busy = False
                    continue
                if line.startswith("Connected. "):
                    self.state = "connected"
                self.store.append("rx", line, parse_metrics(line))
        code = process.wait()
        with self.lock:
            if self.process is process:
                self.process = None
                self.state = "disconnected"
                self.busy = False
                self.capture_stop.set()
                self.capture_commands = []
                self.store.append("status", f"BLE console closed (exit {code})")

    def disconnect(self) -> None:
        with self.lock:
            self.capture_stop.set()
            self.capture_commands = []
            if self.process and self.process.poll() is None:
                self.state = "disconnecting"
                self.process.terminate()
            else:
                self.state = "disconnected"
            self.busy = False

    def send(self, command: str) -> None:
        command = command.strip()
        if not command or len(command) > 80 or "\n" in command or "\r" in command:
            raise ValueError("One command of at most 80 characters is required")
        with self.lock:
            if self.state != "connected" or not self.process or not self.process.stdin:
                raise ValueError("Connect to an MSV4 board first")
            if self.busy and command.lower() != "stop":
                raise ValueError("Wait for the current command, or send stop")
            if command.lower() != "stop":
                self.busy = True
            self.store.append("command", command)
            try:
                self.process.stdin.write(command + "\n")
                self.process.stdin.flush()
            except OSError as exc:
                self.busy = False
                raise RuntimeError(f"BLE console pipe closed: {exc}") from exc

    def start_capture(self, commands: list[str], interval: float) -> None:
        allowed = {"read", "temp", "vbat", "vdd", "iq 16", "spectrum ab"}
        if not isinstance(commands, list) or not commands or len(commands) > 3 or any(c not in allowed for c in commands):
            raise ValueError("Choose one to three supported measurement commands")
        if not 2 <= interval <= 3600:
            raise ValueError("Interval must be 2 to 3600 seconds")
        with self.lock:
            if self.state != "connected":
                raise ValueError("Connect before starting capture")
            if self.capture_commands and not self.capture_stop.is_set():
                raise ValueError("Capture is already running")
            self.capture_stop = threading.Event()
            self.capture_commands = list(commands)
            self.capture_interval = interval
            self.store.append("status", f"Capture started: {', '.join(commands)} every {interval:g}s")
            threading.Thread(target=self._capture_loop, args=(self.capture_stop,), daemon=True).start()

    def _capture_loop(self, stop: threading.Event) -> None:
        while not stop.is_set():
            for command in self.capture_commands:
                while not stop.is_set():
                    with self.lock:
                        ready = self.state == "connected" and not self.busy
                    if ready:
                        break
                    if stop.wait(0.1):
                        return
                if stop.is_set():
                    return
                try:
                    self.send(command)
                except (ValueError, RuntimeError):
                    stop.set()
                    break
                started = time.monotonic()
                while not stop.wait(0.1):
                    with self.lock:
                        if not self.busy or self.state != "connected":
                            break
                    if time.monotonic() - started > 180:
                        self.store.append("status", f"Capture timed out waiting for {command}")
                        stop.set()
                        break
            if stop.wait(self.capture_interval):
                break
        with self.lock:
            if self.capture_stop is stop:
                self.capture_commands = []
                self.store.append("status", "Capture stopped")

    def stop_capture(self) -> None:
        with self.lock:
            self.capture_stop.set()
            self.capture_commands = []
            self.store.append("status", "Capture stopped")


class LabServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], store: SessionStore, bridge: BleBridge):
        super().__init__(address, LabHandler)
        self.store = store
        self.bridge = bridge
        self.token = secrets.token_urlsafe(24)


class LabHandler(BaseHTTPRequestHandler):
    server: LabServer

    def log_message(self, format: str, *args) -> None:
        return  # Session events and startup URL are the useful output.

    def _headers(self, status: int, kind: str, size: int, extra: dict | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(size))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'")
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.end_headers()

    def _json(self, status: int, value: object, extra: dict | None = None) -> None:
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self._headers(status, "application/json; charset=utf-8", len(body), extra)
        self.wfile.write(body)

    def _body(self) -> dict:
        if self.headers.get("Content-Type", "").split(";", 1)[0] != "application/json":
            raise ValueError("Expected application/json")
        size = int(self.headers.get("Content-Length", "0"))
        if not 0 < size <= 16 * 1024 * 1024:
            raise ValueError("Request body must be 1 byte to 16 MB")
        value = json.loads(self.rfile.read(size))
        if not isinstance(value, dict):
            raise ValueError("Expected a JSON object")
        return value

    def do_GET(self) -> None:
        url = urlsplit(self.path)
        query = parse_qs(url.query)
        try:
            if url.path == "/":
                body = PAGE.read_bytes()
                self._headers(200, "text/html; charset=utf-8", len(body))
                self.wfile.write(body)
            elif url.path in ("/lab.css", "/lab.js"):
                file = STYLE if url.path == "/lab.css" else SCRIPT
                kind = "text/css; charset=utf-8" if file == STYLE else "text/javascript; charset=utf-8"
                body = file.read_bytes()
                self._headers(200, kind, len(body))
                self.wfile.write(body)
            elif url.path == "/api/state":
                snapshot = self.server.store.snapshot(int(query.get("after", [0])[0]))
                snapshot.update(self.server.bridge.status())
                self._json(200, snapshot)
            elif url.path == "/api/config":
                self._json(200, {"token": self.server.token})
            elif url.path == "/api/sessions":
                self._json(200, {"sessions": self.server.store.list_sessions()})
            elif url.path == "/api/export":
                session_id = query.get("id", [self.server.store.active["id"]])[0]
                payload = self.server.store.export(session_id)
                self._json(200, payload, {"Content-Disposition": f'attachment; filename="msv4-session-{session_id}.json"'})
            else:
                self._json(404, {"error": "Not found"})
        except (ValueError, KeyError, OSError) as exc:
            self._json(400, {"error": str(exc)})

    def do_POST(self) -> None:
        if self.headers.get("X-MSV4-Token") != self.server.token:
            self._json(403, {"error": "Invalid local UI token"})
            return
        try:
            body = self._body()
            path = urlsplit(self.path).path
            if path == "/api/scan":
                result = {"boards": self.server.bridge.scan()}
            elif path == "/api/connect":
                self.server.bridge.connect(str(body.get("device", "")).strip())
                result = {"ok": True}
            elif path == "/api/disconnect":
                self.server.bridge.disconnect()
                result = {"ok": True}
            elif path == "/api/command":
                self.server.bridge.send(str(body.get("command", "")))
                result = {"ok": True}
            elif path == "/api/capture/start":
                self.server.bridge.start_capture(body.get("commands", []), float(body.get("interval", 10)))
                result = {"ok": True}
            elif path == "/api/capture/stop":
                self.server.bridge.stop_capture()
                result = {"ok": True}
            elif path == "/api/sessions/new":
                result = {"session": self.server.store.new_session(str(body.get("title", "")))}
            elif path == "/api/sessions/activate":
                if self.server.bridge.status()["connection"] != "disconnected":
                    raise ValueError("Disconnect before opening another session")
                result = {"session": self.server.store.activate(str(body.get("id", "")))}
            elif path == "/api/sessions/import":
                if self.server.bridge.status()["connection"] != "disconnected":
                    raise ValueError("Disconnect before importing a session")
                result = {"session": self.server.store.import_session(body)}
            elif path == "/api/note":
                note = str(body.get("text", "")).strip()
                if not note or len(note) > 1000:
                    raise ValueError("Note must be 1 to 1000 characters")
                result = {"event": self.server.store.append("note", note)}
            else:
                self._json(404, {"error": "Not found"})
                return
            self._json(200, result)
        except (ValueError, KeyError, OSError, RuntimeError, subprocess.TimeoutExpired,
                json.JSONDecodeError) as exc:
            self._json(400, {"error": str(exc)})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--data-dir", type=Path, default=ROOT / ".lab-sessions",
                        help="Session storage; use a synced folder to share sessions between PCs")
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    store = SessionStore(args.data_dir)
    bridge = BleBridge(store)
    server = LabServer(("127.0.0.1", args.port), store, bridge)
    url = f"http://127.0.0.1:{server.server_port}/"
    print(f"MSV4 Lab: {url}\nSessions: {store.directory}\nPress Ctrl+C to stop.", flush=True)
    if not args.no_browser:
        threading.Timer(0.3, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        bridge.disconnect()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
