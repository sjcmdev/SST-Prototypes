#pragma once
#include "AnalysisPanel.hpp"
#include <sst/plotlab/GraphTemplates.hpp>
#include <sst/plotlab/PlotEditing.hpp>
#include <set>

namespace SST::Prototypes::PlotLab {
std::shared_ptr<const pl::GraphSnapshot> PublicationSnapshot(const pl::Project&,pl::Id,const plot::PlotViewState* current=nullptr,std::optional<std::array<double,2>> sizeMm={});
struct ExportRequest {
    std::shared_ptr<const pl::GraphSnapshot> graph;
    std::shared_ptr<const pl::DataExportSnapshot> data;
    std::filesystem::path destination,pythonRoot;
    std::string format;
    double dpi=300;
    std::array<double,3> background{1,1,1};
};
struct BatchPlotStatus {
    bool running=false,cancelled=false;
    std::size_t next=0;
    JobId job=0;
    std::vector<pl::Id> graphs;
    std::vector<std::string> errors;
};
struct MapPreviewStatus {std::shared_ptr<pl::Project> project;pl::Id graph=0;bool running=false;std::string message;};
class MapPreviewJob;
class PlotWorkflow {
public:
    PlotWorkflow(Document&,Ref<JobSystem>);
    ~PlotWorkflow();
    AnalysisController analysis;
    AnalysisPanel panel;
    pl::Id CreateMap(const pl::MapPlotRequest&);
    pl::Id CreateImage(pl::ImageGrid);
    JobId Export(ExportRequest);
    void WaitExport(JobId);
    void StartBatch(pl::BatchPlotPlan,std::string format,std::filesystem::path pythonRoot);
    const BatchPlotStatus& PollBatch();
    const BatchPlotStatus& WaitBatch();
    void CancelBatch();
    void StoreState();
    void PreviewMap(pl::MapPlotRequest);
    const MapPreviewStatus& PollMap();
private:
    Document& doc;
    Ref<JobSystem> jobs;
    pl::BatchPlotPlan batch;
    BatchPlotStatus status;
    std::weak_ptr<pl::Project> owner;
    std::uint64_t revision=0;
    std::string batchFormat;
    std::filesystem::path batchPythonRoot;
    Ref<MapPreviewJob> mapWork;
    MapPreviewStatus mapStatus;
    JobId mapJob=0;
    std::weak_ptr<pl::Project> mapOwner;
    std::uint64_t mapRevision=0;
};
}
