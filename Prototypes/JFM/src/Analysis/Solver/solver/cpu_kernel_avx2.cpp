#include "cpu_kernel.hpp"
#include "lm_core.hpp"
#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>

namespace lm::cpu {
namespace {
struct Mask { __m256d v; };
struct V {
    __m256d v;
    V(double x) : v(_mm256_set1_pd(x)) {}
    V(__m256d x) : v(x) {}
};
V operator+(V a,V b){return _mm256_add_pd(a.v,b.v);}
V operator-(V a,V b){return _mm256_sub_pd(a.v,b.v);}
V operator-(V a){return _mm256_xor_pd(a.v,_mm256_set1_pd(-0.));}
V operator*(V a,V b){return _mm256_mul_pd(a.v,b.v);}
V operator/(V a,V b){return _mm256_div_pd(a.v,b.v);}
V& operator-=(V& a,V b){a=a-b;return a;}
Mask operator<(V a,V b){return {_mm256_cmp_pd(a.v,b.v,_CMP_LT_OQ)};}
Mask operator>(V a,V b){return {_mm256_cmp_pd(a.v,b.v,_CMP_GT_OQ)};}
Mask operator==(V a,V b){return {_mm256_cmp_pd(a.v,b.v,_CMP_EQ_OQ)};}
V load(const double* x){return _mm256_loadu_pd(x);}
void store(double* x,V a){_mm256_storeu_pd(x,a.v);}
bool any(Mask m){return _mm256_movemask_pd(m.v)!=0;}
V choose(Mask m,V a,V b){return _mm256_blendv_pd(b.v,a.v,m.v);}
V abs(V a){return _mm256_andnot_pd(_mm256_set1_pd(-0.),a.v);}

V exp(V x) {
    double a[4],k[4],out[4];store(a,x);
    for(int i=0;i<4;++i) {
        if (!std::isfinite(a[i]) || a[i]>709.782712893384 || a[i]<-745.1332191019411) {
            for(int lane=0;lane<4;++lane)out[lane]=shared_exp(a[lane]);
            return load(out);
        }
        k[i]=floor(a[i]*1.4426950408889634+.5);
    }
    V r=(x-load(k)*.6931471803691238)-load(k)*1.9082149292705877e-10;
    store(out,shared_exp_series(r));
    for(int i=0;i<4;++i)out[i]=ldexp(out[i],int(k[i]));
    return load(out);
}
V log(V x) {
    double a[4],m[4],k[4];store(a,x);
    for(int i=0;i<4;++i) {
        if (!(a[i]>0) || !std::isfinite(a[i])) {
            for(int lane=0;lane<4;++lane)m[lane]=shared_log(a[lane]);
            return load(m);
        }
        int e;m[i]=frexp(a[i],&e);
        if(m[i]<.7071067811865476){m[i]*=2;--e;}k[i]=e;
    }
    V core=shared_atanh2((load(m)-1)/(load(m)+1));
    return load(k)*.6931471803691238+(core+load(k)*1.9082149292705877e-10);
}
V log1p(V x) {
    Mask small=abs(x)<.5;
    if (_mm256_movemask_pd(small.v)==15) return shared_atanh2(x/(2+x));
    double a[4];store(a,x);
    for(auto& value:a)value=shared_log1p(value);
    return load(a);
}
V wlog(V L) {
    double values[4];store(values,L);
    const bool small=values[0]<=1;
    for(double value:values) if(!std::isfinite(value)||value<-40||(value<=1)!=small) {
        for(auto& value:values)value=lm::wlog(value,true);
        return load(values);
    }
    V x=small?exp(L):V(0);
    V w=small?choose(L<0,log1p(x),.5*L+.5):choose(L>3,L-log(L),1);
    Mask active{_mm256_castsi256_pd(_mm256_set1_epi64x(-1))};
    for(int k=0;k<60;++k) {
        V next=0;
        if(small) {V ew=exp(w),f=w*ew-x;next=w-f/(ew*(w+1)-(w+2)*f/(2*w+2));}
        else next=w-(w+log(w)-L)/(1+1/w);
        Mask done{_mm256_cmp_pd(abs(next-w).v,(4e-16*abs(next)).v,_CMP_LE_OQ)};
        w=choose(active,next,w);
        active.v=_mm256_andnot_pd(done.v,active.v);
        if(!any(active))break;
    }
    return w;
}
struct Math {
    using Value=V;
    bool shared=true;
    static bool any(Mask mask){return cpu::any(mask);}
    static V choose(Mask mask,V a,V b){return cpu::choose(mask,a,b);}
    static V abs(V x){return cpu::abs(x);}
    static V exp(V x){return cpu::exp(x);}
    static V log(V x){return cpu::log(x);}
    static V log1p(V x){return cpu::log1p(x);}
    static V wlog(V x){return cpu::wlog(x);}
    static V expm1(V x){return any(abs(x)<.5)?choose(abs(x)<.5,shared_exp_series(x,true),exp(x)-1):exp(x)-1;}
    static V pow(V x,double alpha) {
        double a[4];store(a,x);
        // Preserve the scalar signed, zero and non-integral power domains.
        if(any(x<V(0))||any(x==V(0))) {
            for(auto& value:a)value=mpow(value,alpha,true);
            return load(a);
        }
        return exp(alpha*log(x));
    }
    static V fma(double a,double b,V x) {
        double values[4];store(values,x);
        for(auto& value:values)value=std::fma(a,b,value);
        return load(values);
    }
    static V sum(const V* terms) {
        double values[4][4],out[4];
        for(int k=0;k<4;++k)store(values[k],terms[k]);
        for(int lane=0;lane<4;++lane) {
            double scalar[4];for(int k=0;k<4;++k)scalar[k]=values[k][lane];
            out[lane]=sum4(scalar,4);
        }
        return load(out);
    }
};
}

__declspec(noinline) void avx2Block(GpuModelType model,const double* v,const double* p,double T,bool jac,CurrentBlock& out) {
    V derivatives[7]={0,0,0,0,0,0,0};
    store(out.value,currentKernel(Math{},model,load(v),p,T,jac?derivatives:nullptr));
    bool valid=true;
    for(double value:out.value)valid=valid&&std::isfinite(value);
    if(valid) {
        if(jac)for(int k=0;k<7;++k)store(out.jacobian[k],derivatives[k]);
    } else for(int lane=0;lane<4;++lane) {
        double dj[7]={};out.value[lane]=current(model,v[lane],p,T,jac?dj:nullptr,true);
        if(jac)for(int k=0;k<7;++k)out.jacobian[k][lane]=dj[k];
    }
}
}
#else
namespace lm::cpu {
void avx2Block(GpuModelType model,const double* v,const double* p,double T,bool jac,CurrentBlock& out) {
    for(int lane=0;lane<4;++lane) {
        double dj[7]={};out.value[lane]=current(model,v[lane],p,T,jac?dj:nullptr,true);
        if(jac)for(int k=0;k<7;++k)out.jacobian[k][lane]=dj[k];
    }
}
}
#endif
