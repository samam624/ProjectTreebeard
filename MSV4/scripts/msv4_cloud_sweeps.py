"""Raw MSV4 sweeps from Supabase, with every electrode model fitted to each one.

Why: from application version 262172 a probe sends the averaged sweep its moisture fit was made
from in its scan response; the receiver forwards it and receiver-snapshot stores it in
msv4_raw_sweeps (migration 042). This fetches those rows, decodes them and fits the models of
msv4_raw_sweep_models.py to each, so a soil record can say which model's numbers follow the water
(MSV4/docs/MSV4_BOARD2_RAW_SWEEP_MODELS_2026-10-15.md).

    python MSV4/scripts/msv4_cloud_sweeps.py --sensor 11 --since-hours 48 --rs 3300 --cp 8.75 --cd0 13.71 \\
        --out data/sensor_logs/msv4_sensor11_sweep_fits.csv
    python MSV4/scripts/msv4_cloud_sweeps.py --self-test

One output row per sweep: the time, the wake, the 16 decoded values, and per model the residual,
mask C and R (the CPE also n). The probe's tip temperature and its own fitted Rsoil are not in
this table; join on sensor and wake with fetch_supabase_history.py --raw.
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "cloud" / "scripts"))
import fetch_supabase_history as cloud  # noqa: E402
import msv4_raw_sweep_models as models  # noqa: E402
from test_msv4_spectrum_fit import spec_phases  # noqa: E402

RECEIVER = "MSV2088A3104A7AC"
POINT_BYTES = 24


def decode_hex(hex_text: str) -> np.ndarray:
    """The 24 packed bytes (layout: the comment above SWEEP_RSP_LEN in the sketch) as 8 complex H."""
    raw = bytes.fromhex(hex_text)
    if len(raw) != POINT_BYTES:
        raise ValueError(f"a sweep is {POINT_BYTES} bytes, got {len(raw)}")

    def s12(v: int) -> int:
        return v - 4096 if v & 0x800 else v

    out = []
    for i in range(len(models.PROBE_N)):
        b0, b1, b2 = raw[3 * i: 3 * i + 3]
        out.append(complex(s12(b0 | ((b1 & 0x0F) << 8)), s12((b1 >> 4) | (b2 << 4))) / 2048.0)
    return np.array(out)


def as_sweep(h: np.ndarray) -> dict:
    n = np.array(models.PROBE_N)
    return {"n": n, "m": np.array([spec_phases(int(k)) for k in n]), "h": h}


def fetch(sensor: int, since_hours: float) -> list[dict]:
    sql = (
        "select sensor_id, wake, sweep_hex, observed_at from public.msv4_raw_sweeps "
        f"where receiver_id = '{RECEIVER}' and sensor_id = {int(sensor)} "
        f"and observed_at >= now() - interval '{float(since_hours)} hours' order by observed_at"
    )
    return cloud.run_query(sql)


def fit_rows(rows: list[dict], rs: float, cp: float, cd0: float) -> list[dict]:
    out = []
    for row in rows:
        h = decode_hex(row["sweep_hex"])
        result = {"observed_at": row["observed_at"], "sensor": row["sensor_id"], "wake": row["wake"]}
        for i, v in enumerate(h):
            result[f"re{i}"], result[f"im{i}"] = v.real, v.imag
        for name, fit in models.fit_all(as_sweep(h), rs, cp, cd0).items():
            d = models.derived(name, fit["p"], cd0)
            result[f"{name}_rms"] = fit["rms"]
            result[f"{name}_mask_pf"] = d["mask C"] * 1e12
            result[f"{name}_r_ohm"] = d["R"]
            if "n" in d:
                result[f"{name}_n"] = d["n"]
            if "Cd" in d:
                result[f"{name}_cd_pf"] = d["Cd"] * 1e12
        out.append(result)
    return out


def self_test() -> int:
    # Encoded here from the sketch's layout comment, independently of decode_hex().
    values = [0.8424 - 0.3624j, 0.0457 + 0.1967j, -0.0004 + 0.0001j, 1.5 - 1.5j,
              -1.0 + 0.99951171875j, 0.25 - 0.125j, -0.5 + 0.5j, 0.0]
    packed = bytearray()
    for v in values:
        re, im = (max(-2048, min(2047, round(x * 2048))) & 0xFFF for x in (v.real, v.imag))
        packed += bytes([re & 0xFF, (re >> 8) | ((im & 0x0F) << 4), im >> 4])
    got = decode_hex(packed.hex())
    want = np.array([complex(max(-2048, min(2047, round(v.real * 2048))),
                             max(-2048, min(2047, round(v.imag * 2048)))) / 2048 for v in values])
    failures = []
    if not np.array_equal(got, want):
        failures.append(f"decode: {got} != {want}")
    # A synthetic lumped electrode through the firmware's measurement model, quantized as the probe
    # sends it, must come back near its own parameters.
    rs, cp, cd0 = 3300.0, 8.75e-12, 13.71e-12
    cm, gs = 600e-12, 1 / 500.0
    sweep = as_sweep(np.zeros(8, complex))
    clean = models.predict(sweep["n"], sweep["m"],
                           lambda w: 1j * w * 117e-12 + 1 / (1 / (1j * w * cm) + 1 / gs), rs, cp)
    q = np.round(clean.real * 2048) / 2048 + 1j * np.round(clean.imag * 2048) / 2048
    row = fit_rows([{"observed_at": "t", "sensor_id": 11, "wake": 1,
                     "sweep_hex": encode(q)}], rs, cp, cd0)[0]
    if abs(row["lumped_r_ohm"] / 500.0 - 1) > 0.02:
        failures.append(f"lumped R {row['lumped_r_ohm']:.1f}, want 500")
    if abs(row["lumped_cd_pf"] - 117.0) > 3.0:
        failures.append(f"lumped Cd {row['lumped_cd_pf']:.1f}, want 117")
    for line in failures:
        print("FAIL", line)
    print(f"self-test: {'FAILED' if failures else 'passed'} ({len(failures)} failure(s))")
    return 1 if failures else 0


def encode(h: np.ndarray) -> str:
    packed = bytearray()
    for v in h:
        re, im = (max(-2048, min(2047, int(round(x * 2048)))) & 0xFFF for x in (v.real, v.imag))
        packed += bytes([re & 0xFF, (re >> 8) | ((im & 0x0F) << 4), im >> 4])
    return packed.hex()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--sensor", type=int)
    parser.add_argument("--since-hours", type=float, default=48.0)
    parser.add_argument("--rs", type=float, help="R15 in ohms (board 2: 3300)")
    parser.add_argument("--cp", type=float, help="Cp in pF (board 2: 8.75)")
    parser.add_argument("--cd0", type=float, help="the board's Cd in air, pF (board 2: 13.71)")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not (args.sensor and args.rs and args.cp and args.cd0 and args.out):
        parser.error("need --sensor, --rs, --cp, --cd0 and --out")
    rows = fetch(args.sensor, args.since_hours)
    print(f"{len(rows)} sweep(s) for sensor {args.sensor} in the last {args.since_hours:g} h")
    if not rows:
        return 1
    fitted = fit_rows(rows, args.rs, args.cp * 1e-12, args.cd0 * 1e-12)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fields = list(dict.fromkeys(k for r in fitted for k in r))
    with args.out.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(fitted)
    for name in models.MODELS:
        rms = [r[f"{name}_rms"] for r in fitted]
        print(f"  {name:12} median residual {np.median(rms) * 1e4:6.1f}e-4 of drive")
    print(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
