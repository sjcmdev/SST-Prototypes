#include "gpu/gpu_backend.cu"
#include PRE_PORT_KERNEL
// CUDA 12.6 cannot parse the JSON 3.12 boolean-pack fold; use its C++14 path.
#define JSON_HAS_CPP_14
#include <nlohmann/json.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Json = nlohmann::json;

void check(cudaError_t error) {
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}
template<class T> struct Device {
    T* p = nullptr;
    explicit Device(size_t count) { check(cudaMalloc(&p, count*sizeof(T))); }
    ~Device() { cudaFree(p); }
    void upload(const std::vector<T>& values) {
        check(cudaMemcpy(p, values.data(), values.size()*sizeof(T), cudaMemcpyHostToDevice));
    }
};
GpuMonteCarloRequest load(const Json& input, const std::string& mode, bool useLm, int n) {
    const auto& r = input.at("request"); const auto& s = input.at("settings");
    GpuMonteCarloRequest q;
    q.model_type = GpuModelType(r.at("model").get<int>());
    q.layout.n_total = r.at("n_total"); q.layout.n_free = r.at("n_free");
    q.layout.T = r.at("T_K"); q.layout.dof = r.at("dof");
    q.layout.objective = mode == "log_legacy" ? JfmObjective::LogLegacy :
        mode == "isc_shift" ? JfmObjective::IscShift : JfmObjective::SignedSigma;
    q.noise_pct = s.at("mc_noise_percent"); q.layout.sigma_rel = q.noise_pct/100.;
    q.voltages = r.at("voltages_V").get<std::vector<double>>();
    q.true_current = r.at("ideal_current_A").get<std::vector<double>>();
    q.n_samples = n; q.use_lm = useLm;
    q.noise_seed = s.at("mc_seed"); q.start_perturbation_pct = s.at("mc_start_perturbation_percent");
    q.nm_config.max_iter = r.at("max_iter"); q.nm_config.reduced_chi2_tol = r.at("reduced_chi2_tol");
    for (int i=0;i<q.layout.n_total;++i) q.layout.fixed_values[i] = r.at("fixed_values").at(i);
    for (int i=0;i<q.layout.n_free;++i) {
        q.layout.free_to_total[i] = r.at("free_to_total").at(i);
        q.layout.min_bounds[i] = r.at("min_bounds").at(i); q.layout.max_bounds[i] = r.at("max_bounds").at(i);
    }
    if (q.layout.objective != JfmObjective::LogLegacy) {
        std::vector<double> magnitude; for (double y : q.true_current) magnitude.push_back(std::abs(y));
        const double maximum = *std::max_element(magnitude.begin(), magnitude.end());
        auto middle = magnitude.begin()+magnitude.size()/2;
        std::nth_element(magnitude.begin(), middle, magnitude.end());
        q.layout.sigma_abs = std::max(*middle, maximum*1e-12)*1e-4;
        // These captures are positive dark/forward curves; the dark Isc shift is zero.
    }
    return q;
}
__global__ void compareCurrents(GpuModelType model, GpuParamLayout layout, const double* v, int count,
    const GpuMcResult* a, const GpuMcResult* b, int n, unsigned int* mismatches) {
    const int s = blockIdx.x*blockDim.x+threadIdx.x;
    if (s >= n) return;
    double pa[8], pb[8]; lm::full(layout,a[s].best_free_params,pa); lm::full(layout,b[s].best_free_params,pb);
    for (int i=0;i<count;++i) {
        const double x=lm::current(model,v[i],pa,layout.T,nullptr,true);
        const double y=lm::current(model,v[i],pb,layout.T,nullptr,true);
        if (__double_as_longlong(x)!=__double_as_longlong(y)) atomicAdd(mismatches,1u);
    }
}
struct Run { std::vector<GpuMcResult> results; double milliseconds; };
Run run(const GpuMonteCarloRequest& q, int variant) {
    const int points = int(q.voltages.size()), n = q.n_samples;
    Device<double> v(points), ideal(points), sigma(points), measured(size_t(n)*points);
    Device<GpuMcResult> results(n);
    std::vector<double> sig(points), noisy(size_t(n)*points);
    for (int i=0;i<points;++i) sig[i]=std::abs(q.true_current[i]*q.layout.sigma_rel)+q.layout.sigma_abs;
    for (int s=0;s<n;++s) for (int i=0;i<points;++i) {
        const double amplitude=q.layout.objective==JfmObjective::LogLegacy ? q.true_current[i]*(q.noise_pct/100.) : sig[i];
        noisy[size_t(s)*points+i]=q.true_current[i]+lm::normal(q.noise_seed+uint64_t(s)*0xD1B54A32D192ED03ULL,i)*amplitude;
    }
    v.upload(q.voltages); ideal.upload(q.true_current); sigma.upload(sig); measured.upload(noisy);
    check(cudaMemset(results.p,0,n*sizeof(GpuMcResult)));
    auto layout=q.layout; layout.point_sigma=sigma.p;
    const double begin=hostTimeMs();
    if (variant==0) prePortFitKernel<<<(n+127)/128,128>>>(q.model_type,layout,q.nm_config,v.p,measured.p,ideal.p,
        points,n,q.noise_seed,q.start_perturbation_pct,q.noise_pct/100.,q.use_lm,results.p);
    else if (variant==1) sharedFitKernel<<<(n+127)/128,128>>>(q.model_type,layout,q.nm_config,v.p,measured.p,ideal.p,
        points,n,q.noise_seed,q.start_perturbation_pct,q.noise_pct/100.,q.use_lm,results.p);
    else g0FitKernel<<<n,32>>>(q.model_type,layout,q.nm_config,v.p,measured.p,ideal.p,
        points,n,q.noise_seed,q.start_perturbation_pct,q.noise_pct/100.,q.use_lm,results.p);
    check(cudaGetLastError()); check(cudaDeviceSynchronize());
    Run out; out.milliseconds=hostTimeMs()-begin; out.results.resize(n);
    check(cudaMemcpy(out.results.data(),results.p,n*sizeof(GpuMcResult),cudaMemcpyDeviceToHost));
    return out;
}
unsigned currentMismatches(const GpuMonteCarloRequest& q, const std::vector<GpuMcResult>& a, const std::vector<GpuMcResult>& b) {
    Device<GpuMcResult> da(a.size()), db(b.size()); Device<double> v(q.voltages.size()); Device<unsigned> mismatches(1);
    da.upload(a); db.upload(b); v.upload(q.voltages); check(cudaMemset(mismatches.p,0,sizeof(unsigned)));
    compareCurrents<<<(q.n_samples+127)/128,128>>>(q.model_type,q.layout,v.p,int(q.voltages.size()),da.p,db.p,q.n_samples,mismatches.p);
    check(cudaGetLastError()); check(cudaDeviceSynchronize()); unsigned count;
    check(cudaMemcpy(&count,mismatches.p,sizeof(count),cudaMemcpyDeviceToHost)); return count;
}
int main(int argc,char** argv) try {
    if (argc!=7) throw std::runtime_error("usage: organization case.resolved.json mode lm|nm N repeats output.json");
    const std::string mode=argv[2], method=argv[3];
    if (mode!="log_legacy" && mode!="isc_shift" && mode!="signed_sigma") throw std::runtime_error("invalid objective");
    if (method!="lm" && method!="nm") throw std::runtime_error("invalid method");
    const int n=std::stoi(argv[4]), repeats=std::stoi(argv[5]);
    if (n<1 || repeats<1) throw std::runtime_error("invalid count");
    Json input; std::ifstream(argv[1])>>input; auto q=load(input,mode,method=="lm",n);
    auto warm=q; warm.n_samples=1; run(warm,1);
    Run reference=run(q,0); Json times=Json::array(); unsigned mismatch=0, currents=0;
    int fallback=0, flat=0; for (const auto& r:reference.results) { fallback+=r.lm_fallback; flat+=r.flat_directions; }
    for (int variant=0;variant<3;++variant) {
        std::vector<double> ms;
        for (int i=0;i<repeats;++i) {
            auto actual=run(q,variant); ms.push_back(actual.milliseconds);
            // Both device buffers start at zero: padding and unwritten legacy fields are deterministic.
            for (int s=0;s<n;++s) mismatch+=std::memcmp(&reference.results[s],&actual.results[s],sizeof(GpuMcResult))!=0;
            currents+=currentMismatches(q,reference.results,actual.results);
        }
        std::sort(ms.begin(),ms.end()); times.push_back(ms[ms.size()/2]);
    }
    // Exercise the actual public GPU entry point, including host noise generation and transfer.
    const auto product=gpuRunMonteCarlo(q);
    if (!product.success || product.results.size()!=reference.results.size()) throw std::runtime_error(product.message);
    for (int s=0;s<n;++s) {
        const auto& a=reference.results[s]; const auto& b=product.results[s];
        mismatch+=std::memcmp(a.best_free_params,b.best_free_params,q.layout.n_free*sizeof(double))!=0;
        mismatch+=std::memcmp(&a.chi2_min,&b.chi2_min,sizeof(double))!=0;
        mismatch+=std::memcmp(&a.objective_chi2,&b.objective_chi2,sizeof(double))!=0;
        mismatch+=std::memcmp(&a.reduced_chi2_min,&b.reduced_chi2_min,sizeof(double))!=0;
        mismatch+=a.linear_points!=b.linear_points || a.log_points!=b.log_points || a.n_free!=b.n_free;
        mismatch+=a.iterations!=b.iterations || a.converged!=b.converged || a.solver_converged!=b.solver_converged ||
            a.stop_status!=b.stop_status || a.lm_status!=b.lm_status || a.lm_fallback!=b.lm_fallback || a.flat_directions!=b.flat_directions;
    }
    currents+=currentMismatches(q,reference.results,product.results);
    cudaDeviceProp prop{}; check(cudaGetDeviceProperties(&prop,0));
    Json out={{"case",input.at("id")},{"mode",mode},{"method",method},{"samples",n},{"repeats",repeats},
        {"pre_port_ms",times[0]},{"variant_a_ms",times[1]},{"g0_ms",times[2]},
        {"g0_over_a",times[2].get<double>()/times[1].get<double>()},{"result_mismatches",mismatch},
        {"current_mismatches",currents},{"lm_fallback",fallback},{"flat",flat},{"gpu",prop.name}};
    std::ofstream chiFile(std::string(argv[6])+".chi2.bin",std::ios::binary);
    for (const auto& r:product.results) chiFile.write(reinterpret_cast<const char*>(&r.objective_chi2),sizeof(double));
    std::ofstream(argv[6])<<out.dump(2)<<'\n'; std::cout<<out.dump()<<std::endl;
    return mismatch || currents ? 1 : 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<std::endl; return 1; }
