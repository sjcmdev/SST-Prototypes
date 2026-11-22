#pragma once
#include <scaps_core/device_project.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace SST::ScapsLab {
struct DraftIdentity {std::string id,name;std::vector<DraftIdentity> defects;};
// Immutable calculation snapshot. The legacy editor document remains the lossless file/UI adapter during migration.
struct ProjectDraft {
    scaps_core::DeviceProject project;
    std::vector<DraftIdentity> layers;
    std::map<std::string,nlohmann::json> extensions; // JSON pointers, including UI-only profile annotations
};
[[nodiscard]] std::shared_ptr<const ProjectDraft> DraftFromDocument(const nlohmann::json&);
// The numeric registry uses the same schema paths as existing batch axes; it never constructs a document tree.
[[nodiscard]] double ProjectParameter(const scaps_core::DeviceProject&,const std::string& path);
void SetProjectParameter(scaps_core::DeviceProject&,const std::string& path,double value);
[[nodiscard]] std::set<std::string> ProjectFieldPaths(const scaps_core::DeviceProject&);
}
