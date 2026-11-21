#include "Workflow.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
namespace nb=nanobind;
NB_MODULE(sst_scapslab, m) {
    using namespace SST; using namespace SST::ScapsLab;
    nb::class_<Workflow>(m,"Session")
        .def("__init__",[](Workflow *self,const std::string &document) {
            auto parsed=Json::parse(document);if(parsed.is_null()) parsed=ReadProject(SCAPSLAB_PROJECT);
            new(self) Workflow(CreateRef<JobSystem>(nullptr,1)); if(parsed.is_object() && parsed.contains("devices")) self->SetWorkspace(UnpackResults(parsed));else self->Current().document=NormalizeDocument(SelfContained(std::move(parsed)));
        })
        .def("start",[](Workflow &s,const std::string &request) {
            try {return s.Dispatch(Json::parse(request)).dump();}
            catch(const std::exception &e) {return Json{{"ok",false},{"error",{{"code","invalid_request"},{"message",e.what()}}}}.dump();}
        })
        .def("poll",[](Workflow &s) {
            s.OnUpdate(0); Json j={{"running",s.Pending() || s.tutorial.Automatic()},{"auto_recalculate_pending",(s.autoRecalculate && s.Current().needsRecalculate) || (s.autoRecalculateBand && s.Current().needsBand)},{"status",s.status},{"response",s.response}};
            j["tutorial"]=s.tutorial.Report();
            j["batch_progress"]={{"completed",s.batchCompleted},{"total",s.batchTotal}};
            if(s.active) { const auto p=s.jobs->GetJob(s.active); if(p) j["progress"]={{"completed",p->completedWork},{"total",p->totalWork},{"stage",p->currentStage}}; }
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

