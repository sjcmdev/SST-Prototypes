#pragma once
#include "Core/Commands/CommandRegistry.hpp"
#include <sst/plotlab/PlotEditing.hpp>
#include "IO/PrototypeWorkflow.hpp"
namespace SST::Prototypes::CapLab {
Result<void> ExecuteCapLabPlotEdit(CapLabWorkflow&, pl::Id layer, const sst::ui::plot::PlotEdit&);
void RegisterCommands(Ref<CommandRegistry>, Ref<EventBus>, CapLabWorkflow &);
}
