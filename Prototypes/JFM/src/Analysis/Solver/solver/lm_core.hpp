#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include "gpu/gpu_types.hpp"
#include "cpu_kernel.hpp"
#if defined(TRACY_ENABLE) && !defined(__CUDACC__)
#include <tracy/Tracy.hpp>
#endif
#ifdef __CUDACC__
#define JFM_HD __host__ __device__
#else
#define JFM_HD
using std::isfinite;
using std::isnan;
#endif
#include "lm_shared_math.hpp"
namespace lm {
constexpr double K = 1.380649e-23 / 1.602176634e-19;
constexpr double inf = std::numeric_limits<double>::infinity();
constexpr double ftol=1e-12, gtol=1e-8, xtol=1e-9;
struct Result {
 double p[8]={}, chi=inf, gradient=inf, relative_change=inf, step=inf;
 int iterations=0, status=0; // 0=max_iter, 1=stationary, 2=invalid, 3=damping exhausted, 4=NM stagnation
};
JFM_HD inline uint64_t mix(uint64_t x) {
 x+=0x9E3779B97F4A7C15ULL; x=(x^(x>>30))*0xBF58476D1CE4E5B9ULL;
 x=(x^(x>>27))*0x94D049BB133111EBULL; return x^(x>>31);
}
JFM_HD inline double unit(uint64_t b) { return ((b>>11)+1.0)*(1.0/9007199254740993.0); }
JFM_HD inline double normal(uint64_t seed,int i) {
 uint64_t b=seed+uint64_t(i)*0x9E3779B97F4A7C15ULL;
 return sqrt(-2*log(unit(mix(b))))*cos(6.28318530717958647692*unit(mix(b+0xD1B54A32D192ED03ULL)));
}
JFM_HD inline void start(const GpuParamLayout& l,uint64_t seed,int sample,double pct,double* p) {
 for(int j=0;j<l.n_free;++j) {
  double s=l.fixed_values[l.free_to_total[j]];
  double scale=fabs(s)>1e-300?fabs(s):fmax((l.max_bounds[j]-l.min_bounds[j])*1e-12,1e-15);
  p[j]=fmin(fmax(s+(2*unit(mix(seed+uint64_t(sample)*0xD1B54A32D192ED03ULL+j))-1)*pct*.01*scale,l.min_bounds[j]),l.max_bounds[j]);
 }
}
JFM_HD inline double wlog(double L,bool shared=false) {
 if(L < -40) { double x=mexp(L,shared); return x-x*x; }
 if(L<=1) {
  double x=mexp(L,shared), w=L<0?mlog1p(x,shared):.5*L+.5;
  for(int k=0;k<60;++k) {
   double ew=mexp(w,shared),f=w*ew-x,n=w-f/(ew*(w+1)-(w+2)*f/(2*w+2));
   if(fabs(n-w)<=4e-16*fabs(n))return n; w=n;
  } return w;
 }
 double w=L>3?L-mlog(L,shared):1;
 for(int k=0;k<60;++k) { double n=w-(w+mlog(w,shared)-L)/(1+1/w); if(fabs(n-w)<=4e-16*fabs(n))return n; w=n; }
 return w;
}
// Expansion summation preserves the small current after cancellation with Iph.
JFM_HD inline double sum4(const double* x,int count) {
 double partial[4]; int n=0;
 for(int k=0;k<count;++k) {
  double v=x[k]; int m=0;
  for(int j=0;j<n;++j) {
   double y=partial[j]; if(fabs(v)<fabs(y)){double t=v;v=y;y=t;}
   double hi=v+y,lo=y-(hi-v); if(lo!=0)partial[m++]=lo; v=hi;
  } partial[m]=v; n=m+1;
 }
 double hi=0,lo=0;
 if(n) {
  hi=partial[--n];
  while(n) { double xh=hi,y=partial[--n]; hi=xh+y;lo=y-(hi-xh);if(lo!=0)break; }
  if(n&&((lo<0&&partial[n-1]<0)||(lo>0&&partial[n-1]>0))) {
   double y=lo*2,xh=hi+y;if(y==xh-hi)hi=xh;
  }
 } return hi;
}
struct ScalarMath {
 using Value=double;
 bool shared;
 JFM_HD bool any(bool value) const {return value;}
 JFM_HD double choose(bool condition,double a,double b) const {return condition?a:b;}
 JFM_HD double abs(double x) const {return fabs(x);}
 JFM_HD double exp(double x) const {return mexp(x,shared);}
 JFM_HD double expm1(double x) const {return mexpm1(x,shared);}
 JFM_HD double log(double x) const {return mlog(x,shared);}
 JFM_HD double pow(double x,double y) const {return mpow(x,y,shared);}
 JFM_HD double wlog(double x) const {return lm::wlog(x,shared);}
 JFM_HD double fma(double a,double b,double x) const {return ::fma(a,b,x);}
 JFM_HD double sum(const double* terms) const {return sum4(terms,4);}
};
// Drobiazg (31)-(32): one typed model/Jacobian kernel for scalar, AVX2 and CUDA.
template<class Math> JFM_HD inline typename Math::Value currentKernel(Math m,GpuModelType model,
 typename Math::Value v,const double* p,double T,typename Math::Value* jac) {
 using Value=typename Math::Value;
 bool series=model==GpuModelType::SDRs||model==GpuModelType::SDRsIph||model==GpuModelType::SDRsRsh||model==GpuModelType::SDRsRshIph||int(model)>=8;
 bool shunt=model==GpuModelType::SDRsh||model==GpuModelType::SDRshIph||model==GpuModelType::SDRsRsh||model==GpuModelType::SDRsRshIph||int(model)>=8;
 bool nonlinear=model==GpuModelType::SDRsRshAlphaRsh2||model==GpuModelType::SDRsRshAlphaRsh2Iph;
 double i0=p[0],n=p[1],rs=series?p[2]:0,rsh=shunt?p[3]:inf,alpha=nonlinear?p[4]:2,rsh2=nonlinear?p[5]:inf;
 if(!(i0>0&&n>0&&T>0&&rs>=0&&rsh>0))return NAN;
 bool light=(int(model)>=4&&int(model)<=7)||model==GpuModelType::SDRsRshAlphaRsh2Iph;
 double a=n*K*T;
 Value w=0,d=0,u=0;
 if(rs>0) { double c=mlog(i0,m.shared)+mlog(rs,m.shared)-mlog(a,m.shared);w=m.wlog(c+v/a); d=a/rs*w;u=m.choose(w>0,a*(m.log(m.choose(w>0,w,1))-c),v-rs*d); }
 else { d=i0*m.exp(v/a);u=v; }
 // Near u=0, log(w)-c cancels; solve u+b*expm1(u/a)=v-b instead.
 if(rs>0 && m.any(m.abs(u)<.1*a) && isfinite(rs*i0)) {
  auto nearZero=m.abs(u)<.1*a;
  double b=rs*i0;
  Value rhs=m.choose(nearZero,m.fma(-rs,i0,v),0),refined=rhs/(1+b/a);
  for(int k=0;k<4;++k)refined-=(refined+b*m.expm1(refined/a)-rhs)/(1+b/a*m.exp(refined/a));
  u=m.choose(nearZero,refined,u);
  d=m.choose(nearZero,i0*m.exp(m.choose(nearZero,u/a,0)),d);w=m.choose(nearZero,rs*m.choose(nearZero,d,0)/a,w);
 }
 if(nonlinear&&(!(alpha>0&&rsh2>0)||(m.any(u<0)&&alpha!=floor(alpha))))return NAN;
 Value s2=nonlinear?m.pow(u,alpha)/rsh2:Value(0);
 Value terms[4]={d,u/rsh,s2,light?-p[6]:0},val=m.sum(terms);
 if(jac) {
  Value slope=1/rsh+(nonlinear?alpha*m.pow(u,alpha-1)/rsh2:Value(0));
  Value dd[3]={d/i0/(1+w),-d*u/(a*n*(1+w)),-d*d/(a*(1+w))};
  Value du[3]={-rs*dd[0],-rs*dd[1],-d/(1+w)};
  for(int j=0;j<3;++j)jac[j]=dd[j]+slope*du[j];
  if(!series)jac[2]=0;
  jac[3]=shunt?-u/(rsh*rsh):Value(0);jac[4]=nonlinear?s2*m.log(m.choose(u==0,1,m.abs(u))):Value(0);
  jac[5]=nonlinear?-s2/rsh2:Value(0);jac[6]=light?-1:0;
 }
 return val;
}
JFM_HD inline double current(GpuModelType model,double v,const double* p,double T,double* jac=nullptr,bool shared=false) {
 return currentKernel(ScalarMath{shared},model,v,p,T,jac);
}
JFM_HD inline void full(const GpuParamLayout& l,const double* p,double* out) {
 for(int j=0;j<l.n_total;++j)out[j]=l.fixed_values[j];
 for(int j=0;j<l.n_free;++j)out[l.free_to_total[j]]=p[j];
}
// Kept verbatim for dark-fit regression.
JFM_HD inline double relativeLogResidual(double predicted, double measured, bool precise=true, bool shared=true, double* jacobian=nullptr) {
 if(!(predicted>0&&measured>0)||!isfinite(predicted)||!isfinite(measured)||measured==1)return inf;
 double ly=mlog(measured,shared);
 if(jacobian)for(int k=0;k<7;++k)jacobian[k]=-jacobian[k]/predicted/ly;
 return precise&&fabs(predicted-measured)<.5*measured?-mlog1p((predicted-measured)/measured,shared)/ly:1-mlog(predicted,shared)/ly;
}
JFM_HD inline double currentSigma(const GpuParamLayout& l, double measured, int i) {
 return l.point_sigma ? l.point_sigma[i] : l.sigma_rel*fabs(measured)+l.sigma_abs;
}
// Tangent continuation of log keeps the residual and analytic derivative continuous at k sigma.
JFM_HD inline double statisticalResidual(double predicted, double measured, double sigma,
 const GpuParamLayout& l, double* derivative=nullptr, bool shared=true) {
 if(!isfinite(predicted)||!isfinite(measured)||!(sigma>0)||!isfinite(sigma))return inf;
 double r, d;
 if(l.objective==JfmObjective::SignedSigma) { r=(predicted-measured)/sigma; d=1/sigma; }
 else {
  const double x=predicted+l.shift,y=measured+l.shift,t=l.switch_k*sigma;
  const double scale=fmax(y,t)/sigma;
  if(x<=t&&y<=t)r=(predicted-measured)/sigma;
  else if(x>t&&y>t)r=scale*mlog1p((predicted-measured)/y,shared);
  else {
   const double fx=x>t?mlog(x/t,shared):(x-t)/t;
   const double fy=y>t?mlog(y/t,shared):(y-t)/t;
   r=scale*(fx-fy);
  }
  d=scale/fmax(x,t);
 }
 if(derivative)*derivative=d;
 return r;
}
// Measured V=0 interpolation, independent of fitted Iph; never extrapolate.
JFM_HD inline double iscShift(const double* v,const double* y,int count) {
 for(int i=0;i<count;++i)if(v[i]==0)return -y[i];
 for(int i=1;i<count;++i)if((v[i-1]<0&&v[i]>0)||(v[i-1]>0&&v[i]<0))
  return -(y[i-1]+(y[i]-y[i-1])*(-v[i-1])/(v[i]-v[i-1]));
 return NAN;
}
struct ScalarCurrentEvaluator {
 bool shared;
 JFM_HD double operator()(int,double v,GpuModelType model,const double* p,double T,double* jac) {
  return current(model,v,p,T,jac,shared);
 }
};
template<class Evaluator> JFM_HD inline double objectiveKernel(GpuModelType model,const GpuParamLayout& l,const double* p,const double* v,const double* y,int count,double* h,double* g,bool precise,bool shared,Evaluator evaluate) {
#if defined(TRACY_ENABLE) && !defined(__CUDACC__)
 ZoneScopedN("JFM.objective.model");
 // Model, Lambert W and analytic Jacobian are fused; measure the batch, never individual points.
 ZoneNamedN(jfmJacobian,"JFM.Jacobian",g!=nullptr);
 ZoneNamedN(jfmLambertW,"JFM.LambertW.batch",true);
#endif
 double params[8];full(l,p,params);double chi=0;
 if(g)for(int j=0;j<l.n_free;++j){g[j]=0;for(int k=0;k<l.n_free;++k)h[j*8+k]=0;}
 for(int i=0;i<count;++i) {
  double dj[8],I=evaluate(i,v[i],model,params,l.T,g?dj:nullptr);
  double r;
  if(l.objective==JfmObjective::LogLegacy) {
   if(!(y[i]>0&&I>0)||!isfinite(I)||y[i]==1)return inf;
   r=relativeLogResidual(I,y[i],precise,shared,g?dj:nullptr);
  } else {
   double derivative;
   r=statisticalResidual(I,y[i],currentSigma(l,y[i],i),l,&derivative,shared);
   if(!isfinite(r))return inf;
   if(g)for(int k=0;k<7;++k)dj[k]*=derivative;
  }
  // Point order is part of the LM polish acceptance contract.
  chi+=r*r;
  if(g) {
   double jv[8];for(int j=0;j<l.n_free;++j)jv[j]=dj[l.free_to_total[j]];
   for(int j=0;j<l.n_free;++j){g[j]+=jv[j]*r;for(int k=0;k<=j;++k)h[j*8+k]+=jv[j]*jv[k];}
  }
 }
 if(g)for(int j=0;j<l.n_free;++j)for(int k=0;k<j;++k)h[k*8+j]=h[j*8+k];
 return chi;
}
JFM_HD inline double objectiveScalar(GpuModelType model,const GpuParamLayout& l,const double* p,const double* v,const double* y,int count,double* h=nullptr,double* g=nullptr,bool precise=false,bool shared=false) {
 return objectiveKernel(model,l,p,v,y,count,h,g,precise,shared,ScalarCurrentEvaluator{shared});
}
JFM_HD inline double objective(GpuModelType model,const GpuParamLayout& l,const double* p,const double* v,const double* y,int count,double* h=nullptr,double* g=nullptr,bool precise=false,bool shared=false) {
#if defined(_MSC_VER) && defined(_M_X64) && !defined(__CUDACC__)
 if(shared)return cpu::objective(model,l,p,v,y,count,h,g,precise);
#endif
 return objectiveScalar(model,l,p,v,y,count,h,g,precise,shared);
}
JFM_HD inline bool active(const GpuParamLayout& l,const double* p,const double* g,int j) {
 return (p[j]<=l.min_bounds[j]&&g[j]>0)||(p[j]>=l.max_bounds[j]&&g[j]<0);
}
JFM_HD inline double gradient(const GpuParamLayout& l,const double* p,double chi,const double* h,const double* g) {
 double norm=0;
 for(int j=0;j<l.n_free;++j)if(!isfinite(g[j])||!isfinite(h[j*8+j]))return inf;
 for(int j=0;j<l.n_free;++j)if(!active(l,p,g,j))norm=fmax(norm,fabs(g[j])/sqrt(fmax(h[j*8+j]*chi,1e-300)));
 return norm;
}
JFM_HD inline bool solve(double* a,double* b,int n) {
 for(int j=0;j<n;++j) {
  int pivot=j;for(int k=j+1;k<n;++k)if(fabs(a[k*8+j])>fabs(a[pivot*8+j]))pivot=k;
  if(!(fabs(a[pivot*8+j])>1e-30))return false;
  if(pivot!=j) {for(int k=j;k<n;++k){double t=a[j*8+k];a[j*8+k]=a[pivot*8+k];a[pivot*8+k]=t;}double t=b[j];b[j]=b[pivot];b[pivot]=t;}
  for(int k=j+1;k<n;++k){double c=a[k*8+j]/a[j*8+j];for(int t=j+1;t<n;++t)a[k*8+t]-=c*a[j*8+t];b[k]-=c*b[j];}
 }
 for(int j=n-1;j>=0;--j){for(int k=j+1;k<n;++k)b[j]-=a[j*8+k]*b[k];b[j]/=a[j*8+j];}
 return true;
}
}
