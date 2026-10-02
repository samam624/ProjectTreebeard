// Self-test for msv4_lockin_sim.js: iq vs board 1 measurements, sim vs firmware specModel, fit recovery.
const P = require('./msv4_lockin_sim.js');
const t0 = Date.now();
console.log('iq at N=16 (4 phases), electrode A, vs board 1 measured');
const meas = { open: [784, 178.3], air: [760, 182.3], towel: [642, 190.5], hand: [372, 221.5] };
for (const mk of Object.keys(P.MEDIA)) {
  const sim = P.simulate(mk, 16);
  const ks = [0, 4, 8, 12];
  const vs = ks.map(k => P.lockin(sim, k, 's')), vd = ks.map(k => P.lockin(sim, k, 'd'));
  const fs = P.fitPhases(vs, ks, 16), fd = P.fitPhases(vd, ks, 16);
  const m = meas[mk];
  console.log(`${mk.padEnd(6)} sense A=${fs.a.toFixed(1)} phi=${fs.phi.toFixed(1)} | drv A=${fd.a.toFixed(1)} phi=${fd.phi.toFixed(1)} P=${fs.p.toFixed(1)}` + (m ? `   measured A=${m[0]} phi=${m[1]}` : ''));
}
// sweep 16 drive amplitude in theory = 669
{
  const sim = P.simulate('open', 16); const ks = [...Array(16).keys()];
  const fd = P.fitPhases(ks.map(k => P.lockin(sim, k, 'd')), ks, 16);
  console.log(`sweep 16 drive: A=${fd.a.toFixed(1)} (theory 8/pi^2*825=${(8 / Math.PI ** 2 * 825).toFixed(1)}) resid=${fd.resid.toFixed(1)} (doc theory ~53)`);
}
// noiseless spectrum point vs firmware model at truth: checks sim + aliasing model agree
console.log('\nsim X1 ratio vs firmware specModel at true params (no noise)');
for (const mk of ['towel', 'hand', 'soil']) {
  const m = P.MEDIA[mk]; let worst = 0;
  for (const n of P.SPEC_N) {
    const pt = P.specPoint(mk, n, null);
    const mod = P.specModel(n, P.specPhases(n), [m.Cm, m.Gs, m.Cd], P.CP);
    const err = P.cx.abs(P.cx.sub(pt.h, mod));
    worst = Math.max(worst, err);
  }
  console.log(`${mk}: worst |H_sim - H_model| = ${worst.toExponential(2)}`);
}
console.log('\nfull spectrum + fit (noise 0.5 mV/reading)');
for (const mk of Object.keys(P.MEDIA)) {
  const r = P.runSpectrum(mk, 7);
  const f = r.fit, m = P.MEDIA[mk];
  if (m.open) console.log(`open: Cp=${(f.p[2] * 1e12).toFixed(2)} pF +/-${(f.relSigma[2] * 100).toFixed(1)}% iters=${f.hist.length}`);
  else console.log(`${mk.padEnd(6)} Rsoil=${(1 / f.p[1]).toExponential(3)} (true ${(1 / m.Gs).toExponential(3)}) +/-${(f.relSigma[1] * 100).toFixed(1)}%  Cm=${(f.p[0] * 1e12).toFixed(1)} (${(m.Cm * 1e12).toFixed(1)}) +/-${(f.relSigma[0] * 100).toFixed(1)}%  Cd=${(f.p[2] * 1e12).toFixed(2)} (${(m.Cd * 1e12).toFixed(1)}) rms=${f.rms.toExponential(2)} seed=${f.seed} iters=${f.hist.length}`);
  console.log('   points: ' + r.avg.map(a => `${(16 / a.n).toFixed(2)}M |H|${P.cx.abs(a.h).toFixed(3)} ${(P.cx.arg(a.h) * 180 / Math.PI).toFixed(1)}`).join('  '));
}
console.log(`\n${Date.now() - t0} ms`);
