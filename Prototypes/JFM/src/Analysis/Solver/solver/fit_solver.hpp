#pragma once
#include "lm_policy425.hpp"
#include "nm_real.hpp"

namespace lm {
struct FitOutcome {
    Result result;
    int lm_status = -1;
    bool fallback = false;
    bool flat = false;
};

// Local identifiability in relative physical coordinates: sigma_min/sigma_max <= 1e-8.
JFM_HD inline bool flatDirections(GpuModelType model, const GpuParamLayout& l,
    const double* p, const double* v, const double* y, int count) {
    if (l.n_free == 0) return false;
    double h[64], g[8], scale[8];
    if (!isfinite(objective(model, l, p, v, y, count, h, g, true, true))) return false;
    for (int j = 0; j < l.n_free; ++j) {
        const int k = l.free_to_total[j];
        const double nominal = fabs(l.fixed_values[k]);
        scale[j] = fmax(fabs(p[j]), fmax(nominal*1e-12, 1e-30));
        if (k == 6) scale[j] = fmax(nominal, 1e-12);
        if (k == 2 && fabs(p[j]) < nominal*1e-6) scale[j] = nominal;
    }
    for (int j = 0; j < l.n_free; ++j)
        for (int k = 0; k < l.n_free; ++k) h[j*8+k] *= scale[j]*scale[k];
    // Symmetric Jacobi diagonalization of Jrel'Jrel (at most seven columns).
    for (int sweep = 0; sweep < 40; ++sweep)
        for (int j = 0; j < l.n_free; ++j)
            for (int k = j+1; k < l.n_free; ++k) {
                const double b = h[j*8+k];
                if (b == 0) continue;
                const double tau = (h[k*8+k]-h[j*8+j])/(2*b);
                const double t = copysign(1.0, tau)/(fabs(tau)+sqrt(1+tau*tau));
                const double c = 1/sqrt(1+t*t), s = t*c;
                h[j*8+j] -= t*b; h[k*8+k] += t*b; h[j*8+k] = h[k*8+j] = 0;
                for (int q = 0; q < l.n_free; ++q) if (q != j && q != k) {
                    const double a = h[q*8+j], d = h[q*8+k];
                    h[q*8+j] = h[j*8+q] = c*a-s*d;
                    h[q*8+k] = h[k*8+q] = s*a+c*d;
                }
            }
    double maximum = 0;
    for (int j = 0; j < l.n_free; ++j) maximum = fmax(maximum, h[j*8+j]);
    for (int j = 0; j < l.n_free; ++j) if (h[j*8+j] <= maximum*1e-16) return true;
    return false;
}

JFM_HD inline FitOutcome fitSample(GpuModelType model, const GpuParamLayout& l,
    GpuNmConfig config, const double* v, const double* y, int count,
    uint64_t seed, int sample, double perturbPercent, bool useLm) {
    FitOutcome out;
    double initial[8]; start(l, seed, sample, perturbPercent, initial);
    if (l.n_free == 0) {
        out.result.chi = objective(model, l, initial, v, y, count, nullptr, nullptr, true, true);
        out.result.status = isfinite(out.result.chi) ? 1 : 2;
        return out;
    }
    if (useLm) {
        out.result = fit_variant(model, l, initial, v, y, count, config.max_iter < 300 ? config.max_iter : 300);
        out.lm_status = out.result.status;
    }
    if (!useLm || out.result.status != 1) {
        out.fallback = useLm;
        auto nm = gpu3b_nm::fit(model, l, config, v, y, nullptr, nullptr, count, seed, perturbPercent, 0, sample, true);
        const int work = useLm ? out.result.iterations : 0;
        if (!useLm || nm.chi <= out.result.chi) out.result = nm;
        if (useLm && isfinite(nm.chi)) {
            auto polish = fit_variant(model, l, nm.p, v, y, count, config.max_iter < 300 ? config.max_iter : 300);
            if (polish.chi <= out.result.chi) out.result = polish;
            out.result.iterations = work + nm.iterations + polish.iterations;
        } else out.result.iterations += work;
    }
    out.flat = flatDirections(model, l, out.result.p, v, y, count);
    return out;
}

inline const char* stopReason(int status, bool flat) {
    if (flat && status == 1) return "flat_direction";
    switch (status) {
    case 0: return "max_iter";
    case 1: return "converged";
    case 2: return "non_finite";
    case 3: return "damping_exhausted";
    case 4: return "stall";
    default: return "unknown";
    }
}
}
