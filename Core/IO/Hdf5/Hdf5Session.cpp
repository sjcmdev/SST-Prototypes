#include "Core/dspch.hpp"
#include "Hdf5Session.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Path.hpp"
#include "sst/hdf5/storage.hpp"
#include <cmath>

namespace SST {
namespace {
using Json = nlohmann::json;
using namespace sst::hdf5;
Json failure(const std::string& code, const std::string& message) {
    return {{"ok", false}, {"error", {{"code", code}, {"message", message}}}};
}
const char* errorCode(ErrorCode code) {
    switch (code) {
    case ErrorCode::InvalidRequest: return "hdf5.invalid_request";
    case ErrorCode::IO: return "hdf5.io";
    case ErrorCode::Format: return "hdf5.format";
    case ErrorCode::Cancelled: return "hdf5.cancelled";
    }
    return "hdf5.internal";
}
Shape shape(const Json& input) {
    if (!input.is_array()) throw Error(ErrorCode::InvalidRequest, "Shape must be an integer array");
    Shape result;
    for (const auto& n : input) {
        if (!n.is_number_unsigned() && (!n.is_number_integer() || n.get<std::int64_t>() < 0))
            throw Error(ErrorCode::InvalidRequest, "Shape/offset entries must be non-negative integers");
        result.push_back(n.get<std::uint64_t>());
    }
    return result;
}
Json run(const Json& request, JobContext& context) {
    const auto path = Path(std::filesystem::u8path(request.at("file_path").get<std::string>())).Native();
    if (request.at("operation") == "save") {
        const auto& items = request.at("datasets");
        if (!items.is_array()) throw Error(ErrorCode::InvalidRequest, "datasets must be an array");
        std::vector<std::vector<double>> buffers;
        std::vector<Dataset> datasets;
        buffers.reserve(items.size()); datasets.reserve(items.size());
        for (const auto& item : items) {
            context.ThrowIfCancellationRequested();
            const auto& values = item.at("values");
            if (!values.is_array()) throw Error(ErrorCode::InvalidRequest, "values must be a flat numeric array");
            auto& buffer = buffers.emplace_back();
            buffer.reserve(values.size());
            for (const auto& value : values) {
                if (!value.is_number() || !std::isfinite(value.get<double>()))
                    throw Error(ErrorCode::InvalidRequest, "JSON values must be finite numbers");
                buffer.push_back(value.get<double>());
            }
            auto compression = item.value("compression", Json(4));
            if (!compression.is_number_integer() || compression.get<std::int64_t>() < 0 || compression.get<std::int64_t>() > 9)
                throw Error(ErrorCode::InvalidRequest, "compression must be an integer 0..9");
            datasets.push_back({item.at("dataset_id").get<std::string>(), shape(item.at("shape")), buffer,
                item.at("units").get<std::string>(), item.value("attributes", Attributes{}),
                shape(item.value("chunks", Json::array())), compression.get<unsigned>()});
        }
        auto attrs = request.value("attributes", Attributes{});
        if (request.contains("provenance")) attrs["provenance_json"] = request.at("provenance").dump();
        context.SetStage("Save HDF5 snapshot");
        save(path, datasets, attrs, [&] { context.ThrowIfCancellationRequested(); });
        return {{"ok", true}, {"dataset_count", datasets.size()}};
    }
    context.SetStage("Read HDF5 hyperslab");
    auto result = read(path, request.at("dataset_id").get<std::string>(),
        shape(request.value("offset", Json::array())), shape(request.value("count", Json::array())));
    Json values = Json::array();
    for (auto value : result.values) {
        if (!std::isfinite(value)) throw Error(ErrorCode::Format, "Non-finite doubles cannot be represented as JSON numbers");
        values.push_back(value);
    }
    return {{"ok", true}, {"dataset_id", request.at("dataset_id")}, {"shape", result.shape},
        {"count", result.count}, {"values", std::move(values)}, {"units", result.units},
        {"attributes", result.attributes}, {"file_attributes", result.fileAttributes}};
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
            request->response.set_value(run(input, context));
        } catch (const JobCancelledException& e) {
            request->response.set_value(failure("hdf5.cancelled", e.what())); throw;
        } catch (const Error& e) {
            request->response.set_value(failure(errorCode(e.code), e.what())); throw;
        } catch (const Json::exception& e) {
            request->response.set_value(failure("hdf5.invalid_request", e.what())); throw;
        } catch (const std::exception& e) {
            request->response.set_value(failure("hdf5.internal", e.what())); throw;
        }
    }
private:
    Ref<Hdf5Request> request;
};
class Hdf5Command final : public ICommand {
public:
    Hdf5Command(Ref<JobSystem> jobs, std::string operation) : jobs(std::move(jobs)), operation(std::move(operation)) {}
    Result<void> Execute(CommandContext& context) override {
        auto request = context.GetCopy<Ref<Hdf5Request>>("hdf5.request");
        if (!request) return request.Error();
        if (!request.Value()) return StructuredError{ErrorCategory::Validation, Severity::Error,
            "Missing HDF5 request.", {}, {}, "Hdf5", "hdf5.invalid_request"};
        request.Value()->input["operation"] = operation;
        request.Value()->jobId = jobs->Submit(CreateRef<Hdf5Job>(request.Value()));
        if (!request.Value()->jobId) return StructuredError{ErrorCategory::Job, Severity::Error,
            "HDF5 job submission failed.", {}, {}, "Hdf5", "hdf5.job_rejected"};
        return {};
    }
    std::string Description() const override { return "HDF5 " + operation; }
private:
    Ref<JobSystem> jobs;
    std::string operation;
};
}
Result<void> RegisterHdf5Commands(CommandRegistry& registry, Ref<JobSystem> jobs) {
    if (!jobs) return StructuredError{ErrorCategory::Job, Severity::Error,
        "HDF5 requires a Core JobSystem.", {}, {}, "Hdf5", "hdf5.no_job_system"};
    for (std::string operation : {"save", "read"}) {
        auto registered = registry.Register({CommandID{"storage.hdf5." + operation}, "HDF5 " + operation,
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
Json Hdf5Session::Start(const Json& input) {
    if (m_JobId && Poll().at("running").get<bool>()) return failure("hdf5.busy", "A storage operation is already running");
    try {
        auto operation = input.at("operation").get<std::string>();
        if (operation != "save" && operation != "read") return failure("hdf5.invalid_request", "Expected save or read");
        auto request = CreateRef<Hdf5Request>();
        request->input = input;
        auto response = request->response.get_future();
        CommandContext context;
        context.Set("hdf5.request", request);
        auto executed = m_Commands.Execute(CommandID{"storage.hdf5." + operation}, std::move(context));
        if (!executed) return failure(executed.Error().code, executed.Error().userMessage);
        m_Response = std::move(response);
        m_JobId = request->jobId;
        m_LastResponse = nullptr;
        return {{"ok", true}, {"job_id", m_JobId}};
    } catch (const Json::exception& e) { return failure("hdf5.invalid_request", e.what()); }
}
Json Hdf5Session::Poll() {
    auto snapshot = m_Jobs->GetJob(m_JobId);
    const bool running = snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running);
    if (m_Response.valid() && m_Response.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        m_LastResponse = m_Response.get();
    if (!running && snapshot && m_LastResponse.is_null()) {
        // A queued cancellation can finish without ever entering Hdf5Job::Execute.
        m_LastResponse = failure(snapshot->status == JobStatus::Cancelled ? "hdf5.cancelled" : "hdf5.job_failed", snapshot->errorMessage);
    }
    return {{"running", running}, {"job_id", m_JobId}, {"response", m_LastResponse}};
}
Json Hdf5Session::Cancel() { return {{"ok", m_JobId != 0 && m_Jobs->RequestCancel(m_JobId)}, {"job_id", m_JobId}}; }
}
