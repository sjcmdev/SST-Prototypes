#pragma once
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"

namespace SST {
// Contents are the reviewed file-edge preview, never a serialized in-process request.
struct DiagnosticExport { Path destination; std::string preview; };
void RegisterDiagnosticsCommand(const Ref<CommandRegistry>& commands, const Ref<JobSystem>& jobs);
[[nodiscard]] Result<CommandOutcome> ExecuteDiagnosticsExport(CommandRegistry& commands, DiagnosticExport request);
[[nodiscard]] Result<CommandOutcome> ExportDiagnosticsHeadless(DiagnosticExport request);
}
