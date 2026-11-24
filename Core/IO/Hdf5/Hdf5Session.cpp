#include "Core/dspch.hpp"
#include "Hdf5Session.hpp"
#include "Core/JobSystem/JobContext.hpp"

namespace SST {
namespace {
using namespace sst::hdf5;
std::shared_ptr<const Hdf5Result> failure(Hdf5Error code, const std::string& message) {
    return std::make_shared<const Hdf5Result>(Hdf5Result{code, message});
}
Hdf5Error errorCode(ErrorCode code) {
    switch (code) {
    case ErrorCode::InvalidRequest: return Hdf5Error::InvalidRequest;
    case ErrorCode::IO: return Hdf5Error::IO;
    case ErrorCode::Format: return Hdf5Error::Format;
    case ErrorCode::Cancelled: return Hdf5Error::Cancelled;
    }
    return Hdf5Error::Internal;
}
const char* operationName(Hdf5Operation operation) { return operation == Hdf5Operation::Save ? "save" : "read"; }
Hdf5Result run(const Hdf5Input& request, JobContext& context) {
    Hdf5Result result;
    if (request.operation == Hdf5Operation::Save) {
        std::vector<Dataset> datasets;
        datasets.reserve(request.datasets.size());
        for (const auto& item : request.datasets) {
            context.ThrowIfCancellationRequested();
            datasets.push_back({item.id, item.shape, item.values, item.units, item.attributes, item.chunks, item.compression});
        }
        context.SetStage("Save HDF5 snapshot");
        save(request.file, datasets, request.attributes, [&] { context.ThrowIfCancellationRequested(); });
        result.datasetCount = datasets.size();
    } else {
        context.SetStage("Read HDF5 hyperslab");
        result.datasetId = request.datasetId;
        result.data = read(request.file, request.datasetId, request.offset, request.count);
    }
    return result;
}
class Hdf5Job final : public IJob {
public:
    explicit Hdf5Job(Ref<Hdf5Request> request) : request(std::move(request)) {}
    std::string GetName() const override { return "HDF5 storage"; }
    std::string GetType() const override { return "storage.hdf5"; }
    void Execute(JobContext& context) override {
        auto input = std::move(request->input);
        try {
            context.ThrowIfCancellationRequested();
            if (input.validationError) throw Error(ErrorCode::InvalidRequest, input.validationError->message);
            request->response.set_value(std::make_shared<const Hdf5Result>(run(input, context)));
        } catch (const JobCancelledException& e) {
            request->response.set_value(failure(Hdf5Error::Cancelled, e.what())); throw;
        } catch (const Error& e) {
            request->response.set_value(failure(errorCode(e.code), e.what())); throw;
        } catch (const std::exception& e) {
            request->response.set_value(failure(Hdf5Error::Internal, e.what())); throw;
        }
    }
private:
    Ref<Hdf5Request> request;
};
class Hdf5Command final : public ICommand {
public:
    Hdf5Command(Ref<JobSystem> jobs, Hdf5Operation operation) : jobs(std::move(jobs)), operation(operation) {}
    Result<void> Execute(CommandContext& context) override {
        auto request = context.GetCopy<Ref<Hdf5Request>>("hdf5.request");
        if (!request) return request.Error();
        if (!request.Value()) return StructuredError{ErrorCategory::Validation, Severity::Error,
            "Missing HDF5 request.", {}, {}, "Hdf5", "hdf5.invalid_request"};
        request.Value()->input.operation = operation;
        request.Value()->jobId = jobs->Submit(CreateRef<Hdf5Job>(request.Value()));
        if (!request.Value()->jobId) return StructuredError{ErrorCategory::Job, Severity::Error,
            "HDF5 job submission failed.", {}, {}, "Hdf5", "hdf5.job_rejected"};
        return {};
    }
    std::string Description() const override { return std::string("HDF5 ") + operationName(operation); }
private:
    Ref<JobSystem> jobs;
    Hdf5Operation operation;
};
}
Result<void> RegisterHdf5Commands(CommandRegistry& registry, Ref<JobSystem> jobs) {
    if (!jobs) return StructuredError{ErrorCategory::Job, Severity::Error,
        "HDF5 requires a Core JobSystem.", {}, {}, "Hdf5", "hdf5.no_job_system"};
    for (auto operation : {Hdf5Operation::Save, Hdf5Operation::Read}) {
        const std::string name = operationName(operation);
        auto registered = registry.Register({CommandID{"storage.hdf5." + name}, "HDF5 " + name,
            "Storage", "N-D double datasets with explicit units", {}, CommandFlags::AsyncCapable},
            [jobs, operation](CommandContext&) { return CreateUnique<Hdf5Command>(jobs, operation); });
        if (!registered) return registered;
    }
    return {};
}
Hdf5Session::Hdf5Session(Ref<JobSystem> jobs)
    : m_Jobs(jobs ? std::move(jobs) : CreateRef<JobSystem>(nullptr, 1)),
      m_Registry(CreateRef<CommandRegistry>()), m_Commands(m_Registry, {}) {
    auto registered = RegisterHdf5Commands(*m_Registry, m_Jobs);
    if (!registered) throw std::runtime_error(registered.Error().userMessage);
}
Hdf5StartResult Hdf5Session::Start(Hdf5Input input) {
    if (m_JobId && Poll().running) return {0, Hdf5Error::Busy, "A storage operation is already running"};
    if (input.operation != Hdf5Operation::Save && input.operation != Hdf5Operation::Read)
        return {0, Hdf5Error::InvalidRequest, "Expected save or read"};
    const auto operation = input.operation;
    auto request = CreateRef<Hdf5Request>();
    request->input = std::move(input);
    auto response = request->response.get_future();
    CommandContext context;
    context.Set("hdf5.request", request);
    auto executed = m_Commands.Execute(CommandID{std::string("storage.hdf5.") + operationName(operation)}, std::move(context));
    if (!executed) return {0, Hdf5Error::JobRejected, executed.Error().userMessage};
    m_Response = std::move(response);
    m_JobId = request->jobId;
    m_LastResponse.reset();
    return {m_JobId};
}
Hdf5PollResult Hdf5Session::Poll() {
    auto snapshot = m_Jobs->GetJob(m_JobId);
    const bool running = snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running);
    if (m_Response.valid() && m_Response.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { m_LastResponse = m_Response.get(); }
        catch (const std::future_error& e) {
            snapshot = m_Jobs->GetJob(m_JobId); // Cancellation can retire the request after the first snapshot.
            m_LastResponse = failure(snapshot && snapshot->status == JobStatus::Cancelled ? Hdf5Error::Cancelled : Hdf5Error::JobFailed, e.what());
        }
    }
    if (!running && snapshot && !m_LastResponse) {
        // A queued cancellation can finish without ever entering Hdf5Job::Execute.
        m_LastResponse = failure(snapshot->status == JobStatus::Cancelled ? Hdf5Error::Cancelled : Hdf5Error::JobFailed, snapshot->errorMessage);
    }
    return {running, m_JobId, m_LastResponse};
}
Hdf5CancelResult Hdf5Session::Cancel() { return {m_JobId != 0 && m_Jobs->RequestCancel(m_JobId), m_JobId}; }
}
