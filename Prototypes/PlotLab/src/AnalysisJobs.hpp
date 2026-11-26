#pragma once
#include "PlotLabDocument.hpp"
#include <sst/plotlab/Analysis.hpp>

namespace SST::Prototypes::PlotLab {
struct AnalysisSource {
    std::shared_ptr<const pl::Object> worksheet, x, y, sigma;
    std::vector<unsigned char> excluded;
    pl::Id project=0;
    std::uint64_t revision=0;
    pl::AnalysisInput View() const;
};
AnalysisSource CaptureAnalysisSource(const pl::Project&,const pl::AnalysisRecipe&);
struct AnalysisJobItem {std::string id;pl::AnalysisRecipe recipe;AnalysisSource source;};
struct AnalysisJobResult {
    std::vector<pl::AnalysisBatchResult> items;
    std::vector<pl::AnalysisRecipe> recipes;
    pl::Id project=0;
    std::uint64_t revision=0;
    std::size_t snapshotNumericCopiedBytes=0, maskCopiedBytes=0;
    double elapsedMs=0;
};
class AnalysisJob final : public IJob {
public:
    std::vector<AnalysisJobItem> items;
    pl::UserAnalysisModel userModel;
    std::stop_token parentStop;
    std::shared_ptr<const AnalysisJobResult> result;
    std::string GetName() const override {return "PlotLab scientific analysis";}
    std::string GetType() const override {return "plotlab.analysis";}
    bool UsesPythonRuntime() const override {return static_cast<bool>(userModel.evaluate);}
    void Execute(JobContext&) override;
};
enum class AnalysisStage {Idle,Running,PreviewReady,Accepted,Failed,Cancelled,Stale};
struct AnalysisStatus {
    AnalysisStage stage=AnalysisStage::Idle;
    pl::AnalysisError error=pl::AnalysisError::None;
    std::string message;
    JobId job=0;
    bool exact=true;
    // These remain visible during recompute/cancel/failure. No per-frame data copies.
    std::shared_ptr<const AnalysisJobResult> preview, accepted;
    std::size_t publishedCopiedBytes=0;
    std::vector<pl::Id> outputWorksheets;
};
// Owner-thread controller. GUI and Python call Submit -> Core command -> Core job -> Poll/Accept.
// The host owns this alongside Document and passes its existing shared JobSystem.
class AnalysisController {
public:
    AnalysisController(Document&,Ref<JobSystem>);
    ~AnalysisController();
    AnalysisController(const AnalysisController&)=delete;
    AnalysisController& operator=(const AnalysisController&)=delete;
    JobId Submit(pl::AnalysisRecipe,pl::UserAnalysisModel = {},bool preview=false);
    JobId SubmitBatch(std::vector<std::pair<std::string,pl::AnalysisRecipe>>,pl::UserAnalysisModel = {},bool preview=false);
    const AnalysisStatus& Poll();
    const AnalysisStatus& Wait();
    void Cancel();
    void Accept(pl::Id folder=0);
    const AnalysisStatus& Status() const {return status;}
    Document& document;
private:
    Ref<JobSystem> jobs;
    Ref<AnalysisJob> work;
    std::weak_ptr<pl::Project> base;
    AnalysisStatus status;
    bool coarsePreview=false;
};
}
