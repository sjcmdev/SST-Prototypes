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
    void OpenGlobal() {
        globalMode=true;batchOpen=true;batchChecked=false;
    }
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
    bool batchOpen=false,batchCurves=false,batchChecked=false,globalMode=false;
    std::uint64_t checkedProjectRevision=0,checkedSettingsRevision=0;
    std::map<pl::Id,std::array<pl::Id,3>> batchInputs;
    std::weak_ptr<const AnalysisJobResult> drawnPreview;
    struct PreviewRange {
        std::optional<sst::ui::plot::Bounds> target, applied;
        std::string xUnits, yUnits;
        bool manual=false;
    } previewRange, residualRange;
    void Run(bool preview);
    bool DrawPreview();
    void DrawBatch();
    void DrawGlobal();
};
}
