#pragma once
#include "Domain/BatchDraft.hpp"
#include <array>

namespace SST::ScapsLab {
enum class ActionKind { Band, Iv, Cv, Cf, Qe };
inline const char* ActionId(ActionKind kind) {
    switch(kind) {case ActionKind::Band:return "band";case ActionKind::Iv:return "iv";case ActionKind::Cv:return "cv";case ActionKind::Cf:return "cf";case ActionKind::Qe:return "qe";}
    throw std::invalid_argument("Unknown Action kind");
}
inline const char* ActionOperation(ActionKind kind) {return kind==ActionKind::Iv?"run":ActionId(kind);}
struct RunAction {
    ActionKind kind=ActionKind::Iv;
    bool include=false,onEdit=false;
    std::optional<SweepRange> range; // I-V inherits the project range until edited.
    double voltage_V=0,frequency_Hz=1e3,temperature_K=0; // zero temperature inherits the project
    bool constantFlux=true;
    double photon_flux_m2_s=1e16,probe_power_W_m2=1e-3;
};
struct RunPlan {
    std::array<RunAction,5> actions;
    bool pauseAfterAction=false;
    bool pauseAfterMember=false;
    RunPlan() {
        for(int i=0;i<5;++i)actions[i].kind=ActionKind(i);
        actions[1].include=true;
        actions[2].range=SweepRange{SweepSampling::Step,false,-.2,.8,.05,21};
        actions[3].range=SweepRange{SweepSampling::Points,true,1e2,1e6,1,31};
        actions[4].range=SweepRange{SweepSampling::Points,false,300,4100,10,61};
    }
};
inline std::vector<double> ActionSamples(const RunAction& action,const scaps_core::DeviceProject& project) {
    if(action.kind==ActionKind::Band)return {action.voltage_V};
    if(action.range)return SweepValues(*action.range);
    if(action.kind!=ActionKind::Iv)throw std::invalid_argument("Action range is missing");
    return scaps_core::make_voltage_list(project.voltage);
}
inline void ValidateRunPlan(const RunPlan& plan,const ProjectDraft& draft,bool automatic=false) {
    bool any=false;
    for(std::size_t i=0;i<plan.actions.size();++i) {
        const auto& a=plan.actions[i];
        if(a.kind!=ActionKind(i))throw std::invalid_argument("Action IDs/order must be Band, I-V, C-V, C-f, QE");
        if(!(automatic?a.onEdit:a.include))continue;
        any=true;const auto samples=ActionSamples(a,draft.project);
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
