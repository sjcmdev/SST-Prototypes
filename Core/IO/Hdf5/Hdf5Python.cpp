#include "Core/dspch.hpp"
#include "Hdf5Session.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
namespace {
using Json = nlohmann::json;
using namespace SST;
using namespace sst::hdf5;
const char* errorCode(Hdf5Error code) {
    switch (code) {
    case Hdf5Error::InvalidRequest: return "hdf5.invalid_request";
    case Hdf5Error::IO: return "hdf5.io";
    case Hdf5Error::Format: return "hdf5.format";
    case Hdf5Error::Cancelled: return "hdf5.cancelled";
    case Hdf5Error::Busy: return "hdf5.busy";
    case Hdf5Error::JobRejected: return "hdf5.job_rejected";
    case Hdf5Error::JobFailed: return "hdf5.job_failed";
    default: return "hdf5.internal";
    }
}
Json failure(Hdf5Error code, const std::string& message) {
    return {{"ok", false}, {"error", {{"code", errorCode(code)}, {"message", message}}}};
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
Hdf5Input input(const Json& request) {
    Hdf5Input out;
    const auto operation = request.at("operation").get<std::string>();
    if (operation != "save" && operation != "read") throw Error(ErrorCode::InvalidRequest, "Expected save or read");
    out.operation = operation == "save" ? Hdf5Operation::Save : Hdf5Operation::Read;
    out.file = Path(std::filesystem::u8path(request.at("file_path").get<std::string>())).Native();
    if (out.operation == Hdf5Operation::Save) {
        const auto& items = request.at("datasets");
        if (!items.is_array()) throw Error(ErrorCode::InvalidRequest, "datasets must be an array");
        out.datasets.reserve(items.size());
        for (const auto& item : items) {
            Hdf5Dataset data;
            const auto& values = item.at("values");
            if (!values.is_array()) throw Error(ErrorCode::InvalidRequest, "values must be a flat numeric array");
            data.values.reserve(values.size());
            for (const auto& value : values) {
                if (!value.is_number() || !std::isfinite(value.get<double>())) throw Error(ErrorCode::InvalidRequest, "JSON values must be finite numbers");
                data.values.push_back(value.get<double>());
            }
            const auto compression = item.value("compression", Json(4));
            if (!compression.is_number_integer() || compression.get<std::int64_t>() < 0 || compression.get<std::int64_t>() > 9)
                throw Error(ErrorCode::InvalidRequest, "compression must be an integer 0..9");
            data.id = item.at("dataset_id").get<std::string>();
            data.shape = shape(item.at("shape"));
            data.units = item.at("units").get<std::string>();
            data.attributes = item.value("attributes", Attributes{});
            data.chunks = shape(item.value("chunks", Json::array()));
            data.compression = compression.get<unsigned>();
            out.datasets.push_back(std::move(data));
        }
        out.attributes = request.value("attributes", Attributes{});
        if (request.contains("provenance")) out.attributes["provenance_json"] = request.at("provenance").dump();
    } else {
        out.datasetId = request.at("dataset_id").get<std::string>();
        out.offset = shape(request.value("offset", Json::array()));
        out.count = shape(request.value("count", Json::array()));
    }
    return out;
}
Json response(const Hdf5Result& result) {
    if (result.error) return failure(*result.error, result.message);
    if (!result.data) return {{"ok",true},{"dataset_count",result.datasetCount}};
    const auto& data = *result.data;
    for (double value : data.values) if (!std::isfinite(value))
        return failure(Hdf5Error::Format, "Non-finite doubles cannot be represented as JSON numbers");
    return {{"ok",true},{"dataset_id",result.datasetId},{"shape",data.shape},{"count",data.count},
        {"values",data.values},{"units",data.units},{"attributes",data.attributes},{"file_attributes",data.fileAttributes}};
}
}
NB_MODULE(sst_hdf5, module) {
    nanobind::class_<SST::Hdf5Session>(module, "Session")
        .def(nanobind::init<>())
        .def("start", [](SST::Hdf5Session& session, const std::string& request) {
            try {
                const auto parsed = Json::parse(request);
                if (session.Poll().running) return failure(Hdf5Error::Busy,"A storage operation is already running").dump();
                const auto operation = parsed.at("operation").get<std::string>();
                if (operation != "save" && operation != "read") return failure(Hdf5Error::InvalidRequest,"Expected save or read").dump();
                Hdf5Input value;
                value.operation = operation == "save" ? Hdf5Operation::Save : Hdf5Operation::Read;
                // Preserve the asynchronous failure contract of accepted storage requests.
                try { value = input(parsed); }
                catch (const Error& e) { value.validationError = Hdf5ValidationError{e.what()}; }
                catch (const Json::exception& e) { value.validationError = Hdf5ValidationError{e.what()}; }
                const auto result = session.Start(std::move(value));
                return (result.error ? failure(*result.error,result.message) : Json{{"ok",true},{"job_id",result.jobId}}).dump();
            } catch (const Error& e) { return failure(Hdf5Error::InvalidRequest,e.what()).dump(); }
              catch (const Json::exception& e) { return failure(Hdf5Error::InvalidRequest,e.what()).dump(); }
        }, nanobind::arg("request_json"))
        .def("poll", [](SST::Hdf5Session& session) {
            const auto result = session.Poll();
            return Json{{"running",result.running},{"job_id",result.jobId},
                {"response",result.response ? response(*result.response) : Json(nullptr)}}.dump();
        })
        .def("cancel", [](SST::Hdf5Session& session) {
            const auto result = session.Cancel();
            return Json{{"ok",result.ok},{"job_id",result.jobId}}.dump();
        });
}
#endif
namespace SST {
void RegisterHdf5PythonModule() {
#if DS_PYTHON_CAPI_AVAILABLE
    static bool registered = false;
    if (registered) return;
    if (Py_IsInitialized() || PyImport_AppendInittab("sst_hdf5", PyInit_sst_hdf5) != 0)
        throw std::runtime_error("Register sst_hdf5 before starting embedded Python");
    registered = true;
#endif
}
}
