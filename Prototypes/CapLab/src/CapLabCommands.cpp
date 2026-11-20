#include "Domain/CapLabBundleImport.hpp"
#include "Domain/CapLabSourceOperations.hpp"
#include "CapLabCommands.hpp"
#include "Domain/CapLabIS.hpp"
#include "Domain/CapLabProjectTree.hpp"
namespace SST::Prototypes::CapLab {
namespace {
class RequestCommand final : public ICommand {
public:
    RequestCommand(Ref<EventBus> bus, Operation operation) : bus(std::move(bus)), operation(operation) {}
    Result<void> Execute(CommandContext &context) override {
        DocumentRequested event; event.operation = operation;
        if (const auto *path = context.TryGet<Path>("path")) event.target = *path;
        if (const auto *paths = context.TryGet<std::vector<Path>>("paths")) event.sources = *paths;
        if (const auto *recursive = context.TryGet<bool>("recursive")) event.recursive = *recursive;
        bus->Queue(event); return {};
    }
    std::string Description() const override { return "CapLab document operation"; }
private:
    Ref<EventBus> bus;
    Operation operation;
};
class WorkspaceCommand final : public ICommand {
public:
    explicit WorkspaceCommand(CapLabWorkflow &state) : state(state) {}
    Result<void> Execute(CommandContext &context) override {
        const auto *request=context.TryGet<Json>("request");
        if (!request) return StructuredError{ErrorCategory::Validation,Severity::Error,"Missing workspace request","Expected JSON request.","Supply operation.","CapLab","caplab.workspace"};
        return state.WorkspaceRequest(*request);
    }
    std::string Description() const override { return "CapLab workspace"; }
private:
    CapLabWorkflow &state;
};
class CancelCommand final : public ICommand {
public:
    explicit CancelCommand(CapLabWorkflow &state) : state(state) {}
    Result<void> Execute(CommandContext &) override {
        if (state.activeJob && !state.jobs->RequestCancel(state.activeJob))
            return StructuredError{ErrorCategory::Validation,Severity::Error,"Nothing to cancel","The job has already stopped.","Review or discard its draft.","CapLab","caplab.not_running"};
        state.analysisDraft.reset(); state.sourceDraft.reset(); state.sourceDiff=nullptr; return {};
    }
    std::string Description() const override {return "Cancel CapLab operation/discard draft";}
private:
    CapLabWorkflow &state;
};
class TreeCommand final : public ICommand {
public:
    TreeCommand(CapLabWorkflow &state, bool selection) : state(state), selection(selection) {}
    Result<void> Execute(CommandContext &context) override {
        try {
            const auto *edit=context.TryGet<Json>("edit");
            if (!edit) throw std::runtime_error("Missing tree request.");
            if (selection) {state.Select(*edit); return {};}
            if (state.closeRequested) throw std::runtime_error("Resolve pending close first.");
            before=state.Document().project;
            auto draft=state.EditSnapshot(); EditProjectTree(draft,*edit); after=std::move(draft);
            state.Document().project=after; state.Changed(); return {};
        } catch (const std::exception &e) {
            state.status=e.what(); return StructuredError{ErrorCategory::Validation,Severity::Error,"Project tree operation failed",e.what(),"Check stable IDs and metadata.","CapLab",selection ? "caplab.selection" : "caplab.tree"};
        }
    }
    Result<void> Undo(CommandContext &) override {state.Document().project=before; state.Changed(); return {};}
    Result<void> Redo(CommandContext &) override {state.Document().project=after; state.Changed(); return {};}
    bool IsUndoable() const noexcept override {return !selection;}
    std::string Description() const override {return selection ? "Select CapLab object" : "Edit CapLab project tree";}
private:
    CapLabWorkflow &state; bool selection; Project before,after;
};
class AnalysisChange final : public ICommand {
public:
    AnalysisChange(CapLabWorkflow &state, bool area) : state(state), area(area) {}
    Result<void> Execute(CommandContext &context) override {
        try {
            before = state.Document().project;
            if (area) {
                auto draft = state.EditSnapshot();
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
                if (!state.analysisDraft || state.analysisRevision != state.Document().revision) throw std::runtime_error("Stale analysis discarded.");
                after = *state.analysisDraft; Validate(after);
            }
            state.Document().project = after; state.Changed(area); state.analysisDraft.reset(); return {};
        } catch (const std::exception &e) {
            state.status=e.what(); return StructuredError{ErrorCategory::Validation,Severity::Error,"Analysis failed",e.what(),"Check analysis settings.","CapLab","caplab.analysis"};
        }
    }
    Result<void> Undo(CommandContext &) override { state.Document().project=before; state.Changed(area); return {}; }
    Result<void> Redo(CommandContext &) override { state.Document().project=after; state.Changed(area); return {}; }
    bool IsUndoable() const noexcept override { return true; }
    std::string Description() const override { return area ? "Change sample area" : "Accept analysis"; }
private:
    CapLabWorkflow &state; bool area; Project before, after;
};
class DefectChange final : public ICommand {
public:
    DefectChange(CapLabWorkflow &state, bool circuit=false, bool measurement=false) : state(state), circuit(circuit), measurement(measurement) {}
    Result<void> Execute(CommandContext &context) override {
        try {
            const auto *edit=context.TryGet<Json>("edit");
            if (!edit) throw std::runtime_error("Missing defect edit.");
            before=state.Document().project;
            auto draft=state.EditSnapshot();
            if (measurement) DeleteMeasurement(draft,edit->at("measurement_id").get<pl::Id>()); else if (circuit) EditCircuit(draft,*edit); else EditDefect(draft,*edit); RefreshAnalyses(draft); after=std::move(draft);
            state.Document().project=after; state.Changed(); return {};
        } catch (const std::exception &e) {
            state.status=e.what(); return StructuredError{ErrorCategory::Validation,Severity::Error,measurement ? "Measurement removal failed" : circuit ? "Circuit edit failed" : "Defect edit failed",e.what(),"Check input and sample.","CapLab",measurement ? "caplab.measurement" : circuit ? "caplab.circuit" : "caplab.defect"};
        }
    }
    Result<void> Undo(CommandContext &) override {state.Document().project=before; state.Changed(); return {};}
    Result<void> Redo(CommandContext &) override {state.Document().project=after; state.Changed(); return {};}
    bool IsUndoable() const noexcept override {return true;}
    std::string Description() const override {return measurement ? "Delete measurement" : circuit ? "Edit circuit" : "Edit defect";}
private:
    CapLabWorkflow &state; bool circuit,measurement; Project before,after;
};
class ImportCommand final : public ICommand {
public:
    explicit ImportCommand(CapLabWorkflow &state) : state(state) {}
    Result<void> Execute(CommandContext &context) override {
        try {
            if (state.previewRevision != state.Document().revision)
                throw std::runtime_error("Preview is stale; import the file again.");
            before=state.Document().project;
            auto draft=state.EditSnapshot();
            const auto *requestedTarget=context.TryGet<pl::Id>("target_sample_id");
            auto target=requestedTarget ? *requestedTarget : state.importTargetSample;
            if (const auto *name=context.TryGet<std::string>("target_sample_name"); name && !name->empty()) {
                if (target) throw std::runtime_error("Choose a target sample ID or a new sample name, not both.");
                EditProjectTree(draft,{{"action","create_sample"},{"name",*name}}); target=draft.samples.back().id;
                const auto options=Json::parse(state.importOptions.empty() ? "{}" : state.importOptions);
                if (options.contains("area_m2")) EditProjectTree(draft,{{"action","sample_metadata"},{"sample_id",target},{"area_m2",options.at("area_m2")}});
            }
            if (target) FindSample(draft,target);
            auto append = [&](const Json &preview, std::optional<double> temperature) {
                ConfirmMetadata(preview,state.metadataConfirmed);
                std::set<pl::Id> original;
                for (const auto &s:draft.samples) original.insert(s.id);
                if (preview.contains("samples")) { BundleChange change(draft,preview); }
                else { ImportChange change(draft,preview,temperature,state.templates.find("caplab.cf",1)); }
                if (target) {
                    if (preview.contains("samples") && !preview.value("historical",Json::array()).empty())
                        throw std::runtime_error("Historical bundles retain their own samples; move measurements explicitly after import.");
                    std::vector<pl::Id> measurements;
                    for (const auto &m:draft.measurements) if (!original.contains(m.at("sample_id").get<pl::Id>())) measurements.push_back(m.at("id"));
                    for (const auto &m:draft.sweeps) if (!original.contains(m.sample_id)) measurements.push_back(m.id);
                    if (!measurements.empty()) MoveMeasurements(draft,measurements,target);
                    std::erase_if(draft.samples,[&](const auto &s) {return !original.contains(s.id);});
                }
            };
            if (state.sourcePreviews.empty()) {
                if (!state.preview) throw std::runtime_error("No import preview.");
                append(*state.preview,state.temperature);
            } else {
                if (state.selectedSources.empty()) throw std::runtime_error("Select at least one readable source.");
                for (int index : state.selectedSources) {
                    const auto &item=state.sourcePreviews.at(index);
                    if (!item.value || item.error) throw std::runtime_error("Selected source has a preview error: "+item.source.path.Utf8());
                    append(*item.value,index==state.previewSourceIndex ? state.temperature :
                        index<static_cast<int>(state.sourceTemperatures.size()) ? state.sourceTemperatures[index] :
                        item.value->contains("samples") ? std::nullopt : Optional(item.value->at("temperature_K")));
                }
            }
            Validate(draft); after=std::move(draft);
            state.Document().project=after; state.Changed(); state.ClearPreview(); state.status="Imported."; state.failure.reset();
            return {};
        } catch (const std::exception &e) {
            state.status = e.what();
            state.failure=StructuredError{ErrorCategory::Validation, Severity::Error, "Import failed", e.what(),
                "Check the preview and metadata.", "CapLab", "caplab.import"};
            return *state.failure;
        }
    }
    Result<void> Undo(CommandContext &) override { state.Document().project=before; state.Changed(); return {}; }
    Result<void> Redo(CommandContext &) override { state.Document().project=after; state.Changed(); return {}; }
    std::string Description() const override { return "Import measurements"; }
    bool IsUndoable() const noexcept override { return true; }
private:
    CapLabWorkflow &state;
    Project before, after;
};
class SourceCommand final : public ICommand {
public:
    SourceCommand(CapLabWorkflow &state, bool profile) : state(state), profile(profile) {}
    Result<void> Execute(CommandContext &context) override {
        try {
            before=state.Document().project;
            if (profile) {
                const auto *request=context.TryGet<Json>("request");
                if (!request) throw std::runtime_error("Missing profile request.");
                auto draft=state.EditSnapshot(); const std::string name=request->at("name");
                if (request->at("action")=="save") {
                    const auto settings=Json::parse(state.importOptions.empty() ? "{}" : state.importOptions);
                    if (!settings.is_object()) throw std::runtime_error("Parser options must be a JSON object.");
                    pl::ImportOptions options; options.decimal=state.decimal; options.extra["options"]=settings.dump();
                    draft.importProfiles[name]=std::move(options);
                    (void)pl::importProfilesJson(draft.importProfiles);
                } else if (request->at("action")=="load") {
                    const auto &options=draft.importProfiles.at(name);
                    state.importOptions=options.extra.at("options"); state.decimal=options.decimal;
                    state.ClearPreview(); return {};
                } else if (request->at("action")=="delete") {
                    if (!draft.importProfiles.erase(name)) throw std::runtime_error("Unknown format profile.");
                } else throw std::runtime_error("Unknown profile action.");
                Validate(draft);
                after=std::move(draft);
            } else {
                if (!state.sourceDraft || state.sourceRevision!=state.Document().revision) throw std::runtime_error("Stale source diff; refresh preview.");
                if (!state.sourceRelink) ConfirmMetadata(state.sourceDiff.at("import_preview"),state.sourceMetadataConfirmed);
                after=*state.sourceDraft; Validate(after);
            }
            inputChange=!profile && !state.sourceRelink;
            changed=true; state.Document().project=after; state.Changed(inputChange);
            if (profile) ++state.profileRevision;
            if (!profile) {state.sourceDraft.reset(); state.sourceDiff=nullptr;}
            return {};
        } catch (const std::exception &e) {
            state.status=e.what(); return StructuredError{ErrorCategory::Validation,Severity::Error,"Source operation failed",e.what(),"Review source settings and diff.","CapLab",profile ? "caplab.profile" : "caplab.source"};
        }
    }
    Result<void> Undo(CommandContext &) override {state.Document().project=before; state.Changed(inputChange); if (profile) ++state.profileRevision; return {};}
    Result<void> Redo(CommandContext &) override {state.Document().project=after; state.Changed(inputChange); if (profile) ++state.profileRevision; return {};}
    bool IsUndoable() const noexcept override {return changed;}
    std::string Description() const override {return profile ? "Save import profile" : "Accept source change";}
private:
    CapLabWorkflow &state; bool profile,changed=false,inputChange=false; Project before,after;
};
}
void RegisterCommands(Ref<CommandRegistry> registry, Ref<EventBus> bus, CapLabWorkflow &state) {
    registry->SetUndoStack(state.undo); state.commands = registry;
    for (bool profile:{false,true}) {
        const auto registered=registry->Register({CommandID{profile ? "caplab.profile" : "caplab.accept_source"},profile ? "Format profiles" : "Accept source diff","CapLab","",{},CommandFlags::Undoable},
            [&state,profile](CommandContext &) {return CreateUnique<SourceCommand>(state,profile);});
        if (!registered) throw std::runtime_error(registered.Error().technicalDetails);
    }
    for (bool selection:{false,true}) {
        const auto result=registry->Register({CommandID{selection ? "caplab.select" : "caplab.tree"},selection ? "Select CapLab object" : "Edit project tree","CapLab","",{},selection ? CommandFlags::None : CommandFlags::Undoable},
            [&state,selection](CommandContext &) {return CreateUnique<TreeCommand>(state,selection);});
        if (!result) throw std::runtime_error(result.Error().technicalDetails);
    }
    const auto workspace=registry->Register({CommandID{"caplab.workspace"},"CapLab workspace","CapLab"},
        [&state](CommandContext &) { return CreateUnique<WorkspaceCommand>(state); });
    if (!workspace) throw std::runtime_error(workspace.Error().technicalDetails);
    auto cancel=registry->Register({CommandID{"caplab.cancel"},"Cancel / discard draft","CapLab"},
        [&state](CommandContext &) {return CreateUnique<CancelCommand>(state);});
    if (!cancel) throw std::runtime_error(cancel.Error().technicalDetails);
    auto measurement=registry->Register({CommandID{"caplab.measurement"},"Delete measurement","CapLab","",{},CommandFlags::Undoable},
        [&state](CommandContext &) {return CreateUnique<DefectChange>(state,false,true);});
    if (!measurement) throw std::runtime_error(measurement.Error().technicalDetails);
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
        std::tuple{"app.open", "Open project or import measurements", Operation::OpenOrImport},
        std::tuple{"caplab.new", "New CapLab project", Operation::New},
        std::tuple{"caplab.close", "Close CapLab project", Operation::Close},
        std::tuple{"caplab.save_as", "Save CapLab project as", Operation::SaveAs},
        std::tuple{"caplab.save", "Save CapLab project", Operation::Save},
        std::tuple{"caplab.analyze", "Analyze CapLab data", Operation::Analyze},
        std::tuple{"caplab.plot", "Compare stored CapLab data", Operation::Plot},
        std::tuple{"caplab.preview", "Import measurements", Operation::Preview},
        std::tuple{"caplab.preview_folder", "Import measurement folders", Operation::PreviewFolder},
        std::tuple{"caplab.reimport", "Preview re-import", Operation::Reimport},
        std::tuple{"caplab.relink", "Relink moved source", Operation::Relink},
        std::tuple{"caplab.open", "Open CapLab project", Operation::Open},
        std::tuple{"caplab.export", "Export CSV / XLSX / PNG / SVG / PDF", Operation::Export}}) {
        auto result = registry->Register({CommandID{id}, name, "CapLab"},
            [bus, operation](CommandContext &) { return CreateUnique<RequestCommand>(bus, operation); });
        if (!result) throw std::runtime_error(result.Error().technicalDetails);
    }
    auto result = registry->Register({CommandID{"caplab.import"}, "Confirm import", "CapLab", "", {}, CommandFlags::Undoable},
        [&state](CommandContext &) { return CreateUnique<ImportCommand>(state); });
    if (!result) throw std::runtime_error(result.Error().technicalDetails);
}
}
