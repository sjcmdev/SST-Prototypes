#pragma once
#include <string>
#include <vector>
#include "Core/Utils/Path.hpp"
#include "Core/Logging/Logger.hpp"

namespace SST {
enum class CrashProbeMode { Native, Terminate, Signal, Wait };
[[nodiscard]] Path DiagnosticsDirectory();
void InitializeLocalDiagnostics();
void FinishLocalDiagnostics();
void CaptureLocalCrash(void* exceptionPointers = nullptr) noexcept;
[[nodiscard]] std::vector<Path> PreviousInterruptedSessions();
bool AcknowledgeInterruptedSession(const Path& session, std::string& error);
[[nodiscard]] std::string DiagnosticPreview(const std::vector<LogEntry>& entries);
bool ExportDiagnostics(const Path& destination, const std::string& preview, std::string& error);
// Private same-executable helper: the crash handler starts this before touching logger locks.
int RunMinidumpHelper(int argc, char** argv);
[[noreturn]] void RunCrashProbe(CrashProbeMode mode);
}
