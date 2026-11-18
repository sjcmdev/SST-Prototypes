#pragma once

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Path.hpp"

namespace SST::ApplicationDetail
{
    // Internal shared command for CLI and the embedded sst_application module.
    void RegisterScreenshotCommand(const Ref<CommandRegistry> &commands, const Ref<JobSystem> &jobs);
    void RegisterApplicationPythonModule();
    Result<CommandOutcome> OpenStartupFile(CommandRegistry &commands, const Path &path);
}
