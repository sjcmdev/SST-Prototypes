#include "ImportWorkflow.hpp"
#include "PlotLabDocument.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>

namespace SST::Prototypes::PlotLab {
namespace {
bool sameTable(const pl::Project& project,pl::Id sheet,const pl::Table& table) {
    const auto columns=project.children(sheet);const auto& worksheet=std::get<pl::Worksheet>(project.object(sheet).data);
    if(columns.size()!=table.columns.size() || !worksheet.masks.empty() || !worksheet.filters.empty())return false;
    if(worksheet.rows.size()!=std::visit([](const auto& v){return v.size();},table.columns.front().values))return false;
    for(std::size_t i=0;i<columns.size();++i) {
        const auto& object=project.object(columns[i]);const auto& a=std::get<pl::Column>(object.data);const auto& b=table.columns[i];
        if(object.name!=table.names[i] || a.role!=b.role || a.units!=b.units || a.longName!=b.longName || a.comments!=b.comments || a.formula || a.masks!=b.masks)return false;
        const bool equal=std::visit([&](const auto& values){using V=std::decay_t<decltype(values)>;const auto* expected=std::get_if<V>(&b.values);if(!expected || values.size()!=expected->size())return false;
            if constexpr(std::is_same_v<typename V::value_type,double>)return std::equal(values.begin(),values.end(),expected->begin(),[](double x,double y){return x==y || (std::isnan(x) && std::isnan(y));});else return values==*expected;},a.values);
        if(!equal)return false;
    }
    return true;
}
class ImportWorker final : public IJob {
public:
    std::filesystem::path path;
    pl::ImportRecipe recipe;
    bool trusted = false, publish = false;
    std::stop_token parentStop;
    std::shared_ptr<const pl::ImportPlan> accepted;
    std::unique_ptr<pl::Project> snapshot;
    std::optional<pl::Operation> operation;
    ImportItemResult result;
    std::string GetName() const override {return publish?"Import accepted data":"Preview import";}
    std::string GetType() const override {return "plotlab.import";}
    bool UsesPythonRuntime() const override {return pl::hasImportHooks(recipe);}
    void Execute(JobContext& context) override {
        pl::WorkContext work{[&]{return context.IsCancellationRequested() || parentStop.stop_requested();},[&](double n){context.SetProgress(static_cast<float>(n),1);}};
        if(accepted) {
            result.plan=accepted;
            try {if(!pl::importSourceUnchanged(*accepted,work))result.diagnostics.push_back({pl::ImportError::Conflict,"Source changed since preview; preview again"});}
            catch(const std::exception& e){result.diagnostics.push_back({work.cancelled()?pl::ImportError::Cancelled:pl::ImportError::File,e.what()});}
        } else result.plan=std::make_shared<const pl::ImportPlan>(pl::planImportFile(path,recipe,trusted?PythonImportHooks(recipe):pl::ImportHooks{},trusted,work));
        if(!result.plan->ready() || !result.diagnostics.empty())return;
        work.check();
        // Existing importTable/Operation own the single undo transaction.
        for(auto book:snapshot->children(snapshot->root())) {
            if(std::holds_alternative<pl::Workbook>(snapshot->object(book).data))for(auto sheet:snapshot->children(book)) {
                const auto& source=std::get<pl::Worksheet>(snapshot->object(sheet).data).source;
                if(source.contains("fingerprint") && source.at("fingerprint")==result.plan->fingerprint &&
                   source.contains("path") && source.at("path")==result.plan->path &&
                   source.contains("recipe_fingerprint") && source.at("recipe_fingerprint")==pl::importRecipeFingerprint(recipe)) {
                    if(snapshot->object(book).name!=result.plan->outputName || !sameTable(*snapshot,sheet,*result.plan->table)) {
                        result.diagnostics.push_back({pl::ImportError::Collision,"This source/recipe already exists with a different name or transformed data; choose a new recipe version/name explicitly"});return;
                    }
                    result.workbook=book;result.worksheet=sheet;result.reused=true;return;
                }
            }
            if(snapshot->object(book).name==result.plan->outputName){result.diagnostics.push_back({pl::ImportError::Collision,"Output name already exists: "+result.plan->outputName});return;}
        }
        if(!publish)return;
        operation=pl::importTable(*snapshot,snapshot->root(),result.plan->outputName,*result.plan->table);
        snapshot->prepareOperation(*operation);
        for(auto id:snapshot->children(snapshot->root()))if(snapshot->object(id).name==result.plan->outputName) {
            result.workbook=id;result.worksheet=snapshot->children(id).front();break;
        }
        work.check();
    }
};
}
class ImportRun {
public:
    ImportRequest request;
    Ref<ImportWorker> worker;
    JobId current = 0;
    Ref<pl::Project> base;
    std::uint64_t revision = 0;
    std::shared_ptr<const ImportResult> result;
    bool cancelRequested = false;
    bool storedPreset = false;
    std::optional<pl::ImportRecipe> presetBefore;
};
class ImportStartCommand final : public ICommand {
    Document& document;
    ImportRequest request;
public:
    ImportStartCommand(Document& d,ImportRequest r):document(d),request(std::move(r)){}
    Result<void> Execute(CommandContext&) override {
        try{document.StartImport(std::move(request));return {};}
        catch(const std::exception& e){return StructuredError{ErrorCategory::Validation,Severity::Error,"Import request failed",e.what(),"Preview the input and check mapping/units.","PlotLab","plotlab.import.validation"};}
    }
    std::string Description() const override {return "Plan / import PlotLab inputs";}
};
JobId Document::ImportCommand(ImportRequest request) {
    const CommandID id{"plotlab.import"};
    if(!commands->HasCommand(id)) {
        auto registered=commands->Register({id,"Import files","PlotLab","",{},CommandFlags::None},[](CommandContext& context)->Unique<ICommand> {
            auto* doc=context.TryGet<Document*>("document");auto* request=context.TryGet<ImportRequest>("request");
            if(!doc || !request)return {};return CreateUnique<ImportStartCommand>(**doc,std::move(*request));
        });
        if(!registered)throw std::runtime_error(registered.Error().technicalDetails);
    }
    CommandContext context;context.Set("document",this);context.Set("request",std::move(request));
    auto result=commands->Execute(id,std::move(context));if(!result)throw std::invalid_argument(result.Error().technicalDetails);
    return importRun->result->job;
}
JobId Document::StartImport(ImportRequest request) {
    if(importRun && importRun->result->running)throw std::invalid_argument("An import is running; cancel it before starting another");
    if(request.paths.empty())throw std::invalid_argument("Select at least one import source");
    for(auto& path:request.paths)path=std::filesystem::absolute(path).lexically_normal();
    if(auto known=importRecipes.find(pl::importRecipeFingerprint(request.recipe));known!=importRecipes.end() && !(*known->second==request.recipe))throw std::invalid_argument("Import recipe fingerprint collision");
    if(request.publish && request.accepted.size()!=request.paths.size())throw std::invalid_argument("Preview every input before Import");
    if(request.publish && (request.previewProject!=project->id() || request.previewRevision!=project->revision()))
        throw std::invalid_argument("Project changed since preview; preview again");
    for(std::size_t i=0;i<request.accepted.size();++i) {
        const auto& plan=request.accepted[i];
        if(!plan || !(plan->recipe==request.recipe) || std::filesystem::u8path(plan->path)!=request.paths.at(i))
            throw std::invalid_argument("Preview settings/source changed; preview again");
    }
    if(!dataJobs){dataJobs=CreateRef<JobSystem>(bus,1);ownsDataJobs=true;}
    importRun=std::make_shared<ImportRun>();importRun->request=std::move(request);importRun->base=project;importRun->revision=project->revision();
    if(auto preset=importPresets.find(importRun->request.recipe.name);preset!=importPresets.end())importRun->presetBefore=preset->second;
    auto result=std::make_shared<ImportResult>();result->total=importRun->request.paths.size();result->project=project->id();result->revision=project->revision();importRun->result=std::move(result);
    StartImportItem();return importRun->result->job;
}
void Document::StartImportItem() {
    auto& run=*importRun;const auto i=run.result->items.size();
    auto worker=CreateRef<ImportWorker>();worker->path=run.request.paths[i];worker->recipe=run.request.recipe;
    worker->parentStop=importStop;
    worker->trusted=ImportRecipeTrusted(worker->recipe);worker->publish=run.request.publish;
    worker->snapshot=project->operationSnapshot();if(worker->publish)worker->accepted=run.request.accepted[i];
    run.worker=worker;run.current=importContext?importContext->SubmitJob(worker):dataJobs->Submit(worker);
    if(!run.current)throw std::runtime_error("Could not submit import job");
    if(!run.result->job){auto result=std::make_shared<ImportResult>(*run.result);result->job=run.current;run.result=std::move(result);}
}
std::shared_ptr<const ImportResult> Document::PollImport() {
    if(!importRun)return {};
    auto& run=*importRun;if(!run.result->running)return run.result;
    const auto status=dataJobs->GetJob(run.current);
    if(!status || status->status==JobStatus::Queued || status->status==JobStatus::Running)return run.result;
    auto result=std::make_shared<ImportResult>(*run.result);
    auto item=std::move(run.worker->result);
    if(status->status!=JobStatus::Completed) {
        item.diagnostics.push_back({status->status==JobStatus::Cancelled?pl::ImportError::Cancelled:pl::ImportError::Hook,status->errorMessage.empty()?"Import cancelled":status->errorMessage});
        result->cancelled=status->status==JobStatus::Cancelled;
    }
    const bool stale=project!=run.base || project->revision()!=run.revision;
    if(stale)item.diagnostics.push_back({pl::ImportError::Conflict,"Project changed during import; preview again"});
    if(item.diagnostics.empty() && run.worker->operation)try {
        project->acceptOperationSnapshot(*run.worker->snapshot);
        Execute(std::move(*run.worker->operation));run.revision=project->revision();
    } catch(const std::exception& e) {item.diagnostics.push_back({pl::ImportError::Conflict,e.what()});}
    if(run.request.publish && item.worksheet && item.diagnostics.empty() && item.plan && item.plan->ready()) {
        const auto fingerprint=pl::importRecipeFingerprint(run.request.recipe);
        bool changed=false;
        if(!importRecipes.contains(fingerprint)){importRecipes.emplace(fingerprint,std::make_shared<const pl::ImportRecipe>(run.request.recipe));changed=true;}
        if(!run.storedPreset) {
            const auto current=importPresets.find(run.request.recipe.name);
            if(run.presetBefore ? current!=importPresets.end() && current->second==*run.presetBefore : current==importPresets.end())
                if(current==importPresets.end() || !(current->second==run.request.recipe)){importPresets[run.request.recipe.name]=run.request.recipe;changed=true;}
            run.storedPreset=true;
        }
        importPresetsDirty|=changed;
    }
    if(!item.diagnostics.empty()){item.workbook=0;item.worksheet=0;}
    if(!run.request.publish && item.plan && item.plan->ready())for(const auto& previous:result->items)
        if(previous.plan && previous.plan->ready() && previous.plan->outputName==item.plan->outputName)
            item.diagnostics.push_back({pl::ImportError::Collision,"Two inputs produce the same output name; use an explicit naming hook"});
    result->items.push_back(std::move(item));
    result->cancelled|=run.cancelRequested;
    result->running=!stale && !result->cancelled && result->items.size()<result->total;
    run.worker->operation.reset();run.worker->snapshot.reset();run.worker->accepted.reset();run.worker.reset();
    run.result=std::move(result);
    if(run.result->running)StartImportItem();
    else {run.request={};run.base.reset();}
    return run.result;
}
void Document::CancelImport() {
    if(importRun && importRun->result->running){importRun->cancelRequested=true;(void)dataJobs->RequestCancel(importRun->current);}
}
void Document::WaitImportItem() {
    if(importContext && importRun && importRun->result->running)(void)importContext->WaitForJob(importRun->current);
}
JobId Document::CommitImport(std::shared_ptr<const ImportResult> preview) {
    if(!preview || preview->running || preview->cancelled || preview->items.size()!=preview->total)throw std::invalid_argument("Wait for a complete preview before Import");
    ImportRequest request;request.publish=true;request.previewRevision=preview->revision;request.previewProject=preview->project;
    for(const auto& item:preview->items) {
        if(!item.plan)throw std::invalid_argument("Preview failed; preview again");
        if(request.paths.empty())request.recipe=item.plan->recipe;
        request.paths.push_back(std::filesystem::u8path(item.plan->path));
        if(item.diagnostics.empty())request.accepted.push_back(item.plan);
        else {auto rejected=std::make_shared<pl::ImportPlan>(*item.plan);rejected->diagnostics.insert(rejected->diagnostics.end(),item.diagnostics.begin(),item.diagnostics.end());request.accepted.push_back(std::move(rejected));}
    }
    return ImportCommand(std::move(request));
}
bool Document::ImportRecipeTrusted(const pl::ImportRecipe& recipe) const {
    return !pl::hasImportHooks(recipe) || (pythonTrusted && std::find(trustedImportRecipes.begin(),trustedImportRecipes.end(),recipe)!=trustedImportRecipes.end());
}
void Document::TrustImportRecipe(const pl::ImportRecipe& recipe) {
    TrustPython();
    if(std::find(trustedImportRecipes.begin(),trustedImportRecipes.end(),recipe)==trustedImportRecipes.end())trustedImportRecipes.push_back(recipe);
}
}
