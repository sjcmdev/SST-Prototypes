#pragma once
#include "lm_core.hpp"
namespace gpu3b_nm {
JFM_HD inline void buildFullParams_d_b(const GpuParamLayout& layout, const double* free_values, double* full)
{
    for (int i = 0; i < layout.n_total; ++i)
        full[i] = layout.fixed_values[i];
    for (int j = 0; j < layout.n_free; ++j)
        full[layout.free_to_total[j]] = free_values[j];
}

JFM_HD inline double evaluateChi2Serial_d_b(
    GpuModelType model,
    const GpuParamLayout& layout,
    const double* free_values,
    const double* voltages,
    const double* measured,
    const double* sigma,
    int count)
{
    (void)sigma;
    return lm::objective(model, layout, free_values, voltages, measured, count, nullptr, nullptr, true, true);
}

JFM_HD inline void clipToBounds_d_b(const GpuParamLayout& layout, double* values)
{
    for (int j = 0; j < layout.n_free; ++j)
    {
        if (!isnan(values[j]))
            values[j] = fmin(fmax(values[j], layout.min_bounds[j]), layout.max_bounds[j]);
    }
}

JFM_HD inline double simplexDelta_d_b(double start, double range)
{
    return fabs(start) > 1.0e-30 ? fabs(start) * 0.05 : fmax(range * 1.0e-12, 1.0e-15);
}

JFM_HD inline void sortOrder_d_b(const double* chi2, int count, int* order)
{
    for (int i = 0; i < count; ++i)
        order[i] = i;
    for (int i = 1; i < count; ++i)
    {
        const int key = order[i];
        int j = i - 1;
        while (j >= 0 && chi2[order[j]] > chi2[key])
        {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }
}

JFM_HD inline void initSimplexFromStart_d_b(
    GpuModelType model,
    const GpuParamLayout& layout,
    const double* start,
    const double* voltages,
    const double* measured,
    const double* sigma,
    int point_count,
    double vertices[GpuSimplexStepResult::MAX_VERTICES][GpuSimplexStepResult::MAX_FREE],
    double* chi2)
{
    const int n_free = layout.n_free;
    const int n_vertices = n_free + 1;
    for (int j = 0; j < n_free; ++j)
        vertices[0][j] = start[j];

    for (int vertex = 1; vertex < n_vertices; ++vertex)
    {
        for (int j = 0; j < n_free; ++j)
            vertices[vertex][j] = start[j];
        const int dim = vertex - 1;
        const double range = layout.max_bounds[dim] - layout.min_bounds[dim];
        const double delta = simplexDelta_d_b(start[dim], range);
        double candidate = start[dim] + delta;
        if (candidate > layout.max_bounds[dim])
            candidate = start[dim] - delta;
        vertices[vertex][dim] = candidate;
        clipToBounds_d_b(layout, vertices[vertex]);
    }

    for (int vertex = 0; vertex < n_vertices; ++vertex)
        chi2[vertex] = evaluateChi2Serial_d_b(model, layout, vertices[vertex], voltages, measured, sigma, point_count);
}

}
namespace gpu3b_nm {
JFM_HD inline lm::Result fit(GpuModelType model,GpuParamLayout layout,GpuNmConfig config,
 const double* voltages,const double* measured_run,const double* reference_current,
 const double* sigma,int point_count,uint64_t start_seed,double start_perturbation_pct,
 double noise_fraction,int run,bool real_stop) {
 lm::Result result;lm::Result* out=&result;
    double vertices[GpuSimplexStepResult::MAX_VERTICES][GpuSimplexStepResult::MAX_FREE] = {};
    double chi2[GpuSimplexStepResult::MAX_VERTICES] = {};
    int order[GpuSimplexStepResult::MAX_VERTICES] = {};
    const int n_free = layout.n_free;
    const int n_vertices = n_free + 1;
    int iteration = 0;

    for (int j = 0; j < n_free; ++j)
    {
        const double start = layout.fixed_values[layout.free_to_total[j]];
        const double scale = fabs(start) > 1.0e-300
            ? fabs(start)
            : fmax((layout.max_bounds[j] - layout.min_bounds[j]) * 1.0e-12, 1.0e-15);
        const uint64_t bits = lm::mix(start_seed + static_cast<uint64_t>(run) * 0xD1B54A32D192ED03ULL + static_cast<uint64_t>(j));
        const double centered = lm::unit(bits) * 2.0 - 1.0;
        vertices[0][j] = fmin(fmax(start + centered * start_perturbation_pct * 0.01 * scale, layout.min_bounds[j]), layout.max_bounds[j]);
    }
    initSimplexFromStart_d_b(model, layout, vertices[0], voltages, measured_run, sigma, point_count, vertices, chi2);


    for (; iteration < config.max_iter; ++iteration)
    {
        sortOrder_d_b(chi2, n_vertices, order);
        const int best = order[0];

        if (real_stop && iteration % 25 == 0) {
            double h[64], g[8];
            double f=lm::objective(model,layout,vertices[best],voltages,measured_run,point_count,h,g,true,true);
            double gn=lm::gradient(layout,vertices[best],f,h,g), span=0, size=0;
            for(int k=1;k<n_vertices;++k) {
                span=fmax(span,fabs(chi2[order[k]]-f)/fmax(f,layout.objective==JfmObjective::LogLegacy?1e-300:double(layout.dof)));
                for(int j=0;j<n_free;++j) {
                    double scale=fmax(fabs(vertices[best][j]),fmax(fabs(layout.fixed_values[layout.free_to_total[j]]),1e-30));
                    size=fmax(size,fabs(vertices[order[k]][j]-vertices[best][j])/scale);
                }
            }
            out->gradient=gn;out->relative_change=span;out->step=size;
            if(gn<=lm::gtol && (span<=lm::ftol || size<=lm::xtol)) {out->status=1;break;}
            if(span<=lm::ftol && size<=lm::xtol) {out->status=4;break;}
        }
        const int second_worst = order[n_vertices - 2];
        const int worst = order[n_vertices - 1];

        double maxDistanceSquared = 0.0;
        for (int a = 0; a < n_vertices; ++a)
        {
            for (int b = a + 1; b < n_vertices; ++b)
            {
                double distanceSquared = 0.0;
                for (int j = 0; j < n_free; ++j)
                {
                    const double delta = vertices[a][j] - vertices[b][j];
                    distanceSquared += delta * delta;
                }
                maxDistanceSquared = fmax(maxDistanceSquared, distanceSquared);
            }
        }
        if (sqrt(maxDistanceSquared) < config.degenerate_tol)
        {
            double bestPoint[GpuSimplexStepResult::MAX_FREE] = {};
            for (int j = 0; j < n_free; ++j)
                bestPoint[j] = vertices[best][j];
            initSimplexFromStart_d_b(model, layout, bestPoint, voltages, measured_run, sigma, point_count, vertices, chi2);
            continue;
        }

        double centroid[GpuSimplexStepResult::MAX_FREE] = {};
        for (int vertex = 0; vertex < n_vertices; ++vertex)
        {
            if (vertex == worst)
                continue;
            for (int j = 0; j < n_free; ++j)
                centroid[j] += vertices[vertex][j];
        }
        for (int j = 0; j < n_free; ++j)
            centroid[j] /= static_cast<double>(n_free);

        double reflected[GpuSimplexStepResult::MAX_FREE] = {};
        for (int j = 0; j < n_free; ++j)
            reflected[j] = centroid[j] + config.alpha * (centroid[j] - vertices[worst][j]);
        clipToBounds_d_b(layout, reflected);
        const double f_reflected = evaluateChi2Serial_d_b(model, layout, reflected, voltages, measured_run, sigma, point_count);

        if (f_reflected < chi2[best])
        {
            double expanded[GpuSimplexStepResult::MAX_FREE] = {};
            for (int j = 0; j < n_free; ++j)
                expanded[j] = centroid[j] + config.gamma * (reflected[j] - centroid[j]);
            clipToBounds_d_b(layout, expanded);
            const double f_expanded = evaluateChi2Serial_d_b(model, layout, expanded, voltages, measured_run, sigma, point_count);
            if (f_expanded < f_reflected)
            {
                for (int j = 0; j < n_free; ++j)
                    vertices[worst][j] = expanded[j];
                chi2[worst] = f_expanded;
            }
            else
            {
                for (int j = 0; j < n_free; ++j)
                    vertices[worst][j] = reflected[j];
                chi2[worst] = f_reflected;
            }
        }
        else if (f_reflected < chi2[second_worst])
        {
            for (int j = 0; j < n_free; ++j)
                vertices[worst][j] = reflected[j];
            chi2[worst] = f_reflected;
        }
        else
        {
            double contracted[GpuSimplexStepResult::MAX_FREE] = {};
            for (int j = 0; j < n_free; ++j)
                contracted[j] = centroid[j] + config.rho * (vertices[worst][j] - centroid[j]);
            clipToBounds_d_b(layout, contracted);
            const double f_contracted = evaluateChi2Serial_d_b(model, layout, contracted, voltages, measured_run, sigma, point_count);
            if (f_contracted < chi2[worst])
            {
                for (int j = 0; j < n_free; ++j)
                    vertices[worst][j] = contracted[j];
                chi2[worst] = f_contracted;
            }
            else
            {
                for (int vertex = 0; vertex < n_vertices; ++vertex)
                {
                    if (vertex == best)
                        continue;
                    for (int j = 0; j < n_free; ++j)
                        vertices[vertex][j] = vertices[best][j] + config.sigma_shrink * (vertices[vertex][j] - vertices[best][j]);
                    clipToBounds_d_b(layout, vertices[vertex]);
                    chi2[vertex] = evaluateChi2Serial_d_b(model, layout, vertices[vertex], voltages, measured_run, sigma, point_count);
                }
            }
        }
    }

    sortOrder_d_b(chi2, n_vertices, order);
    const int best = order[0];
    for(int j=0;j<n_free;++j)out->p[j]=vertices[best][j];
    out->chi=chi2[best];out->iterations=iteration;
    double h[64],g[8];lm::objective(model,layout,out->p,voltages,measured_run,point_count,h,g,true,true);
    out->gradient=lm::gradient(layout,out->p,out->chi,h,g);
    out->relative_change=0;out->step=0;
    for(int k=1;k<n_vertices;++k) {
        out->relative_change=fmax(out->relative_change,fabs(chi2[order[k]]-out->chi)/fmax(out->chi,1e-300));
        for(int j=0;j<n_free;++j) {
            double scale=fmax(fabs(out->p[j]),fmax(fabs(layout.fixed_values[layout.free_to_total[j]]),1e-30));
            out->step=fmax(out->step,fabs(vertices[order[k]][j]-out->p[j])/scale);
        }
    }
    if(!isfinite(out->chi))out->status=2;
    return result;
}
}
