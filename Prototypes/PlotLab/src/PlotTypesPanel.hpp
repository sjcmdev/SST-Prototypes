#pragma once
#include <sst/plotlab/PlotEditing.hpp>
namespace SST::Prototypes::PlotLab {
struct PlotTypesPanelResult { bool changed = false, apply = false, cancel = false, editing = false; };
// Host owns the draft; changed schedules its existing cancellable preview, Apply publishes one undo.
PlotTypesPanelResult DrawPlotTypesPanel(const sst::plotlab::Project&, sst::plotlab::MapPlotRequest&, const std::string& diagnostic = {});
}
