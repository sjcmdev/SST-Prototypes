#pragma once
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Undo/UndoStack.hpp"
#include <sst/plotlab/Extensions.hpp>
#include "ImportWorkflow.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Path.hpp"
#include "PlotWorkflowState.hpp"
#include "ProjectFiles.hpp"

namespace SST::Prototypes::PlotLab
{
    namespace pl = sst::plotlab;
    namespace plot = sst::ui::plot;
    class OpenJob final : public IJob
    {
    public:
        explicit OpenJob(Path path) : path(std::move(path)) {}
        std::string GetName() const override { return "Open PlotLab file"; }
        std::string GetType() const override { return "plotlab.open"; }
        void Execute(JobContext &) override;
        Path path;
        Ref<pl::Project> project;
        std::optional<pl::Table> table;
        std::optional<ProjectSidecars> sidecars;
    };
    double ParseNumericCell(const std::string &);
    std::vector<double> ParseNumericList(const std::string&);
    struct ModelChanged : BusEvent
    {
        pl::Event change;
    };
    class EngineCommand final : public ICommand
    {
      public:
        EngineCommand(Ref<pl::Project> project, pl::Operation operation)
            : project(std::move(project)), operation(std::move(operation))
        {
        }
        Result<void> Execute(CommandContext &) override;
        Result<void> Undo(CommandContext &) override;
        std::string Description() const override
        {
            return operation.name();
        }
        bool IsUndoable() const noexcept override
        {
            return true;
        }

      private:
        Ref<pl::Project> project;
        pl::Operation operation;
    };
    enum class DataState { Idle, Running, Completed };
    enum class WorksheetError { Validation, Conflict };
    struct DataResult {
        DataState state = DataState::Idle;
        JobId job = 0;
        double elapsedMs = 0;
        std::vector<pl::Id> worksheets;
        std::optional<WorksheetError> error;
        std::string message;
    };
    enum class PythonAction { Script, Console, Formula, Preview, Completion, Parameters };
    enum class PythonErrorCode { None, Execution, Cancelled, Timeout, Conflict };
    struct PythonRequest {
        PythonAction action = PythonAction::Script;
        std::string source;
        pl::Id worksheet = 0, column = 0;
        pl::Formula formula;
        double timeoutSeconds = 5;
        std::map<std::string,double> parameters;
        std::uint64_t consoleSession=0;
    };
    struct PythonCompletion { std::string insert, signature, documentation; };
    struct ScriptParameter { std::string name; double value=0, minimum=0, maximum=0; bool slider=false; };
    struct PythonResult {
        bool running = true, ok = false;
        std::string output, error;
        int line = 0;
        int row = 0;
        PythonErrorCode errorCode=PythonErrorCode::None;
        std::vector<double> preview;
        std::vector<PythonCompletion> completions;
        std::vector<ScriptParameter> parameters;
    };
    class PythonWork;
    class PlotWorkflow;
    class Document final : public pl::Observer
    {
      public:
        Document(Ref<CommandRegistry>, Ref<UndoStack>, Ref<EventBus>, Ref<JobSystem> jobs = {});
        ~Document() override;
        void changed(const pl::Event &) noexcept override;
        void Execute(pl::Operation);
        void WorksheetCommand(pl::WorksheetEdit);
        DataResult WorksheetCommand(pl::DataRequest, bool wait = true);
        DataResult DataStatus();
        JobId ImportCommand(ImportRequest);
        JobId CommitImport(std::shared_ptr<const ImportResult>);
        std::shared_ptr<const ImportResult> PollImport();
        std::shared_ptr<const ImportResult> WaitImport();
        void CancelImport();
        void TrustImportRecipe(const pl::ImportRecipe&);
        bool ImportRecipeTrusted(const pl::ImportRecipe&) const;
        std::map<std::string,pl::ImportRecipe> importPresets;
        const ImportRecipes& ImportRecipeHistory() const { return importRecipes; }
        bool importPresetsDirty = false;
        ImportUiState importUi;
        void SetColumnValues(pl::Id column, pl::Formula);
        JobId SubmitPython(PythonRequest);
        const PythonResult& PollPython(JobId);
        PythonResult WaitPython(JobId);
        void CancelPython(JobId);
        void PollPythonJobs();
        bool PythonTrusted() const { return pythonTrusted; }
        void TrustPython();
        bool HasPython() const;
        PlotWorkflow& Workflow();
        JobContext* ActiveJobContext() const {return importContext;}
        std::map<pl::Id,plot::PlotViewState> graphViews;
        std::shared_ptr<const pl::GraphSnapshot> SnapshotGraph(pl::Id,std::optional<std::array<double,2>> sizeMm={}) const;
        WorkflowState workflowState;
        bool workflowDirty=false;
        pl::Id activeSheet = 0;
        std::vector<std::string> pythonHistory;
        std::map<pl::Id,std::string> scriptDrafts;
        std::map<pl::Id,std::map<std::string,double>> scriptParameterDrafts;
        void RecordPython(std::string);
        void GrowRows(pl::Id, std::size_t);
        void SetValues(pl::Id, pl::Values);
        void SetMasks(pl::Id, const std::vector<bool>&);
        pl::Id SaveScript(std::string name, std::string source, pl::Id id=0);
        void BeginPythonGroup(std::string);
        void EndPythonGroup(bool commit);
        void PollDataOperation();
        void New();
        void Open(const std::filesystem::path &);
        void Open(Ref<pl::Project>, const std::filesystem::path &, std::optional<ProjectSidecars> = {});
        void Save(const std::filesystem::path &);
        bool Dirty() const
        {
            return !undo->IsClean() || !scriptDrafts.empty() || !scriptParameterDrafts.empty() || importPresetsDirty || workflowDirty;
        }
        pl::Id Create(pl::Id parent, int type, std::string name);
        void Rename(pl::Id, std::string);
        std::vector<pl::Id> RemovalIds(pl::Id) const;
        void Delete(pl::Id);
        void ResizeRows(pl::Id sheet, std::optional<std::size_t> eraseRow = {});
        pl::Id AddColumn(pl::Id sheet, bool text = false);
        void ClearCells(pl::Id sheet, const std::vector<pl::Id>& columns, const std::vector<pl::Id>& rows = {});
        std::string NextName(std::size_t type, std::string_view prefix, pl::Id parent = 0) const;
        pl::Id AddGraph(pl::Id folder, pl::Id sheet, pl::Id x, pl::Id y, pl::Id error = 0);
        pl::Id AddSeries(pl::Id graph, pl::Id sheet, pl::Id x, pl::Id y, pl::Id error = 0);
        pl::Id PlotColumns(pl::Id sheet, const std::vector<pl::Id>& columns, plot::SeriesKind,
                           pl::Id graph = 0, pl::Id explicitX = 0, pl::Id error = 0);
        void SelectRow(pl::Id sheet, std::size_t row, pl::Id sourceView, bool toggle = false, pl::Id series = 0);
        Ref<pl::Project> project;
        Ref<CommandRegistry> commands;
        Ref<UndoStack> undo;
        std::filesystem::path path;

      private:
        friend class PythonWork;
        friend class ImportStartCommand;
        Ref<EventBus> bus;
        pl::Id correlation = 0;
        void Replace(Ref<pl::Project>);
        Ref<JobSystem> dataJobs;
        std::unique_ptr<PlotWorkflow> workflow;
        bool ownsDataJobs = false;
        Ref<IJob> dataWork;
        JobId dataJob = 0;
        DataResult dataResult;
        bool pythonTrusted = true;
        std::uint64_t pythonSession=0;
        std::map<JobId,Ref<PythonWork>> pythonWork;
        std::vector<pl::Operation>* recordedOperations = nullptr;
        std::vector<std::size_t> recordingGroups;
        void InstallPythonEvaluator();
        void ResetPython();
        void StartImportItem();
        JobId StartImport(ImportRequest);
        void StopImport();
        void WaitImportItem();
        std::shared_ptr<ImportRun> importRun;
        std::vector<pl::ImportRecipe> trustedImportRecipes;
        std::stop_token importStop;
        JobContext* importContext = nullptr;
        ImportRecipes importRecipes;
    };
    void InstallWorksheetPython(Document* active = nullptr, const std::filesystem::path& pythonRoot = {});
} // namespace SST::Prototypes::PlotLab
