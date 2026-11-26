#include "Domain/Dlts.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>

namespace SST::ScapsLab {
using Json=nlohmann::json;
namespace dlts=sst::physics::dlts;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DltsConfig, series,readout,reverse_V,pulse_V,second_pulse_V,pulse_width_s,first_sample_s,last_sample_s,initial_step_s,max_step_s,samples,max_steps,ac_amplitude_V,generation_multiplier,minority,poole_frenkel,filter,gate_start_s,gate_ratio,window_ratio,windows,rate_min_per_s,rate_max_per_s,alpha,polarity,alpha_method,grid_size,regularization_order,dos_mass_m0,velocity_mass_m0,degeneracy_factor,donor_density_m3,profiling_eps_r,capture_density_m3,capture_velocity_m_s,fermi_minus_trap_reverse_eV,fermi_minus_trap_pulse_eV)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CaptureFit,rate_per_s,sigma_m2,amplitude_F,rms_F)
namespace {
void Finite(const std::vector<double>& v){for(double x:v)if(!std::isfinite(x))throw std::invalid_argument("Nonfinite DLTS data");}
Json LaplaceJson(const dlts::LaplaceResult& r) {
    Json out={{"rate_per_s",r.rate_per_s},{"amplitude_F",r.amplitude_F},{"density_F_per_log_rate",r.density_F_per_log_rate},{"alpha",r.alpha},{"alpha_method",int(r.alpha_method)},{"normal_equation_ridge",r.normal_equation_ridge},{"warnings",r.warnings},{"peaks",Json::array()},
        {"predicted_F",r.residuals.predicted_F},{"residual_F",r.residuals.residual_F},{"standardized_residual",r.residuals.standardized_residual},{"rms_residual_F",r.residuals.rms_residual_F},{"lag1_residual_correlation",r.residuals.lag1_residual_correlation},{"degrees_of_freedom",r.residuals.degrees_of_freedom}};
    for(const auto& p:r.peaks)out["peaks"].push_back({{"id",p.id},{"grid_index",p.grid_index},{"rate_per_s",p.rate_per_s},{"bin_amplitude_F",p.bin_amplitude_F}});
    return out;
}
dlts::LaplaceResult ReadLaplace(const Json& j) {
    dlts::LaplaceResult r;r.rate_per_s=j.at("rate_per_s").get<std::vector<double>>();r.amplitude_F=j.at("amplitude_F").get<std::vector<double>>();r.density_F_per_log_rate=j.at("density_F_per_log_rate").get<std::vector<double>>();
    if(r.rate_per_s.size()!=r.amplitude_F.size() || r.rate_per_s.size()!=r.density_F_per_log_rate.size() || r.rate_per_s.size()>201)throw std::invalid_argument("Invalid saved Laplace grid");
    Finite(r.rate_per_s);Finite(r.amplitude_F);Finite(r.density_F_per_log_rate);r.alpha=j.at("alpha");const int method=j.at("alpha_method");if(method<0 || method>2)throw std::invalid_argument("Unknown Laplace regularization");r.alpha_method=dlts::AlphaMethod(method);r.normal_equation_ridge=j.at("normal_equation_ridge");r.warnings=j.at("warnings").get<std::vector<std::string>>();
    auto& residual=r.residuals;residual.predicted_F=j.at("predicted_F").get<std::vector<double>>();residual.residual_F=j.at("residual_F").get<std::vector<double>>();residual.standardized_residual=j.at("standardized_residual").get<std::vector<double>>();residual.rms_residual_F=j.at("rms_residual_F");residual.lag1_residual_correlation=j.at("lag1_residual_correlation");residual.degrees_of_freedom=j.at("degrees_of_freedom");
    for(const auto& p:j.at("peaks")){dlts::LaplacePeak peak;peak.id=p.at("id");peak.grid_index=p.at("grid_index");peak.rate_per_s=p.at("rate_per_s");peak.bin_amplitude_F=p.at("bin_amplitude_F");if(peak.grid_index>=r.rate_per_s.size())throw std::invalid_argument("Invalid Laplace peak index");r.peaks.push_back(std::move(peak));}return r;
}
Json ArrheniusJson(const dlts::ArrheniusResult& r) {
    return {{"apparent_activation_eV",r.apparent_activation_eV},{"trap_activation_eV",r.trap_activation_eV},{"capture_activation_eV",r.capture_activation_eV},{"sigma_infinity_cm2",r.sigma_infinity_cm2},{"prefactor_per_s_K2",r.prefactor_per_s_K2},{"sigma_activation_eV",r.sigma_activation_eV},{"sigma_cross_section_cm2",r.sigma_cross_section_cm2},{"covariance",r.covariance_activation_eV_prefactor_per_s_K2},{"chi_squared",r.chi_squared},{"degrees_of_freedom",r.degrees_of_freedom},{"dos_mass_m0",r.dos_mass_m0},{"velocity_mass_m0",r.velocity_mass_m0},{"degeneracy_factor",r.degeneracy_factor},{"thermal_velocity",int(r.thermal_velocity)},{"warning",r.warning?Json(*r.warning):Json(nullptr)}};
}
dlts::ArrheniusResult ReadArrhenius(const Json& j) {
    dlts::ArrheniusResult r;r.apparent_activation_eV=j.at("apparent_activation_eV");r.trap_activation_eV=j.at("trap_activation_eV");r.capture_activation_eV=j.at("capture_activation_eV");r.sigma_infinity_cm2=j.at("sigma_infinity_cm2");r.prefactor_per_s_K2=j.at("prefactor_per_s_K2");r.sigma_activation_eV=j.at("sigma_activation_eV");r.sigma_cross_section_cm2=j.at("sigma_cross_section_cm2");r.covariance_activation_eV_prefactor_per_s_K2=j.at("covariance").get<std::array<double,4>>();r.chi_squared=j.at("chi_squared");r.degrees_of_freedom=j.at("degrees_of_freedom");r.dos_mass_m0=j.at("dos_mass_m0");r.velocity_mass_m0=j.at("velocity_mass_m0");r.degeneracy_factor=j.at("degeneracy_factor");r.thermal_velocity=dlts::ThermalVelocity(j.at("thermal_velocity").get<int>());if(!j.at("warning").is_null())r.warning=j.at("warning").get<std::string>();return r;
}
}
Json DltsConfigJson(const DltsConfig& c){return Json(c);}
DltsConfig ReadDltsConfig(const Json& j){
    for(const auto* key:{"series","readout","samples","max_steps","filter","windows","alpha_method","grid_size","regularization_order"})if(j.contains(key) && (!j.at(key).is_number_integer() || j.at(key)<std::numeric_limits<int>::min() || j.at(key)>std::numeric_limits<int>::max()))throw std::invalid_argument("DLTS enum/count fields must be bounded integers");
    auto c=j.get<DltsConfig>();ValidateDlts(c);return c;
}
Json DltsDataJson(const DltsData& r) {
    Json out={{"traces",Json::array()},{"spectra",Json::array()},{"warnings",r.warnings},{"cancelled",r.cancelled}};
    for(const auto& p:r.traces) {
        const auto& t=*p;Json trace={{"id",t.id},{"status",t.status},{"message",t.message},{"temperature_K",t.temperature_K},{"pulse_width_s",t.pulse_width_s},{"reverse_V",t.reverse_V},{"pulse_V",t.pulse_V},{"readout_resolution_F",t.readout_resolution_F},{"baseline_F",t.baseline_F},{"equilibrium_F",t.equilibrium_F},{"density_valid",t.density_valid},{"depth_m",t.depth_m},{"pulse_depth_m",t.pulse_depth_m},{"trap_density_m3",t.trap_density_m3},{"time_s",t.time_s.get()},{"capacitance_F",t.capacitance_F.get()},{"current_A_m2",t.current_A_m2.get()},{"reference_capacitance_F",t.reference_capacitance_F.get()},{"signal_F",t.signal_F},{"amplitude_F",t.amplitude_F}};
        if(t.laplace)trace["laplace"]=LaplaceJson(*t.laplace);out["traces"].push_back(std::move(trace));
    }
    for(const auto& s:r.spectra) {
        Json spectrum={{"id",s.id},{"temperature_K",s.temperature_K},{"signal_F",s.signal_F},{"sigma_signal_F",s.sigma_signal_F},{"amplitude_F",s.amplitude_F},{"rate_per_s",s.rate_per_s},{"gain",s.gain},{"warnings",s.warnings},{"peaks",Json::array()}};
        for(const auto& p:s.peaks)spectrum["peaks"].push_back({{"id",p.id},{"sample_index",p.sample_index},{"temperature_K",p.temperature_K},{"signal_F",p.signal_F},{"prominence_F",p.prominence_F},{"rate_per_s",p.rate_per_s},{"gain",p.gain},{"amplitude_F",p.amplitude_F}});
        out["spectra"].push_back(std::move(spectrum));
    }
    if(r.arrhenius)out["arrhenius"]=ArrheniusJson(*r.arrhenius);if(r.capture)out["capture"]=*r.capture;return out;
}
std::shared_ptr<const DltsData> ReadDltsData(const Json& j) {
    auto r=std::make_shared<DltsData>();r->cancelled=j.at("cancelled");r->warnings=j.at("warnings").get<std::vector<std::string>>();
    if(!j.at("traces").is_array() || j.at("traces").size()>1001)throw std::invalid_argument("Invalid DLTS trace count");
    std::set<std::string> ids;
    for(const auto& t:j.at("traces")) {
        auto p=std::make_shared<DltsTrace>();p->id=t.at("id");p->status=t.at("status");p->message=t.at("message");p->temperature_K=t.at("temperature_K");p->pulse_width_s=t.at("pulse_width_s");p->reverse_V=t.at("reverse_V");p->pulse_V=t.at("pulse_V");p->readout_resolution_F=t.at("readout_resolution_F");if(!std::isfinite(p->readout_resolution_F) || p->readout_resolution_F<0)throw std::invalid_argument("Invalid DLTS readout resolution");p->baseline_F=t.at("baseline_F");p->equilibrium_F=t.at("equilibrium_F");p->density_valid=t.at("density_valid");p->depth_m=t.at("depth_m");p->pulse_depth_m=t.at("pulse_depth_m");p->trap_density_m3=t.at("trap_density_m3");
        p->time_s=Column::Intern(t.at("time_s").get<std::vector<double>>());p->capacitance_F=Column::Intern(t.at("capacitance_F").get<std::vector<double>>());p->current_A_m2=Column::Intern(t.at("current_A_m2").get<std::vector<double>>());p->reference_capacitance_F=Column::Intern(t.at("reference_capacitance_F").get<std::vector<double>>());p->signal_F=t.at("signal_F").get<std::vector<double>>();p->amplitude_F=t.at("amplitude_F").get<std::vector<double>>();
        if(!ids.insert(p->id).second || (p->status!="converged" && p->status!="solve_failed") || !std::isfinite(p->temperature_K) || p->temperature_K<=0 || !std::isfinite(p->pulse_width_s) || p->pulse_width_s<=0 || p->time_s.size()>4097 || (p->status=="converged" && (p->time_s.size()<17 || p->time_s.size()!=p->capacitance_F.size() || p->time_s.size()!=p->current_A_m2.size())) || (!p->reference_capacitance_F.empty() && p->reference_capacitance_F.size()!=p->time_s.size()))throw std::invalid_argument("Invalid saved DLTS trace");
        Finite(p->time_s);Finite(p->capacitance_F);Finite(p->current_A_m2);Finite(p->reference_capacitance_F);Finite(p->signal_F);Finite(p->amplitude_F);
        for(std::size_t i=0;i<p->time_s.size();++i)if(p->time_s[i]<=0 || (i && p->time_s[i]<=p->time_s[i-1]))throw std::invalid_argument("DLTS times must increase");
        if(t.contains("laplace"))p->laplace=ReadLaplace(t.at("laplace"));r->traces.push_back(std::move(p));
    }
    for(const auto& s:j.at("spectra")) {
        dlts::Spectrum spectrum;spectrum.id=s.at("id");spectrum.temperature_K=s.at("temperature_K").get<std::vector<double>>();spectrum.signal_F=s.at("signal_F").get<std::vector<double>>();spectrum.sigma_signal_F=s.at("sigma_signal_F").get<std::vector<double>>();spectrum.amplitude_F=s.at("amplitude_F").get<std::vector<double>>();spectrum.rate_per_s=s.at("rate_per_s").get<std::vector<double>>();spectrum.gain=s.at("gain").get<std::vector<double>>();spectrum.warnings=s.at("warnings").get<std::vector<std::string>>();
        if(spectrum.temperature_K.size()!=spectrum.signal_F.size() || spectrum.temperature_K.size()>1001)throw std::invalid_argument("Invalid DLTS spectrum");
        for(const auto& p:s.at("peaks")){dlts::Peak peak;peak.id=p.at("id");peak.sample_index=p.at("sample_index");peak.temperature_K=p.at("temperature_K");peak.signal_F=p.at("signal_F");peak.prominence_F=p.at("prominence_F");peak.rate_per_s=p.at("rate_per_s");peak.gain=p.at("gain");peak.amplitude_F=p.at("amplitude_F");if(peak.sample_index>=spectrum.temperature_K.size())throw std::invalid_argument("Invalid DLTS peak index");spectrum.peaks.push_back(std::move(peak));}r->spectra.push_back(std::move(spectrum));
    }
    if(j.contains("arrhenius"))r->arrhenius=ReadArrhenius(j.at("arrhenius"));if(j.contains("capture"))r->capture=j.at("capture").get<CaptureFit>();return r;
}
}
