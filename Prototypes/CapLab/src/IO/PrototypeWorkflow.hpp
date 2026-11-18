#pragma once
#include "Core/Layer.hpp"
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
    enum class Operation { Preview, Open, Save, Export, Analyze };
    struct DocumentRequested : BusEvent { Operation operation = Operation::Preview; Path target; };
    struct DocumentChanged : BusEvent { pl::Id project; std::uint64_t revision; };
    class DocumentJob final : public IJob
    {
    public:
        DocumentJob(Operation operation, std::string input, Path path = {}) : operation(operation), input(std::move(input)), path(std::move(path)) {}
        std::string GetName() const override { return "CapLab document"; }
        std::string GetType() const override { return "caplab.document"; }
        void Execute(JobContext &context) override;
        Operation operation;
        std::string input;
        Path path;
        Json preview;
        Json analysisOptions;
        std::shared_ptr<const pl::Importer> importer = std::make_shared<LabASImporter>();
        std::optional<Project> opened;
        char decimal = '.';
        std::string importOptions;
        bool saveCommitted = false;
        std::shared_ptr<const pl::GraphSnapshot> snapshot;
    };
    class CapLabWorkflow final : public Layer
    {
    public:
        Json pendingCandidate;
        explicit CapLabWorkflow(Ref<JobSystem> jobs, Ref<EventBus> bus = {}, Ref<UndoStack> undo = {})
            : Layer("CapLabWorkflow"), jobs(std::move(jobs)), bus(std::move(bus)), undo(std::move(undo))
        { templates.add(std::make_shared<CFTemplate>()); importers.add(labImporter); }
        void Start(Operation operation, Path target = {});
        void OnUpdate(float) override;
        void Changed();
        CapLabDocument document;
        Ref<JobSystem> jobs;
        Ref<EventBus> bus;
        Ref<UndoStack> undo;
        pl::PlotTemplateRegistry templates;
        std::shared_ptr<const pl::Importer> labImporter = std::make_shared<LabASImporter>();
        pl::ImporterRegistry importers;
        JobId activeJob = 0;
        std::string status;
        Json analysisOptions = Json::object();
        Json exportResult;
        std::optional<Project> analysisDraft;
        std::uint64_t analysisRevision = 0;
        Ref<CommandRegistry> commands;
        std::optional<Json> preview;
        std::uint64_t previewRevision = 0;
        std::optional<double> temperature;
        char decimal = '.';
        std::string importOptions;
        sst::ui::plot::PlotViewState view;
    private:
        Ref<DocumentJob> pending;
        std::uint64_t pendingRevision = 0;
    };
}
