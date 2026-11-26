#pragma once
#include <sst/plotlab/ImportWorkflow.hpp>
#include "Core/JobSystem/JobSystemTypes.hpp"

namespace SST::Prototypes::PlotLab {
namespace pl = sst::plotlab;
struct ImportRequest {
    std::vector<std::filesystem::path> paths;
    pl::ImportRecipe recipe;
    std::vector<std::shared_ptr<const pl::ImportPlan>> accepted;
    bool publish = false;
    std::optional<std::uint64_t> previewRevision;
    pl::Id previewProject = 0;
};
struct ImportItemResult {
    std::shared_ptr<const pl::ImportPlan> plan;
    std::vector<pl::ImportDiagnostic> diagnostics;
    pl::Id workbook = 0, worksheet = 0;
    bool reused = false;
};
struct ImportResult {
    JobId job = 0;
    bool running = true, cancelled = false;
    std::size_t total = 0;
    pl::Id project = 0;
    std::uint64_t revision = 0;
    std::vector<ImportItemResult> items;
};
class ImportRun;
class Document;
using ImportRecipes = std::map<std::string,std::shared_ptr<const pl::ImportRecipe>>;
struct ImportUiState {pl::ImportRecipe recipe;std::vector<std::filesystem::path> paths;bool open=false;};
pl::ImportHooks PythonImportHooks(const pl::ImportRecipe&);
void SaveImportPresets(const std::filesystem::path&, const std::map<std::string,pl::ImportRecipe>&,const ImportUiState* = nullptr,const ImportRecipes* = nullptr);
std::map<std::string,pl::ImportRecipe> LoadImportPresets(const std::filesystem::path&,ImportUiState* = nullptr,ImportRecipes* = nullptr);
}
