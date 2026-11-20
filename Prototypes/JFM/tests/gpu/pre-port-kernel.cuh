// Snapshot captured before jfmgpa; G0 changes only the sample index.
__global__ void prePortFitKernel(GpuModelType model, GpuParamLayout layout, GpuNmConfig config,
    const double* voltages, const double* measured, const double* reference_current,
    int point_count, int sample_count, uint64_t seed, double perturb, double noise, bool use_lm, GpuMcResult* result)
{
    const int run = blockIdx.x*blockDim.x+threadIdx.x;
    if (run >= sample_count) return;
    const auto fit = lm::fitSample(model, layout, config, voltages, measured+size_t(run)*point_count,
        point_count, seed, run, perturb, use_lm);
    auto &out = result[run];
    for (int j = 0; j < layout.n_free; ++j) out.best_free_params[j] = fit.result.p[j];
    double full[8]; lm::full(layout, fit.result.p, full);
    double chi = 0;
    for (int i = 0; i < point_count; ++i) {
        const double current = lm::current(model, voltages[i], full, layout.T, nullptr, true);
        if (layout.objective == JfmObjective::LogLegacy && (!(current > 0) || !isfinite(current))) { chi = lm::inf; break; }
        const double sigma = lm::currentSigma(layout, reference_current[i], i);
        const double r = layout.objective == JfmObjective::LogLegacy ?
            (lm::shared_log(current)-lm::shared_log(reference_current[i]))/(noise == 0 ? 1 : noise) :
            sigma > 0 ? lm::statisticalResidual(current, reference_current[i], sigma, layout) : 0;
        chi += r*r;
    }
    out.chi2_min = noise == 0 && layout.sigma_abs == 0 && isfinite(chi) ? 0 : chi;
    out.reduced_chi2_min = layout.objective == JfmObjective::LogLegacy ? out.chi2_min : out.chi2_min / (layout.n_free > 0 ? layout.n_free : 1);
    out.linear_points = out.log_points = 0;
    out.objective_chi2 = fit.result.chi;
    for (int i=0;i<point_count;++i) {
        const double y=measured[size_t(run)*point_count+i];
        if (layout.objective == JfmObjective::SignedSigma || (layout.objective == JfmObjective::IscShift && y+layout.shift <= layout.switch_k*lm::currentSigma(layout,y,i))) ++out.linear_points;
        else ++out.log_points;
    }
    out.converged = isfinite(out.chi2_min) && isfinite(fit.result.chi);
    out.solver_converged = out.converged && fit.result.status == 1 && !fit.flat;
    out.stop_status = fit.result.status; out.lm_status = fit.lm_status;
    out.lm_fallback = fit.fallback; out.flat_directions = fit.flat;
    out.iterations = fit.result.iterations; out.n_free = layout.n_free;
}


__global__ void g0FitKernel(GpuModelType model, GpuParamLayout layout, GpuNmConfig config,
    const double* voltages, const double* measured, const double* reference_current,
    int point_count, int sample_count, uint64_t seed, double perturb, double noise, bool use_lm, GpuMcResult* result)
{
    if (threadIdx.x != 0) return;
    const int run = blockIdx.x;
    if (run >= sample_count) return;
    const auto fit = lm::fitSample(model, layout, config, voltages, measured+size_t(run)*point_count,
        point_count, seed, run, perturb, use_lm);
    auto &out = result[run];
    for (int j = 0; j < layout.n_free; ++j) out.best_free_params[j] = fit.result.p[j];
    double full[8]; lm::full(layout, fit.result.p, full);
    double chi = 0;
    for (int i = 0; i < point_count; ++i) {
        const double current = lm::current(model, voltages[i], full, layout.T, nullptr, true);
        if (layout.objective == JfmObjective::LogLegacy && (!(current > 0) || !isfinite(current))) { chi = lm::inf; break; }
        const double sigma = lm::currentSigma(layout, reference_current[i], i);
        const double r = layout.objective == JfmObjective::LogLegacy ?
            (lm::shared_log(current)-lm::shared_log(reference_current[i]))/(noise == 0 ? 1 : noise) :
            sigma > 0 ? lm::statisticalResidual(current, reference_current[i], sigma, layout) : 0;
        chi += r*r;
    }
    out.chi2_min = noise == 0 && layout.sigma_abs == 0 && isfinite(chi) ? 0 : chi;
    out.reduced_chi2_min = layout.objective == JfmObjective::LogLegacy ? out.chi2_min : out.chi2_min / (layout.n_free > 0 ? layout.n_free : 1);
    out.linear_points = out.log_points = 0;
    out.objective_chi2 = fit.result.chi;
    for (int i=0;i<point_count;++i) {
        const double y=measured[size_t(run)*point_count+i];
        if (layout.objective == JfmObjective::SignedSigma || (layout.objective == JfmObjective::IscShift && y+layout.shift <= layout.switch_k*lm::currentSigma(layout,y,i))) ++out.linear_points;
        else ++out.log_points;
    }
    out.converged = isfinite(out.chi2_min) && isfinite(fit.result.chi);
    out.solver_converged = out.converged && fit.result.status == 1 && !fit.flat;
    out.stop_status = fit.result.status; out.lm_status = fit.lm_status;
    out.lm_fallback = fit.fallback; out.flat_directions = fit.flat;
    out.iterations = fit.result.iterations; out.n_free = layout.n_free;
}

