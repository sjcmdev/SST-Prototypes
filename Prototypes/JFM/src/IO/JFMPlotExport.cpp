#include "IO/JFMPlotExport.hpp"
#include "Core/PlotExport/PlotExport.hpp"
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
    namespace host = SST::PlotExport;
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
            pl::GraphSnapshot snapshot;
            std::string path, format;
            double dpi;
            std::string GetName() const override { return "Export JFM figure"; }
            std::string GetType() const override { return "jfm.plot.export"; }
            void Execute(JobContext &context) override {
                context.ThrowIfCancellationRequested();
                // ExportGraph owns the GIL scope, matching PlotLab's existing exporter job.
                host::ExportGraph(snapshot, std::filesystem::u8path(path), format, dpi, SST_PLOT_EXPORT_PYTHON_ROOT);
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
    nlohmann::json ExportJFMPlot(const pl::GraphSnapshot &snapshot, const std::string &path, const std::string &format, double dpi)
    {
        try {
            if ((format != "svg" && format != "pdf" && format != "png") || path.empty()
                || !std::isfinite(dpi) || dpi <= 0 || !std::isfinite(snapshot.widthMm) || snapshot.widthMm <= 0
                || !std::isfinite(snapshot.heightMm) || snapshot.heightMm <= 0)
                throw std::invalid_argument("Expected SVG/PDF/PNG, a destination, positive width_mm, height_mm and dpi");
            auto jobs = Jobs(); auto commands = Commands();
            auto id = CreateRef<JobId>(0);
            auto job = CreateRef<ExportJob>(); job->snapshot = snapshot; job->path = path; job->format = format; job->dpi = dpi;
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

            return {{"ok", true}, {"job_id", *id}, {"status", "queued"}, {"project_id", snapshot.project}, {"graph_id", snapshot.graph}, {"path", path}};
        } catch (const std::invalid_argument &e) {
            return {{"ok", false}, {"error", {{"code", "jfm.plot.validation"}, {"message", e.what()}}}};
        } catch (const std::exception &e) {
            return {{"ok", false}, {"error", {{"code", "jfm.plot.export_failed"}, {"message", e.what()}}}};
        }
    }
    nlohmann::json RunJFMPlotRequest(const nlohmann::json &request)
    {
        try {
            if (request.at("operation") == "status") {
                const auto id = request.at("job_id").get<JobId>();
                const auto job = Jobs()->GetJob(id);
                if (!job) throw std::invalid_argument("Unknown job_id");
                static constexpr const char *names[] = {"queued", "running", "completed", "failed", "cancelled"};
                nlohmann::json result = {{"ok", true}, {"job_id", id}, {"status", names[static_cast<int>(job->status)]}};
                if (job->status == JobStatus::Failed || job->status == JobStatus::Cancelled) {
                    result["ok"] = false;
                    result["error"] = {{"code", job->status == JobStatus::Cancelled ? "jfm.plot.cancelled" : "jfm.plot.export_failed"},
                        {"message", job->errorMessage}};
                }
                return result;
            }
            auto project = pl::Project::fromJson(request.at("project").dump());
            sst::ui::plot::PlotViewState view = request.contains("view") ? pl::viewStateFromJson(request.at("view").dump()) : sst::ui::plot::PlotViewState{};
            const auto &r = request.at("ranges");
            if (r.size() != 4) throw std::invalid_argument("ranges requires four values");
            view.viewport = sst::ui::plot::Bounds{r.at(0), r.at(1), r.at(2), r.at(3)};
            for (const auto &[key, value] : request.value("visibility", nlohmann::json::object()).items()) view.visibility[key] = value.get<bool>();
            auto ticks = [&](const char *key, auto &out) {
                for (const auto &tick : request.value(key, nlohmann::json::array()))
                    out.push_back({tick.at("value"), tick.value("label", ""), tick.value("major", true)});
            };
            ticks("x_ticks", view.xTicks); ticks("y_ticks", view.yTicks);
            auto snapshot = *pl::snapshotGraph(project, request.at("graph_id"), view);
            snapshot.widthMm = request.value("width_mm", snapshot.widthMm);
            snapshot.heightMm = request.value("height_mm", snapshot.heightMm);
            if (!std::isfinite(snapshot.widthMm) || snapshot.widthMm <= 0 || !std::isfinite(snapshot.heightMm) || snapshot.heightMm <= 0)
                throw std::invalid_argument("width_mm and height_mm must be positive");
            const auto operation = request.at("operation").get<std::string>();
            if (operation == "snapshot") return {{"ok", true}, {"snapshot", nlohmann::json::parse(host::SnapshotJson(snapshot))}};
            if (operation != "export") throw std::invalid_argument("Unknown plot operation");
            return ExportJFMPlot(snapshot, request.at("path"), request.at("format"), request.value("dpi", 300.0));
        } catch (const std::exception &e) {
            return {{"ok", false}, {"error", {{"code", "jfm.plot.validation"}, {"message", e.what()}}}};
        }
    }
}
