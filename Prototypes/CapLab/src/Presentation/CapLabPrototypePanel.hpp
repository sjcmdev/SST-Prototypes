#pragma once
#include <map>
#include <set>
#include "Presentation/Panels/EditorWindow.hpp"
#include "CapLabCommands.hpp"
#include "Presentation/CapLabSchematicEditor.hpp"
namespace SST::Prototypes::CapLab {
class CapLabPrototypePanel final : public EditorWindow {
public:
    enum class View { Project, Plot, Analysis, Import };
    CapLabPrototypePanel(CapLabWorkflow *state, Ref<CommandRegistry> commands, View view = View::Project)
        : EditorWindow(view == View::Project ? "caplab.workspace" : view == View::Plot ? "caplab.plot" :
            view == View::Analysis ? "caplab.analysis" : "caplab.import",
            view == View::Project ? "Project / measurements" : view == View::Plot ? "C-f" :
            view == View::Analysis ? "Analysis / results" : "Import", true, {.auxiliary = view == View::Import, .openCommand = "caplab.preview", .closeCommand = "caplab.import"}), state(state), commands(std::move(commands)), view(view) {
        auto layout = GetState();
        layout.x = view == View::Project ? 10.0f : view == View::Analysis ? 900.0f : 260.0f;
        layout.y = view == View::Import ? 480.0f : 40.0f;
        layout.width = view == View::Project ? 240.0f : view == View::Analysis ? 370.0f : 630.0f;
        layout.height = view == View::Import ? 190.0f : view == View::Plot ? 430.0f : 630.0f;
        ApplyState(layout);
    }
    bool IsDirty() const override { return view == View::Project && state->IsDirty(); }
    void RenderContent() override;
    void RenderAnalysis();
    void RenderAS();
    void RenderIS();
    void RenderWalter();
private:
    void RenderImport();
    void RenderTree();
    void RenderPlot();
    void RenderExport();
    int exportFormat = 0;
    std::set<pl::Id> selectedExportGraphs;
    void RenderISTabs(Project& project, const Json& analysis, sst::ui::plot::PlotViewState& viewState);
    std::uint64_t shownProject = 0;
    View view;
    std::map<pl::Id, double> areaDraft;
    sst::ui::plot::PlotViewState fitView, bodePhaseView;
    Json isParameters=Json::array();
    pl::Id fitCircuit=0;
    ccui::CapLabSchematicEditor circuitEditor;
    pl::Id isPlotAnalysis=0, isPlotGraph=0;
    bool residualOverlay=true, circuitRecalc=false, circuitAutoReady=false;
    std::string circuitDocument;
    pl::Id selectedCircuit=0, circuitMeasurement=0;
    pl::Id selectedDefect=0;
    CapLabWorkflow *state;
    Ref<CommandRegistry> commands;
};
}
