#include "Core/dspch.hpp"
#include "Core/PlotExport/PlotEditing.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/ScientificRuntime/PlotExport/PlotExport.hpp"
#include <imgui.h>
#include "Core/Platform/ProcessRunner.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"

namespace SST {
namespace { WeakRef<EventBus> plotEvents; }
void ConfigurePlotEditingNotifications(WeakRef<EventBus> events) { plotEvents = std::move(events); }
ImFont* ResolvePlotPreviewFont(const std::string& family, const std::string& file) {
    static std::map<std::string,ImFont*> fonts;
    auto& font=fonts[family+"\n"+file];auto* atlas=ImGui::GetIO().Fonts;
    if(!font || !atlas->Fonts.contains(font)) {
        std::string path;
        if(file.empty()) {
            const std::pair<const char*,const char*> bundled[]={{"DejaVu Sans","DejaVuSans.ttf"},{"DejaVu Serif","DejaVuSerif.ttf"},{"DejaVu Sans Mono","DejaVuSansMono.ttf"}};
            for(const auto& [name,filename]:bundled) if(family==name) {
                if(atlas->Locked && !ImGui::GetIO().BackendRendererName) {
                    // shortcut: headless backends use the preloaded host font; preload the publication font before NewFrame for font-metric checks.
                    return ImGui::GetFont();
                }
                if(const auto executable=Platform::CurrentExecutablePath()) {
                    const auto local=executable->parent_path().Native()/"install/app/assets/fonts"/filename;
                    if(std::filesystem::is_regular_file(local)) path=local.string();
                }
                const auto source=std::filesystem::path(SST_PLOT_EXPORT_PYTHON_ROOT).parent_path().parent_path().parent_path()/"Prototypes/PlotLab/install/app/assets/fonts"/filename;
                if(path.empty() && std::filesystem::is_regular_file(source)) path=source.string();
            }
        }
        if(path.empty()) path=ResolvePlotFont(family,file).first;
        if(atlas->Locked) throw std::runtime_error("Preload the graph font before NewFrame on this backend");
        ImFontConfig config;config.Flags|=ImFontFlags_NoLoadError;
        font=atlas->AddFontFromFileTTF(path.c_str(),0,&config);
        if(!font) throw std::runtime_error("Cannot load graph font");
    }
    return font;
}
namespace {
class PlotEditCommand final : public ICommand {
public:
    PlotEditCommand(Ref<sst::plotlab::Project> p, sst::plotlab::Id layer,
                    const sst::ui::plot::PlotEdit& edit, std::function<void()> notify)
        : project(std::move(p)), layer(layer), gesture(edit.gesture), changed(std::move(notify)) {
        operations.push_back(sst::plotlab::editPlot(*project, layer, edit, &status));
    }
    Result<void> Execute(CommandContext&) override { return Apply(false); }
    Result<void> Undo(CommandContext&) override { return Apply(true); }
    bool IsUndoable() const noexcept override { return !operations.front().empty(); }
    std::string Description() const override { return "Edit plot"; }
    std::size_t ByteSize() const noexcept override {
        std::size_t bytes = sizeof(*this);
        for (const auto& operation : operations) bytes += operation.storedBytes();
        return bytes;
    }
    bool CanMerge(const ICommand& other) const noexcept override {
        auto* next = dynamic_cast<const PlotEditCommand*>(&other);
        return gesture && next && project == next->project && layer == next->layer && gesture == next->gesture;
    }
    Result<void> Merge(std::unique_ptr<ICommand> other) override {
        auto& next = static_cast<PlotEditCommand&>(*other);
        for (auto& operation : next.operations) operations.push_back(std::move(operation));
        return {};
    }
private:
    Result<void> Apply(bool undo) {
        if (operations.front().empty()) {
            const StructuredError info{ErrorCategory::Validation, Severity::Info, "Plot unchanged", status,
                status, "PlotEditing", "plot.rescale.empty"};
            DS_LOG_INFO("{}: {}", info.code, info.technicalDetails);
            if (auto events = plotEvents.lock()) events->Queue(NotificationRequestedEvent{ToNotification(info)});
            return {};
        }
        try {
            if (undo) for (auto i = operations.rbegin(); i != operations.rend(); ++i) i->undo(*project);
            else for (auto& operation : operations) operation.apply(*project);
            if (changed) changed();
            return {};
        } catch (const std::exception& e) {
            return StructuredError{ErrorCategory::Validation, Severity::Error, "Plot edit failed", e.what(), {}, "PlotEditing", "plot.edit.invalid"};
        }
    }
    Ref<sst::plotlab::Project> project;
    sst::plotlab::Id layer;
    std::uint64_t gesture;
    std::vector<sst::plotlab::Operation> operations;
    std::string status;
    std::function<void()> changed;
};
}
Result<void> ExecutePlotEdit(CommandRegistry& commands, Ref<sst::plotlab::Project> project,
                             sst::plotlab::Id layer, const sst::ui::plot::PlotEdit& edit,
                             std::function<void()> changed) {
    const CommandID id{"plot.edit"};
    if (!commands.HasCommand(id)) {
        auto result = commands.Register({id, "Edit axes / legend / series", "Plot", "", {}, CommandFlags::Undoable},
            [](CommandContext& context) -> Unique<ICommand> {
                if (!context.TryGet<Ref<sst::plotlab::Project>>("plot.project") || !context.TryGet<sst::plotlab::Id>("plot.layer") || !context.TryGet<sst::ui::plot::PlotEdit>("plot.edit") || !context.TryGet<std::function<void()>>("plot.changed"))
                    throw std::invalid_argument("Missing typed plot edit context");
                return CreateUnique<PlotEditCommand>(*context.TryGet<Ref<sst::plotlab::Project>>("plot.project"),
                    *context.TryGet<sst::plotlab::Id>("plot.layer"), *context.TryGet<sst::ui::plot::PlotEdit>("plot.edit"),
                    *context.TryGet<std::function<void()>>("plot.changed"));
            });
        if (!result) return result.Error();
    }
    CommandContext context;
    context.Set("plot.project", std::move(project)); context.Set("plot.layer", layer);
    context.Set("plot.edit", edit); context.Set("plot.changed", std::move(changed));
    auto result = commands.Execute(id, std::move(context));
    if (!result) {
        DS_LOG_WARN("{}: {}", result.Error().code, result.Error().technicalDetails);
        if (auto events = plotEvents.lock()) events->Queue(NotificationRequestedEvent{ToNotification(result.Error())});
    }
    return result ? Result<void>{} : Result<void>{result.Error()};
}
}
