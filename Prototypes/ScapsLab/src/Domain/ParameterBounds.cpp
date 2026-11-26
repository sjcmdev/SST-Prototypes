#include "Domain/ParameterBounds.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <atomic>
#include <filesystem>
#include <stdexcept>

namespace SST::ScapsLab {
const ParameterBounds& BuiltinBounds() {
    static const auto table=ReadParameterBoundsYaml(
#include "ScapsLabBounds.inc"
    );return table;
}
namespace {
std::atomic<std::shared_ptr<const ParameterBounds>>& Effective() {
    static std::atomic<std::shared_ptr<const ParameterBounds>> table{std::make_shared<const ParameterBounds>(BuiltinBounds())};return table;
}
std::string defaultsPath,userPath;
}
std::shared_ptr<const ParameterBounds> BoundsTable() {return Effective().load();}
std::string UserBoundsFile() {return userPath;}
std::string InitializeParameterBounds(std::string defaultsFile,std::string userFile) {
    defaultsPath=std::move(defaultsFile);userPath=std::move(userFile);return ReloadParameterBounds();
}
std::string ReloadParameterBounds() {
    std::string diagnostic;auto table=LoadParameterBounds(BuiltinBounds(),defaultsPath,userPath,diagnostic);
    Effective().store(std::make_shared<const ParameterBounds>(std::move(table)));return diagnostic;
}
std::string ResetParameterBounds() {
    if(userPath.empty())return "User settings directory is unavailable";
    std::string error;if(!TextFileIO::Save(Path{userPath},"# Override only changed keys/fields. Bounds use SI; see shipped defaults for units.\nschema_version: 1\nparameters: {}\n",error))return error;
    return ReloadParameterBounds();
}
std::string EnsureUserBoundsFile() {
    try {if(std::filesystem::exists(std::filesystem::u8path(userPath)))return {};return ResetParameterBounds();}
    catch(const std::exception& e){return e.what();}
}
double ParameterToSI(std::string_view key) {
    constexpr double electronvolt=1.602176634e-19;
    if(key=="varshni_alpha_eV_K")return electronvolt;
    if(key=="peak_density_m2_eV")return 1/electronvolt;
    // Legacy A/B key exponents are reversed; alpha=(A+B/E)*sqrt(E-Eg).
    if(key=="A_m1_eV_half")return 1/std::sqrt(electronvolt);
    if(key=="B_m1_eV_minus_half")return std::sqrt(electronvolt);
    if(key.ends_with("_eV") || key.starts_with("grading.Eg_eV.") || key.starts_with("grading.chi_eV."))return electronvolt;
    if(key=="grading.absolute_coordinate_um")return 1e-6;
    if(key=="spectral_irradiance_W_m2_nm")return 1e9;
    if(key.ends_with("_nm"))return 1e-9;
    if(key=="radiative_cm3_s")return 1e-6;
    if(key=="auger_n_cm6_s" || key=="auger_p_cm6_s")return 1e-12;
    if(key.ends_with("_uA_cm2"))return .01;
    return 1;
}
std::string ParameterBoundsKey(std::string_view path) {
    if(BuiltinBounds().contains(path))return std::string(path);
    if(path.ends_with("][0]"))return path.find("generation_m_m3s[")!=path.npos?"x_m":"wavelength_nm";
    if(path.ends_with("][1]")) {
        if(path.find("electron_table_nm_m2[")!=path.npos)return "sigma_n_m2";
        if(path.find("hole_table_nm_m2[")!=path.npos)return "sigma_p_m2";
        if(path.find("filter_nm_fraction[")!=path.npos)return "front_transmission";
        if(path.find("generation_m_m3s[")!=path.npos)return "uniform_generation_m3_s";
        if(path.find("spectrum_nm_W_m2[")!=path.npos)return "spectral_irradiance_W_m2_nm";
        return "background_m1";
    }
    const auto last=path.find_last_of('.');auto leaf=std::string(path.substr(last==path.npos?0:last+1));
    const auto grading=path.find(".grading.");const auto densityProfile=path.find(".density_profile_m3.");
    if(grading!=path.npos || densityProfile!=path.npos) {
        if(leaf.starts_with("coordinate["))return "grading.coordinate";
        if(leaf=="background" || leaf=="bowing") {
            const auto start=grading+9,end=path.find('.',start);
            const auto property=densityProfile!=path.npos?std::string("Nt_m3"):std::string(path.substr(start,end-start));
            return "grading."+property+"."+leaf;
        }
        if(leaf=="left" || leaf=="right" || leaf=="middle" || leaf.starts_with("value[")) {
            if(densityProfile!=path.npos)return "Nt_m3";
            const auto start=grading+9,end=path.find('.',start);return std::string(path.substr(start,end-start));
        }
        if(leaf=="midpoint" || leaf=="gaussian_center" || leaf=="gaussian_width" || leaf=="length_left" || leaf=="length_right" || leaf=="beta_a" || leaf=="beta_b" || leaf=="power" || leaf=="bowing")return "grading."+leaf;
    }
    return leaf;
}
void RequireParameter(std::string_view path,double nativeValue) {
    const auto table=BoundsTable();const auto key=ParameterBoundsKey(path);const auto found=table->find(key);
    if(found!=table->end())found->second.Require(path,nativeValue*ParameterToSI(key));
}
}
