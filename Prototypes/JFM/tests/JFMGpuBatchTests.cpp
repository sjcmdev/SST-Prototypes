#include <gtest/gtest.h>
#include "Analysis/JFMFitEngine.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "gpu/gpu_backend.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace SST::Prototypes::JFM {
namespace {
struct ChunkOverride {
    std::string previous;
    static void Set(const char* value) {
#ifdef _WIN32
        _putenv_s("JFM_GPU_MC_CHUNK_SIZE", value);
#else
        if (*value) setenv("JFM_GPU_MC_CHUNK_SIZE", value, 1);
        else unsetenv("JFM_GPU_MC_CHUNK_SIZE");
#endif
    }
    explicit ChunkOverride(const char* value) {
        if (const auto* old = std::getenv("JFM_GPU_MC_CHUNK_SIZE")) previous = old;
        Set(value);
    }
    ~ChunkOverride() { Set(previous.c_str()); }
};
JFMCharacteristicRecord GpuRecord() {
    JFMCharacteristicRecord r;
    r.info.temperatureK = 300.; r.info.prefit = JFMPrefitState{};
    auto& p = *r.info.prefit; p.modelId = "SD-RsRsh"; p.status = "completed";
    const std::unordered_map<std::string, double> values{{"I0",1e-10},{"A",1.5},{"Rs",2.},{"Rsh",1e6}};
    for (auto [name, value] : values) p.parameters[name] = {value, std::nullopt, "test", {}};
    auto d = CreateRef<JFMCharacteristicData>();
    for (int i = 0; i < 48; ++i) {
        const double v = .02 + .7*i/47.;
        d->voltage.push_back(v); d->current.push_back(EvaluateJFMCurrent(p.modelId, v, 300., values));
    }
    p.fitRange = {0,48,.02,.72,"test"}; r.data = d; return r;
}
JFMFitState FitFor(const JFMCharacteristicRecord& r) {
    JFMFitState f; f.modelId = r.info.prefit->modelId; f.parameters = r.info.prefit->parameters; return f;
}
void ExpectSame(const JFMMonteCarloComputation& a, const JFMMonteCarloComputation& b) {
    ASSERT_EQ(a.result.samples.size(), b.result.samples.size());
    EXPECT_EQ(a.state.acceptedCount, b.state.acceptedCount);
    EXPECT_EQ(a.state.convergedCount, b.state.convergedCount);
    for (std::size_t i = 0; i < a.result.samples.size(); ++i) {
        const auto& x = a.result.samples[i]; const auto& y = b.result.samples[i];
        EXPECT_EQ(x.parameters, y.parameters); EXPECT_EQ(x.chiSquared, y.chiSquared);
        EXPECT_EQ(x.reducedChiSquared, y.reducedChiSquared); EXPECT_EQ(x.converged, y.converged);
        EXPECT_EQ(x.objectiveChiSquared, y.objectiveChiSquared); EXPECT_EQ(x.iterations, y.iterations);
        EXPECT_EQ(x.lmStatus, y.lmStatus); EXPECT_EQ(x.stopReason, y.stopReason);
        EXPECT_EQ(x.accepted, y.accepted); EXPECT_EQ(x.solverConverged, y.solverConverged);
        EXPECT_EQ(x.fitMethod, y.fitMethod); EXPECT_EQ(x.flatDirections, y.flatDirections);
        EXPECT_EQ(x.linearPoints, y.linearPoints); EXPECT_EQ(x.logPoints, y.logPoints);
    }
}
}

TEST(JFMGpuBatch, ExplicitDeviceSelection) {
    const auto device=gpuQueryDevice();
    if (std::getenv("JFM_EXPECT_NO_GPU")) {
        EXPECT_FALSE(device.available); EXPECT_FALSE(device.message.empty()); return;
    }
    if (!device.available) GTEST_SKIP() << device.message;
    const auto* selected=std::getenv("JFM_CUDA_DEVICE");
    EXPECT_EQ(device.device_id,selected ? std::atoi(selected) : 0);
}

namespace {
GpuMonteCarloRequest GpuRequest() {
    GpuMonteCarloRequest q; q.model_type=GpuModelType::SD0; q.n_samples=17; q.noise_pct=1.;
    q.layout.n_total=q.layout.n_free=2; q.layout.free_to_total[1]=1;
    q.layout.fixed_values[0]=1e-10; q.layout.fixed_values[1]=1.5;
    q.layout.min_bounds[0]=1e-15; q.layout.max_bounds[0]=1e-5;
    q.layout.min_bounds[1]=.5; q.layout.max_bounds[1]=3.;
    q.nm_config.max_iter=40;
    for(int i=0;i<24;++i) {
        const auto v=.02+.7*i/23.;q.voltages.push_back(v);
        q.true_current.push_back(EvaluateJFMCurrent("SD-0",v,300.,{{"I0",1e-10},{"A",1.5}}));
    }
    return q;
}
void ExpectSameGpuResult(const GpuMcResult& a, const GpuMcResult& b) {
    for (int j=0;j<GpuMcResult::MAX_FREE;++j) EXPECT_EQ(a.best_free_params[j],b.best_free_params[j]);
    EXPECT_EQ(a.chi2_min,b.chi2_min); EXPECT_EQ(a.reduced_chi2_min,b.reduced_chi2_min);
    EXPECT_EQ(a.ref_reduced_chi2,b.ref_reduced_chi2); EXPECT_EQ(a.delta_reduced_chi2,b.delta_reduced_chi2);
    EXPECT_EQ(a.iterations,b.iterations); EXPECT_EQ(a.converged,b.converged);
    EXPECT_EQ(a.solver_converged,b.solver_converged); EXPECT_EQ(a.stop_status,b.stop_status);
    EXPECT_EQ(a.lm_status,b.lm_status); EXPECT_EQ(a.lm_fallback,b.lm_fallback);
    EXPECT_EQ(a.flat_directions,b.flat_directions); EXPECT_EQ(a.objective_chi2,b.objective_chi2);
    EXPECT_EQ(a.linear_points,b.linear_points); EXPECT_EQ(a.log_points,b.log_points); EXPECT_EQ(a.n_free,b.n_free);
}
}

TEST(JFMGpuBatch, BackendInitializesEveryResultFieldAndPadding) {
    if (!gpuQueryDevice().available) GTEST_SKIP() << gpuQueryDevice().message;
    for (const bool useLm : {false,true}) for (const int budget : {0,40}) for (const bool reject : {false,true}) {
        SCOPED_TRACE(::testing::Message() << "LM=" << useLm << " budget=" << budget << " reject=" << reject);
        auto q=GpuRequest(); q.use_lm=useLm; q.nm_config.max_iter=budget; q.start_perturbation_pct=3.;
        if (reject) q.true_current[0]=0.;
        const auto whole=gpuRunMonteCarlo(q); ASSERT_TRUE(whole.success); ASSERT_EQ(whole.results.size(),17u);
        for (const auto& result : whole.results) {
            EXPECT_EQ(result.n_free,q.layout.n_free); EXPECT_EQ(result.converged,!reject);
            for (int j=result.n_free;j<GpuMcResult::MAX_FREE;++j) EXPECT_EQ(result.best_free_params[j],0.);
            EXPECT_EQ(result.ref_reduced_chi2,0.); EXPECT_EQ(result.delta_reduced_chi2,0.);
            const auto* bytes=reinterpret_cast<const unsigned char*>(&result);
            for (auto i=offsetof(GpuMcResult,flat_directions)+sizeof(result.flat_directions);i<offsetof(GpuMcResult,objective_chi2);++i)
                EXPECT_EQ(bytes[i],0);
            for (auto i=offsetof(GpuMcResult,n_free)+sizeof(result.n_free);i<sizeof(result);++i) EXPECT_EQ(bytes[i],0);
        }
        for (const int chunk : {1,8,16}) for (int offset=0;offset<q.n_samples;offset+=chunk) {
            auto part=q; part.n_samples=std::min(chunk,q.n_samples-offset);
            part.noise_seed=q.noise_seed+static_cast<std::uint64_t>(offset)*0xD1B54A32D192ED03ULL;
            const auto output=gpuRunMonteCarlo(part); ASSERT_TRUE(output.success);
            ASSERT_EQ(output.results.size(),static_cast<std::size_t>(part.n_samples));
            for (int i=0;i<part.n_samples;++i) ExpectSameGpuResult(whole.results[offset+i],output.results[i]);
        }
    }
}

TEST(JFMGpuBatch, BackendReturnsOnlyRequestedNoisePrefix) {
    if (!gpuQueryDevice().available) GTEST_SKIP() << gpuQueryDevice().message;
    auto q=GpuRequest();
    auto none=gpuRunMonteCarlo(q);ASSERT_TRUE(none.success);EXPECT_TRUE(none.noisy_current.empty());
    q.noisy_samples=3;auto prefix=gpuRunMonteCarlo(q);ASSERT_TRUE(prefix.success);EXPECT_EQ(prefix.noisy_current.size(),72u);
    q.noisy_samples=q.n_samples;auto all=gpuRunMonteCarlo(q);ASSERT_TRUE(all.success);
    EXPECT_TRUE(std::equal(prefix.noisy_current.begin(),prefix.noisy_current.end(),all.noisy_current.begin()));
    for(std::size_t i=0;i<none.results.size();++i) {
        EXPECT_EQ(none.results[i].objective_chi2,all.results[i].objective_chi2);
        EXPECT_EQ(prefix.results[i].objective_chi2,all.results[i].objective_chi2);
    }
    q.noisy_samples=q.n_samples+1;EXPECT_FALSE(gpuRunMonteCarlo(q).success);
}

TEST(JFMGpuBatch, PlanUsesDeviceAndBoundsOverridesByMemory) {
    const auto device = gpuQueryDevice();
    if (!device.available) GTEST_SKIP() << device.message;
    GpuMonteCarloRequest request; request.n_samples = 100000; request.voltages.resize(96);
    ChunkOverride automatic("");
    auto plan = gpuPlanMonteCarlo(request);
    ASSERT_GT(plan.chunk_size,128); EXPECT_LE(plan.chunk_size,65536);
    EXPECT_GT(device.multiprocessors,0); EXPECT_GT(device.max_threads_per_multiprocessor,0);
    EXPECT_GT(plan.free_bytes,0u); EXPECT_GE(plan.total_bytes,plan.free_bytes);
    EXPECT_GE(plan.bytes_per_sample,96*sizeof(double)+sizeof(GpuMcResult));
    EXPECT_LE(std::size_t(plan.chunk_size)*plan.bytes_per_sample,plan.free_bytes/2);
    { ChunkOverride small("64"); EXPECT_EQ(gpuPlanMonteCarlo(request).chunk_size,64); }
    { ChunkOverride huge("2147483647"); EXPECT_LE(gpuPlanMonteCarlo(request).chunk_size,65536); }
    for (const auto* invalid : {"0", "-1", "abc", "12x", "99999999999999"}) {
        ChunkOverride bad(invalid); EXPECT_EQ(gpuPlanMonteCarlo(request).chunk_size,0);
    }
    request.n_samples=17; EXPECT_EQ(gpuPlanMonteCarlo(request).chunk_size,17);
    request.voltages.resize(1000000);
    plan=gpuPlanMonteCarlo(request); EXPECT_LE(plan.chunk_size,8);
}

TEST(JFMGpuBatch, ChunkSizeAndRetentionPreserveEverySample) {
    if (!gpuQueryDevice().available) GTEST_SKIP() << gpuQueryDevice().message;
    auto r = GpuRecord(); auto f = FitFor(r);
    JFMFitMonteCarloSettings s; s.monteCarloSamples = 641; s.monteCarloMaxIterations = 80;
    s.monteCarloStartPerturbationPercent = 3.; s.saveSamples = false;
    for (const auto* method : {"lm", "nm"}) {
        s.fitMethod = method; s.simplexCalibrationRuns = 129;
        ChunkOverride old("64");
        const auto a = RunJFMMonteCarlo(r, *r.info.prefit, f, s); ASSERT_TRUE(a);
        ASSERT_EQ(a->state.backend, "cuda"); EXPECT_TRUE(a->result.noisyCurrent.empty());
        for (const auto* size : {"257", ""}) {
            ChunkOverride batch(size); s.saveSamples = true; s.savedSampleLimit = 3;
            const auto b = RunJFMMonteCarlo(r, *r.info.prefit, f, s); ASSERT_TRUE(b);
            ASSERT_EQ(b->state.backend, "cuda"); ExpectSame(*a, *b);
            EXPECT_EQ(b->result.noisyCurrent.size(), 3*r.data->voltage.size());
            for (const auto i : {0u, 2u, 63u, 64u, 256u, 640u})
                EXPECT_EQ(JFMMonteCarloNoisyCurve(a->result, i), JFMMonteCarloNoisyCurve(b->result, i));
        }
        s.saveSamples = false;
    }
}

TEST(JFMGpuBatch, InvalidOverrideKeepsCpuFallback) {
    if (!gpuQueryDevice().available) GTEST_SKIP() << gpuQueryDevice().message;
    auto r=GpuRecord();auto f=FitFor(r);ChunkOverride invalid("0");
    JFMFitMonteCarloSettings s;s.monteCarloSamples=3;s.monteCarloMaxIterations=40;
    const auto result=RunJFMMonteCarlo(r,*r.info.prefit,f,s);
    ASSERT_TRUE(result);EXPECT_EQ(result->state.backend,"cpu");EXPECT_FALSE(result->state.warnings.empty());
}

TEST(JFMGpuBatch, CpuGpuParityIncludesRemainderAndSignedObjective) {
    if (!gpuQueryDevice().available) GTEST_SKIP() << gpuQueryDevice().message;
    auto r = GpuRecord(); auto f = FitFor(r); ChunkOverride chunk("127");
    JFMFitMonteCarloSettings s; s.monteCarloSamples = 129; s.monteCarloMaxIterations = 80;
    s.saveSamples = false; s.monteCarloStartPerturbationPercent = 3.;
    for (const auto* method : {"lm", "nm"}) for (const auto* objective : {"auto", "signed_sigma"}) {
        s.fitMethod = method; s.objectiveMode = objective; s.autoCalibrateGpuSimplexIterations = false;
        s.preferGpu = false; auto cpu = RunJFMMonteCarlo(r,*r.info.prefit,f,s); ASSERT_TRUE(cpu);
        s.preferGpu = true; auto gpu = RunJFMMonteCarlo(r,*r.info.prefit,f,s); ASSERT_TRUE(gpu);
        ASSERT_EQ(gpu->state.backend, "cuda");
        ASSERT_EQ(cpu->result.samples.size(), gpu->result.samples.size());
        for (std::size_t i = 0; i < cpu->result.samples.size(); ++i) {
            const auto& a = cpu->result.samples[i]; const auto& b = gpu->result.samples[i];
            EXPECT_EQ(a.accepted,b.accepted); EXPECT_EQ(a.iterations,b.iterations);
            for (std::size_t j = 0; j < a.parameters.size(); ++j)
                EXPECT_NEAR(a.parameters[j],b.parameters[j],std::max(1e-30,std::abs(a.parameters[j])*1e-7));
            EXPECT_NEAR(a.objectiveChiSquared,b.objectiveChiSquared,std::max(1e-12,std::abs(a.objectiveChiSquared)*1e-7));
        }
    }
}

TEST(JFMGpuBatch, CancelAtChunkBoundaryNeverPublishesPartialResult) {
    if (!gpuQueryDevice().available) GTEST_SKIP() << gpuQueryDevice().message;
    auto r = GpuRecord(); auto f = FitFor(r); ChunkOverride chunk("64");
    JFMFitMonteCarloSettings s; s.monteCarloSamples = 65; s.monteCarloMaxIterations = 40;
    for (const auto [method, calibration] : {std::pair{"lm",false},std::pair{"nm",false},std::pair{"nm",true}}) {
        s.fitMethod = method; s.autoCalibrateGpuSimplexIterations=calibration;
        const auto expected=RunJFMMonteCarlo(r,*r.info.prefit,f,s); ASSERT_TRUE(expected);
        ASSERT_EQ(expected->state.backend,"cuda");
        bool cancel = false; int progress = 0;
        JobContext ctx([&](float done,float total){ EXPECT_GT(done,0); EXPECT_LT(done,total); ++progress; cancel=true; },
            {}, {}, {}, [&]{return cancel;});
        EXPECT_THROW((void)RunJFMMonteCarlo(r,*r.info.prefit,f,s,&ctx),JobCancelledException);
        EXPECT_EQ(progress,1);
        const auto repeated=RunJFMMonteCarlo(r,*r.info.prefit,f,s); ASSERT_TRUE(repeated);
        ASSERT_EQ(repeated->state.backend,"cuda"); ExpectSame(*expected,*repeated);
    }
}
} // namespace SST::Prototypes::JFM
