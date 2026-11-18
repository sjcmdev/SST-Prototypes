#pragma once
#include "Core/Commands/CommandRegistry.hpp"
#include "IO/PrototypeWorkflow.hpp"
namespace SST::Prototypes::CapLab {
void RegisterCommands(Ref<CommandRegistry>, Ref<EventBus>, CapLabWorkflow &);
}
