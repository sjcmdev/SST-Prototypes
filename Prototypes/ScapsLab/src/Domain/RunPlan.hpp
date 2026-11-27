#pragma once
#include "Domain/BatchDraft.hpp"
#include "Domain/ParameterBounds.hpp"
#include <array>
#include "Domain/Dlts.hpp"
#include <scaps_core/dlcp.hpp>

namespace SST::ScapsLab {
enum class ActionKind { Band, Iv, Cv, Cf, Qe, Dlcp, Dlts, Laplace, Capture, Optical };
inline constexpr int kActionCount=10;
inline bool IsDlts(ActionKind kind){return kind>=ActionKind::Dlts && kind<=ActionKind::Optical;}
inline const char* ActionId(ActionKind kind) {
    switch(kind) {case ActionKind::Band:return "band";case ActionKind::Iv:return "iv";case ActionKind::Cv:return "cv";case ActionKind::Cf:return "cf";case ActionKind::Qe:return "qe";case ActionKind::Dlcp:return "dlcp";case ActionKind::Dlts:return "dlts";case ActionKind::Laplace:return "laplace_dlts";case ActionKind::Capture:return "capture_dlts";case ActionKind::Optical:return "optical_dlts";}
    throw std::invalid_argument("Unknown Action kind");
}
inline const char* ActionOperation(ActionKind kind) {return kind==ActionKind::Iv?"run":ActionId(kind);}
struct RunAction {
    ActionKind kind=ActionKind::Iv;
    bool include=false,onEdit=false;
    std::optional<SweepRange> range; // I-V inherits the project range until edited.
    double voltage_V=0,frequency_Hz=1e3,temperature_K=0; // zero temperature inherits the project
    scaps_core::DlcpConfig dlcp;
    DltsConfig dlts;
    SweepRange amplitudes{SweepSampling::Step,false,.05,.25,.05,5};
    bool constantFlux=true;
    double photon_flux_m2_s=1e16,probe_power_W_m2=1e-3;
};
struct RunPlan {
    std::array<RunAction,kActionCount> actions;
    bool pauseAfterAction=false;
    bool pauseAfterMember=false;
    RunPlan() {
        for(int i=0;i<kActionCount;++i)actions[i].kind=ActionKind(i);
        actions[1].include=true;
        actions[2].range=SweepRange{SweepSampling::Step,false,-.2,.8,.05,21};
        actions[3].range=SweepRange{SweepSampling::Points,true,1e2,1e6,1,31};
        actions[4].range=SweepRange{SweepSampling::Points,false,300,4100,10,61};
        actions[5].range=SweepRange{SweepSampling::Step,false,-1.4,-.4,.5,3};
        actions[5].frequency_Hz=3e5;
        for(int i=6;i<kActionCount;++i){actions[i].range=SweepRange{SweepSampling::Points,false,200,350,1,31};actions[i].frequency_Hz=1e6;}
        actions[7].range=SweepRange{SweepSampling::List,false,300,300,1,1,{300}};
        actions[8].dlts.series=DltsSeries::PulseWidth;actions[8].range=SweepRange{SweepSampling::Points,true,1e-8,1e-3,1,21};
    }
};
inline std::vector<double> ActionSamples(const RunAction& action,const scaps_core::DeviceProject& project) {
    if(action.kind==ActionKind::Band)return {action.voltage_V};
    if(action.range)return SweepValues(*action.range);
    if(action.kind!=ActionKind::Iv)throw std::invalid_argument("Action range is missing");
    return scaps_core::make_voltage_list(project.voltage);
}
inline std::vector<double> PreviewSamples(std::vector<double> samples,int every) {
    if(samples.size()<8)return samples;
    const auto stride=std::min<std::size_t>(std::clamp(every,1,4),(samples.size()-1)/7);
    if(stride==1)return samples;
    std::vector<double> coarse;coarse.reserve((samples.size()-1)/stride+2);
    for(std::size_t i=0;i<samples.size();++i)if(i%stride==0 || i+1==samples.size())coarse.push_back(samples[i]);
    return coarse;
}
inline void ValidateRunPlan(const RunPlan& plan,const ProjectDraft& draft,bool automatic=false) {
    bool any=false;
    for(std::size_t i=0;i<plan.actions.size();++i) {
        const auto& a=plan.actions[i];
        if(a.kind!=ActionKind(i))throw std::invalid_argument("Action IDs/order must be the registered Run Action order");
        if((a.kind==ActionKind::Dlcp || IsDlts(a.kind)) && a.onEdit)throw std::invalid_argument("DLCP requires explicit Run; On edit is disabled");
        if(!(automatic?a.onEdit:a.include))continue;
        any=true;const auto samples=ActionSamples(a,draft.project);
        if(IsDlts(a.kind)) {
            ValidateDlts(a.dlts);RequireParameter("frequency_Hz",a.frequency_Hz);
            if(samples.empty() || samples.size()>1001)throw std::invalid_argument("DLTS series requires 1..1001 members");
            for(std::size_t j=0;j<samples.size();++j){const double x=samples[j];if(!std::isfinite(x) || (a.dlts.series!=DltsSeries::Depth && x<=0) || (j && x<=samples[j-1]))throw std::invalid_argument("DLTS series must be finite and strictly increasing");if(a.dlts.series==DltsSeries::Temperature)RequireParameter("temperature_K",x);else if(a.dlts.series==DltsSeries::Depth)RequireParameter("voltage_V",x);}
            if(a.kind!=ActionKind::Capture && a.dlts.series!=DltsSeries::Temperature)throw std::invalid_argument("Select the capture/depth/DDLTS Action for this series");
            continue;
        }
        if(a.temperature_K!=0)RequireParameter("temperature_K",a.temperature_K);
        RequireParameter("voltage_V",a.voltage_V);RequireParameter("frequency_Hz",a.frequency_Hz);
        for(double value:samples)RequireParameter(a.kind==ActionKind::Cf?"frequency_Hz":a.kind==ActionKind::Qe?"wavelength_nm":"voltage_V",value);
        if(a.kind==ActionKind::Qe){RequireParameter("probe_photon_flux_m2_s",a.photon_flux_m2_s);RequireParameter("probe_power_W_m2",a.probe_power_W_m2);}
        if(a.kind==ActionKind::Dlcp) {
            const auto amps=SweepValues(a.amplitudes);const auto& c=a.dlcp;
            if(amps.size()<3 || amps.size()>101 || samples.size()>1001)throw std::invalid_argument("DLCP requires 3..101 amplitudes and at most 1001 Vmax points");
            for(double amplitude:amps)RequireParameter("voltage_V",amplitude);
            for(std::size_t j=0;j<amps.size();++j)if(!std::isfinite(amps[j]) || amps[j]<=0 || (j && amps[j]<=amps[j-1]))throw std::invalid_argument("Vpp must be positive and strictly increasing");
            if(!std::isfinite(c.profiling_eps_r) || (c.profiling_eps_r!=0 && c.profiling_eps_r<1) || c.profiling_eps_r>10000 || !std::isfinite(c.periodic_tolerance) || c.periodic_tolerance<=0 || c.samples_per_period<32 || c.samples_per_period>65536 || c.min_periods<3 || c.max_periods<c.min_periods || c.max_periods>10000 || double(c.max_periods)*c.samples_per_period>1e7)throw std::invalid_argument("Invalid DLCP permittivity or PSS controls");
        }
        if(samples.empty())throw std::invalid_argument("Action range is empty");
        if(!std::isfinite(a.temperature_K) || a.temperature_K<0 || !std::isfinite(a.voltage_V))throw std::invalid_argument("Invalid Action working conditions");
        if(a.kind==ActionKind::Cv && (!std::isfinite(a.frequency_Hz) || a.frequency_Hz<=0))throw std::invalid_argument("C-V frequency must be positive");
        if(a.kind==ActionKind::Cf || a.kind==ActionKind::Qe)for(double x:samples)if(x<=0)throw std::invalid_argument("Frequency/wavelength must be positive");
        if(a.kind==ActionKind::Qe && (samples.size()>1001 || !std::isfinite(a.constantFlux?a.photon_flux_m2_s:a.probe_power_W_m2) || (a.constantFlux?a.photon_flux_m2_s:a.probe_power_W_m2)<=0))throw std::invalid_argument("Invalid QE probe or more than 1001 samples");
    }
    if(!any)throw std::invalid_argument("Select at least one Action");
}
// File/Python adapters only; workers and panels pass RunPlan/RunAction directly.
RunPlan ReadRunPlan(const nlohmann::json& actions);
nlohmann::json RunPlanJson(const RunPlan&,const nlohmann::json& original=nlohmann::json::object());
RunAction ReadRunAction(ActionKind,const nlohmann::json& request);
}
