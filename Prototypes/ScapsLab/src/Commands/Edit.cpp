#include "Commands/Commands.hpp"
#include <stdexcept>
// Document, undo/redo, history and device commands. Bodies that mutate state live in the store (Workflow); the commands only decide.
namespace SST::ScapsLab::Cmd {
Out EditDocument::Execute(ILabStore &s) const {
    if(s.ReplacingProject()) return Reject("busy", "Busy", "Project structure is changing; wait before editing", "Wait for the operation");
    if(draft) {s.EditTyped(draft,gesture);return {};}
    if(s.Busy()) {s.QueueEdit(ToJson());return {};} // a job runs: the edit queues behind it (latest wins per gesture)
    s.StartJob(ToJson());
    return {};
}
namespace {
// S9: undo/redo are synchronous and never wait for calculations (the store cancels the active device's job). Only a job that replaces the whole project blocks them.
Out StepOut(ILabStore &s, bool undo) {
    for(const auto &op : s.ActiveOps())
        if(op == "project_load" || op == "load" || op == "project_new") return Reject("busy", "Busy", "The project is being replaced", "Wait for the operation");
    return s.Step(undo);
}
}
Out Undo::Execute(ILabStore &s) const {return StepOut(s, true);}
Out Redo::Execute(ILabStore &s) const {return StepOut(s, false);}
Out ListHistory::Execute(ILabStore &s) const {
    if(args.contains("device_id")) s.RequireDevice(args.at("device_id").get<std::string>());
    s.PublishSnapshot(); // read-only: answered inline
    return {};
}
Out SelectDevice::Execute(ILabStore &s) const {
    for(const auto &op : s.ActiveOps())
        if(op != "run" && op != "band" && op != "cv" && op != "cf" && op != "qe" && op != "dlcp" && op != "single_shot") throw std::invalid_argument("Wait for the project operation before selecting a device");
    const auto id = args.at("device_id").get<std::string>();
    s.RequireDevice(id);
    if(s.EditQueued()) throw std::invalid_argument("Wait for queued edits before selecting a device");
    s.SelectDevice(id);
    return {};
}
namespace {
Out InPlace(ILabStore &s, const Json &request) {
    if(s.Busy()) return Reject("busy", "Busy", "A job is still active", "Wait or cancel");
    s.DeviceOp(request);
    return {};
}
}
Out AddDevice::Execute(ILabStore &s) const {
    if(args.contains("path")) {auto read = ToJson();read["operation"] = "device_read";return Job(s, std::move(read));} // only the file read runs as a job
    return InPlace(s, ToJson());
}
Out DuplicateDevice::Execute(ILabStore &s) const {return InPlace(s, ToJson());}
Out RemoveDevice::Execute(ILabStore &s) const {return InPlace(s, ToJson());}
Out UpdateDevice::Execute(ILabStore &s) const {return InPlace(s, ToJson());}
Out MoveDevice::Execute(ILabStore &s) const {return InPlace(s, ToJson());}
Out ListDevices::Execute(ILabStore &s) const {return InPlace(s, ToJson());}
Out EditProject::Execute(ILabStore &s) const {return InPlace(s, ToJson());}
Out SetActionSettings::Execute(ILabStore &s) const {return InPlace(s, ToJson());}
}
