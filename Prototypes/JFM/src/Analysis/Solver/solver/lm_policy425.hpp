#pragma once
#include "lm_core.hpp"
// Policy 425 from GPU-fit-bench/lm: conductance, log I0/Rs, More, delayed damping, polish.
namespace lm {
JFM_HD inline double logfloor(const GpuParamLayout& l,int j) {
 return l.min_bounds[j]>0?0:fmax(fabs(l.fixed_values[l.free_to_total[j]])*1e-12,1e-30);
}
JFM_HD inline Result fit_variant(GpuModelType model,const GpuParamLayout& l,const double* initial,const double* v,const double* y,int count,int limit=300) {

 constexpr bool shared=true;
 Result out;for(int j=0;j<l.n_free;++j)out.p[j]=initial[j];
 double mu=1e-3,h[64],g[8],scale[8]={};int polish_steps=0;out.chi=objective(model,l,out.p,v,y,count,h,g,true,shared);
 if(!isfinite(out.chi)){out.status=2;return out;}
 for(int it=0;it<limit;++it) {
  out.iterations=it+1;out.gradient=gradient(l,out.p,out.chi,h,g);
  double target=1e-13;
  if(out.gradient<=gtol&&(out.relative_change<=ftol||out.step<=xtol)&&
     (out.gradient<=target||++polish_steps>=8)){out.status=1;break;}
  double chain[8];
  for(int j=0;j<l.n_free;++j) {
   chain[j]=(l.free_to_total[j]==0||l.free_to_total[j]==2)?out.p[j]+logfloor(l,j):1;
   if((l.free_to_total[j]==3||l.free_to_total[j]==5))chain[j]=-out.p[j]*out.p[j];
   double norm=sqrt(fmax(h[j*8+j],1e-300))*fabs(chain[j]);
   // More scaling never shrinks a column's trust-region scale.
   scale[j]=fmax(scale[j],norm);
  }
  bool accepted=false;
  for(int retry=0;retry<48;++retry) {
   double a[64]={},b[8],candidate[8];
   for(int j=0;j<l.n_free;++j) {
    bool aj=active(l,out.p,g,j);b[j]=aj?0:-g[j]*chain[j]/scale[j];
    for(int k=0;k<l.n_free;++k)a[j*8+k]=aj||active(l,out.p,g,k)?(j==k?1:0):h[j*8+k]*chain[j]/scale[j]*chain[k]/scale[k]+(j==k?mu:0);
   }
   if(!solve(a,b,l.n_free)){mu*=2;continue;}
   double step=0;
   for(int j=0;j<l.n_free;++j) {
    double delta=b[j]/scale[j],p=out.p[j];
    if((l.free_to_total[j]==3||l.free_to_total[j]==5)) {
     double q=1/p+delta;
     p=q<=1/l.max_bounds[j]?l.max_bounds[j]:q>=1/l.min_bounds[j]?l.min_bounds[j]:1/q;
    }
    else if((l.free_to_total[j]==0||l.free_to_total[j]==2))p=(p+logfloor(l,j))*mexp(fmin(fmax(delta,-4.),4.),shared)-logfloor(l,j);
    else p+=delta;
    candidate[j]=fmin(fmax(p,l.min_bounds[j]),l.max_bounds[j]);
    double norm=fmax(fabs(out.p[j]),fmax(fabs(l.fixed_values[l.free_to_total[j]]),1e-30));
    step=fmax(step,fabs(candidate[j]-out.p[j])/norm);
   }
   double ch[64],cg[8];
   double chi=objective(model,l,candidate,v,y,count,ch,cg,true,shared);
   double candidate_gradient=gradient(l,candidate,chi,ch,cg);
   bool rounding_polish=chi<=out.chi*(1+2e-14)&&candidate_gradient<out.gradient;
   if(chi<out.chi||rounding_polish) {
    out.relative_change=fabs(out.chi-chi)/fmax(out.chi,l.objective==JfmObjective::LogLegacy?1e-300:double(l.dof));out.step=step;
    for(int j=0;j<l.n_free;++j)out.p[j]=candidate[j];
    out.chi=objective(model,l,out.p,v,y,count,h,g,true,shared);mu=fmax(mu/(10),1e-15);accepted=true;break;
   }
   if(out.gradient<=target&&step<=xtol){out.step=step;out.relative_change=0;out.status=1;return out;}
   mu*=2;
  }
  if(!accepted){out.status=out.gradient<=gtol&&(out.relative_change<=ftol||out.step<=xtol)?1:3;break;}
 }
 out.gradient=gradient(l,out.p,out.chi,h,g);return out;
}
}
