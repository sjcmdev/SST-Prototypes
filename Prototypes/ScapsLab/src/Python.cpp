#include "Workflow.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Commands/Commands.hpp"
#include <thread>
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
namespace nb=nanobind;
NB_MODULE(sst_scapslab, m) {
    using namespace SST; using namespace SST::ScapsLab;
    nb::class_<Workflow>(m,"Session")
        .def("__init__",[](Workflow *self,const std::string &document) {
            auto parsed=Json::parse(document);if(parsed.is_null()) parsed=ReadProject(SCAPSLAB_PROJECT);
            const auto hardware=std::thread::hardware_concurrency();
            new(self) Workflow(CreateRef<JobSystem>(nullptr,hardware>1?hardware-1:1)); if(parsed.is_object() && parsed.contains("devices")) self->SetWorkspace(UnpackResults(parsed));else self->Current().SetDocument(NormalizeDocument(SelfContained(std::move(parsed))));
        })
        .def("start",[](Workflow &s,const std::string &request) {
            try {
                auto value=Json::parse(request);const auto op=value.at("operation").get<std::string>();
                s.bindingBoundsReply=op=="parameter_bounds";
                Json response;
                if(op=="run_settings")response=s.Exec(Cmd::SetRunPlan::Make(CreateRef<const RunPlan>(ReadRunPlan(value.at("actions")))));
                else if(op=="single_shot")response=s.Exec(Cmd::SingleShot::Make(value.contains("actions")?CreateRef<const RunPlan>(ReadRunPlan(value.at("actions"))):nullptr));
                else if(op=="run_batch" && (value.contains("actions") || value.contains("spec") || value.contains("columns")))response=s.Exec(Cmd::RunBatch::Make(value.contains("spec")?ReadBatchDraft(value.at("spec")):s.Current().batch,value.contains("columns")?ReadRecorderDraft(value.at("columns")):s.Current().recorder,value.contains("actions")?CreateRef<const RunPlan>(ReadRunPlan(value.at("actions"))):s.Current().runPlanEdited?s.Current().runPlan:nullptr));
                else response=s.Dispatch(value);
                if(op=="parameter_bounds" && response.value("ok",false)) {
                    if(value.value("action",std::string("inspect"))=="inspect") {
                        auto rows=Json::object();const auto table=BoundsTable();
                        for(const auto& [key,b]:*table)rows[key]={{"hard_min",b.hardMin},{"hard_max",b.hardMax},{"soft_min",b.softMin},{"soft_max",b.softMax},{"unit",b.unit},{"display_unit",b.displayUnit},{"display_scale",b.displayScale},{"scale",b.scale==ParameterScale::Log?"log":"linear"},{"step",b.step},{"format",b.format},{"integer",b.integer},{"allow_infinity",b.allowInfinity},{"source",b.source},{"note",b.note},{"origin",b.userOverride?"user":"default"}};
                        response["parameter_bounds"]=std::move(rows);
                    }
                    s.response=response;
                }
                if((op=="run_settings" || op=="select_bias") && response.value("ok",false))s.response=s.Snapshot();
                return response.dump();
            }
            catch(const std::exception &e) {return Json{{"ok",false},{"error",{{"code","invalid_request"},{"message",e.what()}}}}.dump();}
        })
        .def("poll",[](Workflow &s) {
            s.OnUpdate(0); Json j={{"running",s.Pending() || s.boundsFileBusy || s.tutorial.Automatic()},{"auto_recalculate_pending",(s.autoRecalculate && s.Current().needsRecalculate) || (s.autoRecalculateBand && s.Current().needsBand)},{"status",s.status},{"response",s.response}};
            if(s.bindingBoundsReply && !s.boundsFileDiagnostic.empty())j["response"]={{"ok",false},{"error",{{"code","invalid_bounds_file"},{"message",s.boundsFileDiagnostic}}}};
            j["tutorial"]=s.tutorial.Report();
            j["paused"]=s.ActiveOp() && s.ActiveOp()->paused.load();
            j["batch_progress"]={{"completed",s.batchCompleted},{"total",s.batchTotal}};
            if(const auto aj=s.ActiveJob()) { const auto p=s.jobs->GetJob(aj); if(p) j["progress"]={{"completed",p->completedWork},{"total",p->totalWork},{"stage",p->currentStage}}; }
            return j.dump();
        });
}
#endif
namespace SST::ScapsLab {
void RegisterPython() {
#if DS_PYTHON_CAPI_AVAILABLE
    static bool registered=false;
    if(registered) return;
    if(Py_IsInitialized() || PyImport_AppendInittab("sst_scapslab",PyInit_sst_scapslab)!=0) throw std::runtime_error("Register ScapsLab before Python startup");
    registered=true;
#endif
}
}

