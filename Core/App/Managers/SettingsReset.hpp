#pragma once
#include "Core/App/ApplicationState.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"

namespace SST {
enum class SettingsSection { System, Interface, Viewport };
// Produces a validated draft. The caller retains preview, undo and persistence policy.
Result<ApplicationConfig> ResetSettingsSection(CommandRegistry&,JobSystem&,ApplicationConfig,SettingsSection);
}
