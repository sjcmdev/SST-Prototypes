#include <gtest/gtest.h>
#include "solver/cpu_kernel.hpp"
#include "solver/lm_core.hpp"
#include "solver/lm_policy425.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <vector>

namespace {
using lm::cpu::Dispatch;
void same(double expected,double actual) {
    if(std::isnan(expected)) EXPECT_TRUE(std::isnan(actual));
    else EXPECT_EQ(std::bit_cast<std::uint64_t>(expected),std::bit_cast<std::uint64_t>(actual))
        << "expected=" << expected << " actual=" << actual;
}
GpuParamLayout layout(JfmObjective mode) {
    GpuParamLayout l;
    l.n_total=l.n_free=7;l.T=300;l.objective=mode;
    l.sigma_abs=1e-8;l.sigma_rel=.01;l.shift=.03;l.switch_k=3;
    for(int k=0;k<7;++k)l.free_to_total[k]=6-k;
    return l;
}
}

TEST(JFMCpuKernel, DispatchRequiresIsaAndOsState) {
    EXPECT_TRUE(lm::cpu::dispatch()==Dispatch::Scalar || lm::cpu::avx2Available());
}

TEST(JFMCpuKernel, FullModelDomainAndTailsMatchScalarBits) {
    const double inf=std::numeric_limits<double>::infinity(),nan=std::numeric_limits<double>::quiet_NaN();
    const std::vector<double> v={-.8,-.1,-0.,0.,2e-10,std::nextafter(2e-10,inf),
        std::nextafter(2e-10,-inf),.001,.02,.2,.5,.72,.8,.9,1.,1.2,
        20.,30.,40.,50.,-30.,-40.,-50.,-60.,1e200,-1e200,inf,-inf,nan};
    const std::array<double,7> base={1e-10,1.5,2,1e6,2,2e5,.03};
    for(int model=0;model<10;++model)for(int variant=0;variant<20;++variant) {
        auto p=base;double T=300;
        switch(variant) {
        case 1:p[2]=0;break;
        case 2:p[4]=1.5;break;
        case 3:p[4]=.5;break;
        case 4:p[0]=1e-300;break;
        case 5:p[0]=1e200;p[2]=1e200;break;
        case 6:p[0]=0;break;
        case 7:p[1]=0;break;
        case 8:p[2]=-1;break;
        case 9:p[3]=0;break;
        case 10:p[5]=0;break;
        case 11:p[6]=0;break;
        case 12:p[3]=p[5]=inf;break;
        case 13:T=0;break;
        case 14:T=1e-200;break;
        case 15:p[0]=nan;break;
        case 16:p[4]=3;break;
        case 17:p[4]=1;break;
        case 18:p[4]=0;break;
        case 19:p[6]=-.03;break;
        }
        SCOPED_TRACE(::testing::Message()<<model<<","<<variant);
        for(std::size_t count=0;count<=v.size();++count) {
            std::vector<double> scalar(count),avx(count);
            lm::cpu::currents(GpuModelType(model),v.data(),count,p.data(),T,scalar.data(),Dispatch::Scalar);
            lm::cpu::currents(GpuModelType(model),v.data(),count,p.data(),T,avx.data(),Dispatch::Avx2);
            for(std::size_t i=0;i<count;++i)same(scalar[i],avx[i]);
        }
    }
}

TEST(JFMCpuKernel, ObjectiveAndAnalyticNormalEquationsMatchScalarBits) {
    double p[]={1e-10,1.5,2,1e6,2,2e5,.03};
    for(int model=0;model<10;++model)for(auto mode:{JfmObjective::LogLegacy,JfmObjective::SignedSigma,JfmObjective::IscShift}) {
        auto l=layout(mode);
        double free[7];for(int k=0;k<7;++k)free[k]=p[l.free_to_total[k]];
        for(int count=0;count<=17;++count)for(bool precise:{false,true}) {
            double v[17],y[17],sigma[17];
            for(int i=0;i<count;++i) {
                v[i]=.02+.7*i/16.;
                y[i]=lm::current(GpuModelType(model),v[i],p,300,nullptr,true)*(1+.01*std::sin(i));
                sigma[i]=1e-8+.01*std::abs(y[i]);
            }
            l.point_sigma=count%2?sigma:nullptr;
            double hs[64]={},gs[8]={},ha[64]={},ga[8]={};
            SCOPED_TRACE(::testing::Message()<<model<<","<<int(mode)<<","<<count<<","<<precise);
            const double scalar=lm::cpu::objective(GpuModelType(model),l,free,v,y,count,hs,gs,precise,Dispatch::Scalar);
            const double avx=lm::cpu::objective(GpuModelType(model),l,free,v,y,count,ha,ga,precise,Dispatch::Avx2);
            same(scalar,avx);
            if(std::isfinite(scalar))for(int j=0;j<7;++j) {
                same(gs[j],ga[j]);for(int k=0;k<7;++k)same(hs[j*8+k],ha[j*8+k]);
            }
        }
    }
}

TEST(JFMCpuKernel, DerivativeOracleAndSingularNormalEquations) {
    for(int model=0;model<10;++model)for(auto path:{Dispatch::Scalar,Dispatch::Avx2}) {
        auto l=layout(JfmObjective::SignedSigma);
        double p[]={.03,2e5,2,1e6,2,1.5,1e-10},v[9],y[9];
        for(int i=0;i<9;++i){v[i]=.08+.6*i/8.;y[i]=1e-4*(i-3);}
        double h[64]={},g[8]={};
        ASSERT_TRUE(std::isfinite(lm::cpu::objective(GpuModelType(model),l,p,v,y,9,h,g,true,path)));
        for(int j=0;j<7;++j) {
            const double old=p[j],step=std::abs(old)*1e-5;p[j]=old+step;
            const double plus=lm::cpu::objective(GpuModelType(model),l,p,v,y,9,nullptr,nullptr,true,path);
            p[j]=old-step;
            const double minus=lm::cpu::objective(GpuModelType(model),l,p,v,y,9,nullptr,nullptr,true,path);p[j]=old;
            const double oracle=(plus-minus)/(2*step);
            EXPECT_NEAR(2*g[j],oracle,2e-4*std::max(std::abs(oracle),1e-10)) << model << "," << j;
        }
        // An empty curve has exactly singular normal equations on either path.
        lm::cpu::objective(GpuModelType(model),l,p,v,y,0,h,g,true,path);
        EXPECT_FALSE(lm::solve(h,g,7));
    }
}

TEST(JFMCpuKernel, InvalidResidualsAndFixedParametersMatchScalar) {
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    double p[]={1e-10,1.5,0,1e6,2,2e5,.03};
    const double v[]={-.1,0,.1,.5,.8,.9,1.,1.2,.7};
    for(auto mode:{JfmObjective::LogLegacy,JfmObjective::SignedSigma,JfmObjective::IscShift})
        for(double special:{-1.,0.,1.,nan,inf})for(int index:{0,3,4,8}) {
            auto l=layout(mode);l.n_free=0;
            for(int k=0;k<7;++k)l.fixed_values[k]=p[k];
            double y[9],sigma[9];
            for(int i=0;i<9;++i){y[i]=1e-5*(i-3);sigma[i]=1e-6;}
            for(bool invalidSigma:{false,true}) {
                if(invalidSigma)sigma[index]=special;else y[index]=special;
                l.point_sigma=sigma;
                double hs[64]={},gs[8]={},ha[64]={},ga[8]={};
                same(lm::cpu::objective(GpuModelType::SDRsRshAlphaRsh2Iph,l,p,v,y,9,hs,gs,true,Dispatch::Scalar),
                    lm::cpu::objective(GpuModelType::SDRsRshAlphaRsh2Iph,l,p,v,y,9,ha,ga,true,Dispatch::Avx2));
            }
        }
}

TEST(JFMCpuKernel, ZeroJunctionDoesNotEvaluateUndefinedInactiveTerms) {
    auto l=layout(JfmObjective::SignedSigma);l.sigma_abs=1;
    double p[]={.03,2e5,2,1e6,0,1.5,1e-10},v[4]={},y[4]={},h[64],g[8];
    for(auto path:{Dispatch::Scalar,Dispatch::Avx2}) {
        std::feclearexcept(FE_ALL_EXCEPT);
        EXPECT_TRUE(std::isfinite(lm::cpu::objective(GpuModelType::SDRsRshAlphaRsh2Iph,l,p,v,y,4,h,g,true,path)));
        EXPECT_EQ(std::fetestexcept(FE_INVALID|FE_DIVBYZERO),0);
    }
}

TEST(JFMCpuKernel, N300PointOrderPinsPolishBoundary)
{
	GpuParamLayout layout;
	layout.n_total = layout.n_free = 1;
	layout.objective = JfmObjective::SignedSigma;
	layout.sigma_abs = 1;
	layout.sigma_rel = 0;
	layout.free_to_total[0] = 0;
	layout.min_bounds[0] = 0;
	layout.max_bounds[0] = 2;
	const double parameters[] = {1};
	const std::array<double, 300> voltage{}, measured{};
	double h[64]{}, g[8]{};
	const auto objective = [&](double first, double tail, bool orthogonal)
	{
		const auto evaluate = [=](int i, double, GpuModelType, const double *, double, double *jacobian)
		{
			if (jacobian)
			{
				std::fill_n(jacobian, 7, 0.0);
				jacobian[0] = orthogonal && i == 0 ? 0 : 1;
			}
			return i == 0 ? first : tail;
		};
		return lm::objectiveKernel(GpuModelType::SD0, layout, parameters, voltage.data(), measured.data(),
			300, h, g, true, true, evaluate);
	};
	const double tiny = std::ldexp(1.0, -27);
	const double current = objective(1, tiny, false);
	EXPECT_EQ(std::bit_cast<std::uint64_t>(current), 0x3ff0000000000000ULL);
	const double currentGradient = lm::gradient(layout, parameters, current, h, g);
	const auto acceptsPolish = [&](double candidate, double gradient)
	{
		return candidate <= current * (1 + 2e-14) && gradient < currentGradient;
	};
	const double boundary = objective(std::sqrt(1 + 2e-14), 0, true);
	EXPECT_EQ(std::bit_cast<std::uint64_t>(boundary), 0x3ff000000000005aULL);
	EXPECT_TRUE(acceptsPolish(boundary, lm::gradient(layout, parameters, boundary, h, g)));
	const double outside = objective(std::nextafter(std::sqrt(1 + 2e-14), 2.0), 0, true);
	EXPECT_FALSE(acceptsPolish(outside, lm::gradient(layout, parameters, outside, h, g)));

	// Reversing the same 300 terms changes the decision; a new reduction needs an R2 review.
	const double candidate = objective(std::sqrt(1 + 100 * std::numeric_limits<double>::epsilon()), 0, true);
	EXPECT_EQ(std::bit_cast<std::uint64_t>(candidate), 0x3ff0000000000064ULL);
	EXPECT_FALSE(acceptsPolish(candidate, lm::gradient(layout, parameters, candidate, h, g)));
	double reversed = 0;
	for (int i = 299; i >= 0; --i)
		reversed += i == 0 ? 1 : tiny * tiny;
	EXPECT_EQ(std::bit_cast<std::uint64_t>(reversed), 0x3ff000000000004bULL);
	EXPECT_GT(candidate, current * (1 + 2e-14));
	EXPECT_LE(candidate, reversed * (1 + 2e-14));

	// T257/n300 seed 41 changes bits with margin 0 or 4e-14; pin the production policy too.
	GpuParamLayout fitLayout;
	fitLayout.n_total = 7; fitLayout.n_free = 6; fitLayout.T = 257; fitLayout.dof = 294;
	const double truth[] = {7.5235074385338072e-12, 1.6898464778001436, 7.8649978996147443,
		19447.455372032924, 1.723920713055028, 16795.308326085047, 0};
	const double lower[] = {1e-300, 1.0295310860651916, 1.5309961430036374,
		1545.8295110994384, 0.62010602810366033, 1459.1016346941087};
	const double upper[] = {2.4339047149497036e-08, 2.0295310860651918, 38.274903575090931,
		38645.737777485956, 10, 36477.540867352713};
	for (int j = 0; j < 7; ++j) fitLayout.fixed_values[j] = truth[j];
	for (int j = 0; j < 6; ++j)
	{
		fitLayout.free_to_total[j] = j;
		fitLayout.min_bounds[j] = lower[j]; fitLayout.max_bounds[j] = upper[j];
	}
	std::array<double, 300> fitVoltage, fitCurrent;
	for (int i = 0; i < 300; ++i)
	{
		fitVoltage[i] = 0.1 + (1.02 - 0.1) * i / 299;
		fitCurrent[i] = lm::current(GpuModelType::SDRsRshAlphaRsh2, fitVoltage[i], truth, 257, nullptr, true)
			* (1 + .01 * lm::normal(12345 + std::uint64_t(41) * 0xD1B54A32D192ED03ULL, i));
	}
	double initial[8]{};
	lm::start(fitLayout, 12345, 41, 3, initial);
	const auto fit = lm::fit_variant(GpuModelType::SDRsRshAlphaRsh2, fitLayout, initial,
		fitVoltage.data(), fitCurrent.data(), 300, 50);
	EXPECT_EQ(fit.status, 1); EXPECT_EQ(fit.iterations, 9);
	const double actual[] = {fit.chi, fit.gradient, fit.relative_change, fit.step,
		fit.p[0], fit.p[1], fit.p[2], fit.p[3], fit.p[4], fit.p[5], fit.p[6], fit.p[7]};
	const std::uint64_t expected[] = {0x3f45bec742f54ef2ULL, 0x3d1405f24fe086beULL,
		0x3d049a2b2d64aa89ULL, 0x3d612c36eb807e67ULL, 0x3d9f56638fc5fee2ULL,
		0x3ffaf54921367b3dULL, 0x401fac709d9dd25aULL, 0x40d22aed7ae4ab1cULL,
		0x3ffce5c861f7dce5ULL, 0x40d06b3d00cd66a2ULL, 0, 0};
	for (int i = 0; i < 12; ++i) EXPECT_EQ(std::bit_cast<std::uint64_t>(actual[i]), expected[i]);
}
