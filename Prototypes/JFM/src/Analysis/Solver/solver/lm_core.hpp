#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include "gpu/gpu_types.hpp"
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
// Drobiazg (31)-(32): no -I0; shunts follow the Lambert-W diode.
JFM_HD inline double current(GpuModelType model,double v,const double* p,double T,double* jac=nullptr,bool shared=false) {
 bool series=model==GpuModelType::SDRs||model==GpuModelType::SDRsIph||model==GpuModelType::SDRsRsh||model==GpuModelType::SDRsRshIph||int(model)>=8;
 bool shunt=model==GpuModelType::SDRsh||model==GpuModelType::SDRshIph||model==GpuModelType::SDRsRsh||model==GpuModelType::SDRsRshIph||int(model)>=8;
 bool nonlinear=model==GpuModelType::SDRsRshAlphaRsh2||model==GpuModelType::SDRsRshAlphaRsh2Iph;
 double i0=p[0],n=p[1],rs=series?p[2]:0,rsh=shunt?p[3]:inf,alpha=nonlinear?p[4]:2,rsh2=nonlinear?p[5]:inf;
 if(!(i0>0&&n>0&&T>0&&rs>=0&&rsh>0))return NAN;
 bool light=(int(model)>=4&&int(model)<=7)||model==GpuModelType::SDRsRshAlphaRsh2Iph;
 double a=n*K*T,w=0,d,u;
 if(rs>0) { double c=mlog(i0,shared)+mlog(rs,shared)-mlog(a,shared);w=wlog(c+v/a,shared); d=a/rs*w;u=w>0?a*(mlog(w,shared)-c):v-rs*d; }
 else { d=i0*mexp(v/a,shared);u=v; }
 // Near u=0, log(w)-c cancels; solve u+b*expm1(u/a)=v-b instead.
 if(rs>0 && fabs(u)<.1*a && isfinite(rs*i0)) {
  double b=rs*i0, rhs=fma(-rs,i0,v);u=rhs/(1+b/a);
  for(int k=0;k<4;++k)u-=(u+b*mexpm1(u/a,shared)-rhs)/(1+b/a*mexp(u/a,shared));
  d=i0*mexp(u/a,shared);w=rs*d/a;
 }
 if(nonlinear&&(!(alpha>0&&rsh2>0)||(u<0&&alpha!=floor(alpha))))return NAN;
 double s2=nonlinear?mpow(u,alpha,shared)/rsh2:0;
 double terms[4]={d,u/rsh,s2,light?-p[6]:0},val=sum4(terms,4);
 if(jac) {
  double slope=1/rsh+(nonlinear?alpha*mpow(u,alpha-1,shared)/rsh2:0);
  double dd[3]={d/i0/(1+w),-d*u/(a*n*(1+w)),-d*d/(a*(1+w))};
  double du[3]={-rs*dd[0],-rs*dd[1],-d/(1+w)};
  for(int j=0;j<3;++j)jac[j]=dd[j]+slope*du[j];
  if(!series)jac[2]=0;
  jac[3]=shunt?-u/(rsh*rsh):0;jac[4]=nonlinear?(u==0?0:s2*mlog(fabs(u),shared)):0;
  jac[5]=nonlinear?-s2/rsh2:0;jac[6]=light?-1:0;
 }
 return val;
}
JFM_HD inline void full(const GpuParamLayout& l,const double* p,double* out) {
 for(int j=0;j<l.n_total;++j)out[j]=l.fixed_values[j];
 for(int j=0;j<l.n_free;++j)out[l.free_to_total[j]]=p[j];
}
// Existing light/dark objective; switch residual policy here when the owner decides.
JFM_HD inline double relativeLogResidual(double predicted, double measured, bool precise=true, bool shared=true, double* jacobian=nullptr) {
 if(!(predicted>0&&measured>0)||!isfinite(predicted)||!isfinite(measured)||measured==1)return inf;
 double ly=mlog(measured,shared);
 if(jacobian)for(int k=0;k<7;++k)jacobian[k]=-jacobian[k]/predicted/ly;
 return precise&&fabs(predicted-measured)<.5*measured?-mlog1p((predicted-measured)/measured,shared)/ly:1-mlog(predicted,shared)/ly;
}
JFM_HD inline double objective(GpuModelType model,const GpuParamLayout& l,const double* p,const double* v,const double* y,int count,double* h=nullptr,double* g=nullptr,bool precise=false,bool shared=false) {
 double params[8];full(l,p,params);double chi=0;
 if(g)for(int j=0;j<l.n_free;++j){g[j]=0;for(int k=0;k<l.n_free;++k)h[j*8+k]=0;}
 for(int i=0;i<count;++i) {
  double dj[8],I=current(model,v[i],params,l.T,g?dj:nullptr,shared);
  if(!(y[i]>0&&I>0)||!isfinite(I)||y[i]==1)return inf;
  double r=relativeLogResidual(I,y[i],precise,shared,g?dj:nullptr); chi+=r*r;
  if(g) {
   double jv[8];for(int j=0;j<l.n_free;++j)jv[j]=dj[l.free_to_total[j]];
   for(int j=0;j<l.n_free;++j){g[j]+=jv[j]*r;for(int k=0;k<=j;++k)h[j*8+k]+=jv[j]*jv[k];}
  }
 }
 if(g)for(int j=0;j<l.n_free;++j)for(int k=0;k<j;++k)h[k*8+j]=h[j*8+k];
 return chi;
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
