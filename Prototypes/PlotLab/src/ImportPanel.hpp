#pragma once
#include "ImportWorkflow.hpp"
#include "Core/Utils/Memory.hpp"

namespace SST::Prototypes::PlotLab {
struct Session;
class ImportPanel {
public:
    explicit ImportPanel(Session&);
    void Open(std::vector<std::filesystem::path>);
    void Render();
private:
    Session& state;
    bool open = false, refresh = false, stale = true, committing = false;
    double due = 0;
    int renderedFrame = -1;
    std::vector<std::filesystem::path> paths;
    std::string message, selectedPreset;
    pl::ImportRecipe recipe;
    std::shared_ptr<const ImportResult> lastPreview, lastResult;
    std::map<std::string,std::shared_ptr<const pl::Table>> lastValid;
    void Changed();
    void Preview();
};
}
