#include "Domain/CapLabBundleImport.hpp"
#include "CapLabCommands.hpp"
#include "Domain/CapLabIS.hpp"
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
class AnalysisChange final : public ICommand {
public:
    AnalysisChange(CapLabWorkflow &state, bool area) : state(state), area(area) {}
    Result<void> Execute(CommandContext &context) override {
        try {
            before = state.document.project;
            if (area) {
                auto draft = Parse(Serialize(before));
                const auto *id = context.TryGet<pl::Id>("sample_id"); const auto *value = context.TryGet<double>("area_m2");
                if (!id || !value) throw std::runtime_error("Missing area settings.");
                Optional(Json(*value)); bool found = false;
                for (auto &s : draft.samples) if (s.id == *id) {
                    for (auto &m : draft.measurements) if (m.at("sample_id")==*id &&
                        (!m.at("metadata").contains("normalization_area_m2") || m.at("metadata").at("normalization_area_m2").is_null()))
                        m["metadata"]["normalization_area_m2"] = s.area_m2.value_or(*value);
                    s.area_m2 = *value; found = true;
                }
                if (!found) throw std::runtime_error("Missing sample.");
                RefreshAnalyses(draft); after = std::move(draft);
            } else {
                if (!state.analysisDraft || state.analysisRevision != state.document.revision) throw std::runtime_error("Stale analysis discarded.");
                after = *state.analysisDraft; Validate(after);
            }
            state.document.project = after; state.Changed(); state.analysisDraft.reset(); return {};
        } catch (const std::exception &e) {
            state.status=e.what(); return StructuredError{ErrorCategory::Validation,Severity::Error,"Analysis failed",e.what(),"Check analysis settings.","CapLab","caplab.analysis"};
        }
    }
    Result<void> Undo(CommandContext &) override { state.document.project=before; state.Changed(); return {}; }
    Result<void> Redo(CommandContext &) override { state.document.project=after; state.Changed(); return {}; }
    bool IsUndoable() const noexcept override { return true; }
    std::string Description() const override { return area ? "Change sample area" : "Accept analysis"; }
private:
    CapLabWorkflow &state; bool area; Project before, after;
};
class DefectChange final : public ICommand {
public:
    DefectChange(CapLabWorkflow &state, bool circuit=false) : state(state), circuit(circuit) {}
    Result<void> Execute(CommandContext &context) override {
        try {
            const auto *edit=context.TryGet<Json>("edit");
            if (!edit) throw std::runtime_error("Missing defect edit.");
            before=state.document.project;
            auto draft=Parse(Serialize(before));
            if (circuit) EditCircuit(draft,*edit); else EditDefect(draft,*edit); RefreshAnalyses(draft); after=std::move(draft);
            state.document.project=after; state.Changed(); return {};
        } catch (const std::exception &e) {
            state.status=e.what(); return StructuredError{ErrorCategory::Validation,Severity::Error,circuit ? "Circuit edit failed" : "Defect edit failed",e.what(),"Check input and sample.","CapLab",circuit ? "caplab.circuit" : "caplab.defect"};
        }
    }
    Result<void> Undo(CommandContext &) override {state.document.project=before; state.Changed(); return {};}
    Result<void> Redo(CommandContext &) override {state.document.project=after; state.Changed(); return {};}
    bool IsUndoable() const noexcept override {return true;}
    std::string Description() const override {return circuit ? "Edit circuit" : "Edit defect";}
private:
    CapLabWorkflow &state; bool circuit; Project before,after;
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
    registry->SetUndoStack(state.undo); state.commands = registry;
    auto circuit=registry->Register({CommandID{"caplab.circuit"},"Edit circuit","CapLab","",{},CommandFlags::Undoable},
        [&state](CommandContext &) {return CreateUnique<DefectChange>(state,true);});
    if (!circuit) throw std::runtime_error(circuit.Error().technicalDetails);
    auto defect=registry->Register({CommandID{"caplab.defect"},"Edit defect","CapLab","",{},CommandFlags::Undoable},
        [&state](CommandContext &) {return CreateUnique<DefectChange>(state);});
    if (!defect) throw std::runtime_error(defect.Error().technicalDetails);
    for (const auto &[id, area] : {std::pair{"caplab.accept_analysis",false}, {"caplab.area",true}}) {
        auto r = registry->Register({CommandID{id},area ? "Change sample area" : "Accept analysis","CapLab","",{},CommandFlags::Undoable},
            [&state,area](CommandContext &) { return CreateUnique<AnalysisChange>(state,area); });
        if (!r) throw std::runtime_error(r.Error().technicalDetails);
    }
    for (const auto &[id, name, operation] : {
        std::tuple{"caplab.analyze", "Analyze CapLab data", Operation::Analyze},
        std::tuple{"caplab.preview", "Import measurements", Operation::Preview},
        std::tuple{"caplab.open", "Open CapLab project", Operation::Open},
        std::tuple{"caplab.export", "Export active plot PNG", Operation::Export}}) {
        auto result = registry->Register({CommandID{id}, name, "CapLab"},
            [bus, operation](CommandContext &) { return CreateUnique<RequestCommand>(bus, operation); });
        if (!result) throw std::runtime_error(result.Error().technicalDetails);
    }
    auto result = registry->Register({CommandID{"caplab.import"}, "Confirm import", "CapLab", "", {}, CommandFlags::Undoable},
        [&state](CommandContext &) { return CreateUnique<ImportCommand>(state); });
    if (!result) throw std::runtime_error(result.Error().technicalDetails);
}
}
