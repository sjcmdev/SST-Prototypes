#include "AnalysisJobs.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <thread>

namespace SST::Prototypes::PlotLab {
namespace {
struct Submission {Ref<JobSystem> jobs;Ref<AnalysisJob> work;JobId id=0;JobContext* context=nullptr;};
class SubmitCommand final : public ICommand {
public:
    explicit SubmitCommand(Ref<Submission> submission):submission(std::move(submission)) {}
    Result<void> Execute(CommandContext&) override {submission->id=submission->context?submission->context->SubmitJob(submission->work):submission->jobs->Submit(submission->work);return {};}
    std::string Description() const override {return "Run scientific analysis";}
private:
    Ref<Submission> submission;
};
const pl::Column& Column(const std::shared_ptr<const pl::Object>& object) {return std::get<pl::Column>(object->data);}
}
pl::AnalysisInput AnalysisSource::View() const {
    pl::AnalysisInput input;
    input.x=std::get<std::vector<double>>(Column(x).values);input.y=std::get<std::vector<double>>(Column(y).values);
    input.xUnits=Column(x).units;input.yUnits=Column(y).units;
    if(sigma) {input.sigma=std::get<std::vector<double>>(Column(sigma).values);input.sigmaUnits=Column(sigma).units;input.sigmaColumn=sigma->id;}
    input.rowIds=std::get<pl::Worksheet>(worksheet->data).rows;input.excluded=excluded;
    input.project=project;input.revision=revision;input.worksheet=worksheet->id;input.xColumn=x->id;input.yColumn=y->id;return input;
}
AnalysisSource CaptureAnalysisSource(const pl::Project& project,const pl::AnalysisRecipe& recipe) {
    if(recipe.id.empty() || recipe.version!=1)throw std::invalid_argument("Expected a stable analysis recipe ID and version 1");
    AnalysisSource source;source.project=project.id();source.revision=project.revision();
    source.worksheet=project.objects().at(recipe.worksheet);
    const auto& sheet=std::get<pl::Worksheet>(source.worksheet->data);
    auto capture=[&](pl::Id id) {
        auto object=project.objects().at(id);
        if(object->parent!=recipe.worksheet || !std::holds_alternative<std::vector<double>>(Column(object).values) ||
           std::get<std::vector<double>>(Column(object).values).size()!=sheet.rows.size())
            throw std::invalid_argument("Analysis needs numeric columns from the selected worksheet");
        return object;
    };
    source.x=capture(recipe.xColumn);source.y=capture(recipe.yColumn);
    if(recipe.settings.weights!=pl::WeightSemantics::Unweighted)source.sigma=capture(recipe.sigmaColumn);
    source.excluded.resize(sheet.rows.size());
    std::set<std::size_t> visible;
    if(recipe.excludeFiltered && !sheet.filters.empty())visible.insert(sheet.visibleRows.begin(),sheet.visibleRows.end());
    for(std::size_t i=0;i<sheet.rows.size();++i) {
        const auto row=sheet.rows[i];source.excluded[i]=sheet.masks.contains(row) || Column(source.x).masks.contains(row) ||
            Column(source.y).masks.contains(row) || (source.sigma && Column(source.sigma).masks.contains(row)) ||
            (recipe.excludeFiltered && !sheet.filters.empty() && !visible.contains(i));
    }
    return source;
}
void AnalysisJob::Execute(JobContext& context) {
    const auto cancelled=[&]{return context.IsCancellationRequested() || parentStop.stop_requested();};
    const auto checkCancellation=[&]{if(cancelled())throw JobCancelledException();};
    // Release immutable column owners when work ends, even while Core keeps job metadata.
    auto inputs=std::move(items);
    if(inputs.empty())throw std::invalid_argument("Resubmit analysis with a fresh source snapshot");
    auto output=std::make_shared<AnalysisJobResult>();
    output->project=inputs.front().source.project;output->revision=inputs.front().source.revision;
    const auto start=std::chrono::steady_clock::now();
    auto model=userModel;
    if(globalRecipe) {
        std::vector<pl::GlobalAnalysisItem> datasets;
        for(const auto& item:inputs){output->maskCopiedBytes+=item.source.excluded.size();datasets.push_back({item.id,item.source.View(),item.recipe.settings.parameters});output->recipes.push_back(*globalRecipe);}
        auto computed=pl::analyzeGlobal(datasets,*globalRecipe,{cancelled,{}},model);
        if(computed.fit.error==pl::AnalysisError::Cancelled)checkCancellation();
        output->items=std::move(computed.items);
        output->items.push_back({globalRecipe->id+".global",globalRecipe->id,std::make_shared<const pl::ScientificResult>(std::move(computed.fit))});
        output->recipes.resize(output->items.size(),*globalRecipe);
        context.SetProgress(1,1);
    } else
    for(auto& item:inputs) {
        checkCancellation();output->maskCopiedBytes+=item.source.excluded.size();
        auto computed=pl::analyze(item.source.View(),item.recipe.settings,{cancelled,{}},model);
        if(computed.error==pl::AnalysisError::Cancelled)checkCancellation();
        output->items.push_back({std::move(item.id),item.recipe.id,std::make_shared<const pl::ScientificResult>(std::move(computed))});
        output->recipes.push_back(std::move(item.recipe));
        context.SetProgress(static_cast<float>(output->items.size()),static_cast<float>(inputs.size()));
        item.source={};
    }
    checkCancellation();
    output->elapsedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();result=std::move(output);
}
AnalysisController::AnalysisController(Document& doc,Ref<JobSystem> pool):document(doc),jobs(std::move(pool)) {
    if(!jobs)throw std::invalid_argument("Pass the shared Core JobSystem to analysis");
    if(!document.commands->HasCommand(CommandID{"plotlab.analysis.run"})) {
        auto registered=document.commands->Register({CommandID{"plotlab.analysis.run"},"Run scientific analysis","PlotLab","",{},CommandFlags::AsyncCapable},
            [](CommandContext& context)->Unique<ICommand> {
                const auto* submission=context.TryGet<Ref<Submission>>("analysis.submission");
                return submission?CreateUnique<SubmitCommand>(*submission):nullptr;
            });
        if(!registered)throw std::runtime_error(registered.Error().technicalDetails);
    }
}
AnalysisController::~AnalysisController() {if(work)(void)jobs->RequestCancel(status.job);}
JobId AnalysisController::Submit(pl::AnalysisRecipe recipe,pl::UserAnalysisModel model,bool preview) {
    const auto id=recipe.id;std::vector<std::pair<std::string,pl::AnalysisRecipe>> batch;batch.emplace_back(id,std::move(recipe));
    if(!batch.front().second.globalInputs.empty()) {
        auto global=batch.front().second;
        for(const auto& input:global.globalInputs) {
            auto local=global;local.globalInputs.clear();local.worksheet=input.worksheet;local.xColumn=input.xColumn;local.yColumn=input.yColumn;local.sigmaColumn=input.sigmaColumn;
            if(!input.parameters.empty())local.settings.parameters=input.parameters;
            batch.emplace_back(input.id,std::move(local));
        }
        batch.front().second.globalInputs.clear();
        if(preview)global.settings.tolerance=std::max(global.settings.tolerance,1e-5);
        return SubmitBatch(std::move(batch),std::move(model),preview,std::move(global));
    }
    return SubmitBatch(std::move(batch),std::move(model),preview);
}
JobId AnalysisController::SubmitBatch(std::vector<std::pair<std::string,pl::AnalysisRecipe>> recipes,pl::UserAnalysisModel model,bool preview,std::optional<pl::AnalysisRecipe> global) {
    if(recipes.empty())throw std::invalid_argument("Select at least one analysis source");
    const auto sourceProject=document.project;
    auto next=CreateRef<AnalysisJob>();next->globalRecipe=std::move(global);std::set<std::string> ids;
    for(auto& [id,recipe]:recipes) {
        if(id.empty() || !ids.insert(id).second)throw std::invalid_argument("Analysis batch item IDs must be nonempty and unique");
        if(!recipe.globalInputs.empty())throw std::invalid_argument("Submit global recipes individually, not as independent batch items");
        if(recipe.settings.operation==pl::ScientificOperation::UserFit && !document.PythonTrusted())
            throw pl::AnalysisException(pl::AnalysisError::Trust,"Trust this project before executing its user model");
        auto source=CaptureAnalysisSource(*sourceProject,recipe);
        if(preview)recipe.settings.tolerance=std::max(recipe.settings.tolerance,1e-5);
        next->items.push_back({std::move(id),std::move(recipe),std::move(source)});
    }
    if(std::any_of(next->items.begin(),next->items.end(),[](const auto& item){return item.recipe.settings.operation==pl::ScientificOperation::UserFit;}))next->userModel=std::move(model);
    auto submission=CreateRef<Submission>();submission->jobs=jobs;submission->work=next;
    submission->context=document.ActiveJobContext();if(submission->context)next->parentStop=submission->context->GetStopToken();
    CommandContext context;context.Set("analysis.submission",submission);
    const auto outcome=document.commands->Execute(CommandID{"plotlab.analysis.run"},std::move(context));
    if(!outcome)throw std::runtime_error(outcome.Error().technicalDetails);
    Cancel();base=sourceProject;coarsePreview=preview;work=std::move(next);
    status.job=submission->id;status.exact=!preview;status.stage=AnalysisStage::Running;status.error=pl::AnalysisError::None;status.message="Computing analysis";return status.job;
}
void AnalysisController::Cancel() {
    if(work) {(void)jobs->RequestCancel(status.job);work.reset();status.stage=AnalysisStage::Cancelled;status.error=pl::AnalysisError::Cancelled;status.message="Analysis cancelled";}
}
const AnalysisStatus& AnalysisController::Poll() {
    if(!work) {
        if(status.stage==AnalysisStage::PreviewReady && (base.lock()!=document.project || status.preview->revision!=document.project->revision())) {
            status.stage=AnalysisStage::Stale;status.error=pl::AnalysisError::Stale;status.message="Source changed; recalculate analysis";
        }
        return status;
    }
    const auto job=jobs->GetJob(status.job);
    if(!job || job->status==JobStatus::Queued || job->status==JobStatus::Running)return status;
    auto finished=std::move(work);
    if(job->status==JobStatus::Cancelled || job->cancellationRequested) {status.stage=AnalysisStage::Cancelled;status.error=pl::AnalysisError::Cancelled;status.message="Analysis cancelled";return status;}
    if(base.lock()!=document.project || (finished->result && finished->result->revision!=document.project->revision())) {status.stage=AnalysisStage::Stale;status.error=pl::AnalysisError::Stale;status.message="Source changed; recalculate analysis";return status;}
    if(job->status==JobStatus::Failed || !finished->result) {status.stage=AnalysisStage::Failed;status.error=pl::AnalysisError::Model;status.message=job->errorMessage.empty()?"Analysis failed without diagnostic details.":job->errorMessage;return status;}
    const auto& items=finished->result->items;
    const auto ok=std::count_if(items.begin(),items.end(),[](const auto& item){return item.result->ok();});
    if(!ok) {status.stage=AnalysisStage::Failed;status.error=items.front().result->error;status.message=items.front().result->message.empty()?"Analysis failed without diagnostic details.":items.front().result->message;return status;}
    status.preview=std::move(finished->result);status.stage=AnalysisStage::PreviewReady;status.error=pl::AnalysisError::None;
    status.message=coarsePreview?"Live preview; release control for exact result":"Exact result ready to accept";return status;
}
const AnalysisStatus& AnalysisController::Wait() {if(work && document.ActiveJobContext())(void)document.ActiveJobContext()->WaitForJob(status.job);while(Poll().stage==AnalysisStage::Running)std::this_thread::sleep_for(std::chrono::milliseconds(1));return status;}
void AnalysisController::Accept(pl::Id folder) {
    Poll();
    if(coarsePreview || status.stage!=AnalysisStage::PreviewReady || !status.preview)throw std::invalid_argument("Wait for an exact successful result before accepting");
    if(base.lock()!=document.project || status.preview->revision!=document.project->revision())throw std::invalid_argument("Source changed; recalculate analysis");
    auto& project=*document.project;if(!folder)folder=project.root();
    std::vector<pl::Object> objects;std::vector<pl::Id> sheets;std::size_t copied=0;
    std::map<pl::Id,std::shared_ptr<const pl::AnalysisRecipe>> recipes;
    for(std::size_t k=0;k<status.preview->items.size();++k) {
        const auto& item=status.preview->items[k];const auto& result=*item.result;if(!result.ok())continue;
        const auto book=project.reserveId();auto sheet=project.reserveId();
        recipes.emplace(book,std::make_shared<const pl::AnalysisRecipe>(status.preview->recipes.at(k)));
        pl::Worksheet worksheet;auto count=result.values.size();
        worksheet.rows.reserve(count);
        worksheet.source={{"analysis.model_id",result.modelId},{"analysis.recipe_id",item.recipeId},{"analysis.item_id",item.id}};
        for(std::size_t i=0;i<count;++i)worksheet.rows.push_back(project.reserveId());
        objects.push_back({book,folder,item.id+" analysis",0,pl::Workbook{}});
        if(count) {sheets.push_back(sheet);objects.push_back({sheet,book,"Analysis",0,std::move(worksheet)});}
        std::size_t order=0;
        auto column=[&](std::string name,const std::vector<double>& values,const std::string& units,pl::Role role=pl::Role::Y) {
            if(values.empty())return;
            pl::Column c;c.units=units;c.role=role;c.values=values;copied+=values.size()*sizeof(double);
            std::get<std::vector<double>>(c.values).resize(count,std::numeric_limits<double>::quiet_NaN());
            objects.push_back({project.reserveId(),sheet,std::move(name),0,std::move(c),order++});
        };
        column("X",result.x,result.xUnits,pl::Role::X);column(result.settings.operation==pl::ScientificOperation::Extrapolate?"Extrapolated (linear endpoints)":"Analysis",result.values,result.outputUnits);
        column("Residual",result.residuals,result.yUnits);column("Normalized residual",result.normalizedResiduals,"");column("Baseline",result.baseline,result.yUnits);
        for(std::size_t j=0;j<result.components.size();++j)column(result.settings.peaks.at(j).id+" component",result.components[j],result.yUnits);
        if(!result.peakX.empty()) {
            sheet=project.reserveId();sheets.push_back(sheet);count=result.peakX.size();order=0;pl::Worksheet report;
            for(std::size_t i=0;i<count;++i)report.rows.push_back(project.reserveId());report.source={{"analysis.model_id",result.modelId},{"analysis.recipe_id",item.recipeId},{"analysis.item_id",item.id}};
            objects.push_back({sheet,book,"Peak report",0,std::move(report)});column("Position",result.peakX,result.xUnits,pl::Role::X);column("Peak Y",result.peakY,result.yUnits);column("Height",result.peakHeight,result.yUnits);column("FWHM",result.peakFwhm,result.xUnits);
        }
        if(!result.parameters.empty()) {
            // Keep scalar reports separate; never pad every parameter to the curve's row count.
            sheet=project.reserveId();sheets.push_back(sheet);count=1;order=0;
            pl::Worksheet report;report.rows={project.reserveId()};
            report.source={{"analysis.model_id",result.modelId},{"analysis.recipe_id",item.recipeId},{"analysis.item_id",item.id}};
            objects.push_back({sheet,book,"Fit report",0,std::move(report)});
            for(std::size_t j=0;j<result.parameters.size();++j) {
                const auto& definition=result.parameterDefinitions[j];column(definition.name,{result.parameters[j]},definition.units);
                column(definition.name+" standard error",{result.standardErrors[j]},definition.units);
            }
            column(result.globalFit?"Global objective":"Objective",{result.objective},result.settings.weights==pl::WeightSemantics::Unweighted?"("+result.yUnits+")^2":"");
            column(result.globalFit?"Global degrees of freedom":"Degrees of freedom",{static_cast<double>(result.degreesOfFreedom)},"");
            column("Used points",{static_cast<double>(result.usedPoints)},"");column("Excluded points",{static_cast<double>(result.excludedPoints)},"");
            if(result.modelId=="global" && result.covariance.size()==result.parameters.size()*result.parameters.size()) {
                count=result.parameters.size();sheet=project.reserveId();sheets.push_back(sheet);order=0;pl::Worksheet covariance;
                for(std::size_t i=0;i<count;++i)covariance.rows.push_back(project.reserveId());
                covariance.source={{"analysis.recipe_id",item.recipeId},{"analysis.model_id","global"}};objects.push_back({sheet,book,"Global covariance",0,std::move(covariance)});
                pl::Column labels;labels.role=pl::Role::Label;std::vector<std::string> names;
                for(const auto& p:result.parameterDefinitions)names.push_back(p.name+" ["+p.units+"]");labels.values=std::move(names);objects.push_back({project.reserveId(),sheet,"Row parameter",0,std::move(labels),order++});
                for(std::size_t j=0;j<count;++j){std::vector<double> values;values.reserve(count);for(std::size_t i=0;i<count;++i)values.push_back(result.covariance[i*count+j]);const auto& p=result.parameterDefinitions[j];column(p.name,values,p.units+" * row parameter unit");}
            }
        }
    }
    document.Execute(project.change("Accept scientific analysis",std::move(objects)));
    document.workflowState.acceptedRecipes.insert(recipes.begin(),recipes.end());
    status.accepted=status.preview;status.outputWorksheets=std::move(sheets);status.publishedCopiedBytes=copied;status.stage=AnalysisStage::Accepted;status.message="Analysis accepted";
}
}
