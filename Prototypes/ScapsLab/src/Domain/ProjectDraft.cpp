#include "Domain/ProjectDraft.hpp"
#include "Domain/BatchDraft.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <algorithm>
#include <span>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <scaps_core/illumination.hpp>
#include <scaps_core/grading.hpp>
#include <scaps_core/physics.hpp>
namespace SST::ScapsLab {
namespace {
// One field registry preserves unknown extensions and provides numeric get/set, including optional fields and table cells.
template<class T,class F> void Scalar(const std::string& path,T& value,F& f) {
    f(path,value);
}
template<class T,class F> void Scalar(const std::string& path,std::optional<T>& value,F& f) {f(path+"?",value);if(value)Scalar(path,*value,f);}
template<class T,class F> void Scalar(const std::string& path,const std::optional<T>& value,F& f) {f(path+"?",value);if(value)Scalar(path,*value,f);}
template<class T,class F> void Table(const std::string& path,T& value,F& f) {
    f(path,value);
    for(std::size_t i=0;i<value.size();++i) {const auto p=path+"["+std::to_string(i)+"]";Scalar(p+"[0]",value[i].first,f);Scalar(p+"[1]",value[i].second,f);}
}
template<class T,class F> void Profile(const std::string& path,T& v,F& f) {
    f(path,v);
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
    f(path+".caughey_thomas?",v.caughey_thomas);
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
    f(path+".composition?",v.composition);
    if(v.composition) {auto& g=*v.composition;
        Scalar(path+".composition.file_a",g.file_a,f);
        Scalar(path+".composition.file_b",g.file_b,f);
        Scalar(path+".composition.Eg_a_eV",g.Eg_a_eV,f);
        Scalar(path+".composition.Eg_b_eV",g.Eg_b_eV,f);
        Scalar(path+".composition.uv_a_nm",g.uv_a_nm,f);
        Scalar(path+".composition.uv_b_nm",g.uv_b_nm,f);
        Table(path+".composition.table_a_nm_m1",g.table_a_nm_m1,f);Table(path+".composition.table_b_nm_m1",g.table_b_nm_m1,f);
    }
    Scalar(path+".powers",v.powers,f);
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
    f(path+".density_profile_m3?",v.density_profile_m3);
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
        f(p+".grading?",l.grading);
        if(l.grading) {
            f(p+".grading.composition?",l.grading->composition);
            if(l.grading->composition)Profile(p+".grading.composition",*l.grading->composition,f);
            f(p+".grading.Eg_eV?",l.grading->Eg_eV);
            if(l.grading->Eg_eV)Profile(p+".grading.Eg_eV",*l.grading->Eg_eV,f);
            f(p+".grading.chi_eV?",l.grading->chi_eV);
            if(l.grading->chi_eV)Profile(p+".grading.chi_eV",*l.grading->chi_eV,f);
            f(p+".grading.eps_r?",l.grading->eps_r);
            if(l.grading->eps_r)Profile(p+".grading.eps_r",*l.grading->eps_r,f);
            f(p+".grading.Nc_ref_m3?",l.grading->Nc_ref_m3);
            if(l.grading->Nc_ref_m3)Profile(p+".grading.Nc_ref_m3",*l.grading->Nc_ref_m3,f);
            f(p+".grading.Nv_ref_m3?",l.grading->Nv_ref_m3);
            if(l.grading->Nv_ref_m3)Profile(p+".grading.Nv_ref_m3",*l.grading->Nv_ref_m3,f);
            f(p+".grading.mu_n_m2_V_s?",l.grading->mu_n_m2Vs);
            if(l.grading->mu_n_m2Vs)Profile(p+".grading.mu_n_m2_V_s",*l.grading->mu_n_m2Vs,f);
            f(p+".grading.mu_p_m2_V_s?",l.grading->mu_p_m2Vs);
            if(l.grading->mu_p_m2Vs)Profile(p+".grading.mu_p_m2_V_s",*l.grading->mu_p_m2Vs,f);
            f(p+".grading.vth_n_ref_m_s?",l.grading->vth_n_ref_mps);
            if(l.grading->vth_n_ref_mps)Profile(p+".grading.vth_n_ref_m_s",*l.grading->vth_n_ref_mps,f);
            f(p+".grading.vth_p_ref_m_s?",l.grading->vth_p_ref_mps);
            if(l.grading->vth_p_ref_mps)Profile(p+".grading.vth_p_ref_m_s",*l.grading->vth_p_ref_mps,f);
            f(p+".grading.Nd_m3?",l.grading->Nd_m3);
            if(l.grading->Nd_m3)Profile(p+".grading.Nd_m3",*l.grading->Nd_m3,f);
            f(p+".grading.Na_m3?",l.grading->Na_m3);
            if(l.grading->Na_m3)Profile(p+".grading.Na_m3",*l.grading->Na_m3,f);
        }
    }
    for(std::size_t i=0;i<v.interface_defects.size();++i)Interface("interface_defects["+std::to_string(i)+"]",v.interface_defects[i],f);
    Scalar("scaps_native.action_lines",v.scaps_native.action_lines,f);Scalar("scaps_native.warnings",v.scaps_native.warnings,f);
    Scalar("scaps_native.sections",v.scaps_native.sections,f);Scalar("scaps_native.optics",v.scaps_native.optics,f);
    for(std::size_t i=0;i<v.scaps_native.sections.size();++i) {auto& s=v.scaps_native.sections[i];const auto p="scaps_native.sections["+std::to_string(i)+"]";Scalar(p+".name",s.name,f);Scalar(p+".lines",s.lines,f);}
    for(std::size_t i=0;i<v.scaps_native.optics.size();++i) {auto& o=v.scaps_native.optics[i];const auto p="scaps_native.optics["+std::to_string(i)+"]";
        Scalar(p+".id",o.id,f);Scalar(p+".kind",o.kind,f);Scalar(p+".source_path",o.source_path,f);Scalar(p+".resolved_path",o.resolved_path,f);Scalar(p+".model",o.model,f);Scalar(p+".parameters",o.parameters,f);
    }
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
    Fields(project,[&](const std::string& path,const auto&){if(!path.ends_with("?"))paths.insert(path);});
    return paths;
}

namespace {
using Json=nlohmann::json;
using namespace scaps_core;
template<class T> struct Optional : std::false_type {using Value=void;};
template<class T> struct Optional<std::optional<T>> : std::true_type {using Value=T;};
template<class T> struct Vector : std::false_type {};
template<class T,class A> struct Vector<std::vector<T,A>> : std::true_type {using Value=T;};
template<class T> constexpr bool Leaf=std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T,std::string> || std::is_same_v<T,std::vector<double>> || std::is_same_v<T,std::vector<std::string>> || std::is_same_v<T,OpticalTable>;
std::string Pointer(const std::string& path) {
    std::string result="/";
    for(char c:path) {if(c=='.' || c=='[')result+='/';else if(c!=']')result+=c;}
    return result;
}
template<class T> auto EnumValues();
#define ENUM_VALUES(T, ...) template<> auto EnumValues<T>() {static const auto values=std::vector<std::pair<T,const char*>>{__VA_ARGS__};return std::span(values);}
ENUM_VALUES(ConstantsMode, {ConstantsMode::CODATA,"codata"},{ConstantsMode::ScapsCompat,"scaps_compat"})
ENUM_VALUES(MobilityTemperatureModel, {MobilityTemperatureModel::Constant,"constant"},{MobilityTemperatureModel::SqrtT,"sqrt_T"},{MobilityTemperatureModel::CaugheyThomasT,"caughey_thomas_T"})
ENUM_VALUES(CarrierStatistics, {CarrierStatistics::Boltzmann,"boltzmann"},{CarrierStatistics::FermiDirac,"fermi_dirac"})
ENUM_VALUES(RadiativeLimitMode, {RadiativeLimitMode::Never,"never"},{RadiativeLimitMode::IfNeeded,"if_needed"},{RadiativeLimitMode::Always,"always"})
ENUM_VALUES(TrapChargeType, {TrapChargeType::Neutral,"neutral"},{TrapChargeType::DonorLike,"donor_like"},{TrapChargeType::AcceptorLike,"acceptor_like"})
ENUM_VALUES(DefectEnergyReference, {DefectEnergyReference::RelativeToEi,"relative_to_Ei"},{DefectEnergyReference::RelativeToEc,"relative_to_Ec"},{DefectEnergyReference::RelativeToEv,"relative_to_Ev"})
ENUM_VALUES(MetastableProcess, {MetastableProcess::DoubleCapture,"double_capture"},{MetastableProcess::CaptureAndEmission,"capture_and_emission"})
#undef ENUM_VALUES
template<class T> std::string EnumName(T value,const std::string& path) {
    if constexpr(std::is_same_v<T,GradingLaw>)return grading_law_name(value);
    else {
        if constexpr(std::is_same_v<T,DefectEnergyReference>)if(path.starts_with("metastable_transitions"))return value==T::RelativeToEc?"Ec":value==T::RelativeToEv?"Ev":"Ei";
        for(auto [v,name]:EnumValues<T>())if(value==v)return name;
        throw std::invalid_argument("Invalid enum: "+path);
    }
}
template<class T> T ReadValue(const Json& value,const std::string& path) {
    if constexpr(std::is_enum_v<T>) {
        const auto text=value.get<std::string>();
        if constexpr(std::is_same_v<T,GradingLaw>)return grading_law_from_string(text);
        else {
            if constexpr(std::is_same_v<T,DefectEnergyReference>)if(path.starts_with("metastable_transitions")) {
                if(text=="Ec")return T::RelativeToEc;if(text=="Ev")return T::RelativeToEv;if(text=="Ei")return T::RelativeToEi;
            }
            for(auto [v,name]:EnumValues<T>())if(text==name)return v;
            throw std::invalid_argument("Unknown "+path+": "+text);
        }
    } else if constexpr(std::is_same_v<T,double>) {
        if(value==".inf")return INFINITY;if(value=="-.inf")return -INFINITY;if(value==".nan")return NAN;
        if(value.is_string()){const auto& text=value.get_ref<const std::string&>();std::size_t used=0;try{const double number=std::stod(text,&used);if(used==text.size())return number;}catch(const std::exception&){}throw std::invalid_argument("Expected number: "+path);}
        if(!value.is_number())throw std::invalid_argument("Expected number: "+path);
        return value.get<double>();
    } else if constexpr(std::is_integral_v<T> && !std::is_same_v<T,bool>) {
        if(!value.is_number() && !value.is_string())throw std::invalid_argument("Expected integer: "+path);
        const double v=ReadValue<double>(value,path);
        if(!std::isfinite(v) || v!=std::floor(v) || static_cast<long double>(v)<std::numeric_limits<T>::lowest() || static_cast<long double>(v)>std::numeric_limits<T>::max())throw std::invalid_argument("Expected integer: "+path);
        return static_cast<T>(v);
    }else if constexpr(std::is_same_v<T,std::string>) {
        if(value.is_string())return value.get<std::string>();
        if(path.starts_with("scaps_native.") && value.is_primitive() && !value.is_null()) {
            if(value.is_boolean())return value.get<bool>()?"true":"false";
            if(value.is_number_integer())return std::to_string(value.get<std::int64_t>());
            if(value.is_number_unsigned())return std::to_string(value.get<std::uint64_t>());
            char buffer[64];const auto [last,error]=std::to_chars(buffer,buffer+sizeof(buffer),value.get<double>(),std::chars_format::general,std::numeric_limits<double>::max_digits10);
            if(error==std::errc{})return std::string(buffer,last);
        }
        throw std::invalid_argument("Expected string: "+path);
    }else if constexpr(std::is_same_v<T,std::vector<std::string>>) {std::vector<std::string> out;for(const auto& item:value)out.push_back(ReadValue<std::string>(item,path));return out;}
    else return value.get<T>();
}
template<class T> Json WriteValue(const T& value,const std::string& path) {
    if constexpr(std::is_enum_v<T>)return EnumName(value,path);
    else if constexpr(std::is_same_v<T,double>) {if(std::isinf(value))return value>0?Json(".inf"):Json("-.inf");if(std::isnan(value))return ".nan";return value;}
    else return value;
}
void ReadFields(ProjectDraft& d,const Json& input,const std::string& prefix="") {
    const auto node=[&](const std::string& pointer)->const Json* {
        if(!pointer.starts_with(prefix) || (pointer.size()>prefix.size() && !prefix.empty() && pointer[prefix.size()]!='/'))return nullptr;
        const Json::json_pointer relative(pointer.substr(prefix.size()));return input.contains(relative)?&input.at(relative):nullptr;
    };
    std::string profilePath;
    Fields(d.project,[&](std::string path,auto& target) {
        using T=std::decay_t<decltype(target)>;
        if constexpr(std::is_same_v<T,scaps_core::Profile>)profilePath=path;
        const bool optional=path.ends_with("?");if(optional)path.pop_back();
        const auto pointer=Pointer(path);
        const auto* value=node(pointer);if(!value)return;
        if constexpr(std::is_same_v<T,double>)if(!profilePath.empty() && path.starts_with(profilePath+"."))d.profileFields.insert(pointer);
        if constexpr(Optional<T>::value) {
            using V=typename Optional<T>::Value;
            if(value->is_null()){target.reset();std::erase_if(d.profileFields,[&](const auto& field){return field.starts_with(pointer+"/");});}
            else if constexpr(Leaf<V>)target=ReadValue<V>(*value,path);
            else if(!target) {
                std::erase_if(d.profileFields,[&](const auto& field){return field.starts_with(pointer+"/");});
                target.emplace(V{});
                if constexpr(std::is_same_v<V,scaps_core::Profile>)target->interpolation_logarithmic=path.ends_with("Nd_m3") || path.ends_with("Na_m3") || path.ends_with("Nc_ref_m3") || path.ends_with("Nv_ref_m3") || path.ends_with("density_profile_m3");
            }
        }else if constexpr(Leaf<T>)target=ReadValue<T>(*value,path);
        else if constexpr(Vector<T>::value) {if(!value->is_array())throw std::invalid_argument("Expected list: "+path);target=T(value->size());std::erase_if(d.profileFields,[&](const auto& field){return field.starts_with(pointer+"/");});}
    });
    const auto* layerInput=node("/layers");
    if(layerInput)d.layers.clear();
    d.layers.resize(d.project.layers.size());
    std::set<std::string> used;
    for(const auto& l:d.layers)for(const auto& defect:l.defects)used.insert(defect.id);
    unsigned next=1;
    for(std::size_t i=0;i<d.layers.size();++i) {
        auto& id=d.layers[i];const auto& layer=d.project.layers[i];const auto p="/layers/"+std::to_string(i);
        if(const auto* value=node(p+"/scapslab_id"))id.id=value->get<std::string>();
        if(id.id.empty())id.id="layer-"+std::to_string(i+1);id.name=layer.name;
        if(node(p+"/defects"))id.defects.clear();id.defects.resize(layer.defects.size());
        for(std::size_t k=0;k<id.defects.size();++k) {
            auto& defect=id.defects[k];
            if(const auto* value=node(p+"/defects/"+std::to_string(k)+"/scapslab_id"))defect.id=value->get<std::string>();
            if(defect.id.empty()){while(used.contains("defect-"+std::to_string(next)))++next;defect.id="defect-"+std::to_string(next++);}
            used.insert(defect.id);defect.name=layer.defects[k].label;
        }
    }
}
Json Canonical(const ProjectDraft& d) {
    Json out=Json::object();
    Fields(d.project,[&](std::string path,const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if(path.ends_with("?")) {
            if constexpr(Optional<T>::value) {path.pop_back();const Json::json_pointer key(Pointer(path));if(!value)out[key]=nullptr;else if constexpr(!Leaf<typename Optional<T>::Value>)out[key]=Json::object();}
        } else {
            const Json::json_pointer key(Pointer(path));
            if constexpr(Leaf<T>)out[key]=WriteValue(value,path);
            else if constexpr(Vector<T>::value) {auto arr=Json::array();for(std::size_t i=0;i<value.size();++i)arr.push_back(Json::object());out[key]=std::move(arr);}
        }
    });
    for(std::size_t i=0;i<d.layers.size();++i) {out["layers"][i]["scapslab_id"]=d.layers[i].id;for(std::size_t k=0;k<d.layers[i].defects.size();++k)out["layers"][i]["defects"][k]["scapslab_id"]=d.layers[i].defects[k].id;}
    return out;
}
void PruneFileFields(Json& value) {
    if(value.is_object()){for(auto it=value.begin();it!=value.end();)if(it->is_null())it=value.erase(it);else {PruneFileFields(*it);++it;}}
    else if(value.is_array())for(auto& item:value)PruneFileFields(item);
}
// Unchanged typed values retain the original file spelling, omitted defaults and opaque fields.
Json MergeFile(const Json& before,const Json& after,const Json& original) {
    if(before==after)return original;
    if(before.is_object() && after.is_object()) {
        auto out=original.is_object()?original:Json::object();
        for(const auto& [key,value]:after.items()) {
            if(before.contains(key) && before.at(key)==value)continue;
            if(value.is_null())out.erase(key);
            else out[key]=MergeFile(before.value(key,Json{}),value,out.value(key,Json{}));
        }
        for(const auto& [key,value]:before.items())if(!after.contains(key))out.erase(key);
        return out;
    }
    if(before.is_array() && after.is_array()) {
        auto out=Json::array();
        for(std::size_t i=0;i<after.size();++i) {
            std::size_t old=i;
            const auto& value=after[i];
            const char* id=value.is_object() && value.contains("scapslab_id")?"scapslab_id":value.is_object() && value.contains("id")?"id":nullptr;
            if(id){old=before.size();for(std::size_t k=0;k<before.size();++k)if(before[k].is_object() && before[k].value(id,Json{})==value.at(id)){old=k;break;}}
            out.push_back(old<before.size()?MergeFile(before[old],value,original.is_array() && old<original.size()?original[old]:Json{}):MergeFile(Json{},value,Json{}));
        }
        return out;
    }
    auto out=after;PruneFileFields(out);return out;
}
std::map<std::string,Json> Extensions(const Json& source,const Json& canonical,const std::string& prefix="") {
    std::map<std::string,Json> result;
    const auto collect=[&](auto&& self,const Json& source,const Json& known,const std::string& pointer)->void {
        if(source.is_object())for(const auto& [key,value]:source.items()) {
            std::string escaped;for(char c:key)escaped+=c=='~'?"~0":c=='/'?"~1":std::string(1,c);
            const auto child=pointer+"/"+escaped;
            if(!known.is_object() || !known.contains(key))result[child]=value;else self(self,value,known.at(key),child);
        }else if(source.is_array())for(std::size_t i=0;i<source.size();++i)self(self,source[i],known.is_array() && i<known.size()?known[i]:Json{},pointer+"/"+std::to_string(i));
    };collect(collect,source,canonical,prefix);return result;
}
template<class T> void Assign(ProjectDraft& draft,const std::string& path,const T& value) {
    bool found=false;
    Fields(draft.project,[&](const std::string& key,auto& target){if constexpr(std::is_same_v<std::decay_t<decltype(target)>,T>)if(key==path){target=value;found=true;}});
    if(!found)throw std::invalid_argument("Missing typed field: "+path);
}
template<class T> std::size_t ValueBytes(const T& value) {
    if constexpr(std::is_same_v<T,std::string>)return sizeof(T)+value.capacity();
    else if constexpr(Vector<T>::value)return sizeof(T)+value.capacity()*sizeof(typename Vector<T>::Value);
    else return sizeof(T);
}
template<class T> void Change(DraftDelta& delta,const std::string& path,const T& a,const T& b) {
    delta.changes.push_back({[path,a,b](ProjectDraft& draft,bool forward){Assign(draft,path,forward?b:a);},sizeof(DraftChange)+path.capacity()+ValueBytes(a)+ValueBytes(b)});
}
}
nlohmann::json MergeFileDocument(const nlohmann::json& original,const nlohmann::json& before,const nlohmann::json& after) {return MergeFile(before,after,original);}
nlohmann::json IlluminationDocument(const scaps_core::Illumination& light) {
    Json out=Json::object();auto write=[&](const std::string& path,const auto& value){using T=std::decay_t<decltype(value)>;if constexpr(Leaf<T>)out[Json::json_pointer(Pointer(path.substr(13)))]=WriteValue(value,path);};Light("illumination",light,write);return out;
}
IlluminationPatch ReadIlluminationPatch(const nlohmann::json& input) {
    if(!input.is_object())throw std::invalid_argument("initial_state.illumination must be an object");
    IlluminationPatch result;scaps_core::Illumination light;std::set<std::string> known;
    auto read=[&](const std::string& path,auto& value){using T=std::decay_t<decltype(value)>;const auto key=path.substr(13);if(key.find_first_of(".[")!=key.npos)return;known.insert(key);if(input.contains(key)){if constexpr(Leaf<T>)result[path]=ReadValue<T>(input.at(key),path);}};Light("illumination",light,read);
    for(const auto& [key,value]:input.items())if(!known.contains(key))throw std::invalid_argument("unknown initial illumination field: "+key);return result;
}
void ApplyIlluminationPatch(scaps_core::Illumination& light,const IlluminationPatch& patch) {
    auto apply=[&](const std::string& path,auto& target){using T=std::decay_t<decltype(target)>;if constexpr(Leaf<T>){if(const auto it=patch.find(path);it!=patch.end())target=std::get<T>(it->second);}};Light("illumination",light,apply);
}
scaps_core::MetastableSpec ReadMetastableSpec(const nlohmann::json& input) {
    for(const auto* key:{"id","layer_index","donor_index","acceptor_index","total_density_m3"})(void)input.at(key);
    ProjectDraft draft;draft.project.metastable_transitions.push_back(input.contains("preset")?scaps_core::metastable_preset(input.at("preset").get<std::string>()):scaps_core::MetastableSpec{});
    draft.project.metastable_transitions[0].transition_reference=scaps_core::DefectEnergyReference::RelativeToEv;
    ReadFields(draft,input,"/metastable_transitions/0");return draft.project.metastable_transitions[0];
}
struct DraftFileData {nlohmann::json source,canonical;};
std::optional<scaps_core::Profile>& GradingProfile(scaps_core::LayerGrading& g,const std::string& name) {
    using G=scaps_core::LayerGrading;
    for(const auto& [key,member]:std::initializer_list<std::pair<const char*,std::optional<scaps_core::Profile> G::*>>{
        {"composition",&G::composition},{"Eg_eV",&G::Eg_eV},{"chi_eV",&G::chi_eV},{"eps_r",&G::eps_r},{"Nd_m3",&G::Nd_m3},{"Na_m3",&G::Na_m3},
        {"Nc_ref_m3",&G::Nc_ref_m3},{"Nv_ref_m3",&G::Nv_ref_m3},{"mu_n_m2_V_s",&G::mu_n_m2Vs},{"mu_p_m2_V_s",&G::mu_p_m2Vs},{"vth_n_ref_m_s",&G::vth_n_ref_mps},{"vth_p_ref_m_s",&G::vth_p_ref_mps}})if(name==key)return g.*member;
    throw std::invalid_argument("Unsupported grading parameter: "+name);
}
const std::optional<scaps_core::Profile>& GradingProfile(const scaps_core::LayerGrading& grading,const std::string& name) {return GradingProfile(const_cast<scaps_core::LayerGrading&>(grading),name);}
std::map<std::string,double> ProjectNumbers(const scaps_core::DeviceProject& project) {
    std::map<std::string,double> values;
    Fields(project,[&](const std::string& path,const auto& value){using T=std::decay_t<decltype(value)>;if constexpr(std::is_arithmetic_v<T> && !std::is_same_v<T,bool>)if(path!="schema_version" && !path.starts_with("scaps_native.") && std::isfinite(double(value)))values[path]=double(value);});return values;
}
std::string ProjectFieldPath(const scaps_core::DeviceProject& project,const void* address) {
    std::string result;Fields(project,[&](const std::string& path,const auto& value){if(std::addressof(value)==address && !path.ends_with("?"))result=path;});return result;
}
std::string ProjectFieldPath(const scaps_core::DeviceProject& project,const scaps_core::Profile* address) {
    std::string result;Fields(project,[&](const std::string& path,const auto& value){if constexpr(std::is_same_v<std::decay_t<decltype(value)>,scaps_core::Profile>)if(&value==address)result=path;});return result;
}
std::shared_ptr<const ProjectDraft> DraftFromDocument(const nlohmann::json& document) {
    auto d=std::make_shared<ProjectDraft>();d->project=scaps_core::make_default_device_project();
    ReadFields(*d,document);
    const auto canonical=Canonical(*d);
    d->extensions=Extensions(document,canonical);
    d->file=std::make_shared<DraftFileData>(DraftFileData{document,canonical});
    ValidateDraft(*d);return d;
}
std::shared_ptr<const ProjectDraft> DraftFromProject(scaps_core::DeviceProject project) {
    auto d=std::make_shared<ProjectDraft>();d->project=std::move(project);
    unsigned defect=1;
    for(std::size_t i=0;i<d->project.layers.size();++i) {const auto& l=d->project.layers[i];DraftIdentity id{"layer-"+std::to_string(i+1),l.name};for(const auto& v:l.defects)id.defects.push_back({"defect-"+std::to_string(defect++),v.label});d->layers.push_back(std::move(id));}
    std::string profilePath;Fields(d->project,[&](const std::string& path,const auto& value){using T=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<T,scaps_core::Profile>)profilePath=path;else if constexpr(std::is_same_v<T,double>)if(!profilePath.empty() && path.starts_with(profilePath+"."))d->profileFields.insert(Pointer(path));});
    ValidateDraft(*d);return d;
}
std::shared_ptr<const ProjectDraft> PatchDraft(const ProjectDraft& source,const nlohmann::json& patch,const nlohmann::json& pointerSet) {
    if(!patch.is_object())throw std::invalid_argument("patch must be an object");
    auto out=std::make_shared<ProjectDraft>(source);
    for(const auto* key:{"layers","interface_defects","metastable_transitions","scaps_native"})if(patch.contains(key))std::erase_if(out->extensions,[&](const auto& entry){return entry.first.starts_with(std::string("/")+key+"/");});
    ReadFields(*out,patch);
    for(const auto& entry:pointerSet)ReadFields(*out,entry.at("value"),entry.at("path").get<std::string>());
    const auto canonical=Canonical(*out);
    for(const auto& [path,value]:Extensions(patch,canonical))out->extensions[path]=value;
    for(const auto& entry:pointerSet) {const auto path=entry.at("path").get<std::string>();const Json::json_pointer pointer(path);const auto& value=entry.at("value");if(!canonical.contains(pointer))out->extensions[path]=value;else for(const auto& [key,v]:Extensions(value,canonical.at(pointer),path))out->extensions[key]=v;}
    ValidateDraft(*out);return out;
}
nlohmann::json DraftDocument(const ProjectDraft& draft) {
    const auto canonical=Canonical(draft);
    auto out=draft.file?MergeFile(draft.file->canonical,canonical,draft.file->source):canonical;
    if(!draft.file)PruneFileFields(out);
    for(const auto& path:draft.profileFields){const Json::json_pointer key(path);if(canonical.contains(key) && !out.contains(key))out[key]=canonical.at(key);}
    for(const auto& [path,value]:Extensions(out,canonical))if(!draft.extensions.contains(path)) {
        const Json::json_pointer pointer(path);const auto parent=pointer.parent_pointer();if(out.contains(parent) && out.at(parent).is_object())out.at(parent).erase(pointer.back());
    }
    for(const auto& [path,value]:draft.extensions)out[Json::json_pointer(path)]=value;
    return out;
}
void ValidateDraft(const ProjectDraft& d) {
    const auto& p=d.project;
    const auto positive=[](double v,const char* field,bool zero=false){if(!std::isfinite(v) || (zero?v<0:v<=0))throw std::invalid_argument(std::string("Invalid ")+field);};
    if(p.schema_version!=1)throw std::invalid_argument("Unsupported project schema_version");
    positive(p.temperature_K,"temperature_K");positive(p.solver.residual_tolerance,"residual_tolerance");
    if(p.layers.empty() || p.layers.size()>100 || d.layers.size()!=p.layers.size())throw std::invalid_argument("Expected 1..100 layers");
    if(p.mesh.points_per_layer<3 || p.mesh.points_per_layer>100000)throw std::invalid_argument("Invalid points_per_layer");
    if(p.solver.max_newton_iterations<1)throw std::invalid_argument("Invalid max_newton_iterations");
    positive(p.mesh.adaptive_target_relative_J_error,"adaptive_target_relative_J_error");
    if(!std::set<std::string>{"0","1","2","3","A","SCAPS"}.contains(p.mesh.adaptivity_level))throw std::invalid_argument("Unknown adaptivity_level");
    std::set<std::string> ids,defects;
    for(std::size_t i=0;i<p.layers.size();++i) {
        const auto& l=p.layers[i];const auto& id=d.layers[i];
        if(id.id.empty() || !ids.insert(id.id).second)throw std::invalid_argument("Invalid or duplicate layer ID");
        if(id.defects.size()!=l.defects.size())throw std::invalid_argument("Invalid defect identities");
        for(const auto& v:id.defects)if(v.id.empty() || !defects.insert(v.id).second)throw std::invalid_argument("Invalid or duplicate defect scapslab_id");
        positive(l.thickness_m,"thickness_m");positive(l.material.eps_r,"eps_r");positive(l.material.Eg_eV,"Eg_eV");
        if(!std::isfinite(l.material.chi_eV))throw std::invalid_argument("Invalid chi_eV");
        positive(l.material.Nc_ref_m3,"Nc_ref_m3");positive(l.material.Nv_ref_m3,"Nv_ref_m3");positive(l.material.mu_n_m2Vs,"mu_n_m2_V_s");positive(l.material.mu_p_m2Vs,"mu_p_m2_V_s");positive(l.Nd_m3,"Nd_m3",true);positive(l.Na_m3,"Na_m3",true);
    }
    for(const auto* c:{&p.left_contact,&p.right_contact})if(!std::isfinite(c->phi_m_eV) || std::isnan(c->Sn_mps) || std::isnan(c->Sp_mps) || c->Sn_mps<0 || c->Sp_mps<0)throw std::invalid_argument("Invalid contact work function or surface velocity");
    const auto& v=p.voltage;
    if(!std::isfinite(v.start_V) || !std::isfinite(v.stop_V) || !std::isfinite(v.step_V) || v.step_V==0 || (v.stop_V-v.start_V)*v.step_V<0 || std::abs((v.stop_V-v.start_V)/v.step_V)>10000)throw std::invalid_argument("Invalid voltage sweep (maximum 10001 points)");
    (void)scaps_core::make_device_from_project(p);
}
ProjectValue ProjectField(const scaps_core::DeviceProject& project,const std::string& path) {
    ProjectValue value;bool found=false;
    Fields(project,[&](const std::string& key,const auto& v){using T=std::decay_t<decltype(v)>;
        if(key==path) {
            if constexpr(std::is_integral_v<T> && !std::is_same_v<T,bool>){value=static_cast<std::int64_t>(v);found=true;}
            else if constexpr(std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T,std::string>){value=v;found=true;}
        }
        if constexpr(Optional<T>::value)if(key==path+"?" && !v){value=std::monostate{};found=true;}
    });
    if(!found)throw std::invalid_argument("Unknown typed parameter: "+path);return value;
}
void SetProjectField(scaps_core::DeviceProject& project,const std::string& path,const ProjectValue& value) {
    bool found=false;
    const auto set=[&]<class T>(T& target) {
        if constexpr(std::is_arithmetic_v<T> && !std::is_same_v<T,bool>) {
            double number;
            if(const auto* n=std::get_if<double>(&value))number=*n;else if(const auto* n=std::get_if<std::int64_t>(&value))number=double(*n);else throw std::invalid_argument("Expected number: "+path);
            if(!std::isfinite(number))throw std::invalid_argument("Axis values must be finite numbers");
            if constexpr(std::is_integral_v<T>)if(number!=std::floor(number) || static_cast<long double>(number)<std::numeric_limits<T>::lowest() || static_cast<long double>(number)>std::numeric_limits<T>::max())throw std::invalid_argument("Expected integer parameter: "+path);
            target=static_cast<T>(number);found=true;
        }else if constexpr(std::is_same_v<T,bool> || std::is_enum_v<T> || std::is_same_v<T,std::string>) {if(const auto* p=std::get_if<T>(&value)){target=*p;found=true;}else throw std::invalid_argument("Wrong typed value: "+path);}
    };
    Fields(project,[&](const std::string& key,auto& target){using T=std::decay_t<decltype(target)>;
        if(key==path)set(target);
        if constexpr(Optional<T>::value) {if constexpr(Leaf<typename Optional<T>::Value>)if(key==path+"?") {if(std::holds_alternative<std::monostate>(value)){target.reset();found=true;}else {if(!target)target.emplace();set(*target);}}}
    });
    if(!found)throw std::invalid_argument("Unknown typed parameter: "+path);
}
std::size_t DraftDelta::Bytes() const {std::size_t n=sizeof(*this);for(const auto& c:changes)n+=c.bytes;return n;}
std::shared_ptr<const ProjectDraft> DraftDelta::Apply(const ProjectDraft& source,bool forward) const {
    auto out=std::make_shared<ProjectDraft>(source);
    if(forward)for(const auto& c:changes)c.apply(*out,true);else for(auto i=changes.rbegin();i!=changes.rend();++i)i->apply(*out,false);
    ValidateDraft(*out);return out;
}
DraftDelta CompareDrafts(const ProjectDraft& before,const ProjectDraft& after) {
    DraftDelta result;
    std::map<std::string,const void*> old;
    Fields(before.project,[&](const std::string& path,const auto& value){old[path]=&value;});
    std::vector<std::string> replaced;
    const auto below=[&](const std::string& path){return std::any_of(replaced.begin(),replaced.end(),[&](const std::string& p){return path.starts_with(p+".") || path.starts_with(p+"[") || path==p+"?";});};
    const bool structure=before.layers!=after.layers;
    if(structure) {
        // Structural steps retain typed layer payloads and identities; numerical results are never part of an edit.
        auto a=before.project.layers,b=after.project.layers;auto ai=before.layers,bi=after.layers;
        result.changes.push_back({[a=std::move(a),b=std::move(b),ai=std::move(ai),bi=std::move(bi)](ProjectDraft& d,bool f){d.project.layers=f?b:a;d.layers=f?bi:ai;},sizeof(DraftChange)+(before.project.layers.size()+after.project.layers.size())*sizeof(scaps_core::ProjectLayer)});
        replaced.push_back("layers");
    }
    Fields(after.project,[&](const std::string& path,const auto& b){
        using T=std::decay_t<decltype(b)>;
        if(below(path))return;
        const auto it=old.find(path);if(it==old.end())return;
        const auto& a=*static_cast<const T*>(it->second);
        if constexpr(Optional<T>::value) {
            if(bool(a)!=bool(b)) {Change(result,path,a,b);replaced.push_back(path.substr(0,path.size()-1));}
        }else if constexpr(Leaf<T>) {if(a!=b){Change(result,path,a,b);if constexpr(Vector<T>::value)replaced.push_back(path);}}
        else if constexpr(Vector<T>::value) {if(a.size()!=b.size()){Change(result,path,a,b);replaced.push_back(path);}}
    });
    if(before.profileFields!=after.profileFields)result.changes.push_back({[a=before.profileFields,b=after.profileFields](ProjectDraft& d,bool f){d.profileFields=f?b:a;},sizeof(DraftChange)});
    if(before.file!=after.file)result.changes.push_back({[a=before.file,b=after.file](ProjectDraft& d,bool f){d.file=f?b:a;},sizeof(DraftChange)});
    if(before.extensions!=after.extensions)result.changes.push_back({[a=before.extensions,b=after.extensions](ProjectDraft& d,bool f){d.extensions=f?b:a;},sizeof(DraftChange)});
    return result;
}
namespace {
std::vector<std::string> InterfaceIds(const ProjectDraft& d) {std::vector<std::string> ids;for(const auto& v:d.project.interface_defects)ids.push_back(v.id);return ids;}
void ReindexExtensions(ProjectDraft& draft,const std::vector<DraftIdentity>& layers,const std::vector<std::string>& interfaces) {
    std::map<std::string,Json> out;
    const auto index=[](const std::string& path,std::size_t start){const auto end=path.find('/',start);return std::pair{std::stoul(path.substr(start,end-start)),end};};
    const auto reindex=[&](const std::string& path)->std::string {
        std::string target=path;
        if(path.starts_with("/layers/")) {
            const auto [old,tail]=index(path,8);if(old>=layers.size())return {};
            const auto layer=std::find_if(draft.layers.begin(),draft.layers.end(),[&](const auto& v){return v.id==layers[old].id;});if(layer==draft.layers.end())return {};
            std::string suffix=tail==path.npos?"":path.substr(tail);
            if(suffix.starts_with("/defects/")){const auto [defect,end]=index(suffix,9);if(defect>=layers[old].defects.size())return {};const auto found=std::find_if(layer->defects.begin(),layer->defects.end(),[&](const auto& v){return v.id==layers[old].defects[defect].id;});if(found==layer->defects.end())return {};suffix="/defects/"+std::to_string(found-layer->defects.begin())+(end==suffix.npos?"":suffix.substr(end));}
            target="/layers/"+std::to_string(layer-draft.layers.begin())+suffix;
        }else if(path.starts_with("/interface_defects/")) {
            const auto [old,tail]=index(path,19);if(old>=interfaces.size())return {};const auto& values=draft.project.interface_defects;const auto found=std::find_if(values.begin(),values.end(),[&](const auto& v){return v.id==interfaces[old];});if(found==values.end())return {};target="/interface_defects/"+std::to_string(found-values.begin())+(tail==path.npos?"":path.substr(tail));
        }
        return target;
    };
    for(auto& [path,value]:draft.extensions)if(auto target=reindex(path);!target.empty())out.emplace(std::move(target),std::move(value));
    std::set<std::string> fields;for(const auto& path:draft.profileFields)if(auto target=reindex(path);!target.empty())fields.insert(std::move(target));draft.profileFields=std::move(fields);
    draft.extensions=std::move(out);
}
}
void EditLayer(ProjectDraft& draft,const std::string& action,const std::string& id,int index) {
    auto& layers=draft.project.layers;auto& ids=draft.layers;const auto previous=ids;const auto interfaces=InterfaceIds(draft);
    if(action!="add" && !draft.project.interface_defects.empty())throw std::invalid_argument("Remove interface defects before removing or reordering layers; interface attachment must be preserved");
    if(action=="add") {
        auto l=layers.back();auto identity=ids.back();unsigned n=1;
        while(std::any_of(ids.begin(),ids.end(),[&](const auto& i){return i.id=="layer-"+std::to_string(n);}))++n;
        identity.id="layer-"+std::to_string(n);identity.name=l.name="New layer";
        for(auto& d:identity.defects)d.id=identity.id+"-"+d.id;
        layers.push_back(std::move(l));ids.push_back(std::move(identity));
        const auto prefix="/layers/"+std::to_string(previous.size()-1)+"/",replacement="/layers/"+std::to_string(previous.size())+"/";auto annotations=draft.extensions;for(const auto& [path,value]:annotations)if(path.starts_with(prefix))draft.extensions[replacement+path.substr(prefix.size())]=value;
        const auto fields=draft.profileFields;for(const auto& path:fields)if(path.starts_with(prefix))draft.profileFields.insert(replacement+path.substr(prefix.size()));
    }else {
        const auto it=std::find_if(ids.begin(),ids.end(),[&](const auto& i){return i.id==id;});
        if(it==ids.end())throw std::invalid_argument("Unknown layer_id");const auto at=it-ids.begin();
        if(action=="remove"){if(layers.size()==1)throw std::invalid_argument("Cannot remove last layer");layers.erase(layers.begin()+at);ids.erase(it);}
        else if(action=="move"){if(index<0 || index>=int(layers.size()))throw std::invalid_argument("Invalid layer index");auto l=std::move(layers[at]);auto identity=std::move(ids[at]);layers.erase(layers.begin()+at);ids.erase(ids.begin()+at);layers.insert(layers.begin()+index,std::move(l));ids.insert(ids.begin()+index,std::move(identity));}
        else throw std::invalid_argument("Unknown layer action");
        ReindexExtensions(draft,previous,interfaces);
    }
}
void ResetProfileFields(ProjectDraft& draft,const scaps_core::Profile& profile) {
    const auto prefix=Pointer(ProjectFieldPath(draft.project,&profile))+"/";std::erase_if(draft.profileFields,[&](const auto& path){return path.starts_with(prefix);});
}
double& ProfileParameter(ProjectDraft& draft,scaps_core::Profile& profile,const std::string& name,double fallback) {
    double* result=nullptr;auto find=[&](const std::string& path,auto& value){if constexpr(std::is_same_v<std::decay_t<decltype(value)>,double>)if(path=="."+name)result=&value;};Profile("",profile,find);if(!result)throw std::invalid_argument("Unknown profile parameter: "+name);
    const auto path=ProjectFieldPath(draft.project,&profile);if(path.empty())throw std::invalid_argument("Profile is not in the draft");
    if(draft.profileFields.insert(Pointer(path)+"/"+name).second)*result=fallback;return *result;
}
int AddDraftDefect(ProjectDraft& draft,int layer,bool sheet) {
    if(layer<0 || layer>=int(draft.layers.size()) || (sheet && layer+1>=int(draft.layers.size())))throw std::invalid_argument("Invalid layer index");
    if(sheet) {
        auto& defects=draft.project.interface_defects;scaps_core::ProjectInterfaceDefect d;d.Nt_m2=1e14;d.density_convention="integrated_sheet";d.left_layer_index=layer;
        unsigned n=1;while(std::any_of(defects.begin(),defects.end(),[&](const auto& v){return v.id=="interface-defect-"+std::to_string(n);}))++n;d.id="interface-defect-"+std::to_string(n);defects.push_back(std::move(d));return int(defects.size()-1);
    }
    unsigned n=1;const auto exists=[&](const std::string& id){for(const auto& l:draft.layers)for(const auto& d:l.defects)if(d.id==id)return true;return false;};while(exists("defect-"+std::to_string(n)))++n;
    scaps_core::ProjectDefect d;d.label="Defect";d.Nt_m3=1e20;d.second_energy_offset_eV=.1;draft.project.layers[layer].defects.push_back(std::move(d));draft.layers[layer].defects.push_back({"defect-"+std::to_string(n),"Defect"});return int(draft.layers[layer].defects.size()-1);
}
void MoveDraftDefect(ProjectDraft& draft,int layer,int from,int to,bool sheet) {
    const auto before=draft.layers;const auto interfaces=InterfaceIds(draft);
    const auto move=[&](auto& values){if(from<0 || to<0 || from>=int(values.size()) || to>=int(values.size()))throw std::invalid_argument("Invalid defect index");auto value=std::move(values[from]);values.erase(values.begin()+from);values.insert(values.begin()+to,std::move(value));};
    if(sheet)move(draft.project.interface_defects);else {move(draft.project.layers.at(layer).defects);move(draft.layers.at(layer).defects);}
    ReindexExtensions(draft,before,interfaces);
}
void RemoveDraftDefect(ProjectDraft& draft,int layer,int index,bool sheet) {
    const auto before=draft.layers;const auto interfaces=InterfaceIds(draft);
    if(sheet){auto& a=draft.project.interface_defects;if(index<0 || index>=int(a.size()))throw std::invalid_argument("Invalid defect index");a.erase(a.begin()+index);}else {auto& a=draft.project.layers.at(layer).defects;if(index<0 || index>=int(a.size()))throw std::invalid_argument("Invalid defect index");a.erase(a.begin()+index);auto& ids=draft.layers.at(layer).defects;ids.erase(ids.begin()+index);}
    ReindexExtensions(draft,before,interfaces);
}
}
