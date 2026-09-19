"""2D finite-volume solve of div(eps* grad V) = 0 for the MSV4 blade cross-section.

Geometry from the 2026-09-18 23:03 IPC-2581 export (x = IPC x - 86.0 mm):
  MS_A plate  x in [-7.087, -2.3625] on both faces   (driven, V = 1)
  MS_B plate  x in [ 2.3625,  7.087] on both faces   (V = 0, U3 grounds the cold electrode)
  guard       |x| in [8.123, 8.745] on both faces    (GND)
  blade edge  |x| = 9.0 ; FR4 core thickness t ; solder mask tm over both faces
Conductors are zero-thickness at z = +/- t/2. Soil fills everything outside the board.
Admittance per unit length Y' = j*w * sum_edges c_e * dV_e^2   (Green's identity, V_A = 1).
"""
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

EPS0 = 8.854e-12
L_PLATE = 0.0811  # m, plate length from the export

def grid_axis(fine_edges, lo, hi, h_fine, growth=1.15, h_max=2.0):
    """Nodes: every fine edge exactly, h_fine spacing inside [lo_f, hi_f], geometric outside."""
    fe = sorted(set(round(v, 6) for v in fine_edges))
    lo_f, hi_f = fe[0], fe[-1]
    pts = set(fe)
    for a, b in zip(fe[:-1], fe[1:]):
        n = max(1, int(np.ceil((b - a) / h_fine)))
        pts.update(np.linspace(a, b, n + 1).round(6))
    h, v = h_fine, hi_f
    while v < hi:
        h = min(h * growth, h_max); v += h; pts.add(round(v, 6))
    h, v = h_fine, lo_f
    while v > lo:
        h = min(h * growth, h_max); v -= h; pts.add(round(v, 6))
    return np.array(sorted(pts)) * 1e-3  # metres

def solve(t=1.6, tm=0.02, coat=0.0, soil=(20.0, 0.0), f=1e6, soil_region=None,
          air_gap=0.0, conductors=None, eps_fr4=4.4, eps_mask=3.8, eps_coat=3.0, half_width=9.0,
          dom=30.0, faces='both'):
    """Returns complex Y' (S/m) between the V=1 set and the V=0 set.
    soil_region(xc, zc) -> bool in mm: where soil exists (else air). air_gap in mm between the
    outer surface (mask/coating) and soil on both faces."""
    w = 2 * np.pi * f
    zt = t / 2
    zs = zt + tm + coat              # outer surface of the board stack
    zg = zs + air_gap
    if conductors is None:
        conductors = [(-7.087, -2.3625, 1.0), (2.3625, 7.087, 0.0),
                      (-8.745, -8.123, 0.0), (8.123, 8.745, 0.0)]
    xe = [-half_width, half_width] + [c[0] for c in conductors] + [c[1] for c in conductors]
    ze = [-zg, -zs, -zt - tm, -zt, 0.0, zt, zt + tm, zs, zg]
    x = grid_axis(xe, -dom, dom, h_fine=0.05)
    z = grid_axis(ze + [-zg - 1.0, zg + 1.0], -dom, dom, h_fine=min(0.05, max(tm, 0.005) / 2))
    nx, nz = len(x), len(z)
    xc, zc = (x[:-1] + x[1:]) / 2 * 1e3, (z[:-1] + z[1:]) / 2 * 1e3   # cell centres, mm
    X, Z = np.meshgrid(xc, zc, indexing='ij')
    er_s, sig_s = soil
    eps = np.full(X.shape, EPS0 * er_s - 1j * sig_s / w, dtype=complex)
    if soil_region is not None:
        eps[~soil_region(X, Z)] = EPS0
    inside_w = np.abs(X) < half_width
    eps[(np.abs(Z) > zs) & (np.abs(Z) < zg) & inside_w] = EPS0                       # air gap
    eps[(np.abs(Z) > zt + tm) & (np.abs(Z) < zs) & inside_w] = EPS0 * eps_coat       # coating
    eps[(np.abs(Z) > zt) & (np.abs(Z) < zt + tm) & inside_w] = EPS0 * eps_mask       # mask
    eps[(np.abs(Z) < zt) & inside_w] = EPS0 * eps_fr4                                # core
    dx, dz = np.diff(x), np.diff(z)
    idx = np.arange(nx * nz).reshape(nx, nz)
    rows, cols, vals = [], [], []
    edges = []
    # x-directed edges between (i,j) and (i+1,j)
    for i in range(nx - 1):
        for j in range(nz):
            s = 0j
            if j > 0: s += eps[i, j - 1] * dz[j - 1] / 2
            if j < nz - 1: s += eps[i, j] * dz[j] / 2
            edges.append((idx[i, j], idx[i + 1, j], s / dx[i]))
    for i in range(nx):
        for j in range(nz - 1):
            s = 0j
            if i > 0: s += eps[i - 1, j] * dx[i - 1] / 2
            if i < nx - 1: s += eps[i, j] * dx[i] / 2
            edges.append((idx[i, j], idx[i, j + 1], s / dz[j]))
    E = np.array(edges, dtype=object)
    a = E[:, 0].astype(int); b = E[:, 1].astype(int); c = E[:, 2].astype(complex)
    N = nx * nz
    Lap = sp.coo_matrix((np.concatenate([c, c, -c, -c]),
                         (np.concatenate([a, b, a, b]), np.concatenate([a, b, b, a]))), shape=(N, N)).tocsr()
    fixed = np.zeros(N, bool); Vf = np.zeros(N, complex)
    xm = x * 1e3; zm = z * 1e3
    jz = [int(np.argmin(np.abs(zm - zt)))] + ([int(np.argmin(np.abs(zm + zt)))] if faces == 'both' else [])
    for (x0, x1, v) in conductors:
        ii = np.where((xm >= x0 - 1e-6) & (xm <= x1 + 1e-6))[0]
        for jj in jz:
            fixed[idx[ii, jj]] = True; Vf[idx[ii, jj]] = v
    free = ~fixed
    A = Lap[free][:, free]; rhs = -Lap[free][:, fixed] @ Vf[fixed]
    V = Vf.copy(); V[free] = spla.spsolve(A.tocsc(), rhs)
    dV = V[a] - V[b]
    Yp = 1j * w * np.sum(c * dV * dV)   # S/m (per metre of plate length)
    return Yp, (x, z, V.reshape(nx, nz), eps)

def ceff(Yp, f):
    """Parallel C and G for the whole plate length."""
    Y = Yp * L_PLATE
    return Y.imag / (2 * np.pi * f), Y.real

if __name__ == '__main__':
    import sys
    f = 1e6
    for t in (0.8, 1.6):
        print(f"\n=== board t = {t} mm, mask 20 um, lossless soil ===")
        for er in (1, 4, 10, 20, 30):
            C, G = ceff(solve(t=t, soil=(er, 0.0))[0], f)
            print(f"  soil er {er:4.0f}: C = {C*1e12:7.2f} pF")
