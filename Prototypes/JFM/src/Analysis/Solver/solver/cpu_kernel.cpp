#include "cpu_kernel.hpp"
#include "lm_core.hpp"
#include <cstdlib>
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace lm::cpu {
void avx2Block(GpuModelType, const double*, const double*, double, bool, CurrentBlock&);

bool avx2Available() noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    int r[4];
    __cpuid(r, 0);
    if (r[0] < 7) return false;
    __cpuidex(r, 1, 0);
    if ((r[2] & ((1 << 27) | (1 << 28))) != ((1 << 27) | (1 << 28)) || (_xgetbv(0) & 6) != 6) return false;
    __cpuidex(r, 7, 0);
    return (r[1] & (1 << 5)) != 0;
#else
    return false;
#endif
}

Dispatch dispatch() noexcept {
    static const Dispatch path = [] {
        const char* scalar = std::getenv("JFM_CPU_SCALAR");
        return scalar && scalar[0] == '1' ? Dispatch::Scalar :
            avx2Available() ? Dispatch::Avx2 : Dispatch::Scalar;
    }();
    return path;
}

static bool useAvx2(Dispatch path) {
    return path == Dispatch::Auto ? dispatch() == Dispatch::Avx2 :
        path == Dispatch::Avx2 && avx2Available();
}

void currents(GpuModelType model, const double* v, std::size_t count,
    const double* p, double T, double* output, Dispatch path) {
    std::size_t i = 0;
    if (useAvx2(path)) for (; count-i >= 4; i += 4) {
        CurrentBlock block;
        avx2Block(model, v+i, p, T, false, block);
        for (int lane = 0; lane < 4; ++lane) output[i+lane] = block.value[lane];
    }
    for (; i < count; ++i) output[i] = current(model, v[i], p, T, nullptr, true);
}

double objective(GpuModelType model, const GpuParamLayout& l, const double* p,
    const double* v, const double* y, int count, double* h, double* g, bool precise, Dispatch path) {
    if (count < 4 || !useAvx2(path)) return objectiveScalar(model,l,p,v,y,count,h,g,precise,true);
    struct Evaluator {
        const double* voltage;
        int count;
        CurrentBlock block;
        double operator()(int i,double v,GpuModelType model,const double* p,double T,double* jac) {
            if(i-i%4>count-4)return current(model,v,p,T,jac,true);
            if(i%4==0)avx2Block(model,voltage+i,p,T,jac!=nullptr,block);
            if(jac)for(int k=0;k<7;++k)jac[k]=block.jacobian[k][i%4];
            return block.value[i%4];
        }
    };
    // Use the same residuals and point-ordered reductions as scalar and CUDA.
    return objectiveKernel(model,l,p,v,y,count,h,g,precise,true,Evaluator{v,count,{}});
}
}
