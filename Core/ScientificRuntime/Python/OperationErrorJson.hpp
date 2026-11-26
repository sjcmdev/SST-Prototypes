#pragma once
// Python binding edge only: C++ application code consumes StructuredError/JobSnapshot.
#include "Core/JobSystem/JobOutcome.hpp"
#include <nlohmann/json.hpp>

namespace SST::PythonBoundary {
inline nlohmann::json Error(const StructuredError& error) {
    return {{"code",error.code},{"canonical_code",error.code},{"error_contract_version",1},{"category",ErrorCategoryName(error.category)},
        {"message",error.userMessage},{"details",error.technicalDetails},
        {"suggestion",error.suggestion},{"retryable",error.retryable},
        {"field_path",error.fieldPath},{"job_id",error.jobId},{"run_id",error.runId}};
}
inline nlohmann::json LegacyJobError(const std::optional<JobSnapshot>& job,std::string code,const std::string& message) {
    auto error=job ? SST::JobError(*job) : MakeOperationError(ErrorCategory::Internal,message);
    auto response=Error(error);
    response["code"]=error.category==ErrorCategory::Cancelled ? "cancelled" : std::move(code);
    response["message"]=message;
    return response;
}
inline nlohmann::json Failure(const StructuredError& error) {
    const auto category = error.category;
    const char* status = category == ErrorCategory::Cancelled ? "cancelled" : category == ErrorCategory::TimedOut ? "timed_out" : category == ErrorCategory::Partial ? "partial" : "failed";
    return {{"ok",false},{"status",status},{"error",Error(error)},{"exit_contract_version",CliExitContractVersion}};
}
inline nlohmann::json Failure(const JobSnapshot& job) { return Failure(JobError(job)); }
}
