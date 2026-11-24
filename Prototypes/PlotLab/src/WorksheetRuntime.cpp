#include "WorksheetPython.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <atomic>
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/function.h>
#endif

namespace SST::Prototypes::PlotLab {
std::string PythonString(std::string_view value) {
    std::string result="'";for(char c:value){if(c=='\\'||c=='\'')result+='\\';if(c=='\n')result+="\\n";else if(c=='\r')result+="\\r";else if(c=='\t')result+="\\t";else result+=c;}return result+"'";
}
namespace {
using Objects=std::map<pl::Id,std::shared_ptr<const pl::Object>>;
struct FormulaView { const Objects* objects; pl::Id activeSheet; bool excludeMasked=false, excludeFiltered=false; pl::Project* snapshot=nullptr; };
#if DS_PYTHON_CAPI_AVAILABLE
namespace nb=nanobind;
auto Runtime() {
    InstallWorksheetPython();
    return nb::module_::import_("sst_plotlab.runtime");
}
auto PathString(const std::filesystem::path& path) {
    const auto text=path.u8string(); return nb::str(reinterpret_cast<const char*>(text.c_str()));
}
nb::dict Info(const Objects& objects,pl::Id id) {
    const auto& o=*objects.at(id);
    nb::dict result;result["id"]=o.id;result["parent"]=o.parent;result["name"]=o.name;result["type"]=o.data.index();
    result["long_name"]=o.longName;
    if(const auto* script=std::get_if<pl::Script>(&o.data)){result["source"]=script->source;result["parameters"]=script->parameters;}
    if(const auto* c=std::get_if<pl::Column>(&o.data)) {
        result["long_name"]=c->longName;result["units"]=c->units;result["comments"]=c->comments;
        const char* roles[]={"None","X","Y","Z","yErr","Label","xErr","Disregard"};result["role"]=roles[static_cast<unsigned>(c->role)];
    }
    return result;
}
std::vector<pl::Id> Ids(const Objects& objects,int type,pl::Id parent) {
    std::vector<pl::Id> result;
    for(const auto& [id,o]:objects)if(o->data.index()==type && (!parent || o->parent==parent))result.push_back(id);
    std::stable_sort(result.begin(),result.end(),[&](auto a,auto b){return objects.at(a)->order<objects.at(b)->order;});return result;
}
nb::object Values(const Objects& objects,pl::Id id) {
    auto owner=objects.at(id);const auto& values=std::get<pl::Column>(owner->data).values;
    if(const auto* numbers=std::get_if<std::vector<double>>(&values)) {
        auto capsule=nb::capsule(new std::shared_ptr<const pl::Object>(std::move(owner)),[](void* p) noexcept {delete static_cast<std::shared_ptr<const pl::Object>*>(p);});
        return nb::cast(nb::ndarray<nb::numpy,const double>(numbers->data(),{numbers->size()},capsule));
    }
    return nb::cast(std::get<std::vector<std::string>>(values));
}
std::vector<pl::Id> Rows(const Objects& objects,pl::Id sheet) {return std::get<pl::Worksheet>(objects.at(sheet)->data).rows;}
std::vector<bool> Masks(const Objects& objects,pl::Id id) {
    const auto& o=*objects.at(id);const auto& c=std::get<pl::Column>(o.data);const auto& s=std::get<pl::Worksheet>(objects.at(o.parent)->data);
    std::vector<bool> result;result.reserve(s.rows.size());for(auto row:s.rows)result.push_back(s.masks.contains(row)||c.masks.contains(row));return result;
}
using FormulaOutput=std::tuple<std::vector<double>,std::map<std::string,pl::Id>,std::map<std::string,pl::Id>>;
FormulaOutput Evaluate(const Objects& objects,pl::Id worksheet,const pl::Formula& f,std::stop_token stop,double timeout=2,std::size_t limit=0,pl::Project* snapshot=nullptr) {
    nb::gil_scoped_acquire gil;FormulaView view{&objects,worksheet,f.excludeMasked,f.excludeFiltered,snapshot};
    // Native startup can reach this bridge before the application's bindings were installed.
    auto runtime=Runtime();
    const auto rows=std::get<pl::Worksheet>(objects.at(worksheet)->data).rows.size();
    std::function<bool()> cancelled=[stop]{return stop.stop_requested();};
    return nb::cast<FormulaOutput>(runtime.attr("formula")(nb::cast(&view,nb::rv_policy::reference),f.expression,worksheet,rows,f.parameterBindings,cancelled,timeout,limit));
}
#endif
}

class PythonWork final : public IJob {
public:
    PythonRequest request;
    Ref<pl::Project> snapshot, base;
    std::uint64_t revision=0;
    std::vector<pl::Operation> operations;
    pl::Formula formula;
    PythonResult result;
    bool published=false;
    std::string GetName() const override {return request.action==PythonAction::Script?"Run worksheet Python":"Worksheet Python preview / completion";}
    std::string GetType() const override {return "plotlab.python";}
    bool UsesPythonRuntime() const override {return true;}
    void Execute(JobContext& context) override {
        context.ThrowIfCancellationRequested();
#if DS_PYTHON_CAPI_AVAILABLE
        if(!PythonInterpreter::IsRuntimeInitialized())throw std::runtime_error("Embedded Python is unavailable");
        nb::gil_scoped_acquire gil;
        try {
            auto runtime=Runtime();
            if(request.action==PythonAction::Script || request.action==PythonAction::Console) {
                Document worker(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),CreateRef<EventBus>());
                worker.Open(snapshot,{});worker.pythonTrusted=true;worker.activeSheet=request.worksheet;
                worker.InstallPythonEvaluator();
                worker.recordedOperations=&operations;
                std::function<bool()> cancelled=[&context]{return context.IsCancellationRequested();};
                result.output=nb::cast<std::string>(runtime.attr("execute")(nb::cast(&worker,nb::rv_policy::reference),request.source,request.worksheet,request.timeoutSeconds,cancelled,request.parameters,request.consoleSession));
            } else if(request.action==PythonAction::Completion) {
                FormulaView view{&snapshot->objects(),request.worksheet};
                const auto entries=nb::cast<std::vector<std::tuple<std::string,std::string,std::string>>>(runtime.attr("complete")(nb::cast(&view,nb::rv_policy::reference),request.source,request.worksheet,request.consoleSession));
                for(const auto& [insert,signature,doc]:entries)result.completions.push_back({insert,signature,doc});
            } else if(request.action==PythonAction::Parameters) {
                const auto params=nb::cast<std::vector<std::tuple<std::string,double,double,double,bool>>>(runtime.attr("parameters")(request.source));
                for(const auto& [name,value,lo,hi,slider]:params)result.parameters.push_back({name,value,lo,hi,slider});
            } else {
                Document evaluator(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),CreateRef<EventBus>());
                evaluator.Open(snapshot,{});evaluator.pythonTrusted=true;evaluator.InstallPythonEvaluator();
                formula=request.formula;formula.expression=request.source;formula.language=pl::FormulaLanguage::Python;
                const auto& previous=std::get<pl::Column>(snapshot->object(request.column).data).formula;
                if(previous && previous->language==pl::FormulaLanguage::Python)formula.parameterBindings=previous->parameterBindings;
                auto [values,reads,references]=Evaluate(snapshot->objects(),request.worksheet,formula,context.GetStopToken(),request.timeoutSeconds,20,snapshot.get());
                formula.bindings=std::move(reads);formula.parameterBindings=std::move(references);formula.bindingDiagnostic.clear();
                result.preview.assign(values.begin(),values.begin()+std::min(values.size(),std::size_t{20}));
                if(request.action==PythonAction::Formula) {
                    snapshot->worksheetEdit(pl::SetColumnValues{request.column,formula,false}).apply(*snapshot);
                }
            }
            context.ThrowIfCancellationRequested();result.ok=true;
        } catch(const nb::python_error& error) {
            result.error=error.what();
            // Keep the native editor line jump independent of traceback formatting.
            try {
                auto value=error.value();if(nb::hasattr(value,"lineno") && !value.attr("lineno").is_none())result.line=nb::cast<int>(value.attr("lineno"));
                if(nb::hasattr(value,"row"))result.row=nb::cast<int>(value.attr("row"));
                if(nb::hasattr(value,"line"))result.line=nb::cast<int>(value.attr("line"));
                if(nb::hasattr(value,"plotlab_output"))result.output=nb::cast<std::string>(value.attr("plotlab_output"));
                auto tb=error.traceback();while(!tb.is_none()) {
                    if(nb::cast<std::string>(tb.attr("tb_frame").attr("f_code").attr("co_filename"))=="<PlotLab script>")result.line=nb::cast<int>(tb.attr("tb_lineno"));
                    tb=tb.attr("tb_next");
                }
            } catch(...) {}
        } catch(const std::exception& error) {result.error=error.what();}
#else
        result.error="Embedded Python is unavailable";
#endif
        result.running=false;
        if(!result.ok)result.errorCode=result.error.find("time limit")!=std::string::npos?PythonErrorCode::Timeout:result.error.find("Cancelled")!=std::string::npos?PythonErrorCode::Cancelled:PythonErrorCode::Execution;
    }
};

bool Document::HasPython() const {
    for(const auto& [id,o]:project->objects()) {
        if(std::holds_alternative<pl::Script>(o->data))return true;
        if(const auto* c=std::get_if<pl::Column>(&o->data);c && c->formula && c->formula->language==pl::FormulaLanguage::Python)return true;
    }
    return false;
}
void Document::InstallPythonEvaluator() {
#if DS_PYTHON_CAPI_AVAILABLE
    std::optional<nb::gil_scoped_release> release;if(PythonInterpreter::IsRuntimeInitialized() && PyGILState_Check())release.emplace();
#endif
    project->setPythonFormulaRunner(pythonTrusted ? pl::Project::PythonFormulaRunner{
        [](const auto& formula,auto sheet,const auto& objects,auto rows,auto stop) {
#if DS_PYTHON_CAPI_AVAILABLE
            (void)rows;auto [values,reads,references]=Evaluate(objects,sheet,formula,stop);return pl::PythonFormulaResult{std::move(values),std::move(reads),std::move(references)};
#else
            (void)formula;(void)sheet;(void)objects;(void)rows;(void)stop;
            throw std::runtime_error("Embedded Python is unavailable");return pl::PythonFormulaResult{};
#endif
        }}:pl::Project::PythonFormulaRunner{});
}
void Document::TrustPython() {
#if DS_PYTHON_CAPI_AVAILABLE
    if(!PythonInterpreter::IsRuntimeInitialized())throw std::runtime_error("Embedded Python is unavailable");
    if(!path.empty())PythonBinding::RememberTrustedFile(path);
#endif
    pythonTrusted=true;InstallPythonEvaluator();(void)project->recompute();
}
JobId Document::SubmitPython(PythonRequest request) {
    if(!pythonTrusted && request.action!=PythonAction::Completion && request.action!=PythonAction::Parameters)throw std::invalid_argument("Trust this project to enable Python");
    if(!std::isfinite(request.timeoutSeconds)||request.timeoutSeconds<=0||request.timeoutSeconds>30)throw std::invalid_argument("Python time limit must be in (0,30] seconds");
    const auto limit=request.action==PythonAction::Formula || request.action==PythonAction::Preview?16384u:1048576u;
    if(request.source.size()>limit)throw std::invalid_argument("Python source is too long");
    for(const auto& [name,value]:request.parameters)if(!std::isfinite(value))throw std::invalid_argument("Python parameters must be finite");
    if(request.action==PythonAction::Console){static std::atomic_uint64_t next{1};if(!pythonSession)pythonSession=next++;request.consoleSession=pythonSession;}
    else if(request.action==PythonAction::Completion && request.consoleSession)request.consoleSession=pythonSession;
    if(!dataJobs){dataJobs=CreateRef<JobSystem>(bus,1);ownsDataJobs=true;}
    auto work=CreateRef<PythonWork>();work->request=std::move(request);work->base=project;work->revision=project->revision();
    work->snapshot=Ref<pl::Project>(project->operationSnapshot().release());
    const auto job=dataJobs->Submit(work);if(!job)throw std::runtime_error("Could not submit Python job");
    // Completed UI requests are disposable; callers retain the latest result only.
    if(pythonWork.size()>32)std::erase_if(pythonWork,[](const auto& item){return item.second->published;});
    pythonWork[job]=std::move(work);return job;
}
const PythonResult& Document::PollPython(JobId job) {
    auto work=pythonWork.at(job);
    if(work->published)return work->result;
    const auto status=dataJobs->GetJob(job);
    if(!status || (status->status!=JobStatus::Completed && status->status!=JobStatus::Failed && status->status!=JobStatus::Cancelled)) {static const PythonResult running;return running;}
    work->published=true;auto& result=work->result;result.running=false;
    if(status->status!=JobStatus::Completed) {result.ok=false;result.error=status->errorMessage.empty()?"Python cancelled":status->errorMessage;result.errorCode=status->status==JobStatus::Cancelled?PythonErrorCode::Cancelled:PythonErrorCode::Execution;}
    if(result.ok && (work->base!=project || work->revision!=project->revision())) {result.ok=false;result.error="Project changed during Python execution; run again";result.errorCode=PythonErrorCode::Conflict;}
    if(result.ok)try {
        if((work->request.action==PythonAction::Script || work->request.action==PythonAction::Console) && !work->operations.empty()) {
            project->acceptScriptSnapshot(*work->snapshot);
            auto group=undo->ScopedGroup("Python script");const auto automatic=project->autoRecompute();project->setAutoRecompute(false);
            try {for(auto& operation:work->operations)Execute(std::move(operation));auto committed=group.Commit();if(!committed)throw std::runtime_error(committed.Error().technicalDetails);}
            catch(...) {const auto cancelled=group.Cancel();project->setAutoRecompute(automatic);if(!cancelled)throw std::runtime_error(cancelled.Error().technicalDetails);throw;}
            project->setAutoRecompute(automatic);
        } else if(work->request.action==PythonAction::Formula) {
            Execute(project->worksheetEdit(pl::SetColumnValues{work->request.column,std::move(work->formula),false}));
            RecordPython("project.by_id("+std::to_string(work->request.column)+").set_values("+PythonString(work->request.source)+", mode='python')");
        }
    } catch(const std::exception& error) {result.ok=false;result.error=error.what();result.errorCode=PythonErrorCode::Execution;}
    work->operations.clear();work->snapshot.reset();work->base.reset();return result;
}
PythonResult Document::WaitPython(JobId job) {
#if DS_PYTHON_CAPI_AVAILABLE
    std::optional<nb::gil_scoped_release> release;if(PythonInterpreter::IsRuntimeInitialized() && PyGILState_Check())release.emplace();
#endif
    while(PollPython(job).running)std::this_thread::sleep_for(std::chrono::milliseconds(1));return PollPython(job);
}
void Document::CancelPython(JobId job) {if(job && dataJobs)(void)dataJobs->RequestCancel(job);}
void Document::PollPythonJobs() {for(const auto& [id,work]:pythonWork)if(!work->published)(void)PollPython(id);}
void Document::ResetPython() {
    for(const auto& [id,work]:pythonWork) {
        CancelPython(id);const auto status=dataJobs->GetJob(id);
        if(status && status->status!=JobStatus::Running && status->status!=JobStatus::Queued){work->operations.clear();work->snapshot.reset();work->base.reset();work->published=true;}
    }
#if DS_PYTHON_CAPI_AVAILABLE
    if(pythonSession && PythonInterpreter::IsRuntimeInitialized()) {nb::gil_scoped_acquire gil;Runtime().attr("_consoles").attr("pop")(pythonSession,nb::none());}
#endif
    pythonSession=0;
}
void Document::RecordPython(std::string line) {if(!recordedOperations)pythonHistory.push_back(std::move(line));}
void Document::BeginPythonGroup(std::string name) {
    auto result=undo->BeginGroup(std::move(name));if(!result)throw std::runtime_error(result.Error().technicalDetails);
    recordingGroups.push_back(recordedOperations?recordedOperations->size():0);
}
void Document::EndPythonGroup(bool commit) {
    if(recordingGroups.empty())throw std::invalid_argument("No Python edit group");
    const auto first=recordingGroups.back();recordingGroups.pop_back();auto result=commit?undo->EndGroup():undo->CancelGroup();
    if(!commit && recordedOperations)recordedOperations->resize(first);
    if(!result)throw std::runtime_error(result.Error().technicalDetails);
}
void Document::GrowRows(pl::Id sheet,std::size_t count) {
    auto s=project->object(sheet);auto& rows=std::get<pl::Worksheet>(s.data).rows;
    if(count<=rows.size())return;if(count>10000000)throw std::invalid_argument("Worksheet row limit exceeded");
    while(rows.size()<count)rows.push_back(project->reserveId());std::vector<pl::Object> updates{std::move(s)};
    for(auto id:project->children(sheet)) {
        auto o=project->object(id);auto& c=std::get<pl::Column>(o.data);
        if(c.formula)continue; // The engine resizes derived caches with worksheet row identities.
        std::visit([&](auto& values){using T=typename std::decay_t<decltype(values)>::value_type;if constexpr(std::is_same_v<T,double>)values.resize(count,std::numeric_limits<double>::quiet_NaN());else values.resize(count);},c.values);
        updates.push_back(std::move(o));
    }
    Execute(project->change("Grow worksheet rows",std::move(updates)));
}
void Document::SetValues(pl::Id id,pl::Values values) {
    const auto sheet=project->object(id).parent;const auto count=std::visit([](const auto& v){return v.size();},values);
    BeginPythonGroup("Set column data");try {
        GrowRows(sheet,count);
        const auto rows=std::get<pl::Worksheet>(project->object(sheet).data).rows.size();
        std::visit([&](auto& v){using T=typename std::decay_t<decltype(v)>::value_type;if constexpr(std::is_same_v<T,double>)v.resize(rows,std::numeric_limits<double>::quiet_NaN());else v.resize(rows);},values);
        Execute(project->setCells(id,0,std::move(values)));EndPythonGroup(true);
    } catch(...) {if(!recordingGroups.empty())EndPythonGroup(false);throw;}
}
void Document::SetMasks(pl::Id id,const std::vector<bool>& masks) {
    const auto& node=project->object(id);const auto& rows=std::get<pl::Worksheet>(project->object(node.parent).data).rows;
    if(masks.size()!=rows.size())throw std::invalid_argument("Mask length must match worksheet rows");
    auto sheet=project->object(node.parent);auto& rowMasks=std::get<pl::Worksheet>(sheet.data).masks;
    std::vector<pl::Object> updates;
    for(auto column:project->children(node.parent)) {
        auto o=project->object(column);auto& c=std::get<pl::Column>(o.data);
        if(column==id) {c.masks.clear();for(std::size_t i=0;i<rows.size();++i)if(masks[i])c.masks[rows[i]]=0xff0000ff;}
        else for(const auto& [row,color]:rowMasks)c.masks.try_emplace(row,color);
        if(column==id || !rowMasks.empty())updates.push_back(std::move(o));
    }
    if(!rowMasks.empty()){rowMasks.clear();updates.push_back(std::move(sheet));}
    Execute(project->change("Set column mask",std::move(updates)));
}
pl::Id Document::SaveScript(std::string name,std::string source,pl::Id id) {
    if(name.empty())throw std::invalid_argument("Script name cannot be empty");
    if(id && !std::holds_alternative<pl::Script>(project->object(id).data))throw std::invalid_argument("Expected script ID");
    auto parameters=id?std::get<pl::Script>(project->object(id).data).parameters:std::map<std::string,double>{};
    if(!id)id=project->reserveId();Execute(project->change("Save Python script",{{id,project->root(),std::move(name),0,pl::Script{std::move(source),std::move(parameters)},id}}));return id;
}

#if DS_PYTHON_CAPI_AVAILABLE
namespace PythonBinding {
bool TrustedFile(const std::filesystem::path& path) {if(!PythonInterpreter::IsRuntimeInitialized())return false;nb::gil_scoped_acquire gil;return nb::cast<bool>(Runtime().attr("is_trusted")(PathString(path)));}
void RememberTrustedFile(const std::filesystem::path& path) {nb::gil_scoped_acquire gil;Runtime().attr("remember_trust")(PathString(path));}
void BindWorksheetRuntime(nb::module_& module,nb::class_<Document>& cls) {
    nb::class_<FormulaView>(module,"FormulaView")
        .def_prop_ro("project_id",[](FormulaView&){return pl::Id{0};})
        .def_prop_ro("active_sheet",[](FormulaView& v){return v.activeSheet;})
        .def("info",[](FormulaView& v,pl::Id id){return Info(*v.objects,id);})
        .def("ids",[](FormulaView& v,int type,pl::Id parent){return Ids(*v.objects,type,parent);})
        .def("values",[](FormulaView& v,pl::Id id)->nb::object {
            if(v.snapshot && std::get<pl::Column>(v.objects->at(id)->data).formula){nb::gil_scoped_release release;v.snapshot->recompute().wait();}
            const auto& o=*v.objects->at(id);const auto& c=std::get<pl::Column>(o.data);
            if((!v.excludeMasked && !v.excludeFiltered) || c.values.index()!=0)return Values(*v.objects,id);
            const auto& sheet=std::get<pl::Worksheet>(v.objects->at(o.parent)->data);
            if((!v.excludeMasked || (sheet.masks.empty() && c.masks.empty())) && (!v.excludeFiltered || sheet.filters.empty()))return Values(*v.objects,id);
            auto values=new std::vector<double>(std::get<std::vector<double>>(c.values));
            for(std::size_t row=0;row<values->size();++row)if((v.excludeMasked && (sheet.masks.contains(sheet.rows[row]) || c.masks.contains(sheet.rows[row]))) || (v.excludeFiltered && !sheet.filters.empty() && !std::binary_search(sheet.visibleRows.begin(),sheet.visibleRows.end(),row)))(*values)[row]=std::numeric_limits<double>::quiet_NaN();
            auto owner=nb::capsule(values,[](void* p)noexcept{delete static_cast<std::vector<double>*>(p);});
            return nb::cast(nb::ndarray<nb::numpy,const double>(values->data(),{values->size()},owner));
        })
        .def("rows",[](FormulaView& v,pl::Id id){return Rows(*v.objects,id);})
        .def("masks",[](FormulaView& v,pl::Id id){return Masks(*v.objects,id);});
    nb::class_<PythonResult>(module,"PythonResult")
        .def_ro("ok",&PythonResult::ok).def_ro("running",&PythonResult::running).def_ro("output",&PythonResult::output).def_ro("error",&PythonResult::error).def_ro("line",&PythonResult::line).def_ro("row",&PythonResult::row).def_ro("preview",&PythonResult::preview)
        .def_prop_ro("error_code",[](const PythonResult& r){const char* codes[]={"","execution","cancelled","timeout","conflict"};return codes[static_cast<unsigned>(r.errorCode)];});
    cls.def_rw("active_sheet",&Document::activeSheet)
        .def_prop_ro("project_id",[](Document& d){return d.project->id();})
        .def_prop_ro("trusted",&Document::PythonTrusted).def("trust",&Document::TrustPython)
        .def("info",[](Document& d,pl::Id id){return Info(d.project->objects(),id);})
        .def("ids",[](Document& d,int type,pl::Id parent){return Ids(d.project->objects(),type,parent);})
        .def("values",[](Document& d,pl::Id id){
            if(d.PythonTrusted() && std::get<pl::Column>(d.project->object(id).data).formula){nb::gil_scoped_release release;d.project->recompute().wait();}
            else d.project->pollRecompute();return Values(d.project->objects(),id);
        })
        .def("rows",[](Document& d,pl::Id id){return Rows(d.project->objects(),id);})
        .def("masks",[](Document& d,pl::Id id){return Masks(d.project->objects(),id);})
        .def("begin",&Document::BeginPythonGroup).def("end",&Document::EndPythonGroup)
        .def("create",[](Document& d,pl::Id parent,int type,const std::string& name){return d.Create(parent?parent:d.project->root(),type,name);})
        .def("rename",&Document::Rename).def("delete",&Document::Delete)
        .def("grow",&Document::GrowRows)
        .def("add_column",[](Document& d,pl::Id sheet,const std::string& name,bool text){const auto id=d.AddColumn(sheet,text);d.Rename(id,name);return id;})
        .def("set_values",[](Document& d,pl::Id id,nb::list values){
            if(std::get<pl::Column>(d.project->object(id).data).values.index()==0)d.SetValues(id,nb::cast<std::vector<double>>(values));
            else d.SetValues(id,nb::cast<std::vector<std::string>>(values));
        })
        .def("set_cells",[](Document& d,pl::Id id,std::size_t first,nb::list values){
            const auto count=std::get<pl::Worksheet>(d.project->object(d.project->object(id).parent).data).rows.size();if(first>count || nb::len(values)>count-first)throw std::invalid_argument("Cell range exceeds worksheet rows");
            if(std::get<pl::Column>(d.project->object(id).data).values.index()==0)d.Execute(d.project->setCells(id,first,nb::cast<std::vector<double>>(values)));
            else d.Execute(d.project->setCells(id,first,nb::cast<std::vector<std::string>>(values)));
        })
        .def("set_masks",&Document::SetMasks)
        .def("metadata",[](Document& d,pl::Id id,const std::string& key,const std::string& value){
            if(key=="long_name" && !std::holds_alternative<pl::Column>(d.project->object(id).data)) {auto object=d.project->object(id);object.longName=value;d.Execute(d.project->change("Set display name",{std::move(object)}));return;}
            pl::ColumnSettings settings;settings.column=id;
            if(key=="units")settings.units=value;else if(key=="long_name")settings.longName=value;else if(key=="comments")settings.comments=value;
            else if(key=="role") {const std::vector<std::string> roles={"None","X","Y","Z","yErr","Label","xErr","Disregard"};const auto it=std::find(roles.begin(),roles.end(),value);if(it==roles.end())throw std::invalid_argument("Unknown column role");settings.role=static_cast<pl::Role>(it-roles.begin());}
            else throw std::invalid_argument("Unknown column metadata");d.WorksheetCommand(std::move(settings));
        })
        .def("sort",[](Document& d,pl::Id sheet,pl::Id column,bool descending){pl::DataRequest r;r.worksheet=sheet;r.column=column;r.keys={{column,descending}};auto result=d.WorksheetCommand(std::move(r));if(result.error)throw std::runtime_error(result.message);})
        .def("undo",[](Document& d){auto r=d.undo->Undo();if(!r)throw std::runtime_error(r.Error().technicalDetails);})
        .def("redo",[](Document& d){auto r=d.undo->Redo();if(!r)throw std::runtime_error(r.Error().technicalDetails);})
        .def_prop_ro("undo_depth",[](Document& d){return d.undo->GetUndoDepth();})
        .def("run_python",[](Document& d,const std::string& source,pl::Id sheet,double timeout,const std::map<std::string,double>& parameters){PythonRequest r;r.source=source;r.worksheet=sheet;r.timeoutSeconds=timeout;r.parameters=parameters;return d.WaitPython(d.SubmitPython(std::move(r)));},nb::arg("source"),nb::arg("worksheet_id"),nb::arg("timeout_s")=5,nb::arg("parameters")=std::map<std::string,double>{})
        .def("console",[](Document& d,const std::string& source,pl::Id sheet){PythonRequest r;r.action=PythonAction::Console;r.source=source;r.worksheet=sheet;return d.WaitPython(d.SubmitPython(std::move(r)));})
        .def("set_formula",[](Document& d,pl::Id id,const std::string& source,const std::string& mode,const std::string& recalculate){
            pl::Formula f;f.expression=source;const std::vector<std::string> modes={"None","Auto","Manual"};auto it=std::find(modes.begin(),modes.end(),recalculate);if(it==modes.end())throw std::invalid_argument("Invalid recalculate mode");f.recalculate=static_cast<pl::RecalculateMode>(it-modes.begin());
            if(mode=="origin"){d.SetColumnValues(id,std::move(f));return PythonResult{false,true};}
            if(mode!="python")throw std::invalid_argument("Formula mode must be python or origin");PythonRequest r;r.action=PythonAction::Formula;r.source=source;r.column=id;r.worksheet=d.project->object(id).parent;r.formula=std::move(f);auto result=d.WaitPython(d.SubmitPython(std::move(r)));if(!result.ok)throw std::runtime_error(result.error);return result;
        })
        .def("recompute",[](Document& d){nb::gil_scoped_release release;d.project->recompute().wait();})
        .def("complete",[](Document& d,const std::string& source,pl::Id sheet,bool console){PythonRequest r;r.action=PythonAction::Completion;r.source=source;r.worksheet=sheet;r.consoleSession=console?1:0;auto result=d.WaitPython(d.SubmitPython(std::move(r)));std::vector<std::tuple<std::string,std::string,std::string>> out;for(auto& c:result.completions)out.emplace_back(c.insert,c.signature,c.documentation);return out;},nb::arg("source"),nb::arg("worksheet_id"),nb::arg("console")=false)
        .def("save_script",&Document::SaveScript,nb::arg("name"),nb::arg("source"),nb::arg("id")=0)
        .def("set_script_parameters",[](Document& d,pl::Id id,const std::map<std::string,double>& parameters){for(const auto& [name,value]:parameters)if(name.empty()||!std::isfinite(value))throw std::invalid_argument("Expected named finite parameters");auto object=d.project->object(id);std::get<pl::Script>(object.data).parameters=parameters;d.Execute(d.project->change("Set script parameters",{std::move(object)}));})
        .def("save",[](Document& d,const std::string& path){d.Save(Path(path).Native());})
        .def("open",[](Document& d,const std::string& path){d.Open(Path(path).Native());});
}
}
#endif
}
