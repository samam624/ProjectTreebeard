#!/usr/bin/env python3
"""Pull one receiver snapshot and post it to the mock/cloud API."""

from __future__ import annotations

import argparse
import json
from urllib.error import HTTPError
from urllib.request import Request, urlopen


def get_json(url: str) -> dict:
    with urlopen(url, timeout=10) as response:
        return json.loads(response.read().decode("utf-8"))


def post_json(url: str, payload: dict, token: str = "") -> dict:
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = Request(url, data=body, headers=headers, method="POST")
    try:
        with urlopen(request, timeout=10) as response:
            return json.loads(response.read().decode("utf-8"))
    except HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise SystemExit(f"POST failed: HTTP {exc.code} {detail}") from exc


def main() -> None:
    parser = argparse.ArgumentParser(description="Relay one MSV2 receiver snapshot to a cloud/mock endpoint.")
    parser.add_argument("--receiver", required=True, help="Receiver base URL, e.g. http://10.0.0.236")
    parser.add_argument("--cloud", default="http://127.0.0.1:8088", help="Cloud base URL")
    parser.add_argument("--token", default="", help="Optional receiver API token")
    args = parser.parse_args()

    receiver_base = args.receiver.rstrip("/")
    cloud_base = args.cloud.rstrip("/")
    snapshot = get_json(f"{receiver_base}/snapshot")
    receiver_id = snapshot.get("receiverId")
    if not receiver_id:
        raise SystemExit("Snapshot did not include receiverId")

    target = f"{cloud_base}/api/v1/receivers/{receiver_id}/snapshot"
    result = post_json(target, snapshot, args.token)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
