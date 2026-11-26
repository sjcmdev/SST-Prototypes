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
#include <nanobind/stl/array.h>
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
template<class T> T ImportField(const nb::dict& d,const char* key,T fallback) {return d.contains(key)?nb::cast<T>(d[key]):std::move(fallback);}
void ImportKeys(const nb::dict& d,std::initializer_list<std::string_view> keys) {
    for(auto [key,value]:d)if(std::find(keys.begin(),keys.end(),nb::cast<std::string>(key))==keys.end())throw std::invalid_argument("Unknown import field: "+nb::cast<std::string>(key));
}
nb::dict ImportColumnMetadata(const std::string& name,const pl::Column& column) {
    nb::dict d;d["name"]=name;d["units"]=column.units;d["role"]=static_cast<int>(column.role);d["numeric"]=column.values.index()==0;
    return d;
}
nb::dict ImportColumn(const std::string& name,const pl::Column& column,const std::shared_ptr<const pl::Table>& owner) {
    auto d=ImportColumnMetadata(name,column);
    if(const auto* values=std::get_if<std::vector<double>>(&column.values)) {
        if(!owner)throw std::invalid_argument("Missing immutable import table owner");
        auto capsule=nb::capsule(new std::shared_ptr<const pl::Table>(owner),[](void* p) noexcept {delete static_cast<std::shared_ptr<const pl::Table>*>(p);});
        d["values"]=nb::cast(nb::ndarray<nb::numpy,const double>(values->data(),{values->size()},capsule));
    } else d["values"]=nb::cast(std::get<std::vector<std::string>>(column.values));
    return d;
}
nb::dict MoveImportColumn(const std::string& name,pl::Column& column) {
    auto d=ImportColumnMetadata(name,column);
    if(auto* values=std::get_if<std::vector<double>>(&column.values)) {
        auto* buffer=new std::vector<double>(std::move(*values));
        auto capsule=nb::capsule(buffer,[](void* p) noexcept {delete static_cast<std::vector<double>*>(p);});
        d["values"]=nb::cast(nb::ndarray<nb::numpy,double>(buffer->data(),{buffer->size()},capsule));
    } else d["values"]=nb::cast(std::get<std::vector<std::string>>(column.values));
    return d;
}
pl::Column ImportColumnFrom(const nb::dict& d) {
    ImportKeys(d,{"name","units","role","numeric","values"});
    pl::Column column;column.units=ImportField<std::string>(d,"units","");
    const auto role=ImportField<int>(d,"role",0);if(role<0 || role>7)throw std::invalid_argument("Invalid column role");column.role=static_cast<pl::Role>(role);
    if(ImportField<bool>(d,"numeric",true)) {
        auto values=nb::cast<nb::ndarray<nb::numpy,const double,nb::ndim<1>,nb::c_contig>>(d["values"]);
        column.values=values.size()?std::vector<double>(values.data(),values.data()+values.size()):std::vector<double>{};
    }
    else column.values=nb::cast<std::vector<std::string>>(d["values"]);
    column.longName=nb::cast<std::string>(d["name"]);return column;
}
nb::dict ImportTable(const pl::Table& table,std::size_t limit=0,const std::shared_ptr<const pl::Table>& owner={}) {
    nb::dict d;nb::list columns;
    for(std::size_t c=0;c<table.columns.size();++c) {
        const auto& column=table.columns[c];
        if(!limit){columns.append(ImportColumn(table.names[c],column,owner));continue;}
        nb::dict item;item["name"]=table.names[c];item["units"]=column.units;item["role"]=static_cast<int>(column.role);item["numeric"]=column.values.index()==0;nb::list values;
        std::visit([&](const auto& v){for(std::size_t row=0;row<std::min(limit,v.size());++row){using T=typename std::decay_t<decltype(v)>::value_type;if constexpr(std::is_same_v<T,double>){if(std::isfinite(v[row]))values.append(v[row]);else values.append(nb::none());}else values.append(v[row]);}},column.values);
        item["values"]=values;columns.append(item);
    }
    d["columns"]=columns;d["source"]=table.source;return d;
}
nb::dict MoveImportTable(pl::Table& table) {
    nb::dict d;nb::list columns;
    for(std::size_t c=0;c<table.columns.size();++c)columns.append(MoveImportColumn(table.names[c],table.columns[c]));
    d["columns"]=columns;d["source"]=table.source;return d;
}
pl::Table ImportTableFrom(const nb::dict& d) {
    ImportKeys(d,{"columns","source"});
    pl::Table table;table.source=ImportField<pl::Parameters>(d,"source",{});
    for(auto item:nb::cast<nb::list>(d["columns"])) {auto column=nb::cast<nb::dict>(item);table.names.push_back(nb::cast<std::string>(column["name"]));table.columns.push_back(ImportColumnFrom(column));}
    return table;
}
nb::dict ImportRecipeDict(const pl::ImportRecipe& r) {
    nb::dict d;d["name"]=r.name;d["version"]=r.version;d["reference"]=r.reference;d["preamble_lines"]=r.preambleLines;d["timeout_s"]=r.timeoutSeconds;
    d["delimiter"]=r.options.delimiter?std::string(1,r.options.delimiter):"";d["decimal"]=r.options.decimal?std::string(1,r.options.decimal):"";
    d["header"]=static_cast<int>(r.options.header);d["units_row"]=static_cast<int>(r.options.units);d["extra"]=r.options.extra;
    nb::list mapping;for(const auto& m:r.mapping){nb::dict c;c["source"]=m.sourceName;c["name"]=m.outputName;c["units"]=m.units;c["role"]=static_cast<int>(m.role);c["required"]=m.required;c["numeric"]=m.numeric;mapping.append(c);}d["mapping"]=mapping;
    nb::dict hooks;for(std::size_t i=0;i<r.hooks.size();++i){const auto& h=r.hooks[i];nb::dict v;v["source"]=h.source;v["reference"]=h.reference;v["version"]=h.version;v["numbers"]=h.numbers;v["strings"]=h.strings;hooks[pl::importStageNames[i]]=v;}d["hooks"]=hooks;
    return d;
}
pl::ImportRecipe ImportRecipeFrom(const nb::dict& d) {
    ImportKeys(d,{"name","version","reference","preamble_lines","timeout_s","delimiter","decimal","header","units_row","extra","mapping","hooks"});
    pl::ImportRecipe r;r.name=ImportField<std::string>(d,"name","Import");r.version=ImportField<std::string>(d,"version","1");r.reference=ImportField<std::string>(d,"reference","");
    r.preambleLines=ImportField<std::size_t>(d,"preamble_lines",0);r.timeoutSeconds=ImportField<double>(d,"timeout_s",5);
    auto separator=[&](const char* key){auto s=ImportField<std::string>(d,key,"");if(s.size()>1)throw std::invalid_argument("Separator must be one character or empty for Detect");return s.empty()?char{0}:s.front();};
    r.options.delimiter=separator("delimiter");r.options.decimal=separator("decimal");
    auto row=[&](const char* key){auto n=ImportField<int>(d,key,0);if(n<0 || n>2)throw std::invalid_argument("Row mode: 0 Detect, 1 Present, 2 Absent");return static_cast<pl::RowMode>(n);};
    r.options.header=row("header");r.options.units=row("units_row");r.options.extra=ImportField<pl::Parameters>(d,"extra",{});
    if(d.contains("mapping"))for(auto item:nb::cast<nb::list>(d["mapping"])) {
        auto c=nb::cast<nb::dict>(item);pl::ImportMapping m;m.sourceName=nb::cast<std::string>(c["source"]);m.outputName=ImportField<std::string>(c,"name",m.sourceName);m.units=ImportField<std::string>(c,"units","");
        ImportKeys(c,{"source","name","units","role","required","numeric"});
        const auto role=ImportField<int>(c,"role",2);if(role<0 || role>7)throw std::invalid_argument("Invalid mapping role");m.role=static_cast<pl::Role>(role);
        m.required=ImportField<bool>(c,"required",true);m.numeric=ImportField<bool>(c,"numeric",true);r.mapping.push_back(std::move(m));
    }
    if(d.contains("hooks")) {
        auto hooks=nb::cast<nb::dict>(d["hooks"]);
        for(auto [key,value]:hooks) {
            const auto name=nb::cast<std::string>(key);const auto it=std::find(std::begin(pl::importStageNames),std::end(pl::importStageNames),name);
            if(it==std::end(pl::importStageNames))throw std::invalid_argument("Unknown hook stage: "+name);
            auto h=nb::cast<nb::dict>(value);auto& hook=r.hooks[static_cast<std::size_t>(it-std::begin(pl::importStageNames))];
            ImportKeys(h,{"source","reference","version","numbers","strings"});
            hook.source=ImportField<std::string>(h,"source","");hook.reference=ImportField<std::string>(h,"reference","");hook.version=ImportField<std::string>(h,"version","1");
            hook.numbers=ImportField<std::map<std::string,double>>(h,"numbers",{});hook.strings=ImportField<pl::Parameters>(h,"strings",{});
            for(const auto& [name,value]:hook.numbers)if(!std::isfinite(value))throw std::invalid_argument("Hook settings must be finite");
        }
    }
    return r;
}
nb::object RunImportHook(const pl::ImportRecipe& recipe,pl::ImportStage stage,nb::tuple args,const pl::ImportHookContext& context) {
    const auto& hook=recipe.hooks[static_cast<unsigned>(stage)];nb::dict c;
    c["path"]=context.path;c["fingerprint"]=context.fingerprint;c["recipe"]=context.recipeName;c["recipe_version"]=context.recipeVersion;c["preview"]=context.preview;
    std::function<bool()> cancelled=context.work.cancelled?context.work.cancelled:[] {return false;};
    return nb::module_::import_("sst_plotlab.importing").attr("run_hook")(hook.source,pl::importStageNames[static_cast<unsigned>(stage)],args,c,hook.numbers,hook.strings,recipe.timeoutSeconds,cancelled);
}
nb::dict ImportResponse(std::shared_ptr<const ImportResult> result) {
    nb::dict d;d["ok"]=true;
    if(!result){d["state"]="idle";return d;}
    d["job_id"]=result->job;d["running"]=result->running;d["cancelled"]=result->cancelled;d["total"]=result->total;
    nb::list items;
    for(const auto& item:result->items) {
        nb::dict v;nb::list diagnostics,stages;
        auto diagnostic=[&](const pl::ImportDiagnostic& error){nb::dict e;e["code"]=pl::importErrorNames[static_cast<unsigned>(error.code)];e["message"]=error.message;e["column"]=error.column;e["stage"]=pl::importStageNames[static_cast<unsigned>(error.stage)];diagnostics.append(e);};
        for(const auto& e:item.diagnostics)diagnostic(e);
        if(item.plan) {
            const auto& p=*item.plan;v["path"]=p.path;v["fingerprint"]=p.fingerprint;v["output_name"]=p.outputName;v["recipe"]=ImportRecipeDict(p.recipe);v["raw_preview"]=nb::bytes(p.rawPreview.data(),p.rawPreview.size()).attr("decode")("utf-8","replace");
            v["parsed"]=ImportTable(p.parsedPreview,5);v["transformed"]=p.table?ImportTable(*p.table,5):nb::dict();
            v["row_count"]=p.table?std::visit([](const auto& values){return values.size();},p.table->columns.front().values):0;
            for(const auto& e:p.diagnostics)diagnostic(e);
            for(auto s:p.executedHooks)stages.append(pl::importStageNames[static_cast<unsigned>(s)]);
            v["ready"]=p.ready() && item.diagnostics.empty();
        }
        v["workbook_id"]=item.workbook;v["worksheet_id"]=item.worksheet;v["reused"]=item.reused;v["diagnostics"]=diagnostics;v["executed_hooks"]=stages;items.append(v);
    }
    d["items"]=items;return d;
}
nb::dict ImportFailure(const std::exception& error) {nb::dict d,e;e["code"]="validation";e["message"]=error.what();d["ok"]=false;d["error"]=e;return d;}
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

pl::ImportHooks PythonImportHooks(const pl::ImportRecipe& recipe) {
    if(!pl::hasImportHooks(recipe))return {};
#if DS_PYTHON_CAPI_AVAILABLE
    if(!PythonInterpreter::IsRuntimeInitialized())throw std::invalid_argument("Embedded Python is unavailable");
    auto r=std::make_shared<const pl::ImportRecipe>(recipe);pl::ImportHooks hooks;
    hooks.preParse=[r](std::string bytes,const pl::ImportHookContext& c) {
        nb::gil_scoped_acquire gil;Runtime();auto value=RunImportHook(*r,pl::ImportStage::PreParse,nb::make_tuple(nb::bytes(bytes.data(),bytes.size())),c);
        auto output=nb::cast<nb::bytes>(value);return std::string(output.c_str(),output.size());
    };
    hooks.column=[r](std::string& name,pl::Column& column,const pl::ImportHookContext& c) {
        nb::gil_scoped_acquire gil;Runtime();auto value=nb::cast<nb::dict>(RunImportHook(*r,pl::ImportStage::Column,nb::make_tuple(MoveImportColumn(name,column)),c));
        name=nb::cast<std::string>(value["name"]);column=ImportColumnFrom(value);
    };
    hooks.postImport=[r](pl::Table& table,const pl::ImportHookContext& c) {
        nb::gil_scoped_acquire gil;Runtime();table=ImportTableFrom(nb::cast<nb::dict>(RunImportHook(*r,pl::ImportStage::PostImport,nb::make_tuple(MoveImportTable(table)),c)));
    };
    hooks.validation=[r](const pl::Table& table,const pl::ImportHookContext& c) {
        nb::gil_scoped_acquire gil;Runtime();(void)RunImportHook(*r,pl::ImportStage::Validation,nb::make_tuple(ImportTable(table,0,c.tableOwner)),c);
    };
    hooks.naming=[r](const pl::Table& table,std::string name,const pl::ImportHookContext& c) {
        nb::gil_scoped_acquire gil;Runtime();return nb::cast<std::string>(RunImportHook(*r,pl::ImportStage::Naming,nb::make_tuple(ImportTable(table,0,c.tableOwner),name),c));
    };
    return hooks;
#else
    throw std::invalid_argument("Rebuild with embedded Python");
#endif
}
void SaveImportPresets(const std::filesystem::path& path,const std::map<std::string,pl::ImportRecipe>& recipes,const ImportUiState* ui,const ImportRecipes* history) {
#if DS_PYTHON_CAPI_AVAILABLE
    if(!PythonInterpreter::IsRuntimeInitialized())throw std::invalid_argument("Embedded Python is unavailable");
    nb::gil_scoped_acquire gil;Runtime();nb::dict presets;
    for(const auto& [name,recipe]:recipes)presets[nb::str(name.c_str())]=ImportRecipeDict(recipe);
    nb::dict state;if(ui){state["recipe"]=ImportRecipeDict(ui->recipe);nb::list paths;for(const auto& source:ui->paths)paths.append(PathString(source));state["paths"]=paths;state["open"]=ui->open;}
    nb::dict accepted;if(history)for(const auto& [fingerprint,recipe]:*history)accepted[nb::str(fingerprint.c_str())]=ImportRecipeDict(*recipe);
    nb::module_::import_("sst_plotlab.importing").attr("save_presets")(PathString(path),presets,state,accepted);
#else
    (void)path;(void)recipes;(void)ui;(void)history;throw std::invalid_argument("Rebuild with embedded Python");
#endif
}
std::map<std::string,pl::ImportRecipe> LoadImportPresets(const std::filesystem::path& path,ImportUiState* ui,ImportRecipes* history) {
#if DS_PYTHON_CAPI_AVAILABLE
    if(!PythonInterpreter::IsRuntimeInitialized())throw std::invalid_argument("Embedded Python is unavailable");
    nb::gil_scoped_acquire gil;Runtime();std::map<std::string,pl::ImportRecipe> recipes;
    auto bundle=nb::cast<nb::dict>(nb::module_::import_("sst_plotlab.importing").attr("load_bundle")(PathString(path)));
    auto data=nb::cast<nb::dict>(bundle["presets"]);
    for(auto [key,value]:data){auto recipe=ImportRecipeFrom(nb::cast<nb::dict>(value));if(recipe.name!=nb::cast<std::string>(key))throw std::invalid_argument("Preset key/name mismatch");recipes.emplace(recipe.name,std::move(recipe));}
    if(history && bundle.contains("recipes"))for(auto [key,value]:nb::cast<nb::dict>(bundle["recipes"])) {
        auto recipe=ImportRecipeFrom(nb::cast<nb::dict>(value));auto fingerprint=nb::cast<std::string>(key);
        if(fingerprint!=pl::importRecipeFingerprint(recipe))throw std::invalid_argument("Historical recipe fingerprint mismatch");
        history->emplace(std::move(fingerprint),std::make_shared<const pl::ImportRecipe>(std::move(recipe)));
    }
    if(ui && bundle.contains("ui")){auto state=nb::cast<nb::dict>(bundle["ui"]);ImportKeys(state,{"recipe","paths","open"});if(state.contains("recipe")){ui->recipe=ImportRecipeFrom(nb::cast<nb::dict>(state["recipe"]));for(const auto& source:ImportField<std::vector<std::string>>(state,"paths",{}))ui->paths.push_back(std::filesystem::u8path(source));ui->open=ImportField<bool>(state,"open",false);}}
    return recipes;
#else
    (void)path;(void)ui;(void)history;throw std::invalid_argument("Rebuild with embedded Python");
#endif
}
std::shared_ptr<const ImportResult> Document::WaitImport() {
#if DS_PYTHON_CAPI_AVAILABLE
    std::optional<nb::gil_scoped_release> release;if(PythonInterpreter::IsRuntimeInitialized() && PyGILState_Check())release.emplace();
#endif
    auto result=PollImport();while(result && result->running){WaitImportItem();if(!importContext)std::this_thread::sleep_for(std::chrono::milliseconds(1));result=PollImport();}return result;
}
void Document::StopImport() {CancelImport();(void)WaitImport();importRun.reset();}

class PythonWork final : public IJob {
public:
    PythonRequest request;
    Ref<pl::Project> snapshot, base;
    Ref<JobSystem> jobs;
    std::uint64_t revision=0;
    std::vector<pl::Operation> operations;
    std::map<std::string,pl::ImportRecipe> importPresets;
    ImportRecipes importRecipes;
    std::vector<pl::ImportRecipe> importTrust;
    std::optional<std::map<std::string,pl::ImportRecipe>> importPresetChanges;
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
                Document worker(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),CreateRef<EventBus>(),jobs);
                worker.Open(snapshot,{});worker.pythonTrusted=true;worker.activeSheet=request.worksheet;
                worker.importPresets=importPresets;worker.trustedImportRecipes=std::move(importTrust);
                worker.importRecipes=importRecipes;
                worker.importStop=context.GetStopToken();
                worker.importContext=&context;
                worker.InstallPythonEvaluator();
                worker.recordedOperations=&operations;
                std::function<bool()> cancelled=[&context]{return context.IsCancellationRequested();};
                result.output=nb::cast<std::string>(runtime.attr("execute")(nb::cast(&worker,nb::rv_policy::reference),request.source,request.worksheet,request.timeoutSeconds,cancelled,request.parameters,request.consoleSession));
                if(worker.importPresets!=importPresets)importPresetChanges=std::move(worker.importPresets);
                importTrust=std::move(worker.trustedImportRecipes);
                importRecipes=std::move(worker.importRecipes);
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
    work->jobs=dataJobs;
    if(work->request.action==PythonAction::Script || work->request.action==PythonAction::Console){work->importPresets=importPresets;work->importTrust=trustedImportRecipes;}
    if(work->request.action==PythonAction::Script || work->request.action==PythonAction::Console)work->importRecipes=importRecipes;
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
    if(result.ok && work->importPresetChanges && importPresets!=work->importPresets){result.ok=false;result.error="Import presets changed during Python execution; run again";result.errorCode=PythonErrorCode::Conflict;}
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
        if(work->request.action==PythonAction::Script || work->request.action==PythonAction::Console) {
            if(work->importPresetChanges){importPresets=std::move(*work->importPresetChanges);importPresetsDirty=true;}
            for(auto& recipe:work->importTrust)if(std::find(trustedImportRecipes.begin(),trustedImportRecipes.end(),recipe)==trustedImportRecipes.end())trustedImportRecipes.push_back(std::move(recipe));
            for(auto& [fingerprint,recipe]:work->importRecipes)if(!importRecipes.contains(fingerprint)){importRecipes.emplace(fingerprint,std::move(recipe));importPresetsDirty=true;}
        }
    } catch(const std::exception& error) {result.ok=false;result.error=error.what();result.errorCode=PythonErrorCode::Execution;}
    work->operations.clear();work->importPresets.clear();work->importRecipes.clear();work->importPresetChanges.reset();work->importTrust.clear();work->snapshot.reset();work->base.reset();work->jobs.reset();return result;
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
    cls.def_prop_ro("import_recipes",[](Document& d){nb::dict result;for(const auto& [fingerprint,recipe]:d.ImportRecipeHistory())result[nb::str(fingerprint.c_str())]=ImportRecipeDict(*recipe);return result;});
    cls.def("import_preview",[](Document& d,const std::vector<std::string>& paths,nb::dict recipe,bool wait) {
            try {ImportRequest r;r.recipe=ImportRecipeFrom(recipe);for(const auto& p:paths)r.paths.push_back(std::filesystem::u8path(p));d.ImportCommand(std::move(r));return ImportResponse(wait?d.WaitImport():d.PollImport());}
            catch(const std::exception& e){return ImportFailure(e);}
        },nb::arg("paths"),nb::arg("recipe"),nb::arg("wait")=true)
        .def("import_commit",[](Document& d,bool wait) {
            try {d.CommitImport(d.PollImport());return ImportResponse(wait?d.WaitImport():d.PollImport());}catch(const std::exception& e){return ImportFailure(e);}
        },nb::arg("wait")=true)
        .def("import_status",[](Document& d){return ImportResponse(d.PollImport());})
        .def("import_cancel",&Document::CancelImport)
        .def("trust_import_recipe",[](Document& d,nb::dict recipe){d.TrustImportRecipe(ImportRecipeFrom(recipe));})
        .def("save_import_presets",[](Document& d,const std::string& path,nb::dict presets) {
            std::map<std::string,pl::ImportRecipe> recipes;for(auto [name,value]:presets){auto r=ImportRecipeFrom(nb::cast<nb::dict>(value));if(r.name!=nb::cast<std::string>(name))throw std::invalid_argument("Preset key/name mismatch");recipes.emplace(r.name,std::move(r));}
            SaveImportPresets(std::filesystem::u8path(path),recipes);d.importPresets=std::move(recipes);
        })
        .def("load_import_presets",[](Document& d,const std::string& path) {
            auto presets=LoadImportPresets(std::filesystem::u8path(path));nb::dict result;
            for(const auto& [name,r]:presets)result[nb::str(name.c_str())]=ImportRecipeDict(r);d.importPresets=std::move(presets);return result;
        });
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
