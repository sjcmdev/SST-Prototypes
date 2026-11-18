#pragma once
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Undo/UndoStack.hpp"
#include <sst/plotlab/Extensions.hpp>

namespace SST::Prototypes::PlotLab
{
    namespace pl = sst::plotlab;
    namespace plot = sst::ui::plot;
    double ParseNumericCell(const std::string &);
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
    class Document final : public pl::Observer
    {
      public:
        Document(Ref<CommandRegistry>, Ref<UndoStack>, Ref<EventBus>);
        ~Document() override;
        void changed(const pl::Event &) noexcept override;
        void Execute(pl::Operation);
        void New();
        void Open(const std::filesystem::path &);
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
    };
} // namespace SST::Prototypes::PlotLab
