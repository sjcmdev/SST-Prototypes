#pragma once
#include "Presentation/Panels/EditorWindow.hpp"
#include "CapLabCommands.hpp"
#include "SchematicEditor.h"
namespace SST::Prototypes::CapLab {
class CapLabPrototypePanel final : public EditorWindow {
public:
    CapLabPrototypePanel(CapLabWorkflow *state, Ref<CommandRegistry> commands)
        : EditorWindow("caplab.workspace", "CapLab", true), state(state), commands(std::move(commands)) {}
    void RenderContent() override;
    void RenderAnalysis();
    void RenderAS();
    void RenderIS();
private:
    sst::ui::plot::PlotViewState fitView;
    Json isParameters=Json::array();
    pl::Id fitCircuit=0;
    ccui::SchematicEditor circuitEditor;
    pl::Id selectedCircuit=0, circuitMeasurement=0;
    pl::Id selectedDefect=0;
    Json pendingCandidate;
    CapLabWorkflow *state;
    Ref<CommandRegistry> commands;
};
}
