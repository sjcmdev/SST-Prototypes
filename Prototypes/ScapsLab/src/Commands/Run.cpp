#include "Commands/Commands.hpp"
// Calculation commands: busy gate + a job on the active device's lane (Workflow::StartOperation); stop / run-all / auto-recalculate.
namespace SST::ScapsLab::Cmd {
Out Reject(const char *code, std::string title, std::string details, std::string hint) {
    return StructuredError{ErrorCategory::Validation, Severity::Error, std::move(title), std::move(details), std::move(hint), "ScapsLab", code};
}
Json Merge(Json args, const char *op) {args["operation"] = op;return args;}
Out Job(ILabStore &s, Json request) {
    if(s.Busy()) return Reject("busy", "Busy", "A job is still active", "Wait or cancel");
    s.StartJob(std::move(request));
    return {};
}
Out RunAll::Execute(ILabStore &s) const {
    if(s.Busy()) return Reject("busy", "Busy", "A job is still active", "Wait or cancel");
    s.RunAll();
    return {};
}
Out Cancel::Execute(ILabStore &s) const {
    if(args.contains("device_id")) { // one device: the other lanes keep running
        const auto id = args.at("device_id").get<std::string>();
        s.RequireDevice(id);
        if(!s.DeviceRunning(id)) return Reject("not_running", "No running job", "No running job", "Start a job");
        s.CancelDevice(id);
        return {};
    }
    if(!s.CancelAll()) return Reject("not_running", "No running job", "No running job", "Start a job");
    return {};
}
Out SetAutoRecalculate::Execute(ILabStore &s) const {
    const bool automatic = args.at("enabled").get<bool>();
    std::optional<bool> band;
    if(args.contains("band_enabled")) band = args.at("band_enabled").get<bool>();
    s.SetAutoRecalculate(automatic, band);
    return {};
}
}
