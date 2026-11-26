#pragma once
#include <sst/plotlab/Analysis.hpp>
#include <sst/plotlab/GraphSnapshot.hpp>
namespace SST::Prototypes::PlotLab {
sst::ui::plot::PlotViewState CapturePublicationView(const sst::ui::plot::PlotViewState&);
struct WorkflowState {
    sst::plotlab::AnalysisRecipe recipe;
    sst::ui::plot::PlotViewState previewView,residualView;
    bool analysisOpen=false;
    std::map<sst::plotlab::Id,std::shared_ptr<const sst::plotlab::AnalysisRecipe>> acceptedRecipes;
};
WorkflowState LoadWorkflowState(const std::filesystem::path& project);
void SaveWorkflowState(const std::filesystem::path& project,const WorkflowState&);
}
