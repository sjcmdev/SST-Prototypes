#include "Domain/ProjectDraft.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace SST::ScapsLab {
namespace {
// One field registry preserves unknown extensions and provides numeric get/set, including optional fields and table cells.
template<class T,class F> void Scalar(const std::string& path,T& value,F& f) {
    f(path,value);
}
template<class T,class F> void Scalar(const std::string& path,std::optional<T>& value,F& f) {if(value)Scalar(path,*value,f);else f(path,value);}
template<class T,class F> void Scalar(const std::string& path,const std::optional<T>& value,F& f) {if(value)Scalar(path,*value,f);else f(path,value);}
template<class T,class F> void Table(const std::string& path,T& value,F& f) {
    f(path,value);
    for(std::size_t i=0;i<value.size();++i) {const auto p=path+"["+std::to_string(i)+"]";Scalar(p+"[0]",value[i].first,f);Scalar(p+"[1]",value[i].second,f);}
}
template<class T,class F> void Profile(const std::string& path,T& v,F& f) {
    Scalar(path+".law",v.law,f);Scalar(path+".source_file",v.source_file,f);
    Scalar(path+".coordinate",v.coordinate,f);Scalar(path+".value",v.value,f);
    Scalar(path+".left",v.left,f);
    Scalar(path+".right",v.right,f);
    Scalar(path+".bowing",v.bowing,f);
    Scalar(path+".midpoint",v.midpoint,f);
    Scalar(path+".middle",v.middle,f);
    Scalar(path+".background",v.background,f);
    Scalar(path+".length_left",v.length_left,f);
    Scalar(path+".length_right",v.length_right,f);
    Scalar(path+".beta_a",v.beta_a,f);
    Scalar(path+".beta_b",v.beta_b,f);
    Scalar(path+".power",v.power,f);
    Scalar(path+".gaussian_center",v.gaussian_center,f);
    Scalar(path+".gaussian_width",v.gaussian_width,f);
    Scalar(path+".diffusion_depth_m",v.diffusion_depth_m,f);
    Scalar(path+".projected_range_m",v.projected_range_m,f);
    Scalar(path+".dose_m2",v.dose_m2,f);
    Scalar(path+".surface_density_m3",v.surface_density_m3,f);
    Scalar(path+".interpolation_logarithmic",v.interpolation_logarithmic,f);
    Scalar(path+".position_relative",v.position_relative,f);
    Scalar(path+".shared_range",v.shared_range,f);
    Scalar(path+".composition_dependent",v.composition_dependent,f);
    for(std::size_t i=0;i<v.coordinate.size();++i)Scalar(path+".coordinate["+std::to_string(i)+"]",v.coordinate[i],f);
    for(std::size_t i=0;i<v.value.size();++i)Scalar(path+".value["+std::to_string(i)+"]",v.value[i],f);
}
template<class T,class F> void Material(const std::string& path,T& v,F& f) {
    Scalar(path+".T_ref_K",v.T_ref_K,f);
    Scalar(path+".eps_r",v.eps_r,f);
    Scalar(path+".Eg_eV",v.Eg_eV,f);
    Scalar(path+".chi_eV",v.chi_eV,f);
    Scalar(path+".Nc_ref_m3",v.Nc_ref_m3,f);
    Scalar(path+".Nv_ref_m3",v.Nv_ref_m3,f);
    Scalar(path+".mu_n_m2_V_s",v.mu_n_m2Vs,f);
    Scalar(path+".mu_p_m2_V_s",v.mu_p_m2Vs,f);
    Scalar(path+".vth_n_ref_m_s",v.vth_n_ref_mps,f);
    Scalar(path+".vth_p_ref_m_s",v.vth_p_ref_mps,f);
    if(v.caughey_thomas) {
        auto& p=*v.caughey_thomas;
        Scalar(path+".caughey_thomas.source",p.source,f);
        Scalar(path+".caughey_thomas.T_ref_K",p.T_ref_K,f);
        Scalar(path+".caughey_thomas.alpha",p.alpha,f);
        Scalar(path+".caughey_thomas.beta_min",p.beta_min,f);
        Scalar(path+".caughey_thomas.beta_N",p.beta_N,f);
        Scalar(path+".caughey_thomas.beta_alpha",p.beta_alpha,f);
        for(const auto& name:{std::string("electron"),std::string("hole")}) {auto& c=name=="electron"?p.electron:p.hole;
            Scalar(path+".caughey_thomas."+name+".mu_min_m2_V_s",c.mu_min_m2Vs,f);
            Scalar(path+".caughey_thomas."+name+".mu_delta_m2_V_s",c.mu_delta_m2Vs,f);
            Scalar(path+".caughey_thomas."+name+".N_ref_m3",c.N_ref_m3,f);
            Scalar(path+".caughey_thomas."+name+".beta_delta",c.beta_delta,f);
        }
    }
}
template<class T,class F> void Absorption(const std::string& path,T& v,F& f) {
    Scalar(path+".model",v.model,f);
    Scalar(path+".file",v.file,f);
    Scalar(path+".A_m1_eV_half",v.A_m1_eV_half,f);
    Scalar(path+".B_m1_eV_minus_half",v.B_m1_eV_minus_half,f);
    Scalar(path+".background_m1",v.background_m1,f);
    Scalar(path+".step_m1",v.step_m1,f);
    Scalar(path+".sqrt_alpha_m1",v.sqrt_alpha_m1,f);
    Scalar(path+".sqrt_beta_m1",v.sqrt_beta_m1,f);
    Scalar(path+".urbach_enabled",v.urbach_enabled,f);
    Scalar(path+".urbach_energy_eV",v.urbach_energy_eV,f);
    Table(path+".table_nm_m1",v.table_nm_m1,f);
    if(v.composition) {auto& g=*v.composition;
        Scalar(path+".composition.file_a",g.file_a,f);
        Scalar(path+".composition.file_b",g.file_b,f);
        Scalar(path+".composition.Eg_a_eV",g.Eg_a_eV,f);
        Scalar(path+".composition.Eg_b_eV",g.Eg_b_eV,f);
        Scalar(path+".composition.uv_a_nm",g.uv_a_nm,f);
        Scalar(path+".composition.uv_b_nm",g.uv_b_nm,f);
        Table(path+".composition.table_a_nm_m1",g.table_a_nm_m1,f);Table(path+".composition.table_b_nm_m1",g.table_b_nm_m1,f);
    }
    for(std::size_t i=0;i<v.powers.size();++i) {auto& p=v.powers[i];const auto prefix=path+".powers["+std::to_string(i)+"]";
        Scalar(prefix+".alpha_m1",p.alpha_m1,f);
        Scalar(prefix+".beta_m1",p.beta_m1,f);
        Scalar(prefix+".threshold_eV",p.threshold_eV,f);
        Scalar(prefix+".exponent",p.exponent,f);
    }
}
template<class T,class F> void Light(const std::string& path,T& v,F& f) {
    Scalar(path+".enabled",v.enabled,f);
    Scalar(path+".uniform_generation_m3_s",v.uniform_generation_m3s,f);
    Scalar(path+".mode",v.mode,f);
    Scalar(path+".generation_quadrature",v.generation_quadrature,f);
    Scalar(path+".intensity_multiplier",v.intensity_multiplier,f);
    Scalar(path+".from_right",v.from_right,f);
    Scalar(path+".spectrum_file",v.spectrum_file,f);
    Scalar(path+".generation_file",v.generation_file,f);
    Scalar(path+".wavelength_nm",v.wavelength_nm,f);
    Scalar(path+".monochromatic_power_W_m2",v.monochromatic_power_W_m2,f);
    Scalar(path+".wavelength_min_nm",v.wavelength_min_nm,f);
    Scalar(path+".wavelength_max_nm",v.wavelength_max_nm,f);
    Scalar(path+".front_transmission",v.front_transmission,f);
    Scalar(path+".back_reflection",v.back_reflection,f);
    Scalar(path+".front_internal_reflection",v.front_internal_reflection,f);
    Scalar(path+".front_filter_file",v.front_filter_file,f);
    Scalar(path+".back_filter_file",v.back_filter_file,f);
    Table(path+".spectrum_nm_W_m2",v.spectrum_nm_W_m2,f);
    Table(path+".generation_m_m3s",v.generation_m_m3s,f);
    Table(path+".front_filter_nm_fraction",v.front_filter_nm_fraction,f);
    Table(path+".back_filter_nm_fraction",v.back_filter_nm_fraction,f);
}
template<class T,class F> void Contact(const std::string& path,T& v,F& f) {
    Scalar(path+".flat_band",v.flat_band,f);
    Scalar(path+".label",v.label,f);
    Scalar(path+".phi_m_eV",v.phi_m_eV,f);
    Scalar(path+".Sn_m_s",v.Sn_mps,f);
    Scalar(path+".Sp_m_s",v.Sp_mps,f);
}
template<class T,class F> void Defect(const std::string& path,T& v,F& f) {
    Scalar(path+".charge_type",v.charge_type,f);Scalar(path+".energy_reference",v.energy_reference,f);
    Scalar(path+".density_profile_m3",v.density_profile_m3,f);Scalar(path+".transitions",v.transitions,f);
    Scalar(path+".optical.enabled",v.optical.enabled,f);Scalar(path+".optical.electron_file",v.optical.electron_file,f);Scalar(path+".optical.hole_file",v.optical.hole_file,f);
    Scalar(path+".label",v.label,f);
    Scalar(path+".energy_offset_eV",v.energy_offset_eV,f);
    Scalar(path+".Nt_m3",v.Nt_m3,f);
    Scalar(path+".sigma_n_m2",v.sigma_n_m2,f);
    Scalar(path+".sigma_p_m2",v.sigma_p_m2,f);
    Scalar(path+".energy_distribution",v.energy_distribution,f);
    Scalar(path+".characteristic_energy_eV",v.characteristic_energy_eV,f);
    Scalar(path+".gaussian_width",v.gaussian_width,f);
    Scalar(path+".tail_width",v.tail_width,f);
    Scalar(path+".energy_levels",v.energy_levels,f);
    Scalar(path+".energy_quadrature",v.energy_quadrature,f);
    Scalar(path+".distribution_normalization",v.distribution_normalization,f);
    Scalar(path+".amphoteric",v.amphoteric,f);
    Scalar(path+".correlated",v.correlated,f);
    Scalar(path+".second_energy_offset_eV",v.second_energy_offset_eV,f);
    Scalar(path+".second_sigma_n_m2",v.second_sigma_n_m2,f);
    Scalar(path+".second_sigma_p_m2",v.second_sigma_p_m2,f);
    Scalar(path+".multivalent_type",v.multivalent_type,f);
    Scalar(path+".most_positive_charge",v.most_positive_charge,f);
    Scalar(path+".unity_degeneracy",v.unity_degeneracy,f);
    if(v.density_profile_m3)Profile(path+".density_profile_m3",*v.density_profile_m3,f);
    Table(path+".optical.electron_table_nm_m2",v.optical.electron_table_nm_m2,f);Table(path+".optical.hole_table_nm_m2",v.optical.hole_table_nm_m2,f);
    for(std::size_t i=0;i<v.transitions.size();++i) {auto& t=v.transitions[i];const auto p=path+".transitions["+std::to_string(i)+"]";Scalar(p+".energy_offset_eV",t.energy_offset_eV,f);Scalar(p+".sigma_n_m2",t.sigma_n_m2,f);Scalar(p+".sigma_p_m2",t.sigma_p_m2,f);}
}
template<class T,class F> void Interface(const std::string& path,T& v,F& f) {
    Scalar(path+".charge_type",v.charge_type,f);
    Scalar(path+".id",v.id,f);
    Scalar(path+".left_layer_index",v.left_layer_index,f);
    Scalar(path+".energy_reference",v.energy_reference,f);
    Scalar(path+".energy_offset_eV",v.energy_offset_eV,f);
    Scalar(path+".Nt_m2",v.Nt_m2,f);
    Scalar(path+".sigma_n_left_m2",v.sigma_n_left_m2,f);
    Scalar(path+".sigma_n_right_m2",v.sigma_n_right_m2,f);
    Scalar(path+".sigma_p_left_m2",v.sigma_p_left_m2,f);
    Scalar(path+".sigma_p_right_m2",v.sigma_p_right_m2,f);
    Scalar(path+".density_convention",v.density_convention,f);
    Scalar(path+".peak_density_m2_eV",v.peak_density_m2_eV,f);
    Scalar(path+".energy_distribution",v.energy_distribution,f);
    Scalar(path+".characteristic_energy_eV",v.characteristic_energy_eV,f);
    Scalar(path+".gaussian_width",v.gaussian_width,f);
    Scalar(path+".tail_width",v.tail_width,f);
    Scalar(path+".energy_levels",v.energy_levels,f);
    Scalar(path+".energy_quadrature",v.energy_quadrature,f);
}
template<class T,class F> void Fields(T& v,F f) {
    Scalar("constants_mode",v.constants_mode,f);Scalar("mobility_temperature_model",v.mobility_temperature_model,f);Scalar("carrier_statistics",v.carrier_statistics,f);Scalar("radiative_limit_mode",v.radiative_limit_mode,f);
    Scalar("layers",v.layers,f);Scalar("interface_defects",v.interface_defects,f);Scalar("metastable_transitions",v.metastable_transitions,f);
    Scalar("schema_version",v.schema_version,f);
    Scalar("case_id",v.case_id,f);
    Scalar("temperature_K",v.temperature_K,f);
    Scalar("external.area_m2",v.external.area_m2,f);
    Scalar("external.series_ohm_m2",v.external.series_ohm_m2,f);
    Scalar("external.shunt_ohm_m2",v.external.shunt_ohm_m2,f);
    Scalar("voltage.start_V",v.voltage.start_V,f);
    Scalar("voltage.stop_V",v.voltage.stop_V,f);
    Scalar("voltage.step_V",v.voltage.step_V,f);
    Scalar("mesh.points_per_layer",v.mesh.points_per_layer,f);
    Scalar("mesh.thermionic_homojunctions",v.mesh.thermionic_homojunctions,f);
    Scalar("mesh.clustering_beta",v.mesh.clustering_beta,f);
    Scalar("mesh.recalculate_mesh",v.mesh.recalculate_mesh,f);
    Scalar("mesh.recalculate_max_adaptions_per_voltage",v.mesh.recalculate_max_adaptions_per_voltage,f);
    Scalar("mesh.recalculate_points",v.mesh.recalculate_points,f);
    Scalar("mesh.recalculate_min_ratio",v.mesh.recalculate_min_ratio,f);
    Scalar("mesh.recalculate_max_ratio",v.mesh.recalculate_max_ratio,f);
    Scalar("mesh.recalculate_generation_limit_uA_cm2",v.mesh.recalculate_generation_limit_uA_cm2,f);
    Scalar("mesh.recalculate_recombination_limit_uA_cm2",v.mesh.recalculate_recombination_limit_uA_cm2,f);
    Scalar("mesh.solution_adaptive",v.mesh.solution_adaptive,f);
    Scalar("mesh.adaptive_max_bias",v.mesh.adaptive_max_bias,f);
    Scalar("mesh.adaptive_delta_psi_Vt",v.mesh.adaptive_delta_psi_Vt,f);
    Scalar("mesh.adaptive_delta_log_carrier",v.mesh.adaptive_delta_log_carrier,f);
    Scalar("mesh.adaptivity_level",v.mesh.adaptivity_level,f);
    Scalar("mesh.adaptive_max_layer_fraction",v.mesh.adaptive_max_layer_fraction,f);
    Scalar("mesh.adaptive_target_relative_J_error",v.mesh.adaptive_target_relative_J_error,f);
    Scalar("solver.max_newton_iterations",v.solver.max_newton_iterations,f);
    Scalar("solver.residual_tolerance",v.solver.residual_tolerance,f);
    Scalar("solver.max_line_search_backtracks",v.solver.max_line_search_backtracks,f);
    Scalar("solver.max_step_multiple_of_Vt",v.solver.max_step_multiple_of_Vt,f);
    Scalar("mesh.adaptive_target_profile_error_eV",v.mesh.adaptive_target_profile_error_eV,f);
    Light("illumination",v.illumination,f);Contact("contacts.left",v.left_contact,f);Contact("contacts.right",v.right_contact,f);
    for(std::size_t i=0;i<v.layers.size();++i) {
        auto& l=v.layers[i];const auto p="layers["+std::to_string(i)+"]";
        Scalar(p+".name",l.name,f);Scalar(p+".defects",l.defects,f);Scalar(p+".grading",l.grading,f);
        Scalar(p+".thickness_m",l.thickness_m,f);Scalar(p+".doping.Nd_m3",l.Nd_m3,f);Scalar(p+".doping.Na_m3",l.Na_m3,f);
        Scalar(p+".recombination.radiative_cm3_s",l.radiative_cm3_s,f);Scalar(p+".recombination.auger_n_cm6_s",l.auger_n_cm6_s,f);Scalar(p+".recombination.auger_p_cm6_s",l.auger_p_cm6_s,f);
        Material(p+".material",l.material,f);Absorption(p+".absorption",l.absorption,f);
        for(std::size_t j=0;j<l.defects.size();++j)Defect(p+".defects["+std::to_string(j)+"]",l.defects[j],f);
        if(l.grading) {
            if(l.grading->composition)Profile(p+".grading.composition",*l.grading->composition,f);
            if(l.grading->Eg_eV)Profile(p+".grading.Eg_eV",*l.grading->Eg_eV,f);
            if(l.grading->chi_eV)Profile(p+".grading.chi_eV",*l.grading->chi_eV,f);
            if(l.grading->eps_r)Profile(p+".grading.eps_r",*l.grading->eps_r,f);
            if(l.grading->Nc_ref_m3)Profile(p+".grading.Nc_ref_m3",*l.grading->Nc_ref_m3,f);
            if(l.grading->Nv_ref_m3)Profile(p+".grading.Nv_ref_m3",*l.grading->Nv_ref_m3,f);
            if(l.grading->mu_n_m2Vs)Profile(p+".grading.mu_n_m2_V_s",*l.grading->mu_n_m2Vs,f);
            if(l.grading->mu_p_m2Vs)Profile(p+".grading.mu_p_m2_V_s",*l.grading->mu_p_m2Vs,f);
            if(l.grading->vth_n_ref_mps)Profile(p+".grading.vth_n_ref_m_s",*l.grading->vth_n_ref_mps,f);
            if(l.grading->vth_p_ref_mps)Profile(p+".grading.vth_p_ref_m_s",*l.grading->vth_p_ref_mps,f);
            if(l.grading->Nd_m3)Profile(p+".grading.Nd_m3",*l.grading->Nd_m3,f);
            if(l.grading->Na_m3)Profile(p+".grading.Na_m3",*l.grading->Na_m3,f);
        }
    }
    for(std::size_t i=0;i<v.interface_defects.size();++i)Interface("interface_defects["+std::to_string(i)+"]",v.interface_defects[i],f);
    for(std::size_t i=0;i<v.metastable_transitions.size();++i) {auto& s=v.metastable_transitions[i];const auto p="metastable_transitions["+std::to_string(i)+"]";
        Scalar(p+".electron_process",s.electron_process,f);Scalar(p+".hole_process",s.hole_process,f);Scalar(p+".transition_reference",s.transition_reference,f);
        Scalar(p+".id",s.id,f);
        Scalar(p+".layer_index",s.layer_index,f);
        Scalar(p+".donor_index",s.donor_index,f);
        Scalar(p+".acceptor_index",s.acceptor_index,f);
        Scalar(p+".donor_charge",s.donor_charge,f);
        Scalar(p+".total_density_m3",s.total_density_m3,f);
        Scalar(p+".EC_eV",s.EC_eV,f);
        Scalar(p+".HC_eV",s.HC_eV,f);
        Scalar(p+".TR_eV",s.TR_eV,f);
        Scalar(p+".initial_acceptor_fraction",s.initial_acceptor_fraction,f);
        Scalar(p+".phonon_frequency_Hz",s.phonon_frequency_Hz,f);
        Scalar(p+".sigma_don_n_m2",s.sigma_don_n_m2,f);
        Scalar(p+".sigma_don_p_m2",s.sigma_don_p_m2,f);
        Scalar(p+".sigma_acc_n_m2",s.sigma_acc_n_m2,f);
        Scalar(p+".sigma_acc_p_m2",s.sigma_acc_p_m2,f);
    }
}
}
double ProjectParameter(const scaps_core::DeviceProject& project,const std::string& path) {
    double result=0;bool found=false;
    Fields(project,[&](const std::string& key,const auto& value){
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_arithmetic_v<T> && !std::is_same_v<T,bool>)if(key==path){result=static_cast<double>(value);found=true;}
    });
    if(!found)throw std::invalid_argument("Unknown typed numeric parameter: "+path);
    return result;
}
void SetProjectParameter(scaps_core::DeviceProject& project,const std::string& path,double value) {
    if(!std::isfinite(value))throw std::invalid_argument("Axis values must be finite numbers");
    bool found=false;
    Fields(project,[&](const std::string& key,auto& target){
        using T=std::remove_reference_t<decltype(target)>;
        if constexpr(std::is_arithmetic_v<T> && !std::is_same_v<T,bool>)if(key==path){
        if constexpr(std::is_integral_v<T>)if(value!=std::floor(value) || static_cast<long double>(value)<std::numeric_limits<T>::lowest() || static_cast<long double>(value)>std::numeric_limits<T>::max())throw std::invalid_argument("Expected integer parameter: "+path);
        target=static_cast<T>(value);found=true;
        }
    });
    if(!found)throw std::invalid_argument("Unknown typed numeric parameter: "+path);
}
std::set<std::string> ProjectFieldPaths(const scaps_core::DeviceProject& project) {
    std::set<std::string> paths;
    Fields(project,[&](const std::string& path,const auto&){paths.insert(path);});
    return paths;
}
}
