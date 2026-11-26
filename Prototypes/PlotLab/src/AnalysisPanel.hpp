#pragma once
#include "AnalysisJobs.hpp"
#include <sst/ui/Plot/Model.hpp>

namespace SST::Prototypes::PlotLab {
namespace PythonBinding {pl::UserAnalysisModel LoadUserAnalysisModel(Document&,const std::filesystem::path&,pl::AnalysisRecipe&);}
class AnalysisPanel {
public:
    explicit AnalysisPanel(AnalysisController& controller):controller(controller) {}
    void Draw();
    void SetRecipe(pl::AnalysisRecipe value) {recipe=std::move(value);exactPending=false;++stateRevision;}
    std::uint64_t StateRevision() const {return stateRevision;}
    void OpenBatch() {batchOpen=true;}
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
    std::uint64_t stateRevision=0;
    bool batchOpen=false,batchCurves=false,batchChecked=false;
    std::uint64_t checkedProjectRevision=0,checkedSettingsRevision=0;
    std::map<pl::Id,std::array<pl::Id,3>> batchInputs;
    std::weak_ptr<const AnalysisJobResult> drawnPreview;
    void Run(bool preview);
    void DrawPreview();
    void DrawBatch();
};
}
