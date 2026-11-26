#pragma once
#include "Core/Diagnostics/StructuredError.hpp"
#include <stdexcept>
#include <string_view>

namespace SST {
// v1: all adapters use these terminal categories; only a complete success exits 0.
inline constexpr unsigned CliExitContractVersion = 1;
inline const char* ErrorCategoryName(ErrorCategory category) noexcept {
    switch (category) {
    case ErrorCategory::Config: case ErrorCategory::Configuration: case ErrorCategory::Validation: return "invalid_input";
    case ErrorCategory::UnsupportedVersion: return "unsupported_version";
    case ErrorCategory::Numerical: return "numerical_failure";
    case ErrorCategory::Partial: return "partial";
    case ErrorCategory::IO: return "io";
    case ErrorCategory::Capability: return "unavailable";
    case ErrorCategory::ResourceLimit: return "resource_limit";
    case ErrorCategory::Cancelled: return "cancelled";
    case ErrorCategory::TimedOut: return "timed_out";
    case ErrorCategory::OutOfMemory: return "out_of_memory";
    default: return "internal";
    }
}
inline int CliExitCode(const StructuredError& error) noexcept {
    switch (error.category) {
    case ErrorCategory::Config: case ErrorCategory::Configuration: case ErrorCategory::Validation: case ErrorCategory::UnsupportedVersion: return 2;
    case ErrorCategory::Numerical: case ErrorCategory::Partial: return 3;
    case ErrorCategory::IO: return 4;
    case ErrorCategory::Cancelled: return 130;
    case ErrorCategory::TimedOut: return 124;
    default: return 5;
    }
}
inline int CliExitCode(const VoidResult& result) noexcept { return result ? 0 : CliExitCode(result.Error()); }
inline std::string OperationErrorSummary(const StructuredError& error) {
    return error.userMessage+(error.technicalDetails.empty() || error.technicalDetails==error.userMessage ? std::string{} : " "+error.technicalDetails);
}
inline StructuredError MakeOperationError(ErrorCategory category, std::string details = {}, std::string source = {}, std::string code = {}) {
    StructuredError error;
    error.category = category;
    error.code = code.empty() ? "sst.operation." + std::string(ErrorCategoryName(category)) : std::move(code);
    error.source = std::move(source);
    error.technicalDetails = std::move(details);
    switch (category) {
    case ErrorCategory::Cancelled:
        error.severity = Severity::Info; error.userMessage = "Operation cancelled.";
        error.suggestion = "Completed results are retained. Start a new operation to continue."; break;
    case ErrorCategory::TimedOut:
        error.userMessage = "Operation exceeded its time limit.";
        error.suggestion = "Reduce the workload or increase the time budget, then retry."; error.retryable = true; break;
    case ErrorCategory::Partial:
        error.severity = Severity::Warning; error.userMessage = "Only part of the operation completed.";
        error.suggestion = "Inspect per-item errors; retained results remain usable. Retry the failed items."; break;
    case ErrorCategory::Numerical:
        error.userMessage = "The calculation did not converge.";
        error.suggestion = "Check input units, bounds and initial values before retrying."; break;
    case ErrorCategory::IO:
        error.userMessage = "The file operation failed.";
        error.suggestion = "Check the path, permissions and free disk space; keep the original file until saving succeeds."; break;
    case ErrorCategory::Validation: case ErrorCategory::Config: case ErrorCategory::Configuration:
        error.userMessage = "The supplied settings or input are invalid.";
        error.suggestion = "Correct the field identified in the details and retry."; break;
    case ErrorCategory::UnsupportedVersion:
        error.userMessage = "This file requires a different schema version.";
        error.suggestion = "Use the matching SST version or restore a compatible backup."; break;
    case ErrorCategory::Capability:
        error.userMessage = "The requested runtime capability is unavailable.";
        error.suggestion = "Enable the required runtime or select an available backend."; break;
    case ErrorCategory::ResourceLimit: case ErrorCategory::OutOfMemory:
        error.userMessage = "The operation exceeded available resources.";
        error.suggestion = "Reduce the workload or increase its resource budget before retrying."; break;
    default:
        error.userMessage = "The operation failed unexpectedly.";
        error.suggestion = "Review the job details and export local diagnostics if the failure repeats."; break;
    }
    return error;
}
class OperationException : public std::runtime_error {
public:
    explicit OperationException(StructuredError error) : std::runtime_error(error.userMessage), error(std::move(error)) {}
    StructuredError error;
};
}
