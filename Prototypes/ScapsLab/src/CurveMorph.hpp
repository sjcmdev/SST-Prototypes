#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace SST::ScapsLab {
// Display-only interpolation. Solver results and exported data remain exact.
struct CurveMorph {
    std::vector<double> x,y,from,target,sourceX,sourceY;
    double elapsed=.1;
    bool logarithmic=false;
    static double Sample(const std::vector<double>& x,const std::vector<double>& y,double at,bool log) {
        const bool descending=x.front()>x.back();
        auto it=descending?std::upper_bound(x.begin(),x.end(),at,std::greater<double>{}):std::upper_bound(x.begin(),x.end(),at);
        if(it==x.begin())return y.front();if(it==x.end())return y.back();
        const auto i=std::size_t(it-x.begin());if(x[i]==x[i-1])return y[i];
        const double t=(at-x[i-1])/(x[i]-x[i-1]);
        if(log && y[i]>0 && y[i-1]>0)return std::exp(std::lerp(std::log(y[i-1]),std::log(y[i]),t));
        return std::lerp(y[i-1],y[i],t);
    }
    void Update(const std::vector<double>& xx,const std::vector<double>& yy,double dt,bool log=false,bool holdGrid=false) {
        if(xx.empty() || xx.size()!=yy.size())return;
        if(x.empty() || logarithmic!=log){x=sourceX=xx;y=from=target=sourceY=yy;logarithmic=log;elapsed=.1;return;}
        if(sourceX!=xx || !std::equal(sourceY.begin(),sourceY.end(),yy.begin(),yy.end(),[](double a,double b){return a==b || (std::isnan(a) && std::isnan(b));})) {
            if(!holdGrid && x!=xx){std::vector<double> resampled;resampled.reserve(xx.size());for(double at:xx)resampled.push_back(Sample(x,y,at,log));x=xx;y=std::move(resampled);}
            sourceX=xx;sourceY=yy;from=y;target.clear();target.reserve(x.size());
            if(x==xx)target=yy;else for(double at:x)target.push_back(Sample(xx,yy,at,log));elapsed=0;dt=std::min(dt,1./60.); // a late result must not consume the preceding idle interval
        }
        elapsed=std::min(.1,elapsed+std::max(0.,dt));const double t=elapsed/.1,ease=t*t*(3-2*t);
        for(std::size_t i=0;i<y.size();++i) {
            if(!std::isfinite(target[i])){if(elapsed>=.1)y[i]=target[i];continue;}
            if(!std::isfinite(from[i])){y[i]=target[i];continue;}
            y[i]=log && from[i]>0 && target[i]>0?std::exp(std::lerp(std::log(from[i]),std::log(target[i]),ease)):std::lerp(from[i],target[i],ease);
        }
    }
};
}
