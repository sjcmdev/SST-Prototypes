#include "Domain/CapLabBundleImport.hpp"
#include "CapLabCommands.hpp"
namespace SST::Prototypes::CapLab {
namespace {
class RequestCommand final : public ICommand {
public:
    RequestCommand(Ref<EventBus> bus, Operation operation) : bus(std::move(bus)), operation(operation) {}
    Result<void> Execute(CommandContext &context) override {
        DocumentRequested event; event.operation = operation;
        if (const auto *path = context.TryGet<Path>("path")) event.target = *path;
        bus->Queue(event); return {};
    }
    std::string Description() const override { return "CapLab document operation"; }
private:
    Ref<EventBus> bus;
    Operation operation;
};
class ImportCommand final : public ICommand {
public:
    explicit ImportCommand(CapLabWorkflow &state) : state(state) {}
    Result<void> Execute(CommandContext &) override {
        try {
            if (!state.preview || state.previewRevision != state.document.revision)
                throw std::runtime_error("Preview is stale; import the file again.");
            if (state.preview->contains("samples")) bundle.emplace(state.document.project,*state.preview);
            else change.emplace(state.document.project, *state.preview, state.temperature,
                state.templates.find("caplab.cf", 1));
            state.Changed(); state.preview.reset(); state.status = "Imported.";
            return {};
        } catch (const std::exception &e) {
            state.status = e.what();
            return StructuredError{ErrorCategory::Validation, Severity::Error, "Import failed", e.what(),
                "Check the preview and metadata.", "CapLab", "caplab.import"};
        }
    }
    Result<void> Undo(CommandContext &) override { if (bundle) bundle->Undo(); else change->Undo(); state.Changed(); return {}; }
    Result<void> Redo(CommandContext &) override { if (bundle) bundle->Redo(); else change->Redo(); state.Changed(); return {}; }
    std::string Description() const override { return "Import measurements"; }
    bool IsUndoable() const noexcept override { return true; }
private:
    CapLabWorkflow &state;
    std::optional<ImportChange> change;
    std::optional<BundleChange> bundle;
};
}
void RegisterCommands(Ref<CommandRegistry> registry, Ref<EventBus> bus, CapLabWorkflow &state) {
    registry->SetUndoStack(state.undo);
    for (const auto &[id, name, operation] : {
        std::tuple{"caplab.preview", "Import measurements", Operation::Preview},
        std::tuple{"caplab.open", "Open CapLab project", Operation::Open},
        std::tuple{"caplab.export", "Export C-f PNG", Operation::Export}}) {
        auto result = registry->Register({CommandID{id}, name, "CapLab"},
            [bus, operation](CommandContext &) { return CreateUnique<RequestCommand>(bus, operation); });
        if (!result) throw std::runtime_error(result.Error().technicalDetails);
    }
    auto result = registry->Register({CommandID{"caplab.import"}, "Confirm import", "CapLab", "", {}, CommandFlags::Undoable},
        [&state](CommandContext &) { return CreateUnique<ImportCommand>(state); });
    if (!result) throw std::runtime_error(result.Error().technicalDetails);
}
}
