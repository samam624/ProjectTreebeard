#!/usr/bin/env python
"""Closed-loop test of moisture-percent calibrations.

A pot with a water balance, a sensor model, and a user who waters when the DISPLAY says to, so a
calibration that learns from where the user watered feeds its own output back in. The pot is a
plain water balance (constant uptake above a critical water level, proportional below it, demand
changing day to day); it is NOT built from the measured dry-down curve.

Three sensors: 'affine gain' (ions scale the water-dependent part about a fixed base),
'saturating' (coated electrode: ions are not a pure gain) and 'log conductance' (the MSV4 number:
ions are an offset). Waterings are full or partial, some carry fertilizer, later ones leach it.

Calibrations: 'current' is migration 027 + autocal.ts (--self-test holds it equal to
autocal_replay.py). The rest take one drained level per watering as the scale; see Plateau.

  python cloud/scripts/moisture_percent_closedloop.py --self-test
  python cloud/scripts/moisture_percent_closedloop.py --worlds 40
  python cloud/scripts/moisture_percent_closedloop.py --worlds 40 --base-error
  python cloud/scripts/moisture_percent_closedloop.py --worlds 40 --sensitivity

It shows which way each calibration fails and how they compare in these worlds. Its rates are
not predictions for a real pot.
"""
import sys, numpy as np

SPD = 288; DT = 1.0 / SPD
LOOK = int(18 * 12)                      # the user looks at 18:00

# ---------------------------------------------------------------- sensor models (reading from water w, ions g)
def obs_affine(w, g):   return 700.0 + 400.0 * g ** 0.7 * (0.3 + 0.7 * w)                 # gain about a fixed base
def obs_saturating(w, g):
    y = 0.8 * g * (0.3 + 0.7 * w) ** 2; return 700.0 + 900.0 * y / (1.0 + y)             # coated electrode, not a pure gain
def obs_log(w, g):      return 14000.0 + 10000.0 * np.log10(g * ((0.3 + 0.7 * w) / 0.3) ** 2)  # log conductance: ions are an offset
OBS = {"affine gain": (obs_affine, 700.0), "saturating": (obs_saturating, 700.0), "log conductance": (obs_log, None)}

# ---------------------------------------------------------------- the current algorithm (migration 027 + autocal.ts), uniform 5-min grid
def current_bounds(x, now, manual):
    lo = max(0, now - 21 * SPD); v = x[lo:now + 1]
    if len(v) < 50 or (now - lo) < 3 * SPD: return manual
    p5, p95 = np.percentile(v, [5, 95]); fd, fw = round(p5), round(p95); rng = p95 - p5
    if rng <= 0: return (fd, fw)
    dv = np.diff(v); cyc = []; last = None
    for i in np.where(dv >= 0.20 * rng)[0] + 1:
        if i < 6: continue
        pre = v[max(0, i - 24):i]; post = v[i:i + 37]; later = v[i + 72:i + 145]; prev = v[i - 1]
        if len(pre) < 6 or len(post) < 6: continue
        pm = np.median(pre)
        if np.percentile(post, 80) - pm < 0.5 * rng or abs(prev - pm) > 0.5 * rng or post.max() <= prev: continue
        mature = len(later) >= 12 and (np.median(later) - pm) >= 0.2 * rng
        if not (mature or (len(later) < 12 and (len(v) - 1 - i) <= 144)): continue
        cd = round(0.75 * prev + 0.25 * np.percentile(pre, 20)); cw = round(post.max())
        if cw <= cd: continue
        sup = min(max(len(pre) / 12, .25), 1) * min(max(len(post) / 18, .25), 1) * (min(max(len(later) / 24, .25), 1) if mature else 0.25)
        if last is None or (i - last) > 144: cyc.append([i, cd, cw, sup])
        else: cyc[-1][2] = max(cyc[-1][2], cw); cyc[-1][3] = max(cyc[-1][3], sup)
        last = i
    if not cyc or fw <= fd: return (fd, fw)
    fs = max(1, fw - fd); newest = cyc[-1][0]
    ws = np.array([c[3] * 0.5 ** ((newest - c[0]) / SPD / 7) for c in cyc])
    def robust(vals, floor):
        vals = np.asarray(vals, float)
        if len(vals) >= 3:
            c = np.median(vals); lim = max(floor, 2.5 * 1.4826 * np.median(np.abs(vals - c))); vals = np.clip(vals, c - lim, c + lim)
        return float((vals * ws).sum() / ws.sum())
    ed = robust([c[1] for c in cyc], max(1, fs * 0.05)); es = robust([c[2] - c[1] for c in cyc], max(1, fs * 0.10))
    ew = ws.sum(); pw = 0.20 if len(cyc) == 1 else 0.10
    ld = (ed * ew + fd * pw) / (ew + pw); ls = (es * ew + fs * pw) / (ew + pw)
    return (round(ld), round(ld + max(1, ls)))

# ---------------------------------------------------------------- candidates: one drained plateau per watering as the scale
class Plateau:
    """dry also: frozen = the level the user watered at during start-up, in scaled units, then never moved.
       mode: norm = scale the dry anchor with the plateau about base b (b=None: translate with it);
             raw  = no scaling: wet = median of recent plateaus, dry = lowest pre-watering reading.
       dry:  min  = lowest level ever watered at (in scaled units); knee = where the daily loss
             halves from its peak, less the user's own first-weeks offset, frozen after start-up."""
    def __init__(self, mode, dry, b, leak=0.3, knee_frac=0.5):
        self.mode, self.dry, self.b, self.leak, self.knee_frac = mode, dry, b, leak, knee_frac; self.boot_u = []; self.started = False
        self.P = None; self.uref = None; self.ev = []; self.knees = []; self.offs = []; self.frozen = None
        self.plats = []; self.rawmin = None; self.scan = 0; self.pending = None
    def to_u(self, x, P):   return (x - P) if self.b is None else (x - self.b) / (P - self.b)
    def from_u(self, u, P): return (P + u) if self.b is None else self.b + u * (P - self.b)
    def span(self):
        if self.P is None or self.uref is None: return None
        return self.P - self.from_u(self.uref, self.P)
    def update(self, x, now, boot):
        thr = 0.15 * self.span() if self.span() else None
        i = max(self.scan, 25)
        while self.pending is None and i <= now:                      # find the next watering step
            pre = np.median(x[i - 24:i]); t = thr if thr else 0.03 * abs(pre)
            if x[i] - pre >= t and (not self.ev or i - self.ev[-1][0] > 144): self.pending = (i, pre)
            i += 1
        self.scan = i
        if self.pending and now >= self.pending[0] + 144:              # its drained level, +6..12 h
            i0, pre = self.pending; Pn = float(np.median(x[i0 + 72:i0 + 145])); self.pending = None
            if Pn - pre < (0.5 * thr if thr else 0): self.scan = i0 + 145; return
            if self.P is None:
                self.P = Pn; self.ev.append((i0, pre, Pn, True)); self.plats.append(Pn); self.rawmin = pre; return
            full_prev = self.ev[-1][3]; Pprev = self.P
            upre = self.to_u(pre, Pprev)
            if boot: self.boot_u.append(upre)
            if self.dry == "min" or boot:
                self.uref = upre if self.uref is None else min(self.uref, upre)
            self.rawmin = min(self.rawmin, pre)
            if full_prev:                                              # knee of the cycle that just ended
                s = x[self.ev[-1][0] + 144:i0]
                if len(s) > 3 * SPD:
                    L = s[:-SPD] - s[SPD:]; lev = s[SPD // 2:SPD // 2 + len(L)]; k = int(np.argmax(L)); below = np.where(L[k:] <= self.knee_frac * L[k])[0]
                    if len(below) and L[k] > 0:
                        uk = self.to_u(lev[k + below[0]], Pprev); self.knees.append(uk)
                        if boot: self.offs.append(uk - upre)
            if not boot and not self.started:                         # start-up over: fix the anchor's definition
                self.started = True
                if self.dry in ("frozen", "knee") and self.boot_u: self.uref = float(np.median(self.boot_u))
                if self.dry == "knee" and self.offs: self.frozen = float(np.median(self.offs))
            if self.dry == "knee" and self.frozen is not None and self.knees: self.uref = float(np.median(self.knees[-5:])) - self.frozen
            tol = 0.10 * (Pprev - self.from_u(self.uref, Pprev))
            full = Pn >= Pprev - tol
            if self.mode == "norm": self.P = Pn if full else Pprev - self.leak * (Pprev - Pn)
            self.ev.append((i0, pre, Pn, full)); self.plats.append(Pn)
    def bounds(self):
        if self.P is None or self.uref is None: return None
        if self.mode == "raw": return (self.rawmin, float(np.median(self.plats[-5:])))
        return (self.from_u(self.uref, self.P), self.P)

def pct(x, b):
    d, w = b; w = max(w, d + 1e-9); return min(100.0, 100.0 * (x - d) / (w - d))

KIND = {"raw plateau+min": dict(mode="raw", dry="min"), "plateau+min": dict(mode="norm", dry="min"),
        "plateau+frozen": dict(mode="norm", dry="frozen"), "trusting+frozen": dict(mode="norm", dry="frozen", leak=1.0),
        "plateau+knee50": dict(mode="norm", dry="knee", knee_frac=0.5), "plateau+knee70": dict(mode="norm", dry="knee", knee_frac=0.7)}

# ---------------------------------------------------------------- one world
def run(seed, obs_name, algo, policy="display", days=170, boot_days=28, b_error=0.0, quant=0.0, p_fert=0.2, p_full=0.8):
    f, b_true = OBS[obs_name]; rng = np.random.default_rng(seed); n = days * SPD
    r0 = rng.uniform(0.11, 0.17); z = np.zeros(days)
    for d in range(1, days): z[d] = 0.6 * z[d - 1] + rng.normal(0, 0.2 * np.sqrt(1 - 0.36))
    season = np.linspace(1.0, rng.choice([1.0, 0.7]), days)
    tod = (np.arange(n) % SPD) / SPD
    lam = np.cumsum(np.repeat(r0 * np.exp(z) * season, SPD) * np.clip(1 + 0.8 * np.sin(2 * np.pi * (tod - 0.25)), 0.2, None)) * DT
    wcrit = rng.uniform(0.35, 0.55); wt = rng.uniform(0.20, 0.30)
    noise = rng.normal(0, 1, n); wob = np.sin(2 * np.pi * (tod - 0.6))
    ev_rng = np.random.default_rng(seed + 10_000); K = 400
    e_full = ev_rng.random(K) < p_full; e_frac = ev_rng.uniform(0.3, 0.6, K); e_ov = ev_rng.uniform(0, 1.2, K)
    e_fert = ev_rng.random(K) < p_fert; e_gain = ev_rng.uniform(1.6, 2.4, K); e_leach = ev_rng.uniform(0.3, 0.6, K)
    day_rng = np.random.default_rng(seed + 20_000); d_early = day_rng.random(days); d_skip = day_rng.random(days); d_cal = day_rng.integers(6, 9, K)
    span0 = f(1.0, 1.0) - f(wt, 1.0)
    st = dict(ws=1.0, l0=0.0, g=1.0, ov_amp=0.0, ov_i=-10**9)
    def wseg(l):                                           # water after cumulative demand l, since the last watering
        ws, dl = st["ws"], l - st["l0"]
        if ws > wcrit:
            lc = ws - wcrit; return np.where(dl < lc, ws - dl, wcrit * np.exp(-np.maximum(dl - lc, 0) / wcrit))
        return ws * np.exp(-dl / wcrit)
    x = np.empty(n); w = np.empty(n); gs = np.empty(n)
    def fill(i0, i1):
        idx = np.arange(i0, i1); ww = wseg(lam[idx]); w[idx] = ww; gs[idx] = st["g"]
        tr = st["ov_amp"] * np.exp(-np.clip((idx - st["ov_i"]) / 4.0, 0, 50)) * (idx >= st["ov_i"])
        xx = f(ww, st["g"]) + tr + 0.01 * span0 * wob[idx] + 0.002 * span0 * noise[idx]
        x[idx] = np.round(xx / quant) * quant if quant else xx
    k = 0; next_cal = 7
    A = None if algo in ("oracle", "current", "fixed raw") else Plateau(b=(None if b_true is None else b_true + b_error * 400.0), **KIND[algo])
    manual = (f(wt, 1.0), f(1.0, 1.0)); log = []; shown = []; true = []
    for d in range(days):
        a, z1 = d * SPD, (d + 1) * SPD
        fill(a, a + LOOK + 1); now = a + LOOK; boot = d < boot_days
        if A is not None: A.update(x, now, boot)
        if algo == "oracle" or boot:
            bnd = manual; disp = 100 * (w[now] - wt) / (1 - wt)
        else:
            bnd = manual if algo == "fixed raw" else current_bounds(x, now, manual) if algo == "current" else (A.bounds() or manual); disp = pct(x[now], bnd)
        if not boot: shown.append(pct(x[now], bnd) if algo != "oracle" else disp); true.append(min(100.0, 100 * (w[now] - wt) / (1 - wt)))
        if policy == "calendar" and not boot: water = d >= next_cal
        else: water = (disp <= 0 and d_skip[d] < 0.85) or (0 < disp <= 30 and d_early[d] < 0.15)
        water = water or w[now] <= 0.03
        if water and now + 1 < n:
            wnow = float(w[now]); full = bool(e_full[k]) or boot
            wnew = 1.0 if full else wnow + e_frac[k] * (1 - wnow)
            fert = (not boot) and full and bool(e_fert[k])
            if fert: st["g"] = min(4.0, st["g"] * e_gain[k])
            else: st["g"] = 1 + (st["g"] - 1) * (1 - e_leach[k] * (1.0 if full else 0.5))
            xpre = float(x[now]); st["ws"], st["l0"] = wnew, lam[now]; st["ov_i"] = now + 1; st["ov_amp"] = e_ov[k] * (f(wnew, st["g"]) - xpre)
            log.append(dict(day=d + 0.75, w=wnow, disp=disp, full=full, fert=fert, boot=boot, g=st["g"], dry=bnd[0], wet=bnd[1], i=now))
            k += 1; next_cal = d + int(d_cal[k])
        fill(a + LOOK + 1, z1)
    return dict(x=x, w=w, g=gs, log=log, wt=wt, wcrit=wcrit, boot=boot_days, f=f, shown=np.array(shown), true=np.array(true))

def score(r):
    L = [e for e in r["log"] if not e["boot"]]
    if len(L) < 6: return None
    third = len(L) // 3; ww = np.array([e["w"] for e in L]); dd = np.array([e["day"] for e in L]); iv = np.diff(dd)
    w = r["w"][r["boot"] * SPD:]; e = r["shown"] - r["true"]
    return dict(per30=30 * len(L) / (dd[-1] - dd[0]), w_first=ww[:third].mean(), w_last=ww[-third:].mean(), iv_first=iv[:third].mean(), iv_last=iv[-third:].mean(),
                past=100 * np.mean(w < 0.8 * r["wt"]), too_dry=100 * np.mean(w < 0.5 * r["wt"]), abs_err=np.median(np.abs(e)), wetter=100 * np.mean(e > 20), drier=100 * np.mean(e < -20), wt=r["wt"])

ALGOS = ["oracle", "fixed raw", "current", "raw plateau+min", "plateau+min", "trusting+frozen", "plateau+frozen", "plateau+knee50", "plateau+knee70"]
def table(worlds, policy, sensors=None, algos=None, **kw):
    print(); print(f"=== user policy: {policy}   {kw if kw else ''}  ({worlds} pots per cell)")
    print("%-16s %-16s %9s %11s %11s %13s %8s %8s | %8s %9s %9s" % ("sensor", "calibration", "waterings", "water level", "at watering", "days between", "past", "too dry", "shown-true", "shown >20", "shown >20"))
    print("%-16s %-16s %9s %11s %11s %13s %8s %8s | %8s %9s %9s" % ("", "", "/30 d", "first 1/3", "last 1/3", "first -> last", "% time", "% time", "median |.|", "wetter, %", "drier, %"))
    for o in (sensors or OBS):
        for a in (algos or ALGOS):
            S = [s for s in (score(run(1000 + i, o, a, policy, **kw)) for i in range(worlds)) if s]
            m = lambda key: np.nanmean([s[key] for s in S])
            print("%-16s %-16s %9.2f %11.3f %11.3f %6.1f -> %4.1f %8.1f %8.1f | %8.1f %9.1f %9.1f" % (o, a, m("per30"), m("w_first"), m("w_last"), m("iv_first"), m("iv_last"), m("past"), m("too_dry"), m("abs_err"), m("wetter"), m("drier")))
        print("%-16s the user's own target water level at watering: %.3f" % ("", m("wt")))

if __name__ == "__main__":
    if "--self-test" in sys.argv:
        import pandas as pd; from autocal_replay import sql_rises, derive   # the SQL/TS port, same directory
        r = run(7, "affine gain", "current"); x = r["x"]; t0 = pd.Timestamp("2026-01-01", tz="UTC")
        s = pd.Series(x, index=t0 + pd.to_timedelta(np.arange(len(x)) * 5, unit="min")); bad = 0
        for now in range(30 * SPD + LOOK, 160 * SPD, 9 * SPD + 77):
            a = current_bounds(x, now, (0, 1)); tn = s.index[now]; d = derive(sql_rises(s[s.index <= tn], tn), tn)
            ok = abs(a[0] - d["dry"]) <= 1 and abs(a[1] - d["wet"]) <= 1; bad += not ok
        print("self-test (port of the current algorithm against the SQL/TS port, %d times):" % 14, "PASS" if not bad else f"FAIL ({bad})"); sys.exit(bad)
    W = int(sys.argv[sys.argv.index("--worlds") + 1]) if "--worlds" in sys.argv else 30
    if "--sensitivity" in sys.argv:
        for pf, pfull in ((0.0, 0.8), (0.05, 0.8), (0.2, 0.5), (0.05, 0.5)):
            table(W, "display", sensors=["affine gain"], algos=["oracle", "current", "trusting+frozen", "plateau+frozen"], p_fert=pf, p_full=pfull)
    elif "--base-error" in sys.argv:
        for be in (-0.5, -0.25, 0.25): table(W, "display", sensors=["affine gain"], algos=["plateau+frozen"], b_error=be)
        table(W, "display", sensors=["affine gain", "saturating"], algos=["plateau+frozen", "current"], quant=13.0)
    else:
        table(W, "display"); table(W, "calendar", algos=["fixed raw", "current", "plateau+frozen", "trusting+frozen"])
