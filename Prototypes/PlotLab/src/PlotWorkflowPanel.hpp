#pragma once
#include "PlotWorkflow.hpp"
namespace SST::Prototypes::PlotLab {
struct Session;
class PlotWorkflowPanel {
public:
    explicit PlotWorkflowPanel(Session&);
    void OpenMap(pl::Id worksheet);
    void EditMap(pl::Id graph);
    void OpenAnalysis(pl::Id worksheet);
    void OpenTemplates() {templatesOpen=true;}
    void OpenData(pl::Id worksheet) {if(dataSheet!=worksheet)dataColumns.clear();dataSheet=worksheet;dataOpen=true;}
    void OpenImage() {imageOpen=true;}
    void Render();
private:
    Session& session;
    int frame=-1;
    std::uint64_t analysisRevision=0;
    bool mapOpen=false,templatesOpen=false,dataOpen=false,imageOpen=false;
    pl::MapPlotRequest map;
    pl::Id mapLayer=0;
    plot::PlotViewState mapView;
    pl::Id dataSheet=0;
    std::set<pl::Id> dataColumns,batchSheets;
    std::map<pl::Id,std::map<std::string,pl::Id>> templateBindings;
    int dataFormat=0,rowScope=0,maskPolicy=0,missingPolicy=0,batchFormat=0;
    std::string message;
    std::shared_ptr<const pl::GraphTemplate> graphTemplate;
    std::optional<pl::BatchPlotPlan> plan;
    std::filesystem::path outputDirectory;
    pl::ImageGrid calibration;
    std::filesystem::path imageFile;
    void Maps();
    void Templates();
    void Data();
    void Image();
    template<class F>void Attempt(F&& f) {try{f();message.clear();}catch(const std::exception& e){message=e.what();}}
};
}
