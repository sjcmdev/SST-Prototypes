#pragma once
#include <scaps_core/device_project.hpp>
#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <variant>

namespace SST::ScapsLab {
struct DraftIdentity {
    std::string id,name;
    std::vector<DraftIdentity> defects;
    bool operator==(const DraftIdentity&) const = default;
};
struct DraftFileData; // immutable file spelling/presence and opaque metadata; never solver input
struct ProjectDraft {
    scaps_core::DeviceProject project;
    std::vector<DraftIdentity> layers;
    std::map<std::string,nlohmann::json> extensions;
    std::shared_ptr<const DraftFileData> file;
    std::set<std::string> profileFields; // explicit analytic fields, including values equal to engine defaults
};
using ProjectValue = std::variant<std::monostate,bool,std::int64_t,double,std::string,
    scaps_core::ConstantsMode,scaps_core::MobilityTemperatureModel,scaps_core::CarrierStatistics,
    scaps_core::RadiativeLimitMode,scaps_core::TrapChargeType,scaps_core::DefectEnergyReference,
    scaps_core::GradingLaw,scaps_core::MetastableProcess>;
// File/Python adapters. Runtime edits and jobs use ProjectDraft directly.
[[nodiscard]] std::shared_ptr<const ProjectDraft> DraftFromDocument(const nlohmann::json&);
[[nodiscard]] nlohmann::json DraftDocument(const ProjectDraft&);
[[nodiscard]] std::shared_ptr<const ProjectDraft> DraftFromProject(scaps_core::DeviceProject);
[[nodiscard]] std::shared_ptr<const ProjectDraft> PatchDraft(const ProjectDraft&,const nlohmann::json& patch,const nlohmann::json& pointerSet=nlohmann::json::array());
nlohmann::json IlluminationDocument(const scaps_core::Illumination&);
void ValidateDraft(const ProjectDraft&);
[[nodiscard]] ProjectValue ProjectField(const scaps_core::DeviceProject&,const std::string& path);
void SetProjectField(scaps_core::DeviceProject&,const std::string& path,const ProjectValue&);
[[nodiscard]] std::map<std::string,double> ProjectNumbers(const scaps_core::DeviceProject&);
[[nodiscard]] std::string ProjectFieldPath(const scaps_core::DeviceProject&,const void* address);
[[nodiscard]] std::string ProjectFieldPath(const scaps_core::DeviceProject&,const scaps_core::Profile*);
[[nodiscard]] const std::optional<scaps_core::Profile>& GradingProfile(const scaps_core::LayerGrading&,const std::string&);
[[nodiscard]] double ProjectParameter(const scaps_core::DeviceProject&,const std::string& path);
void SetProjectParameter(scaps_core::DeviceProject&,const std::string& path,double value);
[[nodiscard]] std::set<std::string> ProjectFieldPaths(const scaps_core::DeviceProject&);
[[nodiscard]] std::optional<scaps_core::Profile>& GradingProfile(scaps_core::LayerGrading&,const std::string&);
void EditLayer(ProjectDraft&,const std::string& action,const std::string& id={},int index=0);
void ResetProfileFields(ProjectDraft&,const scaps_core::Profile&);
double& ProfileParameter(ProjectDraft&,scaps_core::Profile&,const std::string&,double fallback);
int AddDraftDefect(ProjectDraft&,int layer,bool sheet);
void MoveDraftDefect(ProjectDraft&,int layer,int from,int to,bool sheet);
void RemoveDraftDefect(ProjectDraft&,int layer,int index,bool sheet);
struct DraftChange {std::function<void(ProjectDraft&,bool)> apply;std::size_t bytes=0;};
struct DraftDelta {
    std::vector<DraftChange> changes;
    std::size_t Bytes() const;
    bool Empty() const {return changes.empty();}
    [[nodiscard]] std::shared_ptr<const ProjectDraft> Apply(const ProjectDraft&,bool forward) const;
};
[[nodiscard]] DraftDelta CompareDrafts(const ProjectDraft&,const ProjectDraft&);
}
