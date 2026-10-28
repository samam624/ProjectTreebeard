"""What a plain single-R, single-C electrode would look like against sensor 11's raw sweeps.

msv4_raw_sweep_models.py already fits the firmware's lumped model, a CPE, a de Levie line and a
two-branch model. This adds the simplest circuits a reader would ask about and sets them beside
those, on the SAME stored sweeps and through the SAME measurement model (R15, Cp, harmonic fold):

  series RC          R in series with C, nothing else          R, C
  series RC + Cd0    the same with the board's own air Cd      R, C          (= "lumped-fixed")
  parallel RC        G in parallel with C                      R, C          (the board's Cd is inside C)
  lumped             firmware model, Cd free                   Cm, Gs, Cd
  cpe / tline / two-branch                                     as msv4_raw_sweep_models.py

    python MSV4/scripts/msv4_single_rc_fit.py --sensor 11 --since-hours 336 --rs 3300 --cp 8.75 --cd0 13.71 \\
        --out data/sensor_logs/msv4_sensor11_single_rc_fits.csv --fig-dir data/sensor_logs

The residual is the RMS of (model - measured) over the 8 complex points, in 1e-4 of drive. A model
with more free parameters can only do better; compare parameter count with residual.
"""
from __future__ import annotations

import argparse
import csv
import statistics
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

import msv4_cloud_sweeps as cs
import msv4_raw_sweep_models as models
from test_msv4_spectrum_fit import FE_CLOCK_HZ

# name: (parameter names, log-scaled flags, admittance)
models.MODELS["series-RC"] = (("R", "C"), (True, True),
                              lambda w, p, cd0: 1.0 / (p[0] + 1.0 / (1j * w * p[1])))
models.MODELS["series-RC+Cd0"] = (("R", "C"), (True, True),
                                  lambda w, p, cd0: 1j * w * cd0 + 1.0 / (p[0] + 1.0 / (1j * w * p[1])))
models.MODELS["parallel-RC"] = (("R", "C"), (True, True),
                                lambda w, p, cd0: 1.0 / p[0] + 1j * w * p[1])
# R above is named "R" so the bounds table treats it as ohms.

ORDER = ("series-RC", "series-RC+Cd0", "parallel-RC", "lumped", "cpe", "tline", "two-branch")
PARAMS = {"series-RC": 2, "series-RC+Cd0": 2, "parallel-RC": 2, "lumped": 3, "cpe": 3, "tline": 3, "two-branch": 4}


def starts_simple(name: str, lump: dict, cd0: float) -> list:
    cm, gs, cd = lump["Cm"], lump["Gs"], lump["Cd"]
    r = 1.0 / gs
    ctot = max(cm + cd - cd0, cm)
    if name == "series-RC":
        return [(r * k, c) for k in (0.3, 1.0, 3.0) for c in (ctot, cm, ctot + cd0)]
    if name == "series-RC+Cd0":
        return [(r * k, c) for k in (0.3, 1.0, 3.0) for c in (ctot, cm)]
    return [(r * k, c) for k in (0.3, 1.0, 3.0) for c in (cd, ctot + cd0, cd0)]   # parallel-RC


def fit_models(sweep: dict, rs: float, cp: float, cd0: float) -> dict:
    lump = models.fit_model("lumped", sweep, rs, cp, 0.0, [(500e-12, g, 5e-12) for g in (1e-5, 1e-4, 1e-3, 1e-2)])
    out = {"lumped": lump}
    for name in ("series-RC", "series-RC+Cd0", "parallel-RC"):
        out[name] = models.fit_model(name, sweep, rs, cp, cd0, starts_simple(name, lump["p"], cd0))
    for name in ("cpe", "tline", "two-branch"):
        out[name] = models.fit_model(name, sweep, rs, cp, cd0, models.seeds(name, lump["p"], cd0))
    return out


def prediction(name: str, p: dict, sweep: dict, rs: float, cp: float, cd0: float) -> np.ndarray:
    names, _, ye = models.MODELS[name]
    values = [p[n] for n in names]
    return models.predict(sweep["n"], sweep["m"], lambda w: ye(w, values, cd0), rs, cp)


def parse_time(text: str) -> datetime:
    return datetime.fromisoformat(text.replace(" ", "T").replace("+00", "+00:00"))


def figures(rows, fits, picks, rs, cp, cd0, fig_dir: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.dates as mdates
    import matplotlib.pyplot as plt

    colors = {"series-RC": "#d62728", "series-RC+Cd0": "#ff7f0e", "parallel-RC": "#8c564b", "lumped": "#1f77b4",
              "cpe": "#2ca02c", "tline": "#9467bd", "two-branch": "#17becf"}
    styles = {"series-RC": "--", "series-RC+Cd0": "--", "parallel-RC": "--"}

    fig, axes = plt.subplots(2, len(picks), figsize=(5.2 * len(picks), 8), squeeze=False)
    for col, (label, i) in enumerate(picks):
        sweep = cs.as_sweep(cs.decode_hex(rows[i]["sweep_hex"]))
        f = np.array([FE_CLOCK_HZ / n for n in sweep["n"]]) / 1e6
        h = sweep["h"]
        for r, (fn, unit) in enumerate(((np.abs, "|H| (fraction of drive)"), (lambda z: np.degrees(np.angle(z)), "phase, degrees"))):
            ax = axes[r][col]
            for name in ORDER:
                ax.plot(f, fn(prediction(name, fits[i][name]["p"], sweep, rs, cp, cd0)), styles.get(name, "-"),
                        color=colors[name], lw=1.3, label=f"{name}  ({fits[i][name]['rms'] * 1e4:.0f})")
            ax.plot(f, fn(h), "ko", ms=6, zorder=5, label="measured")
            ax.set_xscale("log")
            ax.set_xlabel("MHz")
            ax.set_ylabel(unit)
            ax.grid(alpha=0.25)
            if r == 0:
                ax.set_title(label, fontsize=10)
            if r == 1:
                ax.legend(fontsize=7, loc="best", title="residual, 1e-4 of drive", title_fontsize=7)
    fig.suptitle("Sensor 11: measured sweep against each circuit (dashed = single R and C variants)", y=0.995)
    fig.tight_layout()
    out = fig_dir / "msv4_sensor11_single_rc_sweeps.png"
    fig.savefig(out, dpi=125)
    plt.close(fig)
    print(out)

    times = [parse_time(r["observed_at"]).astimezone() for r in rows]
    fig, ax = plt.subplots(figsize=(14, 6))
    for name in ORDER:
        ys = [f[name]["rms"] * 1e4 for f in fits]
        # hourly-ish smoothing: median over 12 sweeps
        sm = [statistics.median(ys[max(0, k - 6):k + 7]) for k in range(len(ys))]
        ax.plot(times, sm, styles.get(name, "-"), color=colors[name], lw=1.3, label=f"{name} ({PARAMS[name]} params)")
    ax.set_yscale("log")
    ax.set_ylabel("fit residual, 1e-4 of drive (rolling median of 13 sweeps)")
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%m-%d", tz=times[0].tzinfo))
    ax.grid(alpha=0.25, which="both")
    ax.legend(ncol=4, fontsize=8, loc="upper center", bbox_to_anchor=(0.5, -0.08), frameon=False)
    ax.set_title("Sensor 11: how well each circuit describes every stored sweep (lower = closer)")
    fig.tight_layout()
    out = fig_dir / "msv4_sensor11_single_rc_residuals.png"
    fig.savefig(out, dpi=125)
    plt.close(fig)
    print(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sensor", type=int, required=True)
    parser.add_argument("--since-hours", type=float, default=336.0)
    parser.add_argument("--rs", type=float, required=True)
    parser.add_argument("--cp", type=float, required=True, help="pF")
    parser.add_argument("--cd0", type=float, required=True, help="pF")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--fig-dir", type=Path, required=True)
    parser.add_argument("--pick", action="append", default=[],
                        help="UTC time 'YYYY-MM-DD HH:MM' of a sweep to draw; label=time also accepted; repeatable")
    args = parser.parse_args()
    rs, cp, cd0 = args.rs, args.cp * 1e-12, args.cd0 * 1e-12

    rows = cs.fetch(args.sensor, args.since_hours)
    sweeps = [cs.as_sweep(cs.decode_hex(r["sweep_hex"])) for r in rows]
    fits = [fit_models(s, rs, cp, cd0) for s in sweeps]

    fields = ["observed_at", "wake", "lumped_R_ohm"] + [f"{m}_rms_e4" for m in ORDER]
    with args.out.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(fields)
        for r, f in zip(rows, fits):
            w.writerow([r["observed_at"], r["wake"], f"{1.0 / f['lumped']['p']['Gs']:.1f}"]
                       + [f"{f[m]['rms'] * 1e4:.2f}" for m in ORDER])
    print(f"{len(rows)} sweeps, {rows[0]['observed_at']} to {rows[-1]['observed_at']} -> {args.out}")

    # Median residual by the lumped R band: wet soil is low R.
    bands = ((0, 2500, "wet   (lumped R < 2.5 kohm)"), (2500, 8000, "moist (2.5-8 kohm)"),
             (8000, 14000, "drier (8-14 kohm)"), (14000, 1e9, "dry   (> 14 kohm)"))
    print("\nmedian residual, 1e-4 of drive, by the lumped fit's R")
    print(f"  {'band':28} {'n':>4}  " + " ".join(f"{m:>13}" for m in ORDER))
    for lo, hi, label in bands:
        idx = [i for i, f in enumerate(fits) if lo <= 1.0 / f["lumped"]["p"]["Gs"] < hi]
        if idx:
            print(f"  {label:28} {len(idx):4d}  "
                  + " ".join(f"{statistics.median(fits[i][m]['rms'] for i in idx) * 1e4:13.1f}" for m in ORDER))
    print("\nparameter count: " + ", ".join(f"{m} {PARAMS[m]}" for m in ORDER))

    picks = []
    for spec in args.pick:
        label, _, text = spec.rpartition("=") if "=" in spec else ("", "", spec)
        target = parse_time(text + ":00+00:00" if len(text) <= 16 else text)
        i = min(range(len(rows)), key=lambda k: abs((parse_time(rows[k]["observed_at"]) - target).total_seconds()))
        t = parse_time(rows[i]["observed_at"]).astimezone()
        r_ohm = 1.0 / fits[i]["lumped"]["p"]["Gs"]
        picks.append((f"{label or 'sweep'}: {t:%m-%d %H:%M}, lumped R {r_ohm:,.0f} ohm", i))
    if picks:
        figures(rows, fits, picks, rs, cp, cd0, args.fig_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
