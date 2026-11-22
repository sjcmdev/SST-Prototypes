#pragma once
#include "Core/IO/ImportPreview.hpp"
#include "Core/Layer.hpp"
#include "Core/App/ProjectWorkspace.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Domain/CapLabImport.hpp"
#include "Domain/CapLabAnalysis.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "IO/LabASImporter.hpp"
#include <sst/plotlab/GraphSnapshot.hpp>
namespace SST::Prototypes::CapLab
{
    enum class Operation { Preview, PreviewFolder, Open, OpenOrImport, Save, SaveAs, New, Close, Export, Analyze, Plot, Reimport, Relink };
    struct DocumentRequested : BusEvent { Operation operation = Operation::Preview; Path target; std::vector<Path> sources; bool recursive = false; };
    struct DocumentChanged : BusEvent { pl::Id project; std::uint64_t revision; };
    class DocumentJob final : public IJob
    {
    public:
        DocumentJob(Operation operation, Project input, Path path = {}) : operation(operation), input(std::move(input)), path(std::move(path)) {}
        std::string GetName() const override { return "CapLab document"; }
        std::string GetType() const override { return "caplab.document"; }
        void Execute(JobContext &context) override;
        Operation operation;
        Project input;
        Path path;
        Json preview;
        std::vector<Path> sources;
        bool recursive = false;
        std::vector<ImportPreviewItem<Json>> sourcePreviews;
        Json analysisOptions;
        std::optional<sst::ui::plot::PlotViewState> plotView;
        pl::Id measurementId=0;
        std::shared_ptr<const LabASImporter> importer = std::make_shared<LabASImporter>();
        std::optional<Project> opened;
        char decimal = '.';
        Json importOptions = Json::object();
        bool saveCommitted = false;
        GraphExportResult graphExport;
        std::shared_ptr<const pl::GraphSnapshot> snapshot;
        std::vector<std::shared_ptr<const pl::GraphSnapshot>> snapshots;
        double dpi = 300;
    };
    class CapLabWorkflow final : public Layer
    {
    public:
        Json pendingCandidate;
        explicit CapLabWorkflow(Ref<JobSystem> jobs, Ref<EventBus> bus = {}, Ref<UndoStack> undo = {})
            : Layer("CapLabWorkflow"), jobs(std::move(jobs)), bus(std::move(bus)), undo(std::move(undo))
        { (void)workspace.Open(CapLabDocument{}); }
        void Start(Operation operation, Path target = {}, std::vector<Path> sources = {}, bool recursive = false);
        void SelectPreview(int index);
        void ClearPreview();
        void OnUpdate(float) override;
        void OnEvent(Event &event) override;
        void Changed(bool inputsChanged = true);
        Project EditSnapshot() const;
        void Select(const Json &request);
        void ReconcileSelection();
        void StoreGraphView(pl::Id graph, const sst::ui::plot::PlotViewState &value);
        void ShowGraph(pl::Id graph);
        sst::ui::plot::PlotViewState GraphView(pl::Id graph) const;
        Json SelectionJson() const;
        Json ImportGroups() const;
        pl::Id importTargetSample = 0;
        pl::Id sourceMeasurement=0;
        bool metadataConfirmed=false;
        bool sourceMetadataConfirmed=false;
        std::optional<Project> sourceDraft;
        Json sourceDiff;
        std::uint64_t sourceRevision=0;
        bool sourceRelink=false;
        std::uint64_t profileRevision=0;
        ProjectWorkspace<CapLabDocument> workspace;
        CapLabDocument &Document() { return *workspace.Find(workspace.active); }
        const CapLabDocument &Document() const { return *workspace.Find(workspace.active); }
        bool Activate(std::uint64_t handle);
        bool CloseProject(bool discard = false);
        bool RequestClose(bool application = false);
        void ResolveClose(const std::string &decision, Path target = {});
        bool IsDirty() const;
        bool closeRequested = false, closeApplication = false, closeReady = false;
        std::vector<Path> recent;
        Json WorkspaceJson() const;
        void RestoreWorkspace(const Json &value);
        Result<void> WorkspaceRequest(const Json &request);
        Ref<JobSystem> jobs;
        Ref<EventBus> bus;
        Ref<UndoStack> undo;
        std::shared_ptr<const LabASImporter> labImporter = std::make_shared<LabASImporter>();
        JobId activeJob = 0;
        std::string status;
        std::optional<StructuredError> failure;
        Json analysisOptions = Json::object();
        std::optional<sst::ui::plot::PlotViewState> plotView;
        bool analysisQueued = false;
        Json exportResult;
        GraphExportResult graphExport;
        std::optional<Project> analysisDraft;
        std::uint64_t analysisRevision = 0;
        Ref<CommandRegistry> commands;
        std::optional<Json> preview;
        std::vector<ImportPreviewItem<Json>> sourcePreviews;
        std::vector<int> selectedSources;
        std::vector<std::optional<double>> sourceTemperatures;
        int previewSourceIndex = -1;
        bool recursiveImport = false;
        std::vector<Path> previewInputs;
        bool previewRecursive = false;
        PreviewGeneration previewGeneration;
        std::uint64_t previewRevision = 0;
        std::optional<double> temperature;
        char decimal = '.';
        Json importOptions = Json::object();
        sst::ui::plot::PlotViewState view;
        std::map<pl::Id, sst::ui::plot::PlotViewState> graphViews;
        std::vector<pl::Id> exportGraphs;
        double exportDpi = 300;
        std::string exportFormat = "png";
    private:
        Ref<DocumentJob> pending;
        std::uint64_t pendingRevision = 0;
        std::uint64_t pendingGeneration = 0;
        std::uint64_t pendingProject = 0;
        std::uint64_t pendingSelectionRevision = 0;
        bool saveBeforeClose = false;
        void ResetProjectUI();
    };
}
