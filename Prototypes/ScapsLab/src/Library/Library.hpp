#pragma once
#include "Core/Assets/Library.hpp"
#include "Domain/ProjectDraft.hpp"
#include <array>
#include <filesystem>
#include <variant>

namespace SST::ScapsLab::Library {
struct State;
struct BandgapTemperature {double Eg0_eV=1.17,alpha_eV_K=4.73e-4,beta_K=636;};
struct EffectiveMass {double electronRatio=1,holeRatio=1;};
struct Material {
    scaps_core::MaterialParams values;
    double Nd_m3=0,Na_m3=0,radiative_cm3_s=0,auger_n_cm6_s=0,auger_p_cm6_s=0;
    scaps_core::AbsorptionModel absorption;
    std::optional<BandgapTemperature> bandgapTemperature;
    std::optional<EffectiveMass> effectiveMass;
};
struct Defect {scaps_core::ProjectDefect value;};
struct Profile {std::string target="Na_m3";scaps_core::Profile value;};
struct Interface {std::vector<scaps_core::ProjectInterfaceDefect> defects;bool referenceReviewRequired=false;};
struct Contact {scaps_core::ProjectContact value;std::optional<double> reflectance;};
struct Illumination {scaps_core::ProjectIllumination value;};
struct Optical {scaps_core::AbsorptionModel absorption;std::vector<std::array<double,3>> refractiveIndex;};
struct Layer {scaps_core::ProjectLayer value;};
struct Device {scaps_core::DeviceProject value;};
using Payload=std::variant<Material,Defect,Profile,Interface,Contact,Illumination,Optical,Layer,Device>;
using Catalog=Assets::Library<Payload>;
using Revision=Assets::Revision<Payload>;
using RevisionRef=std::shared_ptr<const Revision>;
const char* Kind(const Payload&);
void ValidatePayload(const Payload&);
void ValidateRevision(const Revision&);
Catalog Empty(std::string id,std::string name,Assets::Scope scope=Assets::Scope::User);
// The carrier is used by the item editor and file codecs; never serialize it to apply an asset.
scaps_core::DeviceProject Carrier(const Payload&);
Payload FromCarrier(const std::string& kind,const scaps_core::DeviceProject&,const Payload* extras=nullptr,const std::string& profileTarget="Na_m3");
struct Target {std::string layerId;int interfaceAfter=-1;bool rightContact=false;bool appendLayer=false;std::string deviceId,defectId;std::optional<double> composition;};
std::shared_ptr<const ProjectDraft> Apply(const ProjectDraft&,const Revision&,const std::string& libraryId,const Target&,Assets::UsePolicy=Assets::UsePolicy::Copy);
std::vector<Assets::Usage> Usages(const std::string& project,const std::vector<std::pair<std::string,std::shared_ptr<const ProjectDraft>>>& devices);
bool UpdateAvailable(const Assets::Use&,const Catalog&);
struct Difference {std::string path,unit;double before=0,after=0;std::string beforeText,afterText;};
std::vector<Difference> Compare(const Payload&,const Payload&);
std::map<std::string,double> EditableNumbers(const Payload&,const scaps_core::DeviceProject* carrier=nullptr);
std::string NativeUnit(const std::string& path);
void ValidateLinks(const ProjectDraft&,const State&);
struct Preview {std::vector<double> x,y,y2;std::string title,xLabel,yLabel,secondLabel;};
Preview MakePreview(const Payload&);
struct Source {std::filesystem::path path;std::uint64_t fingerprint=0;};
struct Mounted {Catalog catalog;Source source;};
struct State {std::map<std::string,Mounted> libraries;std::vector<std::string> warnings;};
std::shared_ptr<const State> Builtins();
// Real file edges. Import is all-or-nothing; Save uses atomic replace plus optimistic concurrency.
Mounted Load(const std::filesystem::path&,bool readOnly=false);
Mounted LoadShippedLiterature();
void Save(const Catalog&,const std::filesystem::path&,std::optional<std::uint64_t> expectedFingerprint={});
Catalog ImportScaps(const std::filesystem::path&);
RevisionRef ImportCsv(const std::filesystem::path&,const std::string& kind,const std::string& profileTarget="Na_m3");
void ExportCsv(const Payload&,const std::filesystem::path&);
void ExportScaps(const Payload&,const std::filesystem::path&);
std::vector<std::filesystem::path> LiteratureFiles(const std::filesystem::path&);
std::vector<Assets::Usage> ScanProjects(const std::filesystem::path&);
}
