#pragma once
#include <limits>
// Identical FP64 arithmetic on host/device; no FMA, platform libm or fast math.
namespace lm {
constexpr double shared_inf=std::numeric_limits<double>::infinity();
template<class T> JFM_HD inline T shared_exp_series(T r,bool minus_one=false) {
 constexpr double c[]={1.,1.,.5,.16666666666666666,.041666666666666664,.008333333333333333,
  .001388888888888889,.0001984126984126984,.0000248015873015873,.0000027557319223985893,
  .0000002755731922398589,.00000002505210838544172,.00000000208767569878681,
  .00000000016059043836821613,.000000000011470745597729725,.0000000000007647163731819816,
  .00000000000004779477332387385,.0000000000000028114572543455206,.00000000000000015619206968586225};
 T p=c[18];
 #ifdef __CUDA_ARCH__
 #pragma unroll
 #endif
 for(int j=17;j>=1;--j)p=c[j]+r*p;
 return minus_one?r*p:1+r*p;
}
JFM_HD inline double shared_exp(double x) {
 if(isnan(x))return x;
 if(x>709.782712893384)return shared_inf;
 if(x< -745.1332191019411)return 0;
 int k=int(floor(x*1.4426950408889634+.5));
 double r=(x-k*.6931471803691238)-k*1.9082149292705877e-10;
 return ldexp(shared_exp_series(r),k);
}
template<class T> JFM_HD inline T shared_atanh2(T z) {
 T zz=z*z,p=1./39;
 #ifdef __CUDA_ARCH__
 #pragma unroll
 #endif
 for(int j=37;j>=1;j-=2)p=1./j+zz*p;
 return 2*z*p;
}
JFM_HD inline double shared_log(double x) {
 if(x==0)return -shared_inf;
 if(x<0)return NAN;
 if(!isfinite(x))return x;
 int k;double m=frexp(x,&k);
 if(m<.7071067811865476){m*=2;--k;}
 double core=shared_atanh2((m-1)/(m+1));
 return k*.6931471803691238+(core+k*1.9082149292705877e-10);
}
JFM_HD inline double shared_log1p(double x) {
 if(fabs(x)<.5)return shared_atanh2(x/(2+x));
 if(x<=-1)return x==-1?-shared_inf:NAN;
 if(!isfinite(x))return x;
 double u=1+x;return shared_log(u)-((u-1)-x)/u;
}
JFM_HD inline double mexp(double x,bool shared) {return shared?shared_exp(x):exp(x);}
JFM_HD inline double mlog(double x,bool shared) {return shared?shared_log(x):log(x);}
JFM_HD inline double mlog1p(double x,bool shared) {return shared?shared_log1p(x):log1p(x);}
JFM_HD inline double mexpm1(double x,bool shared) {return shared?(fabs(x)<.5?shared_exp_series(x,true):shared_exp(x)-1):expm1(x);}
JFM_HD inline double mpow(double x,double y,bool shared) {
 if(!shared)return pow(x,y);
 if(x==0)return y>0?0:y==0?1:shared_inf;
 if(x<0&&y!=floor(y))return NAN;
 double value=shared_exp(y*shared_log(fabs(x)));
 return x<0&&fmod(fabs(y),2.)==1?-value:value;
}
}
