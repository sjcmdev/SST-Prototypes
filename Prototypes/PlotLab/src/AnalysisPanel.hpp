#pragma once
#include "AnalysisJobs.hpp"
#include <sst/ui/Plot/Model.hpp>

namespace SST::Prototypes::PlotLab {
class AnalysisPanel {
public:
    explicit AnalysisPanel(AnalysisController& controller):controller(controller) {}
    void Draw();
    void SetRecipe(pl::AnalysisRecipe value) {recipe=std::move(value);exactPending=false;}
    const pl::AnalysisRecipe& Recipe() const {return recipe;}
    // Supplied by the trusted runtime adapter, never reconstructed from project source here.
    pl::UserAnalysisModel userModel;
    // Host persists these typed values with its project view state.
    sst::ui::plot::PlotViewState previewView,residualView;
    bool open=true;
private:
    AnalysisController& controller;
    pl::AnalysisRecipe recipe;
    std::string message;
    bool exactPending=false;
    std::weak_ptr<const AnalysisJobResult> drawnPreview;
    void Run(bool preview);
    void DrawPreview();
};
}
