#pragma once
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Undo/UndoStack.hpp"
#include <sst/plotlab/Extensions.hpp>
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Path.hpp"

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
        void PollDataOperation();
        void New();
        void Open(const std::filesystem::path &);
        void Open(Ref<pl::Project>, const std::filesystem::path &);
        void Save(const std::filesystem::path &);
        bool Dirty() const
        {
            return !undo->IsClean();
        }
        pl::Id Create(pl::Id parent, int type, std::string name);
        void Rename(pl::Id, std::string);
        std::vector<pl::Id> RemovalIds(pl::Id) const;
        void Delete(pl::Id);
        void ResizeRows(pl::Id sheet, std::optional<std::size_t> eraseRow = {});
        pl::Id AddColumn(pl::Id sheet, bool text = false);
        pl::Id AddGraph(pl::Id folder, pl::Id sheet, pl::Id x, pl::Id y, pl::Id error = 0);
        void AddSeries(pl::Id graph, pl::Id sheet, pl::Id x, pl::Id y, pl::Id error = 0);
        void SelectRow(pl::Id sheet, std::size_t row, pl::Id sourceView, bool toggle = false, pl::Id series = 0);
        Ref<pl::Project> project;
        Ref<CommandRegistry> commands;
        Ref<UndoStack> undo;
        std::filesystem::path path;

      private:
        Ref<EventBus> bus;
        pl::Id correlation = 0;
        void Replace(Ref<pl::Project>);
        Ref<JobSystem> dataJobs;
        bool ownsDataJobs = false;
        Ref<IJob> dataWork;
        JobId dataJob = 0;
        DataResult dataResult;
    };
    void InstallWorksheetPython(Document* active = nullptr, const std::filesystem::path& pythonRoot = {});
} // namespace SST::Prototypes::PlotLab
