#pragma once

#include "gpu_types.hpp"

#include <string>
#include <cstddef>
#include <cstdint>
#include <vector>

struct GpuDeviceStatus
{
    bool available = false;
    std::string name;
    int compute_major = 0;
    int compute_minor = 0;
    std::string message;
    int device_id = 0;
    int multiprocessors = 0;
    int max_threads_per_multiprocessor = 0;
};

struct GpuLambertWValidationResult
{
    bool cuda_available = false;
    bool passed = false;
    int cases_checked = 0;
    double max_abs_error = 0.0;
    double max_residual = 0.0;
    std::string message;
};

struct GpuNumericValidationResult
{
    bool cuda_available = false;
    bool passed = false;
    int cases_checked = 0;
    double max_abs_error = 0.0;
    double max_rel_error = 0.0;
    std::string message;
};

struct GpuMonteCarloRequest
{
    GpuModelType model_type = GpuModelType::Diode4P;
    GpuParamLayout layout;
    bool use_lm = true;
    bool generate_noise = true;
    GpuNmConfig nm_config;
    std::vector<double> voltages;
    std::vector<double> true_current;
    std::vector<double> sigma;
    int n_samples = 0;
    int noisy_samples = 0; // Only this prefix is retained on the host; zero returns fit results only.
    double noise_pct = 0.0;
    std::uint64_t noise_seed = 42;
    double start_perturbation_pct = 0.0;
};

struct GpuMonteCarloBatchPlan
{
    int chunk_size = 0; // Zero means planning failed; use the existing CPU fallback.
    int active_blocks_per_multiprocessor = 0;
    std::size_t bytes_per_sample = 0;
    std::size_t free_bytes = 0;
    std::size_t total_bytes = 0;
    std::string message;
};

struct GpuMonteCarloOutput
{
    bool success = false;
    std::string message;
    std::vector<GpuMcResult> results;
    std::vector<double> noisy_current;
    double upload_ms = 0.0;
    double noise_ms = 0.0;
    double simplex_ms = 0.0;
    double download_ms = 0.0;
    double total_ms = 0.0;
};

struct GpuSimplexParityCase
{
    int iterations = 0;
    bool passed = false;
    int cases_checked = 0;
    double max_abs_error = 0.0;
    double max_rel_error = 0.0;
    std::string message;
    std::string cpu_trace;
    std::string gpu_trace;
};

struct GpuSimplexParityReport
{
    bool cuda_available = false;
    bool passed = false;
    std::string message;
    std::vector<GpuSimplexParityCase> cases;
};

GpuDeviceStatus gpuQueryDevice();
GpuMonteCarloBatchPlan gpuPlanMonteCarlo(const GpuMonteCarloRequest& request);
GpuLambertWValidationResult gpuValidateLambertW();
GpuNumericValidationResult gpuValidateDiodeCurrent();
GpuNumericValidationResult gpuValidateNoise();
GpuNumericValidationResult gpuValidateSimplexOneStep();
GpuNumericValidationResult gpuValidateSimplexFull();
GpuSimplexParityReport gpuValidateSimplexParityTrace();
GpuMonteCarloOutput gpuRunMonteCarlo(const GpuMonteCarloRequest& request);
