#!/usr/bin/env python
"""Read-only check that several DisplayCenter receivers share the cloud without touching each
other's data. Written 2026-11-21 when the second receiver (build two) joined the main one.

Every statement is a SELECT, run through the linked Supabase CLI like fetch_supabase_history.py.
Nothing here writes, so it is safe to run at any time, including against the live project.

Checks:
  1. every receiver is claimed by its own household (a shared household would let one
     receiver's share token read, and clear, the other's history)
  2. no receiver's history shrank below a floor you pass (--floor RECEIVER=ROWS), e.g. the row
     count recorded in a backup manifest; this is the "nothing was deleted" check
  3. a probe packet heard by two receivers carries identical values under both, and no
     receiver stored the same packet twice. Packets are matched on (sensor, wake), so once the
     receivers are in different houses a different probe that happens to reuse a sensor ID and
     wake number inside the window would show as a mismatch; check its rows before worrying.
  4. the bulk history-delete function is gone (migration 044), or, before 044 is applied, still
     refuses receivers outside the caller's household

  python cloud/scripts/verify_multi_receiver_isolation.py --hours 6 --floor MSV2088A3104A7AC=1027000
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

SUPABASE_CLI = shutil.which("supabase.cmd") or shutil.which("supabase") or "supabase"
SUPABASE_WORKDIR = Path(__file__).resolve().parents[1]
RECEIVER_ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")


def run_query(sql: str) -> list[dict]:
    # supabase.cmd is a batch wrapper on Windows, and cmd.exe drops everything after the first
    # newline of an argument, so the SQL goes over as one line.
    sql = " ".join(sql.split())
    result = subprocess.run(
        [SUPABASE_CLI, "db", "query", "--linked", "--workdir", str(SUPABASE_WORKDIR), sql],
        capture_output=True, text=True, encoding="utf-8", timeout=300,
    )
    out = result.stdout
    try:
        payload = json.loads(out[out.index("{"):])
    except ValueError:
        raise SystemExit(f"supabase query failed: {result.stderr.strip() or out.strip()}")
    if "rows" not in payload:
        raise SystemExit(f"supabase query failed: {payload}")
    return payload["rows"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--hours", type=float, default=6.0, help="window for the cross-receiver packet check")
    parser.add_argument("--floor", action="append", default=[], metavar="RECEIVER=ROWS",
                        help="fail if that receiver holds fewer history rows than ROWS (repeatable)")
    args = parser.parse_args()

    failures: list[str] = []

    def check(ok: bool, label: str, detail: str) -> None:
        print(("PASS " if ok else "FAIL ") + label + (f" :: {detail}" if detail else ""))
        if not ok:
            failures.append(label)

    receivers = run_query(
        "select r.receiver_id, r.last_seen_at, c.household_id from msv2_receivers r "
        "left join msv2_receiver_claims c using (receiver_id) order by r.first_seen_at")
    print("receivers:")
    for r in receivers:
        print(f"  {r['receiver_id']}  household={r['household_id']}  last_seen={r['last_seen_at']}")

    households: dict[str, list[str]] = {}
    for r in receivers:
        if r["household_id"]:
            households.setdefault(r["household_id"], []).append(r["receiver_id"])
    shared = {h: ids for h, ids in households.items() if len(ids) > 1}
    check(not shared, "each claimed receiver has its own household",
          f"shared: {shared}" if shared else f"{len(households)} households")

    for spec in args.floor:
        receiver_id, _, rows = spec.partition("=")
        if not RECEIVER_ID_RE.match(receiver_id) or not rows.isdigit():
            raise SystemExit(f"bad --floor {spec!r}")
        count = run_query(f"select count(*) as n from msv2_sensor_history where receiver_id = '{receiver_id}'")[0]["n"]
        check(count >= int(rows), f"{receiver_id} history not below floor", f"{count} rows, floor {rows}")

    hours = float(args.hours)
    stats = run_query(f"""
        with w as (select * from msv2_sensor_history where received_at > now() - interval '{hours} hours')
        select
          (select count(*) from (select receiver_id, sensor_id, wake from w group by 1,2,3 having count(*) > 1) d) as dupes,
          (select count(*) from w a join w b on a.sensor_id = b.sensor_id and a.wake = b.wake
             and a.receiver_id < b.receiver_id) as shared_packets,
          (select count(*) from w a join w b on a.sensor_id = b.sensor_id and a.wake = b.wake
             and a.receiver_id < b.receiver_id
             where a.moisture is distinct from b.moisture
                or a.raw_fdc_counts is distinct from b.raw_fdc_counts
                or a.raw_battery_mv is distinct from b.raw_battery_mv) as mismatches""")[0]
    check(stats["dupes"] == 0, "no receiver stored a packet twice", f"{stats['dupes']} duplicated (sensor, wake)")
    check(stats["mismatches"] == 0, "packets heard by two receivers agree",
          f"{stats['shared_packets']} shared packets, {stats['mismatches']} differ")

    # Migration 044 drops the function outright; before 044 it must at least keep its guard.
    definition = run_query(
        "select coalesce((select pg_get_functiondef(p.oid) from pg_proc p "
        "where p.proname = 'msv2_clear_share_sensor_history'), '') as d")[0]["d"]
    check(not definition or "receiver is not linked to this household" in definition,
          "bulk history delete is gone (044) or still guarded",
          "function dropped" if not definition else "function present, guard intact - 044 not applied")

    print("ALL PASS" if not failures else f"{len(failures)} FAILED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
