from __future__ import annotations

import math
import sys
from dataclasses import dataclass
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from jfm_constants import K_BOLTZMANN_EV as KB


@dataclass
class Row:
    name: str
    T: float
    x: float
    y: float
    sigma_y: float
    A: float
    A_std: float
    I0: float
    I0_std: float


def finite(value: float) -> bool:
    return math.isfinite(value)


def safe_sigma(value: float) -> tuple[float, bool]:
    if finite(value) and value > 0.0:
        return value, True
    return 1.0, False


def read_input(path: str) -> tuple[dict[str, str], list[Row]]:
    meta: dict[str, str] = {}
    rows: list[Row] = []
    with open(path, "r", encoding="utf-8") as handle:
        header_seen = False
        for raw in handle:
            parts = raw.rstrip("\n").split("\t")
            if not parts or not parts[0]:
                continue
            if not header_seen and len(parts) == 2:
                meta[parts[0]] = parts[1]
                continue
            if parts[0] == "name":
                header_seen = True
                continue
            if len(parts) < 9:
                continue
            try:
                rows.append(Row(parts[0], *(float(value) for value in parts[1:9])))
            except ValueError:
                continue
    return meta, rows


def inv2(a: float, b: float, c: float) -> tuple[float, float, float] | None:
    det = a * c - b * b
    if abs(det) < 1e-300:
        return None
    return c / det, -b / det, a / det


def weighted_linear(x: list[float], y: list[float], sigma: list[float], absolute: bool) -> dict[str, float] | None:
    s = sx = sy = sxx = sxy = 0.0
    for xi, yi, si in zip(x, y, sigma):
        w = 1.0 / (si * si)
        s += w
        sx += w * xi
        sy += w * yi
        sxx += w * xi * xi
        sxy += w * xi * yi
    inv = inv2(s, sx, sxx)
    if inv is None:
        return None
    cbb, cbm, cmm = inv
    b = cbb * sy + cbm * sxy
    m = cbm * sy + cmm * sxy
    chi2 = sum(((yi - (b + m * xi)) / si) ** 2 for xi, yi, si in zip(x, y, sigma))
    if not absolute and len(x) > 2:
        scale = chi2 / (len(x) - 2)
        cbb *= scale
        cbm *= scale
        cmm *= scale
    return {"b": b, "m": m, "cbb": cbb, "cbm": cbm, "cmm": cmm, "chi2": chi2}


def z_coth_z(z: float) -> float:
    az = abs(z)
    if az < 1e-4:
        z2 = z * z
        return 1.0 + z2 / 3.0 - z2 * z2 / 45.0
    if az > 50.0:
        return abs(z)
    return z * math.cosh(z) / math.sinh(z)


def d_z_coth_d_e00(E00: float, T: float) -> float:
    z = E00 / (KB * T)
    az = abs(z)
    if az < 1e-4:
        bracket = 2.0 * z / 3.0 - 4.0 * z**3 / 45.0
    elif az > 50.0:
        bracket = 1.0
    else:
        sinh = math.sinh(z)
        bracket = math.cosh(z) / sinh - z / (sinh * sinh)
    return bracket / (KB * T)


def solve_normal(jtj: list[list[float]], jtr: list[float]) -> list[float] | None:
    if len(jtr) == 1:
        if abs(jtj[0][0]) < 1e-300:
            return None
        return [-jtr[0] / jtj[0][0]]
    a, b, c = jtj[0][0], jtj[0][1], jtj[1][1]
    inv = inv2(a, b, c)
    if inv is None:
        return None
    ia, ib, ic = inv
    return [-(ia * jtr[0] + ib * jtr[1]), -(ib * jtr[0] + ic * jtr[1])]


def lm(theta: list[float], residual_jacobian, absolute: bool, max_iter: int = 80):
    damping = 1e-3
    best = None
    for _ in range(max_iter):
        residuals, jacobian = residual_jacobian(theta)
        chi2 = sum(r * r for r in residuals)
        if best is None or chi2 < best[0]:
            best = (chi2, theta[:], residuals, jacobian)
        p = len(theta)
        jtj = [[0.0 for _ in range(p)] for _ in range(p)]
        jtr = [0.0 for _ in range(p)]
        for r, row in zip(residuals, jacobian):
            for i in range(p):
                jtr[i] += row[i] * r
                for j in range(i, p):
                    jtj[i][j] += row[i] * row[j]
        for i in range(p):
            jtj[i][i] *= 1.0 + damping
        if p == 2:
            jtj[1][0] = jtj[0][1]
        delta = solve_normal(jtj, jtr)
        if delta is None:
            break
        candidate = [value + step for value, step in zip(theta, delta)]
        trial_residuals, _ = residual_jacobian(candidate)
        trial_chi2 = sum(r * r for r in trial_residuals)
        if trial_chi2 < chi2:
            theta = candidate
            damping = max(damping * 0.25, 1e-12)
            if max(abs(step) for step in delta) < 1e-10:
                break
        else:
            damping = min(damping * 10.0, 1e12)
    if best is None:
        return None
    theta = best[1]
    residuals, jacobian = residual_jacobian(theta)
    chi2 = sum(r * r for r in residuals)
    p = len(theta)
    jtj = [[0.0 for _ in range(p)] for _ in range(p)]
    for row in jacobian:
        for i in range(p):
            for j in range(i, p):
                jtj[i][j] += row[i] * row[j]
    if p == 1:
        cov = [[1.0 / jtj[0][0]]] if abs(jtj[0][0]) > 1e-300 else [[0.0]]
    else:
        inv = inv2(jtj[0][0], jtj[0][1], jtj[1][1])
        cov = [[inv[0], inv[1]], [inv[1], inv[2]]] if inv else [[0.0, 0.0], [0.0, 0.0]]
    if not absolute and len(residuals) > p:
        scale = chi2 / (len(residuals) - p)
        cov = [[value * scale for value in row] for row in cov]
    return theta, cov, chi2


def fit_ea_j00(rows: list[Row], quantity: str) -> dict[str, str] | None:
    usable = [row for row in rows if row.T > 0 and row.A > 0 and row.I0 > 0]
    if len(usable) < 2:
        return None
    x = [1.0 / (row.A * KB * row.T) for row in usable]
    y = [math.log(row.I0) for row in usable]
    sx = [row.A_std / (row.A * row.A * KB * row.T) if row.A_std > 0 else 0.0 for row in usable]
    sy_raw = [row.I0_std / row.I0 if row.I0_std > 0 else 0.0 for row in usable]
    absolute = all(value > 0 for value in sy_raw)
    sigma = [value if value > 0 else 1.0 for value in sy_raw]
    result = weighted_linear(x, y, sigma, absolute)
    if result is None:
        return None
    for _ in range(4):
        sigma = [math.sqrt(si * si + (result["m"] * sxi) ** 2) for si, sxi in zip(sigma, sx)]
        result = weighted_linear(x, y, sigma, absolute)
        if result is None:
            return None
    slope, intercept = result["m"], result["b"]
    ea = -slope
    j00 = math.exp(intercept)
    fit_x = [row.x for row in usable]
    if quantity == "ALnI0":
        fit_y = [row.A * (intercept + slope / (row.A * KB * row.T)) for row in usable]
    else:
        fit_y = [intercept + slope / (row.A * KB * row.T) for row in usable]
    return {
        "mode": "E_a/J_00",
        "slope": slope,
        "intercept": intercept,
        "slope_std": math.sqrt(max(result["cmm"], 0.0)),
        "intercept_std": math.sqrt(max(result["cbb"], 0.0)),
        "E_a": ea,
        "E_a_std": math.sqrt(max(result["cmm"], 0.0)),
        "J_00": j00,
        "J_00_std": j00 * math.sqrt(max(result["cbb"], 0.0)),
        "fit_x": fit_x,
        "fit_y": fit_y,
    }


def fit_te_ir(rows: list[Row], quantity: str) -> dict[str, str] | None:
    usable = [row for row in rows if row.T > 0 and row.A > 0]
    if len(usable) < 2:
        return None
    sigma = []
    absolute = True
    for row in usable:
        s, ok = safe_sigma(row.A_std if row.A_std > 0 else row.sigma_y)
        sigma.append(s)
        absolute = absolute and ok
    estimates = [KB * row.T * math.sqrt(max(3.0 * (row.A - 1.0), 1e-10)) for row in usable]
    start = max(1e-6, sorted(estimates)[len(estimates) // 2])

    def residual_jacobian(theta: list[float]):
        e00 = math.exp(theta[0])
        residuals = []
        jacobian = []
        for row, sig in zip(usable, sigma):
            model = z_coth_z(e00 / (KB * row.T))
            derivative = d_z_coth_d_e00(e00, row.T) * e00
            residuals.append((row.A - model) / sig)
            jacobian.append([-derivative / sig])
        return residuals, jacobian

    solved = lm([math.log(start)], residual_jacobian, absolute)
    if solved is None:
        return None
    theta, cov_theta, chi2 = solved
    e00 = math.exp(theta[0])
    e00_std = abs(e00) * math.sqrt(max(cov_theta[0][0], 0.0))
    fit_x = [row.x for row in usable]
    model_a = [z_coth_z(e00 / (KB * row.T)) for row in usable]
    fit_y = [1.0 / value for value in model_a] if quantity == "InvA" else model_a
    lin = weighted_linear(fit_x, fit_y, [1.0] * len(fit_x), False) or {"m": 0.0, "b": 0.0, "cmm": 0.0, "cbb": 0.0}
    return {
        "mode": "TE-IR",
        "slope": lin["m"],
        "intercept": lin["b"],
        "E_00": e00,
        "E_00_std": e00_std,
        "chi2": chi2,
        "fit_x": fit_x,
        "fit_y": fit_y,
    }


def fit_te_scr(rows: list[Row], quantity: str) -> dict[str, str] | None:
    usable = [row for row in rows if row.T > 0 and row.A > 0]
    if len(usable) < 3:
        return None
    g = [1.0 / row.A for row in usable]
    sigma = []
    absolute = True
    for row in usable:
        source_sigma = row.A_std / (row.A * row.A) if row.A_std > 0 else row.sigma_y
        s, ok = safe_sigma(source_sigma)
        sigma.append(s)
        absolute = absolute and ok
    x1 = [1.0 / (row.T * row.T) for row in usable]
    x2 = [row.T for row in usable]
    yy = [2.0 / row.A - 1.0 for row in usable]
    s11 = sum(a * a for a in x1)
    s12 = sum(a * b for a, b in zip(x1, x2))
    s22 = sum(b * b for b in x2)
    t1 = sum(a * y for a, y in zip(x1, yy))
    t2 = sum(b * y for b, y in zip(x2, yy))
    inv = inv2(s11, s12, s22)
    beta1 = inv[0] * t1 + inv[1] * t2 if inv else -1e-10
    beta2 = inv[1] * t1 + inv[2] * t2 if inv else 1e-3
    e00_start = KB * math.sqrt(max(-3.0 * beta1, 1e-8))
    estar_start = KB / beta2 if beta2 > 0 else 0.05
    e00_start = max(e00_start, 1e-8)
    estar_start = max(estar_start, 1e-8)

    def model_values(e00: float, estar: float, row: Row) -> tuple[float, float, float]:
        kbt = KB * row.T
        model = 0.5 * (1.0 - e00 * e00 / (3.0 * kbt * kbt) + kbt / estar)
        d_e00 = -e00 / (3.0 * kbt * kbt)
        d_estar = -kbt / (2.0 * estar * estar)
        return model, d_e00, d_estar

    def residual_jacobian(theta: list[float]):
        e00, estar = math.exp(theta[0]), math.exp(theta[1])
        residuals = []
        jacobian = []
        for row, observed, sig in zip(usable, g, sigma):
            model, d_e00, d_estar = model_values(e00, estar, row)
            residuals.append((observed - model) / sig)
            jacobian.append([-(d_e00 * e00) / sig, -(d_estar * estar) / sig])
        return residuals, jacobian

    solved = lm([math.log(e00_start), math.log(estar_start)], residual_jacobian, absolute)
    if solved is None:
        return None
    theta, cov_theta, chi2 = solved
    e00, estar = math.exp(theta[0]), math.exp(theta[1])
    e00_std = abs(e00) * math.sqrt(max(cov_theta[0][0], 0.0))
    estar_std = abs(estar) * math.sqrt(max(cov_theta[1][1], 0.0))
    tstar = estar / KB
    fit_x = [row.x for row in usable]
    model_g = [model_values(e00, estar, row)[0] for row in usable]
    fit_y = [1.0 / value for value in model_g] if quantity == "A" else model_g
    lin = weighted_linear(fit_x, fit_y, [1.0] * len(fit_x), False) or {"m": 0.0, "b": 0.0}
    return {
        "mode": "TE-SCR",
        "slope": lin["m"],
        "intercept": lin["b"],
        "E_00": e00,
        "E_00_std": e00_std,
        "E_star": estar,
        "E_star_std": estar_std,
        "T_star": tstar,
        "T_star_std": estar_std / KB,
        "chi2": chi2,
        "fit_x": fit_x,
        "fit_y": fit_y,
    }


def fit_linear(rows: list[Row]) -> dict[str, str] | None:
    usable = [row for row in rows if finite(row.x) and finite(row.y)]
    if len(usable) < 2:
        return None
    sigma = []
    absolute = True
    for row in usable:
        s, ok = safe_sigma(row.sigma_y)
        sigma.append(s)
        absolute = absolute and ok
    result = weighted_linear([row.x for row in usable], [row.y for row in usable], sigma, absolute)
    if result is None:
        return None
    return {
        "mode": "linear",
        "slope": result["m"],
        "intercept": result["b"],
        "slope_std": math.sqrt(max(result["cmm"], 0.0)),
        "intercept_std": math.sqrt(max(result["cbb"], 0.0)),
        "fit_x": [row.x for row in usable],
        "fit_y": [result["b"] + result["m"] * row.x for row in usable],
    }


def emit(output_path: str, result: dict[str, object] | None) -> int:
    with open(output_path, "w", encoding="utf-8") as handle:
        if result is None:
            handle.write("ok\t0\n")
            return 2
        handle.write("ok\t1\n")
        for key, value in result.items():
            if isinstance(value, list):
                handle.write(f"{key}\t{','.join(f'{item:.17g}' for item in value)}\n")
            else:
                handle.write(f"{key}\t{float(value):.17g}\n" if isinstance(value, float) else f"{key}\t{value}\n")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        return 2
    meta, rows = read_input(argv[1])
    mode = meta.get("mode", "linear")
    quantity = meta.get("quantity", "")
    if mode == "ea_j00":
        result = fit_ea_j00(rows, quantity)
    elif mode == "te_ir":
        result = fit_te_ir(rows, quantity)
    elif mode == "te_scr":
        result = fit_te_scr(rows, quantity)
    else:
        result = fit_linear(rows)
    return emit(argv[2], result)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
