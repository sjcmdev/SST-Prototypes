#pragma once

#include <future>
#include <optional>
#include "Core/Commands/CommandService.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "sst/hdf5/storage.hpp"

namespace SST {
enum class Hdf5Operation { Save, Read };
enum class Hdf5Error { InvalidRequest, IO, Format, Cancelled, Busy, JobRejected, JobFailed, Internal };
struct Hdf5ValidationError { std::string message; };
struct Hdf5Dataset {
    std::string id;
    sst::hdf5::Shape shape;
    std::vector<double> values;
    std::string units;
    sst::hdf5::Attributes attributes;
    sst::hdf5::Shape chunks;
    unsigned compression = 4;
};
struct Hdf5Input {
    Hdf5Operation operation = Hdf5Operation::Read;
    std::filesystem::path file;
    std::vector<Hdf5Dataset> datasets;
    sst::hdf5::Attributes attributes;
    std::string datasetId;
    sst::hdf5::Shape offset, count;
    std::optional<Hdf5ValidationError> validationError;
};
struct Hdf5Result {
    std::optional<Hdf5Error> error;
    std::string message;
    std::size_t datasetCount = 0;
    std::string datasetId;
    std::optional<sst::hdf5::ReadResult> data;
};
// GUI and embedded Python use these same stable command IDs and job implementation.
struct Hdf5Request {
    Hdf5Input input;
    std::promise<std::shared_ptr<const Hdf5Result>> response;
    JobId jobId = 0;
};
struct Hdf5StartResult { JobId jobId = 0; std::optional<Hdf5Error> error; std::string message; };
struct Hdf5PollResult { bool running = false; JobId jobId = 0; std::shared_ptr<const Hdf5Result> response; };
struct Hdf5CancelResult { bool ok = false; JobId jobId = 0; };
Result<void> RegisterHdf5Commands(CommandRegistry& registry, Ref<JobSystem> jobs);
class Hdf5Session {
public:
    explicit Hdf5Session(Ref<JobSystem> jobs = {});
    Hdf5StartResult Start(Hdf5Input request);
    Hdf5PollResult Poll();
    Hdf5CancelResult Cancel();
private:
    Ref<JobSystem> m_Jobs;
    Ref<CommandRegistry> m_Registry;
    CommandService m_Commands;
    std::future<std::shared_ptr<const Hdf5Result>> m_Response;
    std::shared_ptr<const Hdf5Result> m_LastResponse;
    JobId m_JobId = 0;
};
void RegisterHdf5PythonModule();
}
