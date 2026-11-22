#include "IO/JFMPlotExport.hpp"
#include "Core/ScientificRuntime/PlotExport/PlotExport.hpp"
#include "IO/JFMPythonBindings.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <cmath>
#ifdef _WIN32
#include <shlobj.h>
#undef GetJob // Winspool macro conflicts with Core job queries.
#endif

namespace SST::Prototypes::JFM
{
    namespace pl = sst::plotlab;
    namespace {
        WeakRef<CommandRegistry> appCommands;
        WeakRef<JobSystem> appJobs;
        Ref<JobSystem> Jobs() {
            if (auto jobs = appJobs.lock()) return jobs;
            static auto headless = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
            return headless;
        }
        Ref<CommandRegistry> Commands() {
            if (auto commands = appCommands.lock()) return commands;
            static auto headless = CreateRef<CommandRegistry>();
            return headless;
        }
        class ExportJob final : public IJob {
        public:
            std::shared_ptr<const pl::GraphSnapshot> snapshot;
            std::string path, format;
            double dpi;
            JFMPlotSize size;
            std::string GetName() const override { return "Export JFM figure"; }
            std::string GetType() const override { return "jfm.plot.export"; }
            void Execute(JobContext &context) override {
                context.ThrowIfCancellationRequested();
                // The shared adapter acquires the GIL inside this job.
                const auto request = MakeJFMPlotExportRequest(snapshot, std::filesystem::u8path(path), format, dpi, size);
                ExportPlotsJson(context, {&request, 1});
                context.SetProgress(1, 1);
            }
        };
        class ExportCommand final : public ICommand {
        public:
            JobSystem &jobs;
            Ref<ExportJob> job;
            Ref<JobId> id;
            ExportCommand(JobSystem &j, Ref<ExportJob> e, Ref<JobId> i) : jobs(j), job(std::move(e)), id(i) {}
            Result<void> Execute(CommandContext &) override { *id = jobs.Submit(job); return {}; }
            std::string Description() const override { return "Export JFM figure"; }
        };
    }
    Path JFMPlotExportDirectory()
    {
#ifdef _WIN32
        PWSTR documents = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
            const Path result = Path::FromResolved(documents);
            CoTaskMemFree(documents);
            return result;
        }
#endif
        return Path::FromResolved(FileSystem::TempDirectoryPath());
    }
    void ConfigureJFMPlotServices(WeakRef<CommandRegistry> commands, WeakRef<JobSystem> jobs)
    { appCommands = std::move(commands); appJobs = std::move(jobs); }
    Result<JobId> ExportJFMPlot(std::shared_ptr<const pl::GraphSnapshot> snapshot, const std::string &path, const std::string &format, double dpi, std::optional<JFMPlotSize> size)
    {
        try {
            if (!snapshot) throw std::invalid_argument("Missing graph snapshot");
            const auto publication = size.value_or(JFMPlotSize{snapshot->widthMm, snapshot->heightMm});
            if ((format != "svg" && format != "pdf" && format != "png") || path.empty()
                || !std::isfinite(dpi) || dpi <= 0 || !std::isfinite(publication.widthMm) || publication.widthMm <= 0
                || !std::isfinite(publication.heightMm) || publication.heightMm <= 0)
                throw std::invalid_argument("Expected SVG/PDF/PNG, a destination, positive width_mm, height_mm and dpi");
            auto jobs = Jobs(); auto commands = Commands();
            auto id = CreateRef<JobId>(0);
            auto job = CreateRef<ExportJob>(); job->snapshot = std::move(snapshot); job->path = path; job->format = format; job->dpi = dpi; job->size = publication;
            const CommandID commandId{"jfm.plot.export"};
            if (!commands->HasCommand(commandId)) {
                auto registered = commands->Register({commandId, "Export JFM figure", "JFM"},
                    [](CommandContext &context) {
                        if (!context.TryGet<JobSystem *>("jobs") || !context.TryGet<Ref<ExportJob>>("job") || !context.TryGet<Ref<JobId>>("job_id"))
                            throw std::invalid_argument("Missing JFM plot export context");
                        return CreateUnique<ExportCommand>(**context.TryGet<JobSystem *>("jobs"),
                            *context.TryGet<Ref<ExportJob>>("job"), *context.TryGet<Ref<JobId>>("job_id"));
                    });
                if (!registered) throw std::runtime_error(registered.Error().technicalDetails);
            }
            CommandContext context;
            context.Set("jobs", jobs.get()); context.Set("job", job); context.Set("job_id", id);
            auto executed = commands->Execute(commandId, std::move(context));
            if (!executed) throw std::runtime_error(executed.Error().technicalDetails);
            if (!*id) throw std::runtime_error("Export job submission failed");

            return *id;
        } catch (const std::invalid_argument &e) {
            return StructuredError{ErrorCategory::Validation, Severity::Error, e.what(), e.what(), {}, "JFMPlotExport", "jfm.plot.validation"};
        } catch (const std::exception &e) {
            return StructuredError{ErrorCategory::Internal, Severity::Error, e.what(), e.what(), {}, "JFMPlotExport", "jfm.plot.export_failed"};
        }
    }
    Result<JobSnapshot> JFMPlotExportStatus(JobId id)
    {
        auto job = Jobs()->GetJob(id);
        if (!job) return StructuredError{ErrorCategory::Validation, Severity::Error, "Unknown job_id", "Unknown job_id", {}, "JFMPlotExport", "jfm.plot.validation"};
        return std::move(*job);
    }
}
