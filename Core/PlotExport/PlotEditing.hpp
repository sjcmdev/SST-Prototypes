#pragma once
#include "Core/Commands/CommandRegistry.hpp"
#include <sst/plotlab/PlotEditing.hpp>

struct ImFont;
namespace SST {
class EventBus;
void ConfigurePlotEditingNotifications(WeakRef<EventBus>);
ImFont* ResolvePlotPreviewFont(const std::string& family, const std::string& file);
// GUI and Python hosts dispatch the same typed edit through Core Commands/Undo.
Result<void> ExecutePlotEdit(CommandRegistry&, Ref<sst::plotlab::Project>, sst::plotlab::Id layer,
                             const sst::ui::plot::PlotEdit&, std::function<void()> changed = {});
}
