#include "IO/PrototypePythonBindings.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Domain/CapLabBundleImport.hpp"
#include "Domain/CapLabAnalysis.hpp"
#include "CapLabCommands.hpp"
#include "Core/Threading/ThreadAffinity.hpp"
#include <sst/physics/capacitance.hpp>
#include <sst/physics/arrhenius.hpp>
#include <sst/physics/materials.hpp>
#include <sst/physics/walter.hpp>
#include <sst/math/linear_fit.hpp>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Core/ScientificRuntime/Python/PythonGilScope.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/vector.h>
namespace {
// A caller-supplied operation ID lets another Python thread send the Core cancel command.
std::mutex runningMutex;
std::unordered_map<std::string,std::function<bool()>> runningOperations;
struct RunningOperation {
    std::string id;
    explicit RunningOperation(const std::string &id, std::function<bool()> cancel) : id(id) {
        if (id.empty()) return;
        if (id.size()>128) {cancel(); throw std::runtime_error("operation_id exceeds 128 characters");}
        std::lock_guard lock(runningMutex);
        if (runningOperations.contains(id)) {cancel(); throw std::runtime_error("operation_id already running");}
        runningOperations.emplace(id,std::move(cancel));
    }
    ~RunningOperation() {
        if (!id.empty()) {std::lock_guard lock(runningMutex); runningOperations.erase(id);}
    }
};
std::filesystem::path MaterialDataPath(const char *name = "caplab-materials.json") {
    const auto exe=SST::Platform::CurrentExecutablePath();
    if (exe) {
        const auto local=exe.Value().parent_path().Join(name).Native();
        if (std::filesystem::exists(local)) return local;
    }
    return std::filesystem::path(SST_CAPLAB_MATERIALS_JSON).parent_path()/name;
}
}
NB_MODULE(sst_caplab, module)
{
    using namespace SST::Prototypes::CapLab;
    module.def("project_from_preview", [](const std::string &preview, std::optional<double> temperature)
    {
        Project project;
        const auto j = Json::parse(preview);
        if (j.contains("samples")) { BundleChange change(project,j); }
        else { ImportChange change(project,j,temperature,CFTemplate{}); }
        return Serialize(project);
    });
    module.def("cv_linear_fit", [](const std::vector<double> &x, const std::vector<double> &y) {
        const auto f = sst::math::weightedLinearFit(x,y,{},false);
        if (f.status == sst::math::LinearFitStatus::InvalidInput || f.status == sst::math::LinearFitStatus::RankDeficient)
            throw std::runtime_error("Invalid/rank deficient CV fit (constant V).");
        return Json{{"slope",f.slope},{"intercept",f.intercept},{"covariance",f.covariance},
            {"dof",f.degreesOfFreedom},{"sum_squared_residuals",f.chiSquared}}.dump();
    });
    module.def("arrhenius_fit", [](const std::vector<double> &t, const std::vector<double> &rate, bool compat) {
        using namespace sst::physics;
        const auto model = compat ? ArrheniusTransform::ProPhoLogFrequency : ArrheniusTransform::EmissionOverTemperatureSquared;
        const auto f = fitArrhenius(t,rate,{},false,model);
        const auto &l = f.linearFit;
        if ((l.status != sst::math::LinearFitStatus::Success && l.status != sst::math::LinearFitStatus::InsufficientDegreesOfFreedom) ||
            !std::isfinite(f.activationEnergyEv) || !std::isfinite(f.prefactor) || f.prefactor <= 0)
            throw std::runtime_error("Arrhenius needs at least two distinct temperatures and finite positive rates.");
        Json x=Json::array(), y=Json::array();
        for (std::size_t i=0;i<t.size();++i) {
            const auto v=transformArrhenius(t[i],rate[i],model); x.push_back(v.inverseTemperature); y.push_back(v.logRate);
        }
        return Json{{"E_a_eV",f.activationEnergyEv},{"prefactor",f.prefactor},{"slope",l.slope},{"intercept",l.intercept},
            {"covariance",l.degreesOfFreedom ? Json(f.covariance) : Json(nullptr)},
            {"linear_covariance",l.degreesOfFreedom ? Json(l.covariance) : Json(nullptr)},
            {"dof",l.degreesOfFreedom},{"sum_squared_residuals",l.chiSquared},{"inverse_temperature_K",x},{"log_rate",y}}.dump();
    });
    module.def("walter_point", [](double omega, double t, double prefactor, double vb, double ef, double xd, double d, bool compat) {
        try {
            const auto p=sst::physics::walterPoint(omega,t,prefactor,vb,ef,xd,d,compat ? sst::physics::WalterModel::ProPhoCompat : sst::physics::WalterModel::LinearField1996);
            return Json{{"energy_eV",p.energy_eV},{"field_factor_V_m_C",p.factor},{"density_cm3_eV",p.density_cm3_eV},{"quality","valid"}}.dump();
        } catch (const std::domain_error &e) {
            const double energy=sst::physics::k_B_over_q*t*(std::log(prefactor)+(compat ? 0 : 2*std::log(t))-std::log(omega));
            return Json{{"energy_eV",energy},{"field_factor_V_m_C",nullptr},{"density_cm3_eV",nullptr},{"quality",e.what()}}.dump();
        }
    });
    module.def("walter_integral", [](const std::vector<double> &e, const std::vector<std::optional<double>> &n, double lo, double hi, bool compat) {
        std::vector<double> values; for (const auto &v : n) values.push_back(v.value_or(std::numeric_limits<double>::quiet_NaN()));
        const auto r=sst::physics::integrateWalter(e,values,lo,hi,compat);
        return Json{{"density_cm3",Nullable(r.density_cm3)},{"segments",r.segments},{"gaps",r.gaps},
            {"range_eV",{lo,hi}},{"semantics",compat ? "ProPho-minus-trapz" : "ascending-clipped-trapezoids"}}.dump();
    });
    module.def("compat_sigma", &sst::physics::proPhoCaptureCrossSection);
    module.def("area_inverse_square", &sst::physics::areaInverseCapacitanceSquared);
    module.def("capacitance_from_inverse_square", &sst::physics::capacitanceFromAreaInverseSquare);
    module.def("constants", [] { return Json{{"q_C",sst::physics::q},{"eps0_F_m",sst::physics::eps0},
        {"kappa_eV_K",sst::physics::k_B_over_q},{"k_B_J_K",sst::physics::k_B},{"source","CODATA 2022 / SI; SST-Physics constants.hpp"}}.dump(); });
    module.def("width", &sst::physics::depletionWidth);
    module.def("cv_density", &sst::physics::cvDensity);
    module.def("dlcp_density", &sst::physics::dlcpDensity);
    module.def("fermi_energy", &sst::physics::bulkFermiEnergy);
    module.def("measurement", [](const std::string &text, pl::Id id) { return AnalysisMeasurement(Parse(text),id).dump(); });
    module.def("edit_defect", [](const std::string &text, const std::string &edit) {
        auto p=Parse(text); EditDefect(p,Json::parse(edit)); RefreshAnalyses(p); return Serialize(p);
    });
    module.def("refresh", [](const std::string &text) { auto p=Parse(text); RefreshAnalyses(p); return Serialize(p); });
    module.def("compat_material", [] {
        std::ifstream stream(MaterialDataPath("caplab-propho-compat.json"));
        if (!stream) throw std::runtime_error("Missing sourced material data file.");
        Json data; stream >> data;
        const auto &preset=data;
        sst::physics::positiveCapacitanceInput(preset.at("eps_r").at("value").get<double>());
        sst::physics::positiveCapacitanceInput(preset.at("N_ref_m3").at("value").get<double>());
        return preset.dump();
    });
    module.def("materials", [](double temperature) {
        Json out = Json::array();
        sst::physics::MaterialLibrary library;
        library.mergeProjectFile(MaterialDataPath());
        for (const auto &m : library.list()) {
            if (!m.eps_r) continue;
            out.push_back({{"name",m.name},{"eps_r",m.eps_r->value},{"eps_source",m.eps_r->source},
                {"Nc300_cm3",m.Nc300 ? Json(m.Nc300->value) : Json(nullptr)},
                {"Nv300_cm3",m.Nv300 ? Json(m.Nv300->value) : Json(nullptr)},
                {"Nc_m3",m.Nc300 ? Json(m.Nc(temperature)*1e6) : Json(nullptr)},
                {"Nv_m3",m.Nv300 ? Json(m.Nv(temperature)*1e6) : Json(nullptr)},
                {"Nc_source",m.Nc300 ? Json(m.Nc300->source) : Json(nullptr)},
                {"Nv_source",m.Nv300 ? Json(m.Nv300->source) : Json(nullptr)}});
        }
        return out.dump();
    }, nanobind::arg("temperature_K") = 300.);
    module.def("execute", [](const std::string &project, const std::string &request) {
        using namespace SST;
        // Same Core command/job/main-thread acceptance path as the panel.
        nanobind::gil_scoped_release release;
        Json response;
        try {
            const auto r=Json::parse(request); const std::string operation=r.at("operation");
            if (operation=="cancel") {
                const std::string id=r.at("operation_id");
                std::lock_guard lock(runningMutex);
                const auto it=runningOperations.find(id);
                if (it==runningOperations.end() || !it->second())
                    return Json{{"ok",false},{"error",{{"code","not_running"},{"message","No cancellable operation with this operation_id"}}}}.dump();
                return Json{{"ok",true},{"error",nullptr}}.dump();
            }
            if (!SST::Threading::IsMainThread()) throw std::runtime_error("CapLab operations require the Core main thread; cancel may run from another thread");
            const std::string operationId=r.value("operation_id",std::string{});
            if (operationId.size()>128) throw std::runtime_error("operation_id exceeds 128 characters");
            auto jobs=CreateRef<SST::JobSystem>(nullptr,1);
            auto bus=CreateRef<SST::EventBus>(); auto undo=CreateRef<SST::UndoStack>();
            auto commands=CreateRef<SST::CommandRegistry>();
            CapLabWorkflow state(jobs,bus,undo); state.document.project=Parse(project);
            RegisterCommands(commands,bus,state);
            const auto analysisCount=state.document.project.analyses.size();
            auto subscription=bus->Subscribe<DocumentRequested>([&](const auto &event) {state.Start(event.operation,event.target);});
            if (operation=="export" || operation=="save" || operation=="open" || operation=="import") {
                const auto path=Path{r.at("path").get<std::string>()};
                if (operation=="import" && r.contains("options")) state.importOptions=r.at("options").dump();
                if (r.contains("graph_id")) state.document.project.graph=r.at("graph_id").get<pl::Id>();
                Validate(state.document.project);
                if (operation=="export" && (path.Native().extension()==".png" || path.Native().extension()==".svg" || path.Native().extension()==".pdf")) {
                    if(r.contains("view")) state.view=pl::viewStateFromJson(r.at("view").dump());
                    const auto b=r.at("ranges").get<std::vector<double>>();
                    if (b.size()!=4) throw std::runtime_error("Figure export requires four resolved preview ranges");
                    state.view.viewport=sst::ui::plot::Bounds{b[0],b[1],b[2],b[3]};
                    if (r.contains("visibility")) for (const auto &v : r.at("visibility")) state.view.visibility[state.document.project.engine->series(v.at("plot_id").get<pl::Id>()).id]=v.at("visible").get<bool>();
                }
                const auto result=commands->Execute(CommandID{operation=="export" ? "caplab.export" : operation=="save" ? "caplab.save" : operation=="import" ? "app.open" : "caplab.open"},[&] { CommandContext c; c.Set("path",path); return c; }());
                if (!result) throw std::runtime_error(result.Error().technicalDetails);
                bus->ProcessQueue(); const auto id=state.activeJob;
                jobs->Shutdown(); state.OnUpdate(0);
                const auto job=jobs->GetJob(id);
                if (!job || job->status!=JobStatus::Completed)
                    return Json{{"ok",false},{"project",Json::parse(project)},{"error",{{"code",operation+"_failed"},{"message",state.status}}}}.dump();
                if (state.preview) {
                    // A filename is a suggestion; headless callers confirm metadata through options.
                    if (!state.preview->contains("samples")) state.temperature=Optional(state.preview->at("temperature_K"));
                    const auto imported=commands->Execute(CommandID{"caplab.import"});
                    if (!imported) return Json{{"ok",false},{"project",Json::parse(project)},{"error",{{"code","caplab.import"},{"message",state.status}}}}.dump();
                }
                response["export"]=state.exportResult;
            } else if (operation=="edit_defect" || operation=="area" || operation=="delete_measurement") {
                CommandContext context;
                std::string command="caplab.defect";
                if (operation=="area") { command="caplab.area"; context.Set("sample_id",r.at("sample_id").get<pl::Id>()); context.Set("area_m2",r.at("area_m2").get<double>()); }
                else {command=operation=="delete_measurement" ? "caplab.measurement" : "caplab.defect"; context.Set("edit",operation=="delete_measurement" ? Json{{"measurement_id",r.at("measurement_id")}} : r.at("edit"));}
                const auto result=commands->Execute(CommandID{command},std::move(context));
                if (!result) throw std::runtime_error(result.Error().technicalDetails);
            } else if (operation=="edit_circuit") {
                SST::CommandContext context; context.Set("edit",r.at("edit"));
                const auto result=commands->Execute(SST::CommandID{"caplab.circuit"},std::move(context));
                if (!result) throw std::runtime_error(result.Error().technicalDetails);
            } else if (operation=="accept_fit") {
                const auto &draft=r.at("draft");
                if (Json::parse(Serialize(state.document.project))!=draft.at("base_project")) throw std::runtime_error("Stale fit draft");
                state.analysisDraft=Parse(draft.at("project").dump());
                state.analysisRevision=state.document.revision;
                const auto result=commands->Execute(SST::CommandID{"caplab.accept_analysis"});
                if (!result) throw std::runtime_error(result.Error().technicalDetails);
            } else if (operation=="simulate" || operation=="fit" || operation=="walter" || operation=="integrate_saved_walter" || operation=="analyze") {
                if (r.value("cancel",false) && !jobs->PauseAllJobs()) throw std::runtime_error("Cannot pause job queue");
                state.analysisOptions=r.at("settings"); state.analysisOptions["type"]=operation=="analyze" ? r.at("settings").at("type").get<std::string>() : operation=="walter" ? "Walter" : operation=="integrate_saved_walter" ? "WalterSaved" : "Circuit";
                state.analysisOptions["fit"]=operation=="fit";
                const auto result=commands->Execute(SST::CommandID{"caplab.analyze"});
                if (!result) throw std::runtime_error(result.Error().technicalDetails);
                bus->ProcessQueue();
                if (r.value("cancel",false)) {
                    (void)commands->Execute(SST::CommandID{"caplab.cancel"});
                    (void)jobs->ResumeAllJobs();
                }
                if (!state.activeJob) throw std::runtime_error(state.status);
                const auto id=state.activeJob;
                {
                    RunningOperation running(operationId,[&] {
                        return bool(commands->Execute(SST::CommandID{"caplab.cancel"}));
                    });
                    jobs->Shutdown();
                }
                state.OnUpdate(0);
                const auto job=jobs->GetJob(id);
                if (!job || job->status!=SST::JobStatus::Completed || (!state.analysisDraft && state.document.project.analyses.size()!=analysisCount+1))
                    return Json{{"ok",false},{"project",Json::parse(project)},
                        {"error",{{"code",job && job->status==SST::JobStatus::Cancelled ? "cancelled" : operation=="analyze" ? "analysis_failed" : operation=="fit" ? "fit_failed" : (operation=="walter" || operation=="integrate_saved_walter") ? "walter_failed" : "simulation_failed"},{"message",state.status}}}}.dump();
            } else throw std::runtime_error("Unknown CapLab operation");
            response["draft"]=state.analysisDraft ? Json{{"base_project",Json::parse(project)},
                {"project",Json::parse(Serialize(*state.analysisDraft))}} : Json(nullptr);
            response.update(Json{{"ok",true},{"project",Json::parse(Serialize(state.document.project))},{"error",nullptr}});
        } catch (const std::exception &e) {
            response={{"ok",false},{"error",{{"code","invalid_request"},{"message",e.what()}}}};
        }
        return response.dump();
    });
    module.def("roundtrip", [](const std::string &text) { return Serialize(Parse(text)); });
}
#endif
namespace SST::Prototypes::CapLab
{
    void RegisterPrototypePythonModule()
    {
#if DS_PYTHON_CAPI_AVAILABLE
        static bool registered = false;
        if (registered) return;
        if (Py_IsInitialized() || PyImport_AppendInittab("sst_caplab", PyInit_sst_caplab) != 0)
            throw std::runtime_error("Register sst_caplab before starting Python.");
        registered = true;
#endif
    }
    Json ExportData(const Project &project, const Path &path, JobContext &context)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        if (!PythonInterpreter::IsRuntimeInitialized()) throw std::runtime_error("Embedded Python unavailable for export.");
        nanobind::gil_scoped_acquire gil;
        namespace nb=nanobind;
        try {
            nb::module_::import_("sst_caplab");
            auto paths=nb::module_::import_("sys").attr("path");
            const auto executable=Platform::CurrentExecutablePath();
            if (!executable) throw std::runtime_error("Cannot locate embedded Python package.");
            const auto local=executable.Value().parent_path().Join("python");
            const std::string root=FileSystem::Exists(local.Join("caplab").Native()) ? local.Utf8() : SST_PROTOTYPE_PYTHON_ROOT;
            if (!nb::cast<bool>(paths.attr("__contains__")(root))) paths.attr("insert")(0,root);
            Json data=Json::parse(Serialize(project)); data.erase("engine"); data["measurement_data"]=Json::array();
            for (const auto &m : project.measurements) data["measurement_data"].push_back(AnalysisMeasurement(project,m.at("id").get<pl::Id>()));
            for (const auto &m : project.sweeps) {
                auto measurement=AnalysisMeasurement(project,m.id);
                const auto &sheet=std::get<pl::Worksheet>(project.engine->object(m.id).data);
                measurement["provenance"]=sheet.source;
                for (const auto cid : project.engine->children(m.id)) if (cid!=m.frequency && cid!=m.capacitance && cid!=m.resistance) {
                    const auto &object=project.engine->object(cid);
                    if (const auto *column=std::get_if<pl::Column>(&object.data)) {
                        measurement["names"].push_back(object.name); measurement["units"].push_back(column->units);
                        measurement["columns"].push_back(std::get<std::vector<double>>(column->values));
                    }
                }
                data["measurement_data"].push_back(std::move(measurement));
            }
            std::function<bool()> cancel=[&] {return context.IsCancellationRequested();};
            const auto result=nb::module_::import_("caplab.exporter").attr("export_json")(data.dump(),path.Utf8(),cancel);
            return Json::parse(nb::cast<std::string>(result));
        } catch (const nb::python_error &e) {context.ThrowIfCancellationRequested(); throw std::runtime_error(e.what());}
#else
        (void)project; (void)path; (void)context;
        throw std::runtime_error("Embedded Python unavailable for export.");
#endif
    }
    Json ReadAnalysis(const Json &measurement, const Json &options, const Json &xd, JobContext &context)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        if (!PythonInterpreter::IsRuntimeInitialized()) throw std::runtime_error("Embedded Python unavailable.");
        // Headless callers may hold Core's outer scope mutex while releasing the GIL.
        nanobind::gil_scoped_acquire gil;
        namespace nb = nanobind;
        try {
            nb::module_::import_("sst_caplab");
            auto paths = nb::module_::import_("sys").attr("path");
            const auto executable = Platform::CurrentExecutablePath();
            if (!executable) throw std::runtime_error("Cannot locate Python runtime.");
            const auto local = executable.Value().parent_path().Join("python");
            const std::string root = FileSystem::Exists(local.Join("caplab").Native()) ? local.Utf8() : SST_PROTOTYPE_PYTHON_ROOT;
            if (!nb::cast<bool>(paths.attr("__contains__")(root))) paths.attr("insert")(0,root);
            const std::function<bool()> cancel = [&] { return context.IsCancellationRequested(); };
            const std::function<void(double)> progress = [&](double v) { context.SetProgress(float(v),1); };
            auto value = nb::module_::import_("caplab.analysis").attr("analyze_json")(measurement.dump(),options.dump(),xd.is_null() ? "" : xd.dump(),cancel,progress);
            return Json::parse(nb::cast<std::string>(value));
        } catch (const nb::python_error &e) { context.ThrowIfCancellationRequested(); throw std::runtime_error(e.what()); }
#else
        (void)measurement; (void)options; (void)xd; (void)context;
        throw std::runtime_error("Embedded Python unavailable.");
#endif
    }
    Json ReadLabAS(const std::string &text, const Path &source, char decimal, JobContext &context, const std::string &options)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        if (!PythonInterpreter::IsRuntimeInitialized()) throw std::runtime_error("Embedded Python is unavailable; rebuild with embedding.");
        nanobind::gil_scoped_acquire gil;
        namespace nb = nanobind;
        try
        {
            nb::module_::import_("sst_caplab");
            auto paths = nb::module_::import_("sys").attr("path");
            const auto executable = Platform::CurrentExecutablePath();
            if (!executable) throw std::runtime_error(executable.Error().technicalDetails);
            const auto root = executable.Value().parent_path().Join("python");
            const std::string pythonRoot = FileSystem::Exists(root.Join("caplab").Native()) ? root.Utf8() : SST_PROTOTYPE_PYTHON_ROOT;
            if (!nb::cast<bool>(paths.attr("__contains__")(pythonRoot))) paths.attr("insert")(0,pythonRoot);
            const std::function<bool()> cancel = [&] { return context.IsCancellationRequested(); };
            const std::function<void(double)> progress = [&](double v) { context.SetProgress(float(v),1); };
            // nanobind must be initialized before converting std::function callbacks.
            const auto result = nb::module_::import_("caplab").attr("preview_json")(text,source.Utf8(),std::string(1,decimal),cancel,progress,options);
            return Json::parse(nb::cast<std::string>(result));
        }
        catch (const nb::python_error &e)
        {
            context.ThrowIfCancellationRequested();
            throw std::runtime_error(e.what());
        }
#else
        (void)text; (void)source; (void)decimal; (void)context; (void)options;
        throw std::runtime_error("Embedded Python is unavailable; rebuild with embedding.");
#endif
    }
}
