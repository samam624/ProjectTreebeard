// MSV4 lock-in front-end simulation, ported from MSV4_Probe_Bringup_BLE.ino (2026-10-01).
//
// Time-domain RK4 model of DRV -> R15 -> SENSE loaded by Cp + Cd + (Cm series Gs), the U4 chopper
// window (READ_CNTL HIGH parks the filter on GND), the firmware's fitPhases / specMeasure / specModel /
// specFit, and a runSpectrum() that mirrors `spectrum ab` / `spectrum open`. Media are board 1's
// 2026-10-01 bring-up fits plus the 2026-09-19 field-solve soil prediction.
// The same code is embedded in MSV4/docs/MSV4_LOCKIN_WALKTHROUGH_2026-10-01.html.
// Self-test: node MSV4/scripts/test_msv4_lockin_sim.js
const FCLK = 16e6, VD = 3300, RS = 1000, CP = 8.8e-12, TAU_F = 100e-6;
const SETTLE_MS = 5, NOISE_MV = 0.5;
const SPEC_N = [64, 48, 32, 24, 16, 12, 8, 6];
const MEDIA = {
  open:  { label: 'Open', open: true, Cm: 1, Gs: 0, Cd: 0,
           note: 'U3 /OE high: both plates float. Only the SENSE node\'s own Cp (8.8 pF, measured) loads the divider.' },
  air:   { label: 'Air', Cm: 20e-12, Gs: 1e-14, Cd: 17e-12,
           note: 'Board 1 in air: no conduction (fit Rsoil 10¹⁴–10¹⁷ Ω), Cd 14–20 pF.' },
  towel: { label: 'Damp towel', Cm: 26.7e-12, Gs: 1 / 5900, Cd: 29e-12,
           note: 'Board 1 in a damp paper towel: Rsoil 5.9 kΩ, Cm 26.7 pF, Cd 29 pF.' },
  hand:  { label: 'Hand grip', Cm: 53e-12, Gs: 1 / 1750, Cd: 140e-12,
           note: 'Board 1 gripped by hand: Rsoil 1.6–1.9 kΩ, Cm 51–55 pF, Cd 134–147 pF.' },
  soil:  { label: 'Moist soil', Cm: 671e-12, Gs: 1 / 355, Cd: 5e-12, predicted: true,
           note: 'Field-solve prediction for moist soil (Cm 671 pF, Rsoil 355 Ω). Not yet measured on hardware.' },
};

const cx = {
  add: (a, b) => [a[0] + b[0], a[1] + b[1]],
  sub: (a, b) => [a[0] - b[0], a[1] - b[1]],
  mul: (a, b) => [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]],
  div: (a, b) => { const d = b[0] * b[0] + b[1] * b[1]; return [(a[0] * b[0] + a[1] * b[1]) / d, (a[1] * b[0] - a[0] * b[1]) / d]; },
  scale: (k, a) => [k * a[0], k * a[1]],
  conj: a => [a[0], -a[1]],
  abs: a => Math.hypot(a[0], a[1]),
  arg: a => Math.atan2(a[1], a[0]),
};

function specPhases(n) { for (const m of [16, 12, 8, 6, 4]) if (m <= n && n % m === 0) return m; return 0; }

// Firmware specNodeH: p = [Cm, Gs, Cd]
function nodeH(w, p, cp) {
  const zm = [0, -1 / (w * p[0])];
  const br = cx.div([1, 0], cx.add(zm, [1 / p[1], 0]));
  return cx.div([1, 0], [1 + RS * br[0], RS * (br[1] + w * p[2] + w * cp)]);
}
function mediumH(w, m) {
  if (m.open) return cx.div([1, 0], [1, RS * w * CP]);
  return nodeH(w, [m.Cm, m.Gs, m.Cd], CP);
}
// Firmware specModel: predicted normalised bin-1 with harmonic aliasing.
function specModel(n, m, p, cp) {
  const w1 = 2 * Math.PI * FCLK / n;
  let num = [0, 0], den = 0;
  for (let k = -8; k <= 8; k++) {
    const h = 1 + k * m;
    if ((h & 1) === 0 || h === 0 || h > 63 || h < -63) continue;
    const wt = 1 / (h * h);
    let hv = nodeH(w1 * Math.abs(h), p, cp);
    if (h < 0) hv = cx.conj(hv);
    num = cx.add(num, cx.scale(wt, hv));
    den += wt;
  }
  return cx.scale(1 / den, num);
}

// Time-domain steady state of one period: DRV square (0/3300 mV) -> R15 -> SENSE node loaded by
// Cp + Cd and the series Cm-Gs branch. RK4 with sample edges aligned to timer ticks.
const simCache = new Map();
function simulate(mkey, N) {
  const key = mkey + ':' + N;
  if (simCache.has(key)) return simCache.get(key);
  const m = MEDIA[mkey];
  const SPT = Math.max(8, Math.ceil(960 / N)), P = N * SPT, T = N / FCLK, dt = T / P;
  const nsub = Math.max(1, Math.ceil(dt / 0.4e-9)), h = dt / nsub;
  const Ct = CP + (m.open ? 0 : m.Cd), Gs = m.open ? 0 : m.Gs, Cm = m.open ? 1 : m.Cm;
  const periods = Math.max(6, Math.ceil(16e-6 / T));
  const sense = new Float64Array(P), drv = new Float64Array(P);
  let v = VD / 2, vc = VD / 2;
  const dv = (u, v, vc) => ((u - v) / RS - Gs * (v - vc)) / Ct;
  const dvc = (v, vc) => Gs * (v - vc) / Cm;
  for (let per = 0; per < periods; per++) {
    for (let i = 0; i < P; i++) {
      const u = Math.floor(i / SPT) < N / 2 ? VD : 0;
      const v0 = v;
      for (let s = 0; s < nsub; s++) {
        const k1 = dv(u, v, vc), l1 = dvc(v, vc);
        const k2 = dv(u, v + h / 2 * k1, vc + h / 2 * l1), l2 = dvc(v + h / 2 * k1, vc + h / 2 * l1);
        const k3 = dv(u, v + h / 2 * k2, vc + h / 2 * l2), l3 = dvc(v + h / 2 * k2, vc + h / 2 * l2);
        const k4 = dv(u, v + h * k3, vc + h * l3), l4 = dvc(v + h * k3, vc + h * l3);
        v += h / 6 * (k1 + 2 * k2 + 2 * k3 + k4);
        vc += h / 6 * (l1 + 2 * l2 + 2 * l3 + l4);
      }
      // Trapezoid value over the sample interval, so the chopper integral is second-order.
      if (per === periods - 1) { sense[i] = (v0 + v) / 2; drv[i] = u; }
    }
  }
  const out = { N, SPT, P, sense, drv };
  simCache.set(key, out);
  return out;
}

// Chopper (READ_CNTL) is HIGH on ticks [k, k+N/2) mod N, which parks U4 on GND. The filter sees the
// signal only while READ_CNTL is LOW, so its DC value is the mean of signal x (READ_CNTL low).
function chopHigh(tick, k, N) { return (((tick - k) % N) + N) % N < N / 2; }
function lockin(sim, k, ch) {
  const arr = ch === 's' ? sim.sense : sim.drv;
  let acc = 0;
  for (let i = 0; i < sim.P; i++) if (!chopHigh(Math.floor(i / sim.SPT), k, sim.N)) acc += arr[i];
  return acc / sim.P;
}

function rng(seed) {
  let a = seed >>> 0;
  const u = () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  return () => { let s = 0; for (let i = 0; i < 6; i++) s += u(); return (s - 3) * Math.SQRT2; };  // ~N(0,1)
}

// Firmware fitPhases
function fitPhases(v, ks, n) {
  const m = v.length; let p = 0, c = 0, s = 0;
  for (let i = 0; i < m; i++) { const th = 2 * Math.PI * ks[i] / n; p += v[i]; c += v[i] * Math.cos(th); s += v[i] * Math.sin(th); }
  p /= m; c *= 2 / m; s *= 2 / m;
  let ss = 0;
  for (let i = 0; i < m; i++) { const th = 2 * Math.PI * ks[i] / n; const e = v[i] - (p + c * Math.cos(th) + s * Math.sin(th)); ss += e * e; }
  const a = Math.hypot(c, s);
  let phi = Math.atan2(s, c) * 180 / Math.PI; if (phi < 0) phi += 360;
  return { p, c, s, a, phi, resid: Math.sqrt(ss / m) };
}

// One spectrum point (firmware specMeasure): X1 = sum V e^{-j theta}, H = X1(SENSE)/X1(DRV_OUT)
function specPoint(mkey, n, noise) {
  const sim = simulate(mkey, n), m = specPhases(n);
  let xs = [0, 0], xd = [0, 0];
  const vs = [], vd = [], ks = [];
  for (let i = 0; i < m; i++) {
    const k = (i * n / m) | 0;
    const a = lockin(sim, k, 's') + (noise ? noise() * NOISE_MV : 0);
    const b = lockin(sim, k, 'd') + (noise ? noise() * NOISE_MV : 0);
    const th = 2 * Math.PI * i / m, e = [Math.cos(th), -Math.sin(th)];
    xs = cx.add(xs, cx.scale(a, e)); xd = cx.add(xd, cx.scale(b, e));
    vs.push(a); vd.push(b); ks.push(k);
  }
  return { n, m, h: cx.div(xs, xd), xs, xd, vs, vd, ks };
}

// Firmware specFit (Levenberg-Marquardt on ln-parameters), plus an iteration history for animation.
function specFit(S, p0, freeP, cp) {
  const NP = 3, p = p0.slice(), idx = [0, 1, 2].filter(i => freeP[i]), nf = idx.length, nr = 2 * S.length;
  const resid = q => { const r = new Float64Array(nr); S.forEach((s, i) => { const d = cx.sub(specModel(s.n, s.m, q, cp), s.h); r[2 * i] = d[0]; r[2 * i + 1] = d[1]; }); return r; };
  const sumsq = r => r.reduce((a, x) => a + x * x, 0);
  const jac = (q, r) => idx.map(pi => { const qq = q.slice(); qq[pi] *= Math.exp(1e-4); const rt = resid(qq); return Array.from(rt, (x, i) => (x - r[i]) / 1e-4); });
  let r = resid(p), ss = sumsq(r), lambda = 1e-3;
  const hist = [{ p: p.slice(), ss }];
  for (let it = 0; it < 60; it++) {
    const J = jac(p, r);  // J[a][i]
    const A = [], g = [];
    for (let a = 0; a < nf; a++) { A.push([]); g.push(0); for (let b = 0; b < nf; b++) { let s = 0; for (let i = 0; i < nr; i++) s += J[a][i] * J[b][i]; A[a].push(s); } for (let i = 0; i < nr; i++) g[a] -= J[a][i] * r[i]; }
    let improved = false;
    for (let tries = 0; tries < 8 && !improved; tries++) {
      const M = A.map((row, a) => row.map((x, b) => x * (a === b ? 1 + lambda : 1)).concat([g[a]]));
      for (let c = 0; c < nf; c++) {
        let pv = c; for (let r2 = c + 1; r2 < nf; r2++) if (Math.abs(M[r2][c]) > Math.abs(M[pv][c])) pv = r2;
        [M[c], M[pv]] = [M[pv], M[c]];
        if (Math.abs(M[c][c]) < 1e-300) break;
        for (let r2 = c + 1; r2 < nf; r2++) { const f = M[r2][c] / M[c][c]; for (let k = c; k <= nf; k++) M[r2][k] -= f * M[c][k]; }
      }
      const dx = new Array(nf).fill(0);
      for (let c = nf - 1; c >= 0; c--) { let v = M[c][nf]; for (let k = c + 1; k < nf; k++) v -= M[c][k] * dx[k]; dx[c] = Math.abs(M[c][c]) > 1e-300 ? v / M[c][c] : 0; }
      const q = p.slice();
      for (let a = 0; a < nf; a++) q[idx[a]] *= Math.exp(Math.max(-3, Math.min(3, dx[a])));
      const rt = resid(q), ss2 = sumsq(rt);
      if (ss2 < ss) {
        for (let i = 0; i < NP; i++) p[i] = q[i];
        r = rt; const done = (ss - ss2) < 1e-12 * ss; ss = ss2; lambda *= 0.3; improved = true;
        hist.push({ p: p.slice(), ss });
        if (done) it = 1000;
      } else lambda *= 10;
    }
    if (!improved) break;
  }
  const dof = nr - nf, s2 = dof > 0 ? ss / dof : 0;
  const J = jac(p, r);
  const A = [], Inv = [];
  for (let a = 0; a < nf; a++) { A.push([]); Inv.push([]); for (let b = 0; b < nf; b++) { let s = 0; for (let i = 0; i < nr; i++) s += J[a][i] * J[b][i]; A[a].push(s); Inv[a].push(a === b ? 1 : 0); } }
  for (let c = 0; c < nf; c++) {
    let pv = c; for (let r2 = c + 1; r2 < nf; r2++) if (Math.abs(A[r2][c]) > Math.abs(A[pv][c])) pv = r2;
    [A[c], A[pv]] = [A[pv], A[c]]; [Inv[c], Inv[pv]] = [Inv[pv], Inv[c]];
    const d = A[c][c]; if (Math.abs(d) < 1e-300) continue;
    for (let k = 0; k < nf; k++) { A[c][k] /= d; Inv[c][k] /= d; }
    for (let r2 = 0; r2 < nf; r2++) { if (r2 === c) continue; const f = A[r2][c]; for (let k = 0; k < nf; k++) { A[r2][k] -= f * A[c][k]; Inv[r2][k] -= f * Inv[c][k]; } }
  }
  const relSigma = [0, 0, 0];
  for (let a = 0; a < nf; a++) relSigma[idx[a]] = Math.sqrt(Math.abs(Inv[a][a] * s2));
  return { p, ss, rms: Math.sqrt(ss / nr), relSigma, hist };
}

// Full `spectrum ab` (or `spectrum open`) on simulated data: sweep, average, multi-start fit.
function runSpectrum(mkey, seed) {
  const noise = rng(seed);
  const m = MEDIA[mkey];
  const orient = m.open ? ['open'] : ['A', 'B'];
  // Deterministic part: the firmware's own lock-in model at the medium's parameters. Noise part: ADC
  // noise on every reading, pushed through the same phase sums (noisy minus clean simulated point).
  const truth = m.open ? [1e-15, 1e-12, CP] : [m.Cm, m.Gs, m.Cd], tcp = m.open ? 0 : CP;
  const sweeps = orient.map(() => SPEC_N.map(n => {
    const noisy = specPoint(mkey, n, noise), clean = specPoint(mkey, n, null);
    const model = specModel(n, specPhases(n), truth, tcp);
    return { ...noisy, h: cx.add(model, cx.sub(noisy.h, clean.h)) };
  }));
  const avg = SPEC_N.map((n, i) => ({ n, m: specPhases(n), h: cx.scale(1 / sweeps.length, sweeps.reduce((acc, sw) => cx.add(acc, sw[i].h), [0, 0])) }));
  let best = null;
  if (m.open) {
    best = specFit(avg, [1e-15, 1e-12, 12e-12], [false, false, true], 0);
    best.seed = 12e-12;
  } else {
    for (const g0 of [1e-5, 1e-4, 1e-3, 1e-2]) {
      const f = specFit(avg, [500e-12, g0, 5e-12], [true, true, true], CP);
      f.seed = g0;
      if (!best || f.rms < best.rms) best = f;
    }
  }
  return { orient, sweeps, avg, fit: best };
}

if (typeof module !== 'undefined') module.exports = { FCLK, VD, RS, CP, SPEC_N, MEDIA, cx, specPhases, nodeH, mediumH, specModel, simulate, lockin, fitPhases, specPoint, specFit, runSpectrum, rng };
