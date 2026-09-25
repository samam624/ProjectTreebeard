"""Hardware-free checks for the portable MSV4 lab session and local API."""

import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

sys.path.insert(0, str(Path(__file__).resolve().parent))
from msv4_lab import BleBridge, LabServer, SessionStore, parse_metrics


class LabTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.store = SessionStore(Path(self.temp.name))
        self.bridge = BleBridge(self.store)
        self.server = LabServer(("127.0.0.1", 0), self.store, self.bridge)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.temp.cleanup()

    def get(self, path):
        with urlopen(self.base + path, timeout=3) as response:
            return response.read(), response.headers

    def post(self, path, data, token=None):
        body = json.dumps(data).encode("utf-8")
        request = Request(self.base + path, body,
                          {"Content-Type": "application/json", "X-MSV4-Token": token or ""},
                          method="POST")
        with urlopen(request, timeout=3) as response:
            return json.load(response)

    def test_session_export_import_and_measurement_parse(self):
        result = parse_metrics("SENSE_OUT=125.50 mV DRV_OUT=3100.25 mV (os 64)")
        self.assertEqual(result, {"sense_mv": 125.5, "drv_mv": 3100.25})
        self.store.append("rx", "SENSE_OUT=125.50 mV DRV_OUT=3100.25 mV", result)
        self.store.append("note", "Blade moved to marked depth")
        exported = self.store.export(self.store.active["id"])
        imported = self.store.import_session(exported)
        self.assertEqual(len(self.store.events), 2)
        self.assertEqual(self.store.events[0]["metrics"], result)
        self.assertEqual(self.store.events[1]["text"], "Blade moved to marked depth")
        self.assertEqual(imported["imported_from"], exported["session"]["id"])

    def test_local_api_and_token(self):
        page, headers = self.get("/")
        self.assertIn(b"MSV4 Lab", page)
        self.assertIn("default-src 'self'", headers["Content-Security-Policy"])
        self.assertIn(b".metric-grid", self.get("/lab.css")[0])
        self.assertIn(b"async function poll", self.get("/lab.js")[0])
        token = json.loads(self.get("/api/config")[0])["token"]
        with self.assertRaises(HTTPError) as denied:
            self.post("/api/note", {"text": "test"}, "wrong")
        self.assertEqual(denied.exception.code, 403)
        self.post("/api/note", {"text": "first dry reading"}, token)
        snapshot = json.loads(self.get("/api/state?after=0")[0])
        self.assertEqual(snapshot["events"][0]["text"], "first dry reading")
        self.assertEqual(snapshot["connection"], "disconnected")
        session_id = snapshot["session"]["id"]
        exported = json.loads(self.get(f"/api/export?id={session_id}")[0])
        self.assertEqual(exported["events"][0]["text"], "first dry reading")
        imported = self.post("/api/sessions/import", exported, token)
        self.assertEqual(imported["session"]["imported_from"], session_id)
        with self.assertRaises(HTTPError) as disconnected:
            self.post("/api/command", {"command": "read"}, token)
        self.assertEqual(disconnected.exception.code, 400)


if __name__ == "__main__":
    unittest.main()
