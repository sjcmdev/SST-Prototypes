#pragma once
#include "Core/Diagnostics/OperationOutcome.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"

namespace SST {
inline StructuredError JobError(const JobSnapshot& job) {
    auto error = job.error.value_or(MakeOperationError(job.status == JobStatus::Cancelled ? ErrorCategory::Cancelled : ErrorCategory::Internal, job.errorMessage, job.type));
    error.jobId = job.id;
    return error;
}
inline const char* JobOutcomeName(const JobSnapshot& job) noexcept {
    if (job.status == JobStatus::Completed) return "completed";
    if (job.status == JobStatus::Queued) return "queued";
    if (job.status == JobStatus::Running) return "running";
    if (job.status == JobStatus::Cancelled) return "cancelled";
    if (job.error && job.error->category == ErrorCategory::TimedOut) return "timed_out";
    if (job.error && job.error->category == ErrorCategory::Partial) return "partial";
    return "failed";
}
}
