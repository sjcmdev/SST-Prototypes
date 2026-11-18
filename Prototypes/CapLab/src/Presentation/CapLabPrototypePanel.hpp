#pragma once
#include "Presentation/Panels/IPanel.hpp"
#include "CapLabCommands.hpp"
namespace SST::Prototypes::CapLab {
class CapLabPrototypePanel final : public IPanel {
public:
    CapLabPrototypePanel(CapLabWorkflow *state, Ref<CommandRegistry> commands)
        : IPanel("CapLab: laboratory AS", true), state(state), commands(std::move(commands)) {}
    Ref<IPanel> Clone() const override { return CreateRef<CapLabPrototypePanel>(*this); }
    void Render() override;
private:
    CapLabWorkflow *state;
    Ref<CommandRegistry> commands;
};
}
