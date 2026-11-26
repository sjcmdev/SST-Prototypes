#pragma once
#include <filesystem>
#include <functional>
#include <cstdint>
#include "PlotWorkflowState.hpp"
#include "ImportWorkflow.hpp"

namespace SST::Prototypes::PlotLab {
struct ProjectSidecars {
    WorkflowState workflow;
    std::map<std::string,sst::plotlab::ImportRecipe> presets;
    ImportRecipes recipes;
    ImportUiState ui;
};
ProjectSidecars LoadProjectSidecars(const std::filesystem::path&);
// Hold on the reading thread until the complete typed project bundle has been loaded.
class ProjectFilesLock {
public:
    explicit ProjectFilesLock(const std::filesystem::path&);
    ~ProjectFilesLock();
    ProjectFilesLock(const ProjectFilesLock&)=delete;
    ProjectFilesLock& operator=(const ProjectFilesLock&)=delete;
    bool Recovered() const {return recovered;}
private:
    std::intptr_t native=-1;
    bool recovered=false;
};
// The writer prepares the project and sidecars at the supplied staging path.
void SaveProjectFiles(const std::filesystem::path&, const std::function<bool(const std::filesystem::path&)>& writer,
                      const std::function<void(unsigned)>& afterCommit = {});
bool RecoverProjectFiles(const std::filesystem::path&);
}
