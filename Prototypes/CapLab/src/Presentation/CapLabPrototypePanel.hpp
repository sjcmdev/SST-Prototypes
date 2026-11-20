#pragma once
#include <map>
#include <set>
#include "Presentation/Panels/EditorWindow.hpp"
#include "CapLabCommands.hpp"
#include "Presentation/CapLabSchematicEditor.hpp"
namespace SST::Prototypes::CapLab {
class CapLabPrototypePanel final : public EditorWindow {
public:
    enum class View { Project, Plot, CV, AS, Walter, IS, Import };
    CapLabPrototypePanel(CapLabWorkflow *state, Ref<CommandRegistry> commands, View view = View::Project)
        : EditorWindow(view == View::Project ? "caplab.workspace" : view == View::Plot ? "caplab.plot" :
            view == View::CV ? "caplab.analysis" : view == View::AS ? "caplab.as" : view == View::Walter ? "caplab.walter" : view == View::IS ? "caplab.is" : "caplab.import",
            view == View::Project ? "Project / measurements" : view == View::Plot ? "C-f" :
            view == View::CV ? "CV / DLCP" : view == View::AS ? "AS / Arrhenius" : view == View::Walter ? "Walter" : view == View::IS ? "IS / circuits" : "Import", true, {.auxiliary = view == View::Import, .openCommand = "caplab.preview", .closeCommand = "caplab.import"}), state(state), commands(std::move(commands)), view(view) {
        auto layout = GetState();
        layout.x = view == View::Project ? 10.0f : view == View::Plot ? 260.0f : view == View::Import ? 320.0f : 900.0f;
        layout.y = 40.0f;
        layout.width = view == View::Project ? 300.0f : view == View::Plot ? 630.0f : view == View::Import ? 640.0f : 430.0f;
        layout.height = view == View::Plot ? 430.0f : 630.0f;
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
    void RenderBrowser();
    void RenderExport();
    void SelectObject(const char *kind, pl::Id id);
    void PreviewAfterEdit(const Json &options, bool calculate);
    int exportFormat = 0;
    std::set<pl::Id> selectedExportGraphs;
    std::set<pl::Id> compareMeasurements, compareGraphs;
    int compareType=0;
    int asMapView=0, asMapGrid=0, asMapPoints=101;
    double asMapMinHz=100, asMapMaxHz=1e6;
    std::string compareFilter;
    void RenderISTabs(Project& project, const Json& analysis, sst::ui::plot::PlotViewState& viewState);
    std::uint64_t shownProject = 0;
    View view;
    std::map<pl::Id, double> areaDraft;
    std::uint64_t selectionSeen=UINT64_MAX;
    std::set<pl::Id> treeMeasurements;
    pl::Id detailsSample=0, moveTarget=0;
    std::string sampleName, sampleMaterial, sampleNotes;
    std::map<std::string,Json> previousPreviewOptions;
    std::string savedFormat;
    char formatName[129]="", sampleRule[256]="", treatmentRule[256]="", temperatureRule[256]="", frequencyRule[256]="", biasRule[256]="";
    char variableRules[2048]="[]", variableDelimiter[32]="_";
    bool useMetadataCandidates=false;
    std::set<std::string> previewPending;
    sst::ui::plot::PlotViewState fitView, bodePhaseView;
    Json isParameters=Json::array();
    pl::Id fitCircuit=0;
    ccui::CapLabSchematicEditor circuitEditor;
    pl::Id isPlotAnalysis=0, isPlotGraph=0;
    bool residualOverlay=true, circuitRecalc=false, circuitAutoReady=false, modelAutoReady=false;
    std::string circuitDocument;
    pl::Id selectedCircuit=0, circuitMeasurement=0;
    pl::Id selectedDefect=0;
    CapLabWorkflow *state;
    Ref<CommandRegistry> commands;
};
}
