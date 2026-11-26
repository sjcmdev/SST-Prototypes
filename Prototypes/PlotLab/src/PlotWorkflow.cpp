#include "PlotWorkflow.hpp"
#include "PlotLabExport.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <chrono>
#include <thread>

namespace SST::Prototypes::PlotLab {
plot::PlotViewState CapturePublicationView(const plot::PlotViewState& v) {
    // Only publication state crosses to a worker; contour/axis caches can contain large source-derived arrays.
    return {.viewport=v.viewport,.legendLayout=v.legendLayout,.frame=v.frame,.visibility=v.visibility,.excluded=v.excluded,.fitted=v.fitted,.xTicks=v.xTicks,.yTicks=v.yTicks,.zTicks=v.zTicks,.camera=v.camera};
}
std::shared_ptr<const pl::GraphSnapshot> PublicationSnapshot(const pl::Project& p,pl::Id graph,const plot::PlotViewState* current,std::optional<std::array<double,2>> sizeMm) {
    const auto layer=p.children(graph).front();const auto& settings=std::get<pl::Layer>(p.object(layer).data);auto view=CapturePublicationView(current?*current:settings.view);
    if(!view.viewport) {
        std::vector<plot::SeriesEntryView> series;for(auto id:p.children(layer))series.push_back(p.series(id));
        view.viewport=plot::ResolveBounds(settings.spec,series.data(),series.size(),view);
        if(!view.viewport)throw std::invalid_argument("Graph has no finite visible values in the axis domains");
    }
    return pl::snapshotGraph(p,graph,view,sizeMm);
}
namespace {
class FileExportJob final:public IJob {
public:
    ExportRequest request;
    std::stop_token parentStop;
    std::string GetName() const override {return "Export PlotLab file";}
    std::string GetType() const override {return "plotlab.export";}
    bool UsesPythonRuntime() const override {return true;}
    void Execute(JobContext& context) override {
        context.ThrowIfCancellationRequested();if(parentStop.stop_requested())throw JobCancelledException();context.SetStage("Writing publication/data file");
        if(request.graph)ExportGraph(*request.graph,request.destination,request.format,request.dpi,request.pythonRoot,request.background);
        else ExportData(*request.data,request.destination,request.format,request.pythonRoot,[&]{return context.IsCancellationRequested() || parentStop.stop_requested();});
        request.graph.reset();request.data.reset();context.SetProgress(1,1);
    }
};
struct ExportSubmission {Ref<JobSystem> jobs;Ref<FileExportJob> work;JobId id=0;JobContext* context=nullptr;};
class ExportCommand final:public ICommand {
    Ref<ExportSubmission> submission;
public:
    explicit ExportCommand(Ref<ExportSubmission> s):submission(std::move(s)) {}
    Result<void> Execute(CommandContext&) override {submission->id=submission->context?submission->context->SubmitJob(submission->work):submission->jobs->Submit(submission->work);return {};}
    std::string Description() const override {return "Export PlotLab file";}
};
pl::Id NewGraph(const pl::Project& project,const std::set<pl::Id>& before) {
    for(const auto& [id,o]:project.objects())if(std::holds_alternative<pl::Graph>(o->data) && !before.contains(id))return id;
    throw std::runtime_error("No graph was created");
}
std::set<pl::Id> GraphIds(const pl::Project& p) {
    std::set<pl::Id> ids;for(const auto& [id,o]:p.objects())if(std::holds_alternative<pl::Graph>(o->data))ids.insert(id);return ids;
}
}
PlotWorkflow::PlotWorkflow(Document& d,Ref<JobSystem> pool):analysis(d,pool),panel(analysis),doc(d),jobs(std::move(pool)) {
    panel.SetRecipe(doc.workflowState.recipe);panel.open=doc.workflowState.analysisOpen;
    panel.previewView=doc.workflowState.previewView;panel.residualView=doc.workflowState.residualView;
    if(!doc.commands->HasCommand(CommandID{"plotlab.export.file"})) {
        auto result=doc.commands->Register({CommandID{"plotlab.export.file"},"Export publication/data","PlotLab","",{},CommandFlags::AsyncCapable},[](CommandContext& c)->Unique<ICommand> {
            auto s=c.TryGet<Ref<ExportSubmission>>("export.submission");return s?CreateUnique<ExportCommand>(*s):nullptr;
        });
        if(!result)throw std::runtime_error(result.Error().technicalDetails);
    }
}
PlotWorkflow::~PlotWorkflow() {CancelBatch();if(mapJob)(void)jobs->RequestCancel(mapJob);}
void PlotWorkflow::StoreState() {doc.workflowState.recipe=panel.Recipe();doc.workflowState.analysisOpen=panel.open;doc.workflowState.previewView=CapturePublicationView(panel.previewView);doc.workflowState.residualView=CapturePublicationView(panel.residualView);}
pl::Id PlotWorkflow::CreateMap(const pl::MapPlotRequest& request) {auto ids=GraphIds(*doc.project);doc.Execute(pl::createMapPlot(*doc.project,doc.project->root(),request));return NewGraph(*doc.project,ids);}
pl::Id PlotWorkflow::CreateImage(pl::ImageGrid grid) {auto ids=GraphIds(*doc.project);doc.Execute(pl::createImageMap(*doc.project,doc.project->root(),std::move(grid)));return NewGraph(*doc.project,ids);}
JobId PlotWorkflow::Export(ExportRequest request) {
    if((!request.graph)==(!request.data))throw std::invalid_argument("Choose exactly one graph or data snapshot");
    if(std::filesystem::exists(request.destination))for(const auto& [id,object]:doc.project->objects())if(const auto* sheet=std::get_if<pl::Worksheet>(&object->data)) {
        for(const auto& [key,path]:sheet->source)if(key=="path" || key=="file" || key=="filename") {
            std::error_code error;
            const auto source=std::filesystem::path(std::u8string(path.begin(),path.end()));
            if(std::filesystem::equivalent(request.destination,source,error))throw std::invalid_argument("Export cannot replace a source file; choose a different destination");
        }
    }
    auto s=CreateRef<ExportSubmission>();s->jobs=jobs;s->work=CreateRef<FileExportJob>();s->work->request=std::move(request);
    s->context=doc.ActiveJobContext();if(s->context)s->work->parentStop=s->context->GetStopToken();
    CommandContext c;c.Set("export.submission",s);auto result=doc.commands->Execute(CommandID{"plotlab.export.file"},std::move(c));
    if(!result)throw std::runtime_error(result.Error().technicalDetails);return s->id;
}
void PlotWorkflow::WaitExport(JobId id) {
    if(auto parent=doc.ActiveJobContext())(void)parent->WaitForJob(id);
    for(;;) {auto job=jobs->GetJob(id);if(!job)throw std::invalid_argument("Unknown export job");
        if(job->status==JobStatus::Completed)return;
        if(job->status==JobStatus::Failed || job->status==JobStatus::Cancelled)throw std::runtime_error(job->errorMessage.empty()?"Export cancelled":job->errorMessage);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void PlotWorkflow::StartBatch(pl::BatchPlotPlan plan,std::string format,std::filesystem::path pythonRoot) {
    if(status.running)throw std::invalid_argument("A template batch is already running");
    if(!plan.valid() || plan.items.empty())throw std::invalid_argument("Resolve every template mapping and output collision before Run");
    for(const auto& item:plan.items) {
        if(!pl::mapGraphTemplate(*doc.project,item.worksheet,*plan.graphTemplate,item.mapping.columns).valid())throw std::invalid_argument("Template source changed; preflight again");
        if(!item.destination.empty() && std::filesystem::exists(item.destination))throw std::invalid_argument("Batch destination appeared after preflight; choose another output");
    }
    batch=std::move(plan);batchFormat=std::move(format);batchPythonRoot=std::move(pythonRoot);owner=doc.project;revision=doc.project->revision();status={};status.running=true;
}
const BatchPlotStatus& PlotWorkflow::PollBatch() {
    if(!status.running)return status;
    if(status.job) {
        auto job=jobs->GetJob(status.job);if(job && (job->status==JobStatus::Queued || job->status==JobStatus::Running))return status;
        if(!job || job->status!=JobStatus::Completed)status.errors.push_back(job?job->errorMessage:"Export job disappeared");
        status.job=0;
    }
    if(status.cancelled || status.next==batch.items.size()) {status.running=false;return status;}
    if(owner.lock()!=doc.project || revision!=doc.project->revision()) {status.errors.push_back("Project changed; preflight the remaining batch again");status.running=false;return status;}
    const auto index=status.next++;
    try {
        if(!batch.items[index].destination.empty() && std::filesystem::exists(batch.items[index].destination))throw std::invalid_argument("Batch destination already exists");
        auto ids=GraphIds(*doc.project);doc.Execute(pl::applyBatchPlotItem(*doc.project,doc.project->root(),batch,index));
        revision=doc.project->revision();auto graph=NewGraph(*doc.project,ids);status.graphs.push_back(graph);
        const auto& item=batch.items[index];
        if(!item.destination.empty()) {
            ExportRequest r;r.graph=doc.SnapshotGraph(graph);r.destination=item.destination;r.format=batchFormat;r.pythonRoot=batchPythonRoot;status.job=Export(std::move(r));
        }
    } catch(const std::exception& e) {status.errors.push_back(e.what());}
    return status;
}
const BatchPlotStatus& PlotWorkflow::WaitBatch() {while(PollBatch().running){if(status.job && doc.ActiveJobContext())(void)doc.ActiveJobContext()->WaitForJob(status.job);else std::this_thread::sleep_for(std::chrono::milliseconds(1));}return status;}
void PlotWorkflow::CancelBatch() {status.cancelled=true;if(status.job)(void)jobs->RequestCancel(status.job);}
class MapPreviewJob final:public IJob {
public:
    std::shared_ptr<pl::Project> project;
    pl::MapPlotRequest request;
    pl::Id graph=0;
    std::string GetName() const override {return "Preview map / contours";}
    std::string GetType() const override {return "plotlab.map.preview";}
    void Execute(JobContext& context) override {
        context.ThrowIfCancellationRequested();auto ids=GraphIds(*project);
        pl::createMapPlot(*project,project->root(),request).apply(*project);graph=NewGraph(*project,ids);context.ThrowIfCancellationRequested();
    }
};
void PlotWorkflow::PreviewMap(pl::MapPlotRequest request) {
    if(mapJob)(void)jobs->RequestCancel(mapJob);mapWork=CreateRef<MapPreviewJob>();mapWork->request=std::move(request);
    mapWork->project=std::shared_ptr<pl::Project>(doc.project->operationSnapshot().release());mapOwner=doc.project;mapRevision=doc.project->revision();
    mapJob=jobs->Submit(mapWork);mapStatus.running=true;mapStatus.message="Computing map preview";
}
const MapPreviewStatus& PlotWorkflow::PollMap() {
    if(!mapWork)return mapStatus;auto job=jobs->GetJob(mapJob);
    if(!job || job->status==JobStatus::Queued || job->status==JobStatus::Running)return mapStatus;
    mapStatus.running=false;
    if(mapOwner.lock()!=doc.project || mapRevision!=doc.project->revision())mapStatus.message="Source changed; edit a setting to recalculate";
    else if(job->status==JobStatus::Completed) {mapStatus.project=std::move(mapWork->project);mapStatus.graph=mapWork->graph;mapStatus.message.clear();}
    else mapStatus.message=job->errorMessage;
    mapWork.reset();return mapStatus;
}
}
