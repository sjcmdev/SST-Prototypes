#include "Commands/Commands.hpp"
#include <tuple>
// Table of all typed commands + registration. Workflow::Dispatch reaches them by id "scapslab.<operation>" (the JSON adapter).
namespace SST::ScapsLab::Cmd {
namespace {
using All = std::tuple<LibraryOperation, BoundsFile, Run, RunBand, RunCv, RunCf, RunQe, RunDlcp, RunDlts, RunLaplace, RunCapture, RunOptical, SingleShot, SetRunPlan, CaptureLive, ResumeRun, RecalculateLive, LivePreviewResolution, LiveEditing, SelectBias, RecalculateStoredBias, RunBatch, RetryBatchMember, RecordBatch, PreviewBatch, QuickSweep, BatchParameters, SelectBatchMember, SetBatchSettings, Inspect, RunAll, Cancel, SetAutoRecalculate,
    EditDocument, EditLayer, LoadMaterial, SaveMaterial, SetProfile, LoadProfile, PreviewProfile, ImportDef, Undo, Redo, ListHistory, SelectRun, UpdateRun, ClearHistory,
    AddDevice, DuplicateDevice, RemoveDevice, UpdateDevice, MoveDevice, ListDevices, EditProject, SetActionSettings, SelectDevice,
    NewProject, LoadProject, LoadDocument, SaveProject, SaveDocument, ExportAc, ExportDlcp, ExportGr, ExportResults, ExportBatch, ExportProfile, ExportDef, SendToPlotLab,
    TutorialStart, TutorialStop, TutorialState, TutorialList>;
// Runs the typed command carried in the context ("args": a T from a typed caller) or parsed from the legacy request ("request": Json).
template<class T> class Typed final : public ICommand {
    ILabStore &store;
public:
    explicit Typed(ILabStore &s) : store(s) {}
    std::string Description() const override {return T::info.name;}
    Result<void> Execute(CommandContext &c) override {
        if(const auto *typed = c.TryGet<T>("args")) return typed->Execute(store);
        if(const auto *request = c.TryGet<Json>("request")) return T::FromJson(*request).Execute(store);
        if(T::info.menuDefault) return T{}.Execute(store);
        return StructuredError{ErrorCategory::Validation, Severity::Error, "Missing request", "Missing JSON request", "Supply a request", "ScapsLab", "invalid_request"};
    }
};
template<class T> Result<void> Add(CommandRegistry &registry, ILabStore &store) {
    const auto &i = T::info;
    CommandMeta meta{CommandID{std::string("scapslab.") + i.op}, i.name, "ScapsLab", i.description, {}, CommandFlags::AsyncCapable | (i.hidden ? CommandFlags::HiddenFromPalette : CommandFlags::None)};
    meta.menuPath = i.menu;meta.shortcut = i.shortcut;
    return registry.Register(std::move(meta), [&store](CommandContext &) -> Unique<ICommand> {return CreateUnique<Typed<T>>(store);});
}
}
const std::vector<Entry> &Table() {
    static const auto table = [] {
        std::vector<Entry> t;
        std::apply([&](auto... c) {(t.push_back({&decltype(c)::info}), ...);}, All{});
        return t;
    }();
    return table;
}
Result<void> RegisterAll(CommandRegistry &registry, ILabStore &store) {
    Result<void> out;
    [&]<class... T>(std::tuple<T...>) {((out ? (out = Add<T>(registry, store), 0) : 0), ...);}(All{});
    return out;
}
}
