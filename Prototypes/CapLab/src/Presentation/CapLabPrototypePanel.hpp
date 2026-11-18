#pragma once
#include "Presentation/Panels/EditorWindow.hpp"
#include "CapLabCommands.hpp"
namespace SST::Prototypes::CapLab {
class CapLabPrototypePanel final : public EditorWindow {
public:
    CapLabPrototypePanel(CapLabWorkflow *state, Ref<CommandRegistry> commands)
        : EditorWindow("caplab.workspace", "CapLab", true), state(state), commands(std::move(commands)) {}
    void RenderContent() override;
    void RenderAnalysis();
    void RenderAS();
private:
    pl::Id selectedDefect=0;
    Json pendingCandidate;
    CapLabWorkflow *state;
    Ref<CommandRegistry> commands;
};
}
