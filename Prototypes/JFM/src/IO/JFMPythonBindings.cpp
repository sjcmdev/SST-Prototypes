#include "Core/dspch.hpp"
#include "Core/ScientificRuntime/Python/OperationErrorJson.hpp"

#include "IO/JFMPythonBindings.hpp"

#include "Core/Logging/Logger.hpp"
#include "Domain/JFMTypes.hpp"
#include "IO/JFMPlotExport.hpp"
#include <sst/plotlab/SnapshotJson.hpp>
#include <cmath>
#include "IO/JFMFitJobs.hpp"
#include "Analysis/JFMFitEngine.hpp"
#include "Events/JFMEvents.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/ScientificRuntime/PlotExport/PlotExport.hpp"
#include "ScientificRuntime/Python/PythonApi.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"


namespace SST::Prototypes::JFM {
namespace {
WeakRef<CommandRegistry> fitCommands;
WeakRef<JobSystem> fitJobs;
Ref<JobSystem> FitJobs() {
    if (auto jobs=fitJobs.lock()) return jobs;
    static auto headless=CreateRef<JobSystem>(Ref<EventBus>{},1);
    return headless;
}
Ref<CommandRegistry> FitCommands() {
    if (auto commands=fitCommands.lock()) return commands;
    static auto headless=CreateRef<CommandRegistry>();
    return headless;
}
class SubmitFitCommand final : public ICommand {
    JobSystem &jobs;
    Ref<IJob> job;
    Ref<JobId> id;
public:
    SubmitFitCommand(JobSystem &j, Ref<IJob> work, Ref<JobId> result) : jobs(j), job(std::move(work)), id(std::move(result)) {}
    Result<void> Execute(CommandContext &) override { *id = jobs.Submit(job); return {}; }
    std::string Description() const override { return "Fit JFM characteristic"; }
};
}
void ConfigureJFMFitServices(WeakRef<CommandRegistry> commands, WeakRef<JobSystem> jobs) {
    fitCommands=std::move(commands); fitJobs=std::move(jobs);
}
nlohmann::json RunJFMFitRequest(const nlohmann::json &request) {
    using Json = nlohmann::json;
    try {
        const std::string method = request.value("method", "lm");
        if (method != "lm" && method != "nm") throw std::invalid_argument("method must be lm or nm");
        auto data = CreateRef<JFMCharacteristicData>();
        data->voltage = request.at("voltage_V").get<std::vector<double>>();
        data->current = request.at("current_A").get<std::vector<double>>();
        if (data->voltage.size() != data->current.size() || data->voltage.empty())
            throw std::invalid_argument("voltage_V and current_A require equal nonzero lengths");
        const double temperature = request.at("temperature_K");
        if (!std::isfinite(temperature) || temperature <= 0) throw std::invalid_argument("temperature_K must be positive");
        JFMCharacteristicRecord record;
        record.info.name = request.value("name", "Headless fit"); record.info.temperatureK = temperature; record.data = data;
        record.info.prefit = JFMPrefitState{};
        auto &pre = *record.info.prefit;
        pre.modelId = request.at("model_id"); pre.status = "completed";
        const auto &names = JFMModelParameterNames(pre.modelId);
        if (names.empty()) throw std::invalid_argument("Unknown model_id");
        for (auto name : names) {
            const auto key = std::string(name);
            double value = request.at("parameters").at(key);
            if (!std::isfinite(value)) throw std::invalid_argument("Non-finite parameter: "+key);
            pre.parameters[key] = {value, std::nullopt, "headless", {}};
        }
        auto limits = std::minmax_element(data->voltage.begin(), data->voltage.end());
        const auto range = request.value("fit_range_V", std::vector<double>{*limits.first, *limits.second});
        if (range.size() != 2) throw std::invalid_argument("fit_range_V requires two values");
        pre.fitRange = {0, data->voltage.size(), range[0], range[1], "headless"};
        JFMFitMonteCarloSettings settings;
        settings.objectiveMode = request.value("objective_mode", "auto");
        if (settings.objectiveMode != "auto" && settings.objectiveMode != "isc_shift" && settings.objectiveMode != "signed_sigma" && settings.objectiveMode != "log_legacy")
            throw std::invalid_argument("objective_mode must be auto, isc_shift, signed_sigma or log_legacy");
        settings.sigmaAbsolute = request.value("sigma_abs_A", 0.0);
        settings.objectiveSwitchK = request.value("objective_switch_k", 3.0);
        if (!std::isfinite(settings.sigmaAbsolute) || settings.sigmaAbsolute < 0 || !std::isfinite(settings.objectiveSwitchK) || settings.objectiveSwitchK <= 0)
            throw std::invalid_argument("sigma_abs_A must be nonnegative and objective_switch_k positive, both finite");
        settings.fitMethod = method; settings.preferGpu = request.value("backend", "cpu") == "cuda";
        if (request.value("backend", "cpu") != "cpu" && request.value("backend", "cpu") != "cuda")
            throw std::invalid_argument("backend must be cpu or cuda");
        settings.fitMaxIterations = settings.monteCarloMaxIterations = request.value("max_iterations", 2500);
        if (settings.fitMaxIterations < 1) throw std::invalid_argument("max_iterations must be positive");
        settings.monteCarloSamples = request.value("samples", 0);
        if (settings.monteCarloSamples < 0) throw std::invalid_argument("samples must be nonnegative");
        settings.monteCarloNoisePercent = request.value("noise_percent", 1.0);
        settings.monteCarloStartPerturbationPercent = request.value("start_perturbation_percent", 5.0);
        const auto seed = request.value("seed", Json(42));
        if (!seed.is_number_integer() || seed.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("seed must be an integer in [0, 4294967295]");
        settings.monteCarloSeed = seed.get<std::uint32_t>();
        if (!std::isfinite(settings.monteCarloNoisePercent) || settings.monteCarloNoisePercent < 0 || settings.monteCarloNoisePercent > 100
            || !std::isfinite(settings.monteCarloStartPerturbationPercent) || settings.monteCarloStartPerturbationPercent < 0 || settings.monteCarloStartPerturbationPercent > 1000)
            throw std::invalid_argument("noise_percent must be in [0, 100] and start_perturbation_percent in [0, 1000]");
        settings.autoCalibrateGpuSimplexIterations = false; settings.saveSamples = false;
        for (const auto &[name, bound] : request.value("bounds", Json::object()).items()) {
            if (std::find(names.begin(), names.end(), name) == names.end()) throw std::invalid_argument("Unknown bound parameter: "+name);
            const double lo = bound.at(0), hi = bound.at(1);
            if (!std::isfinite(lo) || !std::isfinite(hi) || lo >= hi) throw std::invalid_argument("Invalid bounds for "+name);
            settings.parameters[name] = {lo, hi, true};
        }
        const auto fixed = request.value("fixed_parameters", std::vector<std::string>{});
        for (const auto &name : fixed) {
            if (std::find(names.begin(), names.end(), name) == names.end()) throw std::invalid_argument("Unknown fixed parameter: "+name);
            if (!settings.parameters.contains(name)) {
                const double v = pre.parameters.at(name).value;
                settings.parameters[name] = {std::nextafter(v, -INFINITY), std::nextafter(v, INFINITY), false};
            } else settings.parameters[name].monteCarloFree = false;
        }
        auto store = CreateRef<JFMWorkspaceStore>(); const auto project = store->CreateProject("Headless fit");
        const auto ids = store->AddCharacteristicRecords(project, {record});
        const JFMCharacteristicRef ref{project, ids.at(0)};
        record = *store->FindCharacteristicRecord(ref);
        auto bus = CreateRef<EventBus>();
        Json result;
        auto objectiveJson = [](const JFMObjectiveSummary &o) -> Json {
            return {{"mode", o.mode}, {"shift_A", o.shift}, {"sigma_abs_A", o.sigmaAbsolute}, {"sigma_rel", o.sigmaRelative},
                {"switch_k", o.switchK}, {"linear_points", o.linearPoints}, {"log_points", o.logPoints}};
        };
        auto failed = [&](const auto &event) { result = {{"ok", false}, {"error", {{"code", "jfm.fit.failed"}, {"message", event.error}}}}; };
        auto fitDone = bus->Subscribe<FitCompleted>([&](const FitCompleted &event) {
            Json parameters = Json::object();
            for (const auto &[name, estimate] : event.state.parameters) parameters[name] = estimate.value;
            result = {{"ok", true}, {"parameters", parameters}, {"chi_squared", event.state.chiSquared},
                {"reduced_chi_squared", event.state.reducedChiSquared}, {"iterations", event.state.iterations},
                {"fit_method", event.state.fitMethod}, {"accepted", event.state.accepted}, {"converged", event.state.converged},
                {"stop_reason", event.state.stopReason}, {"flat_directions", event.state.flatDirections},
                {"lm_status", event.state.lmStatus}, {"backend", event.state.backend}, {"objective", objectiveJson(event.state.objective)}};
        });
        auto fitFailed = bus->Subscribe<FitFailed>(failed);
        auto mcDone = bus->Subscribe<MonteCarloCompleted>([&](const MonteCarloCompleted &event) {
            const auto &mc=*event.result;
            Json samples = Json::array();
            for (std::size_t i = 0; i < mc.samples.size(); ++i) {
                const auto &s = mc.samples[i];
                samples.push_back({{"sample_id", i}, {"parameters", s.parameters}, {"chi_squared", s.chiSquared},
                    {"objective_chi_squared", s.objectiveChiSquared}, {"linear_points",s.linearPoints}, {"log_points",s.logPoints}, {"reduced_chi_squared", s.reducedChiSquared}, {"accepted", s.accepted}, {"converged", s.solverConverged},
                    {"solver_converged", s.solverConverged}, {"stop_reason", s.stopReason}, {"fit_method", s.fitMethod},
                    {"lm_status", s.lmStatus}, {"flat_directions", s.flatDirections}, {"iterations", s.iterations}});
            }
            // Explicit Python output materializes curves only at the binding edge.
            Json noisy=Json::array();
            for(std::size_t sample=0;sample<mc.samples.size();++sample)
                for(double value:JFMMonteCarloNoisyCurve(mc,sample))noisy.push_back(value);
            result = {{"ok", true}, {"samples", samples}, {"parameter_names", mc.parameterNames},
                {"voltage_V", mc.voltage}, {"ideal_current_A", mc.idealCurrent},
                {"noisy_current_A", std::move(noisy)}, {"total_ms", event.state.totalMilliseconds},
                {"accepted_count", event.state.acceptedCount}, {"converged_count", event.state.convergedCount},
                {"backend", event.state.backend}, {"warnings", event.state.warnings}, {"objective", objectiveJson(event.state.objective)},
                {"sigma_current_A", mc.currentSigma}, {"free_parameter_count", mc.freeParameterCount},
                {"fit_dof", static_cast<int>(mc.voltage.size())-mc.freeParameterCount}};
        });
        auto mcFailed = bus->Subscribe<MonteCarloFailed>(failed);
        Ref<IJob> job;
        if (settings.monteCarloSamples == 0) {
            auto queue = CreateRef<JFMFitWorkQueue>();
            (void)queue->Enqueue({{ref, store->BeginFit(ref), record, settings, false}});
            job = CreateRef<JFMFitJob>(bus, store, queue);
        } else {
            JFMFitState fit; fit.modelId = pre.modelId; fit.status = "completed"; fit.parameters = pre.parameters;
            if (!store->ApplyFitResult(ref, store->BeginFit(ref), fit, {})) throw std::runtime_error("Cannot initialize Monte Carlo fit");
            auto queue = CreateRef<JFMMonteCarloWorkQueue>();
            const auto output = Path::FromResolved(std::filesystem::temp_directory_path()/"sst-jfm-headless");
            (void)queue->Enqueue({{ref, store->BeginMonteCarlo(ref), record, *record.info.prefit, fit, settings, output}});
            job = CreateRef<JFMMonteCarloJob>(bus, store, queue);
        }
        auto jobs=FitJobs(); auto commands=FitCommands(); auto id=CreateRef<JobId>(0);
        const CommandID command{"jfm.fit"};
        if (!commands->HasCommand(command)) {
            auto registered = commands->Register({command, "Fit JFM characteristic", "JFM", "Submit a prepared fit job.", {}, CommandFlags::HiddenFromPalette | CommandFlags::AsyncCapable},
                [](CommandContext &context) {
                    auto *jobs=context.TryGet<JobSystem *>("jobs"); auto *job=context.TryGet<Ref<IJob>>("job");
                    auto *id=context.TryGet<Ref<JobId>>("job_id");
                    if (!jobs || !*jobs || !job || !id) throw std::invalid_argument("Missing JFM fit context");
                    return CreateUnique<SubmitFitCommand>(**jobs,*job,*id);
                });
            if (!registered) throw std::runtime_error(registered.Error().technicalDetails);
        }
        CommandContext context; context.Set("jobs",jobs.get()); context.Set("job",job); context.Set("job_id",id);
        auto submitted = commands->Execute(command, std::move(context));
        if (!submitted || !*id) throw std::runtime_error("Fit submission failed");
        while (true) {
            const auto status = jobs->GetJob(*id);
            if (!status) throw std::runtime_error("Fit job disappeared");
            if (status->status == JobStatus::Completed) break;
            if (status->status == JobStatus::Failed || status->status == JobStatus::Cancelled)
            {
                bus->ProcessQueue();
                auto error=SST::JobError(*status);
                error.code=error.category==SST::ErrorCategory::Cancelled ? "jfm.fit.cancelled" : error.category==SST::ErrorCategory::TimedOut ? "jfm.fit.timeout" : error.category==SST::ErrorCategory::Partial ? "jfm.fit.partial" : "jfm.fit.failed";
                error.userMessage=SST::OperationErrorSummary(error);
                result = SST::PythonBoundary::Failure(error);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        bus->ProcessQueue();
        if (result.is_null()) throw std::runtime_error("Fit job produced no result");
        result["project_id"] = project; result["characteristic_id"] = ref.characteristic; result["job_id"] = *id;
        return result;
    } catch (const std::invalid_argument &e) {
        return SST::PythonBoundary::Failure(SST::MakeOperationError(SST::ErrorCategory::Validation,e.what(),"JFM","jfm.fit.validation"));
    } catch (const Json::exception &e) {
        return SST::PythonBoundary::Failure(SST::MakeOperationError(SST::ErrorCategory::Validation,e.what(),"JFM","jfm.fit.validation"));
    } catch (const std::exception &e) {
        return SST::PythonBoundary::Failure(SST::MakeOperationError(SST::ErrorCategory::Internal,e.what(),"JFM","jfm.fit.failed"));
    }
}
}

#if DS_PYTHON_CAPI_AVAILABLE
#include "ScientificRuntime/Python/PythonInterpreter.hpp"

#include <nanobind/nanobind.h>
#include "Core/PlotExport/PlotEditingPython.hpp"
#include "Presentation/JFMPlotDocument.hpp"
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>
#endif

#if DS_PYTHON_CAPI_AVAILABLE
namespace nb = nanobind;

NB_MODULE(sst_jfm, module)
{
    SST::PlotEditingPython::Bind<SST::Prototypes::JFM::JFMPlotDocument>(module);
	using namespace SST::Prototypes::JFM;
	module.attr("schema_version") = JFMProjectSchemaVersion;
    module.def("plot", [](nb::dict request) -> nb::object {
        auto json = nb::module_::import_("json");
        auto response = [&](const nlohmann::json &value) -> nb::object {
            return json.attr("loads")(nb::str(value.dump().c_str()));
        };
        auto failure = [&](const SST::StructuredError &error) {
            return response(SST::PythonBoundary::Failure(error));
        };
        try {
            const auto operation = nb::cast<std::string>(request["operation"]);
            if (operation == "status") {
                const auto id = nb::cast<SST::JobId>(request["job_id"]);
                const auto job = JFMPlotExportStatus(id);
                if (!job) return failure(job.Error());
                static constexpr const char *names[] = {"queued", "running", "completed", "failed", "cancelled"};
                nlohmann::json result = {{"ok", true}, {"job_id", id}, {"status", names[static_cast<int>(job->status)]}};
                if (job->status == SST::JobStatus::Failed || job->status == SST::JobStatus::Cancelled) {
                    result["ok"] = false;
                    result["status"] = SST::JobOutcomeName(*job);
                    result["error"] = SST::PythonBoundary::Error(SST::JobError(*job));
                }
                return response(result);
            }
            // The engine's text API is used only at this Python boundary.
            auto project = sst::plotlab::Project::fromJson(nb::cast<std::string>(json.attr("dumps")(request["project"])));
            auto view = request.contains("view") ? sst::plotlab::viewStateFromJson(nb::cast<std::string>(json.attr("dumps")(request["view"]))) : sst::ui::plot::PlotViewState{};
            auto options = nb::cast<nb::dict>(request.attr("copy")());
            options.attr("pop")("project"); options.attr("pop")("view", nb::none());
            const auto input = nlohmann::json::parse(nb::cast<std::string>(json.attr("dumps")(options)));
            const auto &ranges = input.at("ranges");
            if (ranges.size() != 4) throw std::invalid_argument("ranges requires four values");
            view.viewport = sst::ui::plot::Bounds{ranges.at(0), ranges.at(1), ranges.at(2), ranges.at(3)};
            for (const auto &[key, value] : input.value("visibility", nlohmann::json::object()).items()) view.visibility[key] = value.get<bool>();
            auto ticks = [&](const char *key, auto &out) {
                for (const auto &tick : input.value(key, nlohmann::json::array()))
                    out.push_back({tick.at("value"), tick.value("label", ""), tick.value("major", true)});
            };
            ticks("x_ticks", view.xTicks); ticks("y_ticks", view.yTicks);
            std::shared_ptr<const sst::plotlab::GraphSnapshot> snapshot;
            {
                nb::gil_scoped_release release;
                snapshot = sst::plotlab::snapshotGraph(project, input.at("graph_id"), view);
            }
            const double width = input.value("width_mm", snapshot->widthMm), height = input.value("height_mm", snapshot->heightMm);
            if (!std::isfinite(width) || width <= 0 || !std::isfinite(height) || height <= 0)
                throw std::invalid_argument("width_mm and height_mm must be positive");
            if (operation == "snapshot") {
                nb::dict result; result["ok"] = true;
                auto value = json.attr("loads")(nb::str(sst::plotlab::SnapshotJson(*snapshot).c_str()));
                value["width_mm"] = width; value["height_mm"] = height;
                result["snapshot"] = value;
                return result;
            }
            if (operation != "export") throw std::invalid_argument("Unknown plot operation");
            const std::string path = input.at("path"), format = input.at("format");
            SST::Result<SST::JobId> exported = [&] {
                nb::gil_scoped_release release;
                return ExportJFMPlot(snapshot, path, format, input.value("dpi", 300.0), JFMPlotSize{width, height});
            }();
            if (!exported) return failure(exported.Error());
            return response({{"ok", true}, {"job_id", *exported}, {"status", "queued"}, {"project_id", snapshot->project}, {"graph_id", snapshot->graph}, {"path", path}});
        } catch (const std::exception &e) {
            return response({{"ok", false}, {"error", {{"code", "jfm.plot.validation"}, {"message", e.what()}}}});
        }
    });

    module.def("fit", [](nb::dict request, const std::string &method) {
        auto json = nb::module_::import_("json");
        nlohmann::json result;
        try {
            auto input = nlohmann::json::parse(nb::cast<std::string>(json.attr("dumps")(request)));
            const bool density = input.contains("current_density_A_per_cm2");
            if (density && input.contains("current_A")) throw std::invalid_argument("Choose current_A or current_density_A_per_cm2");
            if (density) { input["current_A"] = std::move(input["current_density_A_per_cm2"]); input.erase("current_density_A_per_cm2"); }
            // Preserve GUI density values: converting to A would change the current log-ratio weights.
            const std::pair<const char *, const char *> fields[] = {
                {"I0", density ? "I0_A_per_cm2" : "I0_A"}, {"A", "ideality_factor"},
                {"Rs", density ? "Rs_ohm_cm2" : "Rs_ohm"}, {"Rsh", density ? "Rsh_ohm_cm2" : "Rsh_ohm"},
                {"alpha", "alpha"}, {"Rsh2", density ? "Rsh2_V_pow_alpha_cm2_per_A" : "Rsh2_V_pow_alpha_per_A"},
                {"I_ph", density ? "Iph_A_per_cm2" : "Iph_A"}};
            auto field = [&](const std::string &name, bool incoming) -> std::string {
                for (const auto &[id, unitName] : fields)
                    if (name == (incoming ? unitName : id)) return incoming ? id : unitName;
                throw std::invalid_argument("Unknown parameter field: " + name);
            };
            if ((density && input.contains("sigma_abs_A")) || (!density && input.contains("sigma_abs_A_per_cm2")))
                throw std::invalid_argument("Absolute sigma units must match the supplied current or current density");
            if (density && input.contains("sigma_abs_A_per_cm2")) {
                input["sigma_abs_A"] = input.at("sigma_abs_A_per_cm2"); input.erase("sigma_abs_A_per_cm2");
            }
            input["method"] = method;
            for (const char *key : {"parameters", "bounds"}) if (input.contains(key)) {
                auto values = nlohmann::json::object();
                for (const auto &[name, value] : input.at(key).items()) values[field(name, true)] = value;
                input[key] = std::move(values);
            }
            if (input.contains("fixed_parameters"))
                for (auto &name : input.at("fixed_parameters")) name = field(name.get<std::string>(), true);
            { nb::gil_scoped_release release; result = RunJFMFitRequest(input); }
            if (result.contains("parameters")) {
                auto values = nlohmann::json::object();
                for (const auto &[name, value] : result.at("parameters").items()) values[field(name, false)] = value;
                result["parameters"] = std::move(values);
            }
            if (density) for (const auto &[from, to] : {std::pair{"ideal_current_A", "ideal_current_density_A_per_cm2"},
                                                         std::pair{"noisy_current_A", "noisy_current_density_A_per_cm2"}})
                if (result.contains(from)) { result[to] = std::move(result[from]); result.erase(from); }
            if (density && result.contains("sigma_current_A")) {
                result["sigma_current_density_A_per_cm2"] = std::move(result["sigma_current_A"]); result.erase("sigma_current_A");
            }
            if (density && result.contains("objective")) for (const auto &[from,to] : {std::pair{"shift_A", "shift_A_per_cm2"}, std::pair{"sigma_abs_A", "sigma_abs_A_per_cm2"}}) {
                result["objective"][to] = std::move(result["objective"][from]); result["objective"].erase(from);
            }
            if (result.contains("parameter_names"))
                for (auto &name : result.at("parameter_names")) name = field(name.get<std::string>(), false);
        } catch (const std::exception &error) {
            result = {{"ok", false}, {"error", {{"code", "jfm.fit.validation"}, {"message", error.what()}}}};
        }
        return json.attr("loads")(nb::str(result.dump().c_str()));
    }, nb::arg("request"), nb::arg("method") = "lm");

	nb::class_<JFMColumn>(module, "JFMColumn")
		.def(nb::init<>())
		.def_rw("name", &JFMColumn::name)
		.def_rw("unit", &JFMColumn::unit)
		.def_rw("role", &JFMColumn::role);

	nb::class_<JFMSweepInfo>(module, "JFMSweepInfo")
		.def(nb::init<>())
		.def_rw("direction", &JFMSweepInfo::direction)
		.def_rw("v_start", &JFMSweepInfo::vStart)
		.def_rw("v_stop", &JFMSweepInfo::vStop)
		.def_rw("n_points", &JFMSweepInfo::pointCount);

	nb::class_<JFMQualityControl>(module, "JFMQualityControl")
		.def(nb::init<>())
		.def_rw("non_monotonic_sweep", &JFMQualityControl::nonMonotonicSweep)
		.def_rw("has_missing_temperature", &JFMQualityControl::hasMissingTemperature)
		.def_rw("warnings", &JFMQualityControl::warnings);

	nb::class_<JFMCharacteristicInfo>(module, "JFMCharacteristicInfo")
		.def(nb::init<>())
		.def_rw("id", &JFMCharacteristicInfo::id)
		.def_rw("name", &JFMCharacteristicInfo::name)
		.def_rw("measurement", &JFMCharacteristicInfo::measurement)
		.def_prop_rw(
			"source_path",
			[](const JFMCharacteristicInfo &info) { return info.sourcePath.Utf8(); },
			[](JFMCharacteristicInfo &info, const std::string &path) { info.sourcePath = SST::Path::FromResolved(std::filesystem::u8path(path)); })
		.def_rw("source_record_key", &JFMCharacteristicInfo::sourceRecordKey)
		.def_rw("parser", &JFMCharacteristicInfo::parser)
		.def_rw("temperature_K", &JFMCharacteristicInfo::temperatureK)
		.def_rw("temperature_source", &JFMCharacteristicInfo::temperatureSource)
		.def_rw("columns", &JFMCharacteristicInfo::columns)
		.def_rw("sweep", &JFMCharacteristicInfo::sweep)
		.def_rw("qc", &JFMCharacteristicInfo::qc)
		.def_rw("metadata", &JFMCharacteristicInfo::metadata);

	nb::class_<JFMCharacteristicData>(module, "JFMCharacteristicData")
		.def(nb::init<>())
		.def_rw("voltage", &JFMCharacteristicData::voltage)
		.def_rw("current", &JFMCharacteristicData::current)
		.def_rw("current_density", &JFMCharacteristicData::currentDensity);

	nb::class_<JFMCharacteristic>(module, "JFMCharacteristic")
		.def(nb::init<>())
		.def_rw("info", &JFMCharacteristic::info)
		.def_rw("data", &JFMCharacteristic::data);

	nb::class_<JFMGroup>(module, "JFMGroup")
		.def(nb::init<>())
		.def_rw("id", &JFMGroup::id)
		.def_rw("name", &JFMGroup::name)
		.def_rw("parent_id", &JFMGroup::parentId);

	nb::class_<JFMSeries>(module, "JFMSeries")
		.def(nb::init<>())
		.def_rw("id", &JFMSeries::id)
		.def_rw("name", &JFMSeries::name)
		.def_rw("group_id", &JFMSeries::groupId)
		.def_rw("characteristic_ids", &JFMSeries::characteristicIds);
}
#endif

namespace SST::Prototypes::JFM
{
	bool RegisterJFMPythonBindings()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		static bool registered = false;
		if (registered)
			return true;

		if (PythonInterpreter::IsRuntimeInitialized() || Py_IsInitialized())
		{
			DS_LOG_WARN("JFM Python bindings registration skipped: Python runtime is already initialized");
			return false;
		}

		if (PyImport_AppendInittab("sst_jfm", PyInit_sst_jfm) != 0)
		{
			DS_LOG_ERROR("JFM Python bindings registration failed");
			return false;
		}

		registered = true;
		return true;
#else
		return false;
#endif
	}
} // namespace SST::Prototypes::JFM

namespace SST::Prototypes::JFM {
SST::PlotExportRequest MakeJFMPlotExportRequest(std::shared_ptr<const sst::plotlab::GraphSnapshot> snapshot,
    const std::filesystem::path &path, const std::string &format, double dpi, JFMPlotSize size)
{
    SST::PlotExportRequest request{{}, path, format, dpi};
#if DS_PYTHON_CAPI_AVAILABLE
    request.pythonSnapshot = [snapshot = std::move(snapshot), size]() -> nanobind::object {
        auto value = nanobind::module_::import_("json").attr("loads")(nanobind::str(sst::plotlab::SnapshotJson(*snapshot).c_str()));
        value["width_mm"] = size.widthMm; value["height_mm"] = size.heightMm;
        return value;
    };
#endif
    return request;
}
}
