#include "Commands/Commands.hpp"
#include <filesystem>
// Project / file commands. They wrap the existing save/load jobs; the implementations (Workspace.cpp, Operation::Execute) are not touched here.
namespace SST::ScapsLab::Cmd {
namespace {
bool OnlyCalculations(const std::vector<std::string> &ops) {
    for(const auto &o : ops) if(o != "run" && o != "band" && o != "cv" && o != "cf" && o != "qe" && o != "run_batch") return false;
    return true;
}
Out Open(ILabStore &s, const Json &request) {
    if(s.AnyActive() && OnlyCalculations(s.ActiveOps())) {s.DeferProjectLoad(request);return {};} // cancel the calculations, open when idle
    return Job(s, request);
}
}
Out LoadProject::Execute(ILabStore &s) const {return Open(s, ToJson());}
Out LoadDocument::Execute(ILabStore &s) const {return Open(s, ToJson());}
Out SaveProject::Execute(ILabStore &s) const {
    (void)args.at("path").get<std::string>();
    s.StartSave(ToJson());
    return {};
}
Out SaveDocument::Execute(ILabStore &s) const {
    if(std::filesystem::path(args.at("path").get<std::string>()).extension() == ".scapslab") {s.StartSave(ToJson());return {};} // project file: own save lane
    return Job(s, ToJson());                                                                                                    // device YAML: job
}
}
