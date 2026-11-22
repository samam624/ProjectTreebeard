"""Offline regressions for receiver exports and post-update progress verification."""
import asyncio
import csv
import importlib.util
from pathlib import Path
import sys
import tempfile
import sqlite3
import re
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'MSV4/scripts'))
import msv4_adv_scan as scan
spec = importlib.util.spec_from_file_location('history', ROOT / 'cloud/scripts/fetch_supabase_history.py')
history = importlib.util.module_from_spec(spec)
spec.loader.exec_module(history)


class ReviewRegressions(unittest.TestCase):
    def test_cached_first_packet_does_not_prove_two_wakes(self):
        class Scanner:
            def __init__(self, callback):
                self.callback = callback

            async def start(self):
                for counter in [40, 41, 41, 41]:
                    data = scan._encode(12, counter, 3550, 3814, scan.FLAG_MSV4, 107, 6710, 88, 15)
                    self.callback(SimpleNamespace(address='fixture', name='fixture'),
                                  SimpleNamespace(manufacturer_data={scan.COMPANY_ID: data},
                                                  service_data={}, local_name='fixture', rssi=-55))

            async def stop(self):
                pass

        with patch.dict(sys.modules, {'bleak': SimpleNamespace(BleakScanner=Scanner)}):
            self.assertEqual(asyncio.run(scan.scan(0.001, 12, None)), {12: [41]})

    def test_interrupted_verification_fails(self):
        with patch.object(sys, 'argv', ['scan', '--sensor', '12', '--require-progress']), \
                patch.object(scan, 'scan', side_effect=KeyboardInterrupt):
            self.assertNotEqual(scan.main(), 0)

    def test_mask_resistance_and_floor_follow_r15(self):
        self.assertIn('918 kohm', scan.describe_mask_check('A+20.0 B+0.0 mV', 5600))
        self.assertNotIn('DC path', scan.describe_mask_check('A+10.0 B+0.0 mV', 5600))

    def isolation_check(self, rows):
        """Run the verifier against an in-memory table shaped like production, unique index included."""
        spec = importlib.util.spec_from_file_location('isolation', ROOT / 'cloud/scripts/verify_multi_receiver_isolation.py')
        isolation = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(isolation)
        database = sqlite3.connect(':memory:')
        self.addCleanup(database.close)
        database.row_factory = sqlite3.Row
        database.execute('create table msv2_sensor_history(id integer primary key, receiver_id, sensor_id, wake, observation_id, moisture, raw_fdc_counts, raw_battery_mv, received_at)')
        database.execute('create unique index obs on msv2_sensor_history(receiver_id, observation_id) where observation_id is not null')
        database.executemany('insert into msv2_sensor_history(receiver_id, sensor_id, wake, observation_id, moisture, raw_fdc_counts, raw_battery_mv, received_at) values(?,?,?,?,?,?,?,?)', rows)

        def query(sql):
            sql = ' '.join(sql.split())  # as run_query() sends it: one line, so "--" would eat the rest
            if 'with w as' in sql:
                sql = re.sub(r"now\(\) - interval '[^']+'", "'2026-11-21 08:00:00'", sql)
                sql = sql.replace('extract(epoch from (a.received_at - b.received_at))',
                                  '(julianday(a.received_at) - julianday(b.received_at)) * 86400')
                return [dict(database.execute(sql).fetchone())]
            if 'pg_get_functiondef' in sql:
                return [{'d': ''}]
            return [{'receiver_id': r, 'household_id': r, 'last_seen_at': 't'} for r in ['A', 'B']]

        with patch.object(isolation, 'run_query', side_effect=query), patch.object(sys, 'argv', ['verify']):
            return isolation.main()

    def test_two_receivers_can_observe_counter_one_after_a_reboot(self):
        rows = [(receiver, 12, 1, receiver + hour, 17, raw, 4000, '2026-11-21 ' + hour + ':00:00')
                for receiver in ['A', 'B'] for hour, raw in [('12', 101), ('13', 102)]]
        self.assertEqual(self.isolation_check(rows), 0)

    def test_one_wake_stored_twice_by_a_receiver_fails(self):
        rows = [('A', 12, 7, 'A-first', 17, 101, 4000, '2026-11-21 12:00:00'),
                ('A', 12, 7, 'A-retry', 17, 101, 4000, '2026-11-21 12:00:04')]
        self.assertNotEqual(self.isolation_check(rows), 0)

    def test_receiver_caches_include_late_observations(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(history, 'CACHE_DIR', Path(directory)):
            for receiver in ['A', 'B']:
                rows = [{'id': 1, 'receiver_id': receiver, 'sensor_id': 4,
                         'received_at': '2026-11-21 15:00:00+00:00', 'moisture': 111}]
                with patch.object(sys, 'argv', ['history', '--receiver', receiver, '--sensor', '4', '--cache']), \
                        patch.object(history, 'run_query', side_effect=lambda sql: rows):
                    self.assertEqual(history.main(), 0)
                    rows.insert(0, {**rows[0], 'id': 2, 'received_at': '2026-11-21 14:59:00+00:00'})
                    self.assertEqual(history.main(), 0)
                with (Path(directory) / receiver / 'sensor4.csv').open() as handle:
                    exported = list(csv.DictReader(handle))
                self.assertEqual(len(exported), 2)
                self.assertEqual({r['receiver_id'] for r in exported}, {receiver})


if __name__ == '__main__':
    unittest.main()
